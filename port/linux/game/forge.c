/*
FORGE.C

In-game dev tools for the native ports (port/linux, port/windows,
port/android), off unless HALO_FORGE is set (see port/linux/README.md, "Dev
tools"). The byte-matching build never compiles this file.

	F2 / D-pad left
	        detach the camera from the player and fly it freely, through
	        walls, with a crosshair; again moves the player to the camera
	        and returns the camera to the player
	F3 / D-pad right
	        open or close the spawn menu: every vehicle, weapon, equipment,
	        biped and scenery object of the map
	F4, and while flying also enter, the left mouse button or A
	        pick up the object under the crosshair

While flying: W A S D (left stick) move, the mouse (right stick) looks,
space and left ctrl or C (the triggers) rise and sink straight up and down,
shift (left stick click) is faster, and the D-pad's up and down change the
speed. Z (right stick click) hands the controls back to the player and
leaves the camera where it is, as the game's own debug flying camera does
(camera/director.c).

In the menu: up and down choose, left and right change the category, enter,
the left mouse button or A take the object, escape, backspace, the right
mouse button or B close the menu. After the object categories come the
pages that source mods add (halo_mod.h, struct halo_mod_menu): rows with a
value that left and right change and that enter or A act on. T / X and V / Y
go to the previous and next tab on every page.

A taken or picked up object is held where the crosshair points (or, not
flying, where the player looks), standing on the surface there, until it
is placed:
	left, right     turn it about the chosen axis
	up, down        raise and lower it
	T / X           choose the axis: yaw, pitch or roll
	V / Y           choose the step: free, or 15, 45, 90 or 180 degrees
	enter, left mouse button / A
	                put it down
	escape, backspace, right mouse button / B
	                cancel: a spawned object goes, a picked up one returns
	delete / back   remove it from the map
The platform layer keeps these keys and buttons from the game while the
menu is open or an object is held.

The keys belong to the player on controller 1, the keyboard and the first
gamepad, and act on local player 0. Spawning, moving and removing objects
and moving the player change the game state, which every machine of a
system link game must compute alike, so they work only in local games.
*/

#include "cseries.h"
#include "cache/cache_files.h"
#include "camera/director.h"
#include "camera/observer.h"
#include "cseries/cseries_windows.h"
#include "game/game.h"
#include "game/player_control.h"
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
#include "tag_files/tag_files.h"
#include "tag_files/tag_groups.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "text/text_group.h"
#include "units/units.h"

#include <stdlib.h>

/* ---------- constants */

enum
{
	FORGE_LOCAL_PLAYER_INDEX = 0,
	FORGE_MAXIMUM_MENU_ENTRIES = 1024,
	FORGE_MENU_VISIBLE_ENTRIES = 14,
	FORGE_MENU_MARGIN = 24,
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

	boolean active;
	boolean menu_open;
	boolean keys_captured;
	short category_index;
	short entry_count;
	short selected_entry_index;
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

	/* a mod's own thing held (halo_forge.h), and when it was last updated */
	struct halo_forge_hold const *mod_hold;
	unsigned long mod_hold_milliseconds;
};

/* ---------- globals */

static struct forge_category const forge_categories[] =
{
	{ _object_type_vehicle, "vehicles" },
	{ _object_type_weapon, "weapons" },
	{ _object_type_equipment, "equipment" },
	{ _object_type_biped, "bipeds" },
	{ _object_type_scenery, "scenery" }
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
	else if (game_connection() != _game_connection_local)
	{
		terminal_printf(global_real_argb_orange, "forge: the player moves only in local games");
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
	else if (director_camera_scripted->camera_scripted)
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

/* the map's objects of the menu's category, by name */
static void forge_menu_build(
	void)
{
	struct halo_mod_menu const *page = forge_menu_page();
	short object_type;
	struct tag_iterator iterator;
	long tag_index;

	if (page)
	{
		forge_globals.entry_count = page->row_count();
		forge_globals.selected_entry_index = 0;
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
	forge_globals.selected_entry_index = 0;

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

	if (game_connection() != _game_connection_local)
	{
		terminal_printf(global_real_argb_orange, "forge: objects spawn only in local games");
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

		object_index = object_new(&data);
		if (object_index == NONE)
			terminal_printf(global_real_argb_orange, "forge: %s could not be created", tag_get_name(definition_index));
		else
			forge_hold_begin(object_index, TRUE);
	}

	return;
}

/* picks up the object under the crosshair */
static void forge_grab(
	short local_player_index)
{
	long object_index;

	if (game_connection() != _game_connection_local)
	{
		terminal_printf(global_real_argb_orange, "forge: objects move only in local games");
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

/* keeps the held object where the crosshair points, turned and raised as
asked, and still */
static void forge_hold(
	struct halo_linux_forge_keys const *keys,
	boolean left,
	boolean right,
	real seconds)
{
	struct object_datum *object = object_get(forge_globals.held_object_index);
	real_point3d point;
	real_vector3d normal;

	if (keys->menu_left && !keys->menu_right)
		forge_turn(1.f, left, seconds);
	else if (keys->menu_right && !keys->menu_left)
		forge_turn(-1.f, right, seconds);
	forge_globals.held_height += (real)((keys->menu_up != 0) - (keys->menu_down != 0)) *
		FORGE_RAISE_SPEED * seconds;

	if (forge_aim(FORGE_LOCAL_PLAYER_INDEX, forge_globals.held_object_index, &point, &normal))
	{
		real_point3d position;
		real_vector3d forward;
		real_vector3d up;

		forge_held_position(object->definition_index, &point, &normal, forge_globals.held_height, &position);
		forge_orientation_from_angles(forge_globals.held_angles, &forward, &up);
		object_set_position(forge_globals.held_object_index, &position, &forward, &up);
		object->object.translational_velocity = *global_zero_vector3d;
		object->object.angular_velocity = *global_zero_vector3d;
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

	if (remove_object || (cancel && forge_globals.held_spawned))
	{
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
	}
	else
	{
		if (toggle_flying)
			forge_toggle_flying(FORGE_LOCAL_PLAYER_INDEX);

		{
			long debug_tab = forge_debug_menu_tab();

			if (debug_tab >= 0 && debug_tab < (long)(NUMBEROF(forge_categories) + halo_mods_menu_page_count()))
			{
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
			input.snap_pressed = rotation_snap;
			input.place = select;
			input.cancel = close;
			input.delete_pressed = remove_object;
			if (forge_globals.mod_hold->update(&input))
				forge_globals.mod_hold = NULL;
		}
		else if (forge_globals.held_object_index != NONE)
		{
			if (select || close || remove_object)
			{
				forge_hold_end(close, remove_object);
			}
			else
			{
				if (rotation_axis)
					forge_globals.held_axis = (forge_globals.held_axis + 1) % NUMBER_OF_FORGE_AXES;
				if (rotation_snap)
					forge_globals.held_snap_index = (forge_globals.held_snap_index + 1) % NUMBEROF(forge_snaps);
				forge_hold(keys, left, right, seconds);
			}
		}
		else if (forge_globals.menu_open)
		{
			if (menu || close)
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
					forge_menu_build();
					page = forge_menu_page();
				}
				if (page)
				{
					forge_menu_page_keys(page, up, down, left, right, select);
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
		else if (menu)
		{
			forge_globals.menu_open = TRUE;
			forge_menu_build();
		}
		else if (grab || (select && director_forge_flying(FORGE_LOCAL_PLAYER_INDEX)))
		{
			/* a mod may have something of its own to pick up there */
			if (game_connection() != _game_connection_local || !halo_mods_grab())
				forge_grab(FORGE_LOCAL_PLAYER_INDEX);
		}
	}

	/* the keys stay away from the game until they are released, so the
	escape that closes the menu does not also pause the game */
	forge_globals.keys_captured =
		forge_globals.menu_open ||
		forge_globals.held_object_index != NONE ||
		forge_globals.mod_hold != NULL ||
		(forge_globals.keys_captured &&
			(keys->menu_up || keys->menu_down || keys->menu_left || keys->menu_right ||
			keys->menu_select || keys->menu_close || keys->rotation_axis || keys->rotation_snap ||
			keys->remove_object || keys->tab_previous || keys->tab_next));
	halo_linux_forge_capture_menu_keys(forge_globals.keys_captured);
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
		"turning: %s (T/X), step: %s (V/Y)",
		forge_axis_names[forge_globals.held_axis],
		forge_snaps[forge_globals.held_snap_index].name);
	forge_draw_line(font_tag_index, &bounds, _text_justification_left, global_real_argb_white, line);
	offset_rectangle2d(&bounds, 0, line_height);

	forge_draw_line(
		font_tag_index,
		&bounds,
		_text_justification_left,
		global_real_argb_grey,
		"aim to move, left/right turn, up/down raise, enter/click/A place, esc/right click/B cancel, delete/back remove");

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
		"aim to move, left/right turn, up/down raise, Y/V step, enter/click/A place, esc/right click/B cancel, delete/back remove");

	return;
}

static void forge_render_menu(
	long font_tag_index,
	rectangle2d const *window,
	short line_height)
{
	char line[256];
	rectangle2d bounds;
	struct halo_mod_menu const *page = forge_menu_page();
	short first_entry_index = PIN(
		forge_globals.selected_entry_index - FORGE_MENU_VISIBLE_ENTRIES / 2,
		0,
		MAX(forge_globals.entry_count - FORGE_MENU_VISIBLE_ENTRIES, 0));
	short entry_index;

	forge_first_line_bounds(window, line_height, &bounds);
	_snprintf(
		line,
		NUMBEROF(line),
		"%s: < %s >  %d of %d",
		page ? "tools" : "spawn",
		page ? page->title : forge_categories[forge_globals.category_index].name,
		forge_globals.entry_count ? forge_globals.selected_entry_index + 1 : 0,
		forge_globals.entry_count);
	forge_draw_line(font_tag_index, &bounds, _text_justification_left, global_real_argb_yellow, line);
	offset_rectangle2d(&bounds, 0, line_height);

	if (forge_globals.entry_count == 0)
	{
		forge_draw_line(font_tag_index, &bounds, _text_justification_left, global_real_argb_grey, "  (none in this map)");
		offset_rectangle2d(&bounds, 0, line_height);
	}
	for (entry_index = first_entry_index;
		entry_index < forge_globals.entry_count &&
			entry_index < first_entry_index + FORGE_MENU_VISIBLE_ENTRIES;
		entry_index++)
	{
		boolean selected = entry_index == forge_globals.selected_entry_index;

		if (page)
		{
			char label[128];
			char value[128];

			label[0] = value[0] = 0;
			page->row_text(entry_index, label, sizeof(label), value, sizeof(value));
			_snprintf(line, NUMBEROF(line), "%s %s%s%s", selected ? ">" : " ", label, value[0] ? ": " : "", value);
		}
		else
		{
			_snprintf(
				line,
				NUMBEROF(line),
				"%s %s",
				selected ? ">" : " ",
				tag_get_name(forge_globals.entries[entry_index]));
		}
		forge_draw_line(
			font_tag_index,
			&bounds,
			_text_justification_left,
			selected ? global_real_argb_green : global_real_argb_white,
			line);
		offset_rectangle2d(&bounds, 0, line_height);
	}

	forge_draw_line(
		font_tag_index,
		&bounds,
		_text_justification_left,
		global_real_argb_grey,
		page
			? "up/down choose, left/right change, enter/A do, T/X V/Y tabs, esc/B close"
			: "up/down choose, left/right category, enter/click/A take, esc/right click/B close");

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
			forge_render_menu(font_tag_index, &window, line_height);
		}
	}

	/* drawn only after an update: nothing is left over when the game stops
	updating the tools (the main menu, loading) */
	forge_globals.active = FALSE;

	return;
}

/* ---------- what source mods may ask the tools (halo_forge.h) */

int forge_busy(
	void)
{
	return forge_globals.menu_open || forge_globals.held_object_index != NONE || forge_globals.mod_hold != NULL;
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
