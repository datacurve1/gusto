# RoF2 tagging

Gusto adapts Zeal 1.4.8's nameplate tagging protocol and its three procedural
models: the downward arrow, extruded octagon (stop sign), and extruded paw.
The models retain Zeal's vertex and triangle-strip geometry. Rendering is a
Gusto Direct3D 9 implementation; Zeal's Direct3D 8 renderer is not loaded.
The MIT attribution is in the adapted files and `THIRD_PARTY_NOTICES.md`.

Basic native tag text and live 3D marker visibility have been confirmed in
game. Server chat interoperability and the broader acceptance checks below
still require live confirmation.

## Commands

Tags start enabled. Settings are held in memory for the current client session.
NPC tag text is prefixed to the native name sprite; player tags display a shape
without adding text, matching Zeal's restriction on player text tags.

```text
/tag local Assist
/tag local ^R^Assist
/tag local ^S^
/tag local ^P^
/tag gsay ^Y^Mez
/tag rsay ^R^Main assist
/tag rsgs Off tank
/tag target Main assist
/tag clear
/tag local clear
/tag off
/tag on
/tag status
```

`rsgs` chooses raid, then group, then local. `clear` clears the selected living
target; with no living target it clears all tags. `<route> clear` clears all tags
for recipients on that route. `target` selects the nearest living tagged spawn
with an exact text-field match; failure clears the current target.

Shape prefixes are case insensitive:

| Prefix | Shape |
| --- | --- |
| `^R^`, `^O^`, `^Y^`, `^G^`, `^B^`, `^W^` | Red, orange, yellow, green, blue, white arrow |
| `^S^` | Red stop sign |
| `^P^` | Green paw |
| `^-^` | Disable shape |

Legacy `*R:` through `*W:`, `*S:`, and `*P:` also work. An NPC text tag gets a
nameplate-colored arrow by default, including Gusto's current target color.
`/tag defaultarrow off` disables that default for subsequent text updates.

Tag text is limited to 32 printable ASCII characters per update. ` | ` is a
reserved field separator and cannot be embedded in a single update. Prefix
characters count towards the update length. Field operations follow Zeal:

```text
/tag local +Off tank
/tag local -Off tank
/tag local @Main assist
/tag local !Main assist
```

`+` appends a field without duplicating it; `-text` removes an exact field.
`@text` moves that field from other tags and appends it to this target.
`!text` moves the field and replaces this target's existing text. Explicit
shapes persist when text is removed; use `^-^`, or clear the tag, to remove them.

## Group, raid, and channels

Outgoing messages use `GUSTOTAG | <update> | <name> | <spawn ID>`. Incoming
`GUSTOTAG` and abbreviated `GT` messages, plus legacy `ZEALTAG`/`ZT` messages,
are accepted only from the group/raid
chat colors or the selected tag channel and its echo. Both spawn ID and
normalized spawn name must match a living local spawn. Out-of-zone tags are
ignored. Local application provides immediate feedback before chat echoes.

```text
/tag join Gt123
/tag chat ^G^Assist
/tag channel Gt123
/tag suppress on
/tag filter on
/tag prettyprint on
```

`join` selects and requests a channel whose name begins with `Gt` and contains
only letters and digits. A request is not a confirmed join: wait for EverQuest's
join response. Gusto reports `joined as /N` only when the channel appears in
the client's joined-channel list. `/list` checks the server's joined channels;
if native `/join Gt123` also fails, the server chat service must be working
before tag-channel exchange can succeed. `chat` uses the confirmed native
channel number. `channel`
broadcasts the selected channel to the raid or group; compatible recipients
join it. `suppress` and `filter` hide successfully handled tag messages locally.
`prettyprint` replaces their wire format with a readable update/target line.
These switches also accept `off`.

Gusto sends only `GUSTOTAG`. Original Zeal clients do not recognize that new
header: use updated Gusto clients on the same `Gt...` channel to share tags.
Legacy Zeal tag messages can still be received over group/raid or the selected
Gt channel, but channel selection/announcements require the new `Gt` prefix.

This port uses native name sprites. Zeal's font renderer, target-window tooltip,
UI controls, alternate-symbol setting, and INI persistence are not included.

## Client addresses and evidence

The client is the existing validated May 10 2013 23:30:08 RoF2 executable:

```text
eqgame.exe SHA256:
2a8702ad9f722704f01355c0750be7d6f164a8b9c9128ba0cf286ea32b405b0e
EQGraphicsDX9.dll SHA256 used for the graphics audit:
ade556b68e98f6f39c5578f70431e11c0b9babcc15b8571eb22cf1e2b654417e
```

Local reference sources:

- `source/Zeal-1.4.8/Zeal/nameplate.cpp` and `tag_arrows.cpp` for protocol,
  field operations, colors, and models.
- `source/Zeal-1.4.8/Zeal/MacroQuest/src/eqlib`, branch `emu-rof2`, commit
  `3eafc8217e690b3c93201a17ec205410db1cac82`, especially `offsets/eqgame.h`,
  `game/PlayerClient.h`, `game/PcClient.h`, `game/EQData.h`,
  `game/EverQuest.h`, and `graphics/Render.h`.
- MacroQuest `src/main/MQ2Spawns.cpp` for the RoF2 removal-hook signature.
- The installed client binaries for code anchors and graphics corrections.

Addresses below are preferred virtual addresses in **eqgame.exe** and are
rebased through Gusto's validated resolver:

| Address | Use |
| --- | --- |
| `0x00E641D0` | Pointer storage for PlayerManagerClient |
| `0x005996E0` | `GetSpawnByID` |
| `0x008CF790` | `PrepForDestroyPlayer` (thiscall, one spawn argument) |
| `0x015D4698` | Pointer storage for SGraphicsEngine |
| `0x00E67CCC` | Pointer storage for CEverQuest |
| `0x00DD261C` | Pointer storage for local PC |
| `0x00DD2690` | Direct raid object |
| `0x0051F1A0` | `CEverQuest::dsp_chat` |
| `0x00519750` | `CEverQuest::SetGameState` |
| `0x0058E2D0` | `PlayerClient::SetNameSpriteState` for native text refresh |
| `0x0051FCE0` | Existing command interpreter, used for native chat commands |

| Object | Offsets used |
| --- | --- |
| PlayerClient | Y/X/Z `0x64/0x68/0x6C`, name `0xA4`, type `0x125`, height `0x13C`, spawn ID `0x148`, actor `0x101C` |
| SGraphicsEngine | CRender pointer `0x0C` |
| CRender | D3D9 device `0xF08`, combined view/projection matrix `0x16C0`, callback **`0xA9C4`** |
| CEverQuest | Current chat connection `0x0C`, chat service `0x2A4`, game state `0x5C8` (5 = in world) |
| UniversalChatProxy | Channel-list pointer `0x0C`, count `0x10` |
| Local PC / group | Group pointer `0x31CC`; group member pointers `0x04` through `0x18` |
| Raid | Raid state `0x3488` (4 = in raid) |

The local MacroQuest `Render.h` lists the callback as **0xA9EC**, which is
incorrect for the audited graphics binary. Disassembly of the setter at
`EQGraphicsDX9 + 0x92540` reads/writes **0xA9C4**; the render loop at
`EQGraphicsDX9 + 0x97729` reads/calls that same field. Gusto checks both byte
sequences before attaching. The callback executes inside the world's active
BeginScene/EndScene region, before the later UI pass. Its existing callback is
chained. This early callback queues a world-frame draw rather than issuing
model draws before terrain and actors. A checked detour at
`EQGraphicsDX9 + 0xAE370` (`C2DPrimitiveManager::Render`) consumes that draw
on `Render(true, false)`, immediately before the world's cached-label pass.
The scene call site at `+0x97815` confirms the two flags. The detour copies
the first eight position-independent bytes (two complete instructions) and
uses Gusto's executable-section and thread checks, authorized by the validated
game module. This avoids guessing a render-interface vtable slot.

The chat proxy source has a general layout warning. Its specific channel count
and list fields were independently confirmed by the getters at `0x007EFA10`
and `0x007EFA60`; the friends fields elsewhere in that class do differ.
The native chat window reads the current connection at `CEverQuest + 0x0C`
before calling `GetChannelName` (see `0x006514C5`). Gusto uses that connection's
list first, and falls back to the service at `+0x2A4` only when the current list
is empty. A populated current list is authoritative, so an obsolete service
list cannot redirect a tag to a different `/N` channel. Unreadable channel-name
slots retain their positions. `/tag status` includes both list counts and the
confirmed selected slot.

The remaining data layouts come from the RoF2 reference and existing Gusto
code; the audit script checks code anchors, not live object contents.

Game detours require the existing client validation token, exact prologue
bytes, whole copied instructions, and Gusto's thread checks. Spawn fields and
pointer chains are checked for readable memory. Zone/game-state transitions
and spawn removal discard tags. Native actor-name formatting preserves the
existing attack brackets.

D3D9 rendering captures/restores device state and transforms. It uses CPU model
data and `DrawIndexedPrimitiveUP`, so it retains no default-pool resources
across Reset. It skips lost devices and invalid positions. Models use the actor's graphics position and spawn height plus a 1.5-unit
vertical offset; stop/paw models face the player's graphics position.
The getter at graphics `+0x54560` returns actor `+0x114`; both its bytes and
the actor's getter slot (`+0x7C`) are checked before reading that position.
Gameplay X/Y fields are used for target-distance comparisons, not model
placement. Animated head-bone anchoring from Zeal is not ported.

## Verification

```bash
cmake --build build
python3 tools/verify_rof2.py
python3 tools/verify_tagging.py
```

Pass a client directory to `verify_tagging.py` to audit a different installation.
The two PE hashes and all code anchors must match that script's audit baseline.
At runtime Gusto validates the game hash, both graphics callback anchors,
the late-pass prologue, and the actor-position getter. It does not hash the
graphics DLL.

The portable tests can run on Linux without the game or Wine:

```bash
g++ -std=c++20 -fsanitize=address,undefined -g -Iinclude \
  test/tagging_tests.cpp src/client/rof2/tag_state.cpp \
  src/client/rof2/tag_models.cpp -o /tmp/gusto-tagging-tests
ASAN_OPTIONS=detect_leaks=0 /tmp/gusto-tagging-tests
```

Leak detection is disabled for environments where LeakSanitizer cannot run
under the process tracer. AddressSanitizer and UBSan remain enabled.
The Windows build also provides `gusto_tagging_tests.exe` and
`gusto_safety_tests.exe`.

`/tag status` reports callback frame and successful draw counts, the current
render stage, and the last Direct3D HRESULT. It also reports late-pass calls
and the model center's normalized clip coordinates and clip W. A center in
front of the camera normally has positive W, X/Y in [-1, 1], and Z in [0, 1]. With an active shape, increasing
frames but zero draws indicates a skipped or failed draw; zero frames after
attachment indicates that the callback is not being dispatched. The renderer
rechecks callback ownership on nameplate updates and restores a replaced
callback chain. Unsupported transform reads no longer abort rendering;
state blocks provide the restoration fallback.

Live-game acceptance checks are still required:

1. Tag a nearby NPC with text and each arrow color, stop, and paw. Rotate the
   camera, move around, and check placement, depth occlusion, and facing.
2. Append/remove/move fields; clear and disable tags; confirm native text
   refreshes and attack brackets still behave normally.
3. Exchange group, raid, and tag-channel messages with another Zeal/Gusto
   client. Check channel broadcast/join, echoes, filters, and malformed messages.
4. Despawn/repop a tagged NPC, zone, and switch characters. Confirm tags do not
   carry into a reused spawn ID.
5. Resize, minimize/restore, and switch display modes; confirm rendering resumes
   and native UI rendering stays intact after a device reset.

A successful build, code-anchor audit, or Wine console test does not establish
that live 3D placement or server chat behavior has passed these checks.
