# Expanded RoF2 bank

Gusto installs native bank hooks for **32 regular and 8 shared bank slots** on
its supported May 10, 2013 RoF2 client. `/gusto bank` reports whether those hooks
installed. Restart the game after replacing GUSTO.asi; a late injection with an
existing character or bank window deliberately leaves expansion disabled.

Install both `assets/ui/Gusto/EQUI_BankWnd.xml` and `EQUI_BigBankWnd.xml` in the
active UI skin. RoF2 uses either layout depending on its settings and inventory.
Older skins with fewer controls can load, but cannot display all added slots.

The plugin expands the bank window allocation from 0x2c8 to 0x368 bytes. Original
fields keep their offsets. New personal/shared slot-pointer arrays live at
+0x2c8/+0x348, avoiding the labels and buttons immediately after the original
arrays. Hooks also change new character bank container sizes, personal-bank
cleanup loops, shared visibility and refresh loops, lookup/movement fallback
counts, and legacy bag-slot decoding. Missing controls in older skins are skipped
in the shared show/hide loops. All code anchors are checked before any patch is
published, with other threads suspended throughout installation.

**The server must also accept and persist the expanded slots.** Gusto changes the
client, not the server's inventory rules. A server still limited to 24/2 can reject
moves into additional slots. The local `akk-stack/code/common/patches/rof2_limits.h`
was observed to specify 24/2 during implementation; its running-server status has
not been verified.

## Validation

Build `gusto` and `gusto_bank_tests`. Run the test executable with the path to the
supported `eqgame.exe`, on Windows or in an isolated Wine prefix. It privately
maps the client for native instruction tests; it does not modify eqgame.exe on
disk, launch EQ, connect to a server, or access character inventory.

Live checks still require a server configured for 32/8: deposit and withdraw at
personal indices 24 and 31 and shared indices 2 and 7, open bags there, and verify
contents after zoning and relogging. Confirm inventory persistence on the server.
