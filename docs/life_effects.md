# Death ghosts and feign-death jester

The original Blender-extruded assets in `assets/life_fx` are embedded into the
ASI and drawn as real world-space 3D meshes before native nameplates/UI.

| Event | Visual | Default size |
| --- | --- | --- |
| Observed living NPC dies | Small white ghost | 1.5 |
| Observed living player dies | Larger orange ghost | 2.8 |
| Living player enters feign-death state | Blue jester head and skeleton laugh | 2.0 |

Effects fade in over 0.75 seconds, slowly rise at 1.1 world units per second,
and fade out over their final 1.8 seconds. Default lifetime is 8 seconds. They
stay at the event's world location even if the corpse is looted or removed.
They face the viewer horizontally, with a slight sway showing the extrusion.
Native depth testing hides portions behind the world; the effects do not write
to the world's depth buffer. Device state is captured/restored around drawing.

Open `/gusto` and select **Death / FD**. The menu controls each event type,
feign-death laughter, lifetime, rise speed, three sizes, separate NPC/player ghost colors, and maximum distance.
Defaults enable all three visual effects and the FD laugh within 250 units.
The FD switch is the master for its jester and sound; the laugh switch mutes
that sound separately. The client controls the native effects volume/mute.
Settings persist with the other HUD options in `%LOCALAPPDATA%\Gusto\hud.ini`.

## Event detection

The bounded spawn snapshot observes RoF2 `PlayerClient` fields from the local
MacroQuest emu-rof2 source: ID `+0x148`, type `+0x125`, stand state `+0x35C`,
HP/max `+0x2E4/+0x2DC`, and graphics actor `+0x101C` / graphics position `+0x114`.
The source defines player/NPC/corpse types 0/1/2, feign state `0x73` and dead
state `0x78`. Native corpse transitions retain the observed live entity type,
so a player's corpse gets an orange ghost instead of being treated as an NPC.

Each ID/object identity has a transition history. Existing corpses and players
already feigning on first observation do not trigger. Holding the same pose
and becoming a corpse after a detected death do not trigger a second effect.
Standing/recovering rearms a later event. NPC feign poses do not produce a jester.
Unknown HP does not imply death; known HP going to zero or the native dead/
corpse state does. Known zero HP in a stale feign pose cannot create a jester.

Snapshots update at 100 ms intervals. The existing validated spawn-removal hook
also samples the entity immediately before destruction, then forgets its
identity while retaining any emitted animation. Zoning clears all histories
and effects. A death with no observed live baseline cannot be inferred safely;
a very brief pose between samples can be missed. No effects are synthesized
merely because a living entity disappears. At most 32 animations remain active.

## Skeleton laugh

The installed `soundassets.txt` maps `321` to `SkelAtt1.wav`, the classic
skeleton attack laugh. Its filename/archive are also listed in the
[EQEmu sound reference](https://docs.eqemu.dev/client/guides/sounds/); the laugh
variants are identified in this
[Project 1999 sound discussion](https://www.project1999.com/forums/showpost.php?p=2525009&postcount=3).

Gusto resolves the filename's existing mapping and calls the validated native
`EqSoundManager::WavePlay` at `0x4A9140` with manager global `0xD20460`. The
entry bytes and loaded asset slot are checked. It uses the client's sound data
and native volume settings. The packaged artwork contains no copied EQ audio.
A nearby new FD event requests one laugh, with a 0.5-second global anti-overlap
interval. Ordinary deaths are silent. Missing/muted assets do not prevent visuals.
`/gusto status` reports active death/FD effects and the resolved laugh ID.

## Art and verification

The SVG component drawings are extruded and beveled in Blender, then exported
as editable `.blend`, GLB and OBJ files. The white ghost has 1,232 triangles;
the jester has 1,780. The standalone orange export is larger, while the game
uses the shared ghost mesh with its orange tint and configurable scale.
`tools/create_life_fx.py` regenerates all assets and the embedded C++ mesh data.
`tools/verify_life_assets.py` validates GLB structure, materials and nonzero
extrusion. Rendering uses fixed-function vertex colors with baked bright shading.

`gusto_life_tests` covers new/duplicate deaths, corpse and resurrection
transitions, feign/real-death distinctions, ID/object reuse, missing positions,
zoning, fade curves and rise timing. It also runs under ASAN/UBSAN on Linux.
The optional `gusto_life_preview` renders the exact embedded meshes through
D3D9 and reads back white/orange/blue pixels, verifies state restoration,
expiration, disabled/distant effects and sound-request gating. Its native sound
request is checked as a value; offline tests do not play EQ audio or control a
live character. Full live death/FD/audio behavior still needs confirmation.

The ghost silhouette and both eyes are mirror-symmetric, and the open mouth has no teeth. NPC and player color selectors cycle white, orange, blue, green, pink, purple, yellow and red. Changes apply to active ghosts too. `GhostNPCColor` and `GhostPlayerColor` in the HUD INI accept any decimal 24-bit RGB value; alpha is controlled by the fade. The audit checks mirrored runtime vertices, and the D3D9 preview checks custom green/purple colors and independent scale changes.
