/*
FORGE_LAYOUT.C

Layouts of the dev tools (port/linux/game/forge.c): what Forge changed in a
map, saved to a file and loaded again, in Forge games and, when chosen to
play on the map, in every local game on it.

A layout holds:
	the objects the tools or mods made (spawned, pasted, brought back):
	        their tag, place, turn and scale
	the map's own objects that the tools moved or removed: found again by
	        their tag and the place the map puts them
	each mod's part (halo_mod.h, layout_save / layout_clear / layout_load):
	        zones, AI characters and waypoints, gravity, the sky

The map's own objects are noted when the map has placed them (the new map
hook, forge_layout_new_map), before a layout is loaded. Objects that the
game makes later (the weapons of a multiplayer map's spawn points, vehicles
that respawn) are not the map's own here: moving one saves a copy of it,
and the game still makes its own.

Files: u:\forge\<map>\layout_NN.txt (NN 01 to 16) in the save root, text,
one thing a line. The first line is the layout's name, "Layout NN" when it
is saved first; the launcher renames it (python -m tools.mod_launcher
layouts). u:\forge\<map>\play.txt holds the number of the layout that
plays on the map in every local game, if any.

The "Map" tab of the tools' menu: choose a layout (left, right), save it,
load it, play it in every game on the map, show it in the map list, reset
the map to its own look, delete it. Only local games change.

A layout shown in the map list ("listed 1" in its file) is a map of its
own: the multiplayer map list of local (splitscreen) games offers it after
the 13 maps of the game, with its base map's picture and the layout's name
(interface/ui_widget_event_handler_functions.c and
ui_widget_game_data_input_functions.c ask forge_custom_map_*; its name
comes through text/text_group.c's fallback strings). Choosing it plays the
base map with that layout loaded (forge_custom_map_select), in place of the
one that plays on the map.
*/

#include "cseries.h"
#include "cache/cache_files.h"
#include "game/game.h"
#include "game/players.h"
#include "interface/terminal.h"
#include "math/real_math.h"
#include "objects/objects.h"
#include "scenario/scenario.h"
#include "tag_files/tag_groups.h"
#include "units/units.h"

#include "network_distributed.h"

#include <direct.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the game's malloc and free are debugging macros; the lines copied with
_strdup are the C library's */
#undef malloc
#undef free

/* ---------- constants */

enum
{
	FORGE_LAYOUT_SLOT_COUNT = 16,
	FORGE_LAYOUT_MAXIMUM_MAP_OBJECTS = 1024,
	FORGE_LAYOUT_MAXIMUM_SPAWNED = 512,
	FORGE_LAYOUT_NAME_SIZE = 64,
	FORGE_LAYOUT_PATH_SIZE = 256,
	FORGE_LAYOUT_LINE_SIZE = 512,
	FORGE_LAYOUT_VERSION = 1,
	/* the multiplayer maps of the game, in the order of the map list and
	of its pictures */
	FORGE_LAYOUT_MULTIPLAYER_MAP_COUNT = 13,
	FORGE_LAYOUT_MAXIMUM_CUSTOM_MAPS = 64,
	/* the most of a layout file that is read, and sent to the clients of a
	system link game */
	FORGE_LAYOUT_MAXIMUM_TEXT = 65536
};

/* what is kept: units (not the players', who come later), items, scenery
and devices */
#define FORGE_LAYOUT_OBJECT_MASK (_object_mask_unit | _object_mask_item | _object_mask_scenery | _object_mask_device)

/* how near the place the map puts an object must be to be that object, and
how far one must be moved (or turned) to be saved as moved */
#define FORGE_LAYOUT_MATCH_DISTANCE 0.05f
#define FORGE_LAYOUT_MOVED_DISTANCE 0.005f
#define FORGE_LAYOUT_TURNED_DOT 0.9999f

enum
{
	_forge_layout_row_layout = 0,
	_forge_layout_row_save,
	_forge_layout_row_load,
	_forge_layout_row_play,
	_forge_layout_row_listed,
	_forge_layout_row_reset,
	_forge_layout_row_delete,
	NUMBER_OF_FORGE_LAYOUT_ROWS
};

/* ---------- structures */

struct forge_layout_map_object
{
	long object_index;
	long definition_index;
	real_point3d position;
	real_vector3d forward;
	real_vector3d up;
	/* found by the layout being loaded */
	boolean matched;
	/* the tools placed it or removed it (a vehicle settling, a weapon
	picked up are not a layout's) */
	boolean placed;
	boolean removed;
};

struct halo_layout_writer
{
	FILE *file;
};

/* a layout shown in the map list */
struct forge_custom_map
{
	short base_index;
	short slot;
	wchar_t title[FORGE_LAYOUT_NAME_SIZE];
};

/* ---------- globals */

static struct
{
	/* the map these are about, the last part of its scenario's name */
	char map_name[FORGE_LAYOUT_NAME_SIZE];

	short map_object_count;
	struct forge_layout_map_object map_objects[FORGE_LAYOUT_MAXIMUM_MAP_OBJECTS];

	short spawned_count;
	long spawned[FORGE_LAYOUT_MAXIMUM_SPAWNED];

	/* the Map tab: the chosen slot (0 a new layout), the one loaded or
	saved last, the one that plays on the map, and the slots' names ("" no
	layout there) */
	short chosen_slot;
	short current_slot;
	short play_slot;
	char slot_names[FORGE_LAYOUT_SLOT_COUNT + 1][FORGE_LAYOUT_NAME_SIZE];
	boolean slot_listed[FORGE_LAYOUT_SLOT_COUNT + 1];

	/* the layouts in the map list, and the one chosen there to play (its
	base map and slot; -1 none), kept until another map is chosen */
	short custom_map_count;
	struct forge_custom_map custom_maps[FORGE_LAYOUT_MAXIMUM_CUSTOM_MAPS];
	short selected_base_index;
	short selected_slot;

	/* the layout loaded in this map (sent to the clients of a system link
	game as they join), and whether one is being loaded on a client */
	unsigned long loaded_length;
	char loaded_text[FORGE_LAYOUT_MAXIMUM_TEXT];
	boolean loading_for_client;

	/* a client's layout from the host, as its parts come, and a checksum
	of the one applied (the host may send it again) */
	unsigned long received_length;
	char received_text[FORGE_LAYOUT_MAXIMUM_TEXT];
	unsigned long applied_checksum;
} forge_layout_globals = { "", 0, { { 0 } }, 0, { 0 }, 0, 0, 0, { { 0 } }, { 0 }, 0, { { 0 } }, -1, 0 };

/* the multiplayer maps (interface/ui_widget_event_handler_functions.c) */
static char const *const forge_layout_multiplayer_maps[FORGE_LAYOUT_MULTIPLAYER_MAP_COUNT] =
{
	"beavercreek",
	"sidewinder",
	"damnation",
	"ratrace",
	"prisoner",
	"hangemhigh",
	"chillout",
	"carousel",
	"boardingaction",
	"bloodgulch",
	"wizard",
	"putput",
	"longest"
};

/* ---------- private code */

static boolean forge_layout_local_game(
	void)
{
	return game_connection() == _game_connection_local && forge_layout_globals.map_name[0] != 0;
}

/* a machine whose game state is the game's: a local game, or the host of a
system link game (network_distributed.c, whose clients copy its objects) */
static boolean forge_layout_authoritative(
	void)
{
	return (game_connection() == _game_connection_local || game_connection() == _game_connection_network_server) &&
		forge_layout_globals.map_name[0] != 0;
}

static void forge_layout_directory(
	char *path,
	unsigned long size)
{
	_snprintf(path, size, "u:\\forge\\%s", forge_layout_globals.map_name);
	path[size - 1] = 0;

	return;
}

static void forge_layout_map_slot_path(
	char const *map_name,
	short slot,
	char *path,
	unsigned long size)
{
	_snprintf(path, size, "u:\\forge\\%s\\layout_%02d.txt", map_name, slot);
	path[size - 1] = 0;

	return;
}

static void forge_layout_slot_path(
	short slot,
	char *path,
	unsigned long size)
{
	forge_layout_map_slot_path(forge_layout_globals.map_name, slot, path, size);

	return;
}

/* a layout file's name ("Layout N" when it has none) and whether it is in
the map list; FALSE when there is no layout there */
static boolean forge_layout_read_header(
	char const *path,
	short slot,
	char *name,
	unsigned long name_size,
	boolean *listed)
{
	char line[FORGE_LAYOUT_LINE_SIZE];
	short line_index;
	FILE *file = fopen(path, "r");

	if (!file)
		return FALSE;
	_snprintf(name, name_size, "Layout %d", slot);
	*listed = FALSE;
	for (line_index = 0; line_index < 8 && fgets(line, sizeof(line), file); line_index++)
	{
		line[strcspn(line, "\r\n")] = 0;
		if (strncmp(line, "name ", 5) == 0 && line[5])
			_snprintf(name, name_size, "%s", line + 5);
		else if (strcmp(line, "listed 1") == 0)
			*listed = TRUE;
		else if (line[0] == '[')
			break;
	}
	name[name_size - 1] = 0;
	fclose(file);

	return TRUE;
}

static void forge_layout_play_path(
	char *path,
	unsigned long size)
{
	_snprintf(path, size, "u:\\forge\\%s\\play.txt", forge_layout_globals.map_name);
	path[size - 1] = 0;

	return;
}

/* the line without its line break */
static void forge_layout_chomp(
	char *line)
{
	line[strcspn(line, "\r\n")] = 0;

	return;
}

/* the names of the saved layouts, and the one that plays */
static void forge_layout_read_slots(
	void)
{
	char path[FORGE_LAYOUT_PATH_SIZE];
	char line[FORGE_LAYOUT_LINE_SIZE];
	short slot;
	FILE *file;

	for (slot = 1; slot <= FORGE_LAYOUT_SLOT_COUNT; slot++)
	{
		forge_layout_slot_path(slot, path, sizeof(path));
		if (!forge_layout_read_header(path, slot, forge_layout_globals.slot_names[slot], FORGE_LAYOUT_NAME_SIZE,
			&forge_layout_globals.slot_listed[slot]))
		{
			forge_layout_globals.slot_names[slot][0] = 0;
			forge_layout_globals.slot_listed[slot] = FALSE;
		}
	}

	forge_layout_globals.play_slot = 0;
	forge_layout_play_path(path, sizeof(path));
	if ((file = fopen(path, "r")) != NULL)
	{
		int play_slot;

		if (fgets(line, sizeof(line), file) && sscanf(line, "%d", &play_slot) == 1 &&
			play_slot >= 1 && play_slot <= FORGE_LAYOUT_SLOT_COUNT &&
			forge_layout_globals.slot_names[play_slot][0])
		{
			forge_layout_globals.play_slot = (short)play_slot;
		}
		fclose(file);
	}

	return;
}

static void forge_layout_write_play_slot(
	short slot)
{
	char path[FORGE_LAYOUT_PATH_SIZE];
	FILE *file;

	forge_layout_play_path(path, sizeof(path));
	if (slot == 0)
	{
		remove(path);
	}
	else if ((file = fopen(path, "w")) != NULL)
	{
		fprintf(file, "%d\n", slot);
		fclose(file);
	}
	forge_layout_globals.play_slot = slot;

	return;
}

/* the object's tag group as four letters, and back */
static void forge_layout_group_name(
	long definition_index,
	char name[5])
{
	unsigned long group = tag_get_group_tag(definition_index);

	name[0] = (char)(group >> 24);
	name[1] = (char)(group >> 16);
	name[2] = (char)(group >> 8);
	name[3] = (char)group;
	name[4] = 0;

	return;
}

static long forge_layout_group_tag(
	char const *name)
{
	return (long)(((unsigned long)(unsigned char)name[0] << 24) | ((unsigned long)(unsigned char)name[1] << 16) |
		((unsigned long)(unsigned char)name[2] << 8) | (unsigned long)(unsigned char)name[3]);
}

static struct forge_layout_map_object *forge_layout_map_object(
	long object_index)
{
	short index;

	for (index = 0; index < forge_layout_globals.map_object_count; index++)
	{
		if (forge_layout_globals.map_objects[index].object_index == object_index)
			return &forge_layout_globals.map_objects[index];
	}

	return NULL;
}

/* the map's own objects, as it placed them */
static void forge_layout_note_map_objects(
	void)
{
	struct object_iterator iterator;
	struct object_datum *object;

	forge_layout_globals.map_object_count = 0;
	object_iterator_new(&iterator, FORGE_LAYOUT_OBJECT_MASK, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL &&
		forge_layout_globals.map_object_count < FORGE_LAYOUT_MAXIMUM_MAP_OBJECTS)
	{
		struct forge_layout_map_object *entry;

		/* what something carries goes with it */
		if (object->object.parent_object_index != NONE)
			continue;
		entry = &forge_layout_globals.map_objects[forge_layout_globals.map_object_count++];
		entry->object_index = iterator.index;
		entry->definition_index = object->definition_index;
		entry->position = object->object.position;
		entry->forward = object->object.forward;
		entry->up = object->object.up;
		entry->matched = FALSE;
		entry->placed = FALSE;
		entry->removed = FALSE;
	}

	return;
}

static long forge_layout_new_object(
	long definition_index,
	real_point3d const *position,
	real_vector3d const *forward,
	real_vector3d const *up,
	real scale)
{
	struct object_placement_data data;
	long object_index;

	object_placement_data_new(&data, definition_index, NONE);
	data.position = *position;
	data.forward = *forward;
	data.up = *up;
	object_index = object_new(&data);
	if (object_index != NONE)
	{
		struct object_datum *object = object_get(object_index);

		if (scale > 0.f && scale != 1.f)
			objects_scripting_set_scale(object_index, scale, 0);
		object->object.translational_velocity = *global_zero_vector3d;
		object->object.angular_velocity = *global_zero_vector3d;
	}

	return object_index;
}

static void forge_layout_place(
	long object_index,
	real_point3d const *position,
	real_vector3d const *forward,
	real_vector3d const *up)
{
	struct object_datum *object = object_get(object_index);

	/* an object not in the world (garbage, another structure) stays */
	if (!TEST_FLAG(object->object.flags, _object_connected_to_map_bit))
		return;
	object_set_position(object_index, (real_point3d *)position, (real_vector3d *)forward, (real_vector3d *)up);
	object->object.translational_velocity = *global_zero_vector3d;
	object->object.angular_velocity = *global_zero_vector3d;

	return;
}

/* the map as it has it: the mods' parts, the spawned objects gone, the
map's own objects back where it puts them */
static void forge_layout_reset(
	void)
{
	short index;

	for (index = 0; index < halo_mods_count(); index++)
	{
		struct halo_mod const *mod = halo_mods_get(index);

		if (mod->layout_clear)
			mod->layout_clear();
	}

	for (index = 0; index < forge_layout_globals.spawned_count; index++)
	{
		if (object_try_and_get(forge_layout_globals.spawned[index]))
			object_delete(forge_layout_globals.spawned[index]);
	}
	forge_layout_globals.spawned_count = 0;

	for (index = 0; index < forge_layout_globals.map_object_count; index++)
	{
		struct forge_layout_map_object *entry = &forge_layout_globals.map_objects[index];
		struct object_datum *object = object_try_and_get(entry->object_index);

		/* only what the tools changed: the rest is the game's (a vehicle
		that settled, a weapon picked up) */
		if (entry->placed && object && object->definition_index == entry->definition_index)
		{
			if (object->object.parent_object_index == NONE)
				forge_layout_place(entry->object_index, &entry->position, &entry->forward, &entry->up);
		}
		else if (entry->removed && (!object || object->definition_index != entry->definition_index))
		{
			entry->object_index = forge_layout_new_object(
				entry->definition_index, &entry->position, &entry->forward, &entry->up, 0.f);
		}
		entry->matched = FALSE;
		entry->placed = FALSE;
		entry->removed = FALSE;
	}

	forge_layout_globals.current_slot = 0;

	return;
}

/* ---------- saving */

void halo_layout_printf(
	struct halo_layout_writer *writer,
	char const *format,
	...)
{
	va_list arguments;

	va_start(arguments, format);
	vfprintf(writer->file, format, arguments);
	va_end(arguments);
	fputc('\n', writer->file);

	return;
}

static void forge_layout_write_orientation(
	FILE *file,
	real_point3d const *position,
	real_vector3d const *forward,
	real_vector3d const *up)
{
	fprintf(file, " %.4f %.4f %.4f %.5f %.5f %.5f %.5f %.5f %.5f",
		position->x, position->y, position->z,
		forward->i, forward->j, forward->k,
		up->i, up->j, up->k);

	return;
}

static boolean forge_layout_save(
	short slot)
{
	char path[FORGE_LAYOUT_PATH_SIZE];
	char group[5];
	struct halo_layout_writer writer;
	char const *imports = getenv("HALO_IMPORT");
	short index;

	_mkdir("u:\\forge");
	forge_layout_directory(path, sizeof(path));
	_mkdir(path);
	forge_layout_slot_path(slot, path, sizeof(path));
	if ((writer.file = fopen(path, "w")) == NULL)
	{
		terminal_printf(global_real_argb_orange, "forge: could not write %s", path);
		return FALSE;
	}

	if (!forge_layout_globals.slot_names[slot][0])
		_snprintf(forge_layout_globals.slot_names[slot], FORGE_LAYOUT_NAME_SIZE, "Layout %d", slot);
	fprintf(writer.file, "name %s\n", forge_layout_globals.slot_names[slot]);
	fprintf(writer.file, "# a Forge layout (port/linux/game/forge_layout.c)\n");
	fprintf(writer.file, "version %d\n", FORGE_LAYOUT_VERSION);
	fprintf(writer.file, "map %s\n", tag_get_name(global_scenario_index));
	if (forge_layout_globals.slot_listed[slot])
		fprintf(writer.file, "listed 1\n");
	/* the tags it was made with from other maps (the launcher's list) */
	if (imports && *imports)
		fprintf(writer.file, "import %s\n", imports);

	fprintf(writer.file, "[forge]\n");
	for (index = 0; index < forge_layout_globals.spawned_count; index++)
	{
		struct object_datum *object = object_try_and_get(forge_layout_globals.spawned[index]);

		if (!object || object->object.parent_object_index != NONE)
			continue;
		forge_layout_group_name(object->definition_index, group);
		fprintf(writer.file, "spawn %s", group);
		forge_layout_write_orientation(writer.file, &object->object.position, &object->object.forward,
			&object->object.up);
		fprintf(writer.file, " %.4f %s\n", object->object.scale > 0.f ? object->object.scale : 1.f,
			tag_get_name(object->definition_index));
	}
	for (index = 0; index < forge_layout_globals.map_object_count; index++)
	{
		struct forge_layout_map_object const *entry = &forge_layout_globals.map_objects[index];
		struct object_datum *object = object_try_and_get(entry->object_index);

		forge_layout_group_name(entry->definition_index, group);
		if (entry->removed && (!object || object->definition_index != entry->definition_index))
		{
			fprintf(writer.file, "remove %s %.4f %.4f %.4f %s\n", group,
				entry->position.x, entry->position.y, entry->position.z, tag_get_name(entry->definition_index));
		}
		else if (entry->placed && object && object->definition_index == entry->definition_index &&
			object->object.parent_object_index == NONE &&
			(distance3d(&object->object.position, &entry->position) > FORGE_LAYOUT_MOVED_DISTANCE ||
				dot_product3d(&object->object.forward, &entry->forward) < FORGE_LAYOUT_TURNED_DOT ||
				dot_product3d(&object->object.up, &entry->up) < FORGE_LAYOUT_TURNED_DOT))
		{
			fprintf(writer.file, "move %s %.4f %.4f %.4f", group,
				entry->position.x, entry->position.y, entry->position.z);
			forge_layout_write_orientation(writer.file, &object->object.position, &object->object.forward,
				&object->object.up);
			fprintf(writer.file, " %s\n", tag_get_name(entry->definition_index));
		}
	}

	for (index = 0; index < halo_mods_count(); index++)
	{
		struct halo_mod const *mod = halo_mods_get(index);

		if (mod->layout_save)
		{
			fprintf(writer.file, "[%s]\n", mod->name);
			mod->layout_save(&writer);
		}
	}
	fclose(writer.file);

	forge_layout_globals.current_slot = slot;
	terminal_printf(global_real_argb_green, "forge: saved %s", forge_layout_globals.slot_names[slot]);

	return TRUE;
}

/* ---------- loading */

/* the map's own object of the tag at the place the map puts it */
static struct forge_layout_map_object *forge_layout_find_map_object(
	long definition_index,
	real_point3d const *position)
{
	short index;

	for (index = 0; index < forge_layout_globals.map_object_count; index++)
	{
		struct forge_layout_map_object *entry = &forge_layout_globals.map_objects[index];

		if (!entry->matched && entry->definition_index == definition_index &&
			distance3d(&entry->position, position) < FORGE_LAYOUT_MATCH_DISTANCE)
		{
			entry->matched = TRUE;
			return entry;
		}
	}

	return NULL;
}

/* the tag of a line: its group and the name at the end of the line */
static long forge_layout_line_tag(
	char const *group,
	char const *name)
{
	long definition_index = tag_loaded(forge_layout_group_tag(group), name);

	if (definition_index == NONE)
		terminal_printf(global_real_argb_orange, "forge: %s is not in this map, left out", name);

	return definition_index;
}

/* whether the host's objects of the definition reach its clients by
themselves (network_objects.c: units, vehicles, weapons, equipment) */
static boolean forge_layout_host_brings(
	long definition_index)
{
	return (FLAG(object_definition_get(definition_index)->object.type) &
		(_object_mask_unit | _object_mask_item)) != 0;
}

static void forge_layout_load_forge_line(
	char const *line,
	boolean for_client)
{
	char group[5];
	float values[13];
	int name_offset = 0;
	long definition_index;

	if (sscanf(line, "spawn %4s %f %f %f %f %f %f %f %f %f %f %n", group,
		&values[0], &values[1], &values[2], &values[3], &values[4], &values[5],
		&values[6], &values[7], &values[8], &values[9], &name_offset) >= 11 && name_offset > 0)
	{
		real_point3d position;
		real_vector3d forward;
		real_vector3d up;
		long object_index;

		if ((definition_index = forge_layout_line_tag(group, line + name_offset)) == NONE ||
			(for_client && forge_layout_host_brings(definition_index)))
		{
			return;
		}
		set_real_point3d(&position, values[0], values[1], values[2]);
		set_real_vector3d(&forward, values[3], values[4], values[5]);
		set_real_vector3d(&up, values[6], values[7], values[8]);
		object_index = forge_layout_new_object(definition_index, &position, &forward, &up, values[9]);
		if (object_index != NONE)
			forge_layout_note_spawned(object_index);
	}
	else if (sscanf(line, "move %4s %f %f %f %f %f %f %f %f %f %f %f %f %n", group,
		&values[0], &values[1], &values[2], &values[3], &values[4], &values[5],
		&values[6], &values[7], &values[8], &values[9], &values[10], &values[11], &name_offset) >= 13 &&
		name_offset > 0)
	{
		struct forge_layout_map_object *entry;
		real_point3d original;
		real_point3d position;
		real_vector3d forward;
		real_vector3d up;

		if ((definition_index = forge_layout_line_tag(group, line + name_offset)) == NONE ||
			(for_client && forge_layout_host_brings(definition_index)))
		{
			return;
		}
		set_real_point3d(&original, values[0], values[1], values[2]);
		set_real_point3d(&position, values[3], values[4], values[5]);
		set_real_vector3d(&forward, values[6], values[7], values[8]);
		set_real_vector3d(&up, values[9], values[10], values[11]);
		entry = forge_layout_find_map_object(definition_index, &original);
		if (entry && object_try_and_get(entry->object_index))
		{
			forge_layout_place(entry->object_index, &position, &forward, &up);
			entry->placed = TRUE;
		}
	}
	else if (sscanf(line, "remove %4s %f %f %f %n", group, &values[0], &values[1], &values[2], &name_offset) >= 4 &&
		name_offset > 0)
	{
		struct forge_layout_map_object *entry;
		real_point3d original;

		if ((definition_index = forge_layout_line_tag(group, line + name_offset)) == NONE ||
			(for_client && forge_layout_host_brings(definition_index)))
		{
			return;
		}
		set_real_point3d(&original, values[0], values[1], values[2]);
		entry = forge_layout_find_map_object(definition_index, &original);
		if (entry && object_try_and_get(entry->object_index))
		{
			object_delete(entry->object_index);
			entry->removed = TRUE;
		}
	}

	return;
}

/* the lines of a layout: the tools' part, then each mod's; on a system link
client (for_client), only what the host's objects do not bring: the
tools' scenery and devices, and the mods' parts (the mods leave out what
is the host's, forge_layout_loading_for_client) */
static void forge_layout_apply(
	char const *text,
	boolean for_client)
{
	struct halo_mod const *section_mod = NULL;
	boolean forge_section = FALSE;
	char const *next = text;

	forge_layout_globals.loading_for_client = for_client;
	forge_layout_reset();
	while (*next)
	{
		char line[FORGE_LAYOUT_LINE_SIZE];
		unsigned long length = (unsigned long)strcspn(next, "\n");

		_snprintf(line, sizeof(line), "%.*s", (int)MIN(length, sizeof(line) - 1), next);
		line[sizeof(line) - 1] = 0;
		next += length;
		if (*next == '\n')
			next++;
		forge_layout_chomp(line);

		if (line[0] == '[')
		{
			char *end = strchr(line, ']');
			short index;

			section_mod = NULL;
			forge_section = FALSE;
			if (end)
				*end = 0;
			if (strcmp(line + 1, "forge") == 0)
				forge_section = TRUE;
			for (index = 0; index < halo_mods_count(); index++)
			{
				if (strcmp(halo_mods_get(index)->name, line + 1) == 0)
					section_mod = halo_mods_get(index);
			}
		}
		else if (line[0] == 0 || line[0] == '#')
		{
			/* nothing */
		}
		else if (forge_section)
		{
			forge_layout_load_forge_line(line, for_client);
		}
		else if (section_mod && section_mod->layout_load)
		{
			section_mod->layout_load(line);
		}
	}
	forge_layout_globals.loading_for_client = FALSE;

	return;
}

static boolean forge_layout_load(
	short slot)
{
	char path[FORGE_LAYOUT_PATH_SIZE];
	unsigned long length;
	FILE *file;

	forge_layout_slot_path(slot, path, sizeof(path));
	if ((file = fopen(path, "rb")) == NULL)
	{
		terminal_printf(global_real_argb_orange, "forge: could not read %s", path);
		return FALSE;
	}
	/* kept whole, for the clients of a system link game */
	length = (unsigned long)fread(forge_layout_globals.loaded_text, 1, FORGE_LAYOUT_MAXIMUM_TEXT - 1, file);
	fclose(file);
	forge_layout_globals.loaded_text[length] = 0;
	forge_layout_globals.loaded_length = length;

	forge_layout_apply(forge_layout_globals.loaded_text, FALSE);

	forge_layout_globals.current_slot = slot;
	forge_layout_globals.chosen_slot = slot;
	terminal_printf(global_real_argb_green, "forge: loaded %s", forge_layout_globals.slot_names[slot]);

	return TRUE;
}

/* ---------- the map list */

/* puts the layout in the map list or takes it out: its file's "listed 1"
line, after the header's other lines */
static boolean forge_layout_set_listed(
	short slot,
	boolean listed)
{
	char path[FORGE_LAYOUT_PATH_SIZE];
	char *lines[4096];
	short line_count = 0;
	short index;
	char line[FORGE_LAYOUT_LINE_SIZE];
	boolean written = FALSE;
	FILE *file;

	forge_layout_slot_path(slot, path, sizeof(path));
	if ((file = fopen(path, "r")) == NULL)
		return FALSE;
	while (line_count < (short)NUMBEROF(lines) && fgets(line, sizeof(line), file))
	{
		forge_layout_chomp(line);
		if (strcmp(line, "listed 1") == 0)
			continue;
		lines[line_count] = _strdup(line);
		if (lines[line_count])
			line_count++;
	}
	fclose(file);

	if ((file = fopen(path, "w")) != NULL)
	{
		boolean placed = !listed;

		for (index = 0; index < line_count; index++)
		{
			/* before the first part, with the header */
			if (!placed && lines[index][0] == '[')
			{
				fprintf(file, "listed 1\n");
				placed = TRUE;
			}
			fprintf(file, "%s\n", lines[index]);
		}
		if (!placed)
			fprintf(file, "listed 1\n");
		fclose(file);
		written = TRUE;
	}
	for (index = 0; index < line_count; index++)
		free(lines[index]);
	if (written)
		forge_layout_globals.slot_listed[slot] = listed;

	return written;
}

/* ---------- the Map tab */

static short forge_layout_first_free_slot(
	void)
{
	short slot;

	for (slot = 1; slot <= FORGE_LAYOUT_SLOT_COUNT; slot++)
	{
		if (!forge_layout_globals.slot_names[slot][0])
			return slot;
	}

	return 0;
}

static char const *forge_layout_chosen_name(
	void)
{
	return forge_layout_globals.chosen_slot
		? forge_layout_globals.slot_names[forge_layout_globals.chosen_slot]
		: "(a new layout)";
}

static short forge_layout_menu_row_count(
	void)
{
	return NUMBER_OF_FORGE_LAYOUT_ROWS;
}

static void forge_layout_menu_row_text(
	short row,
	char *label,
	unsigned long label_size,
	char *value,
	unsigned long value_size)
{
	short chosen = forge_layout_globals.chosen_slot;

	switch (row)
	{
	case _forge_layout_row_layout:
		_snprintf(label, label_size, "Layout");
		_snprintf(value, value_size, "%s", forge_layout_chosen_name());
		break;
	case _forge_layout_row_save:
		_snprintf(label, label_size, "Save");
		_snprintf(value, value_size, "%s", chosen ? "over it" : "as a new layout");
		break;
	case _forge_layout_row_load:
		_snprintf(label, label_size, "Load it");
		_snprintf(value, value_size, "%s", chosen && chosen == forge_layout_globals.current_slot ? "loaded" : "");
		break;
	case _forge_layout_row_play:
		_snprintf(label, label_size, "Play on this map");
		_snprintf(value, value_size, "%s", chosen && chosen == forge_layout_globals.play_slot ? "on" : "off");
		break;
	case _forge_layout_row_listed:
		_snprintf(label, label_size, "Show in the map list");
		_snprintf(value, value_size, "%s", chosen && forge_layout_globals.slot_listed[chosen] ? "on" : "off");
		break;
	case _forge_layout_row_reset:
		_snprintf(label, label_size, "Reset the map");
		break;
	case _forge_layout_row_delete:
		_snprintf(label, label_size, "Delete it");
		break;
	}

	return;
}

static int forge_layout_menu_row_change(
	short row,
	int direction)
{
	short chosen = forge_layout_globals.chosen_slot;

	if (!forge_layout_local_game())
	{
		if (direction == 0 || row != _forge_layout_row_layout)
			terminal_printf(global_real_argb_orange, "forge: layouts only in local games");
		return FALSE;
	}

	switch (row)
	{
	case _forge_layout_row_layout:
		if (direction != 0)
		{
			/* the saved layouts, then a new one */
			short step;

			for (step = 0; step <= FORGE_LAYOUT_SLOT_COUNT; step++)
			{
				chosen = (short)((chosen + FORGE_LAYOUT_SLOT_COUNT + 1 + direction) % (FORGE_LAYOUT_SLOT_COUNT + 1));
				if (chosen == 0 || forge_layout_globals.slot_names[chosen][0])
					break;
			}
			forge_layout_globals.chosen_slot = chosen;
		}
		break;
	case _forge_layout_row_save:
		if (direction == 0)
		{
			if (chosen == 0 && (chosen = forge_layout_first_free_slot()) == 0)
			{
				terminal_printf(global_real_argb_orange, "forge: all %d layouts of this map are used",
					FORGE_LAYOUT_SLOT_COUNT);
			}
			else if (forge_layout_save(chosen))
			{
				forge_layout_globals.chosen_slot = chosen;
			}
		}
		break;
	case _forge_layout_row_load:
		if (direction == 0)
		{
			if (chosen == 0)
				terminal_printf(global_real_argb_orange, "forge: choose a saved layout first");
			else
				forge_layout_load(chosen);
		}
		break;
	case _forge_layout_row_play:
		if (chosen == 0)
		{
			terminal_printf(global_real_argb_orange, "forge: save the layout first");
		}
		else
		{
			forge_layout_write_play_slot(chosen == forge_layout_globals.play_slot ? 0 : chosen);
			terminal_printf(global_real_argb_green, forge_layout_globals.play_slot
				? "forge: %s now plays in every game on this map"
				: "forge: %s no longer plays on this map", forge_layout_globals.slot_names[chosen]);
		}
		break;
	case _forge_layout_row_listed:
		if (chosen == 0)
		{
			terminal_printf(global_real_argb_orange, "forge: save the layout first");
		}
		else if (forge_layout_set_listed(chosen, !forge_layout_globals.slot_listed[chosen]))
		{
			terminal_printf(global_real_argb_green, forge_layout_globals.slot_listed[chosen]
				? "forge: %s is in the multiplayer map list (local games)"
				: "forge: %s is out of the map list", forge_layout_globals.slot_names[chosen]);
		}
		break;
	case _forge_layout_row_reset:
		if (direction == 0)
		{
			forge_layout_reset();
			terminal_printf(global_real_argb_green, "forge: the map is back to its own look");
		}
		break;
	case _forge_layout_row_delete:
		if (direction == 0 && chosen != 0)
		{
			char path[FORGE_LAYOUT_PATH_SIZE];

			forge_layout_slot_path(chosen, path, sizeof(path));
			remove(path);
			terminal_printf(global_real_argb_green, "forge: deleted %s", forge_layout_globals.slot_names[chosen]);
			if (forge_layout_globals.play_slot == chosen)
				forge_layout_write_play_slot(0);
			if (forge_layout_globals.current_slot == chosen)
				forge_layout_globals.current_slot = 0;
			forge_layout_globals.chosen_slot = 0;
			forge_layout_read_slots();
		}
		break;
	}

	return FALSE;
}

static void forge_layout_menu_opened(
	void)
{
	forge_layout_read_slots();
	if (forge_layout_globals.chosen_slot && !forge_layout_globals.slot_names[forge_layout_globals.chosen_slot][0])
		forge_layout_globals.chosen_slot = 0;

	return;
}

static struct halo_mod_menu const forge_layout_menu =
{
	"Map",
	forge_layout_menu_row_count,
	forge_layout_menu_row_text,
	forge_layout_menu_row_change,
	forge_layout_menu_opened
};

static void forge_layout_debug_update(
	void);

static struct halo_mod const forge_layout_mod =
{
	"forge_layout",
	forge_layout_debug_update,
	NULL,
	NULL,
	NULL,
	NULL,
	&forge_layout_menu
};

HALO_MOD_REGISTER(forge_layout_mod)

/* debug.forge_layout_save (port_config.c): a few seconds into a local game,
saves the layout to that slot, once, and logs where local player 0 is, for
automated tests; 0 never */
long config_integer(char const *name);
void platform_log(char const *format, ...);

static void forge_layout_debug_update(
	void)
{
	static long slot = -1;

	if (slot < 0)
		slot = config_integer("debug.forge_layout_save");
	if (slot >= 1 && slot <= FORGE_LAYOUT_SLOT_COUNT && game_time_get() >= 150 && forge_layout_local_game())
	{
		long player_index = local_player_get_player_index(0);
		struct unit_datum *unit = player_index != NONE ? unit_try_and_get(player_get(player_index)->unit_index) : NULL;

		forge_layout_read_slots();
		forge_layout_save((short)slot);
		if (unit)
		{
			platform_log("forge layout: saved slot %ld, player at %.3f %.3f %.3f facing %.3f %.3f", slot,
				unit->object.position.x, unit->object.position.y, unit->object.position.z,
				unit->object.forward.i, unit->object.forward.j);
		}
		slot = 0;
	}

	return;
}

/* ---------- public code */

void forge_layout_note_spawned(
	long object_index)
{
	short read;
	short write = 0;

	if (object_index == NONE || forge_layout_map_object(object_index))
		return;

	/* the ones still there, packed, without this one */
	for (read = 0; read < forge_layout_globals.spawned_count; read++)
	{
		long index = forge_layout_globals.spawned[read];

		if (index != object_index && object_try_and_get(index))
			forge_layout_globals.spawned[write++] = index;
	}
	forge_layout_globals.spawned_count = write;
	if (forge_layout_globals.spawned_count < FORGE_LAYOUT_MAXIMUM_SPAWNED)
		forge_layout_globals.spawned[forge_layout_globals.spawned_count++] = object_index;

	return;
}

void forge_layout_note_placed(
	long object_index)
{
	struct forge_layout_map_object *entry = forge_layout_map_object(object_index);

	if (entry)
		entry->placed = TRUE;
	else
		forge_layout_note_spawned(object_index);

	return;
}

void forge_layout_note_removed(
	long object_index)
{
	struct forge_layout_map_object *entry = forge_layout_map_object(object_index);

	if (entry)
		entry->removed = TRUE;

	return;
}

short forge_custom_maps_refresh(
	void)
{
	short base_index;
	short slot;

	forge_layout_globals.custom_map_count = 0;
	for (base_index = 0; base_index < FORGE_LAYOUT_MULTIPLAYER_MAP_COUNT; base_index++)
	{
		for (slot = 1; slot <= FORGE_LAYOUT_SLOT_COUNT; slot++)
		{
			char path[FORGE_LAYOUT_PATH_SIZE];
			char name[FORGE_LAYOUT_NAME_SIZE];
			boolean listed;

			forge_layout_map_slot_path(forge_layout_multiplayer_maps[base_index], slot, path, sizeof(path));
			if (forge_layout_globals.custom_map_count < FORGE_LAYOUT_MAXIMUM_CUSTOM_MAPS &&
				forge_layout_read_header(path, slot, name, sizeof(name), &listed) && listed)
			{
				struct forge_custom_map *map = &forge_layout_globals.custom_maps[forge_layout_globals.custom_map_count++];
				short character;

				map->base_index = base_index;
				map->slot = slot;
				for (character = 0; name[character] && character < FORGE_LAYOUT_NAME_SIZE - 1; character++)
					map->title[character] = (wchar_t)(unsigned char)name[character];
				map->title[character] = 0;
			}
		}
	}

	return forge_layout_globals.custom_map_count;
}

static struct forge_custom_map const *forge_custom_map_get(
	short index)
{
	return index >= 0 && index < forge_layout_globals.custom_map_count
		? &forge_layout_globals.custom_maps[index]
		: NULL;
}

short forge_custom_map_base_index(
	short index)
{
	struct forge_custom_map const *map = forge_custom_map_get(index);

	return map ? map->base_index : 0;
}

char const *forge_custom_map_base_name(
	short index)
{
	return forge_layout_multiplayer_maps[forge_custom_map_base_index(index)];
}

wchar_t const *forge_custom_map_title(
	short index)
{
	struct forge_custom_map const *map = forge_custom_map_get(index);

	return map ? map->title : NULL;
}

void forge_custom_map_select(
	short index)
{
	struct forge_custom_map const *map = forge_custom_map_get(index);

	forge_layout_globals.selected_base_index = map ? map->base_index : -1;
	forge_layout_globals.selected_slot = map ? map->slot : 0;

	return;
}

short forge_custom_map_selected(
	void)
{
	short index;

	for (index = 0; index < forge_layout_globals.custom_map_count; index++)
	{
		struct forge_custom_map const *map = &forge_layout_globals.custom_maps[index];

		if (map->base_index == forge_layout_globals.selected_base_index &&
			map->slot == forge_layout_globals.selected_slot)
		{
			return index;
		}
	}

	return NONE;
}

int forge_custom_map_select_by_name(
	char const *map_name,
	short slot)
{
	short base_index;

	forge_layout_globals.selected_base_index = -1;
	forge_layout_globals.selected_slot = 0;
	for (base_index = 0; base_index < FORGE_LAYOUT_MULTIPLAYER_MAP_COUNT; base_index++)
	{
		if (_stricmp(map_name, forge_layout_multiplayer_maps[base_index]) == 0 &&
			slot >= 1 && slot <= FORGE_LAYOUT_SLOT_COUNT)
		{
			forge_layout_globals.selected_base_index = base_index;
			forge_layout_globals.selected_slot = slot;
			return TRUE;
		}
	}

	return FALSE;
}

char const *forge_custom_map_base_title(
	short index)
{
	static char const *const titles[FORGE_LAYOUT_MULTIPLAYER_MAP_COUNT] =
	{
		"Battle Creek",
		"Sidewinder",
		"Damnation",
		"Rat Race",
		"Prisoner",
		"Hang 'Em High",
		"Chill Out",
		"Derelict",
		"Boarding Action",
		"Blood Gulch",
		"Wizard",
		"Chiron TL-34",
		"Longest"
	};

	return titles[forge_custom_map_base_index(index)];
}

/* the slot of the layout chosen in the map list for this map, or 0 */
static short forge_layout_selected_slot(
	void)
{
	short base_index = forge_layout_globals.selected_base_index;

	return base_index >= 0 && base_index < FORGE_LAYOUT_MULTIPLAYER_MAP_COUNT &&
		_stricmp(forge_layout_multiplayer_maps[base_index], forge_layout_globals.map_name) == 0
		? forge_layout_globals.selected_slot
		: 0;
}

/* ---------- system link: the host's layout to its clients */

/* a part of the layout's text (_distributed_message_forge_layout) */
enum
{
	FORGE_LAYOUT_MESSAGE_TEXT_SIZE = 3072
};

struct forge_layout_message
{
	struct distributed_message_header header;
	unsigned long total_length;
	unsigned long offset;
	word length;
	char text[FORGE_LAYOUT_MESSAGE_TEXT_SIZE];
};

int forge_layout_loading_for_client(
	void)
{
	return forge_layout_globals.loading_for_client;
}

void forge_layout_send_to_client(
	long machine_index)
{
	struct forge_layout_message message;
	unsigned long offset = 0;

	if (game_connection() != _game_connection_network_server || forge_layout_globals.loaded_length == 0)
		return;
	while (offset < forge_layout_globals.loaded_length)
	{
		unsigned long length = MIN(forge_layout_globals.loaded_length - offset, FORGE_LAYOUT_MESSAGE_TEXT_SIZE);

		message.total_length = forge_layout_globals.loaded_length;
		message.offset = offset;
		message.length = (word)length;
		csmemcpy(message.text, forge_layout_globals.loaded_text + offset, length);
		distributed_send_to_machine_reliably(machine_index, &message, _distributed_message_forge_layout, 0,
			(word)(offsetof(struct forge_layout_message, text) + length));
		offset += length;
	}

	return;
}

void forge_layout_handle_message(
	void const *payload,
	unsigned long size)
{
	struct forge_layout_message message;
	unsigned long header_size = offsetof(struct forge_layout_message, total_length);
	unsigned long fixed_size = offsetof(struct forge_layout_message, text) - header_size;

	if (game_connection() != _game_connection_network_client || size < fixed_size)
		return;
	csmemcpy((byte *)&message + header_size, payload, MIN(size, sizeof(message) - header_size));
	if (message.length > size - fixed_size || message.length > FORGE_LAYOUT_MESSAGE_TEXT_SIZE ||
		message.total_length >= FORGE_LAYOUT_MAXIMUM_TEXT)
	{
		return;
	}
	/* the parts come in order (reliably); a first part starts over */
	if (message.offset == 0)
		forge_layout_globals.received_length = 0;
	if (message.offset != forge_layout_globals.received_length ||
		message.offset + message.length > message.total_length)
	{
		return;
	}
	csmemcpy(forge_layout_globals.received_text + message.offset, message.text, message.length);
	forge_layout_globals.received_length += message.length;

	if (forge_layout_globals.received_length == message.total_length)
	{
		unsigned long checksum = 5381;
		unsigned long index;

		forge_layout_globals.received_text[message.total_length] = 0;
		for (index = 0; index < message.total_length; index++)
			checksum = checksum * 33 + (unsigned char)forge_layout_globals.received_text[index];
		if (checksum != forge_layout_globals.applied_checksum)
		{
			forge_layout_globals.applied_checksum = checksum;
			forge_layout_apply(forge_layout_globals.received_text, TRUE);
		}
		forge_layout_globals.received_length = 0;
	}

	return;
}

void forge_layout_new_map(
	void)
{
	char const *scenario_name = global_scenario_index != NONE ? tag_get_name(global_scenario_index) : NULL;
	char const *separator = scenario_name ? strrchr(scenario_name, '\\') : NULL;

	forge_layout_globals.spawned_count = 0;
	forge_layout_globals.current_slot = 0;
	forge_layout_globals.chosen_slot = 0;
	forge_layout_globals.map_name[0] = 0;
	forge_layout_globals.loaded_length = 0;
	forge_layout_globals.loaded_text[0] = 0;
	forge_layout_globals.received_length = 0;
	forge_layout_globals.applied_checksum = 0;
	if (scenario_name)
	{
		_snprintf(forge_layout_globals.map_name, sizeof(forge_layout_globals.map_name), "%s",
			separator ? separator + 1 : scenario_name);
		forge_layout_globals.map_name[sizeof(forge_layout_globals.map_name) - 1] = 0;
	}
	forge_layout_note_map_objects();

	/* a system link client gets the host's layout when it has loaded
	(forge_layout_handle_message) */
	if (forge_layout_authoritative())
	{
		/* the layout chosen as a map in the map list, else the one that
		plays on the map */
		short slot;

		forge_layout_read_slots();
		slot = forge_layout_selected_slot();
		if (slot && !forge_layout_globals.slot_names[slot][0])
			slot = 0;
		if (!slot)
			slot = forge_layout_globals.play_slot;
		if (slot)
			forge_layout_load(slot);
	}

	return;
}
