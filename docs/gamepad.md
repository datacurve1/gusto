# Controller keyboard and mouse bindings

Open `/gusto gamepad` or select **Gamepad** in `/gusto`. Click
**Map: press controller, then keyboard key**:

1. Press the controller button/direction you want to map.
2. When the prompt changes, press the physical keyboard key or shortcut, or click the left, right, or middle mouse button.
3. Release the keys. Gusto saves the assignment and shows the result.

Ctrl, Alt and Shift combinations are captured. Escape or Cancel preserves the
old assignment. The captured press is consumed so it does not also trigger a
game action. The manual key picker and direct mouse-click assignment remain
available. Controller calibration is separate from this mapping wizard.

These are **keyboard bindings**, not native game-command IDs. For example,
map a stick direction to W and a button to Ctrl+1, then assign W and Ctrl+1 to
the actions you want in EverQuest's **Options → Keys**. Gusto sends a scan-code/modifier KeyCombo to EQ's own KeypressHandler, which
resolves your current Options / Keys bindings. It does not select a native
game command ID or rely on SendInput reaching EQ's buffered DirectInput path.

Direct **Assign LEFT / RIGHT / MIDDLE mouse click** buttons remain available.
Pressing holds the assigned key(s) or mouse button; releasing sends key/button
up. Two controller inputs sharing a key or modifier keep it down until both
release. Modifiers press before the shortcut key and release after it. Keys
release on disconnect, focus loss, menu/typing ownership, disable or remapping;
controller input must return to neutral before rearming. Already held physical
keys are not claimed by the controller; avoid holding the same key through
both devices simultaneously.

Examples:

```
/gusto gamepad bind 1 Space
/gusto gamepad bind 2 Ctrl+1
/gusto gamepad bind 3 MOUSE_RIGHT
/gusto gamepad bind 4 UNBOUND
```

Default examples: A/Cross = Space, B/Circle = C, X/Square = E, Y/Triangle = Q,
bumpers = T/Tab, Back = I, Start = F9, stick clicks = Home/5, D-pad = 1–4,
triggers = Insert/Delete, left stick = arrow keys, right stick = A/D/PageUp/
PageDown. Match them to your own EQ key settings; defaults are not inferred
from your client. Mouse-stick mode overrides that stick's direction bindings.

**Controller / stick / mouse settings** contains backend, slot, dead zone,
mouse stick (off/left/right), mouse speed, invert Y, generic axis layout and
raw-button calibration. The Auto backend tries XInput slots 1–4, then WinMM
joystick slots 1–16. Mouse control works in Gusto and game menus while keyboard
bindings pause. Pointer input requires the pointer inside the foreground game
client. Capturing a controller button does not replay its assigned click.

Assignments persist as `BindingFormat=2` and `KeyBinding0` through
`KeyBinding23` in `[Gamepad]` in `%LOCALAPPDATA%\Gusto\hud.ini`. Modifiers use
Shift `0x100`, Ctrl `0x200`, Alt `0x400` with the Windows virtual key in the low
byte; mouse actions are `0x800`–`0x802`. Old `BindingN` command IDs are ignored
on migration except mouse IDs 479–481, which retain their corresponding click.
Device calibration is preserved. Restore default keyboard bindings affects
only assignments. No other user's INI is shipped in the Windows package.


Stick directions hold their mapped keyboard key continuously while tilted; they
do not send repeated taps. The configured deadzone starts the hold; moving back
inside 70% of that deadzone releases it. This prevents small fluctuations near
the activation threshold from interrupting movement. Diagonals can hold two
keys. Center the stick after chat, menus, focus loss or reconnection to rearm.
A stick assigned to the mouse remains dedicated to pointer movement. Mouse
capture transfers no longer cancel controller keyboard holds or the mapping
wizard; actual keyboard focus loss still releases the keys.

The two-stage mapping wizard also accepts left, right, or middle mouse clicks
at the keyboard-key step. Click once, release, then close the menu to use the
assigned controller button. Mapping consumes the click and waits for its
release before resuming gameplay. Physical DirectInput mouse events and window
mouse messages both support this on Windows and Wine. Escape cancels mapping.

## Linux / Wine controllers

Start with **Backend: Auto** and slot 1. If the device is only visible as a
joystick, Auto falls back to Generic joystick. You can force that backend to
select another device or avoid XInput. `/gusto gamepad status` reports the
active backend. Select the same controller ID shown in `wine control joy.cpl`
inside the game's Wine prefix. If Wine cannot see a device, Gusto cannot read it.
No system-wide device permissions or Wine configuration are changed by Gusto.

Generic joystick has three configurable axis layouts:

| Layout | Left stick | Right stick | Triggers |
| --- | --- | --- | --- |
| Z/R sticks | X/Y | Z/R | Joystick buttons 11/12 by default |
| U/R + Z trigger | X/Y | U/R | Positive/negative half of Z |
| Z/R + U/V triggers | X/Y | Z/R | Separate U/V axes |

On **Input**, choose a face button, bumper, trigger or D-pad direction, then
set **Joystick button** to the device's physical button number (1–32).
POV / none uses the POV hat for D-pad inputs and leaves other buttons unbound.
POV diagonals activate both directions. XInput ignores these joystick-specific
settings. Right-stick movement, mouse control, held clicks and keyboard
bindings work through both backends. A backend change releases held inputs
and requires a neutral controller before rearming.

Tests use simulated XInput and WinMM devices through the actual polling code,
plus real Windows mouse events under Wine. Hardware-specific mappings and
live character movement still need confirmation with your controller.

Wine backend reference: [Wine WinMM joystick implementation](https://raw.githubusercontent.com/wine-mirror/wine/master/dlls/winmm/joystick.c). Gusto calls the public WinMM APIs; it does not embed Wine source.

## Linux sees a receiver, but Wine detects no controller

Run `python3 tools/check_controller_linux.py` on the actual Linux host. A USB
receiver alone does not guarantee a usable input device. In the observed
8BitDo `2dc8:3106` case, its vendor-specific USB interface was **UNBOUND** and
`xpad` was not loaded, so neither XInput nor Wine joystick polling could see it.
An existing module-triggered udev rule registered its USB ID only after xpad
loaded; it did not itself cause xpad to load on receiver insertion.

Load the installed driver in a host terminal:

```sh
sudo /sbin/modprobe xpad
```

If the receiver remains unbound after the driver loads, register its ID for the
current boot:

```sh
printf '2dc8 3106\n' | sudo tee /sys/bus/usb/drivers/xpad/new_id
```

These commands need the host administrator password and apply only to that
specific receiver/driver. Re-run the read-only diagnostic and then open
`wine control joy.cpl` using the game's Wine binary and prefix. Confirm the
controller responds there before testing Gusto. A loaded module or receiver
presence by itself does not confirm that button events work. No kernel driver,
udev rule, global permission or Wine registry change is made by Gusto.


For the observed 8BitDo receiver, `tools/99-8bitdo-xpad.rules` preserves the
existing module-triggered USB-ID registration and adds loading xpad when that
receiver is inserted or discovered at boot. Installing it requires the host's
administrator password:

```sh
sudo install -m 644 tools/99-8bitdo-xpad.rules /etc/udev/rules.d/99-8bitdo-xpad.rules
```

It replaces that specific existing rule file; review it first if you have added
other custom rules to the same file. The current controller does not need to be
unplugged after it has bound successfully. Normal boot loads the installed rule.
The native `tools/controller_probe.cpp` utility checks actual XInput slots and
Wine joystick IDs without loading Gusto. It stops at the first working legacy
joystick by default; passing any argument scans all 16 legacy IDs. Some Wine
builds are very slow when querying absent joystick IDs.

The observed game's desktop launcher uses `~/.wine-eq` even though its game
files live in `~/.wine/drive_c/rof2`. Use the launcher's prefix for diagnostics
and settings. Its controller was detected at API index 0, displayed as slot 1
in Gusto; a saved `Slot=3` instead selects slot 4.


For this Ultimate receiver, `2dc8:3109` is its **idle / no controller connected**
identity; it is not another gamepad that should be forced onto xpad. Wake/pair
the controller and remove it from its dock if needed. The connected receiver
was observed as `2dc8:3106`, with Linux `js0`, XInput index 0 and Wine joystick
index 0. The controller later disconnected and the receiver returned to `3109`.
See the [driver project's receiver-state report](https://github.com/paroj/xpad/issues/228).
The corrected Gusto settings in the actual `.wine-eq` prefix are `Slot=0`
(UI slot 1) and `Backend=0` (Auto), with a timestamped INI backup.


**Controller / stick / mouse settings** opens device calibration separately
from the button assignment page. It retains backend, controller slot, mouse
stick/speed/invert, dead zone, Wine axes and raw-button controls. Restore default
button bindings resets assignments while preserving device/mouse settings.
The picker applies a selection immediately and saves it. Escape cancels the
picker. Selecting a stick direction that is currently owned by Mouse stick
shows that its game binding will remain inactive until mouse-stick mode is off.
Press-to-select listens for a new input edge, so release an already held button
before pressing the button you want to select.


## Native key bridge and capture diagnostics

The supported RoF2 executable uses `KeypressHandler::HandleKeyDown` at
`0x5594E0`, `HandleKeyUp` at `0x559800`, and manager pointer `0xE639B0`.
The four-byte KeyCombo layout is alt/control/shift/DirectInput scan code,
verified against the local MacroQuest layouts and native callers. Extended
keys use their DirectInput scan code. No game command ID is stored or selected.
The verified `ProcessKeyboardEvents` entry at `0x5FDA30` is intercepted only to
consume physical buffered keyboard input during capture and key release.
Physical key-up events are forwarded so pre-existing input is not left held.

Both key handlers and keyboard processing can retain an existing compatible
entry-JMP hook (for example MacroQuest's). Single entry jumps are relocated
when chained; unrecognized patch formats remain rejected. Controller polling
also runs from HUD frames, so keyboard capture does not depend solely on the
mouse processing callback.

`/gusto gamepad status` reports bridge readiness, the last key gate, native
press/release counts, last shortcut and whether the key handler accepted it.
The earlier release's ordinary OS SendInput key events worked in a test window
but did not reliably enter EQ's buffered input path. Tests now exercise the
actual native adapter with a mock EQ KeypressHandler, validate KeyCombo byte
layout, modifiers, extended scans, paired release and no duplicate OS key event.
Physical DirectInput capture is tested through a mocked keyboard device using
the production drain/capture helper. Live EQ operation still needs confirmation.


### RoF2 chat gate correction

The supported client's SetChatMode(1) clears chat edit focus; mode 0 sets it.
Controller keyboard input pauses when chat mode is not 1 or the native window
manager has a focused editable control (`FocusWindow`, offset `+0x64`, confirmed by
native `GetFocusWnd` at `0x875eb0` and the bundled MacroQuest layout).

Manager mode 20 is **not** a reliable chat indicator. Native HandleKeyboardMsg
sets it for every ordinary non-modifier key-down, including movement, then
clears it on key-up. Preview14 live traces caught this self-canceling sequence:
Up down, typing gate 16, Up up, stick still fully forward. Preview15 removes
manager mode and held Enter from the chat gate. The regression mock now sets
mode 20 on native key-down and verifies that 1,800 polls preserve the held keys
with no focused UI, while actual UI focus releases them.

The keyboard diagnostic `gate` value reports combined blocking reasons: 1 game
not ready, 2 no game window, 4 focus elsewhere, 8 Gusto menu, 16 chat/UI focus
guard, 64 mapping capture, 128 disconnected, 256 controller disabled. Zero
means none of these gates is blocking input.

Preview16 narrows the focused-control check to runtime types WRT_EDITWND (2)
and WRT_EDITHOTKEYWND (8). Ordinary buttons (17) can retain UI focus after
remapping and must not block the controller. The runtime type list at +0x44
is decoded using the native CXWnd::IsType layout with bounded, checked reads.
A live blocked state had normal chat mode, completed capture, and focused
runtime types [0,17]; regression fixtures now cover that exact state, both edit
types, null focus and malformed type arrays.
