# RoF2 UI overrides

`Gusto/EQUI_BankWnd.xml` and `Gusto/EQUI_BigBankWnd.xml` define 32 regular
and 8 shared bank slots in both native bank layouts.
`Gusto/EQUI_Inventory.xml` removes the Consume Krono button and Krono slot.

With EQ closed, back up your current UI and copy these files into a custom
RoF2 UI folder, then select that UI in game. These overrides use the local
RoF2 default layout. They have not been installed into the game directory.

UI slot definitions do not increase native client or server inventory capacity.
Gusto installs the native 32/8 client hooks; the server must also support all
requested slots before they are usable. See [bank hooks](../../docs/bank.md).
The mercenary roster capacity is not controlled by these XML files; changing
it from 6 to 100 requires a separate client/server implementation.
