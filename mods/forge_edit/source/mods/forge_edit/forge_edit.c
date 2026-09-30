/*
FORGE_EDIT.C

A source mod (mods/forge_edit): copy, paste and remove objects, with the
dev tools' crosshair (port/linux/game/forge.c).

All of it is on the "Edit" tab of the dev tools' menu (F3 / D-pad right;
LB / RB, Page Up / Page Down, or X / Y change tab): up and down choose a
row, enter or A act, so it works on a controller as on a keyboard, and no
Ctrl key is used. The rows:

	At the crosshair  the object the dev tools would pick up, or nothing
	Copy              copy it: its tag, turn and scale
	Paste             place a copy where the crosshair points
	Remove            remove it
	Undo remove       bring back the last removed object (the last 16, one
	                  at a time)

On a keyboard, Delete removes the object under the crosshair with the menu
closed. The object under the crosshair is the one the dev tools would pick
up: flying, where the crosshair points, else where the player looks. While
the camera flies it is marked on the screen with its name.

Objects are made and removed only with the game's own object_new and
object_delete. The player, what they ride and carry, and anything that
holds a player (a vehicle with another player in it) are never marked, so
they can be neither copied nor removed; object indices are checked with
object_try_and_get before every use. As with the dev tools, all of this
works only in local games, and not during cutscenes or while the tools'
menu is open or they hold an object (their own delete then removes the held
object).

An object brought back is a new object of the same tag, place, turn and
scale: what it carried (a biped's weapon) and its state (damage, an actor's
orders) are gone. Copies and removals are forgotten when the map changes;
removals also when the game goes back to a checkpoint, which brings the
removed objects back itself.
*/

#include "cseries.h"
#include "camera/director.h"
#include "cutscene/cinematics.h"
#include "game/game.h"
#include "game/players.h"
#include "interface/terminal.h"
#include "math/real_math.h"
#include "objects/object_types.h"
#include "objects/objects.h"
#include "scenario/scenario.h"
#include "tag_files/tag_groups.h"
#include "units/units.h"

#include <stdio.h>

/* ---------- constants */

enum
{
	FORGE_EDIT_UNDO_COUNT = 16,
	FORGE_EDIT_LOCAL_PLAYER_INDEX = 0,
	FORGE_EDIT_LABEL_OFFSET = 14,
	FORGE_EDIT_LABEL_MARGIN = 4
};

/* 0xAARRGGBB */
#define FORGE_EDIT_LABEL_COLOR 0xffffff80UL
#define FORGE_EDIT_HINT_COLOR 0xffc0c0c0UL
#define FORGE_EDIT_LABEL_BACKGROUND_COLOR 0x90000000UL

/* ---------- structures */

/* what makes an object again */
struct forge_edit_object
{
	long definition_index;
	real_point3d position;
	real_vector3d forward;
	real_vector3d up;
	real scale;
};

/* ---------- globals */

static struct
{
	struct halo_mod_key_state delete_key;

	/* the map the copy and the removals belong to, and the game time of the
	last update (a checkpoint's revert takes it back) */
	char map_name[256];
	long game_time;
	boolean copied;
	struct forge_edit_object copy;
	short removed_count;
	struct forge_edit_object removed[FORGE_EDIT_UNDO_COUNT];

	/* this frame's marked object, for the render hook */
	boolean marking;
	long marked_object_index;
} forge_edit_globals = { { 0 } };

/* ---------- private code */

/* the object's name for messages: the last part of its tag's name */
static char const *forge_edit_object_name(
	long definition_index)
{
	char const *name = tag_get_name(definition_index);
	char const *separator = strrchr(name, '\\');

	return separator ? separator + 1 : name;
}

/* TRUE when the object is a player's unit, or holds one (a vehicle) */
static boolean forge_edit_holds_player(
	long object_index)
{
	struct object_datum *object = object_try_and_get(object_index);
	boolean holds_player = FALSE;

	if (object)
	{
		long child_index;

		if (TEST_FLAG(_object_mask_unit, object->object.type) &&
			unit_get(object_index)->unit.player_index != NONE)
		{
			holds_player = TRUE;
		}
		for (child_index = object->object.first_child_object_index;
			!holds_player && child_index != NONE;
			child_index = object_get(child_index)->object.next_object_index)
		{
			holds_player = forge_edit_holds_player(child_index);
		}
	}

	return holds_player;
}

/* the object under the crosshair that may be copied and removed, or NONE */
static long forge_edit_marked_object(
	void)
{
	long object_index = forge_object_at_crosshair();

	if (object_index != NONE &&
		(!object_try_and_get(object_index) || forge_edit_holds_player(object_index)))
	{
		object_index = NONE;
	}

	return object_index;
}

static void forge_edit_record(
	long object_index,
	struct forge_edit_object *record)
{
	struct object_datum *object = object_get(object_index);

	record->definition_index = object->definition_index;
	record->position = object->object.position;
	record->forward = object->object.forward;
	record->up = object->object.up;
	record->scale = object->object.scale;

	return;
}

/* makes the object of the record at the position; NONE if the game cannot */
static long forge_edit_create(
	struct forge_edit_object const *record,
	real_point3d const *position)
{
	struct object_placement_data data;
	long object_index;

	object_placement_data_new(&data, record->definition_index, NONE);
	data.position = *position;
	data.forward = record->forward;
	data.up = record->up;
	object_index = object_new(&data);
	if (object_index != NONE && record->scale != 1.f)
		objects_scripting_set_scale(object_index, record->scale, 0);

	return object_index;
}

static void forge_edit_copy(
	long object_index)
{
	if (object_index == NONE)
	{
		terminal_printf(global_real_argb_orange, "forge_edit: nothing to copy there");
	}
	else
	{
		forge_edit_record(object_index, &forge_edit_globals.copy);
		forge_edit_globals.copied = TRUE;
		terminal_printf(global_real_argb_green, "forge_edit: copied %s",
			forge_edit_object_name(forge_edit_globals.copy.definition_index));
	}

	return;
}

static void forge_edit_paste(
	void)
{
	float position[3];

	if (!forge_edit_globals.copied)
	{
		terminal_printf(global_real_argb_orange, "forge_edit: nothing copied yet (Edit tab, Copy)");
	}
	else if (!forge_placement_at_crosshair(forge_edit_globals.copy.definition_index, position))
	{
		terminal_printf(global_real_argb_orange, "forge_edit: the camera is outside the map");
	}
	else
	{
		real_point3d point;

		set_real_point3d(&point, position[0], position[1], position[2]);
		if (forge_edit_create(&forge_edit_globals.copy, &point) == NONE)
		{
			terminal_printf(global_real_argb_orange, "forge_edit: %s could not be created",
				forge_edit_object_name(forge_edit_globals.copy.definition_index));
		}
		else
		{
			terminal_printf(global_real_argb_green, "forge_edit: placed %s",
				forge_edit_object_name(forge_edit_globals.copy.definition_index));
		}
	}

	return;
}

static void forge_edit_delete(
	long object_index)
{
	/* nothing marked: nothing happens */
	if (object_index != NONE)
	{
		struct forge_edit_object *record;

		if (forge_edit_globals.removed_count == FORGE_EDIT_UNDO_COUNT)
		{
			/* the oldest is forgotten */
			memmove(
				&forge_edit_globals.removed[0],
				&forge_edit_globals.removed[1],
				sizeof(forge_edit_globals.removed[0]) * (FORGE_EDIT_UNDO_COUNT - 1));
			forge_edit_globals.removed_count--;
		}
		record = &forge_edit_globals.removed[forge_edit_globals.removed_count++];
		forge_edit_record(object_index, record);
		object_delete(object_index);
		terminal_printf(global_real_argb_green, "forge_edit: removed %s (Edit tab, Undo remove brings it back)",
			forge_edit_object_name(record->definition_index));
	}

	return;
}

static void forge_edit_undo(
	void)
{
	if (forge_edit_globals.removed_count == 0)
	{
		terminal_printf(global_real_argb_orange, "forge_edit: nothing removed to bring back");
	}
	else
	{
		struct forge_edit_object const *record =
			&forge_edit_globals.removed[--forge_edit_globals.removed_count];

		if (forge_edit_create(record, &record->position) == NONE)
		{
			terminal_printf(global_real_argb_orange, "forge_edit: %s could not be brought back",
				forge_edit_object_name(record->definition_index));
		}
		else
		{
			terminal_printf(global_real_argb_green, "forge_edit: brought back %s (%d more)",
				forge_edit_object_name(record->definition_index), forge_edit_globals.removed_count);
		}
	}

	return;
}

/* copies and removals name the tags of the map they were made in, and a
revert to a checkpoint brings back what was removed since */
static void forge_edit_check_map(
	void)
{
	char const *map_name = global_scenario_index != NONE ? tag_get_name(global_scenario_index) : "";
	long game_time = game_time_get();

	if (strcmp(map_name, forge_edit_globals.map_name) != 0)
	{
		_snprintf(forge_edit_globals.map_name, sizeof(forge_edit_globals.map_name), "%s", map_name);
		forge_edit_globals.copied = FALSE;
		forge_edit_globals.removed_count = 0;
	}
	else if (game_time < forge_edit_globals.game_time && forge_edit_globals.removed_count > 0)
	{
		forge_edit_globals.removed_count = 0;
		terminal_printf(global_real_argb_green, "forge_edit: back at a checkpoint, removals forgotten");
	}
	forge_edit_globals.game_time = game_time;

	return;
}

static void forge_edit_update(
	void)
{
	boolean delete_object = halo_mod_key_pressed(&forge_edit_globals.delete_key, HALO_MOD_KEY_DELETE, FALSE);

	forge_edit_globals.marking = FALSE;
	forge_edit_globals.marked_object_index = NONE;
	if (local_player_get_player_index(FORGE_EDIT_LOCAL_PLAYER_INDEX) == NONE)
		return;
	forge_edit_check_map();

	/* the tools' own keys come first while their menu is open or they hold
	an object; a cutscene's objects are the cutscene's */
	if (forge_busy() || cinematic_in_progress())
		return;

	forge_edit_globals.marking = director_forge_flying(FORGE_EDIT_LOCAL_PLAYER_INDEX);
	if (forge_edit_globals.marking || delete_object)
		forge_edit_globals.marked_object_index = forge_edit_marked_object();

	if (delete_object)
	{
		if (game_connection() != _game_connection_local)
		{
			terminal_printf(global_real_argb_orange, "forge_edit: objects change only in local games");
		}
		else
		{
			forge_edit_delete(forge_edit_globals.marked_object_index);
			forge_edit_globals.marked_object_index = NONE;
		}
	}

	return;
}

static void forge_edit_render(
	void)
{
	short x0;
	short y0;
	short x1;
	short y1;
	long font;
	struct object_datum *object;

	if (forge_edit_globals.marking &&
		forge_edit_globals.marked_object_index != NONE &&
		(object = object_try_and_get(forge_edit_globals.marked_object_index)) != NULL &&
		halo_mod_screen(&x0, &y0, &x1, &y1) &&
		(font = halo_mod_font(FALSE)) != NONE)
	{
		char label[160];
		char hint[160];
		short line_height = halo_mod_line_height(font);
		short center_x = (short)((x0 + x1) / 2);
		short top = (short)((y0 + y1) / 2 + FORGE_EDIT_LABEL_OFFSET);
		short width;

		_snprintf(label, sizeof(label), "%s", forge_edit_object_name(object->definition_index));
		_snprintf(hint, sizeof(hint), "%s removes, menu: Edit tab", halo_mod_key_name(HALO_MOD_KEY_DELETE));
		width = (short)(MAX(halo_mod_text_width(font, label), halo_mod_text_width(font, hint)) +
			4 * FORGE_EDIT_LABEL_MARGIN);

		halo_mod_draw_box(
			(short)(center_x - width / 2),
			(short)(top - FORGE_EDIT_LABEL_MARGIN),
			(short)(center_x + width / 2),
			(short)(top + 2 * line_height + FORGE_EDIT_LABEL_MARGIN),
			FORGE_EDIT_LABEL_BACKGROUND_COLOR);
		halo_mod_draw_text(font, x0, top, x1, (short)(top + line_height),
			HALO_MOD_TEXT_CENTER, FORGE_EDIT_LABEL_COLOR, label);
		halo_mod_draw_text(font, x0, (short)(top + line_height), x1, (short)(top + 2 * line_height),
			HALO_MOD_TEXT_CENTER, FORGE_EDIT_HINT_COLOR, hint);
	}

	/* drawn only after an update: nothing is left over when the game stops
	updating the mods (the console, menus, loading) */
	forge_edit_globals.marking = FALSE;

	return;
}

/* ---------- the tools' menu page */

enum
{
	_forge_edit_row_target = 0,
	_forge_edit_row_copy,
	_forge_edit_row_paste,
	_forge_edit_row_remove,
	_forge_edit_row_undo,
	NUMBER_OF_FORGE_EDIT_ROWS
};

static short forge_edit_menu_row_count(
	void)
{
	return NUMBER_OF_FORGE_EDIT_ROWS;
}

static void forge_edit_menu_row_text(
	short row,
	char *label,
	unsigned long label_size,
	char *value,
	unsigned long value_size)
{
	switch (row)
	{
	case _forge_edit_row_target:
	{
		long object_index = local_player_get_player_index(FORGE_EDIT_LOCAL_PLAYER_INDEX) != NONE
			? forge_edit_marked_object()
			: NONE;

		_snprintf(label, label_size, "At the crosshair");
		_snprintf(value, value_size, "%s",
			object_index != NONE ? forge_edit_object_name(object_get(object_index)->definition_index) : "nothing");
		break;
	}
	case _forge_edit_row_copy:
		_snprintf(label, label_size, "Copy");
		break;
	case _forge_edit_row_paste:
		_snprintf(label, label_size, "Paste (a copy at the crosshair)");
		_snprintf(value, value_size, "%s", forge_edit_globals.copied
			? forge_edit_object_name(forge_edit_globals.copy.definition_index)
			: "nothing copied");
		break;
	case _forge_edit_row_remove:
		_snprintf(label, label_size, "Remove");
		break;
	case _forge_edit_row_undo:
		_snprintf(label, label_size, "Undo remove");
		_snprintf(value, value_size, "%d to bring back", forge_edit_globals.removed_count);
		break;
	}

	return;
}

/* enter or A on a row (left and right change nothing here) */
static int forge_edit_menu_row_change(
	short row,
	int direction)
{
	int close_menu = FALSE;

	if (direction != 0 || row == _forge_edit_row_target)
		return FALSE;
	if (game_connection() != _game_connection_local)
	{
		terminal_printf(global_real_argb_orange, "forge_edit: objects change only in local games");
		return FALSE;
	}

	switch (row)
	{
	case _forge_edit_row_copy:
		forge_edit_copy(forge_edit_marked_object());
		break;
	case _forge_edit_row_paste:
		forge_edit_paste();
		close_menu = TRUE;
		break;
	case _forge_edit_row_remove:
		forge_edit_delete(forge_edit_marked_object());
		close_menu = TRUE;
		break;
	case _forge_edit_row_undo:
		forge_edit_undo();
		close_menu = TRUE;
		break;
	}

	return close_menu;
}

static struct halo_mod_menu const forge_edit_menu =
{
	"Edit",
	forge_edit_menu_row_count,
	forge_edit_menu_row_text,
	forge_edit_menu_row_change,
	NULL
};

/* ---------- the mod */

static struct halo_mod const forge_edit_mod =
{
	"forge_edit",
	forge_edit_update,
	forge_edit_render,
	NULL,
	NULL,
	NULL,
	&forge_edit_menu
};

HALO_MOD_REGISTER(forge_edit_mod)
