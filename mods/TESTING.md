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

  In the launcher a number turns a mod on or off, `b` is "Build with
  mods", `r` builds and runs the game (with `SDL_VIDEODRIVER=x11`, and
  `HALO_FORGE=1` when a forge mod is on), `q` quits. The same without the
  menu: `python -m tools.mod_launcher enable checkpoint_handler`,
  `... disable forge_ui`, `... list`, `... build`, `... run`.
- [ ] Watch the terminal the launcher runs in: `checkpoint_handler` writes
      there.
- [ ] To get into a level quickly, put `map_name levels\a10\a10` in
      `assets/init.txt` (the data root's `init.txt`; remove it afterwards),
      or start a campaign from the main menu.

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

## 2. forge_ui (only this mod on; the launcher sets `HALO_FORGE=1`)

Keys of the menu are the dev tools' own (`port/linux/src/xinput_sdl.c`,
`halo_linux_forge_read_keys`); forge_ui changes only how the menu looks and
what it remembers.

- [ ] In a level, press **F3**. A dark panel opens at the left with large
      text: a title bar "Spawn" with "n / m" at its right, the tabs
      **Vehicles Weapons Equipment Bipeds Scenery** with the current one lit
      blue, up to 8 rows with the chosen one on a green bar with `>`, each
      row's folder small and grey at its right, and two lines of keys at
      the bottom.
- [ ] **Down** 5 times: the bar moves to row 6 and "6 / m" follows.
      **Right**: the Weapons tab lights. **Down** twice.
- [ ] Press **F3** (or **Esc**) to close, then **F3** again: the menu opens
      on Weapons, row 3, as it was. **Left**: back on Vehicles at row 6.
- [ ] In a long category (Scenery), go down past row 8: the list scrolls
      one row at a time and `^ more ^` / `v more v` show there is more.
      Close and reopen: the same rows are shown with the same row chosen.
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

## 3. forge_edit (only this mod on; the launcher sets `HALO_FORGE=1`)

Keys: `mods/forge_edit/source/mods/forge_edit/forge_edit_keys.h`. Messages
appear in the terminal at the top left of the game.

- [ ] In a level, look at a weapon on the ground or a crate and hold
      **Ctrl**: under the crosshair a box shows its name and
      "Ctrl+C copy, Delete remove". (Holding Ctrl still crouches, as
      Ctrl always does; **F2**'s flying camera shows the mark without
      Ctrl.)
- [ ] **Ctrl+C**: `forge_edit: copied <name>`. The player does not
      additionally react to C.
- [ ] Look at an empty spot of floor, **Ctrl+V**: a copy appears standing
      there, turned and scaled as the original; `forge_edit: placed
      <name>`. **Ctrl+V** again places another.
- [ ] Look at the copy and press **Delete**: it disappears; `forge_edit:
      removed <name> (Ctrl+Z brings it back)`.
- [ ] Look at the sky, a wall or the floor (nothing marked) and press
      **Delete**: nothing happens and nothing is written.
- [ ] Remove three objects, then **Ctrl+Z** three times: they return one at
      a time, the last removed first, each where it was. A fourth
      **Ctrl+Z**: `nothing removed to bring back`.
- [ ] **Z** alone still zooms and **C** alone still crouches; **Ctrl+Z**
      does not zoom.
- [ ] Protection: look down at your own body, drive a warthog and look at
      it, or fly out with **F2** and look back at your player: no mark,
      and **Delete** and **Ctrl+C** do nothing.
- [ ] With the dev tools holding an object (**F4** on something), press
      **Delete**: only the held object is removed (`forge: removed ...`),
      not what is behind it. With the **F3** menu open, **Ctrl+C** and
      **Delete** do nothing.
- [ ] **Delete** on an enemy removes it; **Ctrl+Z** brings back a new one
      without its weapon (a known limit: only the object itself is
      recorded).
- [ ] Change `FORGE_EDIT_UNDO_KEY` to `HALO_MOD_KEY_U` (with `_CTRL`
      TRUE), rebuild: **Ctrl+U** undoes, and the message after a removal
      says "Ctrl+U brings it back". Change it back.

## 4. All three together

- [ ] Turn on all three in the launcher, `b`: the build succeeds (there is
      no conflict, as each changes different files: two new units and a
      patch of `forge.c`).
- [ ] `r`, load a level. Every step of 1, 2 and 3 above still works.
- [ ] Open the **F3** menu, move to another row, and press **F5** with the
      menu open: the checkpoint is taken. Press **F9**: the checkpoint is
      restored, the menu closes (the map's state went back), and **F3**
      opens it at the row you left.
- [ ] Take a checkpoint (**F5**), remove two objects (**Delete**), press
      **F9**: both are back (the checkpoint had them), and forge_edit says
      `back at a checkpoint, removals forgotten`; **Ctrl+Z** then brings
      back nothing twice over.
- [ ] Place a copy (**Ctrl+V**), **F5**, **F9**: the copy is still there
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
