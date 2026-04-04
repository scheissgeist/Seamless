# Session Log - April 3, 2026

## Testing Participants
- Sean (Gwister) — host, running ds3os + mod
- Michael (Chwaest) — joiner
- Pissass (Coomguy) — joiner

## What Works
- Summoning into each other's worlds via soapstone
- Session persists through deaths (no disconnect)
- Phantom field zero → can open chests, open hidden walls
- Host can pick up items from chests opened by phantoms
- Fog wall entry for phantoms
- Bonfire bits set (isBonfireStart/isBonfireLoop)
- ChrNetworkPhantomId zeroed (local player appears solid to themselves)
- Server redirect to local ds3os working
- Incoming disconnect blocking (RemoveSign, LeaveGuestPlayer, etc.)
- Game session survives Hamachi disconnect (ds3os TCP stays up)
- Clean mod shutdown (no crash on exit)

## What Doesn't Work
- **Boss kill kicks phantoms** — game internally removes phantom objects before sending network notification. Can't block outgoing (dangling pointers → crash). Needs NOP on internal removal function.
- **Ghost appearance for OTHER players** — ChrNetworkPhantomId only affects local rendering. Remote players still appear translucent. Need per-entity iteration.
- **NPCs appear as ghosts to phantoms** — same rendering issue
- **Player names** — NSM+0x20+0x234 always returns HOST's name. Joiners show as "Player". Need a different name path for the local player.
- **4th player can't join** — DS2 caps at 3 players (host + 2 phantoms). Need to patch slot limit in exe.
- **Souls not shared** — only awarded to kill owner. Would need damage attribution hooks.
- **P2P heartbeats unreliable over Hamachi** — peers connect then time out after 60s. HUD player list flickers. Not critical since game session works without P2P.
- **Boss desync** — phantom entering boss room they already cleared causes broken boss state (no health bar, no damage, no sounds initially)

## Crashes Fixed This Session
1. Hollowing write (wrong offset, value 244) → disabled
2. Session-slot TeamType write (value 127, wrong field) → disabled
3. Stale DLL causing offline mode → deployed new build
4. Outgoing LeaveGuestPlayer block → reverted (dangling pointers)

## Architecture Insight
- The ds3os server handles ALL game networking. P2P UDP (port 27015) is only for overlay HUD sync.
- Game sessions survive Hamachi drops because the TCP connection to ds3os stays alive.
- Phantom join/leave detection via protobuf messages works but only on the HOST side (outgoing NotifyJoinGuestPlayer).

## Next Steps (Requiring CE/Ghidra)
1. Boss-kill phantom removal NOP — find the function, trace call stack from NotifyLeaveSession
2. Remote player ChrNetworkPhantomId — iterate CharacterManager entity list (GMImp+0x18)
3. Session slot limit patch — find and expand the 3-player cap
4. Local player name path — find where YOUR OWN character name is stored (not the host's)
5. Event flag sync — for chest/boss state sharing between players

## Next Steps (Code-side)
1. Reduce P2P dependency — HUD should work from protobuf detection alone
2. Auto-resummon after boss kill — detect LeaveSession, auto-place sign + summon
3. Log more incoming protobuf messages to understand what the phantom receives
