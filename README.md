# Dark Souls 2: Seamless Co-op

Play through Dark Souls 2: Scholar of the First Sin with friends. No more getting disconnected at boss fights or deaths. No more re-summoning after every encounter.

## Install

1. **[Download the latest release](https://github.com/scheissgeist/Seamless/releases/latest)**
2. Extract the zip
3. Copy `dinput8.dll` and `ds2_seamless_coop.ini` into your game folder:
   ```
   Steam\steamapps\common\Dark Souls II Scholar of the First Sin\Game\
   ```
4. Launch Dark Souls 2 through Steam — the mod loads automatically

To uninstall, delete `dinput8.dll` from the game folder.

## Play

Press **INSERT** to open the co-op menu.

**Host a session:** Set a password, click Start Hosting. Your IP is shown on screen with a Copy button.

**Join a session:** Enter the host's IP and password, click Connect.

## Connect to friends

| Setup | What to do |
|-------|-----------|
| Same house / LAN | Use the LAN IP from the host menu |
| Over the internet | Host forwards **UDP port 27015** on their router. Friends use the Public IP from the host menu |
| Easiest (no port forwarding) | Everyone installs [Hamachi](https://vpn.net) or [ZeroTier](https://zerotier.com), joins the same network, host shares their VPN IP |

## What it does

- Blocks disconnect messages so your session survives boss kills, deaths, and area transitions
- In-game ImGui overlay for hosting and joining (no config file editing, no alt-tabbing)
- Password-protected sessions
- Player health and position displayed in the session menu
- Public and LAN IP shown with copy-to-clipboard buttons

## Compatibility

- Dark Souls 2: Scholar of the First Sin (Steam)
- Windows 10 / 11
- Tested on Ver 1.03, Calibrations 2.02

## Building from source

Requires Visual Studio 2022 (C++ Desktop workload) and CMake 3.20+.

```
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

Output: `build/bin/Release/dinput8.dll` (drop in game folder) and `build/bin/Release/PlayDS2WithMod.exe` (alternative manual injector).

## Credits

- [ds3os](https://github.com/TLeonardUK/ds3os) by TLeonardUK — protobuf interception technique
- [Dear ImGui](https://github.com/ocornut/imgui) — in-game UI
- [MinHook](https://github.com/TsudaKagewortu/minhook) — function hooking

## License

MIT
