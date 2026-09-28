/*
HALO_FORGE.H

The in-game dev tools of the native ports, off unless HALO_FORGE is set
(port/linux/game/forge.c). Shared by the game, through
halo_linux_source_fixups.h, and the platform layer, which reads their keys
(port/linux/src/xinput_sdl.c).
*/

#ifndef __HALO_FORGE_H
#define __HALO_FORGE_H

/* keyboard, mouse / first gamepad */
struct halo_linux_forge_keys
{
	int toggle_flying; /* F2 / D-pad left */
	int menu; /* F3 / D-pad right */
	int up; /* space / right trigger */
	int down; /* left ctrl, C / left trigger */
	int fast; /* shift / left stick click */
	int menu_up; /* arrows / D-pad */
	int menu_down;
	int menu_left;
	int menu_right;
	int menu_select; /* enter, left button / A */
	int menu_close; /* escape, backspace, right button / B */
	int grab; /* F4 */
	int rotation_axis; /* T / X */
	int rotation_snap; /* V / Y */
	int remove_object; /* delete / back */
};
/* the keys as held now; FALSE, with nothing held, unless HALO_FORGE is set
and the console is closed */
int halo_linux_forge_read_keys(struct halo_linux_forge_keys *keys);
/* while TRUE (the menu is open, or an object is being placed), the menu's
keys and buttons stop driving controller 1 */
void halo_linux_forge_capture_menu_keys(int capture);
void forge_update(void);
void forge_render(void);

/* for source mods (halo_mod.h), about local player 0 */
/* TRUE while the spawn menu is open or an object is held: the tools then
have the menu keys, delete included */
int forge_busy(void);
/* the object under the crosshair (flying) or in the player's view that the
tools would pick up, or -1 (NONE): never the player, what they ride or
carry, nor projectiles, sound scenery and placeholders */
long forge_object_at_crosshair(void);
/* where an object of the definition would be placed at the crosshair,
standing on the surface there; FALSE when the camera is outside the map */
int forge_placement_at_crosshair(long definition_index, float position[3]);

#endif
