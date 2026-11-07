# Gusto

Gusto is an experimental RoF2 compatibility project for the EverQuest Zeal
client modification framework.

The goal of Gusto is to adapt Zeal functionality for the EverQuest Rain of
Fear 2 (RoF2) client through a dedicated compatibility layer, client
initialization, diagnostics, and client hooks.

Gusto is an independent community project and is not affiliated with or
endorsed by Daybreak Game Company.

## Project Status

Gusto is under active development. Client addresses, function signatures,
memory layouts, and hooks are specific to the tested RoF2 executable.

Compatibility with other client versions is not guaranteed. Use a separate
client installation and keep backups of your files.

## Zeal Source Prerequisite

The Zeal source code is intentionally NOT included in this repository.

To prepare the development tree:

1. Obtain the Zeal source separately.
2. Place the source tree at:

   `source/Zeal-1.4.8/Zeal`

3. Keep the separately obtained source out of commits to this repository.

The `source/` directory is ignored by Git. Gusto-specific compatibility code
and project files are maintained in this repository.

## Build Environment

Gusto is built as a 32-bit Windows DLL using a MinGW-w64 cross-compiler.

On Ubuntu, install the build tools:

```bash
sudo apt update
sudo apt install cmake ninja-build mingw-w64
```

Configure the project:

```bash
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=cmake/mingw32-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release
```

Build:

```bash
cmake --build build
```

The CMake project defines the `GUSTO.dll` library and the
`gusto_safety_tests` executable target.

## Slow Turn Keys

Gusto adds Slow Turn Left, Slow Turn Right, and Toggle All Bags to the native
Options → Keys window. The RoF2 command mapping, bag-toggle behavior, speed
patches, and manual verification steps are documented in
[Slow Turn Keys and Toggle All Bags](docs/slow-turn-keys.md).

## Diagnostics

The current InterpretCmd diagnostic hook records intercepted command text to:

`C:\gusto_interpretcmd.log`

It also provides the `/gusto version` command.

The diagnostic log may contain commands entered in the client. Review and
protect this file accordingly. Diagnostic behavior may change during
development.

## Repository Layout

- `include/gusto/` — Gusto headers
- `src/client/rof2/` — RoF2 compatibility and hook implementation
- `test/` — safety tests and test support
- `tools/` — development and verification utilities
- `docs/` — technical documentation and address audit
- `cmake/` — cross-compilation configuration

## License

Gusto is distributed under the GNU General Public License, version 3 only
(GPL-3.0-only). See [LICENSE](LICENSE).

Third-party components retain their applicable copyright and license
notices. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
