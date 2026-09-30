# Forge: what to add next

Status (2026-09-29; later that day the mods' controls moved from Ctrl keys into
tabs of the forge menu, `halo_mod_menu` in `halo_mod.h`, so a controller
reaches everything; forge_zones also got teleport zones): steps 1 to 4 of
"Suggested order" are done. Steps 1-2 (the `tick` / `new_map` hooks,
`mods/gravity`) are tested and work. Steps 3-4 (`mods/forge_zones`,
`mods/TESTING.md` section 7) are built and compile but not yet tested in
game. Step 5 (saving layouts) was skipped for now.

Steps 6 and 7 (post-processing / screen filters, filter zones, per-map
filter chains, the sky mod) were built too, but are kept out of this branch
so it holds only Forge, checkpoints, gravity and the mod tools: they are in
a git stash ("shaders and sky for later", `git stash list`) to be added back
later.

Also that day: the menu (`mods/forge_ui` 2.0) was redone after Halo: Reach's
forge menu (categories down the left, rows beside them, keys on caps); the
launcher got an fzf screen for turning mods on and off (`tools/mod_fzf.py`),
and can open the game straight at a campaign or multiplayer map
(`game.start_map` and friends, `port/linux/game/start_map.c`), which is how
to reach a local game for testing forge quickly.

Wanted: change the skybox, add structures, add shaders to maps (like Halo 3 /
Reach forge), kill zones / barriers, gravity zones.

## What exists today

- `port/linux/game/forge.c`: fly camera (F2), spawn menu of the map's
  vehicles, weapons, equipment, bipeds and scenery (F3), pick up / move /
  rotate / place / delete. Local games only. API for mods in
  `port/linux/include/halo_forge.h` (`forge_busy`, `forge_object_at_crosshair`,
  `forge_placement_at_crosshair`).
- `mods/forge_edit`: Ctrl+C / Ctrl+V / Delete / Ctrl+Z on objects.
- `mods/forge_ui`: patch that restyles the spawn menu (tabs, big text) and
  saves menu state to `u:\forge_ui.txt`.
- Mod API `port/linux/include/halo_mod.h`: `update` and `render` hooks, keys,
  text and box drawing on a 640x480 screen.
- Post-processing and the sky mod: built, but stashed for later (above).

Missing and needed by most of the features below: **placements are not
saved**. Anything placed is gone when the map reloads.

## Limits to keep in mind

1. **One cache file at a time.** `tag_load` returns NONE when a cache file is
   active (`source/cache/cache_files.c:478`). Only the tags of the loaded map
   exist: no Blood Gulch sky on Hang 'Em High, no Warthog on a map without
   one. Bringing in tags from another map is its own big project (see
   "Cross-map tags").
2. **The mod `update` hook runs once per frame, not per tick.** It is called
   from `forge_update` in `source/main/main.c:3269`, before
   `game_time_update`. Gameplay (gravity, killing) must run once per game
   tick (30 Hz, `game_tick` in `source/game/game.c:433`) or it speeds up and
   slows down with the frame rate. **Done:** `struct halo_mod` now has
   `tick` (start of `game_tick`, before `units_update`, local games only)
   and `new_map` (end of `game_initialize_for_new_map`, every game).
3. **Local games only**, as the rest of forge (`game_connection() !=
   _game_connection_local` checks in forge.c). Zones change game state that
   every system link machine would have to compute alike.
4. **Lightmaps are baked.** New geometry and moved scenery get no radiosity;
   the engine lights objects from the lightmap below them, which is fine for
   objects but not for new BSP.
5. Byte-matching: everything here goes in `port/linux/game/`, `port/linux/src/`
   or `mods/`, behind `HALO_LINUX` / `HALO_FORGE`, never in the matching
   build.

## 1. Skybox

How it works: the scenario has a `sky_references` block
(`source/scenario/scenario_definitions.h:249`). Each BSP cluster has a
`sky_index` into it (`struct structure_cluster`,
`source/structures/structure_bsp_definitions.h:91`). The renderer picks
`render.visible_sky_index` / `render.visible_sky_model`
(`source/render/render.h:82`) and `render_sky` (`source/render/render_sky.c:93`)
draws `scenario_get_sky(index)`: its `model`, `animation_graph`, `lights`
(lens flares, sun direction) plus the fog in `sky_definitions.h`
(`outdoor_fog`, `indoor_fog`).

What we can do, easiest first:

- **Sky tweaks without new tags** (easy): at runtime change the loaded `sky`
  tag's outdoor/indoor fog colour, density and distances, and the sky
  lights' direction. Also a GL-side tint: the sky is drawn first, so a flag
  in `d3d8_gl.c` while `render_sky` runs could tint or replace it. A
  "time of day" slider (tint + fog) fits here.
- **Swap between skies the map already has** (easy): set every cluster's
  `sky_index`, or override `render.visible_sky_index` after
  `render_visible_sky` picks it. Most MP maps have one sky, so limited.
- **Procedural / shader sky** (medium, good payoff): skip the sky model and
  draw a full-screen GLSL sky (gradient, stars, clouds, sun, a cubemap from
  a PNG in the mod folder) in the renderer when the sky pass starts. Needs
  a hook in `render_sky` and the camera matrices from the GL layer. This is
  the realistic way to get "any sky" on any map without cross-map tags.
- **Real sky from another map** (hard): needs cross-map tags.

## 2. Structures

"Structures" can mean three things:

- **Scenery the map has** (done): the spawn menu already places every
  scenery tag of the map. Improvements: save the layout (below), a grid /
  snap to surface normal, duplicate-in-a-row, lock objects.
- **Forge blocks / primitives** (medium-hard): walls, ramps, platforms as
  new objects. Two parts:
  - *Drawing*: meshes and textures shipped by the mod, drawn from a GL hook
    in `d3d8_gl.c` after the structure pass (so they get depth, fog is
    optional).
  - *Collision*: the hard part. Collision goes through
    `source/physics/collisions.c` / `collision_bsp.c` against the structure
    BSP and object collision models. Options: (a) a box/convex test added
    next to the structure test in the collision entry points, so bullets,
    bipeds and vehicles all hit it; (b) reuse an existing scenery object of
    the map with a collision model and just scale it (object scale is
    already carried by forge_edit) — cheap, but scale may not affect
    collision the same way; check.
  Start with (b) to see how far scaled scenery gets, then (a) for real
  blocks.
- **Cross-map scenery** (hard): cross-map tags.

## 3. Shaders on the map

Two meanings, both possible:

- **Screen filters, Reach-style "FX"** (easy, builds on postfx, which is
  stashed for later): the chain runs before the HUD. Add:
  - per-map default chain in `mods.json` (`"maps": {"bloodgulch": [...]}`);
  - **filter zones**: a zone (see 4) with a preset + strength; the player's
    position selects the preset and fades the strength in and out at the
    edge. Pass a `u_zone_blend` uniform, or blend two chains.
  - more presets: colour grade / LUT (a PNG LUT in the mod folder), bloom,
    fog/haze, desaturate, night vision, underwater.
- **Surface shaders** (medium-hard): change how the world itself looks
  (new textures, tint, wet, wireframe, toon). The game's shader tags
  (`source/shaders/`, types in `shader_definitions.h`) are turned into D3D8
  pixel shader programs, which `d3d8_gl.c` translates
  (`D3DDevice_SetPixelShaderProgram`, line ~1580). A hook there could swap
  in a mod's GLSL for chosen shader tags (by tag name), or tint every
  environment shader. Also cheap: edit the loaded shader tags' colours and
  texture scales at runtime. Texture replacement (PNG in the mod folder
  replacing a bitmap tag's texture) is a natural companion.

## 4. Kill zones / barriers

The engine already has trigger volumes: `struct scenario_trigger_volume`
(`scenario_definitions.h:159`, axis aligned or oriented box with forward/up,
position, extents) and `scenario_trigger_volume_test_object` /
`_test_point` (`source/scenario/scenario.h:123`). The map's own volumes are
in the scenario, but ours should be forge's own list, not the tag's block.

Plan:

- A **zone** = oriented box (later sphere/cylinder): position, forward, up,
  extents, type, parameters. Reuse the trigger volume maths for the test.
- Place and size zones from forge: a new menu category "zones", held like an
  object (the crosshair places the centre), with keys to change the
  extents per axis. Draw them as translucent boxes: `render_debug.c` has
  debug geometry to reuse, or draw from the GL hook.
- **Kill zone**: every tick, units inside (players first, then AI,
  vehicles optional) are killed with `unit_kill` (`source/units/units.h:554`)
  so it counts as a death with the normal respawn. Options: instant or after
  N seconds, damage per second instead of kill, "soft kill" warning text
  (drawn from the render hook), which teams it applies to.
- **Barrier / player blocker**: push units out of the box (move the unit
  back to its last position outside and cancel velocity into the box).
  Bullets would still pass; a true wall is the collision work in 2.
- **Teleporter** (bonus, same system): zone in, point out, keep or reset
  velocity.

## 5. Gravity zones

Gravity is one global, `global_gravity` (`source/physics/physics.c:279`,
0.0035651792 world units per tick², `extern` in `physics.h:76`), used by
rigid bodies (`physics.c:679`, times each physics tag's `gravity_scale`),
bipeds (`source/units/bipeds.c:2425`, `:2561`), vehicles
(`source/units/vehicles.c`) and point physics (`_point_physics_no_gravity_bit`
for particles and projectiles).

- **Map-wide gravity** (done, `mods/gravity`): a Gravity tab in the forge menu cycles normal / low /
  very low / moon / high / very high; set every tick, reset to the game's
  own on each new map so system link stays normal.
- **Gravity zones** (medium): per tick, for each object in a zone, add
  `(zone_gravity - global_gravity)` along the zone's direction to its
  velocity (`translational_velocity`, `source/objects/objects.h:278`; read
  with `object_get_velocities`). That gives low/high/zero/sideways gravity
  per zone without touching the engine's physics code. Watch out:
  - bipeds on the ground are held by the ground and their own movement
    code; up-forces must beat that (lift them off first), and biped
    velocity may live in the biped's own physics state, not only the
    object's. Check `bipeds.c` around 2400-2560.
  - vehicles at rest may be asleep; wake them.
- **Lifts / man cannons** (same system): constant velocity or impulse along
  a direction instead of an acceleration. Cheap once zones exist.
- Cleaner long-term: a hook in the three gravity sites that asks "gravity at
  this point", so zones work through the engine's own maths. More invasive,
  more exact.

## Shared foundation

Everything from 3 to 5 needs the same things, so build them first:

1. ~~`tick` hook in `halo_mod.h` / `mods.c`, called from `game_tick`, local
   games only.~~ Done.
2. ~~**Zone system**~~ Done as its own mod, `mods/forge_zones` (open
   question 1 answered: a separate mod). It needed `forge_point_at_crosshair`
   in `halo_forge.h` and a `render_world` hook: the `render` hook runs in
   the full-screen overlay window, whose camera is not the player's, so 3D
   debug geometry must be queued from the player's window
   (`render_window`, after `render_debug`).
3. **Save / load layouts** per map: placed objects (tag name, not index —
   indices can differ between builds of the map), removed map objects,
   zones, gravity (later sky/filter) settings. Text or JSON in the save root
   next to `forge_ui.txt` (a JSON parser, `port/linux/src/json.c`, is in the
   stash with the shaders). Load after the map
   starts and after a checkpoint revert.

## Cross-map tags and AI (rocks, grunts, hunters in multiplayer maps)

To bring skies, scenery, vehicles or characters from other maps: open a
second cache file, copy the wanted tag and every tag it references (models,
shaders, bitmaps, sounds, collision) into free space in the tag cache, fix up
tag indices and pointers, and register their vertex/index buffers and
textures (`tags_header_register_vertex_and_index_buffers`,
`texture_cache_open`). Much of what `scenario_tags_load`
(`cache_files.c:651`) does, for a subset.

### What was measured (`python -m tools.map_tags`, read-only)

`tools/map_tags.py` reads a .map (the DVD's are the 0x800 byte header and one
zlib stream; it decompresses as far as the tag data), lists its tags and finds
what a tag needs by looking for tag references (group, name pointer, 0, index;
the name is checked, so a hit is certain). `... BLOODGULCH.map fits DONOR.map
TAG` says what bringing TAG takes. Into Blood Gulch, whose tag data is 6.97 MB
of the tag cache's 22 MB (15 MB free):

| brought in | needs | already in Blood Gulch | new | new tag data |
| --- | --- | --- | --- | --- |
| a rock (a30 `boulder_granite_large`) | 8 tags | 2 | 6 (scen, mode, coll, soso, 2 bitm) | 77 KB |
| a grunt (a30 `grunt minor plasma pistol`) | 424 | 288 | 136 (112 are `snd!`) | 589 KB |
| a hunter (b30 `hunter`) | 200 | 85 | 115 (57 `snd!`, 16 bitm, 11 effe) | 619 KB |
| an elite (a30 `elite minor plasma rifle`) | 371 | 230 | 141 | 985 KB |

So **memory is no obstacle**: characters cost well under 1 MB of tag data each,
because the weapons, effects and impact tags they share are mostly in the
multiplayer map already (sharing is by group and name).

Two more facts:

- **Multiplayer maps have pathfinding data.** Logged at map load (scratch mod
  on the `new_map` hook): Blood Gulch 4916 pathfinding surfaces, Hang 'Em High
  1695, Wizard 1559 (campaign b30: 22037). The engine's own AI navigation
  (`source/ai/path*.c`) can therefore walk a multiplayer map; "make a path"
  needs no path system of our own, only encounters / squads with firing
  positions, or a script that sends actors to points.
- Multiplayer maps already hold some AI tags (3 `actv` in Blood Gulch) but no
  `actr`, and only the MP bipeds (`cyborg_mp`).

### What is not in the tag data, and so is the hard part

- **Bitmap pixels** are read from the *current cache file* by offset
  (`bitmap->pixels_offset += pixel_data.file_offset`, `cache_file_read` in
  `xbox_texture_cache.c:759`). Sounds likewise (`xbox_sound_cache.c`). Idea: a
  wrap of `cache_file_read` that serves offsets past the end of the current
  map from a memory blob holding the donor's pixel and sound data, so the
  texture and sound caches need no change.
- **Models' vertex and index data** are the tag header's buffer tables, which
  `tags_header_register_vertex_and_index_buffers` registers for D3D: the
  imported models' entries must be added and registered.
- **The tag instance table** sits right after the tag header, is `tag_count`
  long, and is what `tag_get` indexes (`global_tag_instances`): the imported
  tags need a bigger table (in the free part of the tag cache), with both
  pointers updated. Tag indices carry a salt in their high 16 bits.
- **Pointers**: tag data has absolute pointers (the tag cache base, 0x803A6000,
  plus an offset) in blocks, data and reference names. A copied tag moves, so
  each pointer needs the delta added and each reference's index remapped
  (donor index -> the map's tag of that group and name, or the new copy).
  Layouts for every tag group are not in the game (only a few groups have
  `tag_block_definition`s), so the copy has to find pointers by value (words
  inside the tag cache's range: floats and small integers cannot be, but a
  flag word could, so verify by round trip on tags already in a map) and
  references by the group / name / index pattern above.
- **Scenario**: the imported characters need `actor_palette` /
  `biped_palette` entries (or be spawned with `object_new` and an actor
  attached by a mod), and the AI needs encounters, squads and firing positions
  in the scenario or a mod that creates actors (`actor_new`, `ai_place`) and
  sends them along points.

### Suggested order

Status (2026-09-29): step 1 is **done and works**: `port/linux/game/tag_import.c`
brought in rocks, pine trees and 34 scenery types from a30 and b30 (200+ tags),
which render with their textures, have collision (found by the crosshair) and
show in the forge menu's Scenery list; map changes and reloads are clean.
What it took, learned on the way:

- A map's tag data is laid out `[tag header][instances][names][vertex
  table][vertex data][index table][index data][tag data]`, so raw geometry
  is never scanned when pointers are moved; every pointer-like word in the
  tags of a rock lands in the tag's own data, the names, a buffer table or
  the index data. A tag's extent is up to the next tag's data.
- References: group, name pointer, 0, index (salt `0xE174 + index`); the
  name is checked. Bitmap groups need their `bitmaps` block (at +96) fixed:
  `pixels_offset` is an absolute file offset (the bitmap tag was already
  post-processed) and `tag_index` a self-reference.
- The tag cache has room (15 MB free in Blood Gulch), but the structure BSP
  is loaded after the tag data and `scenario_structure_bsp_load` clears the
  rest of the 22 MB, so the copy lives elsewhere: in contiguous memory
  (`XPhysicalAlloc`), which Direct3D also needs for vertex data.
- The game's `malloc`/`free` are debugging macros that refuse `free(NULL)`:
  the importer uses the C library's.
- `cache_file_read` ignores its tag argument, so redirecting is by offset: a
  virtual range from `0x40000000`.
- Cost: the donor map is inflated on every map load (a second or two for a
  200 MB campaign map). Extracting once into a small cache file would avoid
  it.
- Not yet checked: tags whose data holds words that look like pointers but
  are not (animations, sounds, compressed vertices are the suspects); those
  are where a character import may need layout knowledge.

Step 2 and step 3 are **done and work** (2026-09-29 as well): a grunt, a hunter
and an elite are brought in and walk the multiplayer map as AI, along a path
the player sets, with the game's own AI and pathfinding (`mods/forge_ai`, the AI
tab of the forge menu). What that took:

- Sounds (`snd!`): the samples are read from the cache file by offset like
  bitmap pixels, so they go to the same store; each permutation's cache
  fields (block index, address, the sound's own tag index) are set as at build
  time. Without this the first sound read fails with an assert.
- Animation graphs (`antr`): their frame data is packed (rotations near -1.0
  look like pointers: `0x8100xxxx`), so the pointer scan skips the frame info,
  default data and frame data of each animation (three tag_datas of the
  180 byte animation element, at 72, 140 and 160).
- A biped alone is asleep: its first animation is "asleep pistol idle" (the
  curled-up heap on the ground is a sleeping grunt, not a broken import), until
  an actor is attached.
- An actor for an existing unit: `actor_customize_unit` and
  `ai_scripting_attach_free` (what `ai_attach_free` does). Multiplayer
  scenarios have no `actr` tags or encounters, and none are needed: the
  actor variant and its tags come with the import.
- Paths: the scenario's `ai_command_lists` is a tag block, a count and an
  address, which cannot grow in place but can point at a copy one element
  longer; the mod makes that copy per map, and its go-to / pause / loop
  commands and points are arrays it rewrites when the waypoints change.
  `ai_scripting_command_list_by_unit` starts a unit on it. A point's
  `surface_index` is the collision surface below it (the pathfinding surface
  is a collision surface of the structure, `bipeds.c`).
- Verified by logging the positions: a grunt with waypoints
  (100.9,-160.2), (92.7,-142.1), (101.3,-174.4) reaches the second in 14
  seconds, waits two, and goes round the wall to the third.

Not done: attaching AI to characters that the map places by scenario, squads
(several characters that move and fight together), other AI teams, vehicles
with AI drivers, saving the waypoints and characters with a layout (step 5 of
the first list), and per-character waypoint sets.

Next:

1. ~~**Rocks**~~ (done): a scenery tag with its model, collision, shader and
   bitmaps. Proved the table growth, the pointer fix-up, vertex buffer
   registration and the bitmap read wrap.
2. ~~**A character with no AI**~~ (done).
3. ~~**AI**~~ (done, with patrols).
4. ~~A way to pick what to bring~~ (done for scenery): `mods.json` "import",
   the launcher's `import` commands and Ctrl-T in the fzf screen.

Each step is verified in game against the log and screenshots.

## Suggested order

1. ~~`tick` hook in the mod API.~~ Done (plus `new_map`).
2. ~~Map-wide gravity slider.~~ Done (`mods/gravity`, a tab in the forge menu); test it.
3. ~~Zone system + drawing + placement UI.~~ Done (`mods/forge_zones`,
   the Zones tab of the forge menu); test it.
4. ~~Kill zone, then gravity zone~~ done, lifts are gravity zones below 0.
   Still to do: teleporter, barrier (push out), per-team kill zones.
5. Layout save/load (objects + zones + settings).
6. Filter zones and per-map filter chains (postfx). Built, stashed for
   later: `"maps"` in `mods.json`, and a filter zone kind in
   `mods/forge_zones` that asks the renderer for a preset with
   `halo_postfx_zone_filter`, blended by how deep the camera is in the box.
7. Sky tweaks (fog, tint), then the procedural GLSL sky. Built, stashed for
   later as `mods/sky`: the map's sky tag fog is blended in memory, and the
   port's `render_sky` wrap (`game/sky_state.c`, `src/skyfx.c`) draws the
   tint or a GLSL sky (gradient, sun, stars, clouds). Still open: a time of day
   slider, the sun following the map's sky light, a cubemap sky from a PNG,
   sky presets in a file instead of the mod's table.
8. Scaled-scenery blocks, then real collision primitives.
9. Surface shader / texture replacement.
10. Cross-map tags.

## Open questions

- Should zones be part of `forge.c` or a separate mod (`mods/forge_zones`)?
  A mod keeps forge.c small; it needs a few more exports from
  `halo_forge.h` (hold/place a "virtual" object, menu category from a mod).
- ~~Kill zone for AI too, or players only?~~ Both, for now (every living
  biped).
- Layout file per map name, or also per game variant?
- Gamepad controls for resizing zones (the D-pad is taken while holding).
