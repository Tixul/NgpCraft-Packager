# NgpCraft Packager

Version 0.4.0 for Windows x64. Package a Neo Geo Pocket or Neo Geo Pocket Color ROM as a standalone game EXE. The packager embeds its player, the NgpCraft desktop C++ core and the current clean-room HLE BIOS. Exporting and playing need no compiler, Python, network download, external core DLL or external BIOS.

The interface is in English. Windows file dialogs may follow the system language. No game ROM is included.

## Create a game EXE

Open `dist/NgpCraftPackager.exe`, choose a ROM (`.ngp`, `.ngc`, `.npc`, `.bin`), a title and optionally an ICO icon. Choose window scale and fullscreen startup, then click **Create EXE**. Distribute the exported EXE.

**Controls and settings...** sets the initial seven keyboard bindings, volume and scaling. Click a binding and press a key. Duplicate and reserved keys are rejected. Tab moves between fields; Enter can be assigned when a binding field is selected.

**Game credits and license...** accepts an author/studio, copyright, license name and full license text. **Import UTF-8 text...** reads `.txt` or `.md` files; the limit is 32,768 characters. Apply keeps changes for export; Cancel discards them. These fields describe the game, not the player. Empty fields do not assign MIT to the game.

ROM size must be between 64 bytes and 4 MiB. ZIP files must be extracted first. Export validates size, not game compatibility: test the resulting EXE.

## In-game controls

| Key | Action |
| --- | --- |
| Arrow keys | Direction |
| Z / X | NGPC A / B |
| Enter | Option |
| XInput A / B / Start | NGPC A / B / Option |
| Escape / F1 | Controls and settings |
| F2 | Game credits and NgpCraft license |
| F3 | Pause |
| F4 | Mute / restore sound |
| F5 | Current controls and help |
| F11 | Fullscreen / window |
| Alt+F4 | Quit |

The Game menu provides the same actions. Keyboard bindings are configurable; function keys, Escape, Tab, standalone modifiers and system shortcuts are reserved. The first available XInput controller is used, including its D-pad and left stick.

The game pauses during settings and when the window loses focus or is minimized. Apply stores changes; Cancel discards them. Restore defaults uses the settings embedded by the game creator. Integer scaling keeps regular pixels and margins; fit mode preserves the 160:152 aspect ratio while using the available space. The GDI presentation fix remains in place; see [VIDEO_FIX.md](VIDEO_FIX.md).

F2 has two separate sections: game credits/license, and the NgpCraft player/core/HLE MIT license. The texts are embedded, scrollable and selectable.

## Saves and preferences

Data is stored under `%LOCALAPPDATA%/NgpCraft/Games/<ROM SHA-256>/`:

- `preferences.dat`: keyboard, volume and display preferences.
- `save.ngpsav`: cartridge flash contents and RTC.
- `session.lock`: prevents simultaneous instances from overwriting each other's save.

Renaming or moving an EXE retains its data. Changing ROM bytes creates another identity. Player preferences override embedded defaults; invalid preferences fall back to those defaults.

Version 0.4 uses a new save format. Earlier private `save.sram` files are ignored; no migration is provided. They are not deleted. New saves store the entire presented flash contents of both chips, plus the emulated RTC and validation metadata (at most 4 MiB + 40 bytes). This covers save data outside the final 64 KiB too. It is an in-game save, not an instant save state. The RTC does not advance while the application is closed.

Saves are written every five seconds and on normal exit using temporary-file replacement. Invalid new-format saves are rejected and left intact; move the file aside to start fresh. Forced termination can lose recent changes. Re-export games to embed the new player and firmware.

## Build

Use CMake 3.20 or newer and MinGW-w64 with C++17. The helper defaults to `C:/Qt/Tools/mingw1310_64/bin`; override it for your installation:

```powershell
.\build.ps1 -CompilerBin 'C:\tools\mingw64\bin'
```

Output: `dist/NgpCraftPackager.exe`. `build/NgpCraftPlayer.exe` is an internal template without a ROM, not a playable game. The template and firmware are embedded at build time. Export injects ROM, configuration, optional credits and icon into a temporary copy, then moves it to the selected destination.

The core snapshot is included in `vendor/core/`, so rebuilding does not require a neighboring checkout. [vendor/source-info.json](vendor/source-info.json) records the desktop revision and exact source/firmware hashes. The HLE hash is checked by CMake. Its matching documentation is [vendor/firmware/README.md](vendor/firmware/README.md).

`src/native_core.cpp` drives the desktop C API directly for startup, timing, input, video and audio. It uses handoff boot, English console language, color-console mode and desktop silicon timing. The desktop ROM-derived character-RAM compatibility fix is retained. No desktop Python or Qt frontend is bundled.

One deliberate internal coupling exists: saving reads the vendored machine's raw flash storage. Normal bus reads return status or chip-ID bytes during flash commands and cannot safely be used to snapshot persistent data. Recheck this access whenever refreshing `vendor/core/`.

## Command line

```powershell
& '.\dist\NgpCraftPackager.exe' --pack 'game.ngc' 'My Game.exe' --title 'My Game' --scale 4 --fullscreen | Out-Null
```

The pipe waits for the GUI executable to exit; check `$LASTEXITCODE`. Optional arguments:

- `--icon game.ico`, `--force` to replace an existing output.
- `--keys 87,83,65,68,74,75,32` for W/S/A/D/J/K/Space, in direction/A/B/Option order using Windows virtual-key codes.
- `--volume 65`, `--fit`.
- `--game-author "My Studio"`, `--game-copyright "Copyright 2026 My Studio"`.
- `--game-license-name "My license"`, `--game-license license.txt`.

## Verification

Python 3 is only needed for the integration/UI tests. Supply your own ROM:

```powershell
ctest --test-dir build --output-on-failure
python tests/integration.py build/NgpCraftPackager.exe 'C:\games\homebrew.ngc'
python tests/options_ui.py build/NgpCraftPackager.exe 'C:\games\homebrew.ngc'
python tests/licenses_ui.py build/NgpCraftPackager.exe 'C:\games\homebrew.ngc'
```

Native tests cover both flash chips, RTC round-trips, corrupt/truncated/wrong-ROM saves, unpadded homebrew ROMs, and saving while flash is busy. GDI tests cover offscreen presentation, scaling, margins, orientation and resource lifetime.

Integration tests cover resources, Unicode paths, icons, export failures and destination preservation, standalone execution for 180 frames, video/audio/input, save reload and the packager GUI. UI tests use a private, non-visible Windows desktop for settings, preferences and credits dialogs. Internal `--smoke report.txt` and `--ui-test report.txt` modes use isolated save directories beside the report. Smoke mode produces a PPM image and does not open an audio device.

## Scope and license

Windows 10/11 x64; runtime DLL dependencies are Windows system libraries. Audio uses stereo PCM through WinMM, video uses GDI, and controllers use XInput. Gamepad remapping, DirectInput, save states and multiplayer are not exposed. Test real audio, physical controllers and the final game on another PC before distribution. Executables are not digitally signed.

The NgpCraft sources and HLE firmware use the [MIT license](LICENSE). Game content retains its own license. Compiler/runtime components retain their respective licenses; removing an emulator adapter does not change the toolchain's terms. No proprietary console BIOS dump is shipped.
