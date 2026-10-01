The forge branch now has only the Forge parts,
  checkpoint, gravity and the mod tools, committed as
  43d36cb7. The shader work is in a stash so you can
  add it back later. Nothing is pushed.

  How I checked it: the normal Linux build links, and
  the build with all six mods links too. The 49 tests
  for the launcher, overlay and map-tag tools pass. I
  have not run the game itself.

  On the branch
  - Forge core: the tools and menu tabs, the mod hooks
    (tick, new_map, render_world, grab, menu pages),
    tag import, start-at-map, and the small hooks in
    source/.
  - Mods: forge_ui, forge_edit, forge_zones, forge_ai,
    checkpoint_handler, gravity.
  - Mod tools: the launcher, the fzf screen, the map
    and tag tools, and their tests.

  Removed (all shader and sky work)
  - The post-processing chain and the sky renderer
    (postfx.c, skyfx.c and the files that go with
    them), plus the mods/screen_filters and mods/sky
    mods.
  - json.c, because only the shader code used it.
    Layout save/load will need a JSON parser later;
    this one is in the stash.
  - The shader changes in d3d8_gl.c/gl.h, the two
    --wrap link flags, and the paths.mods and
    debug.postfx_* settings.
  - Filter zones: forge_zones now has kill, gravity and
    teleport zones only.
  - Launcher: the shader preset commands, the tkinter
    window and the Ctrl-S/Ctrl-G keys.
  - Docs: I cleaned FORGE_INTEGRATION.md,
    FORGE_PLAN.md, TESTING.md and the READMEs. They now
    say the shader work is stashed for later. In
    mods/mods.json I removed the shader settings and
    turned on gravity.

  Stashes
  - stash@{0} "shaders and sky for later…": exactly the
    shader and sky changes. Running git stash apply
    stash@{0} on this branch puts them back.
  - stash@{1}: a full backup of the whole working tree
    from before the split. Once you're happy with the
    result, you can drop it with git stash drop
    stash@{1}.

  Decisions to check
  - I kept forge_ai, tag import and start-at-map as
    Forge parts, since forge_ai needs the imported
    characters.
  - The committed mods/mods.json still has your launch
    map (bloodgulch) and your import list.

  For release on all platforms, the main open points
  are in FORGE_INTEGRATION.md section 6:
  - Forge is still switched on by the HALO_FORGE
    environment variable, set by the launcher.
  - The mods only reach players through a custom
    overlay build.
  - forge_ui is a fragile patch on forge.c.
  - Windows and Android builds haven't been tried with
    this branch.