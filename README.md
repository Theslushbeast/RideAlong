# Ride Along

SKSE plugin for Skyrim SE/AE: your follower rides behind you on your horse. Press **K** while mounted to seat the
nearest follower behind you, press it again (or dismount) to let them off. Settings in `RideAlong.ini`.

Nexus page: https://www.nexusmods.com/games/skyrimspecialedition

## License

Ride Along is free software, licensed under the **GNU General Public License v3.0 or later** (see `LICENSE`).

It is built on [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG) (GPL-3.0-or-later with the Modding
Exception and the GPL-3.0 Linking Exception), which is statically linked into `RideAlong.dll`. This repository is the
Corresponding Source for the released DLL.

## Building

Requirements (Windows):
- Visual Studio 2022 or newer **Build Tools** with "Desktop development with C++" and "C++ CMake tools for Windows"
- Git for Windows

Run `build.bat`. On the first run it downloads vcpkg and CommonLibSSE-NG v11.0.0 (into `extern/`), which takes a while.
The plugin is written to `dist\SKSE\Plugins\RideAlong.dll` (set the `MODDIR` environment variable to copy it straight
into a mod folder instead). Progress and errors go to `build.log`.

Manual build:
```
git clone --depth 1 --branch v11.0.0 --recurse-submodules https://github.com/alandtse/CommonLibSSE-NG.git extern/CommonLibSSE-NG
cmake -S . -B build -A x64 -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
cmake --build build --config Release
```

## Files

- `src/main.cpp` - the whole plugin: hotkey, boarding/alighting, seating the passenger on the saddle every frame,
  leg pose copy, movement hook, cell-change handling, drive-by combat (bow swap / hold on)
- `src/PCH.h` - precompiled header
- `RideAlong.ini` - user settings (shipped next to the DLL)

## Credits

- CommonLibSSE-NG (Ryan McKenzie, powerof3, CharmedBaryon, alandtse and contributors)
- SKSE team; meh321 for Address Library
