# DS2 Seamless Co-op — Code Review & Technical Findings

**Date:** February 4, 2026  
**Status:** First successful test with a friend confirmed  
**Codebase:** `E:\Seamless` (DS2:SotFS Seamless Co-op mod)

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [What Works](#what-works)
3. [Active Bugs](#active-bugs)
4. [Things You're Probably Overlooking](#things-youre-probably-overlooking)
5. [Incomplete Features](#incomplete-features)
6. [Priority Fix List](#priority-fix-list)
7. [File-by-File Status](#file-by-file-status)

---

## Architecture Overview

DLL mod loaded as a `dinput8.dll` proxy. Windows loads it automatically via DLL search order; it proxies all real `DirectInput8Create` calls transparently. Initialization runs in a background thread with a 3-second startup delay.

```
DllMain (dinput8 proxy)
  └── SeamlessCoopMod::Initialize()
        ├── 1. MinHook init
        ├── 2. AOB pattern scan  ← resolves GameManagerImp, NetSessionManager, KatanaMainApp
        ├── 3. Protobuf hooks    ← CORE: blocks disconnect messages
        ├── 4. Winsock hook      ← detects online status (non-critical)
        ├── 5. Game state hooks  ← stubs, never installed
        └── 6. Subsystems
              ├── PeerManager     (UDP P2P, port 27015)
              ├── SessionManager  (player list, drives update loop at 20Hz)
              ├── PlayerSync      (reads game memory, broadcasts pos/state)
              ├── ProgressSync    (boss/bonfire tracking — no game writes yet)
              └── OverlayRenderer (ImGui via DX11 Present + ResizeBuffers hooks)
```

The **core mechanism** is hooking FromSoftware's protobuf serialization layer (`SerializeWithCachedSizesToArray` + `ParseFromArray`). Every outgoing/incoming network message passes through these. When seamless mode is active, disconnect/leave messages are silently dropped. The game thinks they were sent; the session survives boss kills, deaths, and area transitions.

On top of that, a custom UDP P2P layer on port 27015 handles position, state, and event sync between players.

---

## What Works

- **Session persistence** — protobuf hook correctly blocks `NotifyDisconnectSession`, `NotifyLeaveSession`, `NotifyLeaveGuestPlayer`, `BanishPlayer`, `ReturnToOwnWorld`, `RestAtBonfire`, and fog gate leave messages. This is the hard part and it's working.
- **Phantom timer extension** — writes `99999.0f` to `NetSessionManager → +0x18 → +0x17C` (AllottedTime) every 2 seconds. Real memory write, prevents phantom expiry.
- **Hollowing suppression** — clears the hollowing byte every 2 seconds while seamless is active, keeping summon signs usable for hollow hosts.
- **P2P networking layer** — full UDP stack: handshake, heartbeat (5s), timeout (15s), password auth, position at 20Hz, state at 2Hz.
- **Soapstone granting** — AOB scans for the game's `ItemGive` function and calls it with the correct item structs. SEH-wrapped for safety.
- **ImGui overlay** — DX11 Present + ResizeBuffers hooks. Handles fullscreen toggles, resolution changes, and DS2's title→game scene transition (RTV rebuild). Dark Souls gold aesthetic. Hidden IPs by default (streamer safe).
- **Address resolution** — runtime AOB scanning with RIP-relative resolution (correct for ASLR).

---

## Active Bugs

### 1. Deadlock: Lock Ordering Inconsistency
**File:** `src/session/session_manager.cpp` lines 271–304  
**Severity:** High — can silently hang the game

`NotifyPlayerDeath` and `NotifyPlayerRespawn` hold `m_playersMutex` while calling `PeerManager::BroadcastPacket`, which acquires `m_peersMutex`. `PeerManager::Update()` holds `m_peersMutex` and calls `PacketHandler::HandlePacket`, which calls `SessionManager::AddPlayer`, which acquires `m_playersMutex`.

That is a classic ABBA deadlock:
- Thread A: `m_playersMutex` → tries `m_peersMutex`
- Thread B: `m_peersMutex` → tries `m_playersMutex`

**Fix:** In `NotifyPlayerDeath`/`NotifyPlayerRespawn`, copy the data you need under the lock, release it, then broadcast:

```cpp
void SessionManager::NotifyPlayerDeath(uint64_t playerId) {
    std::string name;
    {
        std::lock_guard<std::mutex> lock(m_playersMutex);
        SessionPlayer* player = GetPlayer(playerId);
        if (!player) return;
        player->isAlive = false;
        name = player->playerName;
    }
    // Broadcast WITHOUT the lock held
    Network::PacketHeader pkt{};
    pkt.magic = 0x44533243;
    pkt.type  = Network::PacketType::PlayerDeath;
    pkt.size  = sizeof(Network::PacketHeader);
    Network::PeerManager::GetInstance().BroadcastPacket(&pkt);
}
```

---

### 2. Offset Collision: `Health` vs `PositionY` at the Same Address
**File:** `include/addresses.h` lines 181–190  
**Severity:** High — silently reading garbage for one of the two fields

In `Offsets::GameManager`:

```cpp
constexpr uint32_t PositionX  = 0x30;   // float
constexpr uint32_t PositionY  = 0x34;   // float   ← 0x34
constexpr uint32_t PositionZ  = 0x38;   // float
constexpr uint32_t HP         = 0x34;   // int32   ← SAME address as PositionY
constexpr uint32_t Health     = 0x0;    // int32   ← this is something different
constexpr uint32_t MaxHealth  = 0x1C;   // int32
```

`HP` and `PositionY` are both `0x34` from the same `PlayerData` base. One of them is wrong. `Health` at `0x0` looks like it was from a different pointer chain entirely. Currently `ReadPlayerHealth` uses `Health` (0x0) and `MaxHealth` (0x1C), while `ReadPlayerPosition` uses `PositionX/Y/Z` (0x30–0x38). The `HP` constant at 0x34 is defined but never used — it was probably the correct health offset, left orphaned when `Health = 0x0` was added.

Check the Bob Edition CT to confirm which offset is actually current HP. If `HP = 0x34` is correct, the position reads are also wrong since they share that block.

---

### 3. `SyncLocalPlayerState` Writes `soulLevel` to the Discarded Copy
**File:** `src/sync/player_sync.cpp` lines 237–265  
**Severity:** Medium — soul level in the session record is always 0

`SyncLocalPlayerState` calls `GetPlayers()` which returns by value. It finds `localPlayer` in the copy and does `localPlayer->soulLevel = soulLevel`. The copy is immediately discarded. The session manager's soul level for the local player is never updated.

`health`/`maxHealth` are correctly stored back via `sessionMgr.UpdatePlayerHealth()`, but there's no equivalent `UpdatePlayerLevel()` call.

**Fix:** Add `SessionManager::UpdatePlayerLevel(localId, soulLevel)` and call it, or read it back from the session record in the same pattern used for health.

---

### 4. Notification Stack Y-Gap on Expired Entries
**File:** `src/ui/overlay.cpp` lines 512–528  
**Severity:** Low — visual artifact

```cpp
for (auto it = m_notifications.end(); it != m_notifications.begin(); ) {
    --it;
    it->timeRemaining -= dt;
    if (it->timeRemaining <= 0.0f) continue;  // ← skips rendering but...

    y -= 10.0f;  // ← this still runs for the NEXT entry, not this one
```

Wait — the `y -= 10.0f` is *after* the `continue`, so it actually does NOT run for expired entries. That part is fine. The real issue: `y` is decremented by a flat `10.0f` regardless of the actual rendered height of each notification window (which auto-sizes to its text). For notifications with long text that wrap to two lines, they'll overlap with the one above. The gap should be based on the actual window height, not a fixed constant.

---

### 5. `FetchPublicIPThread` Leaks `WSACleanup` Mismatch
**File:** `src/ui/overlay.cpp` lines 66–125  
**Severity:** Low — minor resource imbalance

`FetchPublicIPThread` calls `WSAStartup` at the start. It calls `WSACleanup` in the DNS failure path (line 77) and socket creation failure path (line 85), but **not** in the happy path after `closesocket`. The Winsock reference count is left +1 permanently in the success case. Since `PeerManager` also calls `WSAStartup`/`WSACleanup`, this creates an imbalance. Add `WSACleanup()` at the end of the success path before returning.

---

### 6. `SerializeHook` Returns Unadvanced Pointer When Blocking
**File:** `src/hooks/session_hooks.cpp` line 158  
**Severity:** Medium — potential buffer corruption

```cpp
static uint8_t* __fastcall SerializeHook(void* thisPtr, uint8_t* target) {
    // ...
    if (IsDisconnectMessage(className)) {
        return target;  // ← returns target unadvanced
    }
    return g_originalSerialize(thisPtr, target);
}
```

`SerializeWithCachedSizesToArray` is a protobuf convention that returns a pointer *past* the bytes it wrote (i.e., the new write cursor). When blocking, we return `target` unchanged (zero bytes written, cursor not advanced). If the caller uses the return value to chain multiple message serializations into a single buffer, this is correct — the blocked message takes up zero bytes and the next message starts at the same position.

However, if any caller checks `returnedPtr > target` as an indicator of success, it will see 0 bytes and may treat this as an error. Monitor the log for any unexpected behavior after a block event. This is ds3os's original approach and is likely safe, but worth verifying in practice.

---

### 7. `PeerManager::Update()` Re-Entrant via `LeaveSession` Inside Lock
**File:** `src/network/peer_manager.cpp` lines 201–218  
**Severity:** Medium — potential issues with `recursive_mutex`

`PeerManager::Update()` holds `m_peersMutex` for the entire duration (line 204). Inside, `CheckTimeouts()` runs, and the handshake timeout handler calls `LeaveSession()` (line 215), which also calls `BroadcastPacket()`, which tries to acquire `m_peersMutex` again.

This works because `m_peersMutex` is a `std::recursive_mutex`, so the re-entrant lock succeeds. But `LeaveSession()` also calls `m_peers.clear()` while iterating in `CheckTimeouts()` is still technically live (though the timeout call is inside `Update()` which holds the lock). This is safe here due to the recursion pattern, but is fragile — if the call chain changes, it will be a bug.

**Better fix:** Have `CheckTimeouts()` mark peers for removal and set a flag, then have `Update()` handle the `LeaveSession` call after releasing the lock.

---

### 8. `PatternScanner::FindPattern` Logs Every Single Scan Byte
**File:** `src/utils/pattern_scanner.cpp` lines 56–57  
**Severity:** Low — performance, and log file flooding

```cpp
Logger::GetInstance().LogInfo("Scanning for pattern in module (base: 0x%p, size: 0x%zX)", ...);
```

This one-time log per scan is fine. But the inner loop doesn't log per-byte, so this is actually okay. The real concern: each call to `FindPattern` scans the entire module image sequentially. With 4 patterns at startup (3 game pointers + 2 protobuf functions + 1 ItemGive), this means 6 full sequential scans of the ~50MB game image. It works, but there's a noticeable stall during init. Consider scanning for all patterns in a single pass.

---

## Things You're Probably Overlooking

### A. The `RestAtBonfire` Block Will Prevent Bonfires From Being Lit
`IsDisconnectMessage` includes:
```cpp
if (strstr(className, "RestAtBonfire")) return true;
```

This blocks the outgoing `RestAtBonfire` message so the phantom doesn't get kicked. But `RestAtBonfire` is also how the game tells the server you've rested (which is how it persists your bonfire). If that message is entirely blocked, your bonfire rest may not be recorded on the server side, meaning after the co-op session ends your last bonfire might revert. Test this: rest at a bonfire, leave the session normally, quit and reload — is your bonfire correct?

Consider only blocking it in specific contexts (e.g., only block if the sender is in phantom state, not if they're the host), or log the exact message class name to see whether the game actually sends this for bonfire rests vs. phantom kicks.

---

### B. No Reconnection Handling — One Drop Ends the Session
If one player's game crashes, their connection to the Fromsoft session is lost. The protobuf hooks can't prevent a disconnect that happens at the network transport layer (TCP drop to the Fromsoft servers). When that player reconnects (relaunches the game), they come back as a fresh client with no way to rejoin the existing custom session.

There's currently no:
- Session state export (session code or host IP persistence)
- Auto-rejoin on reconnect
- Session ID broadcasting over P2P so late joiners can match up

For longer sessions this will be a real friction point. The simplest partial fix is to remember the last host IP/password in the INI and offer a "Rejoin last session" button.

---

### C. The P2P Session and the Fromsoft Session Are Not Coupled
When a player joins via the overlay (enters IP + password), they create a P2P connection on port 27015. But the game's actual co-op session (the summon sign / phantom summon) happens entirely separately through Fromsoft's servers. A player could be in your P2P session but not in your Fromsoft phantom session, or vice versa.

There's no check that both sessions are active before turning on `g_seamlessActive`. If seamless mode is active but the Fromsoft session doesn't exist (no phantom), blocking disconnect messages does nothing useful and the phantom timer write will fail silently (no session pointer). This is fine for now but should be documented clearly — the user flow needs to be: summon your friend normally first, then start the seamless session, not the other way around.

---

### D. `ProtobufHooks::SetSeamlessActive(true)` Is Called Before Confirming the Fromsoft Session Exists
**File:** `src/ui/overlay.cpp` lines 415, 466

Both "Start Hosting" and "Connect" immediately call `SetSeamlessActive(true)`. But at that point:
- The host has no peers yet (no one has joined)
- The client hasn't received a handshake response yet (the connection isn't confirmed)

This means the protobuf disconnect blocking is active even before anyone is actually connected. If you die or kill a boss before your friend joins, those messages will be blocked unnecessarily. This is mostly harmless but means your Fromsoft session state might desync with what you intended.

**Better flow:** Enable seamless mode only after the handshake is confirmed (for the client) or after the first peer successfully connects (for the host). This would require `PeerManager` to call back into `ProtobufHooks::SetSeamlessActive`.

---

### E. `GetAsyncKeyState(VK_INSERT)` Is Polled Every Frame From the Render Thread
**File:** `src/ui/renderer.cpp` lines 247–249

```cpp
static bool insertWasDown = false;
bool insertDown = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
if (insertDown && !insertWasDown) overlay.Toggle();
insertWasDown = insertDown;
```

`GetAsyncKeyState` is fine here. But the `static` variables are accessed from the render thread (D3D Present callback thread). If anything else ever calls `Toggle()` from a different thread, there's a race on `m_visible`. The current code has this covered since only the render thread touches visibility, but it's worth noting.

Also: if the game itself uses INSERT for something (DS2 uses it for nothing standard, but mods might), this will conflict. Consider making the toggle key configurable in the INI.

---

### F. Password Is Sent in Plaintext in the Handshake Response
**File:** `src/network/peer_manager.cpp` line 392

```cpp
strncpy_s(response.password, m_sessionPassword.c_str(), sizeof(response.password));
```

The host echoes the session password back in its handshake response. Anyone on the local network sniffing UDP could capture both the initial handshake (which contains the password) and the response. For LAN play this is fine. For internet play, it means the session password is visible to anyone on the path. At minimum, don't echo the password back in the response — the client already knows it.

---

### G. `ShowNotification` Is Called From Multiple Threads Without a Mutex
**File:** `src/ui/overlay.cpp` line 196–203

`ShowNotification` pushes to `m_notifications` (a `std::vector`). It's called from:
- The update thread (via `SessionManager::AddPlayer`, `RemovePlayer`)
- The `PeerManager` network receive path (also update thread)
- The render thread (via `GrantSoapstones` button)

If host and client events happen near-simultaneously, two threads could call `push_back` on the vector at the same time — undefined behavior, possible crash. Add a `std::mutex` to `Overlay` protecting `m_notifications`, and lock it in both `ShowNotification` and `RenderNotifications`.

---

### H. `ProgressSync` Has No Mutex
**File:** `src/sync/progress_sync.cpp`

`m_eventFlags`, `m_defeatedBosses`, `m_litBonfires`, `m_pickedItems` are all `std::unordered_map/set` written by the update thread (via `PacketHandler::HandlePacket`) and potentially read by the ImGui render thread (if the overlay ever queries them). No mutex. Add `std::mutex m_mutex` and lock all read/write paths.

---

## Incomplete Features

These are known gaps that need reverse engineering work:

| Feature | Status | What's Needed |
|---------|--------|---------------|
| Boss defeat propagation | Broadcasts packet, **does not write to game memory** | Find `SetEventFlag(flagId, value)` AOB |
| Bonfire state sync | Same — broadcast only, no game write | Same `SetEventFlag` function |
| Boss ID extraction | Hook exists but ID is always `0` | Offset into boss object to read ID |
| Game state hooks | Written but `playerDeathAddr` and `bossDefeatedAddr` are `nullptr` | AOB patterns for player death / boss death funcs |
| Fog gate barrier | `WaitForPartyAtFogGate` logs and returns immediately | Hook into fog gate transition, delay until all players ready |
| Enemy sync | Config option exists, no implementation | Large scope — enemy AI state replication |
| Animation/equipment sync | Logs only | Offsets into entity structs for visual state |

The `extracted_addresses/` folder and the Bob Edition CT are the right starting points for all of these.

---

## Priority Fix List

| Priority | Issue | File | Effort |
|----------|-------|------|--------|
| 🔴 High | Deadlock: `m_playersMutex` held across `BroadcastPacket` | `session_manager.cpp` | 15 min |
| 🔴 High | Offset collision: `HP` and `PositionY` both at `0x34` — one is wrong | `addresses.h` | Verify in CT, 5 min to fix |
| 🟠 Medium | `RestAtBonfire` block may prevent bonfire state from saving server-side | `session_hooks.cpp` | Test + decide |
| 🟠 Medium | Seamless mode enabled before connection is confirmed | `overlay.cpp` + `peer_manager.cpp` | 30 min |
| 🟠 Medium | `SerializeHook` returning unadvanced pointer — verify no corruption in practice | `session_hooks.cpp` | Monitor logs |
| 🟠 Medium | `ShowNotification` called from multiple threads without mutex | `overlay.cpp` | 10 min |
| 🟠 Medium | `ProgressSync` unprotected shared state | `progress_sync.cpp` | 10 min |
| 🟠 Medium | `soulLevel` written to discarded copy — never stored back | `player_sync.cpp` | 5 min |
| 🟡 Low | `FetchPublicIPThread` missing `WSACleanup` on success path | `overlay.cpp` | 2 min |
| 🟡 Low | Password echoed back in handshake response | `peer_manager.cpp` | 2 min |
| 🟡 Low | Notification Y-gap is fixed 10px, not actual rendered height | `overlay.cpp` | 10 min |
| 🟡 Low | No reconnection / rejoin last session | multiple | 1–2 hours |

---

## File-by-File Status

| File | Status | Notes |
|------|--------|-------|
| `src/core/dllmain.cpp` | ✅ Good | dinput8 proxy + 3s delay + clean shutdown |
| `src/core/mod.cpp` | ✅ Good | 6-step init, INI parser, 20Hz update loop |
| `src/hooks/session_hooks.cpp` | ✅ Working | Protobuf interception with RTTI + ds3os patterns. Verify `RestAtBonfire` behavior. |
| `src/hooks/network_hooks.cpp` | ✅ Good | Winsock connect hook for online status |
| `src/hooks/game_state_hooks.cpp` | ❌ Stubs | Hooks written; `playerDeathAddr`/`bossDefeatedAddr` are `nullptr` |
| `src/network/peer_manager.cpp` | ✅ Good | Full UDP P2P. Minor: `LeaveSession` inside locked `Update()` |
| `src/network/packet_handler.cpp` | ✅ Good | Routes all packet types correctly |
| `src/session/session_manager.cpp` | ⚠️ Bug | Deadlock in `NotifyPlayerDeath`/`NotifyPlayerRespawn` |
| `src/sync/player_sync.cpp` | ⚠️ Minor | `soulLevel` never stored back. Offset collision risk. |
| `src/sync/progress_sync.cpp` | ❌ Incomplete | No mutex. Broadcasts but never writes to game memory. |
| `src/ui/overlay.cpp` | ⚠️ Minor | `ShowNotification` unprotected. `WSACleanup` missing. Notification Y-gap. |
| `src/ui/renderer.cpp` | ✅ Good | DX11 Present + ResizeBuffers hooks. RTV rebuild on resize. Streamer-safe IP. |
| `src/ui/title_notifier.cpp` | ✅ Good | Title bar session indicator |
| `src/utils/address_resolver.cpp` | ✅ Good | AOB + RIP-relative resolution |
| `src/utils/pattern_scanner.cpp` | ✅ Good | Multi-pass scan works, could be single-pass for performance |
| `include/addresses.h` | ⚠️ Verify | `HP = 0x34` collides with `PositionY = 0x34` — one is wrong |
