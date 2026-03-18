# DS2: Scholar of the First Sin -- Custom Server Technical Breakdown

Based on complete source code analysis of **ds3os** (github.com/TLeonardUK/ds3os).
ds3os has FULL DS2:SotFS support with reverse-engineered protobuf definitions and a working server.

---

## 1. HOW THE GAME FINDS ITS SERVER

### Hostname
The game connects to:
```
frpg2-steam64-ope-login.fromsoftware-game.net
```

This hostname is stored as a **wide string (UTF-16, big-endian byte-swapped)** in the game binary's memory. It's NOT encrypted or obfuscated in DS2 (unlike DS3 which uses TEA encryption).

### Port
DS2 SotFS uses port **50031** for its initial login connection.

From `ReplaceServerPortHook.cpp`:
```cpp
case GameType::DarkSouls2:
{
    redirect_port_number = 50031;
    break;
}
```

### RSA Public Key
The game also contains a hardcoded RSA public key used to encrypt the initial handshake. The key embedded in the DS2 binary:
```
-----BEGIN RSA PUBLIC KEY-----
MIIBCAKCAQEAxSeDuBTm3AytrIOGjDKpwJY+437i1F8leMBASVkknYdzM5HB4z8X
YTXDylr/N6XAhgr/LcFFZ68yQNQ4AquriMONB+TWUiX0xu84ixYH3AqRtIVqLQbQ
xKZsTfyCRC94n9EnvPeS+ueM495YhLIJQBf9T2aCeoHZBFDh2CghJQCdyd4dOT/E
9ZxPImwj1t2fZkkKo4smpGk7GcCask2SGsnk/P2jUJxsOyFlCojaW1IldPxn+lXH
dlgHSLjQvMlWiZ2SmOwvJqPWMv6XyUXYqsOdejRJJQjV7jeDzYG8trX+bSQxnTAw
ENjvjslEcjBmzOCiqFTA/9H1jMjReZpI/wIBAw==
-----END RSA PUBLIC KEY-----
```

---

## 2. HOW ds3os REDIRECTS THE GAME CONNECTION

ds3os uses **DLL injection + in-memory patching**. No hosts file. No DNS. No binary patching on disk.

### The Injection Mechanism
1. The **Loader** (C# GUI app) injects a DLL into the running DS2 process
2. The DLL (`Entry.cpp`) creates a new thread via `DllMain -> DLL_PROCESS_ATTACH -> CreateThread(main)`
3. The injector installs three hooks:

### Hook 1: DS2_ReplaceServerAddressHook (Memory String Patch)
**File:** `Source/Injector/Hooks/DarkSouls2/DS2_ReplaceServerAddressHook.cpp`

This is simpler than DS3. The hostname and RSA key are NOT encrypted in DS2's memory. The hook:

1. **Patches the hostname**: Scans the process memory for the wide string `frpg2-steam64-ope-login.fromsoftware-game.net`, then overwrites it in-place with the custom server's hostname. The bytes are written as big-endian UTF-16 (byte-swapped wchar_t).

2. **Patches the RSA key**: Scans for the known FROM Software RSA public key string in memory and overwrites it with the custom server's public key. This is a straight `memcpy` -- no encryption needed.

Both patches spin-wait in a `while(true)` loop until the strings become writable (Steam DRM initially protects the memory pages). They check `VirtualQuery` for `PAGE_READWRITE` or `PAGE_EXECUTE_READWRITE` before patching.

### Hook 2: ReplaceServerPortHook (Winsock Hook)
**File:** `Source/Injector/Hooks/Shared/ReplaceServerPortHook.cpp`

Uses **Microsoft Detours** to hook `Winsock2 connect()`. When the game calls `connect()` to port 50031 (DS2's login port), the hook replaces it with the custom server's configured port.

```cpp
if (addr->sin_port == htons(50031))
{
    addr->sin_port = ntohs(Config.ServerPort);
}
```

### Hook 3: ChangeSaveGameFilenameHook (Optional)
Renames save files so custom server saves don't interfere with retail saves.

### Injector Config File (Injector.config)
```json
{
    "ServerName": "...",
    "ServerHostname": "192.168.1.100",
    "ServerPublicKey": "-----BEGIN RSA PUBLIC KEY-----\n...",
    "ServerPort": 50050,
    "ServerGameType": "DarkSouls2",
    "EnableSeperateSaveFiles": true
}
```

---

## 3. AUTHENTICATION PROTOCOL

The server runs three services in sequence. All communication is TCP with a custom binary protocol.

### Phase 1: Login Service (Port 50050 default)

The game connects to the login server and sends a single message:

```
RequestQueryLoginServerInfo {
    steam_id: "0011000100abcdef"
    app_version: 17039619
}
```

The server responds with the IP and port of the Auth service:

```
RequestQueryLoginServerInfoResponse {
    server_ip: "1.2.3.4"
    port: 50000
}
```

The login server message stream uses `Frpg2MessageStream` over TCP with RSA encryption for the initial key exchange.

### Phase 2: Auth Service (Port 50000 default)

A 4-step handshake:

**Step 1 - RequestHandshake**: Client sends an AES-CWC-128 key, RSA-encrypted.
```
RequestHandshake {
    aes_cwc_key: <16 bytes>
}
```
Server responds with 27 random bytes (11 random + 16 zeros). Cipher then switches to AES-CWC-128.

**Step 2 - GetServiceStatus**: Client sends version info.
```
GetServiceStatus {
    id: <some_id>
    steam_id: "0011000100abcdef"
    app_version: 17039619
}
```
Server validates app_version is between MIN_APP_VERSION and APP_VERSION (both 17039619 for DS2). Returns confirmation.

**Step 3 - KeyMaterial**: Client sends 8 bytes of key material. Server appends 8 random bytes, creating a 16-byte game session CWC key. Returns all 16 bytes.

**Step 4 - SteamTicket**: Client sends:
- Bytes 0-15: The GameCwcKey from step 3
- Bytes 16+: A Steam auth session ticket

The server validates the ticket via `SteamGameServer()->BeginAuthSession()` **if AUTH_ENABLED is true** (it is by default). Then responds with:

```c
struct Frpg2GameServerInfo {
    uint8_t  stack_data[...];       // zeroed
    char     game_server_ip[...];   // "1.2.3.4"
    uint64_t auth_token;            // random 8 bytes
    int      game_port;             // 50010
};
```

The auth_token and GameCwcKey are stored in the GameService for validating the upcoming game connection.

### Phase 3: Game Service (Port 50010 default, UDP)

Client connects via UDP. First 8 bytes of the connection are the auth_token from Phase 2. The server looks up the token to find the CWC key, then establishes a reliable UDP message stream encrypted with AES-CWC-128.

### Can We Skip Authentication?

**Yes.** The `AUTH_ENABLED` flag in `BuildConfig.h` is a compile-time constant. Set it to false and Steam ticket validation is bypassed. The rest of the handshake (key exchange, CWC cipher setup) is still required because the game client expects it. You can't skip the crypto -- the game will not send game messages without completing the auth flow.

---

## 4. SERVER PROTOCOL (DS2 Specific)

### Transport Layer Stack

```
TCP Connection
  -> Frpg2PacketStream (raw packets)
    -> Frpg2MessageStream (protobuf messages, RSA then CWC encrypted)

UDP Connection
  -> Frpg2UdpPacketStream (raw UDP packets)
    -> Frpg2ReliableUdpPacketStream (reliable, ordered, deduplicated)
      -> Frpg2ReliableUdpFragmentStream (fragmentation/defragmentation)
        -> Frpg2ReliableUdpMessageStream (protobuf messages, CWC encrypted)
```

Login and Auth use TCP. Game uses UDP with a custom reliable UDP layer.

### Protobuf Definitions

ds3os includes COMPLETE .proto files for DS2:

- `Protobuf/DarkSouls2/DS2_Frpg2RequestMessage.proto` -- All request/response messages
- `Protobuf/DarkSouls2/DS2_Frpg2PlayerData.proto` -- Player data structures
- `Protobuf/Shared/Shared_Frpg2RequestMessage.proto` -- Login/Auth messages

### DS2 Message Types (All Opcodes)

Each message has a numeric opcode in the reliable UDP header. Here is the complete opcode table:

**Boot:**
| Opcode | Message Type | Response |
|--------|-------------|----------|
| 0x0386 | RequestWaitForUserLogin | RequestWaitForUserLoginResponse |
| 0x03EC | RequestGetAnnounceMessageList | RequestGetAnnounceMessageListResponse |
| 0x038C | PlayerInfoUploadConfigPushMessage | (push, no response) |

**Player Data:**
| Opcode | Message Type | Response |
|--------|-------------|----------|
| 0x03B3 | RequestGetLoginPlayerCharacter | RequestGetLoginPlayerCharacterResponse |
| 0x03B6 | RequestUpdateLoginPlayerCharacter | RequestUpdateLoginPlayerCharacterResponse |
| 0x03B8 | RequestUpdatePlayerStatus | RequestUpdatePlayerStatusResponse |
| 0x03A8 | RequestUpdatePlayerCharacter | RequestUpdatePlayerCharacterResponse |
| 0x03A9 | RequestGetPlayerCharacter | RequestGetPlayerCharacterList |

**Summon Signs (CO-OP):**
| Opcode | Message Type | Response |
|--------|-------------|----------|
| 0x0394 | RequestCreateSign | RequestCreateSignResponse |
| 0x0395 | RequestUpdateSign | RequestUpdateSignResponse |
| 0x0396 | RequestRemoveSign | RequestRemoveSignResponse |
| 0x0397 | RequestGetSignList | RequestGetSignListResponse |
| 0x0398 | RequestSummonSign | RequestSummonSignResponse |
| 0x039A | RequestRejectSign | RequestRejectSignResponse |
| 0x03FA | RequestGetRightMatchingArea | RequestGetRightMatchingAreaResponse |
| 0x039B | PushRequestSummonSign | (push) |
| 0x039C | PushRequestRejectSign | (push) |
| 0x039D | PushRequestRemoveSign | (push) |

**Blood Messages:**
| Opcode | Message Type | Response |
|--------|-------------|----------|
| 0x03AB | RequestCreateBloodMessage | RequestCreateBloodMessageResponse |
| 0x03AC | RequestRemoveBloodMessage | RequestRemoveBloodMessageResponse |
| 0x03AD | RequestReentryBloodMessage | RequestReentryBloodMessageResponse |
| 0x03AE | RequestGetBloodMessageList | RequestGetBloodMessageListResponse |
| 0x03AF | RequestEvaluateBloodMessage | RequestEvaluateBloodMessageResponse |
| 0x03B0 | RequestGetBloodMessageEvaluation | RequestGetBloodMessageEvaluationResponse |
| 0x03FF | RequestGetAreaBloodMessageList | RequestGetBloodMessageListResponse |

**Bloodstains/Ghosts:**
| Opcode | Message Type | Response |
|--------|-------------|----------|
| 0x0391 | RequestCreateBloodstain | (one-way, no response) |
| 0x0392 | RequestGetBloodstainList | RequestGetBloodstainListResponse |
| 0x0400 | RequestGetAreaBloodstainList | RequestGetBloodstainListResponse |
| 0x0393 | RequestGetDeadingGhost | RequestGetDeadingGhostResponse |
| 0x03B1 | RequestCreateGhostData | RequestCreateGhostDataResponse |
| 0x03B2 | RequestGetGhostDataList | RequestGetGhostDataListResponse |

**Break-In (Invasions):**
| Opcode | Message Type | Response |
|--------|-------------|----------|
| 0x03D2 | RequestGetBreakInTargetList | RequestGetBreakInTargetListResponse |
| 0x03D3 | RequestBreakInTarget | RequestBreakInTargetResponse |
| 0x03D4 | RequestRejectBreakInTarget | RequestRejectBreakInTargetResponse |
| 0x03FB | PushRequestBreakInTarget | (push) |
| 0x03FC | PushRequestRejectBreakInTarget | (push) |
| 0x03FD | PushRequestAllowBreakInTarget | (push) |

**Visitors (Covenant auto-summon):**
| Opcode | Message Type | Response |
|--------|-------------|----------|
| 0x03D5 | RequestGetVisitorList | RequestGetVisitorListResponse |
| 0x03D6 | RequestVisit | RequestVisitResponse |
| 0x03D7 | RequestRejectVisit | RequestRejectVisitResponse |
| 0x03CF | PushRequestVisit | (push) |
| 0x03D0 | PushRequestRejectVisit | (push) |
| 0x03D1 | PushRequestRemoveVisitor | (push) |

**Mirror Knight:**
| Opcode | Message Type | Response |
|--------|-------------|----------|
| 0x039E | RequestCreateMirrorKnightSign | RequestCreateMirrorKnightSignResponse |
| 0x039F | RequestUpdateMirrorKnightSign | RequestUpdateMirrorKnightSignResponse |
| 0x03A0 | RequestRemoveMirrorKnightSign | RequestRemoveMirrorKnightSignResponse |
| 0x03A1 | RequestGetMirrorKnightSignList | RequestGetMirrorKnightSignListResponse |
| 0x03A2 | RequestSummonMirrorKnightSign | RequestSummonMirrorKnightSignResponse |
| 0x03A4 | RequestRejectMirrorKnightSign | RequestRejectMirrorKnightSignResponse |

**Quick Match (Undead Match / Arena):**
| Opcode | Message Type | Response |
|--------|-------------|----------|
| 0x03D9 | RequestRegisterQuickMatch | RequestRegisterQuickMatchResponse |
| 0x03DA | RequestUnregisterQuickMatch | RequestUnregisterQuickMatchResponse |
| 0x03DB | RequestUpdateQuickMatch | RequestUpdateQuickMatchResponse |
| 0x03DC | RequestSearchQuickMatch | RequestSearchQuickMatchResponse |
| 0x03DD | RequestJoinQuickMatch | RequestJoinQuickMatchResponse |
| 0x03DE | RequestRejectQuickMatch | RequestRejectQuickMatchResponse |

**Ranking:**
| Opcode | Message Type | Response |
|--------|-------------|----------|
| 0x03F3 | RequestRegisterPowerStoneData | RequestRegisterPowerStoneDataResponse |
| 0x03F4 | RequestGetPowerStoneRanking | RequestGetPowerStoneRankingResponse |
| 0x03F5 | RequestGetPowerStoneMyRanking | RequestGetPowerStoneMyRankingResponse |
| 0x03F8 | RequestGetPowerStoneRankingRecordCount | RequestGetPowerStoneRankingRecordCountResponse |

**Logging:**
| Opcode | Message Type | Response |
|--------|-------------|----------|
| 0x03E8 | RequestNotifyJoinGuestPlayer | RequestNotifyJoinGuestPlayerResponse |
| 0x03E9 | RequestNotifyLeaveGuestPlayer | RequestNotifyLeaveGuestPlayerResponse |
| 0x03EA | RequestNotifyJoinSession | RequestNotifyJoinSessionResponse |
| 0x03EB | RequestNotifyLeaveSession | RequestNotifyLeaveSessionResponse |
| 0x03ED | RequestNotifyKillPlayer | RequestNotifyKillPlayerResponse |
| 0x03EE | RequestNotifyRingBell | RequestNotifyRingBellResponse |
| 0x03F0 | RequestGetTotalDeathCount | RequestGetTotalDeathCountResponse |
| 0x03F1 | RequestNotifyDeath | RequestNotifyDeathResponse |
| 0x03F2 | RequestNotifyOfflineDeathCount | RequestNotifyOfflineDeathCountResponse |
| 0x03F6 | RequestNotifyKillEnemy | RequestNotifyKillEnemyResponse |
| 0x03F7 | RequestNotifyBuyItem | RequestNotifyBuyItemResponse |
| 0x03F9 | RequestNotifyDisconnectSession | RequestNotifyDisconnectSessionResponse |
| 0x03D8 | RequestNotifyMirrorKnight | RequestNotifyMirrorKnightResponse |

**Misc:**
| Opcode | Message Type | Response |
|--------|-------------|----------|
| 0x0320 | RequestSendMessageToPlayers | RequestSendMessageToPlayersResponse |
| 0x0389 | ManagementTextMessage | (push) |

**Push messages** all use opcode 0x0320 in the UDP header. The first protobuf field (PushMessageId) distinguishes the actual message type.

### All DS2 Game Managers Registered

```cpp
DS2_BootManager           // Login flow, announcements
DS2_PlayerDataManager     // Character data, status updates
DS2_GhostManager          // Ghost replay data
DS2_BloodMessageManager   // Player-written messages
DS2_BloodstainManager     // Death bloodstains
DS2_BreakInManager        // Invasions (Red Eye Orb, Blue Eye Orb)
DS2_LoggingManager        // Event logging (deaths, kills, sessions)
DS2_MiscManager           // SendMessageToPlayers, death count
DS2_VisitorManager        // Covenant auto-summons (Bell Keepers, Rat, Blue Sentinels)
DS2_RankingManager        // Power Stone / covenant rankings
DS2_MirrorKnightManager   // Looking Glass Knight summoning
DS2_SignManager           // Summon signs (co-op and PvP)
DS2_QuickMatchManager     // Arena / Undead Match
```

---

## 5. CO-OP SESSION LIFECYCLE (Sign Placement -> Summoning -> Session)

### Step 1: Sign Placement

Host places a white soapstone. Client sends:
```protobuf
RequestCreateSign {
    online_area_id: 10310000    // Area ID (e.g., Things Betwixt)
    matching_parameter { ... }  // Soul memory, calibration version, covenant, etc.
    player_struct: <bytes>      // Serialized player appearance/data
    cell_id: 16777216           // Sub-area cell
    sign_type: 1                // 1=White, 3=Small White, 4=Red, 6=Dragon
}
```

Server assigns a `sign_id` (incrementing counter starting at 1000), stores the sign in a `LiveCache` keyed by `(cell_id, online_area_id)`, and responds:
```protobuf
RequestCreateSignResponse {
    sign_id: 1000
}
```

### Step 2: Sign Discovery

Phantom (summoner) polls for signs:
```protobuf
RequestGetSignList {
    online_area_id: 10310000
    search_areas: [
        { cell_id: 16777216, local_signs: [...], max_signs: 10 }
    ]
    max_signs: 20
    matching_parameter { ... }
}
```

Server checks soul memory matching tiers (DS2 uses soul memory, not soul level). Returns:
```protobuf
RequestGetSignListResponse {
    sign_data: [
        {
            sign_info { player_id: 12345, sign_id: 1000 }
            online_area_id: 10310000
            matching_parameter { ... }
            player_struct: <bytes>
            player_steam_id: "0011000100abcdef"
            cell_id: 16777216
            sign_type: 1
        }
    ]
}
```

Players who have seen a sign are tracked in `Sign->AwarePlayerIds` so they can be notified on removal.

### Step 3: Summoning

Summoner activates the sign:
```protobuf
RequestSummonSign {
    online_area_id: 10310000
    sign_info { player_id: 12345, sign_id: 1000 }
    player_struct: <bytes>      // Summoner's data
    cell_id: 16777216
}
```

Server validates:
1. Sign still exists in cache
2. Sign isn't already being summoned (`BeingSummonedByPlayerId == 0`)

On success, server pushes to the sign owner:
```protobuf
PushRequestSummonSign {
    push_message_id: 0x039B
    player_id: <summoner's player_id>
    sign_id: 1000
    player_struct: <summoner's data>
    player_steam_id: <summoner's steam_id>
}
```

Sets `Sign->BeingSummonedByPlayerId = summoner's player_id`.

On failure, server pushes to summoner:
```protobuf
PushRequestRejectSign {
    push_message_id: 0x039C
    sign_info { player_id: 12345, sign_id: 1000 }
    error: SignHasDisappeared  // or SignAlreadyUsed
    player_steam_id: "..."
}
```

### Step 4: P2P Session Establishment

After the server relays the summon request, **the actual gameplay session is P2P via Steam Networking**. The server does NOT relay gameplay data. The server only facilitates matchmaking.

The game clients exchange Steam IDs through the server messages (`player_steam_id` fields), then establish a direct P2P connection using Steamworks networking APIs.

### Step 5: Session Notifications (Logging Only)

During the session, the game sends logging messages:
- `RequestNotifyJoinSession` -- player joined a session
- `RequestNotifyJoinGuestPlayer` -- a guest player appeared
- `RequestNotifyLeaveGuestPlayer` -- a guest player left
- `RequestNotifyLeaveSession` -- session ended
- `RequestNotifyDisconnectSession` -- session disconnected
- `RequestNotifyKillPlayer` -- PvP kill happened

**These are pure logging -- the server just sends empty responses.** The server has no control over the actual P2P session.

### Step 6: Sign Cleanup

When the sign owner disconnects or the sign is consumed:
- `RequestRemoveSign` sent by client
- Server removes from LiveCache
- Server sends `PushRequestRemoveSign` to all aware players

### Step 7: RequestUpdateSign (Heartbeat)

The game periodically sends `RequestUpdateSign` to keep signs alive. ds3os comments say "the game uses this as something of a heartbeat." The server just sends an empty response. Signs persist until explicitly removed.

---

## 6. MATCHING / SOUL MEMORY TIERS

DS2 uses **Soul Memory** for matchmaking (not Soul Level). The matching system uses tiers:

```
Tier  0: 0 - 9,999
Tier  1: 10,000 - 19,999
Tier  2: 20,000 - 29,999
...
Tier 23: 1,000,000 - 1,099,999
...
Tier 43: 45,000,000 - 999,999,999
```

Each sign type has different tier ranges:
- **White Soapstone**: Host -3/+1 tiers (or -6/+4 with password/Name-Engraved Ring)
- **Small White Soapstone**: Host -4/+2 tiers (or -7/+5)
- **Red Soapstone**: Host -5/+2 tiers
- **Dragon Eye**: Host -5/+5 tiers

**To disable matching for Seamless, set `DisableSoulMemoryMatching = true` in the config, or always return true from `CanMatchWith`.**

---

## 7. PHANTOM TYPE CONTROL

### What the Server Controls

The server does NOT directly control phantom types (host vs phantom). Here is what the server knows and manages:

1. **Sign Type** (`SignType` enum): White (1), Small White (3), Red (4), Dragon (6), MirrorKnight (99). This determines the sign color and base phantom behavior.

2. **Visitor Type** (`VisitorType` enum): BlueSentinels (0), BellKeepers (1), Rat (2). This is for covenant auto-summoning.

3. **BreakIn Type** (`BreakInType` enum): RedEyeOrb (0), BlueEyeOrb (2). For invasions.

4. **Player Status fields** the server tracks:
   - `sitting_at_bonfire` -- affects invadability
   - `human_effigy_burnt` -- blocks invasions
   - `covenant` -- determines visitor pool
   - `named_ring_god`, `guardians_seal`, `bell_keepers_seal`, `crest_of_the_rat` -- covenant items
   - `soul_memory`, `soul_level` -- matching

### What the Server Cannot Control

The host/phantom distinction is determined by the game client based on who initiated the summon. The server cannot tell the game "this player has host privileges." The host is always the player whose world the session takes place in.

The `player_struct` bytes contain serialized player appearance data that the clients exchange. The exact format is opaque to the server.

### Invadability State

The server derives invadability from player status:
```cpp
bool NewState = true;
if (sitting_at_bonfire || human_effigy_burnt || online_activity_area == 0)
{
    NewState = false;
}
```

---

## 8. MINIMUM VIABLE SERVER FOR CO-OP

### Absolute Minimum Message Handlers

To get two players connected for co-op, you need:

**Layer 1: Connection (mandatory)**
1. Login Service -- handle `RequestQueryLoginServerInfo` (return auth server IP/port)
2. Auth Service -- handle the 4-step handshake (RequestHandshake, GetServiceStatus, KeyMaterial, SteamTicket)
3. Game Service -- accept UDP connections, validate auth tokens

**Layer 2: Boot (mandatory)**
4. `RequestWaitForUserLogin` (0x0386) -- assign player_id, return steam_id mapping
5. `PlayerInfoUploadConfigPushMessage` (0x038C push) -- tell client what status data to upload
6. `RequestGetAnnounceMessageList` (0x03EC) -- return announcements (can be empty)

**Layer 3: Player Data (mandatory)**
7. `RequestUpdateLoginPlayerCharacter` (0x03B6) -- assign character_id
8. `RequestUpdatePlayerStatus` (0x03B8) -- receive player status updates
9. `RequestUpdatePlayerCharacter` (0x03A8) -- store character data
10. `RequestGetLoginPlayerCharacter` (0x03B3) -- return character data
11. `RequestGetPlayerCharacter` (0x03A9) -- return character data for other players

**Layer 4: Signs (mandatory for co-op)**
12. `RequestCreateSign` (0x0394) -- store sign, return sign_id
13. `RequestGetSignList` (0x0397) -- return matching signs
14. `RequestSummonSign` (0x0398) -- relay summon request via PushRequestSummonSign
15. `RequestRemoveSign` (0x0396) -- remove sign, notify aware players
16. `RequestUpdateSign` (0x0395) -- heartbeat, just send empty response
17. `RequestRejectSign` (0x039A) -- relay rejection

**Layer 5: Session Relay (mandatory for the actual P2P connection)**
18. `RequestSendMessageToPlayers` (0x0320) -- relay raw protobuf between players. **This is critical.** DS2 uses this for the P2P connection setup during invasions AND quick matches. For summon signs, the PushRequestSummonSign contains the steam_id needed for P2P.

**Layer 6: Logging (should handle, can be stubs)**
19. `RequestNotifyJoinSession` -- empty response
20. `RequestNotifyLeaveSession` -- empty response
21. `RequestNotifyJoinGuestPlayer` -- empty response
22. `RequestNotifyLeaveGuestPlayer` -- empty response
23. `RequestNotifyDisconnectSession` -- empty response
24. `RequestNotifyDeath` -- empty response
25. `RequestNotifyKillEnemy` -- empty response
26. `RequestNotifyKillPlayer` -- empty response
27. `RequestNotifyBuyItem` -- empty response
28. `RequestNotifyOfflineDeathCount` -- empty response
29. `RequestGetTotalDeathCount` -- return a number

**Layer 7: Optional but expected**
30. `RequestGetRightMatchingArea` (0x03FA) -- return area population hints (orange borders)
31. Blood messages, bloodstains, ghosts -- the game will request these

### Can We Skip Authentication?

No. The crypto handshake is built into the client's connection flow. You CAN skip Steam ticket validation (set `AUTH_ENABLED = false`) but you must still:
- Generate an RSA keypair
- Patch the game's RSA key to match yours
- Complete the CWC key exchange
- Complete the key material exchange

### Can We Skip Most Messages?

**Mostly yes.** Any message that has a response can have an empty response returned. The game will continue. However:
- Without `RequestWaitForUserLogin`, the game won't proceed past the initial load
- Without `PlayerInfoUploadConfigPushMessage`, the game won't send status updates
- Without sign handlers, there's no matchmaking
- Without `RequestSendMessageToPlayers`, arena/invasion P2P setup breaks

The logging messages (RequestNotify*) are fire-and-forget -- the game sends them but doesn't care about the response content. Just return empty responses.

---

## 9. KEY ARCHITECTURAL INSIGHTS FOR SEAMLESS CO-OP

### The Server is Only a Matchmaker

The server NEVER relays gameplay data. It only:
1. Authenticates players
2. Tracks player locations and status
3. Manages sign/invasion/visitor listings
4. Relays matchmaking push messages between players
5. Provides the Steam IDs needed for P2P connection

Actual gameplay (position sync, damage, enemy state) goes through **Steam P2P networking** directly between clients.

### Session Duration

**There is nothing on the server side that ends a co-op session.** The server:
- Does not track active sessions (only signs in a LiveCache)
- Does not send any "end session" message
- Only knows about sessions from client-sent logging messages (RequestNotifyJoinSession, RequestNotifyLeaveSession)

The session ends when:
1. A boss is killed (game logic)
2. The phantom dies (game logic)
3. The host dies (game logic)
4. The phantom uses Black Separation Crystal (game logic)
5. The P2P connection drops (network)

**To make sessions permanent (Seamless co-op), you would need to modify the game client**, not the server. The server already doesn't end sessions. The game client's own logic decides when to send the phantom home.

### RequestSendMessageToPlayers -- The Relay Function

This is the most powerful server function. It lets one client send arbitrary protobuf data to another client by player_id. DS2 uses it for:
- Invasion P2P setup (`PushRequestAllowBreakInTarget` contains `player_struct`)
- Quick match setup
- Potentially other things

The server just relays the bytes. The CVE-2022-24125 fix adds validation to prevent malicious data.

### Push Messages

All push messages (server -> client, unsolicited) use opcode 0x0320 in the UDP header. The first protobuf field is always a `PushMessageId` enum that distinguishes the message type:
- 0x039B = PushRequestSummonSign
- 0x039C = PushRequestRejectSign
- 0x039D = PushRequestRemoveSign
- 0x038C = PlayerInfoUploadConfigPushMessage
- etc.

---

## 10. FILE LOCATIONS IN ds3os

| Path | Purpose |
|------|---------|
| `Protobuf/DarkSouls2/DS2_Frpg2RequestMessage.proto` | **ALL DS2 protobuf message definitions** |
| `Protobuf/DarkSouls2/DS2_Frpg2PlayerData.proto` | DS2 player data structures |
| `Protobuf/Shared/Shared_Frpg2RequestMessage.proto` | Login/Auth message definitions |
| `Source/Server.DarkSouls2/Server/Streams/DS2_Frpg2ReliableUdpMessageTypes.inc` | Complete opcode table |
| `Source/Server.DarkSouls2/Server/GameService/GameManagers/Signs/DS2_SignManager.cpp` | Full sign/summon implementation |
| `Source/Server.DarkSouls2/Server/GameService/GameManagers/Boot/DS2_BootManager.cpp` | Login + announcements |
| `Source/Server.DarkSouls2/Server/GameService/GameManagers/PlayerData/DS2_PlayerDataManager.cpp` | Character/status management |
| `Source/Server.DarkSouls2/Server/GameService/GameManagers/BreakIn/DS2_BreakInManager.cpp` | Invasion system |
| `Source/Server.DarkSouls2/Server/GameService/GameManagers/Visitor/DS2_VisitorManager.cpp` | Covenant auto-summons |
| `Source/Server.DarkSouls2/Server/GameService/GameManagers/Misc/DS2_MiscManager.cpp` | SendMessageToPlayers relay |
| `Source/Server.DarkSouls2/Server/DS2_Game.cpp` | Game manager registration |
| `Source/Injector/Hooks/DarkSouls2/DS2_ReplaceServerAddressHook.cpp` | Memory patching (hostname + RSA key) |
| `Source/Injector/Hooks/Shared/ReplaceServerPortHook.cpp` | Winsock connect() hook |
| `Source/Server/Server/AuthService/AuthClient.cpp` | Full auth handshake state machine |
| `Source/Server/Server/LoginService/LoginClient.cpp` | Login server implementation |
| `Source/Server/Server/GameService/GameService.cpp` | Game service (UDP, auth token management) |
| `Source/Server/Server/GameService/GameClient.cpp` | Per-client message routing |
| `Source/Server/Config/BuildConfig.h` | AUTH_ENABLED flag, app versions, Steam AppID (335300) |
| `Source/Server/Server/Streams/README` | Transport layer documentation |

---

## 11. DS2 SotFS SPECIFIC CONSTANTS

- **Steam AppID**: 335300
- **App Version**: 17039619
- **Login Port (retail)**: 50031
- **Hostname (retail)**: `frpg2-steam64-ope-login.fromsoftware-game.net`
- **Game executable**: `DarkSoulsII.exe`
- **RSA Key**: Not encrypted/obfuscated in memory (unlike DS3)
- **Matching**: Soul Memory tiers (not Soul Level + Weapon Level like DS3)
