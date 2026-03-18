# Changelog

All notable changes to the DS2 Seamless Co-op mod are documented here.

---

## [Unreleased] — February 4, 2026

### Summary

Full AI-assisted code review and bug-fix pass following the first successful
co-op test. Seven bugs were identified and fixed. Zero compiler warnings on
rebuild.

---

### Fixed

#### 1. Deadlock: `m_playersMutex` held across `BroadcastPacket`
**File:** `src/session/session_manager.cpp`

`NotifyPlayerDeath` and `NotifyPlayerRespawn` were holding `m_playersMutex`
while calling `PeerManager::BroadcastPacket`, which internally acquires
`m_peersMutex`. `PeerManager::Update()` holds `m_peersMutex` and calls into
`SessionManager::AddPlayer`, which acquires `m_playersMutex`. This was a
classic ABBA deadlock — under the right timing it would silently hang the
game.

**Fix:** Updated both functions to copy the required state under the lock,
release it, then broadcast outside. `m_peersMutex` is no longer acquired
while `m_playersMutex` is held.

---

#### 2. Offset collision: `HP` and `PositionY` both at `0x34`
**File:** `include/addresses.h`

Inside `Offsets::GameManager`, two constants pointed to the same address from
the same `PlayerData` base:

```cpp
// BEFORE (broken)
constexpr uint32_t PositionY = 0x34;  // float
constexpr uint32_t HP        = 0x34;  // int32  ← same address
constexpr uint32_t Health    = 0x0;   // int32  ← wrong sub-struct
constexpr uint32_t MaxHealth = 0x1C;  // int32  ← wrong sub-struct
```

`ReadPlayerHealth` was reading from `Health = 0x0` and `MaxHealth = 0x1C`,
which are offsets into a different sub-object entirely. The orphaned `HP`
constant was left over from an earlier iteration.

**Fix:** Removed `HP`, corrected `Health` and `MaxHealth` to the verified
direct offsets from DS2S-META (`0x3C` and `0x40`):

```cpp
// AFTER (correct)
constexpr uint32_t PositionY = 0x34;  // float
constexpr uint32_t Health    = 0x3C;  // int32 — current HP
constexpr uint32_t MaxHealth = 0x40;  // int32 — max HP
```

---

#### 3. Soul level stored to a discarded `GetPlayers()` copy
**Files:** `src/sync/player_sync.cpp`, `src/session/session_manager.cpp`,
`include/session.h`

`SyncLocalPlayerState` called `GetPlayers()` which returns the player list
**by value**. It located the local player in the copy, wrote `soulLevel` to
it, then the copy was immediately discarded. The session manager's own record
never reflected the updated soul level — it stayed at `0` forever.

`health` and `maxHealth` were correctly handled via the existing
`UpdatePlayerHealth()` method, but no equivalent existed for soul level.

**Fix:** Added `SessionManager::UpdatePlayerLevel(uint64_t playerId,
uint32_t soulLevel)` to the session manager and declared it in `session.h`.
`player_sync.cpp` now calls it instead of writing into the copy.

---

#### 4. `ShowNotification` data race on `m_notifications`
**Files:** `include/ui.h`, `src/ui/overlay.cpp`

`ShowNotification` pushes to `m_notifications` (a `std::vector`). It is
called from the background update thread (peer join/leave events in
`SessionManager`) and from the render thread (button click callbacks and
the `GrantSoapstones` result). Concurrent `push_back` on a non-thread-safe
container is undefined behaviour and can cause heap corruption or a crash.

**Fix:** Added `mutable std::mutex m_notifMutex` to the `Overlay` class.
`ShowNotification` now acquires the lock before pushing. `RenderNotifications`
takes a snapshot of the list under the lock, renders from the snapshot,
then re-acquires the lock to tick timers and erase expired entries. The
render thread never holds the lock while calling ImGui functions.

---

#### 5. `FetchPublicIPThread` leaked a `WSAStartup` reference on success
**File:** `src/ui/overlay.cpp`

The public-IP background thread called `WSAStartup` at entry and
`WSACleanup` in every early-exit error path, but **not** in the happy path
after `closesocket`. The Winsock reference count was left incremented by 1
permanently every time a public IP was fetched successfully.

**Fix:** Added `WSACleanup()` immediately after `closesocket(sock)` in the
success path, before parsing the HTTP response body.

---

#### 6. Notification stack Y-gap was a fixed `10.0f` regardless of content
**File:** `src/ui/overlay.cpp`

Notification windows auto-size to fit their text content. The previous code
used a flat `y -= 10.0f` gap between entries. For any notification whose
rendered height exceeded 10 pixels (i.e., every notification), entries
would overlap each other in the stack.

**Fix:** After rendering each notification window, reads the actual rendered
window height via `ImGui::GetWindowHeight()` and steps `y` by
`winH + 4.0f`. This correctly stacks windows of any height with a 4-pixel
gap between them.

---

#### 7. Seamless disconnect-blocking activated before a connection existed
**Files:** `src/ui/overlay.cpp`, `src/network/peer_manager.cpp`

`ProtobufHooks::SetSeamlessActive(true)` was called immediately when the
user clicked **"Start Hosting"** or **"Connect"** — before any peer had
actually connected and before any handshake had been exchanged. This meant
the protobuf disconnect filter was live during the window between button
click and actual connection, which could block legitimate Fromsoft session
messages (e.g., if the player died or killed a boss while their friend was
still connecting).

**Fix:** Moved `SetSeamlessActive(true)` into `PeerManager::HandleHandshakePacket`:

- **Host side:** fires when the first new peer's handshake is validated and
  accepted (password matched, peer registered).
- **Client side:** fires when the host's handshake response is received and
  `m_handshakeConfirmed` is set to `true`.

The UI buttons no longer call `SetSeamlessActive` at all — they only start
the session and show a "waiting" notification. The overlay's **"Leave
Session"** button continues to call `SetSeamlessActive(false)` to turn
blocking back off when the session ends.

---

#### 8. Session password echoed in plaintext in handshake response
**File:** `src/network/peer_manager.cpp`

When the host accepted a joining client, it constructed a `HandshakePacket`
response and copied the session password into `response.password` before
sending it over UDP. The client already knows the password (it sent it in
the initial handshake). Any observer on the network path (LAN sniffing,
internet MITM) could capture the plaintext password from the UDP response
packet.

**Fix:** Changed to `response.password[0] = '\0'` — the password field in
the response is always empty. The client-side handler ignores the password
field in responses (`sequence == 1` responses only update peer identity).

---

### Changed

- `SessionManager` now exposes `UpdatePlayerLevel(uint64_t, uint32_t)`.
- `Overlay` now holds `mutable std::mutex m_notifMutex` (added to `ui.h`).
- `ui.h` now includes `<mutex>`.
- Hosting notification text changed from `"Hosting! Password: <pw>"` to
  `"Hosting! Waiting for players..."` since seamless mode is no longer
  active at that point and broadcasting the password in a notification was
  a minor information-leak.

---

### Build

```
cmake --build build --config Release
```

Output: `ds2_seamless_coop.dll`, `DS2SeamlessCoopLauncher.exe`,
`PlayDS2WithMod.exe` — all built clean, zero warnings.
Release folder updated with fresh binaries.

---

## [0.1.0] — February 4, 2026 (Initial Working Build)

### Summary

First end-to-end working build. Successfully tested co-op session with a
friend. Session persisted through a boss fight (disconnect messages blocked
by protobuf hook).

### Architecture

- `dinput8.dll` proxy loader — no file association required, drops into
  game folder.
- AOB pattern scanning at runtime (ASLR-safe). Three core patterns:
  `GameManagerImp`, `NetSessionManager`, `KatanaMainApp`.
- Protobuf interception hooks (`SerializeWithCachedSizesToArray` +
  `ParseFromArray`) block disconnect/leave messages while seamless mode
  is active. Patterns sourced from ds3os by TLeonardUK (verified working).
- MSVC x64 RTTI used to identify message class names without needing
  opcode tables.
- Custom UDP P2P layer on port 27015: handshake, heartbeat (5 s), timeout
  (15 s), password auth.
- Position broadcast at 20 Hz, state (HP/level/stamina) at 2 Hz.
- Phantom timer write (`AllottedTime = 99999.0f`) every 2 s to prevent
  phantom expiry.
- Hollowing byte zeroed every 2 s while seamless is active so summon signs
  remain usable for hollow hosts.
- Soapstone grant via AOB-resolved `ItemGive` function and pointer chain
  to `AvailableItemBag`.
- ImGui overlay via DX11 `Present` + `ResizeBuffers` hooks. RTV rebuilt on
  resize/fullscreen toggle. Dark Souls gold colour scheme. IP hidden by
  default (streamer safe).

### Known Gaps (not yet implemented)

- Boss defeat flags not written to game memory (broadcast only).
- Bonfire state not written to game memory (broadcast only).
- Game state hooks (`PlayerDeath`, `BossDefeated`) have no addresses — not
  installed.
- Boss ID extracted as `0` for every kill (TODO in `game_state_hooks.cpp`).
- Fog gate synchronisation barrier is a logging stub.
- Enemy, animation, and equipment sync not implemented.
