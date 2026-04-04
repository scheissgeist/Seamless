# DS2 SotFS: Boss Kill Phantom Removal -- Research Report
**Date:** 2026-04-03
**Focus:** How the game removes phantoms after boss kills, and how to prevent it

---

## 1. HOW DS2 REMOVES PHANTOMS AFTER A BOSS KILL

DS2 SotFS does NOT have a single "boss defeated -> kick phantoms" function call. Instead, the boss kill triggers a **cascade of protobuf network messages** that instruct the server and all clients to tear down the session. The flow is:

### The Message Cascade (verified via ds3os + RTTI logging)

1. **Boss dies** -- game logic marks the boss entity as dead
2. **Game client sends `RequestNotifyKillEnemy`** (opcode `0x03F6`) -- logs the enemy kill with enemy_id and count
3. **Game client sends `RequestRemoveSign`** (opcode `0x0396`) -- removes the summoner's sign from the server
4. **Server sends `PushRequestRemoveSign`** (opcode `0x039D`) -- pushes sign removal to all aware players
5. **Game client sends `RequestNotifyLeaveGuestPlayer`** (opcode `0x03E9`) -- notifies server that phantom is leaving
6. **Game client sends `RequestNotifyLeaveSession`** (opcode `0x03EB`) -- notifies server that session is ending
7. **Game client sends `RequestNotifyDisconnectSession`** (opcode `0x03F9`) -- final disconnect notification
8. **Server sends `PushRequestRejectSign`** (opcode `0x039C`) -- if the phantom had an active sign being summoned

The key insight: **there is no "boss kill -> send home" RPC call**. The game client itself decides to send these messages. The "Duty Fulfilled" message appears, then the game generates LeaveGuestPlayer/LeaveSession/DisconnectSession messages through normal protobuf serialization.

### Phantom Timer (separate mechanism)

Independently, phantoms have a time limit tracked by `AllottedTime`:
- **White Phantom:** 66 minutes 40 seconds (4000 seconds)
- **Small White Sign (Shade):** 8 minutes 20 seconds (500 seconds)
- Timer is **shortened** by enemy kills during the session
- When timer expires: same cascade of Leave/Disconnect messages fires

### Where in Memory

**NetSessionManager** pointer chain (Bob Edition CT-verified):
- `NetSessionManager -> +0x18` = session pointer (active co-op session)
- `NetSessionManager -> +0x18 -> +0x17C` = `AllottedTime` (float, seconds remaining)
- `NetSessionManager -> +0x20` = player pointer (local player data in session context)
- `NetSessionManager -> +0x20 -> +0x1F4` = `PhantomType` (uint32: 0=Host)
- `NetSessionManager -> +0x20 -> +0x234` = `PlayerName` (wchar_t[32])

---

## 2. HOW TO PREVENT PHANTOM REMOVAL (Multiple Layers)

Your mod already implements the correct architecture. Here is the complete defense:

### Layer 1: Protobuf Interception (IMPLEMENTED -- session_hooks.cpp)

Hook `SerializeWithCachedSizesToArray` and `ParseFromArray`. These are the two functions every protobuf message in the game passes through.

**AOB for SerializeWithCachedSizesToArray** (verified by TLeonardUK in ds3os):
```
40 55 56 57
48 81 EC D0 00 00 00
48 C7 44 24 28 FE FF FF FF
48 89 9C 24 00 01 00 00
?? ?? ?? ?? ?? ?? ??
48 33 C4
48 89 84 24 C0 00 00 00
48 8B F2 48 8B D9 33 FF
89 7C 24 24 48 8B 01 FF 50 58
48 63 E8 41 83 C9 FF
44 8B C5 48 8B D6 48 8D 4C 24 50
```
76 bytes, 7 wildcards at offset 28-34.

**AOB for ParseFromArray** (verified by TLeonardUK in ds3os):
```
49 8B C0 4C 8B CA 4C 8B C1 48 8B D0 49 8B C9 E9 AC FE FF FF
```
20 bytes, no wildcards.

**Messages to block INCOMING (ParseHook return false):**
| RTTI Class Name Contains | Opcode | Purpose |
|---|---|---|
| `DisconnectSession` | 0x03F9 | Server telling us session is over |
| `LeaveGuestPlayer` | 0x03E9 | Server telling us phantom left |
| `LeaveSession` | 0x03EB | Server telling us to leave session |
| `BanishPlayer` | n/a | Server telling us to kick a player |
| `BreakInTarget` | 0x03FB | Invasion-related removal |
| `RemovePlayer` | n/a | Generic player removal |
| `RemoveSign` | 0x039D | Server removing a sign (triggers crash on host if processed mid-session) |
| `RejectSign` | 0x039C | Phantom rejection (carries "returned home" state, crashes host) |

**CRITICAL: Do NOT block outgoing messages.** Blocking outgoing serialize corrupts the server's protobuf stream (caused the offline mode bug 2026-04-02). Let all outgoing messages serialize normally. Only block incoming disconnect pushes.

### Layer 2: Phantom Timer Refresh (IMPLEMENTED -- player_sync.cpp)

Every 5 seconds, write `99999.0f` to `AllottedTime`:
```
NetSessionManager -> +0x18 -> +0x17C = 99999.0f
```
This prevents the timer-based phantom expiry.

### Layer 3: Permission Patches (IMPLEMENTED -- player_sync.cpp)

**TeamType patch:** Scan near player data for value `513` (WhitePhantom), write `0` (Host). This gives the phantom host-equivalent permissions.
```
TeamType values: 0=Host, 513=WhitePhantom, 515=Sunbro, 1799=DarkSpirit
Written by DarkSoulsII.exe+0xDF1719
```

**Phantom field:** Zero `NetSessionManager -> +0x20 -> +0x1F4` to make game treat local player as host for permission checks (bonfire, NPC, chest, fog wall).

**ChrNetworkPhantomId:** Zero `GameManagerImp -> +0xD0 -> +0xB0 -> +0x3C` to remove ghostly phantom rendering (solid appearance).

**Bonfire bits:** Set bits 4+5 at `GameManagerImp -> +0xD0 -> +0xB8 -> +0x4C8` to allow bonfire interaction.

---

## 3. DS2 PROTOBUF MESSAGE CATALOG (Session-Relevant)

From ds3os `DS2_Frpg2ReliableUdpMessageTypes.inc` and `DS2_Frpg2RequestMessage.proto`:

### Session Lifecycle (Logging category -- sent by client to server)
| Opcode | Message | Fields | Notes |
|---|---|---|---|
| 0x03EA | RequestNotifyJoinSession | field_1 (player_id), field_2, field_3, field_4 | Client joined a session |
| 0x03EB | RequestNotifyLeaveSession | field_1 (player_id), field_2, field_3, field_4 | Client leaving session |
| 0x03F9 | RequestNotifyDisconnectSession | field_1 | Final session disconnect |
| 0x03E8 | RequestNotifyJoinGuestPlayer | field_1-8 + field_9 (bytes) | Phantom joined host world |
| 0x03E9 | RequestNotifyLeaveGuestPlayer | field_1-4 | Phantom leaving host world |
| 0x03F1 | RequestNotifyDeath | (unknown fields) | Player died |
| 0x03ED | RequestNotifyKillPlayer | field_1-5 | Player killed another player |
| 0x03F6 | RequestNotifyKillEnemy | enemy_count[] (enemy_id, count) | Enemy/boss killed |

### Summon Signs (active session management)
| Opcode | Message | Direction | Notes |
|---|---|---|---|
| 0x0394 | RequestCreateSign | Client->Server | Place summon sign |
| 0x0397 | RequestGetSignList | Client->Server | Poll for available signs |
| 0x0398 | RequestSummonSign | Client->Server | Host picks up a sign |
| 0x039B | PushRequestSummonSign | Server->Client (push) | Tell phantom they're being summoned |
| 0x039C | PushRequestRejectSign | Server->Client (push) | **BLOCK THIS** -- Rejection/return home |
| 0x039D | PushRequestRemoveSign | Server->Client (push) | **BLOCK THIS** -- Sign removed, triggers crash |
| 0x0396 | RequestRemoveSign | Client->Server | Remove own sign |
| 0x039A | RequestRejectSign | Client->Server | Reject a summon |

### PlayerStatus Phantom Data (protobuf field)
```protobuf
message PlayerStatus_Phantom_leave_at {
    optional uint32 unknown_1 = 1;
    optional DateTime datetime = 2;  // When phantom should leave
}

message PlayerStatus {
    optional PlayerStatus_Phantom_leave_at phantom_leave_at = 8;
    // ... other fields
}
```
This `phantom_leave_at` field in the player status update may control server-side phantom expiry scheduling.

---

## 4. BOB EDITION CHEAT TABLE -- RELEVANT ENTRIES

From Bob Edition v4.09.5 (boblord14/Dark-Souls-2-SotFS-CT-Bob-Edition):

### Pointer Chains Used in This Project
| Entry | Chain | Offset | Type | Purpose |
|---|---|---|---|---|
| GameManagerImp | AOB scan | +0x38 | ptr | Player data base |
| NetSessionManager | AOB scan | +0x18 | ptr | Active session pointer |
| AllottedTime | NSM->+0x18->+0x17C | | float | Phantom time remaining |
| PlayerPointer | NSM->+0x20 | | ptr | Local player in session |
| PhantomType | NSM->+0x20->+0x1F4 | | uint32 | 0=Host, 513=White, etc. |
| PlayerName | NSM->+0x20->+0x234 | | wchar[32] | Character name |
| PhantomData | NSM->+0x1E8 | | ptr | Phantom slot array |
| ChrNetworkPhantomId | GMI->+0xD0->+0xB0->+0x3C | | byte | Phantom rendering (0=solid) |
| Bonfire flags | GMI->+0xD0->+0xB8->+0x4C8 | | uint32 | Bits 4+5 = bonfire access |
| TeamType | heap (scan for 513) | | uint16 | Written by exe+0xDF1719 |
| HP | GMI->+0x38->+0x3C | | int32 | Current health |
| MaxHP | GMI->+0x38->+0x40 | | int32 | Max health |
| Position XYZ | GMI->+0x38->+0x30/0x34/0x38 | | float | Player position |

### AOB Patterns (from ds3os, verified for SotFS x64)
| Pattern | Purpose | Source |
|---|---|---|
| `48 8B 05 ?? ?? ?? ?? 48 8B 58 38 48 85 DB 74 ?? F6` | GameManagerImp base pointer | Community CT |
| `48 8B 0D ?? ?? ?? ?? 48 85 C9 74 ?? 48 8B 49 18 E8` | NetSessionManager base pointer | Community CT |
| `48 8B 15 ?? ?? ?? ?? 45 32 C0 85 C9` | KatanaMainApp base pointer | Community CT |
| `48 89 5C 24 18 56 57 41 56 48 83 EC 30 45 8B F1 41` | ItemGive function | DS2S-META |

---

## 5. LUKEYUI'S DS2 SEAMLESS CO-OP MOD

From Patreon posts and GitHub (LukeYui/DarkSouls2SeamlessCoopRelease):

### Technical Approach
- Uses the **same protobuf interception technique** as ds3os (hook SerializeWithCachedSizesToArray and ParseFromArray)
- "An overwhelming amount of the work has involved reverse engineering and trawling through lots of x86 assembly"
- DS2 SotFS is described as "entirely different to other Dark Souls games" requiring RE "from scratch"
- Getting sessions to work **without the matchmaking server** was a major step

### Features Confirming Phantom Persistence
- "Persisting multiplayer sessions after death, boss kills, teleports"
- "No multiplayer boundaries, no multiplayer timers"
- Optional difficulty modifiers (increased enemy health/damage, friendly fire)
- PvP invasions

### Status (as of April 2026)
- Public release repo exists at github.com/LukeYui/DarkSouls2SeamlessCoopRelease
- Contains VERSION, BLOCKSYNC, and README.md (admin files only -- no source code)
- The mod is closed-source; technical implementation is not publicly documented

---

## 6. FUNCTION ADDRESSES OF INTEREST (for future Ghidra/IDA work)

These are NOT directly the boss-kill session teardown, but are related and documented:

| Address (exe-relative) | Function | Source |
|---|---|---|
| +0x1F4D80 | ComputeWeaponDurabilityDamage | DS2Fix64 |
| +0x348A00 | GetCurrentDurability | DS2Fix64 |
| +0x342560 | GetMaxDurability | DS2Fix64 |
| +0x151E90 | SetNewDurability | DS2Fix64 |
| +0xDF1719 | TeamType write instruction | CE live trace |

### What Needs Ghidra Work
The actual boss-defeat-to-disconnect flow is triggered **client-side in game logic**, not by a single function. To find the exact trigger:
1. Set a breakpoint on the outgoing `RequestNotifyLeaveGuestPlayer` serialize call
2. Walk the call stack backwards from SerializeWithCachedSizesToArray
3. The function that queues LeaveGuestPlayer after boss HP hits 0 is the target
4. That function likely checks a "boss defeated" flag in the area/session state

Alternatively, use the existing protobuf hook to log the exact message sequence during a boss kill and confirm which message the game sends first.

---

## 7. WHAT YOUR MOD ALREADY COVERS vs. GAPS

### Covered (working)
- [x] Protobuf serialize/parse hooks with correct AOB patterns
- [x] Incoming disconnect message blocking (ParseHook returns false)
- [x] AllottedTime refresh every 5 seconds
- [x] TeamType patch (scan for 513, write 0)
- [x] Phantom field zeroing (NSM+0x20+0x1F4)
- [x] ChrNetworkPhantomId zeroing (solid appearance)
- [x] Bonfire access bits
- [x] Outgoing messages NOT blocked (learned from 2026-04-02 bug)

### Gaps / Investigation Needed
- [ ] **Boss defeat game state hook** -- `game_state_hooks.cpp` has placeholder, no AOB pattern yet. Need Ghidra to find the boss-death handler function
- [ ] **`PlayerStatus_Phantom_leave_at`** -- the server-side player status contains a `phantom_leave_at` datetime field. If the server uses this to schedule phantom removal, it may need to be cleared or set to a far-future date in outgoing RequestUpdatePlayerStatus messages
- [ ] **Area transition handling** -- boss kill triggers area transition which may have separate logic beyond the protobuf messages
- [ ] **Fog wall removal** -- Enhanced Multiplayer mod removes fog walls but the technical method (binary patch vs memory write) is unknown

---

Sources:
- [ds3os by TLeonardUK](https://github.com/TLeonardUK/ds3os)
- [Bob Edition Cheat Table](https://github.com/boblord14/Dark-Souls-2-SotFS-CT-Bob-Edition)
- [DS2Fix64 by eur0pa](https://github.com/eur0pa/DS2Fix64)
- [DS2S-META by Nordgaren/pseudostripy](https://github.com/pseudostripy/DS2S-META)
- [LukeYui DS2 Seamless Coop](https://github.com/LukeYui/DarkSouls2SeamlessCoopRelease)
- [LukeYui Patreon - Development Update](https://www.patreon.com/posts/update-dark-2-co-129795630)
- [LukeYui Patreon - Progress Report](https://www.patreon.com/posts/update-dark-2-co-143065821)
- [Atvaark CE Guide](https://gist.github.com/Atvaark/f308e1d8e00e07106452)
- [FearLess Revolution DS2 SotFS](https://fearlessrevolution.com/viewtopic.php?t=268)
- [DS2 Co-op Wiki](http://darksouls2.wikidot.com/co-op)
- [Enhanced Multiplayer Mod](https://www.nexusmods.com/darksouls2/mods/1196)
- [Tim Leonard - RE DS3 Networking](https://timleonard.uk/2022/06/02/reverse-engineering-dark-souls-3-networking-part-2)
