# Play testing Forge

For people trying this branch (`forge`) with others. Linux only so far: the
Windows and Android builds have not been tried with it. The full checklist
is `mods/TESTING.md`; this is the short way in.

## Get it running

1. What the Linux build needs, and where the game data goes (`assets/`):
   `port/linux/README.md`, "Requirements" and "Start the game".
2. Build and start the game with the Forge mods:

   ```sh
   python -m tools.mod_launcher run
   ```

   The first build takes a few minutes. All seven mods are on in
   `mods/mods.json`; `python -m tools.mod_launcher` is the screen that turns
   them on and off.

Every player must run a build of the same commit, with the same mods on.

## Play together

- **Host**: Multiplayer, System Link, create a game, game type **Forge**
  (the last of the default game types), any map. The game puts an invite
  link (`halo://join/...`) on the clipboard and prints it in the terminal:
  send it to the others.
- **Join**: copy the link, then switch to the game (or open the link). The
  host's game appears under Multiplayer, System Link. On one network the
  game shows up there by itself.

More on this: `port/linux/README.md`, "System link" and "Internet play".

## In the game

| Keyboard | Controller | |
| --- | --- | --- |
| F2 | D-pad up | fly the camera (forge mode); again to land |
| F3 | D-pad right or X, flying | the menu: spawn objects, and the mods' tabs |
| F4, enter or click | A, flying | pick up the object under the crosshair |
| enter / escape / delete | A / B / Y | put down / cancel / remove what is held |
| page up, page down | LB, RB | previous and next tab |

Everyone can spawn, move and remove objects. The host's game is the game: a
player who joined asks the host, so their object appears a moment later, and
what they pick up moves on their own screen until they put it down. The
**Map** tab (save, load, name and describe layouts) and the Zones, AI,
Gravity, Sky and Edit tabs work on the host only.

## What to try

- Host and guest each spawn a vehicle, a weapon and a piece of scenery, move
  them and remove them. Does every machine show the same thing?
- Host: place a kill zone, a lift (gravity zone) and a teleport pair. Do
  they act on the guests?
- Host, **Map** tab: type a **Name** and a **Description**, **Save**, change
  things, **Load it**. Is everyone back at the saved layout?
- Turn **Show in the map list** on, leave to the menus: is the layout in the
  multiplayer map list with its name and description? Host a game on it.
- Someone joins late: do they see everything where it is now?
- Play an ordinary game type on the layout: does it play as usual?

## Known limits

- When anything of a layout changes, a guest's Forge scenery is rebuilt, so
  it blinks once.
- Names and descriptions are typed on a keyboard (US layout). Without one:
  `python -m tools.mod_launcher layouts rename MAP N NAME` and
  `layouts describe MAP N TEXT`.
- Tags brought in from other maps (the launcher's import list) must be the
  same on every machine; it is empty by default.

## Report

Say what you did, what you expected and what happened, which machine (host
or guest), and the commit (`git rev-parse --short HEAD`). Attach the
terminal output of the host and of the guest: lines starting `halo-linux:
forge` say what was sent and taken. `debug.txt` in the data folder
(`assets/`) is the game's own log.

Developers: `tools/forge_net_test.sh` runs a host and a guest on one machine
without the menus and checks that a spawn, a move and a removal get through.
