# Gusto 1.1.0-beta.1

Gusto brings Zeal-inspired features to the **EverQuest Rain of Fear 2 (RoF2)**
client, with a Hailstone-style minimap and native Windows/Wine controller support.
It is a 32-bit ASI plugin for the **May 10, 2013, 23:30:08** client.

This beta follows Gusto 1.0 and replaces the numbered HUD development previews.
Gusto is an independent community project, not affiliated with or endorsed by
Daybreak Game Company.

## Download and install

- [Windows package](https://github.com/datacurve1/gusto/raw/refs/heads/master/releases/GUSTO-1.1.0-beta.1-RoF2-Windows.zip) · [SHA256](releases/GUSTO-1.1.0-beta.1-RoF2-Windows.zip.sha256)
- [Windows/Wine package with artwork, previews and documentation](https://github.com/datacurve1/gusto/raw/refs/heads/master/releases/GUSTO-1.1.0-beta.1-RoF2-Windows-Wine.zip) · [SHA256](releases/GUSTO-1.1.0-beta.1-RoF2-Windows-Wine.zip.sha256)

1. Fully exit EverQuest and back up your existing `GUSTO.asi`.
2. Extract a package and copy `GUSTO.asi` beside `eqgame.exe`.
3. Install or enable a compatible **32-bit ASI loader** if you do not already use one.
4. Start EverQuest through your normal Windows or Wine launcher.
5. Enter `/gusto version` to verify the version and `/gusto` to open settings.

The Windows package also includes `Install-Gusto.ps1`, which checks the client
and plugin hashes and backs up an existing plugin. See [Windows installation](docs/windows.md).
Replacing an ASI while the game runs only stages the update; restart to load it.
The plugin uses Windows system DLLs and embeds its meshes. Players do not need
Blender, Python, a compiler, or a separate GCC/C++ runtime installation.

### Windows startup and release builds

Use the supported 32-bit RoF2 client and put `GUSTO.asi` beside the
`eqgame.exe` your shortcut actually launches. With EverQuest closed, back up
the installed ASI before replacing it. Use the existing compatible 32-bit ASI
loader, restart EverQuest, then enter `/gusto version` to verify the plugin.

If Gusto starts but reports a client-validation failure, run
`python tools/verify_rof2.py "C:\path\to\rof2\eqgame.exe"` from the source
checkout. A passing result confirms the file on disk. The older validator
can still fail at startup with `UnexpectedImageBase` when Windows relocates
the executable. In a Windows reproduction, the verified client loaded at
`0x00900000` instead of its preferred `0x00400000`. A local build initialized
successfully after allowing the mapped PE header's image base to match
either the preferred base or the actual module address. The client version,
image size, and SHA-256 checks still apply. This startup test did not require
changing Defender or Windows exploit-protection settings.

For a native Windows build, put CMake, Ninja and the 32-bit MinGW-w64
toolchain on PATH, then run from the source checkout:

```powershell
cmake -S . -B build-win32 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw32-toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-win32 --target gusto
```

The output is `build-win32/GUSTO.dll`. Distribute it as `GUSTO.asi` using
the Windows packaging script described below. For a public update, commit
the source fix first, bump `VERSION`, then rebuild and test that version on
Windows. Publish the versioned Windows ZIP and its matching SHA-256 as
release assets, and update the download links above. The download links
in this README still point to the existing beta until a fixed package is
published.

## Features

| Feature | Included controls and behavior |
| --- | --- |
| Minimap | Hailstone Midnight-style circular map, zone geometry and labels, heading rotation, height filtering, zoom and adjustable arrow sizes. Player arrows use distinct colors; pets use light brown arrows. |
| External map | Independent resizable map window that can be dragged to another monitor. |
| Combat floats | Damage, misses, large hits and healing floats with filters and size controls. |
| Overhead bars | HP, mana and stamina controls with independent sizes/colors and optional RPG frames. NPCs/pets show HP only; other players' mana is limited to known group data. |
| Nameplates | Con colors, pink target highlighting, brackets, attack flashing, and EverQuest-style pet owner labels. |
| Tagging | Native labels and 3D arrow/stop/paw markers; local, group, raid and channel exchange using `GUSTOTAG` messages and `Gt...` channel names. |
| Death / feign death | Original extruded ghost and jester meshes; rising/fading effects, color/size controls, and native skeleton laugh for player feign death. |
| Camera | Optional orientation smoothing, FOV, zoom and native camera-mode controls. |
| Gamepad | XInput and generic joystick/WinMM backends for Windows and Wine; keyboard shortcuts, held stick directions, mouse movement and mouse-click bindings. |
| Named pipes | Outbound Zeal-compatible JSON snapshots through `gusto_PID`, with an optional `zeal_PID` alias. |
| Native keybinds | Slow Turn Left, Slow Turn Right and Toggle All Bags in EverQuest Options → Keys. |

The settings menu has Map, Combat, Bars, Nameplates, Camera, Tags, Death / FD,
Gamepad and Pipes pages. Slow turn and bag keybinds use the native game options.
Settings persist in `%LOCALAPPDATA%\Gusto\hud.ini` inside the active Windows/Wine
profile. Updating the plugin preserves those settings.

Hold a single click on the minimap to preview the larger map. Double-click and
hold to move the minimap. Ordinary menu clicks do not take ownership of the cursor.
Zone `.txt` map files belong in the game client's `maps` directory; maps are not
bundled with Gusto.

## Controller setup

1. Open **Gamepad** and enable your controller; start with the Auto backend.
2. Select the mapping wizard and press the controller input to assign.
3. Press a keyboard key/shortcut **or click left, right or middle mouse**.
4. Release the captured input, close the menu, and test in the game.

Keys use your existing EverQuest **Options → Keys** bindings. A stick holds its
key while tilted and releases near center. A stick assigned to mouse movement
is dedicated to the pointer. Escape cancels mapping.

Controller output pauses for menus, text editing, lost focus and disconnection.
Return the sticks/buttons to neutral after a pause to rearm. Windows and Wine
use the native EQ keybinding handler; ordinary focused UI buttons do not block
movement. Linux controllers must first be recognized by the host and exposed
to the game's active Wine prefix. See [controller setup and troubleshooting](docs/gamepad.md).

## Useful commands

```text
/gusto
/gusto version
/gusto status
/gusto gamepad status
/minimap on
/minimap external
/fcd on
/camera on
/tag status
```

See the detailed guides for complete commands and settings:

- [HUD, map, combat, bars and camera](docs/hud.md)
- [Controller mappings and Linux/Wine troubleshooting](docs/gamepad.md)
- [Tagging and chat channels](docs/tagging.md)
- [Death/feign-death effects and artwork](docs/life_effects.md)
- [Named pipes and example reader](docs/pipes.md)

## Compatibility and beta status

Only the supported RoF2 build is targeted. Startup validates the executable
before installing hooks. `tools/verify_hud.py` checks the expected client and
graphics DLL hashes plus the native code anchors used by these features.
MacroQuest and compatible existing entry-JMP hooks can be chained; unrecognized
patches are rejected.

Live testing has confirmed 3D tag visibility, controller keyboard delivery,
sustained stick movement, button remapping and mouse-click mapping under Wine.
The latest NPC/pet bar change draws in the world-complete render stage before
the label/UI pass; automated occlusion tests pass, but **live wall/tree occlusion
still needs confirmation**. Physical Windows hardware, server-channel
interoperability and broader device-reset coverage also need further testing.

Some resource values and pipe fields depend on what the client knows. The pipes
export snapshots; they do not accept inbound game commands. See the guides for
field coverage and implementation limits.

## Build from source

Requirements: CMake 3.20+, a C++20-capable 32-bit Windows toolchain, and Windows
SDK/MinGW headers. On Linux with MinGW-w64:

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw32-toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

The output is `build/GUSTO.dll`; packages rename it to `GUSTO.asi`. `VERSION` is
the source of the semantic version used by CMake, `/gusto version`, Windows file
metadata and release filenames.

The repository includes safety, tagging, HUD, controller, life-effect and pipe
tests, plus opt-in Wine/D3D9 preview and runtime smoke targets. For the graphical
and runtime checks:

```sh
cmake --build build --target gusto_hud_preview gusto_life_preview gusto_gamepad_preview gusto_windows_runtime_smoke -j4
```

Run Windows test executables on Windows or in an **isolated** Wine prefix. The
HUD preview uses synthetic data and requires a working D3D9 display. Full source,
original Blender/SVG artwork, runtime meshes, packaging tools and third-party
notices are included; proprietary game executables and map packs are not.

After building and generating the preview PNGs, package the current version:

```sh
python3 tools/package_hud.py
python3 tools/package_windows.py
```

## License and credits

Gusto and its original ghost/jester artwork use the [MIT license](LICENSE).
Thanks to Zeal, MacroQuest/eqlib, Hailstone and nlohmann/json. Attribution and
upstream licenses are preserved in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
