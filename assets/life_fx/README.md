# Original Gusto ghost and jester artwork

Created as polygon drawings in Blender 5.1.2, filled, extruded, and beveled into
closed meshes. The eyes, empty ghost mouth, hat, bells and jester teeth are also extruded.
These original visual assets use the repository's MIT license.

- `gusto_life_effects.blend`: editable gallery with white NPC ghost, larger orange
  player ghost, blue jester head, camera and studio lights.
- `spooky_ghost.glb` / `.obj`: white ghost template.
- `player_ghost.glb` / `.obj`: larger orange ghost, including its materials.
- `blue_jester.glb` / `.obj`: blue jester head.
- `ghost_outline.svg` / `jester_outline.svg`: original component drawings.
- `blender_preview.png`: rendered art gallery.
- `mesh_stats.json`: runtime template triangle counts and bounds.

Regenerate from the project root:

```sh
blender --background --factory-startup --python tools/create_life_fx.py
python3 tools/verify_life_assets.py
```

The generator exports the runtime meshes into `include/gusto/client/rof2/life_art.hpp`.
Runtime coordinates use Z up, with the artwork facing negative Y. The ASI embeds
the meshes, so the game does not need to load Blender or read these files.
White and orange ghosts share the embedded template with different sizes/tints.
GLB/OBJ use their format's normal export-axis conversion.

Audio is resolved from the installed EQ client through its native sound manager.
See `docs/life_effects.md` for game integration and controls.
