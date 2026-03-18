# Dark Souls 2: Seamless Co-op

Seamless co-op mod for Dark Souls 2: Scholar of the First Sin. Play through the entire game with friends without getting disconnected at boss fights, deaths, or area transitions.

## Download

Grab the latest release from the [Releases page](https://github.com/scheissgeist/Seamless/releases). No build tools required.

## How to use

1. Launch Dark Souls 2 normally through Steam
2. Run `PlayDS2WithMod.exe` as Administrator
3. Press **INSERT** in-game to open the co-op menu
4. **Host:** Set a password, click Start Hosting, share your IP with friends
5. **Join:** Enter host's IP and password, click Connect

## Connecting to friends

**Same network (LAN):** Use the LAN IP shown in the host menu.

**Over the internet:**
- Host forwards UDP port 27015 on their router, friends use the Public IP shown in the menu
- Or install [Hamachi](https://vpn.net) / [ZeroTier](https://zerotier.com) and use the virtual LAN IP

## How it works

The mod hooks the game's protobuf serialization layer (the same technique used by [ds3os](https://github.com/TLeonardUK/ds3os)) to intercept and block disconnect messages (`NotifyDisconnectSession`, `NotifyLeaveSession`, `NotifyLeaveGuestPlayer`). When the game tries to end your co-op session after a boss kill or death, the message is silently dropped and the session continues.

Additional systems:
- DX11 Present hook with ImGui overlay for the in-game menu
- AOB pattern scanning for game memory addresses
- UDP P2P networking with password-protected sessions
- Player position/health sync from game memory

## Building from source

Requires Visual Studio 2022 with C++ Desktop workload and CMake 3.20+.

```
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

Outputs:
- `build/bin/Release/ds2_seamless_coop.dll` — the mod
- `build/bin/Release/PlayDS2WithMod.exe` — the launcher

## Compatibility

Tested on Dark Souls 2: Scholar of the First Sin, Steam version, Ver 1.03 / Calibrations 2.02.

## Credits

- [ds3os](https://github.com/TLeonardUK/ds3os) by TLeonardUK — protobuf interception technique and network protocol research
- [Bob Edition Cheat Table](https://github.com/Atvaark/Dark-Souls-2-SotFS-CT-Bob-Edition) — game memory offsets
- [Dear ImGui](https://github.com/ocornut/imgui) — in-game UI
- [MinHook](https://github.com/TsudaKagewortu/minhook) — function hooking

## License

MIT
