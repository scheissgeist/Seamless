# DS2 Seamless Co-op — Session Log: April 1-2, 2026

## Summary
First real multiplayer testing session with 3 players (Sean/Gwister, MichaelBeee/Chwaest, pisssass/Coomguy). Custom server running on Sean's machine via Hamachi. Major bugs found and fixed live.

## What Works
- **DLL injection via dinput8.dll** — auto-loads with DS2 SotFS
- **Custom server redirect** — game connects to our ds3os server instead of FromSoft
- **Protobuf disconnect blocking** — blocks `DisconnectSession` and `LeaveGuestPlayer` so sessions survive boss kills and deaths
- **3 players connected simultaneously** on custom server
- **Summoning works** — players can see and use each other's summon signs
- **Death survival** — phantom dies, game tries to disconnect, mod blocks it, other players stay connected
- **ImGui overlay** — shows player count, INSERT menu works
- **JoinGame.bat** — auto-detects DS2 install, asks for host IP, installs mod, launches game
- **TeamType found via Cheat Engine** — value 513 (uint16) = White Phantom, 0 = Host. Address at offset 0x1374 from player data base. Write instruction at DarkSoulsII.exe+0xDF1719 (`movaps [rax+rdx-10], xmm0`)

## Bugs Found & Fixed

### 1. RSA Key Format Mismatch
- **Problem:** Generated key was PKCS#8 (460 bytes) but game expects PKCS#1 (426 bytes)
- **Fix:** Let ds3os server generate PKCS#1 keys, removed strict size check in patcher

### 2. Only Login Port Redirected
- **Problem:** Game connects to login (50031), auth (50000), and game (50010+) ports. Only 50031 was redirected.
- **Fix:** Redirect all ports in the 50000-50100 range

### 3. Server Hostname = 127.0.0.1
- **Problem:** Server told remote clients "connect to auth at 127.0.0.1" — their localhost
- **Fix:** Set ServerHostname to Sean's Hamachi IP (25.35.223.224)

### 4. Auth Token Validation
- **Problem:** Game Service rejected connections with "authentication token does not appear to be valid"
- **Fix:** Modified ds3os GameService.cpp to create fallback auth state for unrecognized tokens

### 5. Sign Filter Blocking All Signs
- **Problem:** `ContainsSessionSteamId()` rejected entire SignList responses if ANY sign had an unrecognized Steam ID. Result: nobody could see signs.
- **Fix:** Disabled the sign filter entirely — private server has no randoms

### 6. Death Crash (ResizeBuffers)
- **Problem:** Game rapidly fires ResizeBuffers during death screen transition. ImGui objects invalidated while Present was rendering.
- **Fix:** Added `g_resizing` flag to skip Present rendering during resize operations

### 7. Crystal Crash (All Players)
- **Problem:** Black Separation Crystal triggers `LeaveSession` which we blocked. Returning `target` unchanged from SerializeHook left the buffer in a bad state, crashing the game's state machine.
- **Fix:** Let serialize complete (buffer advances), then zero the output bytes so the server ignores the corrupt message

### 8. Host Crash on Phantom Departure
- **Problem:** Same as #7 — when a phantom dies and the game tries to send `LeaveGuestPlayer`, the serialize block crashed the host
- **Fix:** Same fix as #7

### 9. Anti-Cheat Warning
- **Problem:** ds3os server flagged MichaelBeee for "illegal disconnects" after repeated crashes
- **Fix:** Set all anti-cheat thresholds to 999999, zeroed disconnect penalty score, wiped database

### 10. JoinGame.bat Crashes
- **Problem:** `for /f` registry query and `findstr` with `enabledelayedexpansion` crashed on some systems
- **Fix:** Simplified to basic `if exist` checks across drives C-G, no delayed expansion

### 11. TeamType Wrong Values
- **Problem:** Code checked for values 1-5, but real DS2 SotFS values are 0 (Host), 513 (White Phantom), 515 (Sunbro), 1799 (Dark Spirit)
- **Fix:** Runtime scan for uint16 value 513 near player data structures, write 0 (Host)

### 12. Debug Logging Spam
- **Problem:** Player sync logged garbage HP values (-1393501216) at 20Hz, flooding the log
- **Fix:** Commented out per-frame debug logging

## Still Broken

### Crystal Crash
- Using the Black Separation Crystal still crashes the remaining players. The server tears down the entire session when one player sends LeaveSession. Needs server-side fix to only remove that one player.
- **Workaround:** Don't use the crystal. Quit to title menu instead.

### Phantom Restrictions
- Phantoms still can't rest at bonfires, interact with NPCs, open chests, or initiate fog walls
- TeamType is found and zeroed (513 → 0) but this alone doesn't unlock bonfire access
- DS2 has MULTIPLE checks: TeamType, phantom count, session state flag
- The bonfire block is "are there ANY phantoms in the world" not just "am I a phantom"
- Need to find and NOP the phantom count check or the `CanRestAtBonfire()` function

### Host Crash on Phantom Death
- When a phantom dies in the host's world, the host sometimes crashes
- The zeroed serialize output may still trigger server-side cleanup that feeds back to the host
- Needs more testing with latest build

### Item Sharing
- When someone opens a chest or picks up an item, only they get it
- Need to intercept item pickup protobuf messages and duplicate to all players
- Added logging for Item/Chest/Treasure messages but haven't analyzed results yet

## Cheat Engine Findings

### TeamType (Phantom vs Host)
- **Address:** Heap-allocated, found at `0x7FF4A3236BB4` in one session
- **Offset:** `0x1374` from player data base pointer
- **Value Type:** uint16 (2 bytes)
- **Values:** 0=Host, 513=WhitePhantom, 515=Sunbro, 1799=DarkSpirit
- **Write Instruction:** `DarkSoulsII.exe+0xDF1719` — `movaps [rax+rdx-10], xmm0`
- **Registers:** RAX=base object, RDX=0xB500, target=RAX+RDX-0x10
- **Note:** Setting to 0 alone doesn't unlock bonfires — separate check exists

### Bonfire Rest Flag (NOT FOUND)
- Scanned for 1 (can rest) → 0 (can't rest with phantoms) — found 13 results at DarkSoulsII.exe+16Axxxx but they didn't change when phantom was summoned
- The bonfire check might use phantom count (int > 0) rather than a boolean flag
- Need different CE approach or binary analysis

## Architecture

```
Player Machine                          Host Machine
┌─────────────┐                      ┌─────────────────┐
│ DS2 SotFS   │                      │ DS2 SotFS       │
│ + dinput8   │◄──── Hamachi ───────►│ + dinput8       │
│   .dll      │     (25.x.x.x)      │   .dll          │
│             │                      │                 │
│ Redirects   │                      │ Redirects to    │
│ to host IP  │                      │ 127.0.0.1       │
└──────┬──────┘                      └────────┬────────┘
       │                                      │
       │         ┌──────────────┐              │
       └────────►│ ds3os Server │◄─────────────┘
                 │ (Port 50031) │
                 │ (Port 50000) │
                 │ (Port 50010) │
                 └──────────────┘
```

## Plans / Next Steps

1. **Fix phantom restrictions** — find the bonfire/NPC/fog wall check via CE or binary analysis. Likely a phantom count comparison or a session state flag. Could try NOPing the instruction at the check.

2. **Fix crystal crash** — server-side: modify ds3os to only remove the leaving player, not tear down the entire session. Or client-side: hook the session teardown function to prevent cleanup.

3. **Item sharing** — intercept item pickup protobuf messages, broadcast to all players via P2P, use item give function to duplicate items.

4. **Solid phantom appearance** — make phantoms appear as normal players (not ghostly white). Related to TeamType/PhantomType but separate visual flag.

5. **Player names in overlay** — character name read from game memory uses wrong offset. Need correct pointer chain.

6. **Auto-summon** — skip the soapstone entirely. When a player joins the server session, auto-create a summon and accept it programmatically.

7. **Stress test** — try with 4-6 players to find the real limit.

8. **Souls sharing** — when any player kills an enemy, all players get the souls.

## File Changes This Session
- `src/hooks/session_hooks.cpp` — serialize fix, sign filter removal, incoming block updates
- `src/hooks/network_hooks.cpp` — multi-port redirect, force VirtualProtect
- `src/sync/player_sync.cpp` — CE-verified TeamType scan (value 513), phantom count zeroing
- `src/ui/renderer.cpp` — ResizeBuffers crash guard
- `third_party/ds3os/Source/Server/Server/GameService/GameService.cpp` — auth token bypass
- `Release/Server/Saved/default/config.json` — Hamachi IP, anti-cheat disabled, welcome message
- `dist/joiner/JoinGame.bat` — auto-detect, fallback path, always update DLL
- `dist/` — host and joiner packages with latest binaries

## Git Commits (this session)
- d87d6ce — Add distribution packages
- b260ae9 — JoinGame.bat
- 83d0cf6 — RSA key fix
- 76216e5 — Multi-port redirect
- df441d3 — Auto-launch DS2
- 011a55d — Force VirtualProtect
- 41ec883 — Scan Steam libraries
- aa57265 — Gitignore ds3os build
- fb93650 — Auth token bypass
- 9598d8c — Simplified JoinGame.bat
- f5f010c — Fix bat crash
- acf0d5e — ResizeBuffers crash guard
- ff631e2 — Allow LeaveSession through
- 4a7741f — Block incoming leave notifications
- 2dfb088 — Phantom count zeroing
- f8b7d5f — Item/interaction logging
- e00e09b — Remove debug spam
- 2fdea19 — Disable sign filter
- 34b9dc9 — Anti-cheat thresholds
- 50f0072 — CE-verified TeamType patch
- b4e8175 — Fix serialize crash (let buffer advance)
