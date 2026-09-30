/*
FORGE_AI.C

A source mod (mods/forge_ai): AI characters in any map, from the dev tools'
menu (F3 / D-pad right), on the "AI" tab. A keyboard and a controller work
alike.

- Character: the actor variants the map has (tags of the group actv whose
  actor and biped are there): in a multiplayer map only those the launcher
  brings in from another map (mods.json "import", the actv group; the
  launcher's Ctrl-T list), such as the grunts, hunters and elites of the
  campaign.
- Spawn at the crosshair: a character stands there as the map's own
  creatures do (the biped is made, dressed as the variant says and given an
  actor with the game's own ai_attach_free), so it sees, shoots, takes cover
  and dies as in the campaign. It is hostile to the players by the game's own
  teams.
- Waypoints: "add a waypoint" puts a point at the crosshair; the points are
  drawn in the world. With two or more, characters walk from one to the next
  and round again, pausing at each, until something draws their attention:
  the game's own command list (ai_command_lists of the scenario, one made
  here for each map, with go to and pause commands and a loop) sends them, so
  the game's pathfinding, which multiplayer maps have, takes them round walls
  and up ramps.
- New characters patrol along the points by themselves, or "send everyone"
  does it for the ones already there; "remove all" takes them away.

The points and characters are forgotten when the map changes; nothing is
kept for other players: local games only.
*/

#include "cseries.h"
#include "ai/actor_definitions.h"
#include "ai/actors.h"
#include "ai/ai_scenario_definitions.h"
#include "ai/ai_script.h"
#include "cache/cache_files.h"
#include "camera/observer.h"
#include "cseries/cseries_windows.h"
#include "game/game.h"
#include "game/players.h"
#include "interface/terminal.h"
#include "math/real_math.h"
#include "objects/objects.h"
#include "physics/collisions.h"
#include "rasterizer/rasterizer_debug.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "tag_files/tag_groups.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the game's malloc and free are debugging macros; this mod's blocks belong
to the scenario for a whole map, so they are the C library's */
#undef malloc
#undef free

/* ---------- constants */

enum
{
	FORGE_AI_LOCAL_PLAYER_INDEX = 0,
	FORGE_AI_MAXIMUM_CHARACTERS = 48,
	FORGE_AI_MAXIMUM_WAYPOINTS = 24,
	FORGE_AI_MAXIMUM_UNITS = 64,
	/* a go to and a pause for each point, and one loop */
	FORGE_AI_MAXIMUM_COMMANDS = 2 * FORGE_AI_MAXIMUM_WAYPOINTS + 1,
	/* how far below a point the ground is looked for, and how far above it it starts */
	FORGE_AI_GROUND_SEARCH_UP = 1,
	FORGE_AI_GROUND_SEARCH_DOWN = 4,
	FORGE_AI_PAUSE_SECONDS = 2
};

#define FORGE_AI_WAYPOINT_COLOR_R 1.f
#define FORGE_AI_WAYPOINT_COLOR_G 0.75f
#define FORGE_AI_WAYPOINT_COLOR_B 0.1f

enum
{
	_forge_ai_row_character = 0,
	_forge_ai_row_spawn,
	_forge_ai_row_add_waypoint,
	_forge_ai_row_show_waypoints,
	_forge_ai_row_clear_waypoints,
	_forge_ai_row_patrol,
	_forge_ai_row_send,
	_forge_ai_row_remove,
	NUMBER_OF_FORGE_AI_ROWS
};

/* ---------- globals */

static struct
{
	/* the actor variants that can be spawned, by name */
	long characters[FORGE_AI_MAXIMUM_CHARACTERS];
	short character_count;
	short character_index;

	real_point3d waypoints[FORGE_AI_MAXIMUM_WAYPOINTS];
	long waypoint_surfaces[FORGE_AI_MAXIMUM_WAYPOINTS];
	short waypoint_count;
	boolean show_waypoints;
	boolean patrol_new;

	/* what was spawned: the units (object indices) */
	long units[FORGE_AI_MAXIMUM_UNITS];
	short unit_count;

	/* the scenario's command lists with ours at the end, made for this map */
	struct ai_command_list_definition *command_lists;
	struct ai_command_definition commands[FORGE_AI_MAXIMUM_COMMANDS];
	struct ai_command_point_definition points[FORGE_AI_MAXIMUM_WAYPOINTS];
	short command_list_index;
} forge_ai_globals = { { 0 }, 0, 0, { { 0 } }, { 0 }, 0, TRUE, TRUE };

/* ---------- private code */

static boolean forge_ai_local_game(
	void)
{
	return game_connection() == _game_connection_local &&
		local_player_get_player_index(FORGE_AI_LOCAL_PLAYER_INDEX) != NONE;
}

/* the last part of a tag's name */
static char const *forge_ai_short_name(
	long tag_index)
{
	char const *name = tag_get_name(tag_index);
	char const *separator = strrchr(name, '\\');

	return separator ? separator + 1 : name;
}

/* the map's actor variants that have an actor and a biped, by name */
static void forge_ai_find_characters(
	void)
{
	struct tag_iterator iterator;
	long tag_index;

	forge_ai_globals.character_count = 0;
	tag_iterator_new(&iterator, ACTOR_VARIANT_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE &&
		forge_ai_globals.character_count < FORGE_AI_MAXIMUM_CHARACTERS)
	{
		struct actor_variant_definition *variant = actor_variant_definition_get(tag_index);
		struct actor_definition *actor = variant->actor_reference.index != NONE
			? actor_definition_get(variant->actor_reference.index)
			: NULL;

		if (actor && variant->unit_reference.index != NONE &&
			!TEST_FLAG(actor->flags, _actor_definition_swarm_actor_bit))
		{
			short position = forge_ai_globals.character_count++;

			/* insertion sort by name */
			while (position > 0 &&
				_stricmp(tag_get_name(forge_ai_globals.characters[position - 1]), tag_get_name(tag_index)) > 0)
			{
				forge_ai_globals.characters[position] = forge_ai_globals.characters[position - 1];
				position--;
			}
			forge_ai_globals.characters[position] = tag_index;
		}
	}
	if (forge_ai_globals.character_index >= forge_ai_globals.character_count)
		forge_ai_globals.character_index = 0;

	return;
}

/* the units still there, packed */
static short forge_ai_living_units(
	void)
{
	short read;
	short write = 0;

	for (read = 0; read < forge_ai_globals.unit_count; read++)
	{
		if (object_try_and_get(forge_ai_globals.units[read]))
			forge_ai_globals.units[write++] = forge_ai_globals.units[read];
	}
	forge_ai_globals.unit_count = write;

	return write;
}

/* the collision surface (the pathfinding surface) below a point, or NONE */
static long forge_ai_ground_surface(
	real_point3d *point)
{
	struct collision_result collision;
	real_point3d origin = *point;
	real_vector3d ray;

	origin.z += FORGE_AI_GROUND_SEARCH_UP;
	ray.i = 0.f;
	ray.j = 0.f;
	ray.k = -(real)(FORGE_AI_GROUND_SEARCH_UP + FORGE_AI_GROUND_SEARCH_DOWN);
	if (collision_test_vector(
		FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
		&origin,
		&ray,
		NONE,
		&collision) &&
		collision.type == _collision_result_structure)
	{
		*point = collision.point;
		return collision.surface_index;
	}

	return NONE;
}

/* ---------- the command list

The scenario of a multiplayer map has no command lists. A block cannot grow
(tag_block_resize is not supported with a cache file), but the block is only
a count and an address, so each map gets a copy of its list with one more at
the end, ours, whose commands and points are arrays here that change when
the waypoints do. */

/* makes the command list of this map: its own plus ours */
static void forge_ai_install_command_list(
	void)
{
	struct scenario *scenario = global_scenario_get();
	long count = scenario->ai_command_lists.count;
	struct ai_command_list_definition *lists;
	struct ai_command_list_definition *list;

	lists = (struct ai_command_list_definition *)calloc(count + 1, sizeof(struct ai_command_list_definition));
	if (!lists)
		return;
	if (count > 0)
		memcpy(lists, scenario->ai_command_lists.address, count * sizeof(struct ai_command_list_definition));
	list = &lists[count];
	strcpy(list->name, "forge_ai_patrol");
	list->manual_structure_bsp_reference_index = global_structure_bsp_index;
	list->runtime_structure_bsp_reference_index = global_structure_bsp_index;
	list->commands.address = forge_ai_globals.commands;
	list->points.address = forge_ai_globals.points;

	free(forge_ai_globals.command_lists);
	forge_ai_globals.command_lists = lists;
	forge_ai_globals.command_list_index = (short)count;
	scenario->ai_command_lists.count = count + 1;
	scenario->ai_command_lists.address = lists;

	return;
}

/* the commands for the waypoints: go to each and pause there, then again from the first */
static void forge_ai_update_command_list(
	void)
{
	struct ai_command_list_definition *list = forge_ai_globals.command_lists
		? &forge_ai_globals.command_lists[forge_ai_globals.command_list_index]
		: NULL;
	short index;
	short command_count = 0;

	if (!list)
		return;
	for (index = 0; index < forge_ai_globals.waypoint_count; index++)
	{
		struct ai_command_definition *go_to = &forge_ai_globals.commands[command_count++];
		struct ai_command_definition *pause = &forge_ai_globals.commands[command_count++];

		memset(go_to, 0, sizeof(*go_to));
		go_to->atom_type = _ai_atom_go_to;
		go_to->atom_modifier = _ai_atom_go_to_modifier_stop_at_point;
		go_to->point1_index = index;
		go_to->point2_index = NONE;
		go_to->animation_reference_index = NONE;
		go_to->script_reference_index = NONE;
		go_to->recording_reference_index = NONE;
		go_to->command_index = NONE;
		go_to->object_name_index = NONE;
		*pause = *go_to;
		pause->atom_type = _ai_atom_pause;
		pause->atom_modifier = 0;
		pause->parameter1 = FORGE_AI_PAUSE_SECONDS;
		pause->point1_index = NONE;
		forge_ai_globals.points[index].position = forge_ai_globals.waypoints[index];
		forge_ai_globals.points[index].surface_index = forge_ai_globals.waypoint_surfaces[index];
		forge_ai_globals.points[index].unused = 0;
	}
	if (index > 0)
	{
		struct ai_command_definition *loop = &forge_ai_globals.commands[command_count++];

		memset(loop, 0, sizeof(*loop));
		loop->atom_type = _ai_atom_loop;
		loop->atom_modifier = 0;
		loop->point1_index = NONE;
		loop->point2_index = NONE;
		loop->animation_reference_index = NONE;
		loop->script_reference_index = NONE;
		loop->recording_reference_index = NONE;
		loop->command_index = 0;
		loop->object_name_index = NONE;
	}
	list->commands.count = command_count;
	list->points.count = forge_ai_globals.waypoint_count;

	return;
}

/* sends the living characters along the waypoints */
static short forge_ai_send_patrols(
	void)
{
	short index;
	short sent = 0;

	if (forge_ai_globals.waypoint_count < 2 || !forge_ai_globals.command_lists)
		return 0;
	for (index = 0; index < forge_ai_living_units(); index++)
	{
		ai_scripting_command_list_by_unit(forge_ai_globals.units[index], forge_ai_globals.command_list_index);
		sent++;
	}

	return sent;
}

/* ---------- the actions */

static void forge_ai_spawn(
	void)
{
	struct actor_variant_definition *variant;
	long variant_index;
	long unit_definition_index;
	float position[3];
	struct object_placement_data data;
	struct observer_result const *camera = observer_get_camera(FORGE_AI_LOCAL_PLAYER_INDEX);
	long unit_index;

	if (!forge_ai_local_game())
	{
		terminal_printf(global_real_argb_orange, "forge_ai: characters are made only in local games");
		return;
	}
	if (forge_ai_globals.character_count == 0)
	{
		terminal_printf(global_real_argb_orange,
			"forge_ai: this map has no characters (import one: the launcher, Ctrl-T, tags of the actv group)");
		return;
	}
	if (forge_ai_living_units() >= FORGE_AI_MAXIMUM_UNITS)
	{
		terminal_printf(global_real_argb_orange, "forge_ai: too many characters");
		return;
	}
	variant_index = forge_ai_globals.characters[forge_ai_globals.character_index];
	variant = actor_variant_definition_get(variant_index);
	unit_definition_index = variant->unit_reference.index;
	if (!forge_placement_at_crosshair(unit_definition_index, position))
	{
		terminal_printf(global_real_argb_orange, "forge_ai: the camera is outside the map");
		return;
	}

	object_placement_data_new(&data, unit_definition_index, NONE);
	data.position.x = position[0];
	data.position.y = position[1];
	data.position.z = position[2];
	/* facing the player */
	data.forward.i = -camera->forward.i;
	data.forward.j = -camera->forward.j;
	data.forward.k = 0.f;
	if (data.forward.i * data.forward.i + data.forward.j * data.forward.j < 0.0001f)
		data.forward = *global_forward3d;
	else
		normalize3d(&data.forward);
	data.up = *global_up3d;
	unit_index = object_new(&data);
	if (unit_index == NONE)
	{
		terminal_printf(global_real_argb_orange, "forge_ai: %s could not be made", forge_ai_short_name(variant_index));
		return;
	}
	actor_customize_unit(variant_index, unit_index);
	ai_scripting_attach_free(unit_index, variant_index);
	forge_ai_globals.units[forge_ai_globals.unit_count++] = unit_index;
	if (forge_ai_globals.patrol_new && forge_ai_globals.waypoint_count >= 2 && forge_ai_globals.command_lists)
		ai_scripting_command_list_by_unit(unit_index, forge_ai_globals.command_list_index);
	terminal_printf(global_real_argb_green, "forge_ai: %s", forge_ai_short_name(variant_index));

	return;
}

static void forge_ai_add_waypoint(
	void)
{
	float position[3];
	float normal[3];
	real_point3d point;

	if (!forge_ai_local_game())
	{
		terminal_printf(global_real_argb_orange, "forge_ai: waypoints are for local games");
		return;
	}
	if (forge_ai_globals.waypoint_count >= FORGE_AI_MAXIMUM_WAYPOINTS)
	{
		terminal_printf(global_real_argb_orange, "forge_ai: at most %d waypoints", FORGE_AI_MAXIMUM_WAYPOINTS);
		return;
	}
	if (!forge_point_at_crosshair(position, normal))
	{
		terminal_printf(global_real_argb_orange, "forge_ai: the camera is outside the map");
		return;
	}
	point.x = position[0];
	point.y = position[1];
	point.z = position[2];
	{
		long surface_index = forge_ai_ground_surface(&point);

		if (surface_index == NONE)
		{
			terminal_printf(global_real_argb_orange, "forge_ai: no floor there for a waypoint");
			return;
		}
		forge_ai_globals.waypoints[forge_ai_globals.waypoint_count] = point;
		forge_ai_globals.waypoint_surfaces[forge_ai_globals.waypoint_count] = surface_index;
	}
	forge_ai_globals.waypoint_count++;
	forge_ai_update_command_list();
	forge_ai_send_patrols();
	terminal_printf(global_real_argb_green, "forge_ai: waypoint %d", forge_ai_globals.waypoint_count);

	return;
}

/* ---------- the tab */

static short forge_ai_menu_row_count(
	void)
{
	return NUMBER_OF_FORGE_AI_ROWS;
}

static void forge_ai_menu_row_text(
	short row,
	char *label,
	unsigned long label_size,
	char *value,
	unsigned long value_size)
{
	switch (row)
	{
	case _forge_ai_row_character:
		_snprintf(label, label_size, "Character");
		if (forge_ai_globals.character_count > 0)
			_snprintf(value, value_size, "%s", forge_ai_short_name(forge_ai_globals.characters[forge_ai_globals.character_index]));
		else
			_snprintf(value, value_size, "none: import one");
		break;
	case _forge_ai_row_spawn:
		_snprintf(label, label_size, "Add a character (crosshair)");
		_snprintf(value, value_size, "%d there", (int)forge_ai_living_units());
		break;
	case _forge_ai_row_add_waypoint:
		_snprintf(label, label_size, "Add a waypoint (crosshair)");
		_snprintf(value, value_size, "%d set", (int)forge_ai_globals.waypoint_count);
		break;
	case _forge_ai_row_show_waypoints:
		_snprintf(label, label_size, "Show the waypoints");
		_snprintf(value, value_size, "%s", forge_ai_globals.show_waypoints ? "on" : "off");
		break;
	case _forge_ai_row_clear_waypoints:
		_snprintf(label, label_size, "Clear the waypoints");
		break;
	case _forge_ai_row_patrol:
		_snprintf(label, label_size, "New ones patrol the waypoints");
		_snprintf(value, value_size, "%s", forge_ai_globals.patrol_new ? "on" : "off");
		break;
	case _forge_ai_row_send:
		_snprintf(label, label_size, "Send all along the waypoints");
		break;
	case _forge_ai_row_remove:
		_snprintf(label, label_size, "Remove all");
		break;
	}

	return;
}

/* direction -1 or +1 changes a value; 0 acts. TRUE closes the menu. */
static int forge_ai_menu_row_change(
	short row,
	int direction)
{
	boolean close_menu = FALSE;

	switch (row)
	{
	case _forge_ai_row_character:
		if (forge_ai_globals.character_count > 0 && direction != 0)
		{
			forge_ai_globals.character_index = (short)((forge_ai_globals.character_index +
				forge_ai_globals.character_count + direction) % forge_ai_globals.character_count);
		}
		break;
	case _forge_ai_row_spawn:
		if (direction == 0)
		{
			forge_ai_spawn();
			close_menu = TRUE;
		}
		break;
	case _forge_ai_row_add_waypoint:
		if (direction == 0)
		{
			forge_ai_add_waypoint();
			close_menu = TRUE;
		}
		break;
	case _forge_ai_row_show_waypoints:
		forge_ai_globals.show_waypoints = (boolean)!forge_ai_globals.show_waypoints;
		break;
	case _forge_ai_row_clear_waypoints:
		if (direction == 0)
		{
			forge_ai_globals.waypoint_count = 0;
			forge_ai_update_command_list();
		}
		break;
	case _forge_ai_row_patrol:
		forge_ai_globals.patrol_new = (boolean)!forge_ai_globals.patrol_new;
		break;
	case _forge_ai_row_send:
		if (direction == 0)
		{
			short sent = forge_ai_send_patrols();

			if (forge_ai_globals.waypoint_count < 2)
				terminal_printf(global_real_argb_orange, "forge_ai: set two or more waypoints first");
			else
				terminal_printf(global_real_argb_green, "forge_ai: %d sent along %d waypoints", (int)sent,
					(int)forge_ai_globals.waypoint_count);
		}
		break;
	case _forge_ai_row_remove:
		if (direction == 0)
		{
			short index;

			for (index = 0; index < forge_ai_living_units(); index++)
				object_delete(forge_ai_globals.units[index]);
			forge_ai_globals.unit_count = 0;
		}
		break;
	}

	return close_menu;
}

static void forge_ai_menu_opened(
	void)
{
	forge_ai_find_characters();

	return;
}

static struct halo_mod_menu const forge_ai_menu =
{
	"AI",
	forge_ai_menu_row_count,
	forge_ai_menu_row_text,
	forge_ai_menu_row_change,
	forge_ai_menu_opened
};

/* ---------- the hooks */

/* a new map: nothing of the last is left, and this one gets its command list */
static void forge_ai_new_map(
	void)
{
	forge_ai_globals.waypoint_count = 0;
	forge_ai_globals.unit_count = 0;
	forge_ai_globals.command_lists = NULL;
	forge_ai_globals.command_list_index = NONE;
	forge_ai_find_characters();
	if (game_connection() == _game_connection_local)
	{
		/* (the previous map's list array is not freed: its scenario is gone, and a stale
		pointer to it could be in a command list still running for a moment) */
		forge_ai_install_command_list();
		forge_ai_update_command_list();
	}

	return;
}

/* the waypoints, in each player's view */
static void forge_ai_render_world(
	void)
{
	short index;
	real_argb_color color;

	if (!forge_ai_globals.show_waypoints || forge_ai_globals.waypoint_count == 0 ||
		game_connection() != _game_connection_local)
		return;

	color.alpha = 0.9f;
	color.red = FORGE_AI_WAYPOINT_COLOR_R;
	color.green = FORGE_AI_WAYPOINT_COLOR_G;
	color.blue = FORGE_AI_WAYPOINT_COLOR_B;
	for (index = 0; index < forge_ai_globals.waypoint_count; index++)
	{
		real_point3d const *point = &forge_ai_globals.waypoints[index];
		real_point3d top = *point;
		real_point3d next;
		real_point3d corners[4];
		short corner;

		top.z += 1.2f;
		rasterizer_debug_line_shaded(point, &top, &color, &color);
		/* a small diamond on the point, so it is easy to find */
		for (corner = 0; corner < 4; corner++)
		{
			corners[corner] = *point;
			corners[corner].z += 0.2f;
			corners[corner].x += (corner == 0 ? 0.25f : corner == 2 ? -0.25f : 0.f);
			corners[corner].y += (corner == 1 ? 0.25f : corner == 3 ? -0.25f : 0.f);
		}
		for (corner = 0; corner < 4; corner++)
		{
			rasterizer_debug_line_shaded(&corners[corner], &corners[(corner + 1) % 4], &color, &color);
			rasterizer_debug_line_shaded(&corners[corner], &top, &color, &color);
		}
		/* the way round */
		next = forge_ai_globals.waypoints[(index + 1) % forge_ai_globals.waypoint_count];
		if (forge_ai_globals.waypoint_count > 1 && (index + 1 < forge_ai_globals.waypoint_count || forge_ai_globals.waypoint_count > 2))
		{
			real_point3d from = *point;
			real_point3d to = next;

			from.z += 0.3f;
			to.z += 0.3f;
			rasterizer_debug_line_shaded(&from, &to, &color, &color);
		}
	}

	return;
}

static struct halo_mod const forge_ai_mod =
{
	"forge_ai",
	NULL,
	NULL,
	NULL,
	forge_ai_new_map,
	forge_ai_render_world,
	&forge_ai_menu,
	NULL
};

HALO_MOD_REGISTER(forge_ai_mod)
