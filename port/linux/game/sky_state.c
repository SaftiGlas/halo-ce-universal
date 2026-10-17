/*
SKY_STATE.C

The game's side of the sky of the ports (port/linux/src/skyfx.c,
halo_sky.h): where each window's sky is drawn, and the direction each of
its pixels looks along, for a sky the renderer draws itself and for the
tint over the map's sky.

The game's one call of render_sky, in render_window (render/render.c) right
after the window begins, is a call of halo_sky_render here, in every build.
*/

#include "cseries.h"
#include "render/render.h"
#include "render/render_cameras_internal.h"

#include "halo_sky.h"

/* render/render_sky.c's */
void render_sky(void);

/* ---------- private code */

/* the world direction the screen point (-1 to 1, y down) looks along */
static void sky_screen_direction(
	real x,
	real y,
	float direction[3])
{
	real_point2d screen_point;
	real_point3d origin;
	real_vector3d vector;

	screen_point.x = x;
	screen_point.y = y;
	render_camera_screen_to_world(&render.camera, &render.frustum, &screen_point, &origin, &vector);
	direction[0] = vector.i;
	direction[1] = vector.j;
	direction[2] = vector.k;

	return;
}

/* ---------- public code */

void halo_sky_render(
	void)
{
	if (halo_sky_changed() && render.visible_sky_model && !render.camera.mirrored)
	{
		struct halo_sky_view view;
		float corner_x[3];
		float corner_y[3];
		short axis;

		sky_screen_direction(0.f, 0.f, view.direction);
		sky_screen_direction(1.f, 0.f, corner_x);
		sky_screen_direction(0.f, 1.f, corner_y);
		for (axis = 0; axis < 3; axis++)
		{
			view.step_x[axis] = corner_x[axis] - view.direction[axis];
			view.step_y[axis] = corner_y[axis] - view.direction[axis];
		}

		if (!halo_sky_draw(&view, FALSE))
			render_sky();
		halo_sky_draw(&view, TRUE);
	}
	else
	{
		render_sky();
	}

	return;
}
