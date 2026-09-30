/*
SKYFX.H

The sky of the native ports (skyfx.c): a sky a shader draws instead of the
map's sky model, and a tint over the map's sky, both set by source mods
through halo_sky.h. Only the port draws them; the game's code is not
changed.
*/

#ifndef __HALO_LINUX_SKYFX_H
#define __HALO_LINUX_SKYFX_H

#include "../include/halo_sky.h"

/* for the renderer (d3d8_gl.c), on its thread with GL current and the
window's targets bound: draws over viewport (x, y, width, height in the
targets' pixels, as the renderer sets it). after_sky FALSE: the shader's sky
when it is on (returns TRUE, and the map's sky is not to be drawn); after_sky
TRUE: the tint over what the map's sky drew. GL state is left as the
renderer's cache expects after xgpu_gl_state_invalidate, which the caller
calls. */
int skyfx_draw(const struct halo_sky_view *view, const int viewport[4], int after_sky);

#endif
