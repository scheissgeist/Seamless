# Dark Souls 2: Seamless Co-op

Play through the entirety of Dark Souls 2: Scholar of the First Sin with friends. Boss kills, deaths, bonfires, fog gates — nothing disconnects you. One summon, the whole game.

## Download

**[Get the latest release](https://github.com/scheissgeist/Seamless/releases/latest)**

## Install

1. Extract the zip
2. Copy `dinput8.dll` and `ds2_seamless_coop.ini` into your game folder:
   ```
   Steam\steamapps\common\Dark Souls II Scholar of the First Sin\Game\
   ```
3. Launch Dark Souls 2 through Steam — the mod loads automatically
4. To uninstall, delete `dinput8.dll` from the game folder

## How to play

1. Press **INSERT** to open the co-op menu
2. **Host:** Set a password, click Start Hosting
3. **Join:** Enter the host's IP and password, click Connect
4. Use the White Sign Soapstone to summon each other (click "Grant Soapstones" if you don't have one)
5. Play through the game together

## Connecting to friends

| Setup | What to do |
|-------|-----------|
| Same network | Use the LAN IP from the host menu |
| Over the internet | Host forwards **UDP port 27015** on their router, friends use the Public IP |
| No port forwarding | Everyone installs [Hamachi](https://vpn.net) or [ZeroTier](https://zerotier.com), joins the same network, host shares the VPN IP |

IPs are hidden by default in the menu for streamer safety. Click to reveal.

## Features

- Sessions survive boss kills, player deaths, bonfires, fog gates, and area transitions
- In-game overlay menu (no alt-tabbing, no config editing)
- Password-protected sessions
- Real character names shown in player list
- Player count displayed in the HUD
- Join/leave notifications
- Soapstone granting (uses the game's own ItemGive function)
- Phantom timer automatically maxed (summons never expire)
- Hollowing automatically cleared (summon signs always visible)
- Public and LAN IP with copy-to-clipboard
- Auto-loads on game start (dinput8 proxy)

## How it works

The mod hooks the game's protobuf serialization layer to intercept and block disconnect messages. When the game tries to end your co-op session after a boss kill, death, or transition, the message is silently dropped and the session continues. This is the same technique used by [ds3os](https://github.com/TLeonardUK/ds3os) for Dark Souls 3 and by LukeYui's Seamless Co-op for Elden Ring.

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

## Credits

- [ds3os](https://github.com/TLeonardUK/ds3os) by TLeonardUK — protobuf interception technique
- [Dear ImGui](https://github.com/ocornut/imgui) — in-game UI
- [MinHook](https://github.com/TsudaKagewortu/minhook) — function hooking

## License

MIT
