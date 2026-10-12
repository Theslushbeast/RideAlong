# Ride Along

An SKSE plugin for Skyrim Special Edition / Anniversary Edition: your follower rides behind you on your horse.

Nexus: https://www.nexusmods.com/skyrimspecialedition/mods/194579

## Features (v0.2.0)
- Ask a follower "Ride with me." / "You can get down now." (RideAlong.esp), or use a hotkey / held controller button
- Passenger pinned to the saddle every frame, copying your leg movement
- Dismount animation, stepping down on the horse's left
- Peaceful passenger: stays out of fights while riding, AI stays on so you can still talk
- Optional Princess Seat (side-saddle for female passengers) via Open Animation Replacer
- In-game settings in SKSE Menu Framework; everything also in `RideAlong.ini`

## Files
| Path | What it is |
|---|---|
| `src/main.cpp` | the whole plugin |
| `src/PCH.h` | precompiled header |
| `src/SKSEMenuFramework.h` | SKSE Menu Framework API header (optional settings page) |
| `RideAlong.ini` | default settings |
| `esp/RideAlong.esp` | the dialogue plugin (ESL) |
| `esp_tools/` | Python script that generates `RideAlong.esp` |
| `princess_seat/` | OAR config for the optional Princess Seat add-on (animations not included, see its readme) |
| `build.bat` | one-click build |

## Building
Requirements: Windows, Git, Visual Studio 2022 Build Tools (C++), CMake 3.25+.

1. Clone CommonLibSSE-NG into `extern/CommonLibSSE-NG` (`build.bat` does this if Git is installed).
2. Edit `MODDIR` at the top of `build.bat` to point at your mod folder.
3. Run `build.bat`. The first build downloads dependencies and can take 15-30 minutes. Output: `build/Release/RideAlong.dll`.

## License
GPL-3.0-or-later (see `LICENSE`).
Built with [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG), licensed GPL-3.0-or-later WITH Modding Exception AND GPL-3.0 Linking Exception (with Corresponding Source).
The Princess Seat's side-saddle animations are yukl's (YY Anim Replacer - Princess' Horse Riding) and are not part of this repository.
