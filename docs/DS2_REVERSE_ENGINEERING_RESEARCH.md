# DS2 SotFS x64 Reverse Engineering Research
## Compiled 2026-04-03

Research covering: boss phantom removal, remote player phantom rendering, and boss state flags.

---

## 1. BASE POINTER RESOLUTION

### GameManagerImp (BaseA) - The Master Pointer

**AOB Pattern (SotFS x64, confirmed in Bob Edition CT + DS2S-META):**
```
48 8B 05 ?? ?? ?? ?? 48 8B 58 38 48 85 DB 74 ?? F6
Mask: xxx????xxxxxxxx?x
```
- Offset from match: 3 (the RIP-relative offset starts at byte 3)
- Instruction size: 7 (for RIP resolution: `match_addr + 7 + *(int32_t*)(match_addr + 3)`)
- This resolves to a static global pointer to GameManagerImp

**Static offset (DS2Mod by GrandpaGameHacker, SotFS v1.0.1):**
```
DarkSoulsII.exe + 0x160B8D0 -> GameManagerImp pointer
```
Note: This is version-specific. The AOB is version-independent.

**Module sizes for version detection (DS2S-META):**
- SotFS V1.03: `0x1D76000`
- SotFS V1.02: `0x20B6000`

### NetSessionManager (BaseB)

**AOB Pattern:**
```
48 8B 0D ?? ?? ?? ?? 48 85 C9 74 ?? 48 8B 49 18 E8
Mask: xxx????xxxx?xxxxx
```
- Offset from match: 3, instruction size: 7

### KatanaMainApp

**AOB Pattern:**
```
48 8B 15 ?? ?? ?? ?? 45 32 C0 85 C9
Mask: xxx????xxxxx
```
- Offset from match: 3, instruction size: 7

---

## 2. GAMEMANAGERIMP STRUCTURE MAP

All offsets from the resolved GameManagerImp pointer (Bob Edition CT + DS2S-META):

| Offset | Field | Type | Notes |
|--------|-------|------|-------|
| +0x10 | Unknown (network-related) | ptr | Scanned for TeamType value |
| +0x18 | **CharacterManager** | ptr | Character/entity management |
| +0x20 | CameraManager | ptr | Camera control |
| +0x38 | PlayerData (GameManagerImp->PlayerData) | ptr | Primary player data pointer |
| +0x70 | **EventManager** | ptr | Event flag system (SotFS), 0x44 (Vanilla) |
| +0x90 | NetSvrBloodstainManager chain start | ptr | +0x28 -> +0x88 |
| +0xA8 | GameDataManager / AvailableItemBag chain | ptr | +0x10 -> +0x10 for AvailItemBag |
| +0xD0 | **PlayerCtrl** | ptr | Local player controller (SotFS), 0x74 (Vanilla) |
| +0x22E0 | ItemGiveWindowPointer | ptr | SotFS, 0xCC4 Vanilla |
| +0x24AC | GameState | int32 | SotFS, 0xDEC Vanilla |
| +0x24B1 | ForceQuit | byte | SotFS, 0xDF1 Vanilla |

### CharacterManager (GameManagerImp + 0x18)

This is the character/entity management system. From Bob Edition CT, this is a direct 8-byte pointer at offset 0x18. The structure contains:
- The loaded character array/list
- All active entities (player, NPCs, enemies, phantoms)

**How to iterate loaded characters (from DS2S-META LoadedEnemiesTable):**
The LoadedEnemiesTable pointer chain from BaseA (SotFS V1.03):
```
GameManagerImp -> MapManager (+0x38) -> +0x18 (LoadedEnemiesTable)
```
Each entry in the table:
- +0x28 = Character ID
- +0x30 = Next entry / inventory slot

### Key Sub-managers (Bob Edition CT GameManagerImp children):

| Name | Purpose |
|------|---------|
| AiManager | AI behavior |
| AppResourceManager | Resource loading |
| CameraManager | Camera control |
| **CharacterManager** | Entity list, all loaded characters |
| EnemyGeneratorManager | Enemy spawning |
| **EventManager** | Event flags, boss state |
| FaceGenManager | Face generation |
| GameDataManager | Save data, items |
| PadOwnershipManager | Controller input |
| **PlayerCtrl** | Local player entity |
| RumbleManager | Controller haptics |
| SaveLoadSystem | Save/load |

---

## 3. BOSS KILL PHANTOM REMOVAL

### How DS2 Removes Phantoms After Boss Kill

The boss kill phantom removal is NOT a single game function that can be NOP'd. It works through the network protocol layer:

1. When a boss dies, the host game sends `RequestNotifyKillEnemy` (opcode `0x03F6`) to the server
2. The server processes the boss kill and sends session-ending messages:
   - `PushRequestRemoveSign` (opcode `0x039D`) - removes summon signs
   - `PushRequestLeaveGuestPlayer` - tells phantoms to leave
   - `RequestNotifyLeaveSession` (opcode `0x03EB`) - session leave notification
3. The game client receives these and processes the phantom departure

### Preventing Phantom Removal (Seamless Co-op Approach)

**Protobuf Interception** (ds3os technique, verified working):

The game routes ALL network messages through protobuf serialize/parse functions. Hook these:

**SerializeWithCachedSizesToArray AOB (76 bytes, from ds3os):**
```
40 55 56 57
48 81 EC D0 00 00 00
48 C7 44 24 28 FE FF FF FF
48 89 9C 24 00 01 00 00
?? ?? ?? ?? ?? ?? ??    (7 wildcard bytes)
48 33 C4
48 89 84 24 C0 00 00 00
48 8B F2 48 8B D9 33 FF
89 7C 24 24 48 8B 01 FF 50 58
48 63 E8 41 83 C9 FF
44 8B C5 48 8B D6 48 8D 4C 24 50

Mask:
xxxx xxxxxxx xxxxxxxxx xxxxxxxx ??????? xxx xxxxxxxx xxxxxxxx xxxxxxxxxx xxxxxxx xxxxxxxxxx
```

**ParseFromArray AOB (20 bytes, no wildcards):**
```
49 8B C0 4C 8B CA 4C 8B C1 48 8B D0 49 8B C9
E9 AC FE FF FF
```

**Message types to BLOCK (incoming only, on parse):**
- Anything containing "DisconnectSession"
- Anything containing "LeaveGuestPlayer"
- Anything containing "LeaveSession"
- Anything containing "RemoveSign"
- Anything containing "RejectSign"
- Anything containing "BanishPlayer"
- Anything containing "RemovePlayer"

**RTTI identification**: Read vtable[-1] -> RTTICompleteObjectLocator -> pTypeDescriptor -> name string. The class name contains the message type (e.g. ".?AVRequestNotifyDisconnectSession@@").

### Network Opcodes (from ds3os reverse engineering)

| Opcode | Message | Purpose |
|--------|---------|---------|
| 0x03E8 | NotifyJoinGuestPlayer | Phantom joins |
| 0x03E9 | NotifyLeaveGuestPlayer | Phantom leaves |
| 0x03EA | NotifyJoinSession | Session joined |
| 0x03EB | NotifyLeaveSession | Session left |
| 0x03ED | NotifyKillPlayer | Player killed |
| 0x03F1 | NotifyDeath | Death notification |
| 0x03F6 | NotifyKillEnemy | Enemy/boss killed |
| 0x03F9 | NotifyDisconnectSession | Session disconnect |
| 0x0394 | CreateSign | Create summon sign |
| 0x0397 | GetSignList | Get available signs |
| 0x0398 | SummonSign | Summon from sign |
| 0x039B | PushSummonSign | Server push: summon |
| 0x039C | PushRejectSign | Server push: reject |
| 0x039D | PushRemoveSign | Server push: remove sign |

### ds3os Protobuf Messages (from DS2_Frpg2RequestMessage.proto)

**RequestNotifyKillEnemy** fields:
```protobuf
message RequestNotifyKillEnemy {
    repeated group Enemy_count {
        int64 enemy_id;
        int64 enemy_count;
    }
}
```

**RequestNotifyLeaveSession** fields:
```protobuf
message RequestNotifyLeaveSession {
    int64 field_1;  // player ID
    int64 field_2;  // session type
    int64 field_3;
    int64 field_4;
}
```

### Alternative: Direct Game Function Hook

If you wanted to NOP the boss-triggered departure directly in the game binary (not through protobuf), you would need to find the function that calls `RequestNotifyLeaveSession` in response to a boss death event. This function is NOT publicly documented. The address would be:

**Approach to find it:**
1. In Ghidra/IDA, find the instruction that writes to `0x03EB` (NotifyLeaveSession opcode)
2. Find cross-references to that write
3. The calling function that checks boss death state is your target
4. There will be a conditional jump (JE/JNE) before the leave-session call that can be NOP'd

**The instruction at `DarkSoulsII.exe+0xDF1719`** is confirmed (from CE verification 2026-04-02) to write TeamType values. The boss departure likely lives in the same region of the binary.

---

## 4. REMOTE PLAYER PHANTOM RENDERING

### ChrNetworkPhantomId - Local Player

**Pointer chain (Bob Edition CT, verified):**
```
GameManagerImp -> [+0xD0] (PlayerCtrl) -> [+0xB0] (PlayerTypeOffset) -> +0x3C (byte)
```
- DS2S-META calls this offset `NetworkPhantomID` at `PlayerTypeOffset + 0x3C` (SotFS), `+0x38` (V102)
- `PlayerTypeOffset` is at `PlayerCtrl + 0xB0` (SotFS), `+0x90` (Vanilla)

**Values (from CHR_NETWORK_PHANTOM_PARAM):**
- 0 = Normal (Host) - solid rendering
- Non-zero = Phantom appearance (ghostly/translucent)
- 18, 19 = No collision states (from DS2S-META CoreGameState)

### ChrNetworkPhantomId - Per-Entity (Entity Helper)

**Pointer chain for any entity (Bob Edition CT Entity Helper):**
```
target (entity base pointer) -> CharacterCtrl (+0x0) -> +0x3C (byte)
```
Where `target` is a CE-allocated symbol pointing to any entity in the loaded character list.

The Entity Helper also exposes:
```
target -> CharacterCtrl (+0x0) -> ChrParam -> ChrPhantomParam (+0x20)
```
ChrPhantomParam is a 32-byte struct controlling phantom rendering colors:
- +0x00..0x13: 5 sets of RGBA color channels (u8 each)
- +0x14: Ghost Alpha 1 (f32)
- +0x18: Ghost Alpha 2 (f32)
- +0x1C..0x1F: Unknown bytes

### CHR_NETWORK_PHANTOM_PARAM (full param definition)

96-byte struct (0x60), fields:
| Offset | Type | Field |
|--------|------|-------|
| 0x00 | s32 | Unk00 |
| 0x04 | s32 | Full Body SFX ID 1 |
| 0x08 | s32 | Full Body SFX ID 2 |
| 0x0C | s32 | Full Body SFX ID 3 |
| 0x10 | s32 | Full Body SFX ID 4 |
| 0x14 | u8 | Unk14 |
| 0x18 | s32 | Physics Texture ID |
| 0x20 | s32 | Dead Type ID |

### TeamType

**Current approach (CE-verified scan):**
- Heap-allocated uint16 written by `DarkSoulsII.exe+0xDF1719`
- Values: 0=Host, 513=WhitePhantom, 514=?, 515=Sunbro, 1799=DarkSpirit
- Found by scanning near PlayerData/NetPlayerManager for value 513

**Pointer chain (from your existing code, PlayerCtrl approach):**
```
GameManagerImp -> [+0xD0] -> +0xB0 -> +0x3D (TeamType byte)
```
Note: This is offset 0x3D per your addresses.h (Offsets::GameManager::TeamType = 0x3D)

### Changing ALL Players' Phantom Appearance

**Problem:** The pointer chain `GameManagerImp -> +0xD0 -> +0xB0 -> +0x3C` only reaches the LOCAL player's ChrNetworkPhantomId.

**To reach remote players, you need the CharacterManager entity list:**

```
GameManagerImp -> +0x18 (CharacterManager) -> ???
```

The CharacterManager structure is NOT fully documented publicly. What IS known:

1. **LoadedEnemiesTable** (DS2S-META): `GameManagerImp -> MapManager(+0x38) -> +0x18`
   - Each entry has +0x28 = Character ID
   - This is for ALL loaded entities (enemies, NPCs, players)

2. **Entity Helper approach** (Bob Edition CT):
   - The CT allocates a `target` symbol
   - Uses "Find What Accesses" to identify entities
   - Then reads `target -> +0x3C` for ChrNetworkPhantomId

3. **To iterate ALL characters and set their ChrNetworkPhantomId to 0:**
   - Walk the LoadedEnemiesTable linked list/array
   - For each entity, check if it's a player (ChrType check)
   - Write 0 to entity_base + 0x3C

**Entity types (Bob Edition Entity Helper):**
```
target -> CharacterCtrl -> Chr Type
target -> CharacterCtrl -> Chr ID
target -> CharacterCtrl -> ChrCommonParam -> Enemy Type
```

### Phantom Field (NetSession)

**Pointer chain (Bob Edition CT, verified):**
```
NetSessionManager -> [+0x20] (PlayerPointer) -> +0x1F4 (PhantomType, uint32)
```
- 0 = Host/normal
- Non-zero = phantom

---

## 5. BOSS STATE FLAGS / EVENT FLAGS

### Event Flag System Architecture

DS2's event flags are managed through the EventManager:
```
GameManagerImp -> [+0x70] (EventManager, SotFS) / [+0x44] (Vanilla)
```

The EventManager contains:
```
EventManager -> [+0x20] -> event flag data area
```

### Reading/Writing Event Flags (Bob Edition CT script)

**The native SetEventFlag function** (from Bob Edition Set.cea):
```asm
; Pointer chain to event flag manager:
; RCX = [[GameManagerImp]+0x70]+0x20
mov rcx, [GameManagerImp]
mov rcx, [rcx+70]
mov rcx, [rcx+20]

; EDX = flag ID (e.g., boss defeated flag number)
mov edx, [FlagID]

; R8D = value (0=unset, 1=set)
mov r8d, [FlagVal]

; Call the game's native SetEventFlag function
call rcx+4750b0    ; VERSION-SPECIFIC HARDCODED ADDRESS
```

**CRITICAL**: The `+4750b0` offset to the SetEventFlag function is version-specific and hardcoded. It would need to be found via AOB scan for portability.

### Event Flag ID Ranges

From EMEVD documentation:
- 8-digit flags by map number: `1100xxxx`, `1101xxxx`, etc.
- 8-digit flags starting with 51, 61, 71
- Flags outside defined ranges silently fail

### Boss Kill Detection via Event Flags

The game sets event flags when bosses die. These are checked via:
```
IfCharacterDeadAlive(condition, entity_id, DeathState.Dead)
```

Boss entity IDs and their corresponding event flags are defined per-map in the EMEVD files. They follow the pattern:
```
Map m10_02_00_00 (Forest of Fallen Giants) -> flags 11020xxx
Map m10_04_00_00 (Heide's Tower) -> flags 11040xxx
etc.
```

### Respawn All Enemies (Boss Reset)

The "Respawn All Enemies" script in Bob Edition injects at:
```
DarkSoulsII.exe + 0x1F2F95
```
It writes 0 to `[rcx+r10+0x210]` which is the per-enemy respawn flag byte.

### Relevant Param Tables

| Param | Purpose |
|-------|---------|
| BOSS_BATTLE_PARAM | Boss fight configuration |
| BOSS_ENEMY_GENERATE_PARAM | Boss enemy spawning |
| BOSS_PARAM | Boss properties |
| EVENT_FLAG_LIST_PARAM | Event flag definitions |
| EVENT_PARAM | Event scripting |
| NPC_EVENT_PARAM | NPC event triggers |
| ONLINE_EVENT_PARAM | Online event config |

### ds3os Player Status Proto (boss tracking)

From `DS2_Frpg2PlayerData.proto`:
```protobuf
message StatsInfo {
    uint32 sinner_points;
    repeated StatsInfo_Bonfire_levels bonfire_levels;
    repeated PhantomTypeCount phantom_type_count_6;
    repeated PhantomTypeCount phantom_type_count_7;
    repeated PhantomTypeCount phantom_type_count_8;
    repeated PhantomTypeCount phantom_type_count_9;
    repeated uint32 unlocked_bonfires;
}

message PlayerStatus {
    string name;
    uint32 soul_level;
    uint32 soul_memory;
    uint32 play_time_seconds;
    uint32 character_id;
    repeated uint32 played_areas;
    PlayerStatus_Phantom_leave_at phantom_leave_at;
}
```

---

## 6. PLAYERCTRL STRUCTURE (from DS2Mod + DS2S-META)

Complete PlayerCtrl structure with verified offsets:

| Offset (SotFS) | Offset (Vanilla) | Field | Type |
|----------------|-------------------|-------|------|
| 0x58 | - | Transform coordinates | Matrix4 |
| 0xA8 | - | pos2 | Vec3 |
| 0xB0 | 0x90 | PlayerTypeOffset (-> +0x3C ChrNetworkPhantomId) | ptr |
| 0xB8 | - | (bonfire access flags chain) | ptr |
| 0xE0 | - | m_pPlayerActionControl | ptr |
| 0xE8 | - | m_pPlayerOperator | ptr |
| 0xF8 | 0xB4 | PlayerPositionPtr (-> +0xF0 SotFS / +0xA8 Vanilla) | ptr |
| 0x100 | 0xB8 | PlayerDataMapPtr chain | ptr |
| 0x160 | - | m_PlayerSpeedMax | float |
| 0x164 | - | m_PlayerSpeedMin | float |
| 0x168 | 0xFC | HP (current) | StatInt |
| 0x16C | 0x100 | HP min | int |
| 0x170 | 0x104 | HP max | int |
| 0x174 | 0x108 | HP cap | int |
| 0x1A8 | - | m_Stamina | StatFloat |
| 0x1AC | 0x140 | SP | float |
| 0x1B0 | 0x144 | SP min | float |
| 0x1B4 | 0x148 | SP max | float |
| 0x378 | - | m_pChrAsmCtrl | ptr |
| 0x388 | - | m_pPlayerLockTargetCtrl | ptr |
| 0x3A0 | - | m_SpEffectOwner | SpEffectOwner |
| 0x3E0 | 0x308 | SpEffectCtrl | ptr |
| 0x480 | - | m_pPlayerActionCtrl | ptr |
| 0x490 | 0x378 | PlayerParam (stats) | ptr |

### PlayerParam / Stats Block (at PlayerCtrl + 0x490 SotFS)

| Offset | Field | Type |
|--------|-------|------|
| +0x08 | VGR (Vigor) | int16 |
| +0x0A | END (Endurance) | int16 |
| +0x0C | VIT (Vitality) | int16 |
| +0x0E | ATN (Attunement) | int16 |
| +0x10 | STR (Strength) | int16 |
| +0x12 | DEX (Dexterity) | int16 |
| +0x14 | INT (Intelligence) | int16 |
| +0x16 | FTH (Faith) | int16 |
| +0x18 | ADP (Adaptability) | int16 |
| +0xD0 | Soul Level | int32 |
| +0xEC | Souls | int32 |
| +0xF4 | Soul Memory | int32 |
| +0xFC | Soul Memory 2 | int32 |
| +0x1A4 | Total Deaths | int32 |
| +0x1AC | Hollow Level | byte |
| +0x1AD | Current Covenant | byte |
| +0x1D6 | Sinner Level | byte |
| +0x1D7 | Sinner Points | byte |

### Player Position (via PlayerPositionPtr)

```
PlayerCtrl -> [+0xF8] -> [+0xF0] -> position data (SotFS)
PlayerCtrl -> [+0xB4] -> [+0xA8] -> position data (Vanilla)
```

Position offsets within the final struct:
| Offset | Field |
|--------|-------|
| +0x20 | PosZ |
| +0x24 | PosY |
| +0x28 | PosX |
| +0x34 | AngZ |
| +0x38 | AngY |
| +0x3C | AngX |

### PlayerCtrl VTable (from DS2Mod DarkSoulsII.exe.h)

| VTable Offset | Function |
|---------------|----------|
| 0x90 | PlayerSpeedFunction |
| 0x108 | GetCharacterModelCtrl |
| 0x120 | GetChrAsmCtrl |
| 0x138 | GetPlayerCoordinates2 |
| 0x140 | SetPlayerCoordinates |
| 0x148 | GetPlayerPos |
| 0x150 | GetPlayerPos2 |
| 0x158 | MovementFunction |
| 0x1B0 | IsDead |
| 0x1B8 | GetCurrentHealth |
| 0x1C0 | GetHealthUndead |
| 0x248 | GetRotationZ |

---

## 7. AOB PATTERN SUMMARY (all from DS2S-META DS2REData.cs)

### Base Pointers
| Name | Pattern (SotFS) |
|------|----------------|
| BaseA (GameManagerImp) | `48 8B 05 ?? ?? ?? ?? 48 8B 58 38 48 85 DB 74 ?? F6` |
| BaseB (NetSessionMgr) | `48 8B 0D ?? ?? ?? ?? 48 85 C9 74 ?? 48 8B 49 18 E8` |

### Functions
| Name | Pattern (SotFS) |
|------|----------------|
| ItemGiveFunc | `48 89 5C 24 18 56 57 41 56 48 83 EC 30 45 8B F1 41` |
| GiveSoulsFunc | `48 83 EC 28 48 8b 01 48 85 C0 74 23 48 8b 80 b8 00 00 00` |
| RemoveSoulsFunc | `44 8b 81 ec 00 00 00 41 3b d0 73 05 44 2b c2 eb 03` |
| SetWarpTarget | `48 89 5C 24 08 48 89 74 24 20 57 48 83 EC 60 0F B7 FA` |
| Warp | `40 53 48 83 EC 60 8B 02 48 8B D9 89 01 8B 42 04` |
| ApplySpEffect (V1.03) | `48 89 6C 24 f8 48 8d 64 24 f8 48 8D 2d 33 A7 0A 00` |
| InfiniteSpells (V1.03) | `88 4D 20 49 8B CE` |
| InfiniteGoods (V1.03) | `66 29 73 20 48 8B D3` |
| DisablePoisonBuildup (V1.03) | `89 84 8B C4 01 00 00` |
| DisableSkirtPoison (V1.03) | `FF 50 08 C7 47 50 00` |

### DS2Fix64 (eur0pa) Signatures
| Name | Pattern | Address (v1.0.1) |
|------|---------|-------------------|
| ApplyDurabilityDamage | `48 89 5C 24 10 48 89 6C 24 18 56 57 41 56 48 83 EC 70 48 8B D9 48 8B 49 08 44 0F 29 44 24 40` | 0x7FF629164D80 |

---

## 8. KEY SOURCE REFERENCES

### GitHub Repositories
- **Bob Edition CT**: github.com/boblord14/Dark-Souls-2-SotFS-CT-Bob-Edition
  - `CheatTable/CheatEntries/.../Important Pointers/GameManagerImp/` — full pointer tree
  - `CheatTable/CheatEntries/.../Event Flags/` — event flag scripts
  - `CheatTable/CheatEntries/.../Scripts/Helpers/Enity Helper/` — entity iteration
- **DS2S-META**: github.com/pseudostripy/DS2S-META
  - `DS2S META/Utils/Offsets/DS2REData.cs` — ALL AOB patterns and offset chains
  - `DS2S META/Resources/Paramdex_DS2S_09272022/Defs/` — param definitions
- **ds3os**: github.com/TLeonardUK/ds3os
  - `Protobuf/DarkSouls2/DS2_Frpg2RequestMessage.proto` — network protocol
  - `Protobuf/DarkSouls2/DS2_Frpg2PlayerData.proto` — player data
  - `Source/Injector/Hooks/DarkSouls2/` — protobuf hook implementation
- **DS2Mod**: github.com/samsonpianofingers/DS2Mod
  - `DS2.h` — PlayerCtrl structure, GameManagerOffset (0x160B8D0)
  - `Notes/DarkSoulsII.exe.h` — vtable layout
- **DS2Fix64**: github.com/eur0pa/DS2Fix64
  - `DS2Fix64/Core/Signatures.h` — verified AOB patterns

### Key Files in This Project
- `include/addresses.h` — current AOB patterns and offsets
- `src/hooks/session_hooks.cpp` — protobuf interception (working)
- `src/sync/player_sync.cpp` — memory reads + TeamType/phantom patching

---

## 9. REMAINING UNKNOWNS / TODO

### CharacterManager Entity Iteration
The exact structure of the CharacterManager entity list at `GameManagerImp + 0x18` is NOT publicly documented. Need to:
1. Open Ghidra, load DarkSoulsII.exe
2. Navigate to GameManagerImp vtable
3. Find cross-references to the +0x18 field
4. Identify if it's an array, linked list, or std::vector
5. Document the entry stride and fields per entry
6. Each entry should have ChrNetworkPhantomId at +0x3C within its CharacterCtrl

### Boss Defeated Event Flag IDs
Specific event flag numbers for each boss are in the EMEVD files (map-specific .emevd.dcx files). They follow the pattern:
```
Map 10_02 (FoFG): flags 11020xxx — Last Giant, Pursuer
Map 10_04 (Heide): flags 11040xxx — Dragonrider, Old Dragonslayer
etc.
```
Need to unpack the .emevd files with DarkScript3 or Soulstruct to get exact flag IDs.

### SetEventFlag Function AOB
The native SetEventFlag function is called at `EventManager_base + 0x4750B0` (version-specific). Need an AOB pattern for this function to make it version-independent.

### Direct Boss Death Hook
If blocking at the protobuf layer isn't sufficient (e.g., the game also removes phantoms locally before sending the network message), we'd need to hook the function that actually triggers the departure. Finding this requires:
1. Setting a breakpoint on the NotifyLeaveSession protobuf serialize
2. Walking up the call stack to find the game function that initiated it
3. Finding the boss-death check (likely `IfCharacterDead` on the boss entity)
4. NOP'ing the conditional jump after the check
