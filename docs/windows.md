# Gusto for Windows

This package contains a 32-bit Windows ASI plugin for the supported May 10,
2013 RoF2 client. It can be redistributed with its included licenses and
notices. No EverQuest executable, graphics DLL, audio, user settings or maps
are included. Existing game map files are used from the game's `maps` folder.

## Install on Windows 10 / 11

1. Close EverQuest. Extract the entire Windows release ZIP to a temporary folder.
2. Keep your existing compatible **32-bit ASI loader** enabled. If this is a
   fresh install without ASI loading, install the Win32/x86 version of
   [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
   following its instructions. RoF2 loads DirectInput8; `dinput8.dll` is its
   usual proxy name. Preserve an existing proxy/mod DLL instead of overwriting
   it blindly. The loader is a separate prerequisite, not bundled here.
3. Copy `GUSTO.asi` beside `eqgame.exe`, or run the included installer from
   PowerShell 5.1 or newer:

   ```powershell
   .\Install-Gusto.ps1 -GameDirectory 'C:\Games\EverQuest'
   ```

   The installer checks the client/graphics/plugin hashes and backs up an old
   GUSTO.asi. It does not replace a loader, change execution policy, edit the
   registry, copy somebody else's settings, or install Linux drivers. If local
   script policy blocks it, use the manual copy method; no policy change is
   necessary to install the ASI.
4. Start EQ and open `/gusto`. Controller input defaults off on a fresh machine.
   Enable it, select a controller input and choose a keyboard key/shortcut or
   mouse button. Set EverQuest's **Options → Keys** to use those same keys.

The plugin exports `InitializeASI` for standard ASI loaders and retains its
Miles ASI provider startup path. Initialization still validates the exact
supported executable before installing hooks. Other EQ versions are rejected.
The required executable/graphics hashes are in BUILD.json and the installer.

## Runtime and portability

Gusto's C++ and compiler runtimes are statically linked. The package's plugin
import table is audited to contain only Windows system libraries: KERNEL32,
USER32, GDI32, bcrypt and msvcrt. It needs no Python, Blender, compiler, Linux
library, MinGW runtime DLL or separately installed C++ redistributable. Its
D3D9 device comes from the game's existing graphics system. Normal EQ graphics
prerequisites and a compatible ASI loading path remain necessary.

XInput DLLs are loaded dynamically with version fallbacks. Generic joystick
support uses Windows WinMM; both are native Windows APIs. Keyboard shortcuts use the validated EQ keybinding handler; mouse input uses
Windows SendInput. Controller input applies only while the game is the
foreground window. Use the same permission level for the game and its loader.

Settings are per user at `%LOCALAPPDATA%\Gusto\hud.ini`; there are no hardcoded
Linux paths. Device calibration survives the upgrade. Old numeric game-command
bindings are not reinterpreted as keys: keyboard assignments start with the
new defaults, while legacy mouse-click assignments are preserved.

A clean-prefix Wine smoke test verifies DLL loading, exports and rejection of
an unsupported host, and tests cover the native EQ keybinding adapter, physical keyboard capture and Windows mouse input APIs.
This is not a claim of testing on a physical Windows machine; live Windows EQ
integration still needs confirmation.
