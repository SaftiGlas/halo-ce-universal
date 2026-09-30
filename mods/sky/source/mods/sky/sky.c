/*
SKY.C

A source mod (mods/sky): the look of the sky and of the fog.

The dev tools' menu (F3 / D-pad right) has a "Sky" tab with one row, the
look: left and right change it, so it works on a controller as it does on a
keyboard. The looks, in order: the map's own sky and
fog; the map's sky tinted, with a fog to match, for a sunset, a night and
a fog; then skies a shader draws instead of the map's sky (the port's
GLSL sky, port/linux/src/skyfx.c) with their fog: a clear day, a golden
sunset, a night with stars, an overcast day and an alien dusk.

The tint and the shader's sky are drawn by the renderer (halo_sky.h): the
tint multiplies what the map's sky drew, and the shader's sky replaces the
map's wherever the map's sky shows (not indoors). The fog is the game's
own, so it is the map's fog tag that changes: at each new map this mod
keeps the fog of each of the scenario's skies, and a look blends them
towards its own colour, density and distances by how much it says (a map
with no fog gets the look's, from nothing); the map's own look puts them
back. Only local games change (the fog is in the tags every machine of a
system link game loads alike); the choice is kept from map to map.
*/

#include "cseries.h"
#include "game/game.h"
#include "interface/terminal.h"
#include "math/real_math.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "scenario/sky_definitions.h"

#include <stdio.h>
#include <string.h>

/* ---------- constants */

/* the opaque distance a fogless map's fog starts from, world units */
#define SKY_NO_FOG_DISTANCE 2000.f

/* ---------- structures */

struct sky_look
{
	char const *name;

	/* the map's sky, or the renderer's own (halo_sky.h) */
	boolean procedural;
	real tint[3];
	real tint_amount;
	real zenith[3];
	real horizon[3];
	real ground[3];
	real sun_elevation;
	real sun_azimuth;
	real sun_color[3];
	real sun_size;
	real stars;
	real clouds;

	/* the fog: how much of the look's, 0..1 (0 leaves the map's own), and
	its colour, greatest density and distances (world units) */
	real fog_amount;
	real fog_color[3];
	real fog_density;
	real fog_start;
	real fog_opaque;
};

/* ---------- globals */

static struct sky_look const sky_looks[] =
{
	{ "the map's own", FALSE, { 1.f, 1.f, 1.f }, 0.f },

	/* the map's sky, tinted */
	{
		"sunset (map's sky)", FALSE, { 1.f, 0.62f, 0.42f }, 0.75f,
		{ 0 }, { 0 }, { 0 }, 0.f, 0.f, { 0 }, 0.f, 0.f, 0.f,
		0.7f, { 0.85f, 0.45f, 0.30f }, 0.55f, 10.f, 140.f
	},
	{
		"night (map's sky)", FALSE, { 0.10f, 0.14f, 0.32f }, 0.9f,
		{ 0 }, { 0 }, { 0 }, 0.f, 0.f, { 0 }, 0.f, 0.f, 0.f,
		0.8f, { 0.03f, 0.05f, 0.12f }, 0.85f, 4.f, 80.f
	},
	{
		"fog (map's sky)", FALSE, { 0.75f, 0.78f, 0.80f }, 0.6f,
		{ 0 }, { 0 }, { 0 }, 0.f, 0.f, { 0 }, 0.f, 0.f, 0.f,
		1.f, { 0.72f, 0.75f, 0.78f }, 0.9f, 2.f, 45.f
	},

	/* skies the shader draws */
	{
		"clear day", TRUE, { 1.f, 1.f, 1.f }, 0.f,
		{ 0.16f, 0.40f, 0.85f }, { 0.66f, 0.80f, 0.95f }, { 0.30f, 0.33f, 0.30f },
		45.f, 200.f, { 1.f, 0.95f, 0.80f }, 1.f, 0.f, 0.35f,
		0.4f, { 0.66f, 0.80f, 0.95f }, 0.35f, 20.f, 250.f
	},
	{
		"golden sunset", TRUE, { 1.f, 1.f, 1.f }, 0.f,
		{ 0.10f, 0.14f, 0.42f }, { 1.f, 0.55f, 0.25f }, { 0.25f, 0.16f, 0.14f },
		4.f, 200.f, { 1.f, 0.60f, 0.30f }, 1.3f, 1.f, 0.5f,
		0.6f, { 0.85f, 0.45f, 0.25f }, 0.6f, 10.f, 160.f
	},
	{
		"night", TRUE, { 1.f, 1.f, 1.f }, 0.f,
		{ 0.01f, 0.02f, 0.08f }, { 0.05f, 0.07f, 0.16f }, { 0.02f, 0.03f, 0.05f },
		-25.f, 200.f, { 0.6f, 0.7f, 1.f }, 1.f, 1.f, 0.2f,
		0.8f, { 0.02f, 0.03f, 0.08f }, 0.8f, 5.f, 90.f
	},
	{
		"overcast", TRUE, { 1.f, 1.f, 1.f }, 0.f,
		{ 0.45f, 0.48f, 0.52f }, { 0.65f, 0.68f, 0.70f }, { 0.30f, 0.30f, 0.30f },
		60.f, 200.f, { 0.4f, 0.4f, 0.4f }, 0.5f, 0.f, 1.f,
		0.7f, { 0.62f, 0.65f, 0.68f }, 0.7f, 5.f, 120.f
	},
	{
		"alien dusk", TRUE, { 1.f, 1.f, 1.f }, 0.f,
		{ 0.25f, 0.05f, 0.35f }, { 0.95f, 0.35f, 0.55f }, { 0.12f, 0.05f, 0.14f },
		8.f, 200.f, { 1.f, 0.50f, 0.70f }, 2.2f, 0.8f, 0.4f,
		0.6f, { 0.60f, 0.20f, 0.40f }, 0.6f, 8.f, 150.f
	}
};

#define NUMBER_OF_SKY_LOOKS ((short)(sizeof(sky_looks) / sizeof(sky_looks[0])))

static struct
{
	short look_index;

	/* the fog of each sky of the scenario as the map has it, from the new
	map hook on */
	short sky_count;
	struct sky_atmospheric_fog outdoor_fog[MAXIMUM_SKIES_PER_SCENARIO];
	struct sky_atmospheric_fog indoor_fog[MAXIMUM_SKIES_PER_SCENARIO];
} sky_globals = { { 0 } };

/* ---------- private code */

static real sky_blend(
	real from,
	real to,
	real amount)
{
	return from + (to - from) * amount;
}

/* the map's fog towards the look's by the look's amount; a map without fog
(no opaque distance) starts from a fog too far and too thin to see */
static void sky_blend_fog(
	struct sky_atmospheric_fog const *original,
	struct sky_look const *look,
	struct sky_atmospheric_fog *fog)
{
	real amount = PIN(look->fog_amount, 0.f, 1.f);
	boolean has_fog = original->opaque_distance != 0.f;

	*fog = *original;
	if (amount > 0.f)
	{
		fog->color.red = sky_blend(original->color.red, look->fog_color[0], amount);
		fog->color.green = sky_blend(original->color.green, look->fog_color[1], amount);
		fog->color.blue = sky_blend(original->color.blue, look->fog_color[2], amount);
		fog->maximum_density = sky_blend(has_fog ? original->maximum_density : 0.f, look->fog_density, amount);
		fog->start_distance = sky_blend(has_fog ? original->start_distance : look->fog_start,
			look->fog_start, amount);
		fog->opaque_distance = sky_blend(has_fog ? original->opaque_distance : SKY_NO_FOG_DISTANCE,
			look->fog_opaque, amount);
	}

	return;
}

static void sky_apply(
	struct sky_look const *look)
{
	struct halo_sky_appearance appearance;
	short sky_index;

	halo_sky_default_appearance(&appearance);
	appearance.procedural = look->procedural;
	memcpy(appearance.tint, look->tint, sizeof(appearance.tint));
	appearance.tint_amount = look->tint_amount;
	if (look->procedural)
	{
		memcpy(appearance.zenith, look->zenith, sizeof(appearance.zenith));
		memcpy(appearance.horizon, look->horizon, sizeof(appearance.horizon));
		memcpy(appearance.ground, look->ground, sizeof(appearance.ground));
		memcpy(appearance.sun_color, look->sun_color, sizeof(appearance.sun_color));
		appearance.sun_elevation = look->sun_elevation;
		appearance.sun_azimuth = look->sun_azimuth;
		appearance.sun_size = look->sun_size;
		appearance.stars = look->stars;
		appearance.clouds = look->clouds;
	}
	halo_sky_set_appearance(&appearance);

	for (sky_index = 0; sky_index < sky_globals.sky_count; sky_index++)
	{
		struct sky *sky = scenario_get_sky(sky_index);

		if (sky)
		{
			sky_blend_fog(&sky_globals.outdoor_fog[sky_index], look, &sky->outdoor_fog);
			sky_blend_fog(&sky_globals.indoor_fog[sky_index], look, &sky->indoor_fog);
		}
	}

	return;
}

/* ---------- the tools' menu page */

static short sky_menu_row_count(
	void)
{
	return 1;
}

static void sky_menu_row_text(
	short row,
	char *label,
	unsigned long label_size,
	char *value,
	unsigned long value_size)
{
	_snprintf(label, label_size, "Sky and fog look");
	_snprintf(value, value_size, "%s", sky_looks[sky_globals.look_index].name);

	return;
}

static int sky_menu_row_change(
	short row,
	int direction)
{
	if (direction != 0)
	{
		if (game_connection() != _game_connection_local)
		{
			terminal_printf(global_real_argb_orange, "sky: changes only in local games");
		}
		else
		{
			short step = direction < 0 ? NUMBER_OF_SKY_LOOKS - 1 : 1;

			sky_globals.look_index = (short)((sky_globals.look_index + step) % NUMBER_OF_SKY_LOOKS);
			sky_apply(&sky_looks[sky_globals.look_index]);
		}
	}

	return FALSE;
}

static struct halo_mod_menu const sky_menu =
{
	"Sky",
	sky_menu_row_count,
	sky_menu_row_text,
	sky_menu_row_change,
	NULL
};

/* every game, local or not: a system link game is drawn with the map's own
sky and fog, and the chosen look comes back with the next local game */
static void sky_new_map(
	void)
{
	short sky_index;

	sky_globals.sky_count = 0;
	if (global_scenario)
	{
		sky_globals.sky_count = (short)MIN(global_scenario->sky_references.count, MAXIMUM_SKIES_PER_SCENARIO);
	}
	for (sky_index = 0; sky_index < sky_globals.sky_count; sky_index++)
	{
		struct sky *sky = scenario_get_sky(sky_index);

		if (sky)
		{
			sky_globals.outdoor_fog[sky_index] = sky->outdoor_fog;
			sky_globals.indoor_fog[sky_index] = sky->indoor_fog;
		}
		else
		{
			memset(&sky_globals.outdoor_fog[sky_index], 0, sizeof(sky_globals.outdoor_fog[sky_index]));
			memset(&sky_globals.indoor_fog[sky_index], 0, sizeof(sky_globals.indoor_fog[sky_index]));
		}
	}

	if (game_connection() == _game_connection_local)
		sky_apply(&sky_looks[sky_globals.look_index]);
	else
		halo_sky_set_appearance(NULL);

	return;
}

/* ---------- layouts (port/linux/game/forge_layout.c) */

static void sky_layout_save(
	struct halo_layout_writer *writer)
{
	halo_layout_printf(writer, "look %s", sky_looks[sky_globals.look_index].name);

	return;
}

static void sky_layout_clear(
	void)
{
	sky_globals.look_index = 0;
	sky_apply(&sky_looks[0]);

	return;
}

static void sky_layout_load(
	char const *line)
{
	short index;

	if (strncmp(line, "look ", 5) == 0)
	{
		for (index = 0; index < NUMBER_OF_SKY_LOOKS; index++)
		{
			if (strcmp(line + 5, sky_looks[index].name) == 0)
			{
				sky_globals.look_index = index;
				sky_apply(&sky_looks[index]);
			}
		}
	}

	return;
}

/* ---------- the mod */

static struct halo_mod const sky_mod =
{
	"sky",
	NULL,
	NULL,
	NULL,
	sky_new_map,
	NULL,
	&sky_menu,
	NULL,
	NULL,
	sky_layout_save,
	sky_layout_clear,
	sky_layout_load
};

HALO_MOD_REGISTER(sky_mod)
