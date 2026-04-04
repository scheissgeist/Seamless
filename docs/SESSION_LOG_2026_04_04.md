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

### 4. Ghidra Automation Script
- Created `E:\Seamless\tools\ghidra_analysis.py` — Jython script for Ghidra's Python console.
- Searches strings, traces XREFs, finds CMP constants, dumps results to `ghidra_results.txt`.
- Partial results: boss-kill patch confirmed, PhantomManager/SummonManager RTTI found.
- Player cap location not yet found — deep in vtable dispatch, no named string.

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
- **Ghost appearance for remote players** — ChrNetworkPhantomId only affects local. Need entity iteration.
- **4-player cap** — player cap likely in SummonManager vtable. No named string. Need CMP scan.
- **Boss desync** — phantom entering already-cleared boss room gets broken state.
- **Souls not shared** — damage attribution not hooked.
- **Event flag sync** — chests/bosses not shared between worlds.

## Build State
- All changes committed and pushed to `main`
- dist/host/dinput8.dll and dist/joiner/dinput8.dll updated
- DS2_Seamless_Host.zip and DS2_Seamless_Joiner.zip rebuilt
