# Session Log - April 4, 2026

## What We Did This Session

### 1. Local Player Name Fix
- **Problem:** NSM+0x20+0x234 always returns HOST name. Joiners showed as "Gwister" (Sean's name).
- **Fix:** Switched to `GMImp -> [+0xA8] (GameDataManager) -> +0x114` as primary path.
- **Source:** Bob Edition CT + DS2S-META: `OFLD(ANYSOTFS, STRBASEA, 0xa8, 0x114)`
- **Result:** Each player's own character name sent in P2P handshake.

### 2. P2P Timeout Disabled During Seamless
- `PeerManager::CheckTimeouts()` now returns early when seamless is active.
- Players no longer disappear from HUD due to Hamachi UDP heartbeat timeouts.

### 3. Boss-Kill Phantom Return NOP (Ghidra analysis)
- **Problem:** Boss dies -> phantoms instantly sent home. No text, no effect. Just gone.
- **Ghidra call chain:**
  ```
  FUN_14044ef30 (event dispatch)
    -> CALL at exe+0x44ef7b (e8 30 2c d4 ff)
    -> FUN_140191bb0 (creates EventPhantomReturn objects)
    -> FUN_14018d660 (EventPhantomReturn constructor, vtable at 1410c33f8)
    -> Iterates player list, sends LeaveGuestPlayer per phantom
  ```
- **Fix:** Runtime NOP of 5 bytes at exe+0x44ef7b on DLL init. Verifies expected bytes first.
- **Code:** `PatchPhantomReturnOnBossKill()` in `player_sync.cpp`

### 4. Duplicate Phantom Join Fix
- **Problem:** HUD showed "1 player" even with 3 people in session.
- **Root cause:** `OnPhantomJoined()` was called from BOTH `SerializeHook` (outgoing) AND `ParseHook` (incoming), adding every player twice.
- **Fix:** Removed the call from `SerializeHook`. `ParseHook` is correct — both host and joiner see it there.

### 5. Diagnostic Name Scan Removed
- Stripped the 15-path diagnostic scan from `ReadCharacterName()` now that `GMImp+0xA8+0x114` is confirmed.
- Cleaner logs.

### 6. Player Cap 3 -> 6 (Ghidra analysis)
- **Problem:** 4th player couldn't join — DS2 hardcodes max 3 players.
- **Ghidra trace:** `FUN_1406ab050` (JoinGuestPlayer handler) contains two hardcoded `3` values:
  - `exe+0x6ab0b6`: `MOV dword ptr [RBP+local_6c], 0x3` (c7 45 c3 03 00 00 00) — local var
  - `exe+0x6ab15b`: `MOV dword ptr [RBX+0x1c], 0x3` (c7 43 1c 03 00 00 00) — MaxPlayers field in protobuf message
- **Fix:** Both `03` bytes patched to `06` at runtime. Byte verification before patching.
- **Code:** `PatchPlayerCap()` in `player_sync.cpp`
- **Note:** ds3os server may also have its own cap — check server config if 4th player still fails.

### 7. Ghidra Automation Script
- Created `E:\Seamless\tools\ghidra_analysis.py` — Jython script for Ghidra's Python console.
- Run: `execfile(r"E:\Seamless\tools\ghidra_analysis.py")` in Window -> Python
- Outputs to `E:\Seamless\tools\ghidra_results.txt`
- Jython quirks: `import jarray`, masks use `-1` not `0xFF`, no non-ASCII chars in script

## Exe Patches Applied at Runtime (player_sync.cpp Initialize)
| Patch | Offset | Bytes | Effect |
|-------|--------|-------|--------|
| Boss-kill phantom return | exe+0x44ef7b | e8 30 2c d4 ff -> 90 90 90 90 90 | Phantoms stay on boss kill |
| Player cap (local var) | exe+0x6ab0b9 | 03 -> 06 | local_6c = 6 instead of 3 |
| Player cap (protobuf msg) | exe+0x6ab15e | 03 -> 06 | MaxPlayers=6 in JoinGuestPlayer message |

## RTTI Classes Found (Ghidra)
| Class | RTTI Offset |
|-------|-------------|
| EventPhantomReturn | +0x156A7D4 |
| EventBossBattleManager | +0x156A344 |
| PhantomManager (NPC) | +0x15758C2 |
| SummonManager | +0x15792AA |
| SessionManager | +0x15886EF (multiple) |
| NetSessionManager | +0x15886EC (multiple) |

## Still Outstanding
- **Ghost appearance for remote players** — ChrNetworkPhantomId only affects local. Need entity iteration via GMImp+0x18 CharacterManager.
- **Boss desync** — phantom entering already-cleared boss room gets broken state (no health bar, no damage, no sounds). Needs test run to characterize after current patches.
- **Souls not shared** — damage attribution not hooked. Low priority.
- **Event flag sync** — chests/bosses not shared between worlds. Large feature.
- **ds3os server cap** — server may independently cap at 3. Check server config for `max_players`.

## Build State
- All changes committed and pushed to `main`
- dist/host/dinput8.dll and dist/joiner/dinput8.dll updated
- DS2_Seamless_Host.zip and DS2_Seamless_Joiner.zip rebuilt
- Commits: ca5ad56 (name fix), 8c0f792 (boss-kill NOP), 327cb3f (duplicate join fix), a5df633 (player cap)
