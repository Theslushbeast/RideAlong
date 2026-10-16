# Ride Along

An SKSE plugin for Skyrim Special Edition, Anniversary Edition and VR: your followers ride with you on your horse,
and one of them can take the reins and ride you somewhere.

Nexus: https://www.nexusmods.com/skyrimspecialedition/mods/194579

## Features (v0.4.0)
- Up to four passengers on one horse: two behind you, one in front, one on your shoulders
- Ask in dialogue ("Ride with me.", "Ride in front of me.", "Ride on my shoulders.", "You can get down now.") or use a hotkey / held controller button
- Passengers are pinned to their seat every frame (camera-update hook + `Actor::ApplyMovementDelta` hook), with no character controller while they ride
- Climb-on and climb-off animations for the seats behind you (rider-only versions of the game's paired mount clips)
- "Take me somewhere on my horse.": a follower takes the saddle, you ride behind her, and the horse travels to one of 31 destinations on its own AI (a Travel-template package in `RideAlong.esp`, handed over through PapyrusUtil's `ActorUtil.AddPackageOverride`)
- Passengers stay out of fights, can't be hit by your swings, and keep weapons sheathed (or bows only)
- Optional Princess Seat (side-saddle for female passengers) via Open Animation Replacer
- In-game settings in SKSE Menu Framework; everything also in `RideAlong.ini`
- Follower frameworks supported (any actor in CurrentFollowerFaction counts)

## Files
| Path | What it is |
|---|---|
| `src/main.cpp` | the whole plugin |
| `src/PCH.h` | precompiled header |
| `src/SKSEMenuFramework.h` | SKSE Menu Framework API header (optional settings page) |
| `RideAlong.ini` | default settings |
| `esp/RideAlong.esp` | dialogue, passenger faction, globals and the horse's travel package (ESL) |
| `esp_tools/mkesp.py` | generates `RideAlong.esp` (`python mkesp.py ..\esp\RideAlong.esp`) |
| `esp_tools/ride_voices.json` | which vanilla follower voice lines each voice type has |
| `esp_tools/make_climb_clips.py` | builds the climb-on/off clips from your own `Skyrim - Animations.bsa` |
| `oar/RideAlongSeats/` | OAR configs for the front, shoulder and climbing animations (animations not in the repo, see below) |
| `princess_seat/` | OAR config for the optional Princess Seat add-on (animations not included, see its readme) |
| `build.bat` | one-click build |

## Animations
The `.hkx` files are not in this repository:
- **Getting on / Getting off** are made from the game's own mount and dismount clips. Generate them from your copy of Skyrim:
  `python esp_tools\make_climb_clips.py "<Skyrim>\Data" oar\RideAlongSeats` (needs `pip install lz4`).
- **Front (hug) / Shoulders** were made for Ride Along by a community animator and ship only in the Nexus download.
- **Princess Seat** uses yukl's YY Anim Replacer - Princess' Horse Riding; see `princess_seat/README_PrincessSeat.txt`.

## Building
Requirements: Windows, Git, Visual Studio 2022 Build Tools (C++), CMake 3.25+.

1. Clone CommonLibSSE-NG into `extern/CommonLibSSE-NG` (`build.bat` does this if Git is installed).
2. Edit `MODDIR` at the top of `build.bat` to point at your mod folder.
3. Run `build.bat`. The first build downloads dependencies and can take 15-30 minutes. Output: `build/Release/RideAlong.dll`.

## License
GPL-3.0-or-later (see `LICENSE`).
Built with [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG), licensed GPL-3.0-or-later WITH Modding Exception AND GPL-3.0 Linking Exception (with Corresponding Source).
