# RoF2 Slow Turn Keys and Toggle All Bags

Gusto exposes **Slow Turn Left** and **Slow Turn Right** in the client's native
Options → Keys window. Their primary and alternate bindings use the client's
KeyMaps persistence. No separate keybinding window is used.

## Command mapping

Gusto uses unused Options/keymap command IDs 484 and 486:

| Gusto command | ID | Native RoF2 movement command |
| --- | ---: | --- |
| Slow Turn Left | 484 | 5 (Left) |
| Slow Turn Right | 486 | 4 (Right) |

The native dispatcher confirms the mapping: command 5 sets the left-turn state
at `0xDE0D9C` and clears the right-turn state; command 4 sets the right-turn
state at `0xDE0D98`. The custom dispatcher wrapper maps each Gusto command to
that native movement command for both key-down and key-up events.

When Enter activates AutoRun or chat takes keyboard focus, Gusto latches held
movement and slow-turn inputs. It dispatches their releases on the next
movement-key press. The latch release path uses the same Left=5 and Right=4
mapping as the initial key-down.

## Slow speed and AutoRun mask fix

Slow speed is enabled while either slow-turn key is held. Gusto temporarily
changes five verified immediate operands in the RoF2 movement update routine:

| Address | Original | Slow | Purpose |
| --- | ---: | ---: | --- |
| `0x52BF46` | `0x08` | `0x04` | Turn-counter clamp (`EBX`) |
| `0x52BF4C` | `0xFB` | `0x00` | Secondary clamp (`EDI = EBX`) |
| `0x52C0DC` | `0x0C` | `0x04` | Left-turn clamp |
| `0x52C3C8` | `0x0C` | `0x04` | Right-turn comparison |
| `0x52C3CC` | `0x0C` | `0x04` | Right-turn clamp |

The same `EBX` value is also used as a bit mask later in the movement routine.
At `0x52C263`, the original `test [0xDDF720], BL` must test AutoRun bit
`0x08`. During slow turning `BL` is `0x04`; with Forward and AutoRun active,
the state can contain `0x04` even when the right-turn flag is clear. That
incorrectly enters the right-turn calculation while Slow Turn Left is active.

Gusto validates the original 12 bytes at `0x52C263` and replaces the test and
conditional branch with a small stub. The stub tests the fixed AutoRun mask
`0x08`, then follows the original destinations:

- Set: continue at `0x52C26F`.
- Clear: continue at `0x52C3E2`.

The stub does not change registers or the stack. The patch is included in the
installer's existing validate-and-apply transaction. This preserves the
AutoRun decision while leaving the slow-turn clamp values active.

## Build and manual verification

Build the 32-bit Windows DLL with the repository's configured build tree:

```bash
cmake --build build --target gusto -j2
```

The Forward + Slow Turn Left + Enter/AutoRun case was tested in the RoF2
client. Before the mask fix, the trace showed Forward active, AutoRun state
`0x05`, left-turn state set, right-turn state clear, and the right counter at
the slow clamp. After the mask fix, the user confirmed that the combination
turns left as expected.

Useful in-game checks after changes to this code:

1. Confirm both entries appear in Options → Keys.
2. Assign primary and alternate bindings and restart to check persistence.
3. Hold each slow-turn key alone and with Forward.
4. Hold Forward + Slow Turn Left, activate AutoRun with Enter, and verify it
   keeps turning left.
5. Repeat for Slow Turn Right and verify it keeps turning right.
6. Release each key and verify normal turn speed returns.
7. Confirm typing in chat does not trigger either slow-turn command.

## Toggle All Bags

Gusto also adds **Toggle All Bags** to Options → Keys at command ID 485. Set a
primary or alternate key there; the binding is saved through the client's
KeyMaps mechanism. Pressing it opens closed bags in the character's inventory.
When all occupied inventory bag slots are already open, it closes those bags.
It does not close world containers.

The implementation follows MacroQuest's RoF2 inventory layout: `pinstLocalPC`
(`0xDD261C`), `pinstCContainerMgr` (`0xD1FD84`), the current profile and
inventory containers, and inventory bag slots 23 through 32. It calls the
client's `OpenContainer` and `CloseContainer` routines (`0x65CAE0` and
`0x65D2F0`). To distinguish bags from ordinary items, Gusto calls the RoF2
item's virtual `GetItemDefinition` accessor and checks that its type is
`ITEMTYPE_PACK`; the legacy item-definition pointer is null for inventory
items in the tested client. Gusto validates the native function signatures
during initialization and ignores key-down events while chat owns keyboard
focus.

To verify in game, assign a primary and (optionally) alternate key to Toggle
All Bags. With some bags closed, press the binding and confirm it opens the
remaining inventory bags without opening ordinary items as containers. Press
it again when all occupied inventory bags are open and confirm they close.
Also confirm the binding persists after restarting the client and that typing
in chat does not trigger it.
