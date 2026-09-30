/*
SKYFX.C

The sky of the native ports (see skyfx.h): what a source mod sets with
halo_sky_set_appearance (halo_sky.h), and its drawing.

The game draws each window's sky first, right after the window begins
(render_sky in render/render_sky.c). The Linux link wraps that function
(port/linux/game/sky_state.c): before it, the renderer draws the shader's
sky over the window's viewport, with the direction each pixel looks along,
and the game's own sky is skipped; after it, the tint multiplies what the
sky drew, which is all that is in the picture yet.

The shader's sky is a gradient (overhead, horizon, below), a sun with its
glow, stars that come out as the sun sets, and clouds that drift; all of
it from the appearance's numbers. World z is up.
*/

#include "xgpu.h"
#include "skyfx.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef HALO_ANDROID
#define SKYFX_SHADER_VERSION "#version 300 es\nprecision highp float;\nprecision highp int;\n"
#define SKYFX_NDC_SIGN 1.0f
#else
#define SKYFX_SHADER_VERSION "#version 330 core\n"
#define SKYFX_NDC_SIGN -1.0f
#endif

/* ---------- shaders */

/* a triangle over the whole viewport; v_screen is -1 to 1, x from the left
to the right, y from the top to the bottom, whichever way the renderer's
targets are turned (u_ndc_sign flips y for the row order of each
platform's targets) */
static const char skyfx_vertex_body[] =
	"uniform float u_ndc_sign;\n"
	"out vec2 v_screen;\n"
	"void main()\n"
	"{\n"
	"\tvec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
	"\tfloat row = 1.0 - p.y;\n"
	"\tv_screen = vec2(p.x * 2.0 - 1.0, row * 2.0 - 1.0);\n"
	"\tgl_Position = vec4(p.x * 2.0 - 1.0, (row * 2.0 - 1.0) * u_ndc_sign, 0.0, 1.0);\n"
	"}\n";

static const char skyfx_fragment_body[] =
	"in vec2 v_screen;\n"
	"layout(location = 0) out vec4 sky_color;\n"
	"uniform vec4 u_direction;   /* the world direction of the screen's middle */\n"
	"uniform vec4 u_step_x;      /* and how it changes to the right, and down */\n"
	"uniform vec4 u_step_y;\n"
	"uniform vec4 u_zenith;\n"
	"uniform vec4 u_horizon;\n"
	"uniform vec4 u_ground;\n"
	"uniform vec4 u_sun;         /* xyz its direction, w its size */\n"
	"uniform vec4 u_sun_color;\n"
	"uniform vec4 u_params;      /* stars, clouds, cloud speed, time in seconds */\n"
	"uniform vec4 u_tint;        /* rgb, and how much of it */\n"
	"uniform float u_tint_only;  /* 1: only the multiplier of the tint pass */\n"
	"\n"
	"float hash12(vec2 p)\n"
	"{\n"
	"\tvec3 p3 = fract(vec3(p.xyx) * 0.1031);\n"
	"\tp3 += dot(p3, p3.yzx + 33.33);\n"
	"\treturn fract((p3.x + p3.y) * p3.z);\n"
	"}\n"
	"float hash13(vec3 p)\n"
	"{\n"
	"\tp = fract(p * 0.1031);\n"
	"\tp += dot(p, p.zyx + 31.32);\n"
	"\treturn fract((p.x + p.y) * p.z);\n"
	"}\n"
	"float value_noise(vec2 p)\n"
	"{\n"
	"\tvec2 i = floor(p);\n"
	"\tvec2 f = fract(p);\n"
	"\tf = f * f * (3.0 - 2.0 * f);\n"
	"\treturn mix(mix(hash12(i), hash12(i + vec2(1.0, 0.0)), f.x),\n"
	"\t\tmix(hash12(i + vec2(0.0, 1.0)), hash12(i + vec2(1.0, 1.0)), f.x), f.y);\n"
	"}\n"
	"float cloud_noise(vec2 p)\n"
	"{\n"
	"\tfloat amplitude = 0.5;\n"
	"\tfloat sum = 0.0;\n"
	"\tfor (int octave = 0; octave < 5; octave++)\n"
	"\t{\n"
	"\t\tsum += amplitude * value_noise(p);\n"
	"\t\tp = p * 2.03 + vec2(17.1, 9.2);\n"
	"\t\tamplitude *= 0.5;\n"
	"\t}\n"
	"\treturn sum;\n"
	"}\n"
	"\n"
	"void main()\n"
	"{\n"
	"\tif (u_tint_only > 0.5)\n"
	"\t{\n"
	"\t\tsky_color = vec4(mix(vec3(1.0), u_tint.rgb, u_tint.a), 1.0);\n"
	"\t\treturn;\n"
	"\t}\n"
	"\tvec3 d = normalize(u_direction.xyz + v_screen.x * u_step_x.xyz + v_screen.y * u_step_y.xyz);\n"
	"\tvec3 sun = normalize(u_sun.xyz);\n"
	"\tfloat up = d.z;\n"
	"\tfloat sun_up = sun.z;\n"
	"\tfloat toward_sun = max(dot(d, sun), 0.0);\n"
	"\n"
	"\tvec3 color = mix(u_horizon.rgb, u_zenith.rgb, pow(clamp(up, 0.0, 1.0), 0.45));\n"
	"\tcolor = mix(u_ground.rgb, color, smoothstep(-0.25, 0.0, up));\n"
	"\n"
	"\t/* the sun: its glow, warming the horizon towards it, and its disc */\n"
	"\tfloat radius = 0.03 * u_sun.w;\n"
	"\tfloat disc = smoothstep(cos(radius * 1.3), cos(radius), toward_sun) * smoothstep(-0.06, 0.02, up);\n"
	"\tfloat glow = pow(toward_sun, 6.0) * 0.22 + pow(toward_sun, 60.0) * 0.5;\n"
	"\tfloat horizon_band = pow(1.0 - clamp(abs(up), 0.0, 1.0), 6.0);\n"
	"\tcolor += u_sun_color.rgb * (glow * smoothstep(-0.3, 0.05, up) + disc * 8.0 +\n"
	"\t\thorizon_band * pow(toward_sun, 3.0) * 0.35 * smoothstep(0.35, -0.05, sun_up));\n"
	"\n"
	"\t/* stars, as the sun goes down */\n"
	"\tfloat night = (1.0 - smoothstep(-0.12, 0.05, sun_up)) * u_params.x;\n"
	"\tif (night > 0.001 && up > 0.0)\n"
	"\t{\n"
	"\t\tvec3 scaled = d * 220.0;\n"
	"\t\tvec3 cell = floor(scaled);\n"
	"\t\tfloat seed = hash13(cell);\n"
	"\t\tif (seed > 0.9965)\n"
	"\t\t{\n"
	"\t\t\tvec3 center = cell + 0.5 + (vec3(hash13(cell + 1.7), hash13(cell + 5.3), hash13(cell + 9.1)) - 0.5) * 0.6;\n"
	"\t\t\tfloat star = smoothstep(0.35, 0.0, length(scaled - center));\n"
	"\t\t\tfloat twinkle = 0.75 + 0.25 * sin(u_params.w * 3.0 + seed * 400.0);\n"
	"\t\t\tcolor += vec3(1.0, 0.96, 0.9) * star * twinkle * night * smoothstep(0.0, 0.15, up) *\n"
	"\t\t\t\t(0.5 + 0.5 * fract(seed * 977.0));\n"
	"\t\t}\n"
	"\t}\n"
	"\n"
	"\t/* clouds, on a plane overhead that drifts */\n"
	"\tif (u_params.y > 0.001 && up > 0.005)\n"
	"\t{\n"
	"\t\tvec2 plane = d.xy / (up + 0.15) * 1.6 + vec2(0.02, 0.006) * u_params.w * u_params.z;\n"
	"\t\tfloat threshold = mix(0.78, 0.30, u_params.y);\n"
	"\t\tfloat cloud = smoothstep(threshold, threshold + 0.25, cloud_noise(plane));\n"
	"\t\tfloat lit = clamp(0.3 + 0.7 * max(sun_up, 0.0) + 0.2 * pow(toward_sun, 4.0), 0.0, 1.0);\n"
	"\t\tvec3 cloud_color = (0.5 + 0.5 * u_sun_color.rgb) * lit + u_horizon.rgb * 0.25 +\n"
	"\t\t\tu_sun_color.rgb * pow(toward_sun, 8.0) * 0.3;\n"
	"\t\tcolor = mix(color, cloud_color, cloud * smoothstep(0.005, 0.12, up) * 0.92);\n"
	"\t}\n"
	"\n"
	"\tsky_color = vec4(clamp(color, 0.0, 1.0), 1.0);\n"
	"}\n";

/* ---------- globals */

static struct
{
	SDL_SpinLock lock;
	struct halo_sky_appearance appearance;
	int initialized;
	int changed;

	int failed;
	GLuint program;
	GLuint vertex_array;
	struct
	{
		GLint ndc_sign, direction, step_x, step_y, zenith, horizon, ground, sun, sun_color, params, tint, tint_only;
	} uniforms;
} skyfx;

/* ---------- private code */

static void skyfx_print(const char *format, ...) __attribute__((format(printf, 1, 2)));

static void skyfx_print(const char *format, ...)
{
	va_list arguments;

	fputs("halo-linux: sky: ", stdout);
	va_start(arguments, format);
	vprintf(format, arguments);
	va_end(arguments);
	fputc('\n', stdout);
	fflush(stdout);
}

static GLuint skyfx_compile(GLenum type, const char *body, const char *name)
{
	const char *sources[2] = { SKYFX_SHADER_VERSION, body };
	GLuint shader = glCreateShader(type);
	GLint compiled = GL_FALSE;

	glShaderSource(shader, 2, sources, NULL);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
	if (!compiled)
	{
		char log[4096];

		log[0] = '\0';
		glGetShaderInfoLog(shader, sizeof(log), NULL, log);
		skyfx_print("the %s shader does not compile:\n%s", name, log);
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

/* the program and its vertex array, once; FALSE when they do not build */
static int skyfx_gl_initialize(void)
{
	GLuint vertex, fragment;
	GLint linked = GL_FALSE;

	if (skyfx.program)
		return TRUE;
	if (skyfx.failed)
		return FALSE;
	skyfx.failed = TRUE;
	vertex = skyfx_compile(GL_VERTEX_SHADER, skyfx_vertex_body, "sky vertex");
	fragment = skyfx_compile(GL_FRAGMENT_SHADER, skyfx_fragment_body, "sky fragment");
	if (!vertex || !fragment)
		return FALSE;
	skyfx.program = glCreateProgram();
	glAttachShader(skyfx.program, vertex);
	glAttachShader(skyfx.program, fragment);
	glLinkProgram(skyfx.program);
	glDeleteShader(vertex);
	glDeleteShader(fragment);
	glGetProgramiv(skyfx.program, GL_LINK_STATUS, &linked);
	if (!linked)
	{
		char log[4096];

		log[0] = '\0';
		glGetProgramInfoLog(skyfx.program, sizeof(log), NULL, log);
		skyfx_print("the sky program does not link:\n%s", log);
		skyfx.program = 0;
		return FALSE;
	}
	glGenVertexArrays(1, &skyfx.vertex_array);
#define LOCATE(field, name) skyfx.uniforms.field = glGetUniformLocation(skyfx.program, name)
	LOCATE(ndc_sign, "u_ndc_sign");
	LOCATE(direction, "u_direction");
	LOCATE(step_x, "u_step_x");
	LOCATE(step_y, "u_step_y");
	LOCATE(zenith, "u_zenith");
	LOCATE(horizon, "u_horizon");
	LOCATE(ground, "u_ground");
	LOCATE(sun, "u_sun");
	LOCATE(sun_color, "u_sun_color");
	LOCATE(params, "u_params");
	LOCATE(tint, "u_tint");
	LOCATE(tint_only, "u_tint_only");
#undef LOCATE
	skyfx.failed = FALSE;
	return TRUE;
}

static void skyfx_uniform4(GLint location, float x, float y, float z, float w)
{
	float value[4];

	value[0] = x;
	value[1] = y;
	value[2] = z;
	value[3] = w;
	glUniform4fv(location, 1, value);
}

static void skyfx_use_defaults(void)
{
	if (!skyfx.initialized)
	{
		halo_sky_default_appearance(&skyfx.appearance);
		skyfx.initialized = TRUE;
	}
}

/* ---------- public code */

void halo_sky_default_appearance(struct halo_sky_appearance *appearance)
{
	memset(appearance, 0, sizeof(*appearance));
	appearance->tint[0] = appearance->tint[1] = appearance->tint[2] = 1.0f;
	appearance->zenith[0] = 0.16f;
	appearance->zenith[1] = 0.40f;
	appearance->zenith[2] = 0.85f;
	appearance->horizon[0] = 0.66f;
	appearance->horizon[1] = 0.80f;
	appearance->horizon[2] = 0.95f;
	appearance->ground[0] = 0.30f;
	appearance->ground[1] = 0.33f;
	appearance->ground[2] = 0.30f;
	appearance->sun_elevation = 45.0f;
	appearance->sun_azimuth = 200.0f;
	appearance->sun_color[0] = 1.0f;
	appearance->sun_color[1] = 0.95f;
	appearance->sun_color[2] = 0.80f;
	appearance->sun_size = 1.0f;
	appearance->stars = 1.0f;
	appearance->clouds = 0.35f;
	appearance->cloud_speed = 1.0f;
}

void halo_sky_set_appearance(const struct halo_sky_appearance *appearance)
{
	struct halo_sky_appearance defaults;

	if (!appearance)
	{
		halo_sky_default_appearance(&defaults);
		appearance = &defaults;
	}
	SDL_LockSpinlock(&skyfx.lock);
	skyfx.appearance = *appearance;
	skyfx.initialized = TRUE;
	skyfx.changed = appearance->procedural || appearance->tint_amount > 0.001f;
	SDL_UnlockSpinlock(&skyfx.lock);
}

void halo_sky_get_appearance(struct halo_sky_appearance *appearance)
{
	SDL_LockSpinlock(&skyfx.lock);
	skyfx_use_defaults();
	*appearance = skyfx.appearance;
	SDL_UnlockSpinlock(&skyfx.lock);
}

int halo_sky_changed(void)
{
	return skyfx.changed;
}

int skyfx_draw(const struct halo_sky_view *view, const int viewport[4], int after_sky)
{
	struct halo_sky_appearance look;
	GLint previous_vertex_array = 0;
	float elevation, azimuth;
	float seconds = (float)SDL_GetTicks() / 1000.0f;

	if (!skyfx.changed || viewport[2] <= 0 || viewport[3] <= 0)
		return FALSE;
	halo_sky_get_appearance(&look);
	if (after_sky ? look.tint_amount <= 0.001f : !look.procedural)
		return FALSE;
	if (!skyfx_gl_initialize())
		return FALSE;

	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous_vertex_array);
	glBindVertexArray(skyfx.vertex_array);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_SCISSOR_TEST);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
	if (after_sky)
	{
		/* the tint multiplies what is there */
		glEnable(GL_BLEND);
		glBlendEquation(GL_FUNC_ADD);
		glBlendFunc(GL_DST_COLOR, GL_ZERO);
	}
	else
	{
		glDisable(GL_BLEND);
	}

	glUseProgram(skyfx.program);
	glUniform1f(skyfx.uniforms.ndc_sign, SKYFX_NDC_SIGN);
	glUniform1f(skyfx.uniforms.tint_only, after_sky ? 1.0f : 0.0f);
	skyfx_uniform4(skyfx.uniforms.tint, look.tint[0], look.tint[1], look.tint[2], look.tint_amount);
	if (!after_sky)
	{
		elevation = look.sun_elevation * (3.14159265f / 180.0f);
		azimuth = look.sun_azimuth * (3.14159265f / 180.0f);
		skyfx_uniform4(skyfx.uniforms.direction, view->direction[0], view->direction[1], view->direction[2], 0.0f);
		skyfx_uniform4(skyfx.uniforms.step_x, view->step_x[0], view->step_x[1], view->step_x[2], 0.0f);
		skyfx_uniform4(skyfx.uniforms.step_y, view->step_y[0], view->step_y[1], view->step_y[2], 0.0f);
		skyfx_uniform4(skyfx.uniforms.zenith, look.zenith[0], look.zenith[1], look.zenith[2], 1.0f);
		skyfx_uniform4(skyfx.uniforms.horizon, look.horizon[0], look.horizon[1], look.horizon[2], 1.0f);
		skyfx_uniform4(skyfx.uniforms.ground, look.ground[0], look.ground[1], look.ground[2], 1.0f);
		skyfx_uniform4(skyfx.uniforms.sun, cosf(elevation) * cosf(azimuth), cosf(elevation) * sinf(azimuth),
			sinf(elevation), look.sun_size > 0.05f ? look.sun_size : 0.05f);
		skyfx_uniform4(skyfx.uniforms.sun_color, look.sun_color[0], look.sun_color[1], look.sun_color[2], 1.0f);
		skyfx_uniform4(skyfx.uniforms.params, look.stars, look.clouds, look.cloud_speed, seconds);
	}
	glDrawArrays(GL_TRIANGLES, 0, 3);

	glBindVertexArray((GLuint)previous_vertex_array);
	return !after_sky;
}
