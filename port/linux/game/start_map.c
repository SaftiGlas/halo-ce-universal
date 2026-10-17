/*
START_MAP.C

Starts a map when the game starts, without the menus (game.start_map,
game.start_variant and game.start_difficulty in config.toml; the environment
variables HALO_START_MAP, HALO_START_VARIANT and HALO_START_DIFFICULTY; the
mod launcher's --map, --variant and --difficulty set them).

It does what the developer console does with the lines of d:\init.txt, and
runs after them, so it wins over an init.txt that starts a map:

	game_difficulty_set <difficulty>
	game_variant <variant>         (only when a variant is given)
	map_name <map>

A campaign map ("a10", "levels\a10\a10") is a local game. A multiplayer map
("bloodgulch", "levels\test\bloodgulch\bloodgulch") is a local game too,
with the game variant given (the built-in ones of
game_engine_get_variant_by_name: slayer, team_slayer, ctf, king, oddball,
race...), the only kind the dev tools (forge.c) and the source mods work in.

A short name is expanded to the map's scenario path. The campaign levels
and the 13 multiplayer maps of the game are known; another name is looked
for as levels\test\<name>\<name>, as the game's own multiplayer maps are.

With game.start_layout (HALO_START_LAYOUT, the launcher's --layout) a
multiplayer map is played with that Forge layout of it loaded
(forge_layout.c, forge_custom_map_select_by_name), as when it is chosen as
a map of its own in the map list.

Called at the end of console_startup (main/console.c).
*/

#include "cseries.h"
#include "hs/hs.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* the platform layer's (port/linux/src/port_config.c) */
const char *config_string(char const *name);
long config_integer(char const *name);
void platform_log(char const *format, ...);

/* ---------- constants */

/* the campaign's levels, which are levels\<name>\<name> */
static char const *const campaign_levels[] =
{
	"a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40", "ui",
};

/* ---------- private code */

static boolean start_map_is_campaign_level(
	char const *name)
{
	short index;

	for (index = 0; index < NUMBEROF(campaign_levels); index++)
	{
		if (!_stricmp(name, campaign_levels[index]))
			return TRUE;
	}

	return FALSE;
}

/* the scenario path of a short name, as the game wants it (backslashes) */
static void start_map_scenario_path(
	char const *name,
	char *path,
	size_t size)
{
	if (strchr(name, '\\') || strchr(name, '/'))
	{
		size_t index;

		snprintf(path, size, "%s", name);
		for (index = 0; path[index]; index++)
		{
			if (path[index] == '/')
				path[index] = '\\';
		}
	}
	else if (start_map_is_campaign_level(name))
	{
		snprintf(path, size, "levels\\%s\\%s", name, name);
	}
	else
	{
		snprintf(path, size, "levels\\test\\%s\\%s", name, name);
	}

	return;
}

/* whether a name is safe to hand to the console: no spaces, quotes or
semicolons that would make it more than one word or a comment */
static boolean start_map_word_is_plain(
	char const *word)
{
	if (!word[0])
		return FALSE;
	for (; *word; word++)
	{
		if (!isalnum((unsigned char)*word) && !strchr("_\\/.-", *word))
			return FALSE;
	}

	return TRUE;
}

static void start_map_command(
	char const *format,
	char const *argument)
{
	char command[300];

	snprintf(command, sizeof(command), format, argument);
	platform_log("start: %s", command);
	if (!hs_compile_and_evaluate(command))
		platform_log("start: the console refused \"%s\"", command);

	return;
}

/* ---------- public code */

void halo_start_map(
	void)
{
	char const *map = config_string("game.start_map");
	char const *variant = config_string("game.start_variant");
	char const *difficulty = config_string("game.start_difficulty");
	char path[256];

	if (!map[0])
		return;
	if (!start_map_word_is_plain(map) ||
		(variant[0] && !start_map_word_is_plain(variant)) ||
		(difficulty[0] && !start_map_word_is_plain(difficulty)))
	{
		platform_log("start: game.start_map, start_variant or start_difficulty has a character it may not");
		return;
	}

	start_map_scenario_path(map, path, sizeof(path));
	if (config_integer("game.start_layout") > 0)
	{
		char const *separator = strrchr(path, '\\');

		if (!forge_custom_map_select_by_name(separator ? separator + 1 : path, (short)config_integer("game.start_layout")))
			platform_log("start: game.start_layout is for the multiplayer maps of the game, 1 to 16");
	}
	if (difficulty[0])
		start_map_command("game_difficulty_set %s", difficulty);
	if (variant[0])
		start_map_command("game_variant %s", variant);
	start_map_command("map_name %s", path);

	return;
}
