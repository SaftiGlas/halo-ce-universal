/*
HALO_MOD.H

What the native ports offer source mods (mods/, tools/mod_overlay.py), so
that mods need not change the game's files to run or to read keys: a mod
adds its own unit, registers its hooks with HALO_MOD_REGISTER, and reads
the keyboard with halo_mod_key_down. Several mods then work together
without touching the same file.

Shared by the game, through halo_linux_source_fixups.h, and the platform
layer (port/linux/src/xinput_sdl.c). The hooks are called by
port/linux/game/mods.c.
*/

#ifndef __HALO_MOD_H
#define __HALO_MOD_H

/* ---------- hooks */

struct halo_mod
{
	char const *name;
	/* once a frame while a game is in progress and the console is closed,
	before the dev tools (forge_update); may be NULL */
	void (*update)(void);
	/* once a frame over the other overlays, before the dev tools
	(forge_render); may be NULL */
	void (*render)(void);
};

/* at most this many mods run; more are refused with a message on stderr */
#define HALO_MOD_MAXIMUM_COUNT 32

void halo_mod_register(struct halo_mod const *mod);
void halo_mods_update(void);
void halo_mods_render(void);

/* registers a struct halo_mod of the unit before the game starts */
#define HALO_MOD_REGISTER(mod) \
	static void __attribute__((constructor)) halo_mod_register_##mod(void) \
	{ \
		halo_mod_register(&(mod)); \
	}

/* ---------- keys */

/* keyboard keys: SDL scancodes, which are the USB HID usage numbers
(xinput_sdl.c checks that they agree) */
enum
{
	HALO_MOD_KEY_A = 4,
	HALO_MOD_KEY_B,
	HALO_MOD_KEY_C,
	HALO_MOD_KEY_D,
	HALO_MOD_KEY_E,
	HALO_MOD_KEY_F,
	HALO_MOD_KEY_G,
	HALO_MOD_KEY_H,
	HALO_MOD_KEY_I,
	HALO_MOD_KEY_J,
	HALO_MOD_KEY_K,
	HALO_MOD_KEY_L,
	HALO_MOD_KEY_M,
	HALO_MOD_KEY_N,
	HALO_MOD_KEY_O,
	HALO_MOD_KEY_P,
	HALO_MOD_KEY_Q,
	HALO_MOD_KEY_R,
	HALO_MOD_KEY_S,
	HALO_MOD_KEY_T,
	HALO_MOD_KEY_U,
	HALO_MOD_KEY_V,
	HALO_MOD_KEY_W,
	HALO_MOD_KEY_X,
	HALO_MOD_KEY_Y,
	HALO_MOD_KEY_Z,
	HALO_MOD_KEY_1,
	HALO_MOD_KEY_2,
	HALO_MOD_KEY_3,
	HALO_MOD_KEY_4,
	HALO_MOD_KEY_5,
	HALO_MOD_KEY_6,
	HALO_MOD_KEY_7,
	HALO_MOD_KEY_8,
	HALO_MOD_KEY_9,
	HALO_MOD_KEY_0,
	HALO_MOD_KEY_RETURN,
	HALO_MOD_KEY_ESCAPE,
	HALO_MOD_KEY_BACKSPACE,
	HALO_MOD_KEY_TAB,
	HALO_MOD_KEY_SPACE,
	HALO_MOD_KEY_F1 = 58,
	HALO_MOD_KEY_F2,
	HALO_MOD_KEY_F3,
	HALO_MOD_KEY_F4,
	HALO_MOD_KEY_F5,
	HALO_MOD_KEY_F6,
	HALO_MOD_KEY_F7,
	HALO_MOD_KEY_F8,
	HALO_MOD_KEY_F9,
	HALO_MOD_KEY_F10,
	HALO_MOD_KEY_F11,
	HALO_MOD_KEY_F12,
	HALO_MOD_KEY_INSERT = 73,
	HALO_MOD_KEY_HOME,
	HALO_MOD_KEY_PAGE_UP,
	HALO_MOD_KEY_DELETE,
	HALO_MOD_KEY_END,
	HALO_MOD_KEY_PAGE_DOWN,
	HALO_MOD_KEY_RIGHT,
	HALO_MOD_KEY_LEFT,
	HALO_MOD_KEY_DOWN,
	HALO_MOD_KEY_UP,
	HALO_MOD_KEY_LEFT_CTRL = 224,
	HALO_MOD_KEY_LEFT_SHIFT,
	HALO_MOD_KEY_LEFT_ALT,
	HALO_MOD_KEY_RIGHT_CTRL = 228,
	HALO_MOD_KEY_RIGHT_SHIFT,
	HALO_MOD_KEY_RIGHT_ALT,

	/* either side */
	HALO_MOD_KEY_CTRL = 0x1000,
	HALO_MOD_KEY_SHIFT,
	HALO_MOD_KEY_ALT
};

/* TRUE while the key is held; always FALSE while the console is open */
int halo_mod_key_down(int key);
/* the key's name, as the keyboard labels it ("F5", "Delete", "Ctrl") */
char const *halo_mod_key_name(int key);
/* from now on, while either Ctrl is held, key reaches neither the game's
controller 1 (so Ctrl+C does not also crouch) nor the dev tools */
void halo_mod_ctrl_shortcut(int key);

/* a key that works when it goes down, not while it is held */
struct halo_mod_key_state
{
	int down;
};

/* TRUE on the frame the key (or, with ctrl, Ctrl and the key) goes down;
ctrl FALSE ignores Ctrl, ctrl TRUE needs it */
int halo_mod_key_pressed(struct halo_mod_key_state *state, int key, int ctrl);

/* ---------- drawing, from a render hook */

/* screen coordinates: 640x480 for one player, whatever the window's size */

enum
{
	HALO_MOD_TEXT_LEFT = 0,
	HALO_MOD_TEXT_RIGHT,
	HALO_MOD_TEXT_CENTER
};

/* local player 0's view; FALSE when there is none (menus, loading) */
int halo_mod_screen(short *x0, short *y0, short *x1, short *y1);
/* the terminal font, or with large the map's largest font that holds
ordinary text and still fits 18 lines on the screen; NONE (-1) when no map
is loaded */
long halo_mod_font(int large);
short halo_mod_line_height(long font);
short halo_mod_text_width(long font, char const *text);
/* text in the box, colour 0xAARRGGBB */
void halo_mod_draw_text(long font, short x0, short y0, short x1, short y1, int justification,
	unsigned long argb, char const *text);
/* a filled box, colour 0xAARRGGBB, blended by its alpha */
void halo_mod_draw_box(short x0, short y0, short x1, short y1, unsigned long argb);

#endif
