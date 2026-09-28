/*
CHECKPOINT_HANDLER.C

A source mod (mods/checkpoint_handler): checkpoints by hand, with the game's
own checkpoints.

	F5      take a checkpoint now
	F9      go back to the latest checkpoint, taken by hand or by the game

(the keys are in checkpoint_handler_keys.h). A checkpoint is the game's
whole state, as its automatic checkpoints are: the player's position,
health, shields and ammunition, and everything else in the level. Each
shows a message on the screen and writes "halo-linux: checkpoint set" or
"halo-linux: checkpoint restored" to standard output.

Checkpoints are refused outside local campaign games, during cutscenes and
while the player is dead (a checkpoint of a death would be one that can
never be survived); going back is refused only outside local campaign games
and during cutscenes. Without any checkpoint in the level, going back
restarts the level, as the game's own revert does.
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "cutscene/cinematics.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "main/main.h"
#include "objects/objects.h"

#include "checkpoint_handler_keys.h"

#include <stdio.h>

/* ---------- constants */

enum
{
	CHECKPOINT_MESSAGE_MILLISECONDS = 2500,
	CHECKPOINT_MESSAGE_MARGIN = 8
};

/* 0xAARRGGBB */
#define CHECKPOINT_SET_COLOR 0xff60ff60UL
#define CHECKPOINT_RESTORED_COLOR 0xff60c0ffUL
#define CHECKPOINT_REFUSED_COLOR 0xffffa040UL
#define CHECKPOINT_BACKGROUND_COLOR 0xa0000000UL

/* ---------- globals */

static struct
{
	struct halo_mod_key_state set_key;
	struct halo_mod_key_state restore_key;
	/* a checkpoint was asked for and the game has not written it yet */
	boolean saving;
	char message[128];
	unsigned long message_color;
	unsigned long message_end_milliseconds;
} checkpoint_globals;

/* ---------- private code */

static void checkpoint_log(
	char const *text)
{
	printf("halo-linux: %s\n", text);
	fflush(stdout);

	return;
}

static void checkpoint_show(
	unsigned long color,
	char const *text)
{
	_snprintf(checkpoint_globals.message, sizeof(checkpoint_globals.message), "%s", text);
	checkpoint_globals.message_color = color;
	checkpoint_globals.message_end_milliseconds = system_milliseconds() + CHECKPOINT_MESSAGE_MILLISECONDS;

	return;
}

/* why the game cannot go back to a checkpoint now, or NULL */
static char const *checkpoint_restore_refusal(
	void)
{
	char const *refusal = NULL;

	if (game_connection() != _game_connection_local || game_engine_running())
		refusal = "checkpoints work only in local campaign games";
	else if (cinematic_in_progress())
		refusal = "not during a cutscene";

	return refusal;
}

/* why the game cannot take a checkpoint now, or NULL */
static char const *checkpoint_set_refusal(
	void)
{
	char const *refusal = checkpoint_restore_refusal();

	if (!refusal)
	{
		long player_index = local_player_get_player_index(0);
		long unit_index = player_index != NONE ? player_get(player_index)->unit_index : NONE;
		struct object_datum *unit = unit_index != NONE ? object_try_and_get(unit_index) : NULL;

		if (!unit || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
			refusal = "not while the player is dead";
	}

	return refusal;
}

static void checkpoint_refuse(
	char const *action,
	char const *refusal)
{
	char text[160];

	_snprintf(text, sizeof(text), "%s: %s", action, refusal);
	checkpoint_log(text);
	checkpoint_show(CHECKPOINT_REFUSED_COLOR, text);

	return;
}

static void checkpoint_handler_update(
	void)
{
	boolean set = halo_mod_key_pressed(&checkpoint_globals.set_key, CHECKPOINT_SET_KEY, CHECKPOINT_SET_CTRL);
	boolean restore = halo_mod_key_pressed(&checkpoint_globals.restore_key, CHECKPOINT_RESTORE_KEY, CHECKPOINT_RESTORE_CTRL);

	if (local_player_get_player_index(0) == NONE)
		return;

	/* the game writes the checkpoint once it stops saving (main/main.c) */
	if (checkpoint_globals.saving && !main_saving_map())
	{
		checkpoint_globals.saving = FALSE;
		checkpoint_log("checkpoint set");
		checkpoint_show(CHECKPOINT_SET_COLOR, "Checkpoint set");
	}

	if (set)
	{
		char const *refusal = checkpoint_set_refusal();

		if (refusal)
		{
			checkpoint_refuse("checkpoint not set", refusal);
		}
		else
		{
			/* at once, rather than waiting for a quiet moment as the game's
			own checkpoints do */
			main_save_map_nonsafe();
			checkpoint_globals.saving = TRUE;
		}
	}
	else if (restore)
	{
		char const *refusal = checkpoint_restore_refusal();

		if (refusal)
		{
			checkpoint_refuse("checkpoint not restored", refusal);
		}
		else
		{
			/* the game goes back later in this frame (main/main.c) */
			main_revert_map();
			checkpoint_globals.saving = FALSE;
			checkpoint_log("checkpoint restored");
			checkpoint_show(CHECKPOINT_RESTORED_COLOR, "Checkpoint restored");
		}
	}

	return;
}

static void checkpoint_handler_render(
	void)
{
	short x0;
	short y0;
	short x1;
	short y1;
	long font;

	if (checkpoint_globals.message[0] &&
		(long)(system_milliseconds() - checkpoint_globals.message_end_milliseconds) < 0 &&
		halo_mod_screen(&x0, &y0, &x1, &y1) &&
		(font = halo_mod_font(TRUE)) != NONE)
	{
		short line_height = halo_mod_line_height(font);
		short width = halo_mod_text_width(font, checkpoint_globals.message) + 4 * CHECKPOINT_MESSAGE_MARGIN;
		short center = (short)((x0 + x1) / 2);
		short top = (short)(y0 + (y1 - y0) / 4);

		halo_mod_draw_box(
			(short)(center - width / 2),
			(short)(top - CHECKPOINT_MESSAGE_MARGIN),
			(short)(center + width / 2),
			(short)(top + line_height + CHECKPOINT_MESSAGE_MARGIN),
			CHECKPOINT_BACKGROUND_COLOR);
		halo_mod_draw_text(
			font,
			x0,
			top,
			x1,
			(short)(top + line_height),
			HALO_MOD_TEXT_CENTER,
			checkpoint_globals.message_color,
			checkpoint_globals.message);
	}

	return;
}

/* ---------- the mod */

static struct halo_mod const checkpoint_handler_mod =
{
	"checkpoint_handler",
	checkpoint_handler_update,
	checkpoint_handler_render
};

HALO_MOD_REGISTER(checkpoint_handler_mod)
