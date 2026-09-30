/*
FORGE_AI.C

A source mod (mods/forge_ai): AI characters in any map, from the dev tools'
menu (in forge mode: F3, D-pad right or X), on the "AI" tab. A keyboard and
a controller work alike.

- Character: the actor variants the map has (tags of the group actv whose
  actor and biped are there): in a multiplayer map only those the launcher
  brings in from another map (mods.json "import", the actv group; the
  launcher's Ctrl-T list), such as the grunts, hunters and elites of the
  campaign.
- Spawn at the crosshair: a character stands there as the map's own
  creatures do (the biped is made, dressed as the variant says and given an
  actor with the game's own ai_attach_free), so it sees, shoots, takes cover
  and dies as in the campaign.
- Team: new characters are enemies of the player, or friends (the player's
  own team: they fight the enemies instead, the map's and these).
- Attack on sight: patrolling characters break off to fight an enemy they
  see or hear (the command list's initiative and targeting), and go back to
  their patrol once the fight is over and they are idle again. Off, they
  walk their round whatever happens.
- Waypoints: "add a waypoint" puts a point at the crosshair; the points are
  drawn in the world. With two or more, characters walk from one to the next
  and round again, pausing at each, until something draws their attention:
  the game's own command list (ai_command_lists of the scenario, one made
  here for each map, with go to and pause commands and a loop) sends them, so
  the game's pathfinding, which multiplayer maps have, takes them round walls
  and up ramps.
- New characters patrol along the points by themselves, or "send everyone"
  does it for the ones already there; "remove all" takes them away.
- A waypoint is picked up like an object while the waypoints are shown:
  aim at it and press A / enter (flying) or F4, aim to move it, A / enter
  puts it down (on the floor there), B / escape puts it back, Y / delete
  removes it. The characters then walk the changed round.

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
#include "rasterizer/rasterizer.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "tag_files/tag_groups.h"
#include "units/units.h"

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
	FORGE_AI_PAUSE_SECONDS = 2,
	/* how often idle characters are sent back to their patrol */
	FORGE_AI_REPATROL_MILLISECONDS = 3000,
	/* the game's teams (game/game_allegiance.c) */
	FORGE_AI_TEAM_COUNT = 10
};

/* a waypoint is aimed at within this far of the middle of its marker */
#define FORGE_AI_WAYPOINT_PICK_RADIUS 0.7f
#define FORGE_AI_WAYPOINT_PICK_HEIGHT 0.6f

#define FORGE_AI_WAYPOINT_COLOR_R 1.f
#define FORGE_AI_WAYPOINT_COLOR_G 0.75f
#define FORGE_AI_WAYPOINT_COLOR_B 0.1f

enum
{
	_forge_ai_row_character = 0,
	_forge_ai_row_team,
	_forge_ai_row_attack,
	_forge_ai_row_spawn,
	_forge_ai_row_add_waypoint,
	_forge_ai_row_move_waypoint,
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
	/* new characters are the player's friends */
	boolean friendly;
	/* patrols break off to fight */
	boolean attack_on_sight;
	unsigned long repatrol_milliseconds;

	/* what was spawned: the units (object indices), their actor variants,
	and whether they are the player's friends */
	long units[FORGE_AI_MAXIMUM_UNITS];
	long unit_variants[FORGE_AI_MAXIMUM_UNITS];
	boolean unit_friendly[FORGE_AI_MAXIMUM_UNITS];
	short unit_count;

	/* the scenario's command lists with ours at the end, made for this map */
	struct ai_command_list_definition *command_lists;
	struct ai_command_definition commands[FORGE_AI_MAXIMUM_COMMANDS];
	struct ai_command_point_definition points[FORGE_AI_MAXIMUM_WAYPOINTS];
	short command_list_index;
} forge_ai_globals = { { 0 }, 0, 0, { { 0 } }, { 0 }, 0, TRUE, TRUE, FALSE, TRUE };

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
		{
			forge_ai_globals.units[write] = forge_ai_globals.units[read];
			forge_ai_globals.unit_variants[write] = forge_ai_globals.unit_variants[read];
			forge_ai_globals.unit_friendly[write] = forge_ai_globals.unit_friendly[read];
			write++;
		}
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
	/* with initiative and targeting the walkers stop to fight what they see */
	list->flags = forge_ai_globals.attack_on_sight
		? FLAG(_ai_command_list_allow_initiative_bit) | FLAG(_ai_command_list_allow_targeting_bit)
		: 0;
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

/* the team of local player 0's unit, 0 without one */
static short forge_ai_player_team(
	void)
{
	long player_index = local_player_get_player_index(FORGE_AI_LOCAL_PLAYER_INDEX);
	long unit_index = player_index != NONE ? player_get(player_index)->unit_index : NONE;
	struct unit_datum *unit = unit_index != NONE ? unit_try_and_get(unit_index) : NULL;

	return unit ? unit->object.owner_team_index : 0;
}

/* characters that fought and are idle again walk their patrol again */
static void forge_ai_repatrol(
	void)
{
	short index;

	if (forge_ai_globals.waypoint_count < 2 || !forge_ai_globals.command_lists || !forge_ai_globals.patrol_new)
		return;
	for (index = 0; index < forge_ai_living_units(); index++)
	{
		struct unit_datum *unit = unit_try_and_get(forge_ai_globals.units[index]);
		struct actor_datum *actor;
		short action;

		if (!unit || unit->unit.actor_index == NONE)
			continue;
		actor = actor_get(unit->unit.actor_index);
		action = actor->state.action;
		if (actor->state.combat_status <= 1 &&
			(action == _actor_action_none || action == _actor_action_alert ||
				action == _actor_action_guard || action == _actor_action_wait))
		{
			ai_scripting_command_list_by_unit(forge_ai_globals.units[index], forge_ai_globals.command_list_index);
		}
	}

	return;
}

/* ---------- the actions */

/* makes a character of the variant, standing at the position, facing
forward; NONE when the game cannot */
static long forge_ai_create(
	long variant_index,
	real_point3d const *position,
	real_vector3d const *forward,
	boolean friendly)
{
	struct actor_variant_definition *variant = actor_variant_definition_get(variant_index);
	struct object_placement_data data;
	long unit_index;

	if (forge_ai_living_units() >= FORGE_AI_MAXIMUM_UNITS)
	{
		terminal_printf(global_real_argb_orange, "forge_ai: too many characters");
		return NONE;
	}
	object_placement_data_new(&data, variant->unit_reference.index, NONE);
	data.position = *position;
	data.forward = *forward;
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
		return NONE;
	}
	actor_customize_unit(variant_index, unit_index);
	{
		/* the actor takes the unit's team when it is attached */
		struct unit_datum *unit = unit_get(unit_index);
		short player_team = forge_ai_player_team();

		if (friendly)
			unit->object.owner_team_index = player_team;
		else if (unit->object.owner_team_index == player_team)
			unit->object.owner_team_index = (short)((player_team + 1) % FORGE_AI_TEAM_COUNT);
	}
	ai_scripting_attach_free(unit_index, variant_index);
	forge_ai_globals.units[forge_ai_globals.unit_count] = unit_index;
	forge_ai_globals.unit_variants[forge_ai_globals.unit_count] = variant_index;
	forge_ai_globals.unit_friendly[forge_ai_globals.unit_count] = friendly;
	forge_ai_globals.unit_count++;
	if (forge_ai_globals.patrol_new && forge_ai_globals.waypoint_count >= 2 && forge_ai_globals.command_lists)
		ai_scripting_command_list_by_unit(unit_index, forge_ai_globals.command_list_index);

	return unit_index;
}

static void forge_ai_spawn(
	void)
{
	struct actor_variant_definition *variant;
	long variant_index;
	float position[3];
	struct observer_result const *camera = observer_get_camera(FORGE_AI_LOCAL_PLAYER_INDEX);
	real_point3d point;
	real_vector3d forward;

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
	variant_index = forge_ai_globals.characters[forge_ai_globals.character_index];
	variant = actor_variant_definition_get(variant_index);
	if (!forge_placement_at_crosshair(variant->unit_reference.index, position))
	{
		terminal_printf(global_real_argb_orange, "forge_ai: the camera is outside the map");
		return;
	}
	set_real_point3d(&point, position[0], position[1], position[2]);
	/* facing the player */
	set_real_vector3d(&forward, -camera->forward.i, -camera->forward.j, 0.f);
	if (forge_ai_create(variant_index, &point, &forward, forge_ai_globals.friendly) != NONE)
	{
		terminal_printf(global_real_argb_green, "forge_ai: %s (%s)", forge_ai_short_name(variant_index),
			forge_ai_globals.friendly ? "friend" : "enemy");
	}

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

/* ---------- picking up a waypoint */

static struct
{
	short waypoint_index;
	real_point3d original;
	long original_surface;
} forge_ai_hold_globals = { NONE };

/* the waypoint the crosshair points at, or NONE */
static short forge_ai_aimed_waypoint(
	void)
{
	struct observer_result const *camera = observer_get_camera(FORGE_AI_LOCAL_PLAYER_INDEX);
	real best_along = 1000.f;
	short best_index = NONE;
	short index;

	for (index = 0; index < forge_ai_globals.waypoint_count; index++)
	{
		real_point3d middle = forge_ai_globals.waypoints[index];
		real_vector3d to_point;
		real along;

		middle.z += FORGE_AI_WAYPOINT_PICK_HEIGHT;
		vector_from_points3d(&camera->position, &middle, &to_point);
		along = dot_product3d(&to_point, &camera->forward);
		if (along > 0.f && along < best_along &&
			magnitude_squared3d(&to_point) - along * along <=
				FORGE_AI_WAYPOINT_PICK_RADIUS * FORGE_AI_WAYPOINT_PICK_RADIUS)
		{
			best_along = along;
			best_index = index;
		}
	}

	return best_index;
}

static void forge_ai_remove_waypoint(
	short waypoint_index)
{
	short index;

	for (index = waypoint_index; index + 1 < forge_ai_globals.waypoint_count; index++)
	{
		forge_ai_globals.waypoints[index] = forge_ai_globals.waypoints[index + 1];
		forge_ai_globals.waypoint_surfaces[index] = forge_ai_globals.waypoint_surfaces[index + 1];
	}
	forge_ai_globals.waypoint_count--;
	forge_ai_update_command_list();
	forge_ai_send_patrols();
	terminal_printf(global_real_argb_green, "forge_ai: waypoint %d removed, %d left", waypoint_index + 1,
		(int)forge_ai_globals.waypoint_count);

	return;
}

static void forge_ai_hold_describe(
	char *line,
	unsigned long size)
{
	_snprintf(line, size, "waypoint %d of %d", forge_ai_hold_globals.waypoint_index + 1,
		(int)forge_ai_globals.waypoint_count);

	return;
}

static int forge_ai_hold_update(
	struct halo_forge_hold_input const *input)
{
	short waypoint_index = forge_ai_hold_globals.waypoint_index;
	float position[3];
	float normal[3];

	/* gone with a new map */
	if (waypoint_index < 0 || waypoint_index >= forge_ai_globals.waypoint_count)
		return TRUE;

	if (!input || input->cancel)
	{
		forge_ai_globals.waypoints[waypoint_index] = forge_ai_hold_globals.original;
		forge_ai_globals.waypoint_surfaces[waypoint_index] = forge_ai_hold_globals.original_surface;
		forge_ai_update_command_list();
		return TRUE;
	}
	if (input->delete_pressed)
	{
		forge_ai_remove_waypoint(waypoint_index);
		return TRUE;
	}

	if (forge_point_at_crosshair(position, normal))
	{
		forge_ai_globals.waypoints[waypoint_index].x = position[0];
		forge_ai_globals.waypoints[waypoint_index].y = position[1];
		forge_ai_globals.waypoints[waypoint_index].z = position[2];
	}

	if (input->place)
	{
		real_point3d point = forge_ai_globals.waypoints[waypoint_index];
		long surface_index = forge_ai_ground_surface(&point);

		if (surface_index == NONE)
		{
			terminal_printf(global_real_argb_orange, "forge_ai: no floor there for a waypoint");
			return FALSE;
		}
		forge_ai_globals.waypoints[waypoint_index] = point;
		forge_ai_globals.waypoint_surfaces[waypoint_index] = surface_index;
		forge_ai_update_command_list();
		forge_ai_send_patrols();
		terminal_printf(global_real_argb_green, "forge_ai: waypoint %d moved", waypoint_index + 1);
		return TRUE;
	}

	return FALSE;
}

static struct halo_forge_hold const forge_ai_hold =
{
	"a waypoint",
	forge_ai_hold_describe,
	forge_ai_hold_update
};

static boolean forge_ai_pick_up_waypoint(
	short waypoint_index)
{
	if (waypoint_index == NONE || !forge_ai_local_game())
		return FALSE;
	forge_ai_hold_globals.waypoint_index = waypoint_index;
	forge_ai_hold_globals.original = forge_ai_globals.waypoints[waypoint_index];
	forge_ai_hold_globals.original_surface = forge_ai_globals.waypoint_surfaces[waypoint_index];

	return forge_mod_hold_begin(&forge_ai_hold);
}

/* the tools' pick up: a shown waypoint under the crosshair, unless an
object is nearer */
static int forge_ai_grab(
	void)
{
	short waypoint_index;
	long object_index;

	if (!forge_ai_globals.show_waypoints || !forge_ai_local_game())
		return FALSE;
	waypoint_index = forge_ai_aimed_waypoint();
	if (waypoint_index == NONE)
		return FALSE;

	object_index = forge_object_at_crosshair();
	if (object_index != NONE && object_try_and_get(object_index))
	{
		struct observer_result const *camera = observer_get_camera(FORGE_AI_LOCAL_PLAYER_INDEX);
		real_point3d middle = forge_ai_globals.waypoints[waypoint_index];

		middle.z += FORGE_AI_WAYPOINT_PICK_HEIGHT;
		if (distance3d(&camera->position, &object_get(object_index)->object.bounding_sphere_center) <
			distance3d(&camera->position, &middle))
		{
			return FALSE;
		}
	}

	return forge_ai_pick_up_waypoint(waypoint_index);
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
	case _forge_ai_row_team:
		_snprintf(label, label_size, "New characters are");
		_snprintf(value, value_size, "%s", forge_ai_globals.friendly ? "friends" : "enemies");
		break;
	case _forge_ai_row_attack:
		_snprintf(label, label_size, "Attack on sight");
		_snprintf(value, value_size, "%s", forge_ai_globals.attack_on_sight ? "on" : "off (just patrol)");
		break;
	case _forge_ai_row_spawn:
		_snprintf(label, label_size, "Add a character (crosshair)");
		_snprintf(value, value_size, "%d there", (int)forge_ai_living_units());
		break;
	case _forge_ai_row_add_waypoint:
		_snprintf(label, label_size, "Add a waypoint (crosshair)");
		_snprintf(value, value_size, "%d set", (int)forge_ai_globals.waypoint_count);
		break;
	case _forge_ai_row_move_waypoint:
		_snprintf(label, label_size, "Pick up a waypoint (crosshair, or A on it)");
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
	case _forge_ai_row_team:
		forge_ai_globals.friendly = (boolean)!forge_ai_globals.friendly;
		break;
	case _forge_ai_row_attack:
		forge_ai_globals.attack_on_sight = (boolean)!forge_ai_globals.attack_on_sight;
		forge_ai_update_command_list();
		/* the flags are read when a list starts */
		forge_ai_send_patrols();
		break;
	case _forge_ai_row_move_waypoint:
		if (direction == 0)
		{
			forge_ai_globals.show_waypoints = TRUE;
			if (!forge_ai_pick_up_waypoint(forge_ai_aimed_waypoint()))
				terminal_printf(global_real_argb_orange, "forge_ai: aim at a waypoint first");
			close_menu = TRUE;
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

/* ---------- layouts (port/linux/game/forge_layout.c) */

static void forge_ai_layout_save(
	struct halo_layout_writer *writer)
{
	short index;

	halo_layout_printf(writer, "settings %d %d %d %d", (int)forge_ai_globals.friendly,
		(int)forge_ai_globals.attack_on_sight, (int)forge_ai_globals.patrol_new, (int)forge_ai_globals.show_waypoints);
	for (index = 0; index < forge_ai_globals.waypoint_count; index++)
	{
		real_point3d const *point = &forge_ai_globals.waypoints[index];

		halo_layout_printf(writer, "waypoint %.4f %.4f %.4f", point->x, point->y, point->z);
	}
	for (index = 0; index < forge_ai_living_units(); index++)
	{
		struct object_datum *object = object_get(forge_ai_globals.units[index]);

		/* the dead lie where they fell: only the living are kept */
		if (object->object.body_vitality <= 0.f)
			continue;
		halo_layout_printf(writer, "character %d %.4f %.4f %.4f %.5f %.5f %s",
			(int)forge_ai_globals.unit_friendly[index],
			object->object.position.x, object->object.position.y, object->object.position.z,
			object->object.forward.i, object->object.forward.j,
			tag_get_name(forge_ai_globals.unit_variants[index]));
	}

	return;
}

static void forge_ai_layout_clear(
	void)
{
	short index;

	for (index = 0; index < forge_ai_living_units(); index++)
		object_delete(forge_ai_globals.units[index]);
	forge_ai_globals.unit_count = 0;
	forge_ai_globals.waypoint_count = 0;
	forge_ai_hold_globals.waypoint_index = NONE;
	forge_ai_update_command_list();

	return;
}

static void forge_ai_layout_load(
	char const *line)
{
	int values[4];
	float numbers[5];
	int name_offset = 0;

	if (sscanf(line, "settings %d %d %d %d", &values[0], &values[1], &values[2], &values[3]) == 4)
	{
		forge_ai_globals.friendly = values[0] != 0;
		forge_ai_globals.attack_on_sight = values[1] != 0;
		forge_ai_globals.patrol_new = values[2] != 0;
		forge_ai_globals.show_waypoints = values[3] != 0;
		forge_ai_update_command_list();
	}
	else if (sscanf(line, "waypoint %f %f %f", &numbers[0], &numbers[1], &numbers[2]) == 3 &&
		forge_ai_globals.waypoint_count < FORGE_AI_MAXIMUM_WAYPOINTS)
	{
		real_point3d point;
		long surface_index;

		set_real_point3d(&point, numbers[0], numbers[1], numbers[2]);
		if ((surface_index = forge_ai_ground_surface(&point)) != NONE)
		{
			forge_ai_globals.waypoints[forge_ai_globals.waypoint_count] = point;
			forge_ai_globals.waypoint_surfaces[forge_ai_globals.waypoint_count] = surface_index;
			forge_ai_globals.waypoint_count++;
			forge_ai_update_command_list();
		}
	}
	else if (sscanf(line, "character %d %f %f %f %f %f %n", &values[0], &numbers[0], &numbers[1], &numbers[2],
		&numbers[3], &numbers[4], &name_offset) >= 6 && name_offset > 0 &&
		/* a system link client has the host's characters */
		!forge_layout_loading_for_client())
	{
		long variant_index = tag_loaded(ACTOR_VARIANT_DEFINITION_TAG, line + name_offset);
		real_point3d point;
		real_vector3d forward;

		if (variant_index == NONE)
		{
			terminal_printf(global_real_argb_orange, "forge_ai: %s is not in this map, left out", line + name_offset);
			return;
		}
		set_real_point3d(&point, numbers[0], numbers[1], numbers[2]);
		set_real_vector3d(&forward, numbers[3], numbers[4], 0.f);
		forge_ai_create(variant_index, &point, &forward, values[0] != 0);
	}

	return;
}

/* ---------- the hooks */

/* once a frame: now and then, idle characters go back to their patrol */
static void forge_ai_update(
	void)
{
	unsigned long milliseconds = system_milliseconds();

	/* where the AI runs: a local game, the host of a system link game */
	if (!halo_mods_authoritative() || !forge_ai_globals.attack_on_sight ||
		(long)(milliseconds - forge_ai_globals.repatrol_milliseconds) < FORGE_AI_REPATROL_MILLISECONDS)
	{
		return;
	}
	forge_ai_globals.repatrol_milliseconds = milliseconds;
	forge_ai_repatrol();

	return;
}

/* a new map: nothing of the last is left, and this one gets its command list */
static void forge_ai_new_map(
	void)
{
	forge_ai_globals.waypoint_count = 0;
	forge_ai_globals.unit_count = 0;
	forge_ai_hold_globals.waypoint_index = NONE;
	forge_ai_globals.command_lists = NULL;
	forge_ai_globals.command_list_index = NONE;
	forge_ai_find_characters();
	if (halo_mods_authoritative())
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
	forge_ai_update,
	NULL,
	NULL,
	forge_ai_new_map,
	forge_ai_render_world,
	&forge_ai_menu,
	forge_ai_grab,
	NULL,
	forge_ai_layout_save,
	forge_ai_layout_clear,
	forge_ai_layout_load
};

HALO_MOD_REGISTER(forge_ai_mod)
