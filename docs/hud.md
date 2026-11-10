# RoF2 HUD development preview

Gusto's new HUD integrates a Hailstone-style minimap, Zeal-style floating
combat text and camera controls with the May 10 2013 RoF2 client. This code
uses the local MacroQuest **emu-rof2** layouts; Zeal's Titanium addresses and
Hailstone's runtime interfaces are not used for client access.

## Commands

| Command | Action |
| --- | --- |
| `/gusto`, `/gusto menu`, `/gusto options` | Toggle the settings menu |
| `/gusto status` | HUD frame count, map/effect counts and hook status |
| `/minimap on`, `/minimap off` | Show or hide the minimap |
| `/minimap rotate on` | Rotate the map with your heading |
| `/minimap autoz on` | Fade geometry above/below the selected height range |
| `/minimap group on`, `/minimap labels on` | Group dots / expanded-map labels |
| `/minimap zoom 1` through `5` | Ranges 150, 300, 600, 1200, 2400 |
| `/minimap scale 0.5` through `3` | Scale the overlay |
| `/minimap reload` | Reload this zone's external map files |
| `/minimap external` | Open the independent full-map window |
| `/minimap full` | Toggle the expanded Gusto map |
| `/fcd on`, `/fcd off` | Floating damage, miss and healing text |
| `/fcd melee on`, `/fcd spells on` | Filter melee / spell hits |
| `/fcd self on`, `/fcd others off` | Incoming hits / unrelated combat |
| `/fcd healing on`, `/fcd healing off` | HP-gain / healing floats |
| `/fcd bighit 100` | Highlight hits at or above a damage threshold |
| `/camera on`, `/camera off` | Enable or disable custom camera settings |
| `/camera fov 45` | FOV in Zeal half-angle units; range 10–100 |
| `/camera zoom 100` | Native maximum zoom; range 5–200 |
| `/camera smooth 0.12` | Orientation smoothing time; range 0–0.5 seconds |
| `/camera mode 0` through `4` | Select a native camera mode |

`/map` aliases `/minimap`. `/gusto map`, `/gusto floating` and `/gusto camera`
also accept the corresponding options. Camera changes default to **off**.
Own/pet outgoing damage and damage incoming to you are enabled by default;
unrelated combat is off. Big-hit highlighting uses a configurable threshold,
not an inferred critical-hit flag from the native packet.

Hold a single left click on the minimap to preview the expanded map; releasing
hides the preview. Double-click and hold to move the minimap, then release to
finish. Only that move gesture captures the cursor. Ordinary menu clicks and
single minimap presses do not capture it. Native DirectInput mouse processing
is also blocked while the HUD owns the pointer; its buffered events are drained
to prevent a UI click replaying into the world afterward. Native left/right
down hooks prevent the cursor-hiding/mouselook path, and the HUD supplies the
arrow cursor for its client area. The OS double-click time and distance
settings are respected. Right-click toggles rotation; Alt+right-click opens the
menu. The wheel and +/- still zoom. `/minimap full` or the menu's Expanded map
switch opens a persistent map; Escape closes it.

The settings menu has nine pages: **Map, Combat, Bars, Nameplates,
Camera, Tags, Death / FD, Gamepad and Pipes**. See [death/FD models and sound](life_effects.md). Every feature setting is exposed there; Slow Turn and
Toggle All Bags retain their existing keybind controls. Settings persist in
`%LOCALAPPDATA%\Gusto\hud.ini` inside the Wine prefix on Wine. A menu action
writes the settings section once, and opening/closing the menu does not grab
the cursor.

### Healing and overhead bars

Healing floats follow Zeal's HP-update approach: green `+N` for your observed
HP gains, and `+N%` for other entities. The bounded spawn snapshot is refreshed
at 100 ms intervals. Small remote player/pet gains below 5% are suppressed,
as in Zeal, to reduce regeneration noise. New spawns, reused IDs/objects,
changes in maximum HP, zoning and resurrection do not produce an initial
healing float. These are observed HP gains, not spell-event attribution;
self regeneration can appear, and a heal offset by damage between samples
can be missed. Healing has its own Combat-page switch.

Health (green), mana (blue) and stamina/endurance (gold) bars render above
player/NPC/pet heads. Each resource and each entity category, self, target-only
mode and maximum distance have menu controls. A known empty resource draws an
empty bar; absent/invalid maximums are hidden. NPCs (including pets) only show
HP. Mana is restricted to your own character and grouped player characters;
nongroup player mana is hidden. Player endurance remains available where known.
Servers may not provide remote mana/endurance, so enabling the bars cannot
reveal unavailable values. Your own mana comes from the current PcProfile and
native CharacterZoneClient::Max_Mana, rather than the spawn's cached mana fields.
The RoF2 endurance resource is used for stamina, rather than Titanium's old
fatigue inversion. Your endurance reads the active profile at `+0x3390` and
uses the native `Max_Endurance` getter; remote-player values are still shown
only when actually available.

### Nameplates and markers

NPC con colors, pink NPC targets, auto-attack brackets and pink/white flashing
have independent Nameplates-page switches. Flashing only runs while attacking
a pink-highlighted NPC target, defaults to **15.5 color changes per second**,
and has a 1–16 changes/second control. Brackets apply only to the attacked NPC.
Changing a setting refreshes live NPC tints; changing targets/attack state also
refreshes the affected text.

All loaded player characters can appear on the minimap and expanded map, using
stable per-ID colors and heading arrows. Group members retain their player arrow colors without a surrounding ring. Pets have standard light brown
heading arrows. Player, pet and group displays each have a menu switch. Corpses
and ordinary NPCs do not receive map arrows. The local player's arrow stays
blue. Player and pet arrows use base size 7, with local size 9. The Map page's
**Arrow size** slider adjusts these from 50% to 300% and saves the setting.
It affects the minimap, expanded map and external map. Slider dragging does
not capture the game's cursor. Updates are limited to entities already known to the client.

Owned NPCs use a standard owner line within the **native nameplate**, such as
`(Aggatha's pet)`, instead of a separately positioned overlay caption. The
current `MasterID` resolves the actual player name; changing charm ownership
replaces that line and breaking charm removes it. Ordinary subtitles and tag
text are preserved. Attack brackets enclose the NPC name alone. The native
sprite sets placement, font, tint and visibility. Ownership is checked every
500 ms, with forced caption refreshes only when needed. The Nameplates page
retains the **Pet owner names** switch. Unknown owners do not get a guessed name.

The Tags page controls tagging, 3D markers, tag text, default arrows and chat
filter/suppress/pretty-print options, plus clearing local/received tags.
Creating/sharing tags and joining a named channel retain their `/tag` commands.

## Map files and appearance

Install standard map files in `maps` beside the actual `eqgame.exe`:
`<zoneshortname>.txt`, `<zoneshortname>_1.txt`, `_2.txt`, `_3.txt`.
The loader supports L line records and P point/label records. It rejects unsafe
zone names, nonfinite coordinates and oversized files/records. Missing maps
produce a frame/player marker with a diagnostic in `/gusto status`.

The reference is Hailstone's **modern UI, Midnight palette**:
`ui/modern/minimap.lua`, `ui/modern/lib/palettes.lua` and Arial font sizes from
`src/ui/imgui_layer.cpp`. At scale 1 the frame is 200×248, the disc radius is
86, and its center is (100,116) relative to the frame. The north badge, ring
buttons at 35°/58°, zone title and clock use the reference positions, colors
and 13/16 px font sizes. Text rasterization differs between GDI/D3D9 and
Hailstone's ImGui renderer; a pixel-identical live result is not yet verified.
The full map and Gusto settings panel are Gusto interfaces.

## External full-map window

Use **Map > Open external map window**, or `/minimap external`. This opens a
separate resizable desktop window using the same standard zone files, layer
filters, height fading, labels and player/pet markers. Drag its title bar to
another monitor. Wheel to zoom, drag the map to pan, and press Space or
double-click to recenter/follow your character. Close it with its normal X.
It is a full Gusto map view, rather than a detached native MapViewWnd editor.

The window paints independently through GDI on its own message thread, so it
can move across monitors without moving or resizing the game. Immutable map
data and value-only snapshots cross threads; the window never reads game
memory or uses the game's D3D device. Initial map data is published before
opening, and live snapshots update while the game renders. Exclusive-fullscreen
focus behavior follows the client's normal desktop-window behavior.

## Native integration and provenance

Local reference: `source/Zeal-1.4.8/Zeal/MacroQuest/src/eqlib`, branch
`emu-rof2`, commit `3eafc8217e690b3c93201a17ec205410db1cac82`.
Zeal map projection, altitude filtering, damage animation and angle smoothing
were adapted from `zone_map`, `floating_damage` and `camera_math`; rendering
and hooks were rewritten for RoF2. See `THIRD_PARTY_NOTICES.md` for licenses.

| Native interface | RoF2 address / layout |
| --- | --- |
| `CEverQuest::ReportSuccessfulHit` | `0x52EE40`; target/source uint16 at 0/2, spell int32 at 5, damage int32 at 9 |
| `ProcessMouseEvents` | `0x539E60`, cdecl(); DirectInput device global `0xE67B50` |
| `CEverQuest::LMouseDown` / `RMouseDown` | `0x518870` / `0x518910`; cursor-hiding entries |
| Local mana | LocalPC `+8` vbtable, signed displacement at vbtable `+4`, ProfileManager at LocalPC + displacement + 12; selected profile mana `+0x338C` |
| Local endurance | Active profile current `+0x3390`; native max `0x582020`, this = LocalPC `+0x2DC8`, bool cap argument |
| Native maximum mana | `0x581E60`, this = LocalPC `+0x2DC8`, bool cap argument; caller verified at `0x57A706` |
| `CEverQuest::LMouseUp` / `RMouseUp` | `0x5384D0` / `0x537D30` |
| `HandleMouseWheel` | `0x5F9C90`, cdecl(int) |
| Spawn manager / GetSpawnByID | `0xE641D0` / `0x5996E0` |
| Spawn list | Manager `+8` first pointer; PlayerBase `+8` next pointer (vptr-adjusted MacroQuest TList layouts) |
| Health / mana / endurance | Current/max: `0x2E4/0x2DC`, `0x494/0x354`, `0x1A0/0x3C4` |
| Pet owner | `MasterID +0x38C` |
| Spawn coordinates / height / ID / pet | Y `0x64`, X `0x68`, Z `0x6C`; `0x13C`, `0x148`, `0x2B4` |
| Spawn graphics actor / world position | spawn `+0x101C`, actor `+0x114` |
| LocalPC group | global `0xDD261C`, `+0x31CC`; members `+4+i*4`, player `+0x28` |
| World clock | global `0xDD2600`; hour byte `+4`, minute byte `+5` |
| Active camera | CDisplay global `0xDD2660`, camera `+0x118` |
| Camera type / zoom limit | `0xD1FD9C` / float `0x9D0DE4` (original 53) |
| Camera matrix builder | `EQGraphicsDX9.dll +0x6470` |
| Camera FOV / orientation / position | `+4`, `+0xB0`, `+0xBC` |
| Rendering device / view-projection | CRender `+0xF08` / `+0x16C0` |

Addresses are preferred-image addresses, rebased through the existing validated
client layer. Hook entry bytes are checked before patching. Graphics getters
and rendering offsets were cross-checked against the local client binaries.
`tools/verify_hud.py` and `tools/verify_tagging.py` audit hashes and selected
instruction anchors without modifying game files.

Only the active native camera receives FOV/orientation changes. Its native
collision-resolved position is retained. Teleports, zoning and long frame gaps
reset smoothing. Disabling restores the original FOV and zoom limit. This
preview does not add Zeal's custom orbit/free-camera/mouselook replacement.

## Verification and current scope

The portable model tests cover map parsing (including CRLF), projections,
circular clipping, altitude filtering, smoothing and floating text lifetimes,
plus 10,000 malformed inputs under address/undefined-behavior sanitizers.
Additional checks cover HP gains, unavailable resources, directional markers,
flash timing and single/double-click gesture transitions.
Existing safety and tagging tests remain available. The opt-in
`gusto_hud_preview` target renders synthetic data with the actual D3D9 HUD and
reads every menu page back for visual inspection, checks resource-bar pixels
and button bounds, and round-trips the persisted settings. Additional Windows
fixtures verify profile selection/cycle rejection, bounded DirectInput draining,
input fallthrough, NPC resource suppression, and profile endurance. External
window checks open a real top-level window, resize/move it to negative desktop
coordinates, render/read pixels, pan/zoom/recenter, and close its message thread.
A physical second-monitor setup still requires live confirmation. It does not inject into or launch EQ.

This is a development preview requiring live validation of hooks, mouse
routing, zoning, native combat packet delivery and device resets. Previously
confirmed 3D tagging remains a separate feature.

This is not a complete port of every Zeal subsystem: no map editor/internal
map database, native `/find` trail, spell-icon atlas,
or replacement right-click menus have been added. Floating text uses
successful-hit damage/miss events and sampled HP gains. The requested menu is implemented as
a settings menu for the new features.

See [gamepad bindings](gamepad.md) for controller setup and command names.

The Pipes page includes named-pipe enable/verbose/interval and Zeal endpoint controls; see [pipe protocol](pipes.md). The Gamepad page includes mouse-stick, speed, invert Y and left/right/middle button mappings.


The Bars page adjusts width (24–160 base pixels), height (2–12 base pixels),
and independent HP, mana and stamina colors. HUD scale multiplies the selected
sizes. The nine-color palette includes green, blue, gold, white, orange, pink,
purple, red and teal. INI keys `BarWidth`, `BarHeight`, `HPBarColor`,
`ManaBarColor` and `StaminaBarColor` persist settings; color keys also accept
custom decimal 24-bit RGB values. NPC and non-group player resource visibility
rules are unchanged.


### Individual resource sizes and RPG frames

Use **Bars → Edit bar** to select Health, Mana or Stamina. **Selected width**
(24–160) and **Selected height** (2–16) change only that resource. The three
resource color selectors remain independent. **RPG bronze frames** toggles
bronze beveled borders, pointed end ornaments, a dark well, highlight and
bottom shading. It defaults on; turning it off restores the simple flat style.
Each bar is centered independently and spacing follows its own height.

The settings persist as `HPBarWidth/Height`, `ManaBarWidth/Height`,
`StaminaBarWidth/Height` and `RpgBars`. Older global `BarWidth/BarHeight` values
seed all three sizes on migration; individual saved sizes take precedence.
Resource visibility, range, ownership, group rules and resource data sources
are unchanged. The visual is drawn with native overlay primitives, requiring
no texture or font assets beyond the existing renderer.


### Separate NPC HP appearance

In **Bars → Edit bar**, choose **NPC HP** for independent width/height.
**NPC HP color** changes its fill separately from **Player HP color**. NPCs
and NPC pets use this HP style; player HP, mana and stamina retain their own
sizes and colors. The common RPG-frame toggle applies to all styles. NPCs
continue showing HP only. Existing configurations seed NPC size/color from
the current player HP style until customized. New INI keys are
`NPCHPBarWidth`, `NPCHPBarHeight` and `NPCHPBarColor`.

NPC and pet HP bars now depth-test against the current world depth buffer at
the projected head depth. Opaque meshes in front cover the obscured portions
of the bar. Bars do not write depth; player bars and the map/menu remain normal
HUD overlays. Transparent materials only occlude if the game writes their depth.

Preview18 draws NPC/pet bars in the world-complete callback before the game's
first label/UI primitive pass, alongside the existing 3D tag rendering. The
later HUD callback draws player bars only. This keeps NPC depth tests on the
world buffer before UI rendering can clear or replace it. The regression now
clears depth between the world and UI phases and verifies that an occluded NPC
bar does not reappear during the UI phase. Live scene confirmation is pending.
