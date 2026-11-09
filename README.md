# Gusto

Gusto adapts EverQuest Zeal features for the **Rain of Fear 2 (RoF2)** client
through a dedicated compatibility layer. It supports Windows and Wine with a
compatible 32-bit ASI loader.

Gusto is an independent community project and is not affiliated with or
endorsed by Daybreak Game Company.

## Download and install

[Download the tagging preview](https://github.com/datacurve1/gusto/raw/refs/heads/master/releases/GUSTO-RoF2-Tagging-Preview.zip)
([SHA256](releases/GUSTO-RoF2-Tagging-Preview.zip.sha256)).

1. Fully exit EverQuest.
2. Back up the existing `GUSTO.asi` in your game directory.
3. Extract the preview ZIP and copy its `GUSTO.asi` beside `eqgame.exe`.
4. Ensure your compatible 32-bit ASI loader is installed and enabled.
5. Restart EverQuest using your normal Windows or Wine launcher.

Replacing the file while EverQuest is running does not update the loaded
plugin. Fully exit and restart after every update.

The older [GUSTO 1.0 package](releases/GUSTO-1.0-RoF2-Windows-Wine.zip) does
**not** include the new tagging commands.

## Status and compatibility

This is an experimental tagging preview. The supported client is **May 10
2013, 23:30:08 RoF2**; addresses and memory layouts are specific to that client.
Startup validates the executable before installing hooks.

**Native tag text and live 3D marker visibility have been confirmed in game.**
Automated protocol, geometry, channel-lookup, and safety tests pass. Server chat
interoperability and broader device-reset testing still need live confirmation.
Use a separate client installation for testing.

## Features

- Native Slow Turn Left, Slow Turn Right, and Toggle All Bags keybinds.
- NPC nameplate colors and attack-target brackets.
- `/tag` text labels with colored arrow, stop-sign, and paw geometry.
- Local, group, raid, and selected-channel tag exchange.
- `GUSTOTAG` outgoing messages and `Gt...` tagging channels.
- Exact tag targeting, append/remove/move operations, clear commands, and chat
  filtering.
- In-game diagnostics through `/tag status` and `/gusto version`.

## Tagging quick start

Target a living NPC, then add a tag for yourself. No group is required:

```text
/tag local ^R^Assist
```

For a stop sign or paw:

```text
/tag local ^S^
/tag local ^P^
```

Share tags with a group or raid:

```text
/tag gsay ^Y^Mez
/tag rsay ^R^Main assist
/tag rsgs Off tank
```

`rsgs` chooses raid, then group, then local.

### Shared tagging channel

All participants must use the updated Gusto build and the same channel:

```text
/tag join Gt123
```

Wait for EverQuest's join confirmation. Once joined, target an NPC and send:

```text
/tag chat ^R^Assist
```

Gusto reports `joined as /N` only after it finds the channel in the client's
joined-channel list. If it remains pending, run native `/join Gt123` and
`/list` to check the server's response. A working server chat service is
required for channel exchange.

Outgoing messages use `GUSTOTAG`; incoming `GUSTOTAG`/`GT` and legacy
`ZEALTAG`/`ZT` messages are accepted. Original Zeal clients do not understand
Gusto's new outgoing header. Tag channels must begin with `Gt` and contain only
letters and digits; matching is case insensitive.

### Manage tags

```text
/tag target Assist
/tag clear
/tag local clear
/tag off
/tag on
/tag status
```

`/tag clear` clears the selected living target, or all tags when no living
target is selected. `/tag local clear` always clears all local tags.
`/tag status` reports tracked tags, renderer stages, frame/draw counts,
projected marker coordinates, and channel lookup details.

Settings are currently held in memory for the session. Zeal's font renderer,
target-window tooltip, UI settings controls, and INI persistence are not part
of this port.

See [RoF2 tagging](docs/tagging.md) for the complete command reference,
verified offsets, limitations, and live acceptance checks.

## Slow turn and bag keybinds

Slow Turn Left, Slow Turn Right, and Toggle All Bags appear in the native
Options → Keys window. See [Slow Turn Keys and Toggle All Bags](docs/slow-turn-keys.md)
for the command mappings and verification steps.

## Build from source

Build as a **32-bit Windows DLL** with a MinGW-w64 cross-compiler. On Ubuntu:

```bash
sudo apt update
sudo apt install cmake ninja-build mingw-w64
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=cmake/mingw32-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The targets are `GUSTO.dll`, `gusto_safety_tests.exe`, and
`gusto_tagging_tests.exe`. Install the built `GUSTO.dll` as `GUSTO.asi` beside
`eqgame.exe`, then fully restart the game.

The current CMake targets include the adapted tagging code and do not require
a separate Zeal checkout. The full Zeal/MacroQuest reference sources are not
included. Developers obtaining those sources for comparison may place them
under `source/Zeal-1.4.8/Zeal`; `source/` is ignored by Git.

### Verification

Audit your client installation:

```bash
python3 tools/verify_rof2.py /path/to/rof2/eqgame.exe
python3 tools/verify_tagging.py /path/to/rof2
```

Run the Windows test executables directly on Windows, or through Wine. Portable
tagging tests can also run on Linux:

```bash
g++ -std=c++20 -fsanitize=address,undefined -g -Iinclude \
  test/tagging_tests.cpp src/client/rof2/tag_state.cpp \
  src/client/rof2/tag_models.cpp -o /tmp/gusto-tagging-tests
ASAN_OPTIONS=detect_leaks=0 /tmp/gusto-tagging-tests
```

The suite covers `GUSTOTAG` round trips, legacy reception, malformed messages,
field operations, channel selection, and model geometry. It also exercises
10,000 malformed-message inputs. These checks do not replace live rendering,
network, and device-reset testing.

## Hook behavior

Optional hook failures do not by themselves abort initialization. Code patches
check expected instructions and temporarily suspend other client threads;
those threads are resumed before their handles are released.

Runtime hooks write no diagnostic log files. Rendering diagnostics are
available in game through `/tag status`.

## Repository layout

- `include/gusto/` — Gusto headers
- `src/client/rof2/` — client compatibility, hooks, and tagging implementation
- `test/` — safety, protocol, channel, and geometry tests
- `tools/` — executable and tagging-address audits
- `docs/` — command references, technical details, and manual checks
- `releases/` — downloadable packages
- `cmake/` — cross-compilation configuration

## License

Gusto is distributed under **GPL-3.0-only**. See [LICENSE](LICENSE).
Adapted Zeal components retain their MIT attribution; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
