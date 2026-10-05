/*
HALO_FORGE.H

The in-game dev tools of the native ports, on in local and system link
games of the Forge game type (a slayer variant among the default game
types), or in every game with HALO_FORGE=all (port/linux/game/forge.c). Shared by the game, through
halo_linux_source_fixups.h, and the platform layer, which reads their keys
(port/linux/src/xinput_sdl.c).
*/

#ifndef __HALO_FORGE_H
#define __HALO_FORGE_H

#include <stddef.h>

/* keyboard, mouse / first gamepad */
struct halo_linux_forge_keys
{
	int toggle_flying; /* home / D-pad up */
	int menu; /* 1 / D-pad right, only while flying */
	int up; /* R / right shoulder (RB) */
	int down; /* F / left shoulder (LB) */
	int fast; /* shift / left trigger (LT) */
	/* the keyboard's move keys ([controls], W A S D), which in the game are
	actions rather than controller 1's left stick: -1 to 1, forward and right
	positive */
	int move_forward;
	int move_right;
	int faster; /* while flying, the speed: arrow up and down */
	int slower;
	int menu_up; /* arrows / D-pad */
	int menu_down;
	int menu_left;
	int menu_right;
	int menu_select; /* enter, left button / A */
	int menu_close; /* escape, backspace / B */
	/* the right mouse button: closes the menu, and held while flying the
	mouse turns what is held */
	int mouse_right;
	int grab; /* F4 */
	int rotation_axis; /* T */
	int rotation_snap; /* V */
	int remove_object; /* delete / back */
	int tab_previous; /* page up / left shoulder (LB) */
	int tab_next; /* page down / right shoulder (RB) */
	int pad_menu; /* X: the menu while flying, the step while holding */
	int pad_remove; /* Y: remove the object under the crosshair or held */
	int pad_turn; /* right trigger (RT) held: the right stick turns what is held */
	float pad_look_x; /* the right stick, -1 to 1, right and up positive */
	float pad_look_y;
};
/* TRUE while the tools are on: a local or system link game of the Forge
game type (GAME_VARIANT_FORGE_FLAG), or any game with HALO_FORGE=all; for
the platform layer and for mods */
int forge_mode_on(void);
/* the keys as held now; FALSE, with nothing held, unless the tools are on
and the console is closed */
int halo_linux_forge_read_keys(struct halo_linux_forge_keys *keys);
/* while TRUE (the menu is open, or an object is being placed), the menu's
keys and buttons stop driving controller 1 */
void halo_linux_forge_capture_menu_keys(int capture);
/* while TRUE (the camera flies), the D-pad's up and the shoulder buttons
stop driving controller 1 */
void halo_linux_forge_set_flying(int flying);
/* while TRUE (a name is being typed, forge_text_entry_begin), the keyboard
drives neither controller 1 nor the mods' keys (halo_mod_key_down);
halo_linux_forge_read_text_keys still reads it: whether each of the first
count keys is held, by SDL scancode, all at once (nothing held while the
console is open) */
void halo_linux_forge_capture_text(int capture);
void halo_linux_forge_read_text_keys(unsigned char *keys, int count);
void forge_update(void);
void forge_render(void);

/* for the flying camera (camera/director.c), about local player 0: TRUE
while the menu is open (the rise and sink keys are then the menu's); TRUE
while the right stick turns a held object rather than looking; above 0,
the distance the right stick orbits the camera at around a held object;
forge_camera_zoom takes a step towards the held object (negative away)
while it is turned, and returns the step allowed by the nearest and
farthest it may be; and, after each update of the flying camera, where it
is and looks */
int forge_menu_is_open(void);
int forge_camera_turning(void);
/* the mouse's look while the right button turns a held object, radians */
void forge_camera_turn_held(float yaw, float pitch);
float forge_camera_orbit_distance(void);
float forge_camera_zoom(float step);
void forge_flying_camera_moved(float const position[3], float const forward[3]);

/* asks for a line of text, typed on the keyboard over the tools' menu:
letters, digits, space and a few marks; backspace deletes, enter (or A)
takes it and calls done with it, escape (or B) leaves it. initial is what
the line starts as, maximum_length the most characters it may have. FALSE
when a line is being typed already. */
int forge_text_entry_begin(char const *prompt, char const *initial, unsigned long maximum_length,
	void (*done)(char const *text));

/* layouts (port/linux/game/forge_layout.c) */
/* a new map, after the mods' new map hooks: notes the map's own objects,
then, in a local game, loads the layout chosen to play on the map */
void forge_layout_new_map(void);
/* an object the tools or a mod made (spawned, pasted, brought back), kept
by the layout */
void forge_layout_note_spawned(long object_index);
/* an object the tools put down (a map's own one is saved as moved, any
other as made), and one they are about to remove (a map's own one is saved
as removed): call before object_delete */
void forge_layout_note_placed(long object_index);
void forge_layout_note_removed(long object_index);
/* the objects the tools and the mods made that the layout keeps, and the
most it keeps: one made past that is not saved with it (the spawn menu makes
none then, and says so) */
void forge_layout_object_budget(short *used, short *maximum);

/* layouts shown as maps of their own in the multiplayer map list of local
games (the "Show in the map list" row): after the game's 13 maps, with the
picture of the map they are made on (its index in that list, 0 to 12) and
the layout's name. forge_custom_maps_refresh reads them again and returns
how many there are; forge_custom_map_select chooses the one to play the
next game with (NONE: none, a map of the game), and forge_custom_map_selected
is that one's index, or NONE. By name: the base map's short name
("bloodgulch") and the layout's number, for game.start_layout. */
short forge_custom_maps_refresh(void);
short forge_custom_map_base_index(short index);
char const *forge_custom_map_base_name(short index);
wchar_t const *forge_custom_map_title(short index);
/* what the layout's "description" line says, or NULL when it has none */
wchar_t const *forge_custom_map_description(short index);
void forge_custom_map_select(short index);
short forge_custom_map_selected(void);
int forge_custom_map_select_by_name(char const *map_name, short slot);
/* system link (port/linux/game/network_distributed.c): the host sends the
layout of the map to a client that has loaded it, and again to every
client when the tools or a mod have changed it; the client applies what
the host's objects do not bring (scenery, devices, the mods' parts). While
it does, forge_layout_loading_for_client is TRUE, and mods leave out what
is the host's (forge_ai's characters); while the host writes that text,
forge_layout_saving_for_client is TRUE, and mods leave the same out of
what they save (it only moves about, and would be sent over and over). */
void forge_layout_send_to_client(long machine_index);
void forge_layout_handle_message(void const *payload, unsigned long size);
int forge_layout_loading_for_client(void);
int forge_layout_saving_for_client(void);
/* a client's tools change nothing themselves: they ask the host, which
spawns an object of the definition at position, or moves or removes its
object (object_index when the client has the host's object at that index,
else the one of the definition nearest original). FALSE when this machine
is not a client. The host's answer is its objects and its layout. */
enum
{
	_forge_edit_spawn = 0,
	_forge_edit_move,
	_forge_edit_remove
};
int forge_layout_client_edit(int kind, long object_index, long definition_index, float const original[3],
	float const position[3], float const forward[3], float const up[3]);
void forge_layout_handle_edit(void const *payload, unsigned long size);
/* the base map's name as the game shows it ("Blood Gulch") */
char const *forge_custom_map_base_title(short index);
/* string indices, past the end of every string list, that the menus show
the Forge maps' names and descriptions with (text/text_group.c) */
#define FORGE_CUSTOM_MAP_NAME_STRING 1000
#define FORGE_CUSTOM_MAP_DESCRIPTION_STRING 2000
#define FORGE_CUSTOM_MAP_STRING_COUNT 64

/* for source mods (halo_mod.h), about local player 0 */
/* a line of text in the tools' menu font (the high-res one with
display.high_res_text), as halo_mod_draw_text draws one: its line's height,
a text's width, and the text at the left, right or center of x0 to x1 with
its line's top at y, colour 0xAARRGGBB */
short forge_label_height(void);
short forge_label_width(char const *text);
void forge_draw_label(short x0, short y, short x1, int justification, unsigned long argb, char const *text);
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
/* the surface point the crosshair points at and its normal, or, in the
open, a point a little in front of the camera with an upward normal; FALSE
when the camera is outside the map */
int forge_point_at_crosshair(float position[3], float normal[3]);

/* a mod's own thing held like an object: aimed to move, turned, raised,
put down or cancelled with the tools' keys and buttons (so on a controller
too). While it is held the tools' menu keys belong to it and forge_busy()
is TRUE. */
struct halo_forge_hold_input
{
	/* the time since the last update, at most a tenth of a second */
	float seconds;
	/* the keys held now (D-pad or arrows) */
	int left, right, up, down;
	/* the same, once when pressed and again while held (repeating) */
	int left_pressed, right_pressed, up_pressed, down_pressed;
	/* T, and V or X */
	int axis_pressed, snap_pressed;
	/* enter, click or A; escape, right click or B; delete, Back or Y */
	int place, cancel, delete_pressed;
};

struct halo_forge_hold
{
	/* what is held: the text drawn while it is */
	char const *name;
	/* more about it, or NULL (not drawn: only the name is) */
	void (*describe)(char *line, unsigned long size);
	/* once a frame while it is held; returns TRUE when it is done (put
	down, cancelled or removed). With a NULL input the hold has been cut
	short (a new map, a checkpoint): put things back and return TRUE. */
	int (*update)(struct halo_forge_hold_input const *input);
};

/* starts holding; FALSE when another hold is going or an
object is held. The hold is called from the next frame on. */
int forge_mod_hold_begin(struct halo_forge_hold const *hold);

#endif
