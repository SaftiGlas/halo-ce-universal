/*
FORGE.C

In-game dev tools for the native ports (port/linux, port/windows,
port/android): on in local and system link games of the Forge game type,
the last of the default game types (a slayer variant with GAME_VARIANT_FORGE_FLAG,
game/game_engine.c), or in every game with HALO_FORGE=all (see
port/linux/README.md, "Dev tools"). The byte-matching build never compiles
this file.

	F2 / D-pad up
	        detach the camera from the player and fly it freely, through
	        walls, with a crosshair (forge mode); again moves the player to
	        the camera and returns the camera to the player
	F3 / D-pad right or X, only while flying
	        open or close the spawn menu: every vehicle, weapon, equipment,
	        biped and scenery object of the map (the menu is part of forge
	        mode: landing the camera closes it)
	F4, and while flying also enter, the left mouse button or A
	        pick up the object under the crosshair
	Y, while flying
	        remove the object under the crosshair (Delete with forge_edit)

While flying: W A S D (left stick) move, the mouse (right stick) looks,
space and left ctrl or C (RB and LB) rise and sink straight up and down,
shift (the left trigger, LT) is faster, and the arrows' up and down change the
speed. Z (right stick click) hands the controls back to the player and
leaves the camera where it is, as the game's own debug flying camera does
(camera/director.c).

In the menu: up and down choose, left and right change the category, enter,
the left mouse button or A take the object, escape, backspace, the right
mouse button or B close the menu. After the object categories come the
pages that source mods add (halo_mod.h, struct halo_mod_menu): rows with a
value that left and right change and that enter or A act on. T and V, or LB
and RB, go to the previous and next tab on every page; X closes the menu too.

The menu (with the forge_ui mod, mods/forge_ui) is drawn after the forge
menu of Halo: Reach: a dark panel with the categories down its left side
(the map's objects under "spawn", the mods' pages under "tools"), the
chosen category's rows beside them with a scroll bar, the chosen object's
whole tag name under them, and the keys on caps along the bottom. The
highlights glide to the chosen rows and the panel fades in. It keeps its
place while it is closed: the category, and in each category the chosen row
(by tag name, so it carries over to other maps that have the same tag) and
how far the list is scrolled. The place lasts for the whole session and is
written to u:\forge_ui.txt (forge_ui.txt in the directory u/ of the save
root, ~/.local/share/halo-linux/u/) when the menu closes, and read back the
first time it opens after the game starts. A closed menu does nothing else.

A taken or picked up object is held where the crosshair points (or, not
flying, where the player looks), standing on the surface there, until it
is placed:
	left, right     turn it about the chosen axis
	up, down        raise and lower it
	T               choose the axis: yaw, pitch or roll
	V / X           choose the step: free, or 15, 45, 90 or 180 degrees
	enter, left mouse button / A
	                put it down
	escape, backspace, right mouse button / B
	                cancel: a spawned object goes, a picked up one returns
	delete / back, Y
	                remove it from the map
While flying, the right stick orbits the camera around the held object,
which then stays that far in front of the camera wherever it flies (LB and
RB lower and raise both), and with the right trigger (RT) held the right
stick turns the object instead: left and right about its yaw, up and down
about its pitch, while the left stick's forward and back take the camera
nearer to it and further away.
The platform layer keeps these keys and buttons from the game while the
menu is open or an object is held.

The keys belong to the player on controller 1, the keyboard and the first
gamepad, and act on local player 0.

System link: every player of a Forge game builds. Spawning, moving and
removing objects change the game state, which is the host's
(network_distributed.c): the host's tools change it as in a local game, and
its clients see it as they see the rest of its game (its units and items
through network_objects.c, its scenery, devices and the mods' parts through
the layout it sends, forge_layout.c). A client's tools ask the host
(forge_layout_client_edit): the menu's object is spawned where the
crosshair points, to be picked up from there; an object picked up moves on
the client alone until it is put down, which the host then does; a removed
one the host removes. The mods' pages and the Map tab are the host's.

A page may ask for a line of text (forge_text_entry_begin, the Map tab's
names and descriptions): it is typed on the keyboard, which until enter or
escape belongs to it alone.
*/

#include "cseries.h"
#include "cache/cache_files.h"
#include "camera/director.h"
#include "camera/director_forge.h"
#include "camera/observer.h"
#include "cseries/cseries_windows.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "interface/interface.h"
#include "interface/terminal.h"
#include "math/integer_math.h"
#include "math/real_math.h"
#include "objects/object_definitions.h"
#include "objects/object_types.h"
#include "objects/objects.h"
#include "physics/collisions.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "scenario/scenario.h"
#include "tag_files/tag_files.h"
#include "tag_files/tag_groups.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "text/text_group.h"
#include "units/units.h"

#include <stdio.h>
#include <stdlib.h>

/* ---------- constants */

enum
{
	FORGE_LOCAL_PLAYER_INDEX = 0,
	FORGE_MAXIMUM_MENU_ENTRIES = 1024,
	FORGE_MENU_VISIBLE_ENTRIES = 8,
	FORGE_MENU_MARGIN = 24,
	/* the menu's panel (forge_ui): padding inside it, its widest, the share
	of it that is the list of categories, the scroll bar's width, the gap
	between hints, and the fade in and highlight glide speeds (per second) */
	FORGE_MENU_PADDING = 5,
	FORGE_MENU_MAX_WIDTH = 400,
	/* the menu's font fits this many lines on the screen: smaller than the
	mods' large font, so the menu leaves most of the view free */
	FORGE_MENU_FONT_LINES = 26,
	FORGE_MENU_CATEGORY_PERCENT = 28,
	FORGE_MENU_SCROLL_WIDTH = 3,
	FORGE_MENU_HINT_GAP = 14,
	FORGE_MENU_FADE_SPEED = 8,
	FORGE_MENU_GLIDE_SPEED = 22,
	/* a menu not drawn for this long is opening again */
	FORGE_MENU_FRESH_MILLISECONDS = 150,
	FORGE_CATEGORY_COUNT = 5,
	FORGE_TAG_NAME_SIZE = 256,
	FORGE_KEY_REPEAT_DELAY_MILLISECONDS = 400,
	FORGE_KEY_REPEAT_INTERVAL_MILLISECONDS = 70,
	/* surfaces of objects the aim passes through (the player's own vehicle,
	objects that cannot be picked up) */
	FORGE_MAXIMUM_AIM_TESTS = 4
};

/* draw_string's justifications (text/draw_string.c) */
enum
{
	_text_justification_left = 0,
	_text_justification_right,
	_text_justification_center
};

enum forge_axis
{
	_forge_axis_yaw = 0,
	_forge_axis_pitch,
	_forge_axis_roll,
	NUMBER_OF_FORGE_AXES
};

/* what can be picked up: not projectiles, sound scenery or placeholders */
#define FORGE_PICKABLE_OBJECT_MASK (_object_mask_unit | _object_mask_item | _object_mask_scenery | _object_mask_device)

/* what the pick up aim hits: the structure and every object but projectiles */
#define FORGE_PICK_COLLISION_FLAGS (FLAG(_collision_test_front_facing_surfaces_bit) | \
	FLAG(_collision_test_ignore_invisible_surfaces_bit) | \
	FLAG(_collision_test_structure_bit) | \
	FLAG(_collision_test_objects_bit) | \
	(_collision_test_objects_all_types_flags & ~FLAG(_collision_test_objects_projectiles_bit)))

/* world units the crosshair reaches; beyond it, or in the open, objects are
held this far in front of the camera instead */
#define FORGE_AIM_RANGE 200.f
#define FORGE_AIM_DISTANCE 3.f
/* how far past a surface the aim goes on when it passes through it */
#define FORGE_AIM_STEP 0.01f
/* items (weapons, equipment) lie without collision geometry and are picked
up by their bounding sphere, at least this big */
#define FORGE_ITEM_PICK_MINIMUM_RADIUS 0.25f
/* a held object turns half a circle, and rises two world units, a second */
#define FORGE_TURN_SPEED _pi
#define FORGE_RAISE_SPEED 2.f
/* how near and far an object held in front of the flying camera may be */
#define FORGE_ORBIT_MINIMUM_DISTANCE 1.5f
#define FORGE_ORBIT_MAXIMUM_DISTANCE 50.f

/* the menu's colours (forge_ui), 0xAARRGGBB: the steel blue and pale
cyan of Reach's menus */
#define FORGE_MENU_PANEL_COLOR 0xd00e1620UL
#define FORGE_MENU_TITLE_BAR_COLOR 0xe0182636UL
#define FORGE_MENU_SIDE_COLOR 0x60000000UL
#define FORGE_MENU_HINT_BAR_COLOR 0xe0080e14UL
#define FORGE_MENU_ACCENT_COLOR 0xff62c8ffUL
#define FORGE_MENU_ACCENT_DIM_COLOR 0xff3f7fa6UL
#define FORGE_MENU_BAR_COLOR 0x9a2a86c4UL
#define FORGE_MENU_BAR_EDGE_COLOR 0xffbfeaffUL
#define FORGE_MENU_RULE_COLOR 0x60a0d0f0UL
#define FORGE_MENU_CAP_COLOR 0xff34495eUL
#define FORGE_MENU_CAP_EDGE_COLOR 0xff8fb4d0UL
#define FORGE_MENU_TITLE_TEXT_COLOR 0xffbfeaffUL
#define FORGE_MENU_TEXT_COLOR 0xffc9d6e0UL
#define FORGE_MENU_SELECTED_TEXT_COLOR 0xffffffffUL
#define FORGE_MENU_DIM_TEXT_COLOR 0xff7f95a8UL

/* where the menu's place is kept (see above) */
#define FORGE_MENU_STATE_FILE "u:\\forge_ui.txt"

/* ---------- structures */

struct forge_category
{
	short object_type;
	char const *name;
};

struct forge_snap
{
	real step;
	char const *name;
};

struct forge_key
{
	boolean down;
	unsigned long repeat_milliseconds;
};

struct forge_globals
{
	long held_object_index;
	boolean held_spawned;
	short held_axis;
	short held_snap_index;
	real held_angles[NUMBER_OF_FORGE_AXES];
	real held_height;
	real_point3d held_original_position;
	real_vector3d held_original_forward;
	real_vector3d held_original_up;
	/* held in front of the flying camera, this far, since the right stick
	orbited it (rather than where the crosshair points) */
	boolean held_anchored;
	real held_distance;
	/* the right trigger turns it with the right stick */
	boolean held_turning;

	boolean active;
	boolean menu_open;
	boolean keys_captured;
	short category_index;
	short entry_count;
	short selected_entry_index;
	/* the first row shown */
	short first_entry_index;
	/* how many objects each object category has in this map */
	short category_counts[FORGE_CATEGORY_COUNT];
	/* the menu's place in each category (see above) */
	char category_selected_names[FORGE_CATEGORY_COUNT][FORGE_TAG_NAME_SIZE];
	short category_first_entry_indices[FORGE_CATEGORY_COUNT];
	boolean menu_state_loaded;
	boolean menu_state_changed;
	long entries[FORGE_MAXIMUM_MENU_ENTRIES];
	long game_time;
	unsigned long milliseconds;

	struct forge_key toggle_flying_key;
	struct forge_key menu_key;
	struct forge_key menu_up_key;
	struct forge_key menu_down_key;
	struct forge_key menu_left_key;
	struct forge_key menu_right_key;
	struct forge_key menu_select_key;
	struct forge_key menu_close_key;
	struct forge_key grab_key;
	struct forge_key rotation_axis_key;
	struct forge_key rotation_snap_key;
	struct forge_key remove_object_key;
	struct forge_key tab_previous_key;
	struct forge_key tab_next_key;
	struct forge_key pad_menu_key;
	struct forge_key pad_remove_key;

	/* a mod's own thing held (halo_forge.h), and when it was last updated */
	struct halo_forge_hold const *mod_hold;
	unsigned long mod_hold_milliseconds;

	/* where the flying camera is and looks, after its last update */
	boolean camera_valid;
	real_point3d camera_position;
	real_vector3d camera_forward;
};

/* ---------- globals */

static struct forge_category const forge_categories[FORGE_CATEGORY_COUNT] =
{
	{ _object_type_vehicle, "Vehicles" },
	{ _object_type_weapon, "Weapons" },
	{ _object_type_equipment, "Equipment" },
	{ _object_type_biped, "Bipeds" },
	{ _object_type_scenery, "Scenery" }
};

static char const *const forge_axis_names[NUMBER_OF_FORGE_AXES] =
{
	"yaw",
	"pitch",
	"roll"
};

/* a step of 0 turns freely while the key is held */
static struct forge_snap const forge_snaps[] =
{
	{ 0.f, "free" },
	{ DEGREES_TO_RADIANS(15), "15 degrees" },
	{ DEGREES_TO_RADIANS(45), "45 degrees" },
	{ DEGREES_TO_RADIANS(90), "90 degrees" },
	{ DEGREES_TO_RADIANS(180), "180 degrees" }
};

static struct forge_globals forge_globals = { NONE };

/* a line of text being typed (forge_text_entry_begin); the keys are SDL
scancodes (halo_linux_forge_read_text_keys) */
enum
{
	FORGE_TEXT_MAXIMUM_LENGTH = 127,
	FORGE_TEXT_KEY_COUNT = 57,
	FORGE_TEXT_KEY_A = 4,
	FORGE_TEXT_KEY_1 = 30,
	FORGE_TEXT_KEY_ESCAPE = 41,
	FORGE_TEXT_KEY_BACKSPACE = 42,
	FORGE_TEXT_KEY_SPACE = 44,
	FORGE_TEXT_KEY_LEFT_SHIFT = 225,
	FORGE_TEXT_KEY_RIGHT_SHIFT = 229,
	/* the keys read each frame: all up to the right shift */
	FORGE_TEXT_KEYS_READ = 230
};

/* what the keys from 1 on type, plain and with shift (a US keyboard); a
space where a key types nothing (the space bar is read by itself, and a
percent sign would be taken for a format by the menus' strings) */
static char const forge_text_plain[] = "1234567890     -=[]  ;'`,./";
static char const forge_text_shifted[] = "!@#$ ^&*()     _+()  :\"~<>?";

static struct
{
	boolean active;
	/* the keys held when it began do not type */
	boolean primed;
	char const *prompt;
	void (*done)(char const *text);
	unsigned long maximum_length;
	unsigned long length;
	char text[FORGE_TEXT_MAXIMUM_LENGTH + 1];
	struct forge_key keys[FORGE_TEXT_KEY_COUNT];
} forge_text_entry_globals;

/* ---------- private code */

/* TRUE when the key goes down, and again every repeat interval while it is
held past the repeat delay, if it repeats */
static boolean forge_key_pressed(
	struct forge_key *key,
	int down,
	boolean repeats,
	unsigned long milliseconds)
{
	boolean pressed = FALSE;

	if (!down)
	{
		key->down = FALSE;
	}
	else if (!key->down)
	{
		key->down = TRUE;
		key->repeat_milliseconds = milliseconds + FORGE_KEY_REPEAT_DELAY_MILLISECONDS;
		pressed = TRUE;
	}
	else if (repeats && (long)(milliseconds - key->repeat_milliseconds) >= 0)
	{
		key->repeat_milliseconds = milliseconds + FORGE_KEY_REPEAT_INTERVAL_MILLISECONDS;
		pressed = TRUE;
	}

	return pressed;
}

/* whether this machine's game is the game's (a local game, the host of a
system link game), and whether it is a system link client, whose tools ask
the host (forge_layout_client_edit); neither in a saved film */
static boolean forge_authoritative(
	void)
{
	return halo_mods_authoritative();
}

static boolean forge_client(
	void)
{
	return game_connection() == _game_connection_network_client;
}

/* the object the player is: the vehicle they ride, else their unit */
static long forge_player_object_index(
	short local_player_index)
{
	long player_index = local_player_get_player_index(local_player_index);
	long object_index = NONE;

	if (player_index != NONE)
	{
		long unit_index = player_get(player_index)->unit_index;

		if (unit_index != NONE)
		{
			object_index = unit_get(unit_index)->object.parent_object_index;
			if (object_index == NONE)
				object_index = unit_index;
		}
	}

	return object_index;
}

/* forward and up of yaw (about the world's up), then pitch (the nose up),
then roll (about forward) */
static void forge_orientation_from_angles(
	real const angles[NUMBER_OF_FORGE_AXES],
	real_vector3d *forward,
	real_vector3d *up)
{
	real yaw = angles[_forge_axis_yaw];
	real pitch = angles[_forge_axis_pitch];
	real roll = angles[_forge_axis_roll];

	set_real_vector3d(forward, cosine(pitch) * cosine(yaw), cosine(pitch) * sine(yaw), sine(pitch));
	set_real_vector3d(up, -sine(pitch) * cosine(yaw), -sine(pitch) * sine(yaw), cosine(pitch));
	rotate_vector_about_axis(up, forward, sine(roll), cosine(roll));

	return;
}

/* the angles of an orientation, as forge_orientation_from_angles builds it */
static void forge_angles_from_orientation(
	real_vector3d const *forward,
	real_vector3d const *up,
	real angles[NUMBER_OF_FORGE_AXES])
{
	real_vector3d unrolled_forward;
	real_vector3d unrolled_up;
	real_vector3d side;

	angles[_forge_axis_yaw] = arctangent(forward->j, forward->i);
	angles[_forge_axis_pitch] = arctangent(
		forward->k,
		square_root(forward->i * forward->i + forward->j * forward->j));
	angles[_forge_axis_roll] = 0.f;
	forge_orientation_from_angles(angles, &unrolled_forward, &unrolled_up);
	/* rolling turns up towards forward x up */
	cross_product3d(&unrolled_forward, &unrolled_up, &side);
	angles[_forge_axis_roll] = arctangent(dot_product3d(up, &side), dot_product3d(up, &unrolled_up));

	return;
}

/* the level direction of a yaw */
static void forge_forward_from_yaw(
	real yaw,
	real_vector3d *forward)
{
	set_real_vector3d(forward, cosine(yaw), sine(yaw), 0.f);

	return;
}

/* where the crosshair points: the first surface along the camera's view,
past the player (and what they ride) and the ignored object, up to the aim
range; else the aim distance in front of the camera, with an upward
normal. FALSE when the camera is outside the map. */
static boolean forge_aim(
	short local_player_index,
	long ignore_object_index,
	real_point3d *point,
	real_vector3d *normal)
{
	struct observer_result const *camera = observer_get_camera(local_player_index);
	long player_object_index = forge_player_object_index(local_player_index);
	real_point3d origin = camera->position;
	real_vector3d ray;
	boolean hit = FALSE;
	short test_index;

	if (camera->location.cluster_index == NONE)
		return FALSE;

	scale_vector3d(&camera->forward, FORGE_AIM_RANGE, &ray);
	for (test_index = 0; !hit && test_index < FORGE_MAXIMUM_AIM_TESTS; test_index++)
	{
		struct collision_result collision;

		if (!collision_test_vector(
			_collision_test_for_line_of_sight_flags,
			&origin,
			&ray,
			ignore_object_index,
			&collision))
		{
			break;
		}

		if (collision.type == _collision_result_object &&
			player_object_index != NONE &&
			collision.object_index == player_object_index)
		{
			/* through the player's own vehicle */
			origin.x = collision.point.x + camera->forward.i * FORGE_AIM_STEP;
			origin.y = collision.point.y + camera->forward.j * FORGE_AIM_STEP;
			origin.z = collision.point.z + camera->forward.k * FORGE_AIM_STEP;
		}
		else
		{
			*point = collision.point;
			*normal = collision.plane.n;
			hit = TRUE;
		}
	}

	if (!hit)
	{
		real_vector3d forward;

		forge_forward_from_yaw(arctangent(camera->forward.j, camera->forward.i), &forward);
		point->x = camera->position.x + forward.i * FORGE_AIM_DISTANCE;
		point->y = camera->position.y + forward.j * FORGE_AIM_DISTANCE;
		point->z = camera->position.z;
		*normal = *global_up3d;
	}

	return TRUE;
}

/* the object that would be picked up for this object: what it is attached
to, if anything; NONE for the player, what they ride and what they carry,
and for objects that cannot be picked up */
static long forge_pickable_object_index(
	short local_player_index,
	long object_index)
{
	long result = object_get_ultimate_parent(object_index);
	long player_index = local_player_get_player_index(local_player_index);
	long unit_index = player_index != NONE ? player_get(player_index)->unit_index : NONE;

	if (!object_try_and_get_and_verify_type(result, FORGE_PICKABLE_OBJECT_MASK) ||
		(unit_index != NONE && result == object_get_ultimate_parent(unit_index)))
	{
		result = NONE;
	}

	return result;
}

/* the object under the crosshair, or NONE: the first object surface along
the view before the structure, or an item whose bounding sphere the view
passes through nearer than that */
static long forge_pick(
	short local_player_index)
{
	struct observer_result const *camera = observer_get_camera(local_player_index);
	real_point3d origin = camera->position;
	real_vector3d ray;
	real nearest_distance = FORGE_AIM_RANGE;
	long picked_object_index = NONE;
	short test_index;

	scale_vector3d(&camera->forward, FORGE_AIM_RANGE, &ray);
	for (test_index = 0; test_index < FORGE_MAXIMUM_AIM_TESTS; test_index++)
	{
		struct collision_result collision;
		long object_index;

		if (!collision_test_vector(FORGE_PICK_COLLISION_FLAGS, &origin, &ray, NONE, &collision))
			break;

		nearest_distance = distance3d(&camera->position, &collision.point);
		if (collision.type != _collision_result_object)
			break;

		object_index = forge_pickable_object_index(local_player_index, collision.object_index);
		if (object_index != NONE)
		{
			picked_object_index = object_index;
			break;
		}

		/* through what cannot be picked up */
		origin.x = collision.point.x + camera->forward.i * FORGE_AIM_STEP;
		origin.y = collision.point.y + camera->forward.j * FORGE_AIM_STEP;
		origin.z = collision.point.z + camera->forward.k * FORGE_AIM_STEP;
		nearest_distance = FORGE_AIM_RANGE;
	}

	{
		struct object_iterator iterator;
		struct object_datum *object;

		object_iterator_new(&iterator, _object_mask_item, 0);
		while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
		{
			real_vector3d to_center;
			real along;
			real radius = MAX(object->object.bounding_sphere_radius, FORGE_ITEM_PICK_MINIMUM_RADIUS);

			vector_from_points3d(&camera->position, &object->object.bounding_sphere_center, &to_center);
			along = dot_product3d(&to_center, &camera->forward);
			if (along > 0.f && along < nearest_distance &&
				magnitude_squared3d(&to_center) - along * along <= radius * radius &&
				forge_pickable_object_index(local_player_index, iterator.index) == iterator.index)
			{
				picked_object_index = iterator.index;
				nearest_distance = along;
			}
		}
	}

	return picked_object_index;
}

/* the object's origin on the aimed surface: out of it by the object's
radius, as the origin of most objects is at their middle (scenery stands
on its origin), then raised by the held height */
static void forge_held_position(
	long definition_index,
	real_point3d const *point,
	real_vector3d const *normal,
	real height,
	real_point3d *position)
{
	struct object_definition *definition = object_definition_get(definition_index);
	real lift = definition->object.type == _object_type_scenery
		? 0.f
		: definition->object.bounding_radius;

	position->x = point->x + normal->i * lift;
	position->y = point->y + normal->j * lift;
	position->z = point->z + normal->k * lift + height;

	return;
}

static void forge_return_to_player(
	short local_player_index)
{
	struct observer_result const *camera = observer_get_camera(local_player_index);
	real_point3d position = camera->position;
	real_vector3d forward = camera->forward;
	long object_index = forge_player_object_index(local_player_index);

	if (object_index == NONE)
	{
		terminal_printf(global_real_argb_green, "forge: camera returned to the player");
	}
	else if (!forge_authoritative())
	{
		/* (a client's own player is where the host takes it to be) */
		terminal_printf(global_real_argb_green, "forge: camera returned to the player");
	}
	else if (camera->location.cluster_index == NONE)
	{
		terminal_printf(global_real_argb_orange, "forge: the camera is outside the map, the player stays");
	}
	else
	{
		object_set_position(object_index, &position, NULL, NULL);
		player_control_set_facing(local_player_index, &forward);
		terminal_printf(global_real_argb_green, "forge: player moved to the camera");
	}

	director_forge_set_flying(local_player_index, FALSE);

	return;
}

static void forge_toggle_flying(
	short local_player_index)
{
	if (director_forge_flying(local_player_index))
	{
		forge_return_to_player(local_player_index);
	}
	else if (*director_camera_scripted)
	{
		terminal_printf(global_real_argb_orange, "forge: the camera is scripted");
	}
	else
	{
		director_forge_set_flying(local_player_index, TRUE);
		terminal_printf(global_real_argb_green, "forge: flying camera");
	}

	return;
}

static struct halo_mod_menu const *forge_menu_page(
	void);

static int forge_compare_entries(
	void const *a,
	void const *b)
{
	return _stricmp(tag_get_name(*(long const *)a), tag_get_name(*(long const *)b));
}

/* the mod page of the menu's current tab, or NULL on an object category */
static struct halo_mod_menu const *forge_menu_page(
	void)
{
	short page_index = (short)(forge_globals.category_index - NUMBEROF(forge_categories));

	return page_index >= 0 ? halo_mods_menu_page(page_index) : NULL;
}

/* the keys on a mod's page: choose a row, change its value, act on it */
static void forge_menu_page_keys(
	struct halo_mod_menu const *page,
	boolean up,
	boolean down,
	boolean left,
	boolean right,
	boolean select)
{
	short count = page->row_count();

	forge_globals.entry_count = count;
	forge_globals.selected_entry_index = (short)PIN(forge_globals.selected_entry_index, 0, MAX(count - 1, 0));
	if (count > 0)
	{
		if (up)
			forge_globals.selected_entry_index = (short)((forge_globals.selected_entry_index + count - 1) % count);
		if (down)
			forge_globals.selected_entry_index = (short)((forge_globals.selected_entry_index + 1) % count);
		if (page->row_change)
		{
			if (left != right)
				page->row_change(forge_globals.selected_entry_index, right ? 1 : -1);
			if (select && page->row_change(forge_globals.selected_entry_index, 0))
				forge_globals.menu_open = FALSE;
		}
	}

	return;
}

/* scrolls the list as little as it takes to show the chosen row */
static void forge_menu_scroll_to_selection(
	void)
{
	short last_first_entry_index = (short)MAX(forge_globals.entry_count - FORGE_MENU_VISIBLE_ENTRIES, 0);

	if (forge_globals.selected_entry_index < forge_globals.first_entry_index)
		forge_globals.first_entry_index = forge_globals.selected_entry_index;
	if (forge_globals.selected_entry_index >= forge_globals.first_entry_index + FORGE_MENU_VISIBLE_ENTRIES)
		forge_globals.first_entry_index = (short)(forge_globals.selected_entry_index - FORGE_MENU_VISIBLE_ENTRIES + 1);
	forge_globals.first_entry_index = PIN(forge_globals.first_entry_index, 0, last_first_entry_index);

	return;
}

/* how many objects each object category has in the map */
static void forge_menu_count_categories(
	void)
{
	struct tag_iterator iterator;
	long tag_index;
	short category_index;

	for (category_index = 0; category_index < FORGE_CATEGORY_COUNT; category_index++)
		forge_globals.category_counts[category_index] = 0;
	tag_iterator_new(&iterator, OBJECT_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		short object_type = object_definition_get(tag_index)->object.type;

		for (category_index = 0; category_index < FORGE_CATEGORY_COUNT; category_index++)
		{
			if (forge_categories[category_index].object_type == object_type)
				forge_globals.category_counts[category_index]++;
		}
	}

	return;
}

/* the map's objects of the menu's category, by name */
static void forge_menu_build(
	void)
{
	struct halo_mod_menu const *page = forge_menu_page();
	short object_type;
	struct tag_iterator iterator;
	long tag_index;

	forge_menu_count_categories();
	if (page)
	{
		forge_globals.entry_count = page->row_count();
		forge_globals.selected_entry_index = 0;
		forge_globals.first_entry_index = 0;
		if (page->opened)
			page->opened();
		return;
	}

	object_type = forge_categories[forge_globals.category_index].object_type;
	forge_globals.entry_count = 0;
	tag_iterator_new(&iterator, OBJECT_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE &&
		forge_globals.entry_count < FORGE_MAXIMUM_MENU_ENTRIES)
	{
		if (object_definition_get(tag_index)->object.type == object_type)
			forge_globals.entries[forge_globals.entry_count++] = tag_index;
	}
	qsort(
		forge_globals.entries,
		forge_globals.entry_count,
		sizeof(forge_globals.entries[0]),
		forge_compare_entries);

	/* back where the menu was in this category */
	forge_globals.selected_entry_index = 0;
	{
		char const *selected_name = forge_globals.category_selected_names[forge_globals.category_index];
		short entry_index;

		for (entry_index = 0; *selected_name && entry_index < forge_globals.entry_count; entry_index++)
		{
			if (_stricmp(tag_get_name(forge_globals.entries[entry_index]), selected_name) == 0)
			{
				forge_globals.selected_entry_index = entry_index;
				break;
			}
		}
	}
	forge_globals.first_entry_index = forge_globals.category_first_entry_indices[forge_globals.category_index];
	forge_menu_scroll_to_selection();

	return;
}

/* keeps the menu's place in its category, for when it opens again */
static void forge_menu_remember(
	void)
{
	short category_index = forge_globals.category_index;
	char const *selected_name = forge_globals.entry_count > 0
		? tag_get_name(forge_globals.entries[forge_globals.selected_entry_index])
		: "";

	if (strcmp(forge_globals.category_selected_names[category_index], selected_name) != 0 ||
		forge_globals.category_first_entry_indices[category_index] != forge_globals.first_entry_index)
	{
		_snprintf(
			forge_globals.category_selected_names[category_index],
			FORGE_TAG_NAME_SIZE,
			"%s",
			selected_name);
		forge_globals.category_first_entry_indices[category_index] = forge_globals.first_entry_index;
		forge_globals.menu_state_changed = TRUE;
	}

	return;
}

/* reads the menu's place from the last session, once */
static void forge_menu_state_load(
	void)
{
	FILE *file;

	forge_globals.menu_state_loaded = TRUE;
	if ((file = fopen(FORGE_MENU_STATE_FILE, "r")) != NULL)
	{
		char line[FORGE_TAG_NAME_SIZE + 64];

		while (fgets(line, sizeof(line), file))
		{
			int category_index;
			int first_entry_index;
			int name_offset = 0;

			line[strcspn(line, "\r\n")] = 0;
			if (sscanf(line, "category %d", &category_index) == 1)
			{
				if (category_index >= 0 && category_index < FORGE_CATEGORY_COUNT)
					forge_globals.category_index = (short)category_index;
			}
			else if (sscanf(line, "place %d %d %n", &category_index, &first_entry_index, &name_offset) >= 2 &&
				name_offset > 0 &&
				category_index >= 0 && category_index < FORGE_CATEGORY_COUNT &&
				first_entry_index >= 0 && first_entry_index < FORGE_MAXIMUM_MENU_ENTRIES)
			{
				_snprintf(
					forge_globals.category_selected_names[category_index],
					FORGE_TAG_NAME_SIZE,
					"%s",
					line + name_offset);
				forge_globals.category_first_entry_indices[category_index] = (short)first_entry_index;
			}
		}
		fclose(file);
	}

	return;
}

/* writes the menu's place for the next session */
static void forge_menu_state_save(
	void)
{
	FILE *file;

	forge_globals.menu_state_changed = FALSE;
	if ((file = fopen(FORGE_MENU_STATE_FILE, "w")) != NULL)
	{
		short category_index;

		fprintf(file, "# the forge menu's place (mods/forge_ui): its category, and in each\n");
		fprintf(file, "# category the first row shown and the chosen tag\n");
		fprintf(file, "category %d\n", forge_globals.category_index);
		for (category_index = 0; category_index < FORGE_CATEGORY_COUNT; category_index++)
		{
			fprintf(
				file,
				"place %d %d %s\n",
				category_index,
				forge_globals.category_first_entry_indices[category_index],
				forge_globals.category_selected_names[category_index]);
		}
		fclose(file);
	}
	else
	{
		terminal_printf(global_real_argb_orange, "forge: could not write %s", FORGE_MENU_STATE_FILE);
	}

	return;
}

/* starts holding an object, turned as it is */
static void forge_hold_begin(
	long object_index,
	boolean spawned)
{
	struct object_datum *object = object_get(object_index);

	forge_globals.held_object_index = object_index;
	forge_globals.held_spawned = spawned;
	forge_globals.held_height = 0.f;
	forge_globals.held_anchored = FALSE;
	forge_globals.held_turning = FALSE;
	forge_globals.held_original_position = object->object.position;
	forge_globals.held_original_forward = object->object.forward;
	forge_globals.held_original_up = object->object.up;
	forge_angles_from_orientation(&object->object.forward, &object->object.up, forge_globals.held_angles);

	return;
}

/* spawns the object where the crosshair points and holds it there */
static void forge_take(
	short local_player_index,
	long definition_index)
{
	struct observer_result const *camera = observer_get_camera(local_player_index);
	real_point3d point;
	real_vector3d normal;

	if (!forge_authoritative() && !forge_client())
	{
		terminal_printf(global_real_argb_orange, "forge: objects spawn only in a game being played");
	}
	else if (!forge_aim(local_player_index, NONE, &point, &normal))
	{
		terminal_printf(global_real_argb_orange, "forge: the camera is outside the map");
	}
	else
	{
		struct object_placement_data data;
		long object_index;

		object_placement_data_new(&data, definition_index, NONE);
		forge_held_position(definition_index, &point, &normal, 0.f, &data.position);
		forge_forward_from_yaw(arctangent(camera->forward.j, camera->forward.i), &data.forward);
		data.up = *global_up3d;

		if (forge_client())
		{
			/* the host makes it there; picked up from there to move it */
			forge_layout_client_edit(_forge_edit_spawn, NONE, definition_index, &data.position.x, &data.position.x,
				&data.forward.i, &data.up.i);
			terminal_printf(global_real_argb_green, "forge: asked the host for %s", tag_get_name(definition_index));
			return;
		}

		object_index = object_new(&data);
		if (object_index == NONE)
		{
			terminal_printf(global_real_argb_orange, "forge: %s could not be created", tag_get_name(definition_index));
		}
		else
		{
			/* kept by the layout (forge_layout.c) */
			forge_layout_note_spawned(object_index);
			forge_hold_begin(object_index, TRUE);
		}
	}

	return;
}

/* picks up the object under the crosshair */
static void forge_grab(
	short local_player_index)
{
	long object_index;

	if (!forge_authoritative() && !forge_client())
	{
		terminal_printf(global_real_argb_orange, "forge: objects move only in a game being played");
	}
	else if (observer_get_camera(local_player_index)->location.cluster_index == NONE)
	{
		terminal_printf(global_real_argb_orange, "forge: the camera is outside the map");
	}
	else if ((object_index = forge_pick(local_player_index)) == NONE)
	{
		terminal_printf(global_real_argb_orange, "forge: nothing to pick up there");
	}
	else
	{
		forge_hold_begin(object_index, FALSE);
	}

	return;
}

/* removes the object under the crosshair, or lets a mod do it (forge_edit,
which can bring it back) */
static void forge_remove_at_crosshair(
	short local_player_index)
{
	long object_index;

	if (!forge_authoritative() && !forge_client())
	{
		terminal_printf(global_real_argb_orange, "forge: objects change only in a game being played");
	}
	else if (forge_authoritative() && halo_mods_remove())
	{
		/* the mod removed it */
	}
	else if (observer_get_camera(local_player_index)->location.cluster_index == NONE)
	{
		terminal_printf(global_real_argb_orange, "forge: the camera is outside the map");
	}
	else if ((object_index = forge_pick(local_player_index)) == NONE)
	{
		terminal_printf(global_real_argb_orange, "forge: nothing to remove there");
	}
	else if (forge_client())
	{
		struct object_datum *object = object_get(object_index);

		forge_layout_client_edit(_forge_edit_remove, object_index, object->definition_index, &object->object.position.x,
			&object->object.position.x, &object->object.forward.i, &object->object.up.i);
		terminal_printf(global_real_argb_green, "forge: asked the host to remove %s",
			tag_get_name(object->definition_index));
	}
	else
	{
		terminal_printf(global_real_argb_green, "forge: removed %s",
			tag_get_name(object_get(object_index)->definition_index));
		forge_layout_note_removed(object_index);
		object_delete(object_index);
	}

	return;
}

/* turns the held angle of the chosen axis: freely while turning, or to the
next step of the snap */
static void forge_turn(
	real direction,
	boolean pressed,
	real seconds)
{
	real *angle = &forge_globals.held_angles[forge_globals.held_axis];
	real step = forge_snaps[forge_globals.held_snap_index].step;

	if (step == 0.f)
	{
		*angle += direction * FORGE_TURN_SPEED * seconds;
	}
	else if (pressed)
	{
		*angle = (real)floor(*angle / step + 0.5f) * step + direction * step;
	}

	return;
}

/* puts the held object at the position, turned as held, and still */
static void forge_hold_place(
	real_point3d const *position)
{
	struct object_datum *object = object_get(forge_globals.held_object_index);
	real_vector3d forward;
	real_vector3d up;

	forge_orientation_from_angles(forge_globals.held_angles, &forward, &up);
	object_set_position(forge_globals.held_object_index, position, &forward, &up);
	object->object.translational_velocity = *global_zero_vector3d;
	object->object.angular_velocity = *global_zero_vector3d;
	/* a system link host: not at rest, so that its clients are told where it
	is every tick (network_objects.c) and see it move as it is held */
	if (game_connection() == _game_connection_network_server)
		SET_FLAG(object->object.flags, _object_at_rest_bit, FALSE);

	return;
}

/* the held object in front of the flying camera (held_anchored) */
static void forge_hold_place_anchored(
	void)
{
	real_point3d position;

	position.x = forge_globals.camera_position.x + forge_globals.camera_forward.i * forge_globals.held_distance;
	position.y = forge_globals.camera_position.y + forge_globals.camera_forward.j * forge_globals.held_distance;
	position.z = forge_globals.camera_position.z + forge_globals.camera_forward.k * forge_globals.held_distance +
		forge_globals.held_height;
	forge_hold_place(&position);

	return;
}

/* keeps the held object where the crosshair points (or, anchored, in front
of the flying camera), turned and raised as asked, and still */
static void forge_hold(
	struct halo_linux_forge_keys const *keys,
	boolean left,
	boolean right,
	real seconds)
{
	struct object_datum *object = object_get(forge_globals.held_object_index);
	boolean flying = director_forge_flying(FORGE_LOCAL_PLAYER_INDEX);
	real_point3d point;
	real_vector3d normal;

	if (keys->menu_left && !keys->menu_right)
		forge_turn(1.f, left, seconds);
	else if (keys->menu_right && !keys->menu_left)
		forge_turn(-1.f, right, seconds);
	forge_globals.held_height += (real)((keys->menu_up != 0) - (keys->menu_down != 0)) *
		FORGE_RAISE_SPEED * seconds;

	/* the controller's right stick: with the right trigger it turns the
	object, else (camera/director.c) it orbits the camera around it, which
	from then on holds the object in front of it */
	forge_globals.held_turning = flying && keys->pad_turn;
	if (forge_globals.held_turning)
	{
		forge_globals.held_angles[_forge_axis_yaw] -= keys->pad_look_x * FORGE_TURN_SPEED * seconds;
		forge_globals.held_angles[_forge_axis_pitch] += keys->pad_look_y * FORGE_TURN_SPEED * seconds;
	}
	if (!flying || !forge_globals.camera_valid)
	{
		forge_globals.held_anchored = FALSE;
	}
	else if (!forge_globals.held_anchored &&
		(forge_globals.held_turning || keys->pad_look_x != 0.f || keys->pad_look_y != 0.f))
	{
		real_vector3d to_object;

		vector_from_points3d(&forge_globals.camera_position, &object->object.position, &to_object);
		forge_globals.held_distance = PIN(
			dot_product3d(&to_object, &forge_globals.camera_forward),
			FORGE_ORBIT_MINIMUM_DISTANCE,
			FORGE_ORBIT_MAXIMUM_DISTANCE);
		forge_globals.held_height = 0.f;
		forge_globals.held_anchored = TRUE;
	}

	if (forge_globals.held_anchored)
	{
		/* and again after the camera moves (forge_flying_camera_moved) */
		forge_hold_place_anchored();
	}
	else if (forge_aim(FORGE_LOCAL_PLAYER_INDEX, forge_globals.held_object_index, &point, &normal))
	{
		real_point3d position;

		forge_held_position(object->definition_index, &point, &normal, forge_globals.held_height, &position);
		forge_hold_place(&position);
	}

	return;
}

/* lets go of the held object: placed where it is, put back, or removed */
static void forge_hold_end(
	boolean cancel,
	boolean remove_object)
{
	long object_index = forge_globals.held_object_index;
	struct object_datum *object = object_get(object_index);
	char const *name = tag_get_name(object->definition_index);

	if (forge_client())
	{
		/* the host does it: this machine's copy goes back where it was (a
		removed one until the host's word comes) or stays where it was put */
		if (!cancel || remove_object)
		{
			forge_layout_client_edit(remove_object ? _forge_edit_remove : _forge_edit_move, object_index,
				object->definition_index, &forge_globals.held_original_position.x, &object->object.position.x,
				&object->object.forward.i, &object->object.up.i);
			terminal_printf(global_real_argb_green, remove_object
				? "forge: asked the host to remove %s"
				: "forge: asked the host to place %s", name);
		}
		if (cancel || remove_object)
		{
			object_set_position(
				object_index,
				&forge_globals.held_original_position,
				&forge_globals.held_original_forward,
				&forge_globals.held_original_up);
		}
		object->object.translational_velocity = *global_zero_vector3d;
		object->object.angular_velocity = *global_zero_vector3d;
	}
	else if (remove_object || (cancel && forge_globals.held_spawned))
	{
		forge_layout_note_removed(object_index);
		object_delete(object_index);
		if (remove_object)
			terminal_printf(global_real_argb_green, "forge: removed %s", name);
	}
	else
	{
		if (cancel)
		{
			object_set_position(
				object_index,
				&forge_globals.held_original_position,
				&forge_globals.held_original_forward,
				&forge_globals.held_original_up);
		}
		else
		{
			/* kept by the layout: a map's own object as moved, one the game
			made (a spawn point's weapon) as one of the tools' (forge_layout.c) */
			forge_layout_note_placed(object_index);
			terminal_printf(global_real_argb_green, "forge: placed %s", name);
		}
		object->object.translational_velocity = *global_zero_vector3d;
		object->object.angular_velocity = *global_zero_vector3d;
	}
	forge_globals.held_object_index = NONE;

	return;
}

/* debug.forge_menu_tab (port_config.c): a few seconds into a game, opens
the menu on that tab, once, for automated screenshots of it
(HALO_SCREENSHOT_DIR); -1 never. Returns the tab, or -1 */
long config_integer(char const *name);

static long forge_debug_menu_tab(
	void)
{
	static long tab = -2;
	long result = -1;

	if (tab == -2)
		tab = config_integer("debug.forge_menu_tab");
	if (tab >= 0 && game_time_get() >= 90)
	{
		result = tab;
		tab = -1;
	}

	return result;
}

/* the line being typed: the keys that went down (and those held, again and
again) type, backspace deletes, enter takes the line, escape leaves it */
static void forge_text_entry_update(
	boolean take,
	unsigned long milliseconds)
{
	unsigned char held[FORGE_TEXT_KEYS_READ];
	boolean shift;
	boolean primed = forge_text_entry_globals.primed;
	short key;

	halo_linux_forge_read_text_keys(held, sizeof(held));
	shift = held[FORGE_TEXT_KEY_LEFT_SHIFT] || held[FORGE_TEXT_KEY_RIGHT_SHIFT];
	forge_text_entry_globals.primed = TRUE;
	for (key = FORGE_TEXT_KEY_A; key < FORGE_TEXT_KEY_COUNT; key++)
	{
		struct forge_key *state = &forge_text_entry_globals.keys[key];
		char character = 0;

		if (!forge_key_pressed(state, held[key], TRUE, milliseconds))
			continue;
		if (!primed)
		{
			/* held since before the line: it types when pressed again */
			state->repeat_milliseconds = milliseconds + 0x3fffffff;
			continue;
		}
		if (key == FORGE_TEXT_KEY_ESCAPE)
		{
			forge_text_entry_globals.active = FALSE;
			return;
		}
		if (key == FORGE_TEXT_KEY_BACKSPACE)
		{
			if (forge_text_entry_globals.length > 0)
				forge_text_entry_globals.text[--forge_text_entry_globals.length] = 0;
		}
		else if (key < FORGE_TEXT_KEY_1)
		{
			character = (char)((shift ? 'A' : 'a') + key - FORGE_TEXT_KEY_A);
		}
		else if (key == FORGE_TEXT_KEY_SPACE)
		{
			character = ' ';
		}
		else if (key - FORGE_TEXT_KEY_1 < (short)(sizeof(forge_text_plain) - 1) &&
			forge_text_plain[key - FORGE_TEXT_KEY_1] != ' ')
		{
			character = (shift ? forge_text_shifted : forge_text_plain)[key - FORGE_TEXT_KEY_1];
			if (character == ' ')
				character = 0;
		}
		if (character && forge_text_entry_globals.length < forge_text_entry_globals.maximum_length)
		{
			forge_text_entry_globals.text[forge_text_entry_globals.length++] = character;
			forge_text_entry_globals.text[forge_text_entry_globals.length] = 0;
		}
	}
	if (take && primed)
	{
		forge_text_entry_globals.active = FALSE;
		forge_text_entry_globals.done(forge_text_entry_globals.text);
	}

	return;
}

static void forge_update_keys(
	struct halo_linux_forge_keys const *keys,
	unsigned long milliseconds,
	real seconds)
{
	boolean toggle_flying = forge_key_pressed(&forge_globals.toggle_flying_key, keys->toggle_flying, FALSE, milliseconds);
	boolean menu = forge_key_pressed(&forge_globals.menu_key, keys->menu, FALSE, milliseconds);
	boolean close = forge_key_pressed(&forge_globals.menu_close_key, keys->menu_close, FALSE, milliseconds);
	boolean up = forge_key_pressed(&forge_globals.menu_up_key, keys->menu_up, TRUE, milliseconds);
	boolean down = forge_key_pressed(&forge_globals.menu_down_key, keys->menu_down, TRUE, milliseconds);
	boolean left = forge_key_pressed(&forge_globals.menu_left_key, keys->menu_left, TRUE, milliseconds);
	boolean right = forge_key_pressed(&forge_globals.menu_right_key, keys->menu_right, TRUE, milliseconds);
	boolean select = forge_key_pressed(&forge_globals.menu_select_key, keys->menu_select, FALSE, milliseconds);
	boolean grab = forge_key_pressed(&forge_globals.grab_key, keys->grab, FALSE, milliseconds);
	boolean rotation_axis = forge_key_pressed(&forge_globals.rotation_axis_key, keys->rotation_axis, FALSE, milliseconds);
	boolean rotation_snap = forge_key_pressed(&forge_globals.rotation_snap_key, keys->rotation_snap, FALSE, milliseconds);
	boolean remove_object = forge_key_pressed(&forge_globals.remove_object_key, keys->remove_object, FALSE, milliseconds);
	boolean tab_previous = forge_key_pressed(&forge_globals.tab_previous_key, keys->tab_previous, FALSE, milliseconds);
	boolean tab_next = forge_key_pressed(&forge_globals.tab_next_key, keys->tab_next, FALSE, milliseconds);
	boolean pad_menu = forge_key_pressed(&forge_globals.pad_menu_key, keys->pad_menu, FALSE, milliseconds);
	boolean pad_remove = forge_key_pressed(&forge_globals.pad_remove_key, keys->pad_remove, FALSE, milliseconds);
	boolean flying = forge_globals.active && director_forge_flying(FORGE_LOCAL_PLAYER_INDEX);

	/* a held object that is gone (a new map, a revert, a deletion) is let go */
	if (forge_globals.held_object_index != NONE &&
		!object_try_and_get(forge_globals.held_object_index))
	{
		forge_globals.held_object_index = NONE;
	}

	/* the entries are the tags of the map they were listed in: a new map
	(or a revert, as game time also goes back) closes the menu */
	if (!forge_globals.active || game_time_get() < forge_globals.game_time)
	{
		if (forge_globals.mod_hold)
		{
			forge_globals.mod_hold->update(NULL);
			forge_globals.mod_hold = NULL;
		}
		forge_globals.menu_open = FALSE;
		/* what was held stays where it is */
		forge_globals.held_object_index = NONE;
		forge_text_entry_globals.active = FALSE;
	}
	else if (forge_text_entry_globals.active)
	{
		/* the keyboard is the line's until it is taken or left */
		if (flying)
			forge_text_entry_update(select, milliseconds);
		else
			forge_text_entry_globals.active = FALSE;
	}
	else
	{
		if (toggle_flying)
			forge_toggle_flying(FORGE_LOCAL_PLAYER_INDEX);

		{
			long debug_tab = forge_debug_menu_tab();

			if (debug_tab >= 0 && debug_tab < (long)(NUMBEROF(forge_categories) + halo_mods_menu_page_count()))
			{
				/* the menu belongs to forge mode */
				if (!flying)
				{
					director_forge_set_flying(FORGE_LOCAL_PLAYER_INDEX, TRUE);
					flying = TRUE;
				}
				forge_globals.category_index = (short)debug_tab;
				forge_globals.menu_open = TRUE;
				forge_menu_build();
			}
		}

		if (forge_globals.mod_hold)
		{
			struct halo_forge_hold_input input;

			csmemset(&input, 0, sizeof(input));
			input.seconds = seconds;
			input.left = keys->menu_left;
			input.right = keys->menu_right;
			input.up = keys->menu_up;
			input.down = keys->menu_down;
			input.left_pressed = left;
			input.right_pressed = right;
			input.up_pressed = up;
			input.down_pressed = down;
			input.axis_pressed = rotation_axis;
			input.snap_pressed = rotation_snap || pad_menu;
			input.place = select;
			input.cancel = close;
			input.delete_pressed = remove_object || pad_remove;
			if (forge_globals.mod_hold->update(&input))
				forge_globals.mod_hold = NULL;
		}
		else if (forge_globals.held_object_index != NONE)
		{
			if (select || close || remove_object || pad_remove)
			{
				forge_hold_end(close, remove_object || pad_remove);
			}
			else
			{
				if (rotation_axis)
					forge_globals.held_axis = (forge_globals.held_axis + 1) % NUMBER_OF_FORGE_AXES;
				if (rotation_snap || pad_menu)
					forge_globals.held_snap_index = (forge_globals.held_snap_index + 1) % NUMBEROF(forge_snaps);
				forge_hold(keys, left, right, seconds);
			}
		}
		else if (forge_globals.menu_open)
		{
			if (menu || close || pad_menu)
			{
				forge_globals.menu_open = FALSE;
			}
			else
			{
				short category_count = (short)(NUMBEROF(forge_categories) + halo_mods_menu_page_count());
				struct halo_mod_menu const *page = forge_menu_page();

				if (rotation_axis || rotation_snap || tab_previous || tab_next || (!page && (left || right)))
				{
					boolean next = rotation_snap || tab_next || (!rotation_axis && !tab_previous && right);

					forge_globals.category_index =
						(forge_globals.category_index + (next ? 1 : category_count - 1)) % category_count;
					forge_globals.menu_state_changed = TRUE;
					forge_menu_build();
					page = forge_menu_page();
				}
				if (page)
				{
					forge_menu_page_keys(page, up, down, left, right, select);
					forge_menu_scroll_to_selection();
				}
				else if (forge_globals.entry_count > 0)
				{
					if (up)
					{
						forge_globals.selected_entry_index =
							(forge_globals.selected_entry_index + forge_globals.entry_count - 1) % forge_globals.entry_count;
					}
					if (down)
					{
						forge_globals.selected_entry_index =
							(forge_globals.selected_entry_index + 1) % forge_globals.entry_count;
					}
					if (up || down)
					{
						forge_menu_scroll_to_selection();
						forge_menu_remember();
					}
					if (select)
					{
						forge_globals.menu_open = FALSE;
						forge_take(
							FORGE_LOCAL_PLAYER_INDEX,
							forge_globals.entries[forge_globals.selected_entry_index]);
					}
				}
			}
		}
		else if ((menu || pad_menu) && !flying)
		{
			if (menu)
				terminal_printf(global_real_argb_orange, "forge: the menu is for forge mode (F2 / D-pad up)");
		}
		else if (menu || pad_menu)
		{
			if (!forge_globals.menu_state_loaded)
				forge_menu_state_load();
			forge_globals.menu_open = TRUE;
			forge_menu_build();
		}
		else if (grab || (select && flying))
		{
			/* a mod may have something of its own to pick up there */
			if (!forge_authoritative() || !halo_mods_grab())
				forge_grab(FORGE_LOCAL_PLAYER_INDEX);
		}
		else if (pad_remove && flying)
		{
			forge_remove_at_crosshair(FORGE_LOCAL_PLAYER_INDEX);
		}
	}
	if (!flying)
	{
		/* the menu belongs to forge mode */
		forge_globals.camera_valid = FALSE;
		forge_globals.menu_open = FALSE;
	}
	if (forge_globals.held_object_index == NONE)
	{
		forge_globals.held_anchored = FALSE;
		forge_globals.held_turning = FALSE;
	}

	/* a closed menu's place is written once, whichever way it closed */
	if (!forge_globals.menu_open && forge_globals.menu_state_changed)
		forge_menu_state_save();

	/* the keys stay away from the game until they are released, so the
	escape that closes the menu does not also pause the game */
	halo_linux_forge_capture_text(forge_text_entry_globals.active);
	forge_globals.keys_captured =
		forge_globals.menu_open ||
		forge_text_entry_globals.active ||
		forge_globals.held_object_index != NONE ||
		forge_globals.mod_hold != NULL ||
		(forge_globals.keys_captured &&
			(keys->menu_up || keys->menu_down || keys->menu_left || keys->menu_right ||
			keys->menu_select || keys->menu_close || keys->rotation_axis || keys->rotation_snap ||
			keys->remove_object || keys->tab_previous || keys->tab_next || keys->pad_menu ||
			keys->pad_remove));
	halo_linux_forge_capture_menu_keys(forge_globals.keys_captured);
	halo_linux_forge_set_flying(flying);
	forge_globals.game_time = forge_globals.active ? game_time_get() : 0;

	return;
}

static void forge_draw_line(
	long font_tag_index,
	rectangle2d const *bounds,
	short justification,
	real_argb_color const *color,
	char const *string)
{
	draw_string_set_draw_mode(font_tag_index, _text_style_plain, justification, 0, color);
	rasterizer_draw_string(bounds, NULL, NULL, 0, string);

	return;
}

static void forge_render_crosshair(
	long font_tag_index,
	rectangle2d const *window,
	short line_height)
{
	rectangle2d bounds = *window;

	bounds.y0 = (short)((window->y0 + window->y1 - line_height) / 2);
	bounds.y1 = (short)(bounds.y0 + line_height);
	forge_draw_line(font_tag_index, &bounds, _text_justification_center, global_real_argb_white, "+");

	return;
}

/* the first line of the tools' text, under the window's top margin */
static void forge_first_line_bounds(
	rectangle2d const *window,
	short line_height,
	rectangle2d *bounds)
{
	*bounds = *window;
	bounds->x0 = (short)(window->x0 + FORGE_MENU_MARGIN);
	bounds->x1 = (short)(window->x1 - FORGE_MENU_MARGIN);
	bounds->y0 = (short)(window->y0 + FORGE_MENU_MARGIN);
	bounds->y1 = (short)(bounds->y0 + line_height);

	return;
}

static void forge_render_held(
	long font_tag_index,
	rectangle2d const *window,
	short line_height)
{
	char line[256];
	rectangle2d bounds;

	forge_first_line_bounds(window, line_height, &bounds);
	_snprintf(
		line,
		NUMBEROF(line),
		"%s %s",
		forge_globals.held_spawned ? "placing" : "moving",
		tag_get_name(object_get(forge_globals.held_object_index)->definition_index));
	forge_draw_line(font_tag_index, &bounds, _text_justification_left, global_real_argb_yellow, line);
	offset_rectangle2d(&bounds, 0, line_height);

	_snprintf(
		line,
		NUMBEROF(line),
		"turning: %s (T), step: %s (V/X), right stick orbits, RT + right stick turns",
		forge_axis_names[forge_globals.held_axis],
		forge_snaps[forge_globals.held_snap_index].name);
	forge_draw_line(font_tag_index, &bounds, _text_justification_left, global_real_argb_white, line);
	offset_rectangle2d(&bounds, 0, line_height);

	forge_draw_line(
		font_tag_index,
		&bounds,
		_text_justification_left,
		global_real_argb_grey,
		"aim to move, left/right turn, up/down raise, enter/click/A place, esc/right click/B cancel, delete/Y remove");

	return;
}

/* what a mod holds (halo_forge.h) */
static void forge_render_mod_hold(
	long font_tag_index,
	rectangle2d const *window,
	short line_height)
{
	char line[256];
	rectangle2d bounds;

	forge_first_line_bounds(window, line_height, &bounds);
	_snprintf(line, NUMBEROF(line), "moving %s", forge_globals.mod_hold->name);
	forge_draw_line(font_tag_index, &bounds, _text_justification_left, global_real_argb_yellow, line);
	offset_rectangle2d(&bounds, 0, line_height);

	line[0] = 0;
	if (forge_globals.mod_hold->describe)
		forge_globals.mod_hold->describe(line, sizeof(line));
	if (line[0])
	{
		forge_draw_line(font_tag_index, &bounds, _text_justification_left, global_real_argb_white, line);
		offset_rectangle2d(&bounds, 0, line_height);
	}

	forge_draw_line(
		font_tag_index,
		&bounds,
		_text_justification_left,
		global_real_argb_grey,
		"aim to move, left/right turn, up/down raise, V/X step, enter/click/A place, esc/right click/B cancel, delete/Y remove");

	return;
}

/* the last part of a tag's name, and its folder shortened from the left
("...\") to fit the width in the font */
static void forge_split_tag_name(
	char const *tag_name,
	long font,
	short width,
	char *folder,
	short folder_size,
	char const **name)
{
	char const *separator = strrchr(tag_name, '\\');
	char const *start = tag_name;

	*name = separator ? separator + 1 : tag_name;
	_snprintf(folder, folder_size, "%.*s", separator ? (int)(separator - tag_name) : 0, tag_name);
	while (*folder && halo_mod_text_width(font, folder) > width)
	{
		char const *next = separator ? strchr(start, '\\') : NULL;

		if (!next || next >= separator)
		{
			*folder = 0;
		}
		else
		{
			start = next + 1;
			_snprintf(folder, folder_size, "...\\%.*s", (int)(separator - start), start);
		}
	}

	return;
}

/* a tab's name: an object category, or a mod's page */
static char const *forge_tab_name(
	short tab_index)
{
	struct halo_mod_menu const *page = tab_index >= FORGE_CATEGORY_COUNT
		? halo_mods_menu_page((short)(tab_index - FORGE_CATEGORY_COUNT))
		: NULL;

	return page ? page->title : forge_categories[tab_index].name;
}

/* ---------- the menu's look (forge_ui) */

/* one hint of the keys along the bottom: the keys on a cap, then what they do */
struct forge_menu_hint
{
	char const *keys;
	char const *action;
};

/* what the menu's motion remembers between frames */
static struct
{
	unsigned long milliseconds;
	/* 0 to 1 while the menu fades in */
	real fade;
	/* the highlights' top edges, gliding to the chosen rows */
	real category_bar_y;
	real entry_bar_y;
} forge_menu_look;

/* the fade of the frame being drawn (forge_menu_look.fade) */
static real forge_menu_fade = 1.f;

static unsigned long forge_menu_color(
	unsigned long argb)
{
	return ((unsigned long)((real)(argb >> 24) * forge_menu_fade) << 24) | (argb & 0x00ffffffUL);
}

static void forge_menu_box(
	short x0,
	short y0,
	short x1,
	short y1,
	unsigned long argb)
{
	if (x1 > x0 && y1 > y0)
		halo_mod_draw_box(x0, y0, x1, y1, forge_menu_color(argb));

	return;
}

/* text at the left, right or center of x0 to x1, its line's top at y */
static void forge_menu_text(
	long font,
	short x0,
	short y,
	short x1,
	int justification,
	unsigned long argb,
	char const *text)
{
	halo_mod_draw_text(font, x0, y, x1, (short)(y + halo_mod_line_height(font)),
		justification, forge_menu_color(argb), text);

	return;
}

/* text shortened, with "...", to fit the width (the font's measure runs a
little short, so with room to spare) */
static void forge_menu_fit(
	long font,
	short width,
	char const *text,
	char *fitted,
	short fitted_size)
{
	int length = (int)strlen(text);

	_snprintf(fitted, fitted_size, "%s", text);
	while (length > 1 && halo_mod_text_width(font, fitted) > width - 2)
	{
		length--;
		_snprintf(fitted, fitted_size, "%.*s...", length, text);
	}

	return;
}

/* a highlight's top edge gliding from where it was to its row's: most of the
way in a tenth of a second, or straight there (snap) when the menu has just
opened */
static real forge_menu_glide(
	real from,
	real to,
	real seconds,
	boolean snap)
{
	return snap ? to : from + (to - from) * MIN(seconds * FORGE_MENU_GLIDE_SPEED, 1.f);
}

/* the hints along the bottom, from x0, wrapping to another row when the
next does not fit (the font's measure runs a little short, so with room to
spare); draws them unless draw is FALSE, and returns how many rows they take */
static short forge_menu_hints(
	long font,
	short x0,
	short x1,
	short y,
	struct forge_menu_hint const *hints,
	short hint_count,
	boolean draw)
{
	short cap_height = (short)(halo_mod_line_height(font) + 4);
	short x = x0;
	short rows = 1;
	short hint_index;

	for (hint_index = 0; hint_index < hint_count; hint_index++)
	{
		struct forge_menu_hint const *hint = &hints[hint_index];
		short keys_width = (short)(halo_mod_text_width(font, hint->keys) * 106 / 100 + 8);
		short action_width = (short)(halo_mod_text_width(font, hint->action) * 106 / 100 + 2);
		short width = (short)(keys_width + 4 + action_width);

		if (x > x0 && x + width > x1)
		{
			x = x0;
			rows++;
			y += cap_height + 3;
		}
		if (draw)
		{
			forge_menu_box(x, y, (short)(x + keys_width), (short)(y + cap_height), FORGE_MENU_CAP_COLOR);
			forge_menu_box(x, y, (short)(x + keys_width), (short)(y + 1), FORGE_MENU_CAP_EDGE_COLOR);
			forge_menu_text(font, x, (short)(y + 2), (short)(x + keys_width), HALO_MOD_TEXT_CENTER,
				FORGE_MENU_SELECTED_TEXT_COLOR, hint->keys);
			forge_menu_text(font, (short)(x + keys_width + 4), (short)(y + 2), x1, HALO_MOD_TEXT_LEFT,
				FORGE_MENU_TEXT_COLOR, hint->action);
		}
		x += width + FORGE_MENU_HINT_GAP;
	}

	return rows;
}

/* the spawn menu (forge_ui), after the forge menu of Halo: Reach: a dark
panel at the left of the screen with the categories down its left side, the
chosen category's rows beside them, and the keys along the bottom. The
highlights glide to the chosen rows and the panel fades in. Up and down
choose a row, left and right (or the tab buttons) another category; a mod's
page is a category too, in the list under "tools". */
static void forge_render_menu(
	rectangle2d const *window)
{
	static struct forge_menu_hint const object_hints[] =
	{
		{ "Up/Down", "Choose" }, { "Left/Right", "Category" }, { "Enter/A", "Take" }, { "Esc/B/X", "Close" },
	};
	static struct forge_menu_hint const page_hints[] =
	{
		{ "Up/Down", "Choose" }, { "Left/Right", "Change" }, { "Enter/A", "Do" }, { "LB/RB", "Tab" },
		{ "Esc/B/X", "Close" },
	};
	unsigned long milliseconds = system_milliseconds();
	real seconds = MIN((real)(milliseconds - forge_menu_look.milliseconds) / MILLISECONDS_PER_SECOND, 0.1f);
	boolean fresh = milliseconds - forge_menu_look.milliseconds > FORGE_MENU_FRESH_MILLISECONDS;
	long font = halo_mod_font_for_lines(FORGE_MENU_FONT_LINES);
	long small_font = halo_mod_font(FALSE);
	short line_height = halo_mod_line_height(font);
	short small_height = halo_mod_line_height(small_font);
	short row_height = (short)(line_height + 3);
	short section_height = (short)(small_height + 8);
	short title_height = (short)(line_height + 10);
	struct halo_mod_menu const *page = forge_menu_page();
	struct forge_menu_hint const *hints = page ? page_hints : object_hints;
	short hint_count = page ? NUMBEROF(page_hints) : NUMBEROF(object_hints);
	short details_height = (short)(small_height + 8);
	short tab_count = (short)(FORGE_CATEGORY_COUNT + halo_mods_menu_page_count());
	short tool_count = (short)(tab_count - FORGE_CATEGORY_COUNT);
	short window_width = (short)(window->x1 - window->x0);
	short window_height = (short)(window->y1 - window->y0);
	short x0 = (short)(window->x0 + FORGE_MENU_MARGIN);
	short x1 = (short)(x0 + MIN(window_width - 2 * FORGE_MENU_MARGIN, FORGE_MENU_MAX_WIDTH));
	short y0 = (short)(window->y0 + FORGE_MENU_MARGIN);
	short hint_rows = forge_menu_hints(small_font, (short)(x0 + FORGE_MENU_PADDING), (short)(x1 - FORGE_MENU_PADDING),
		0, hints, hint_count, FALSE);
	short hint_height = (short)(hint_rows * (small_height + 7) + 6);
	short left_width = (short)PIN((x1 - x0) * FORGE_MENU_CATEGORY_PERCENT / 100, 100, 150);
	short right_x0 = (short)(x0 + left_width + 1);
	short text_x1 = (short)(x1 - FORGE_MENU_PADDING - FORGE_MENU_SCROLL_WIDTH - 2);
	/* the categories share what height there is */
	short sections_height = (short)(section_height * (tool_count > 0 ? 2 : 1));
	short max_body_height = (short)(window_height - 2 * FORGE_MENU_MARGIN - title_height - hint_height);
	short category_height = (short)PIN((max_body_height - sections_height) / tab_count, small_height + 2, row_height);
	long category_font = category_height >= line_height + 2 ? font : small_font;
	short left_height = (short)(sections_height + category_height * tab_count + FORGE_MENU_PADDING);
	short right_height = (short)(row_height + FORGE_MENU_PADDING + FORGE_MENU_VISIBLE_ENTRIES * row_height +
		details_height);
	short body_height = MAX(left_height, right_height);
	short body_y0 = (short)(y0 + title_height);
	short body_y1 = (short)(body_y0 + body_height);
	short y1 = (short)(body_y1 + hint_height);
	char fitted[FORGE_TAG_NAME_SIZE + 64];
	char line[FORGE_TAG_NAME_SIZE + 64];
	short category_index;
	short entry_index;
	short y;

	/* the fade and the glides */
	forge_menu_look.milliseconds = milliseconds;
	forge_menu_look.fade = fresh ? 0.f : MIN(forge_menu_look.fade + seconds * FORGE_MENU_FADE_SPEED, 1.f);
	forge_menu_fade = forge_menu_look.fade;

	/* the panel: a title, the categories, the rows, the keys */
	forge_menu_box(x0, y0, x1, y1, FORGE_MENU_PANEL_COLOR);
	forge_menu_box(x0, y0, x1, (short)(y0 + title_height), FORGE_MENU_TITLE_BAR_COLOR);
	forge_menu_box(x0, y0, x1, (short)(y0 + 2), FORGE_MENU_ACCENT_COLOR);
	forge_menu_box(x0, body_y0, (short)(x0 + left_width), body_y1, FORGE_MENU_SIDE_COLOR);
	forge_menu_box((short)(x0 + left_width), body_y0, right_x0, body_y1, FORGE_MENU_RULE_COLOR);
	forge_menu_box(x0, body_y1, x1, y1, FORGE_MENU_HINT_BAR_COLOR);

	/* title: the tools, and the map they are in */
	forge_menu_text(font, (short)(x0 + FORGE_MENU_PADDING), (short)(y0 + 6), x1, HALO_MOD_TEXT_LEFT,
		FORGE_MENU_ACCENT_COLOR, "FORGE");
	{
		char const *map_name = tag_get_name(global_scenario_index);
		char const *separator = map_name ? strrchr(map_name, '\\') : NULL;

		if (map_name)
		{
			forge_menu_text(small_font, x0, (short)(y0 + 6 + line_height - small_height),
				(short)(x1 - FORGE_MENU_PADDING), HALO_MOD_TEXT_RIGHT, FORGE_MENU_DIM_TEXT_COLOR,
				separator ? separator + 1 : map_name);
		}
	}

	/* the categories: the map's objects, then the tools' pages */
	{
		short category_y = body_y0;
		short bar_target_y;

		for (category_index = 0; category_index < tab_count; category_index++)
		{
			if (category_index == 0 || (category_index == FORGE_CATEGORY_COUNT))
			{
				forge_menu_text(small_font, (short)(x0 + FORGE_MENU_PADDING), (short)(category_y + 6),
					(short)(x0 + left_width), HALO_MOD_TEXT_LEFT, FORGE_MENU_ACCENT_DIM_COLOR,
					category_index == 0 ? "SPAWN" : "TOOLS");
				category_y += section_height;
			}
			if (category_index == forge_globals.category_index)
				forge_menu_look.category_bar_y = forge_menu_glide(
					fresh ? (real)category_y : forge_menu_look.category_bar_y, (real)category_y, seconds, fresh);
			category_y += category_height;
		}

		bar_target_y = (short)forge_menu_look.category_bar_y;
		forge_menu_box((short)(x0 + 2), bar_target_y, (short)(x0 + left_width - 1),
			(short)(bar_target_y + category_height), FORGE_MENU_BAR_COLOR);
		forge_menu_box((short)(x0 + 2), bar_target_y, (short)(x0 + 5),
			(short)(bar_target_y + category_height), FORGE_MENU_BAR_EDGE_COLOR);

		category_y = body_y0;
		for (category_index = 0; category_index < tab_count; category_index++)
		{
			boolean chosen = category_index == forge_globals.category_index;
			short text_y;

			if (category_index == 0 || category_index == FORGE_CATEGORY_COUNT)
				category_y += section_height;
			text_y = (short)(category_y + (category_height - halo_mod_line_height(category_font)) / 2);
			forge_menu_fit(category_font, (short)(left_width - 2 * FORGE_MENU_PADDING - 12),
				forge_tab_name(category_index), fitted, sizeof(fitted));
			forge_menu_text(category_font, (short)(x0 + FORGE_MENU_PADDING + 6), text_y,
				(short)(x0 + left_width), HALO_MOD_TEXT_LEFT,
				chosen ? FORGE_MENU_SELECTED_TEXT_COLOR : FORGE_MENU_TEXT_COLOR, fitted);
			if (category_index < FORGE_CATEGORY_COUNT)
			{
				_snprintf(line, sizeof(line), "%d", forge_globals.category_counts[category_index]);
				forge_menu_text(small_font, x0,
					(short)(category_y + (category_height - small_height) / 2),
					(short)(x0 + left_width - FORGE_MENU_PADDING), HALO_MOD_TEXT_RIGHT,
					chosen ? FORGE_MENU_TEXT_COLOR : FORGE_MENU_DIM_TEXT_COLOR, line);
			}
			category_y += category_height;
		}
	}

	/* the rows' header: the category, and the place in it */
	forge_menu_text(font, (short)(right_x0 + FORGE_MENU_PADDING), (short)(body_y0 + 2),
		text_x1, HALO_MOD_TEXT_LEFT, FORGE_MENU_TITLE_TEXT_COLOR, forge_tab_name(forge_globals.category_index));
	_snprintf(line, sizeof(line), "%d / %d",
		forge_globals.entry_count ? forge_globals.selected_entry_index + 1 : 0,
		forge_globals.entry_count);
	forge_menu_text(small_font, right_x0, (short)(body_y0 + 2 + line_height - small_height), text_x1,
		HALO_MOD_TEXT_RIGHT, FORGE_MENU_DIM_TEXT_COLOR, line);
	y = (short)(body_y0 + row_height);
	forge_menu_box((short)(right_x0 + FORGE_MENU_PADDING), y, (short)(x1 - FORGE_MENU_PADDING), (short)(y + 1),
		FORGE_MENU_RULE_COLOR);
	y += FORGE_MENU_PADDING;

	/* the rows: the highlight under the chosen one, then every row */
	{
		short rows_y = y;
		short bar_y;
		/* the rows: the name at the left, the folder (or a mod's value) at the right */
		short value_x0 = (short)(right_x0 + (text_x1 - right_x0) * 55 / 100);

		forge_menu_look.entry_bar_y = forge_menu_glide(
			fresh ? (real)rows_y : forge_menu_look.entry_bar_y,
			(real)(rows_y + (forge_globals.selected_entry_index - forge_globals.first_entry_index) * row_height),
			seconds, fresh);
		bar_y = (short)PIN((short)forge_menu_look.entry_bar_y, rows_y,
			rows_y + (FORGE_MENU_VISIBLE_ENTRIES - 1) * row_height);
		if (forge_globals.entry_count > 0)
		{
			forge_menu_box((short)(right_x0 + 2), bar_y, (short)(x1 - 2), (short)(bar_y + row_height),
				FORGE_MENU_BAR_COLOR);
			forge_menu_box((short)(right_x0 + 2), bar_y, (short)(right_x0 + 5), (short)(bar_y + row_height),
				FORGE_MENU_BAR_EDGE_COLOR);
		}
		else
		{
			forge_menu_text(font, (short)(right_x0 + FORGE_MENU_PADDING + 6), rows_y, text_x1,
				HALO_MOD_TEXT_LEFT, FORGE_MENU_DIM_TEXT_COLOR,
				page ? "(nothing here)" : "(none in this map)");
		}

		for (entry_index = forge_globals.first_entry_index;
			entry_index < forge_globals.entry_count &&
				entry_index < forge_globals.first_entry_index + FORGE_MENU_VISIBLE_ENTRIES;
			entry_index++)
		{
			boolean selected = entry_index == forge_globals.selected_entry_index;
			short text_y = (short)(rows_y + (entry_index - forge_globals.first_entry_index) * row_height +
				(row_height - line_height) / 2);
			unsigned long text_color = selected ? FORGE_MENU_SELECTED_TEXT_COLOR : FORGE_MENU_TEXT_COLOR;

			if (page)
			{
				char label[128];
				char value[128];
				char value_text[136];
				short label_width = (short)(text_x1 - right_x0 - FORGE_MENU_PADDING - 6);
				long row_font = font;
				short row_text_y = text_y;

				label[0] = value[0] = 0;
				page->row_text(entry_index, label, sizeof(label), value, sizeof(value));
				/* a mod's value at the right, in brackets on the chosen row: left and right change it */
				_snprintf(value_text, sizeof(value_text), value[0] ? (selected ? "< %s >" : "%s") : "", value);
				if (halo_mod_text_width(font, label) + halo_mod_text_width(font, value_text) + 24 > label_width)
				{
					/* not both in the large font: both in the small */
					row_font = small_font;
					row_text_y = (short)(text_y + (line_height - small_height) / 2);
				}
				if (value_text[0])
				{
					forge_menu_fit(row_font, (short)(label_width * 60 / 100), value_text, value, sizeof(value));
					forge_menu_text(row_font, right_x0, row_text_y, text_x1, HALO_MOD_TEXT_RIGHT,
						selected ? FORGE_MENU_TITLE_TEXT_COLOR : FORGE_MENU_TEXT_COLOR, value);
					label_width -= (short)(halo_mod_text_width(row_font, value) + 12);
				}
				forge_menu_fit(row_font, label_width, label, fitted, sizeof(fitted));
				forge_menu_text(row_font, (short)(right_x0 + FORGE_MENU_PADDING + 6), row_text_y, text_x1,
					HALO_MOD_TEXT_LEFT, text_color, fitted);
			}
			else
			{
				char folder[FORGE_TAG_NAME_SIZE];
				char const *name;

				forge_split_tag_name(
					tag_get_name(forge_globals.entries[entry_index]),
					small_font,
					(short)(text_x1 - value_x0 - FORGE_MENU_PADDING),
					folder,
					sizeof(folder),
					&name);
				forge_menu_fit(font, (short)(value_x0 - right_x0 - FORGE_MENU_PADDING - 12), name, fitted,
					sizeof(fitted));
				forge_menu_text(font, (short)(right_x0 + FORGE_MENU_PADDING + 6), text_y, value_x0,
					HALO_MOD_TEXT_LEFT, text_color, fitted);
				forge_menu_text(small_font, value_x0,
					(short)(text_y + line_height - small_height), text_x1, HALO_MOD_TEXT_RIGHT,
					selected ? FORGE_MENU_TEXT_COLOR : FORGE_MENU_DIM_TEXT_COLOR, folder);
			}
		}

		/* a scroll bar when there are more rows than fit */
		if (forge_globals.entry_count > FORGE_MENU_VISIBLE_ENTRIES)
		{
			short track_height = (short)(FORGE_MENU_VISIBLE_ENTRIES * row_height);
			short thumb_height = (short)MAX(track_height * FORGE_MENU_VISIBLE_ENTRIES / forge_globals.entry_count, 10);
			short thumb_y = (short)(rows_y + (track_height - thumb_height) * forge_globals.first_entry_index /
				(forge_globals.entry_count - FORGE_MENU_VISIBLE_ENTRIES));
			short bar_x1 = (short)(x1 - FORGE_MENU_PADDING);

			forge_menu_box((short)(bar_x1 - FORGE_MENU_SCROLL_WIDTH), rows_y, bar_x1, (short)(rows_y + track_height),
				FORGE_MENU_RULE_COLOR);
			forge_menu_box((short)(bar_x1 - FORGE_MENU_SCROLL_WIDTH), thumb_y, bar_x1, (short)(thumb_y + thumb_height),
				FORGE_MENU_ACCENT_COLOR);
		}
	}

	/* under the rows: the chosen object's whole tag name */
	y = (short)(body_y1 - details_height);
	forge_menu_box((short)(right_x0 + FORGE_MENU_PADDING), y, (short)(x1 - FORGE_MENU_PADDING), (short)(y + 1),
		FORGE_MENU_RULE_COLOR);
	if (!page && forge_globals.entry_count > 0)
	{
		char const *tag_name = tag_get_name(forge_globals.entries[forge_globals.selected_entry_index]);

		forge_menu_fit(small_font, (short)(text_x1 - right_x0 - FORGE_MENU_PADDING), tag_name, fitted, sizeof(fitted));
		forge_menu_text(small_font, (short)(right_x0 + FORGE_MENU_PADDING), (short)(y + 4), text_x1,
			HALO_MOD_TEXT_LEFT, FORGE_MENU_DIM_TEXT_COLOR, fitted);
	}

	/* the keys */
	forge_menu_hints(small_font, (short)(x0 + FORGE_MENU_PADDING), (short)(x1 - FORGE_MENU_PADDING),
		(short)(body_y1 + 5), hints, hint_count, TRUE);

	forge_menu_fade = 1.f;

	return;
}

/* the line being typed, low in the window, over the menu */
static void forge_render_text_entry(
	long font_tag_index,
	rectangle2d const *window,
	short line_height)
{
	char line[256];
	rectangle2d bounds = *window;

	bounds.x0 = (short)(window->x0 + FORGE_MENU_MARGIN);
	bounds.x1 = (short)(window->x1 - FORGE_MENU_MARGIN);
	bounds.y0 = (short)(window->y1 - FORGE_MENU_MARGIN - 4 * line_height);
	bounds.y1 = (short)(bounds.y0 + line_height);
	halo_mod_draw_box((short)(bounds.x0 - 8), (short)(bounds.y0 - 6), (short)(bounds.x1 + 8),
		(short)(bounds.y0 + 3 * line_height + 6), 0xD0101418);

	forge_draw_line(font_tag_index, &bounds, _text_justification_left, global_real_argb_yellow,
		forge_text_entry_globals.prompt);
	offset_rectangle2d(&bounds, 0, line_height);
	/* (the cursor blinks) */
	_snprintf(line, NUMBEROF(line), "> %s%s", forge_text_entry_globals.text,
		(forge_globals.milliseconds / 400) % 2 ? "_" : "");
	line[NUMBEROF(line) - 1] = 0;
	forge_draw_line(font_tag_index, &bounds, _text_justification_left, global_real_argb_white, line);
	offset_rectangle2d(&bounds, 0, line_height);
	_snprintf(line, NUMBEROF(line), "type on the keyboard (%lu of %lu), backspace deletes, enter/A takes it, escape leaves it",
		forge_text_entry_globals.length, forge_text_entry_globals.maximum_length);
	line[NUMBEROF(line) - 1] = 0;
	forge_draw_line(font_tag_index, &bounds, _text_justification_left, global_real_argb_grey, line);

	return;
}

/* ---------- public code */

/* once a frame, with the game's other debug keys (main/main.c) */
void forge_update(
	void)
{
	struct halo_linux_forge_keys keys;
	unsigned long milliseconds = system_milliseconds();
	/* at most a tenth of a second, so a hitch does not fling a held object */
	real seconds = MIN((real)(milliseconds - forge_globals.milliseconds) / MILLISECONDS_PER_SECOND, 0.1f);

	/* source mods first (game/mods.c), whether or not the tools are on */
	halo_mods_update();

	forge_globals.milliseconds = milliseconds;
	forge_globals.active = halo_linux_forge_read_keys(&keys) &&
		local_player_get_player_index(FORGE_LOCAL_PLAYER_INDEX) != NONE;
	if (!forge_globals.active)
		csmemset(&keys, 0, sizeof(keys));
	forge_update_keys(&keys, milliseconds, seconds);

	return;
}

/* once a frame, over everything else (interface/interface.c) */
void forge_render(
	void)
{
	long font_tag_index = interface_get_tag_index(_interface_font_terminal);

	halo_mods_render();

	if (forge_globals.active && font_tag_index != NONE)
	{
		struct font_header *font = font_definition_get(font_tag_index);
		short line_height = font->ascending_height + font->descending_height + font->leading_height;
		rectangle2d window = render.camera.window_bounds;

		offset_rectangle2d(&window, -render.camera.viewport_bounds.x0, -render.camera.viewport_bounds.y0);
		if (director_forge_flying(FORGE_LOCAL_PLAYER_INDEX) ||
			forge_globals.held_object_index != NONE ||
			forge_globals.mod_hold)
		{
			forge_render_crosshair(font_tag_index, &window, line_height);
		}
		if (forge_globals.mod_hold)
		{
			forge_render_mod_hold(font_tag_index, &window, line_height);
		}
		else if (forge_globals.held_object_index != NONE &&
			object_try_and_get(forge_globals.held_object_index))
		{
			forge_render_held(font_tag_index, &window, line_height);
		}
		else if (forge_globals.menu_open)
		{
			forge_render_menu(&window);
		}
		if (forge_text_entry_globals.active)
			forge_render_text_entry(font_tag_index, &window, line_height);
	}

	/* drawn only after an update: nothing is left over when the game stops
	updating the tools (the main menu, loading) */
	forge_globals.active = FALSE;

	return;
}

/* ---------- whether the tools are on (halo_forge.h) */

int forge_mode_on(
	void)
{
	static int every_game = -1;

	if (every_game < 0)
	{
		char const *text = getenv("HALO_FORGE");

		every_game = text && strcmp(text, "all") == 0;
	}

	/* (not in a saved film, which replays what was recorded) */
	return every_game ||
		(game_engine_variant_is_forge() && (forge_authoritative() || forge_client()));
}

/* ---------- a line of text (halo_forge.h) */

int forge_text_entry_begin(
	char const *prompt,
	char const *initial,
	unsigned long maximum_length,
	void (*done)(char const *text))
{
	if (forge_text_entry_globals.active)
		return FALSE;
	csmemset(&forge_text_entry_globals, 0, sizeof(forge_text_entry_globals));
	forge_text_entry_globals.active = TRUE;
	forge_text_entry_globals.prompt = prompt;
	forge_text_entry_globals.done = done;
	forge_text_entry_globals.maximum_length = MIN(maximum_length, FORGE_TEXT_MAXIMUM_LENGTH);
	_snprintf(forge_text_entry_globals.text, sizeof(forge_text_entry_globals.text), "%.*s",
		(int)forge_text_entry_globals.maximum_length, initial ? initial : "");
	forge_text_entry_globals.text[FORGE_TEXT_MAXIMUM_LENGTH] = 0;
	forge_text_entry_globals.length = (unsigned long)strlen(forge_text_entry_globals.text);

	return TRUE;
}

/* ---------- what the flying camera asks the tools (halo_forge.h) */

int forge_menu_is_open(
	void)
{
	return forge_globals.menu_open;
}

int forge_camera_turning(
	void)
{
	return forge_globals.held_object_index != NONE && forge_globals.held_turning;
}

float forge_camera_orbit_distance(
	void)
{
	return forge_globals.held_object_index != NONE && forge_globals.held_anchored && !forge_globals.held_turning
		? forge_globals.held_distance
		: 0.f;
}

float forge_camera_zoom(
	float step)
{
	real distance;

	if (!forge_camera_turning() || !forge_globals.held_anchored)
		return 0.f;
	distance = PIN(forge_globals.held_distance - step, FORGE_ORBIT_MINIMUM_DISTANCE, FORGE_ORBIT_MAXIMUM_DISTANCE);
	step = forge_globals.held_distance - distance;
	forge_globals.held_distance = distance;

	return step;
}

void forge_flying_camera_moved(
	float const position[3],
	float const forward[3])
{
	forge_globals.camera_valid = TRUE;
	set_real_point3d(&forge_globals.camera_position, position[0], position[1], position[2]);
	set_real_vector3d(&forge_globals.camera_forward, forward[0], forward[1], forward[2]);
	if (forge_globals.held_object_index != NONE && forge_globals.held_anchored &&
		object_try_and_get(forge_globals.held_object_index))
	{
		forge_hold_place_anchored();
	}

	return;
}

/* ---------- what source mods may ask the tools (halo_forge.h) */

int forge_busy(
	void)
{
	return forge_globals.menu_open || forge_globals.held_object_index != NONE || forge_globals.mod_hold != NULL ||
		forge_text_entry_globals.active;
}

int forge_mod_hold_begin(
	struct halo_forge_hold const *hold)
{
	boolean begun = FALSE;

	if (!forge_globals.mod_hold && forge_globals.held_object_index == NONE)
	{
		forge_globals.mod_hold = hold;
		begun = TRUE;
	}

	return begun;
}

long forge_object_at_crosshair(
	void)
{
	long object_index = NONE;

	if (local_player_get_player_index(FORGE_LOCAL_PLAYER_INDEX) != NONE &&
		observer_get_camera(FORGE_LOCAL_PLAYER_INDEX)->location.cluster_index != NONE)
	{
		object_index = forge_pick(FORGE_LOCAL_PLAYER_INDEX);
	}

	return object_index;
}

int forge_placement_at_crosshair(
	long definition_index,
	float position[3])
{
	real_point3d point;
	real_vector3d normal;
	boolean placed = FALSE;

	if (local_player_get_player_index(FORGE_LOCAL_PLAYER_INDEX) != NONE &&
		forge_aim(FORGE_LOCAL_PLAYER_INDEX, NONE, &point, &normal))
	{
		real_point3d result;

		forge_held_position(definition_index, &point, &normal, 0.f, &result);
		position[0] = result.x;
		position[1] = result.y;
		position[2] = result.z;
		placed = TRUE;
	}

	return placed;
}

int forge_point_at_crosshair(
	float position[3],
	float normal[3])
{
	real_point3d point;
	real_vector3d surface_normal;
	boolean aimed = FALSE;

	if (local_player_get_player_index(FORGE_LOCAL_PLAYER_INDEX) != NONE &&
		forge_aim(FORGE_LOCAL_PLAYER_INDEX, NONE, &point, &surface_normal))
	{
		position[0] = point.x;
		position[1] = point.y;
		position[2] = point.z;
		normal[0] = surface_normal.i;
		normal[1] = surface_normal.j;
		normal[2] = surface_normal.k;
		aimed = TRUE;
	}

	return aimed;
}
