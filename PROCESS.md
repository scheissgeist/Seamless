# How We Built DS2 Seamless Co-op in One Conversation

## The Numbers

- **~6,100 lines** of C++ across 17 source files
- **21 commits**, each solving a specific problem
- **One conversation** between a human and Claude (Anthropic's AI)
- **First working multiplayer test** within the same session
- A second player connected, the protobuf hook blocked a disconnect, and two players fought together in Dark Souls 2

## Why This Was Fast (And Why LukeYui's Took Years)

LukeYui's Seamless Co-op mods for DS1, DS3, and Elden Ring are genuinely incredible engineering. They took years because he was doing something harder:

1. **Full phantom replication** — his mods spawn and control phantom entities in the game world, syncing animations, equipment, attacks, and AI targeting across players. That requires reverse engineering hundreds of game functions.

2. **Custom server** — ds3os (which LukeYui's work builds on) is a full replacement for FromSoftware's matchmaking server. That's a separate server application that handles session orchestration, summon signs, invasions, and player matching.

3. **Solo reverse engineering** — one person in IDA/Ghidra for months, finding function signatures, understanding data structures, testing hypotheses against a running game.

4. **Every edge case** — invasion interactions, covenant effects, NPC summons, boss arena barriers, cutscene triggers, item duplication prevention, anti-cheat compatibility.

What we built is simpler but functional:

- We don't spawn phantom entities — we keep the game's own summon system alive
- We don't run a custom server — we use FromSoft's server but block disconnect messages
- We don't replicate animations — the game does that natively during co-op

## The Process

### Phase 1: Research (Minutes, Not Months)

Instead of opening IDA and spending weeks finding function addresses, we:

1. **Read the ds3os source code** — TLeonardUK already figured out that FromSoftware games use protobuf for network messages, and that blocking specific message types prevents session disconnects. We applied the same technique to DS2.

2. **Used the Bob Edition Cheat Table** — community cheat tables contain verified memory offsets for player position, health, stamina, level, character name, and more. These are the result of years of community reverse engineering. We extracted what we needed into `addresses.h`.

3. **Used DS2S-META** — pseudostripy's DS2 memory editor had the ItemGive function AOB pattern and the AvailableItemBag pointer chain. We used those directly.

**Key insight:** We didn't reverse engineer the game. We synthesized existing community knowledge from three sources (ds3os, Bob Edition CT, DS2S-META) into a single mod. The AI could read all three codebases, understand the patterns, and combine them — something that would take a human researcher days of cross-referencing.

### Phase 2: Architecture (One Prompt)

The user said: "Make a seamless co-op mod for Dark Souls 2. We want the easiest integration for the player."

From that, the AI designed the full architecture:
- DLL injection via dinput8 proxy (auto-loads, no launcher needed)
- MinHook for function hooking
- AOB pattern scanning for ASLR-safe address resolution
- Protobuf interception for disconnect blocking
- UDP P2P for custom session management
- DX11 Present hook for ImGui overlay
- Background update thread for sync

This wasn't designed iteratively — the architecture was laid out in the first response and remained correct through the entire project.

### Phase 3: Implementation (Iterative, Fast)

The code was written in large chunks:
- `session_hooks.cpp` — the core protobuf interception, RTTI class name extraction
- `peer_manager.cpp` — full UDP P2P with handshake, heartbeat, timeout
- `renderer.cpp` — DX11 Present hook, ImGui initialization, WndProc hook
- `overlay.cpp` — host/join menu, notifications, player list
- `player_sync.cpp` — game memory reading, position/health broadcast

Each file was written mostly complete on the first pass, then refined through testing.

### Phase 4: Live Testing & Bug Fixing (The Real Work)

This is where most of the 21 commits happened. The user ran the game, reported what happened, and bugs were fixed in real-time:

1. **"The launcher freezes"** → It was waiting for user input (Y/N prompt) with garbled Unicode. Fixed the encoding and removed the prompt.

2. **"No overlay shows up"** → `OverlayRenderer::Initialize()` was never called from `mod.cpp`. One missing line.

3. **"Game stuck at 720p"** → No `ResizeBuffers` hook. The render target view went stale after resolution changes. Added vtable slot 13 hook.

4. **"Game crashes when I host"** → Deadlock. `Update()` locked `m_peersMutex`, then called `SendHeartbeats()` → `BroadcastPacket()` which tried to lock again. `std::mutex` is not recursive. Changed to `std::recursive_mutex`.

5. **"Friend connects, then crash"** → Race condition. Network thread added a player to `m_players` vector while render thread was iterating it for ImGui display. Added `std::mutex` and changed `GetPlayers()` to return a copy.

6. **"Soapstones don't appear"** → Direct memory write to inventory offset was wrong (DS2 uses a managed item system, not a flat array). Replaced with AOB scan for the game's internal `ItemGive` function and proper `DS2ItemStruct` calling convention.

7. **"Game crashes when clicking Grant Soapstones"** → The `ItemGive` function was being called before the `AvailableItemBag` pointer chain was fully resolved. Added detailed logging to each pointer dereference.

8. **"My friend's game crashes on connect"** → Same deadlock bug — friend was running an older build. Updated the release zip.

Every bug was found by a human tester and fixed by the AI within minutes. No debugging sessions, no printf archaeology, no "let me reproduce this" — the AI read the log, identified the root cause, and wrote the fix.

### Phase 5: Polish

- Streamer IP hiding (toggle button, hidden by default)
- Sound notifications (Windows system sounds on join/leave)
- Character name reading from game memory
- Automatic hollowing clear for summon sign visibility
- Expanded disconnect blocking (fog gates, bonfires, invasions)
- CODE_REVIEW.md from a parallel AI review that found the HP offset bug and additional deadlocks

## What Made This Possible

### 1. Standing on Giants
We didn't invent the technique. ds3os proved protobuf interception works. The Bob Edition CT mapped the memory. DS2S-META found the ItemGive function. We assembled existing knowledge.

### 2. AI as Synthesizer
The AI's value wasn't in being smarter than LukeYui — it's in being able to read three separate codebases, understand the patterns, and combine them into working code in minutes. A human doing this cross-referencing would spend days.

### 3. Instant Iteration
Bug → log → root cause → fix → rebuild → deploy. This loop ran in under 5 minutes per cycle. A human developer would spend 30-60 minutes per cycle (reproduce, debug, fix, test, commit). We ran this loop ~15 times.

### 4. No Tooling Overhead
No IDE setup, no build system debugging, no dependency management rabbit holes. CMake was configured once, MinHook and ImGui were dropped in as source, and everything built cleanly from the first attempt.

### 5. The Human in the Loop
The user made critical design decisions:
- "Can we do this as a menu in the game, not a Windows popup?"
- "The IP addresses should be hidden for streamers"
- "We want resilience for every event — bonfires, fog gates, everything"
- "Make it so they just drop a file in the game folder and it works"
- "Boss fights should still be fair — no respawning inside"

The AI executed; the human directed. Neither could have done this alone.

## What's Missing (Honest Assessment)

This mod is simpler than LukeYui's because:

1. **No phantom spawning** — we keep the game's summon system alive, we don't create our own phantoms. Players still need to use soapstones.

2. **No boss/bonfire state sync to game memory** — when player A kills a boss, player B's game world doesn't update. The fog gate stays up for them.

3. **No custom server** — we're still using FromSoft's servers. If they detect modified traffic, there's a softban risk (low, but nonzero).

4. **No animation/equipment sync** — the game handles this natively during co-op, but we don't add anything beyond what the game provides.

5. **Limited testing** — tested with 2 players on one session. Not stress-tested with 4 players, long sessions, or edge cases.

## The Real Lesson

The gap between "impossible" and "done in a day" is often just knowing where to look. The DS2 modding community spent years building the knowledge base (cheat tables, ds3os, DS2S-META). We spent minutes reading it and hours assembling it. The AI didn't replace the reverse engineers — it stood on their shoulders and moved faster because it could read everything at once.

LukeYui's mods are still better. They handle edge cases we don't, spawn phantoms we can't, and run custom servers we didn't build. But for "I want to play through DS2 with my friend this weekend" — this works, and it was built in one conversation.
