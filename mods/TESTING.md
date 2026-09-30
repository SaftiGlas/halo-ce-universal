# Testing the source mods

A checklist for the three example mods, one at a time and together. Each
step says what to press and what should happen; tick it off as you go.
Background: `port/linux/README.md`, "Source mods".

## Before you start

- [ ] The regular build still works and is unaffected: `ninja linux`
      builds `build/linux/halo` as before (no mods in it).
- [ ] Every mod builds on its own and together (the launcher prints the
      commands it runs):

      python -m tools.mod_launcher                # the interactive launcher

  With fzf installed the launcher is a list: **Space** or **Tab** turns the
  mod under the cursor on or off, **Enter** builds and runs the game (with
  `SDL_VIDEODRIVER=x11`; the tools are on in the Forge game type),
  **Ctrl-B** only builds, **Ctrl-L** chooses the map to open at (see
  section 0), **Esc** quits. Without fzf a number turns a mod on or off,
  `b` is "Build with mods", `r` builds and runs, `m` chooses the map,
  `q` quits. The same without any menu:
  `python -m tools.mod_launcher enable checkpoint_handler`,
  `... disable forge_ui`, `... list`, `... build`, `... run`.
- [ ] Watch the terminal the launcher runs in: `checkpoint_handler` writes
      there.
- [ ] To get into a level quickly, choose it in the launcher (Ctrl-L) or
      `python -m tools.mod_launcher map b30`; see section 0.

## 0. The launcher: fzf and starting at a map

- [ ] `python -m tools.mod_launcher` in a terminal with fzf opens a list of
      every mod with `[x]` / `[ ]`, its kind (`code` or `no code`) and its
      description, and on the right a preview: what the chosen mod patches,
      adds or sets. Type to search.
- [ ] **Space** on a mod: its mark flips at once and the cursor stays on it.
      `mods/mods.json` (`"enabled"`) changes with it. **Tab** does the same.
      **Ctrl-A** turns all on, **Ctrl-X** all off.
- [ ] **Ctrl-L**: a list of the campaign levels and multiplayer maps with
      their titles. Choose **Blood Gulch**: a list of game variants, choose
      `team_slayer`. Back on the mod list, the header says `Starts at: Blood
      Gulch (bloodgulch), team_slayer`, and `mods.json` has `"launch"`.
- [ ] **Ctrl-L**, choose **The Silent Cartographer**: the list is now the
      difficulties. Choose `hard`. **Ctrl-L**, then **Main menu** clears it.
      **Esc** in either list leaves the choice as it was.
- [ ] Choose Blood Gulch again and press **Enter**. The game builds, opens
      straight in Blood Gulch, no menus, and you are a player of a local
      team slayer game (the score is on the HUD, hold Back for the list of
      scores). **F3** works; the terminal shows `start: game_variant
      team_slayer` and `start: map_name levels\test\bloodgulch\bloodgulch`.
- [ ] Choose a campaign level (b30, hard): the game opens on it, on hard.
- [ ] `python -m tools.mod_launcher run --menu` opens the main menu once;
      `run --map hangemhigh --variant ctf` opens that once and leaves the
      saved choice alone; `map none` clears the saved choice.
- [ ] Without fzf (`PATH` without it, or output piped): the numbered menu,
      with `m` to choose the map.

## 0b. Tags from other maps (rocks and trees in a multiplayer map)

Needs the game data in `assets/maps` (a30.map is the donor). Any mods.

- [ ] In the fzf launcher press **Ctrl-T**, choose **a30** (the list of maps),
      wait a second for "reading a30...". In the list of scenery type
      `boulder_granite_large` and press **Space**: the row shows `[x]`, and
      `mods.json` has `"import"` with `a30:scen:scenery\rocks\boulder_granite_large\...`.
      Space again turns it off. Do the same for `tree_pine\tree_pine`. Esc,
      and the header of the list says `Tags from other maps: 2`.
      (Without fzf: `python -m tools.mod_launcher import add a30
      boulder_granite_large`, `import show`, `import list a30 rock`.)
- [ ] **Ctrl-L**, Blood Gulch, slayer, then **Enter**. The game loads a
      second or two longer. The terminal shows `import: N tags from a30
      added to bloodgulch`.
- [ ] Press **F3**, go to **Scenery**: it has 2 objects more than without the
      imports (19 becomes 21), the rock and the tree among them, sorted by
      their tag names (`scenery\rocks\...`).
- [ ] Take the rock (**Enter**): it is held at the crosshair, textured like
      in Halo's first level (grey granite, not white, not black), and
      **Enter** places it. Take the tree: the needles are see-through where
      the texture is, and the trunk is solid.
- [ ] Walk into the rock and stand on it: it is solid, as the scenery of the
      map is. Shoot it: the bullets mark it as they do a wall. **F4** on it
      picks it up again, so its collision is found.
- [ ] Load another multiplayer map (hang 'em high) from the game's menus or
      the launcher: the rock is in its Scenery list too, and nothing is
      wrong when you go back to Blood Gulch, or quit.
- [ ] `import into bloodgulch`: only Blood Gulch gets them. `import clear`:
      the count is back to 19 and the terminal shows no `import:` line.
- [ ] Not tried: vehicles, weapons of their own, skies. Do not import them.

## 0c. Characters and AI (mod forge_ai; needs the imports of 0b)

Turn on `forge_ai` and, in the launcher (Ctrl-T), bring in from **a30**
`characters\grunt\grunt minor plasma pistol` and from **b30**
`characters\hunter\hunter` (actv). Start in Blood Gulch (or any multiplayer
map) as in 0b. The game loads a few seconds longer.

- [ ] **F3**: the tabs under TOOLS now begin with **AI**. Its rows: Character,
      Add a character (crosshair), Add a waypoint (crosshair), Show the
      waypoints, Clear the waypoints, New ones patrol the waypoints, Send all
      along the waypoints, Remove all. Left / Right on **Character** goes
      through the grunts and hunters (and the variants they bring: major,
      needler...).
- [ ] Choose a grunt. Look at a spot on the ground and press Enter on **Add a
      character**: the menu closes and a grunt stands there facing you. It
      is awake (not curled up), the crosshair goes red over it, a red blip
      shows on the motion tracker, and it shoots at you and takes cover
      as in the campaign. Kill it: it falls as a grunt does.
- [ ] A hunter does the same (heavy, with its shield); shoot it in the back
      for the orange, in the shield for nothing.
- [ ] **Remove all** takes them away (no body left, no errors).
- [ ] Put the crosshair on the ground and **Add a waypoint**; move; another;
      a third. Orange markers stand on the points (a pole with a diamond) and
      lines join them; **Show the waypoints** off hides them.
- [ ] With **New ones patrol** on, add a grunt out of your sight (put the
      character where you are not looking, then turn away, or use F2's flying
      camera): it walks to the first point, waits about two seconds, walks
      round walls to the next, and so on round again, until it sees you, when
      it fights instead.
- [ ] With **New ones patrol** off, add a grunt: it stands. **Send all along
      the waypoints** and it walks the path.
- [ ] Add a waypoint more while one patrols: the path takes it up. **Clear the
      waypoints** and nothing crashes.
- [ ] Load another map: the waypoints and characters are gone; the AI tab
      lists the characters of the new map.
- [ ] Without the imports, the AI tab's Character row says `none: import one`
      and Add a character says so in the terminal.

## 1. checkpoint_handler (only this mod on)

Keys: `mods/checkpoint_handler/source/mods/checkpoint_handler/checkpoint_handler_keys.h`.

- [ ] Start a campaign level. Walk to a spot you will recognise, fire a few
      shots (ammunition down) and take some damage (shields down).
- [ ] Press **F5**. The HUD shows the game's own "Checkpoint... done", a
      green **Checkpoint set** box appears at the top middle for 2.5
      seconds, and the terminal shows `halo-linux: checkpoint set`.
- [ ] Walk far away, empty a magazine, throw a grenade, get hurt.
- [ ] Press **F9**. You are back at the spot, facing the same way, with the
      health, shields and ammunition (magazine, reserve and grenades) of
      the checkpoint. A blue **Checkpoint restored** box appears, and the
      terminal shows `halo-linux: checkpoint restored`. Enemies are back
      where they were too: it is the game's own whole checkpoint.
- [ ] Get killed. While dead, press **F5**: an orange box and the terminal
      say `checkpoint not set: not while the player is dead`. Press **F9**:
      you are restored to the last checkpoint.
- [ ] Wait for an automatic checkpoint of the game ("Checkpoint... done"
      after a fight), then press **F9**: you go back to that one, the
      latest.
- [ ] Open the console with **`**, press **F5** and **F9**: nothing
      happens. Close it again.
- [ ] During a cutscene (the start of a10), **F5** and **F9** are refused
      with `not during a cutscene`.
- [ ] In a multiplayer game (a split screen or system link game), **F5**
      and **F9** are refused with `checkpoints work only in local campaign
      games`.
- [ ] Change `CHECKPOINT_SET_KEY` to `HALO_MOD_KEY_F6`, rebuild (`b`), and
      check that **F6** now takes the checkpoint and **F5** does nothing.
      Change it back.

## 2. forge_ui (only this mod on; in a Forge game)

Keys of the menu are the dev tools' own (`port/linux/src/xinput_sdl.c`,
`halo_linux_forge_read_keys`); forge_ui changes only how the menu looks and
what it remembers.

- [ ] In a level, press **F3**. A dark panel fades in at the left, after
      Halo: Reach's forge menu: a title bar "FORGE" with the map's name at
      its right; down the left side **SPAWN** with **Vehicles Weapons
      Equipment Bipeds Scenery** and how many objects each has in this map,
      then **TOOLS** with the pages of the mods that are on (Edit, Zones,
      AI, Gravity); the chosen category on a blue bar with a pale
      edge; at the right its name, "n / m", and up to 8 rows with the chosen
      one on the bar, each row's folder small and grey at its right; the
      chosen object's whole tag name under the rows; and along the bottom
      the keys on caps (`Up/Down Choose`, `Left/Right Category`,
      `Enter/A Take`, `Esc/B Close`), on a second line when they do not fit.
      Nothing is cut off at the edges.
- [ ] **Down** 5 times: the bar glides to row 6 (it does not jump) and
      "6 / m" follows. **Right**: the bar on the left glides down to Weapons
      and the rows change. **Down** twice.
- [ ] Press **F3** (or **Esc**) to close, then **F3** again: the menu opens
      on Weapons, row 3, as it was, and fades in again. **Left**: back on
      Vehicles at row 6.
- [ ] In a long category (Scenery), go down past row 8: the list scrolls
      one row at a time and a thin scroll bar at the right shows where you
      are. Close and reopen: the same rows are shown with the same row
      chosen.
- [ ] **Right** past Scenery: the category bar goes on down to **Edit**
      (under TOOLS) and the rows are that page's, a label at the left and
      its value at the right in the large font, or both in the small font
      when they do not fit; on the chosen row the value has `<` `>` around
      it, and **Left / Right** change it. The keys along the bottom now say
      `Left/Right Change`, `Enter/A Do` and `T/V Tab`.
- [ ] **T / V** and **LB / RB** move to the previous and next
      category, through the tools and round to Vehicles.
- [ ] With the menu closed, **W A S D**, the arrows, **Enter** and **Esc**
      work as in the game (Esc opens the pause menu): the closed menu takes
      nothing.
- [ ] **Enter** takes the chosen object: it is spawned and held at the
      crosshair as before (placing, turning, cancelling are unchanged).
- [ ] Quit the game. `~/.local/share/halo-linux/u/forge_ui.txt` (or
      `$HALO_SAVE_ROOT/u/forge_ui.txt`) exists with a `category` line and
      a `place` line per category. Start the game again, load a level,
      press **F3**: the category and row are those you left.
- [ ] Load another map that has the same tag (for example a warthog):
      the menu chooses it again; with a tag the map lacks, row 1.

## 3. forge_edit (only this mod on; in a Forge game)

Everything is on the **Edit** tab of the dev tools' menu (F3 / D-pad right;
**LB / RB**, Page Up / Page Down, or T / V change tab; up/down
choose, Enter or A act, B or Esc close). No Ctrl key is used. Messages
appear in the terminal at the top left of the game.

- [ ] Open the menu on the Edit tab. Rows: `At the crosshair: <name or
      nothing>`, `Copy`, `Paste (a copy at the crosshair): nothing
      copied`, `Remove`, `Undo remove: 0 to bring back`. Look at a weapon
      on the ground or a crate first: `At the crosshair` names it. **F2**'s
      flying camera also shows a box with its name under the crosshair.
- [ ] `Copy` and A: `forge_edit: copied <name>`; the row `Paste` now shows
      the name. The menu stays open.
- [ ] Close the menu, look at an empty spot of floor, open it, `Paste` and A:
      the menu closes and a copy stands there, turned and scaled as the
      original; `forge_edit: placed <name>`. Paste again places another.
- [ ] Look at the copy, `Remove` and A: it disappears; `Undo remove: 1 to
      bring back`.
- [ ] Look at the sky, a wall or the floor (nothing marked): `At the
      crosshair: nothing`, `Copy` says `nothing to copy there` and `Remove`
      does nothing.
- [ ] Remove three objects, then `Undo remove` three times: they return one at
      a time, the last removed first, each where it was. A fourth: `nothing
      removed to bring back`.
- [ ] On a keyboard with the menu closed, **Delete** on the object under the
      crosshair removes it too.
- [ ] Protection: look down at your own body, drive a warthog and look at
      it, or fly out with **F2** and look back at your player: `At the
      crosshair` says nothing, and Copy and Remove do nothing.
- [ ] With the dev tools holding an object (**F4** on something), **Delete**
      (Back on a controller) only removes the held object (`forge: removed
      ...`), not what is behind it.
- [ ] Removing an enemy and `Undo remove`: a new one without its weapon (a
      known limit: only the object itself is recorded).
- [ ] The same steps with a controller alone: D-pad, A, B, LB/RB.

## 4. All three together

- [ ] Turn on all three in the launcher, `b`: the build succeeds (there is
      no conflict, as each changes different files: two new units and a
      patch of `forge.c`).
- [ ] `r`, load a level. Every step of 1, 2 and 3 above still works.
- [ ] Open the **F3** menu, move to another row, and press **F5** with the
      menu open: the checkpoint is taken. Press **F9**: the checkpoint is
      restored, the menu closes (the map's state went back), and **F3**
      opens it at the row you left.
- [ ] Take a checkpoint (**F5**), remove two objects (Edit tab), press
      **F9**: both are back (the checkpoint had them), and forge_edit says
      `back at a checkpoint, removals forgotten`; `Undo remove` then brings
      back nothing (0 to bring back).
- [ ] Paste a copy, **F5**, **F9**: the copy is still there
      (it was in the checkpoint).

## 5. The conflict check

- [ ] Copy the forge_ui mod: `cp -r mods/forge_ui mods/forge_ui_copy`, and
      turn both on. `b` stops with an error that names the file and both
      mods:

      conflict: port/linux/game/forge.c is changed by more than one mod:
      forge_ui (patches), forge_ui_copy (patches); enable only one of them,
      or move what they share into the ports (port/linux/include/halo_mod.h)

  Turn the copy off (or delete it): the build works again.
- [ ] Break the patch: change one `-` context line in
      `mods/forge_ui/patches/001-forge-menu.patch` and build: it fails
      with `mod forge_ui: ... does not apply to port/linux/game/forge.c`.
      Undo the change.

## 6. gravity (the tick and new map hooks; in a Forge game)

Everything is in the dev tools' menu now: **F3** (D-pad right on a
controller) opens it, **LB / RB** (or T / V) change tab until **Gravity**, and
left/right change the row. No Ctrl key is used.

- [ ] Turn on `gravity`, `b`, `r`, load a level (campaign or a local
      multiplayer game). Open the menu (F3): after Vehicles, Weapons,
      Equipment, Bipeds, Scenery there are tabs for the mods (Edit, Zones,
      AI, Gravity). Go to Gravity with Y or V: `Gravity of the
      whole map: < normal (100%) >`.
- [ ] **Right**: `low (50%)`. Close the menu (B, Esc). The top right of
      the screen says `gravity: low (50%)`. Jump: you go about twice as
      high and fall slower. Grenades fly further; a Warthog jumped off a
      ramp floats.
- [ ] Right again: very low (25%), moon (17%), high (150%), very high
      (200%), then normal, where the label goes away. **Left** goes the
      other way.
- [ ] Frame rate does not matter: with low gravity, jump with the frame
      rate uncapped and capped (or with interpolation on and off): the
      jump is the same height.
- [ ] Pick moon, load another map (or restart the level): the new map is
      at moon gravity too.
- [ ] Pick moon, then join or host a system link game: gravity is normal
      there, there is no label, and the row says `gravity: changes only in
      local games`. Back in a local game it is moon again.
- [ ] With **F9** (checkpoint_handler) at moon gravity: the checkpoint
      comes back and gravity stays moon.

## 7. forge_zones (kill, gravity and teleport zones; in a Forge game)

All of it is on the **Zones** tab of the dev tools' menu (F3 / D-pad right;
LB / RB or T / V change tab; up/down choose a row, left/right change it,
Enter or A act, B or Esc close). No Ctrl key, and every step works with a
controller alone. Try it in a local multiplayer game (a flat map such as
Blood Gulch is easiest), then in a campaign level for the AI.

Showing and placing:
- [ ] Open the menu, go to Zones: rows `Show zones: off`, `New zone (place
      at the crosshair): < kill zone >`, `Zone: none yet`. Only these
      three, because there is no zone.
- [ ] Look at the ground a few metres away, open the menu, on `New zone`
      press A (Enter): the menu closes, and a red, see-through box with
      bright edges stands on the ground there (the terminal says `new kill
      zone, at once, 3.00 x 3.00 x 2.00`). **Fly (F2)** and look at it from
      above and below: it is a box in the world, hidden behind walls like
      anything else. *If no box shows up, say so: the debug geometry path
      of the renderer is then the thing to fix.*
- [ ] Open the menu again: the rows now have `Zone: 1 of 1`, `Kind`, `Kill
      after`, `Width`, `Length`, `Height`, `Resize step`, `Move here`,
      `Remove`. **Right/Left** on Width, Length and Height change the size
      (held they repeat; the floor stays), `Resize step` chooses 0.25, 1 or
      4 per press.
- [ ] Aim at the zone with the menu closed (zones shown): its edges get
      brighter (it is the selected one). Open the menu on Zones: the
      `Zone` row says which.
- [ ] Turn to face another direction, aim elsewhere, open the menu, `Move
      here` and A: the zone moves there, turned the way you look.
- [ ] `Zone` with left/right goes through several zones; the selected one is
      the bright one. `Show zones` off hides them all; on shows them.
- [ ] `Remove` and A removes the selected zone (`zone removed`).

Picking a zone up (like an object):
- [ ] With a zone selected, `Pick up and move (aim, turn, raise)` and A: the
      menu closes, a crosshair shows and the top left says `moving a zone`,
      its turn step and height. Aim around: the box follows the crosshair,
      standing on the surface.
- [ ] Left/right (D-pad or arrows) turn it, up/down raise and lower it; **Y
      / V** cycle the turn step (free, 15, 45, 90 degrees: then each press
      turns one step). **A / Enter** puts it down there.
- [ ] Pick it up again and press **B / Esc**: it goes back where it was and
      as it was turned. **Delete / Back** removes it.
- [ ] Zones shown, aim at one and press **F4** (or **A while flying**): it is
      picked up the same way, and an object that is nearer than the zone
      is picked up instead.
- [ ] Load another map or press F9 while holding one: the hold just ends.

Kill zones:
- [ ] Place a kill zone (at once) and walk into it: you die, the game
      counts it as a death (suicide) and you respawn normally.
- [ ] On the `Kill after` row press right until it says `after 3 s`. Walk in: "Leave the kill zone:
      3", 2, 1 in red in the middle of the screen, then you die. Walk in
      and out again before 0: the countdown stops and starts over next
      time.
- [ ] Drive a vehicle into an instant kill zone: the driver dies (the
      vehicle stays).
- [ ] Campaign: a zone where AI walks (or grenade them into it): they die
      too.
- [ ] `Show zones` off hides the zones: the kill zone still kills.

Gravity zones:
- [ ] Place a zone, on `Kind` press right: a blue gravity zone, `Gravity: 25%`. Jump in
      it: much higher, slower fall. Walk out mid-air: normal gravity again.
- [ ] Left/right on the `Gravity` row through the settings: `weightless` (a jump keeps
      going up until you leave the top), `200%`/`300%` (short jumps),
      `slow lift`/`lift`/`fast lift` (green: walking in lifts you off the
      ground and up out of the top).
- [ ] Drop a weapon in a lift: it rises.
      A weapon lying in a 25% zone stays where it is until something moves
      it; knocked, it floats down slowly.
- [ ] Drive a Warthog through a 25% zone off a ramp: it floats.
- [ ] Together with the Gravity tab (moon): a 200% zone is then twice the
      moon's gravity, not Earth's.

Maps and checkpoints:
- [ ] Load another map: the zones are gone (`forge zones: 0`).
- [ ] With checkpoint_handler, **F5**, place a zone, **F9**: the zone
      stays (zones are not part of checkpoints).
- [ ] System link: `New zone` says `zones work only in local games`, and
      nothing is drawn.

Teleport zones:
- [ ] `New zone` kind `teleport zone`, A: a purple box, and the row `Channel
      (...): 1 (no partner yet)`. Walk in: nothing happens.
- [ ] Walk away, set the kind to teleport again and place a second one:
      it takes channel 1 as well (`linked`), both boxes are the same colour
      with a line between them (zones shown).
- [ ] Walk into one: you appear in the other, at the same place in the
      box, and keep walking without going back. Leave and walk into the
      other one: you go to the first.
- [ ] Turn one box a different way (Move here while facing another way):
      arriving turns you and your speed by the difference: walk straight
      through and keep going straight out of the other box.
- [ ] `Direction` on one of them: `entrance only`, then it sends but
      never receives (walking into the other goes nowhere); `exit only`
      receives and does not send. The line between them points from
      entrance to exit.
- [ ] Three zones on channel 1: each sends to the next one placed (a
      ring). Channel 2 on one of the three: it stops being part of it.
- [ ] Grenade or AI walking into a teleport: bipeds go through as well. A
      Warthog driven in goes with its driver. *(Untested guess: a vehicle
      with people on it may arrive with them slightly inside the ground.)*
- [ ] Dead bodies and things held or carried do not teleport. A kill zone
      next to the exit still works.
