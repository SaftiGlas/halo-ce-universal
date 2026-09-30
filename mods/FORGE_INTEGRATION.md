# Forge: what the `forge` branch contains, and what it takes to ship it

Written 2026-09-29 from the state of branch `forge` (4 commits on top of
`main`, plus a large amount of uncommitted work). Companion to
`mods/FORGE_PLAN.md` (feature ideas, measurements, status) and
`mods/TESTING.md` (in-game test checklists). This file answers a different
question: **what is there, where does it touch the base game, and what must be
decided or changed to make Forge a normal part of the game for other users.**

Scope of this branch: Forge (the tools, menu, edit, zones, AI, tag import,
start map), `checkpoint_handler`, `gravity`, the `sky` mod and the mod
tools (overlay build, launcher). The shader work (post-processing / screen
filters and their `.frag` files, filter zones, per-map filter chains) was
split off into a git stash, **"shaders and sky for later"** (`git stash
list`), to be added back on top of this branch later. The sky mod has been
brought back from it: its sky shader is built into the port
(`port/linux/src/skyfx.c`), with no `.frag` files or post-processing chain.

## 1. What Forge is

An in-game map editor for the native ports (Linux first),
in the spirit of Halo 3 / Reach Forge, working on the campaign and multiplayer
maps. Local (single machine) games only.

| Feature | Where | State |
| --- | --- | --- |
| Fly camera (F2), spawn menu of the map's objects (F3), pick up / move / rotate / place / delete (F4) | `port/linux/game/forge.c` | works (POC commit) |
| Copy / paste / remove / undo objects | `mods/forge_edit` | works |
| Reach-style menu (categories left, rows right, key caps) | `mods/forge_ui` | works |
| Map-wide gravity | `mods/gravity` | works, tested |
| Kill / gravity (and lift) / teleport zones, placed at the crosshair | `mods/forge_zones` | built, **not tested in game** |
| Bring tags (scenery, characters) from other maps into the current map | `port/linux/game/tag_import.c`, `tools/map_tags.py` | works (rocks, trees, 34 scenery types, grunt/hunter/elite) |
| AI characters with patrol waypoints (game's own AI + pathfinding) | `mods/forge_ai` | works |
| F5 manual checkpoint, F9 revert | `mods/checkpoint_handler` | works |
| Start straight in a map (no menus) | `port/linux/game/start_map.c`, `tools/mod_maps.py` | works |
| Save / load layouts (placed objects, zones, gravity) | — | **not done** |

Controls: everything is reachable from a keyboard **and** a controller
(D-pad up = forge mode (fly); only then D-pad right or X = menu; LB/RB or
PgUp/PgDn = tab; flying, LB/RB sink and rise, LT is faster and Y removes;
holding, the right stick orbits the object, RT + right stick turns it and
RT + left stick moves nearer or further). Mods add
tabs to the menu through `struct halo_mod_menu`.

## 2. How it is built (architecture)

Three layers, so the byte-matching decompilation stays untouched:

1. **Base hooks** (`source/`, all inside `#ifdef HALO_LINUX`): a handful of
   one-line calls into the port. Listed in section 3.
2. **The port** (`port/linux/`): `forge.c` (the tools), `mods.c` + `halo_mod.h`
   (the mod API and hooks), `tag_import.c`, `start_map.c`, config settings.
3. **Source mods** (`mods/<name>/`): native C units built into the game by the
   overlay build. A mod is `mod.json` plus files that *replace* a game/port
   file, *add* a new unit, or *patch* one (`patches/*.patch`). Two mods may not
   touch the same file. Built with `python configure.py --mods a b` into
   `build/mods/<key>/halo`; the root `build.ninja` and the matching build
   never see mods (`tools/mod_overlay.py`, tested by `tools/test_mod_overlay.py`).
   `tools/mod_launcher.py` (+ `mod_fzf.py`, `mod_maps.py`) is the front end:
   enable/disable mods (`mods/mods.json`), pick a map, build, run, choose
   tags to import.

Hooks a mod can register (`halo_mod.h`): `update` (per frame), `render`
(fullscreen overlay), `tick` (30 Hz, start of `game_tick`, local games only),
`new_map`, `render_world` (3D debug geometry inside each player's view), a
menu page, and `grab` (F4 interception).

## 3. Changes in the base game (`source/`)

Everything is guarded by `#ifdef HALO_LINUX`, so the Xbox-matching build is
unchanged. Only the *forge-specific* changes are listed; `git diff main...HEAD`
also contains a big merge from `origin/main` (netcode, Android, CI, etc.) that
is not Forge.

### From the first version (commit `88f2b853`, "forge support POC")

| File | Change | Why |
| --- | --- | --- |
| `source/main/main.c` (`main_loop`) | `forge_update()` before `player_control_update` | per-frame input for the tools and mods |
| `source/interface/interface.c` (`interface_draw_fullscreen_overlays`) | `forge_render()` after `terminal_draw` | menu / HUD text of the tools |
| `source/camera/director.c/.h` | `director_forge_flying()`, `director_forge_set_flying()`, speed constants | fly camera built on the game's `flying_camera_update` |
| `source/game/player_control.c` | mouse look skipped when `director_inhibited_facing()` | so mouse drives the flying camera, not the player |

### Added on this branch (uncommitted)

| File | Change | Why |
| --- | --- | --- |
| `source/game/game.c` | `halo_mods_tick()` at start of `game_tick`; `halo_mods_new_map()` at end of `game_initialize_for_new_map` | tick / new-map hooks (gravity, zones, undo state) |
| `source/render/render.c` (`render_window`) | `halo_mods_render_world()` after `render_debug` | draw zones in 3D from the player's camera |
| `source/main/console.c` (`console_startup`) | `halo_start_map()` after `init.txt` | `game.start_map` setting |
| `source/cache/cache_files.c` | `halo_tag_import()` after tags load; `halo_tag_import_release()` on unload | cross-map tags |
| `source/cache/cache_files_windows.c` (`cache_file_read`) | `halo_tag_import_read()` serves offsets `>= 0x40000000` from memory | bitmap/sound data of imported tags |

One link-time change: the Linux link wraps `render_sky`
(`-Wl,--wrap=render_sky`, `tools/linux_build.py`) so the sky mod's sky is
drawn where the game draws its own (`port/linux/game/sky_state.c`). Windows
and Android builds leave the wrap out and keep the map's sky (the fog of
the sky mod's looks still changes there). The `hud_draw_screen` wrap
belongs to the stashed post-processing.

## 4. Changes in the port (`port/linux/`, `tools/`)

- `game/forge.c` (+~270 lines): tab navigation, mod menu pages, `grab` hook,
  mod "hold" API (`forge_mod_hold_begin`), `forge_point_at_crosshair`,
  debug tab setting.
- `include/halo_forge.h`, `halo_mod.h`, `halo_linux_source_fixups.h`:
  public headers; the last one pulls them into every source file.
- `game/mods.c`: registry and dispatch of hooks, key/text/box helpers.
- `game/tag_import.c`, `game/start_map.c`: cross-map tags, start at a map.
- `src/port_config.c`: new settings `game.start_map`, `start_variant`,
  `start_difficulty`, `game.import`, `game.import_into`,
  `debug.forge_menu_tab`.
- `src/xinput_sdl.c`: shoulder buttons + PgUp/PgDn as tab keys; the tools
  capture WHITE/BLACK too.
- `tools/`: `mod_overlay.py`, `mod_launcher.py`, `mod_fzf.py`, `mod_maps.py`,
  `map_tags.py` (+ tests); `configure.py --mods`.
- READMEs: `port/linux/README.md` (Source mods, settings).

## 5. The `mods/` folder

| Mod | Kind | Notes |
| --- | --- | --- |
| `forge_ui` 2.0 | **patch** of `game/forge.c` (`001-forge-menu.patch`, ~700 lines) | the restyled menu. Fragile: breaks whenever `forge.c` changes. |
| `forge_edit` 2.0 | added unit | Edit tab |
| `forge_zones` 2.0 | added unit (1.6k lines) | Zones tab (kill, gravity, teleport); in Forge games |
| `forge_ai` 1.0 | added unit | AI tab; needs imported actor variants |
| `gravity` 1.1 | added unit | Gravity tab |
| `checkpoint_handler` | added unit | F5 / F9 |
| `mods.json` | launcher state | enabled mods, launch map, import list |
| `enabled.json` | old launcher state | looks superseded by `mods.json`; remove if unused |
| `FORGE_PLAN.md`, `TESTING.md`, this file | docs | |

The tools are on in local games of the **Forge** game type (the last
default game type, `build_game_variant_forge`, `GAME_VARIANT_FORGE_FLAG`);
`HALO_FORGE=all` turns them on in every game for development.

## 6. What must change to ship Forge to other users

Today Forge is a **developer workflow**: clone, install a toolchain, run the
launcher, which compiles a custom binary per mod set. Users of the normal
release (CI/rolling builds) cannot use it. To make it part of the game,
decide and do the following, roughly in priority order.

### 6.1 Commit and clean up first
- The Forge work is committed on `forge` (without the stashed shader/sky
  parts). Before a PR, consider splitting it into logical pieces: (a) mod
  API + hooks, (b) tools/forge menu, (c) tag import, (d) start_map,
  (e) mods, (f) launcher/tools.
- Delete/reconcile `mods/enabled.json`; decide whether `mods/mods.json` (which
  currently holds one developer's launch map and import list) is
  committed as a default or generated on first run and git-ignored.
- `git diff main...HEAD` shows ~560 files because `main` moved; rebase or merge
  `origin/main` again before opening a PR so the Forge diff is reviewable.

### 6.2 Decide how mods reach users (the big architectural choice)
Source mods are compiled C. A release binary cannot load them at runtime.
Options:
1. **Ship Forge in the main build (recommended).** Move the stable mods
   (`forge_edit`, `forge_zones`, `gravity`, `forge_ai`, `checkpoint_handler`)
   into `port/linux/game/` as normal units, register them with
   `HALO_MOD_REGISTER`, and let CI produce one binary. Keep the overlay build
   only for third-party/experimental mods.
2. Keep the overlay build and document "build it yourself". No engine work,
   but only developers use it.
3. Load mods as shared libraries. Big change (32-bit guest process, ABI),
   probably not worth it.

### 6.3 Fold `forge_ui` into `forge.c`
`forge_ui` is a 700-line patch against `forge.c`, so it fails to apply after
any edit. If the Reach-style menu is the intended UI, merge it into `forge.c`
and drop the patch (and the "must apply exactly" failure mode).

### 6.4 Replace the `HALO_FORGE` environment switch (done)
Forge is a game type: "Forge" is added after the 26 default game types
(`playlist_profile.c`; its name and description are built-in strings in
`text_group.c`), and `forge_mode_on()` (`forge.c`) is TRUE in local games
of it. Still open: printing the key help on first use.

### 6.5 Base-code changes worth making permanent
From the POC and this branch, the following are the minimal, reviewable base
edits; keep them, they are already `HALO_LINUX`-only:
- `main.c` `forge_update`, `interface.c` `forge_render`, `director.c` fly camera,
  `player_control.c` mouse-look gate (POC).
- `game.c` tick / new-map hooks, `render.c` `render_world` hook.
- `console.c` `halo_start_map`, `cache_files*.c` tag-import hooks.

Suggested cleanups when upstreaming:
- Replace the `halo_mods_*` call sites with **one** `halo_forge_hook(event)`
  dispatcher so `source/` has a single symbol to know about.
- Give `game_tick` hooks a clear rule: they run only in local games; assert
  that `game_connection()` is local in every state-changing mod function.
- Declare the hooks in a small header included by `source/` instead of
  relying on `halo_linux_source_fixups.h` pulling everything everywhere.
- Windows/Android: the Forge code is plain C in `port/linux/game/` with no
  link-time wraps, so it builds wherever the Linux game sources build; check
  that `windows_build.py` / `android_build.py` pick up the new units and that
  `HALO_FORGE` input works there.
- Gamepad/touch controls for Android are not designed at all.

### 6.6 Finish and test what is only "built"
Not yet run in game: zones (kill, gravity, teleport). Work through
`mods/TESTING.md` section 7 before release.

### 6.7 Layout save / load (done, first version)
`port/linux/game/forge_layout.c` and the "Map" tab: up to 16 layouts a map
in `u:\forge\<map>\layout_NN.txt` (text, tag **names**), with the objects
the tools made, the map's own objects they moved or removed (found again
by tag and placement), and each mod's part through the `layout_save`,
`layout_clear` and `layout_load` hooks of `struct halo_mod` (zones, AI
characters and waypoints, gravity, sky). The layout chosen with "Play on
this map" (`play.txt`) loads at every new map in any local game, Forge or
not. New layouts are named "Layout N"; `python -m tools.mod_launcher
layouts rename MAP N NAME` renames one. Automated test hook:
`debug.forge_layout_save` / `HALO_FORGE_LAYOUT_SAVE`.

Layouts with "Show in the map list" (`listed 1`) are maps of their own in
the multiplayer map list of local games, after the 13 maps, with the base
map's picture and the layout's name (`port_multiplayer_levels` in
`ui_widget_event_handler_functions.c`, `mp_level_select_list_update_displayed_items`,
the lobby's map name, and `fallback_string` string indices 1000+/2000+ in
`text_group.c`); `game.start_layout` starts one.

Still open: renaming in the game (text entry); the map list remembers the
game's maps only (a Forge map chosen last is found again while the game
runs); a checkpoint revert restores
the game state as it was, not the layout; weapons of the map's spawn points
and vehicles the game respawns are not the map's own objects to a layout
(moving one saves a copy); the game type's rules still apply in normal games
(slayer's vehicle set keeps only warthogs).

### 6.8 Cross-map tag import: product decisions
- It needs the user's own campaign `.map` files (donor maps); nothing may be
  redistributed. State that plainly in docs.
- Each map load inflates the donor map (1–2 s for a 200 MB campaign map).
  Extract the wanted tags once into a small cache file for shipping.
- Unverified: tags whose data looks like pointers (some sounds, animations)
  beyond the ones already handled; more character types may need layout
  knowledge. Vehicles and skies from other maps are untried.
- Imported tags exist only on this machine: never in system link / netplay.

### 6.9 Multiplayer and safety
Everything is local-only by design (state must match on all machines of a
system-link/distributed game). Keep the `game_connection()` checks; make sure
`gravity` resets on each new map (it does) and that no Forge object or zone
leaks into a networked game after a session change. Do not enable Forge in
the internet-play / netcode path without a separate design.

### 6.10 Docs, tests, packaging
- Merge the pieces of `port/linux/README.md` about Forge into one "Forge"
  section aimed at players (keys, menu tabs, limits), separate from the
  developer "Source mods" section.
- Add automated tests for what is testable headlessly: tag import fix-ups (round-trip on maps), layout files.
  Existing: `tools/test_mod_overlay.py`, `test_mod_launcher.py`, `test_map_tags.py`.
- `mods/mods.json` (launch map, imports) is personal; ship neutral defaults.
- CI: run `configure.py --mods` on the example mods to catch overlay breakage.

## 7. Known limits (carry into user docs)
1. One cache file at a time: only the map's own tags plus imported ones.
2. Lightmaps are baked; new/moved objects get no radiosity. New BSP is not possible.
3. Mod `update` runs per frame; gameplay changes must use `tick` (30 Hz).
4. Local games only.
5. Barriers and real collision blocks (scaled scenery, primitives), surface
   shaders / texture replacement and squads / AI vehicles are not built.

## 8. Suggested order of work
1. Commit + merge `origin/main` (6.1).
2. Test zones (6.6).
3. Layout save/load (6.7).
4. Decide 6.2, then fold `forge_ui` (6.3) and the stable mods into the main build.
5. Replace `HALO_FORGE` with a real setting and a menu entry (6.4).
6. Windows/Android parity (6.5).
7. Player-facing docs and CI (6.10); then cross-map import packaging (6.8).
8. Later: bring back the stashed shader work on top (the sky mod is back
   already; when applying the stash, keep this branch's sky files).
