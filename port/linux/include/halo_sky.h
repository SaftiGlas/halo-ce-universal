/*
HALO_SKY.H

Between the game, the sky mod and the renderer of the native ports: how the
sky looks (port/linux/src/skyfx.c keeps it and draws it), and where the
game draws the sky of each window (port/linux/game/sky_state.c is the
game's side).
*/

#ifndef __HALO_SKY_H
#define __HALO_SKY_H

/* ---------- how the sky looks: for source mods (halo_mod.h) */

struct halo_sky_appearance
{
	/* TRUE: a sky the shader draws (below) instead of the map's sky model,
	wherever the map's sky shows */
	int procedural;

	/* the map's own sky, multiplied by this colour (1, 1, 1 changes
	nothing) by tint_amount, 0..1; also over the shader's sky */
	float tint[3];
	float tint_amount;

	/* the shader's sky: colours (linear, 0..1) overhead, at the horizon and
	below it */
	float zenith[3];
	float horizon[3];
	float ground[3];
	/* the sun: degrees above the horizon and along it (0 is the map's +x,
	90 its +y), its colour, and its size, about 1 */
	float sun_elevation;
	float sun_azimuth;
	float sun_color[3];
	float sun_size;
	/* 0..1: stars come out as the sun is below the horizon (that much of
	them), clouds cover that much of the sky, and drift with the time */
	float stars;
	float clouds;
	float cloud_speed;
};

/* the defaults: nothing changed, and a clear day for the shader's sky */
void halo_sky_default_appearance(struct halo_sky_appearance *appearance);
/* the look, from the next frame; halo_sky_set_appearance(NULL) puts the
defaults back */
void halo_sky_set_appearance(const struct halo_sky_appearance *appearance);
void halo_sky_get_appearance(struct halo_sky_appearance *appearance);

/* ---------- where the game draws the sky */

/* a window's view for the sky: the world direction of each pixel is
direction + x * step_x + y * step_y, with x -1 to 1 from the left to the
right of the view and y -1 to 1 from its top to its bottom (not unit) */
struct halo_sky_view
{
	float direction[3];
	float step_x[3];
	float step_y[3];
};

/* the game's render_sky of the window being drawn (the Linux link wraps it:
port/linux/game/sky_state.c): the renderer's own function, port/linux/src/
d3d8_gl.c. after_sky FALSE: before the map's sky is drawn; returns TRUE when
the sky was drawn here instead, and the game must not draw its own.
after_sky TRUE: after the map's sky, for its tint. */
int halo_sky_draw(const struct halo_sky_view *view, int after_sky);
/* TRUE when the look is anything but the map's own sky, so the game need
not compute the view for nothing */
int halo_sky_changed(void);

#endif
