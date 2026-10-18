// Input: 3DS buttons sit in the same physical places as the PS1 face buttons
// (A=Circle, B=Cross, X=Triangle, Y=Square), so the mapping is 1:1.
// The circle pad doubles as the d-pad for digital games and is also the
// left analog stick; the New 3DS C-stick is the right stick.
#include "psx3d.h"

static touchPosition touch;

static u32 held, down;
static u16 touch_mask(void);
static circlePosition cpad, cstick;

#define CPAD_DPAD_THRESHOLD 60

// remappable buttons (input_map_*); -1 = does nothing in the game
static const u32 remap_keys[INPUT_REMAP_N] = { KEY_A, KEY_B, KEY_X, KEY_Y, KEY_L, KEY_R, KEY_ZL, KEY_ZR, KEY_START, KEY_SELECT };
static const char *const remap_names[INPUT_REMAP_N] = { "A", "B", "X", "Y", "L", "R", "ZL", "ZR", "START", "SELECT" };
static const int remap_default[INPUT_REMAP_N] = {
	RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_B, RETRO_DEVICE_ID_JOYPAD_X, RETRO_DEVICE_ID_JOYPAD_Y,
	RETRO_DEVICE_ID_JOYPAD_L, RETRO_DEVICE_ID_JOYPAD_R, RETRO_DEVICE_ID_JOYPAD_L2, RETRO_DEVICE_ID_JOYPAD_R2,
	RETRO_DEVICE_ID_JOYPAD_START, RETRO_DEVICE_ID_JOYPAD_SELECT,
};
static int remap[INPUT_REMAP_N];
static bool remap_init;

// the PS1 buttons a 3DS button can be set to, in menu order
static const int ps1_ids[] = {
	RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_B, RETRO_DEVICE_ID_JOYPAD_X, RETRO_DEVICE_ID_JOYPAD_Y,
	RETRO_DEVICE_ID_JOYPAD_L, RETRO_DEVICE_ID_JOYPAD_R, RETRO_DEVICE_ID_JOYPAD_L2, RETRO_DEVICE_ID_JOYPAD_R2,
	RETRO_DEVICE_ID_JOYPAD_L3, RETRO_DEVICE_ID_JOYPAD_R3, RETRO_DEVICE_ID_JOYPAD_START, RETRO_DEVICE_ID_JOYPAD_SELECT, -1,
};
#define N_PS1 (int)(sizeof(ps1_ids) / sizeof(ps1_ids[0]))

const char *input_ps1_name(int id)
{
	switch (id) {
	case RETRO_DEVICE_ID_JOYPAD_A: return "Circle";
	case RETRO_DEVICE_ID_JOYPAD_B: return "Cross";
	case RETRO_DEVICE_ID_JOYPAD_X: return "Triangle";
	case RETRO_DEVICE_ID_JOYPAD_Y: return "Square";
	case RETRO_DEVICE_ID_JOYPAD_L: return "L1";
	case RETRO_DEVICE_ID_JOYPAD_R: return "R1";
	case RETRO_DEVICE_ID_JOYPAD_L2: return "L2";
	case RETRO_DEVICE_ID_JOYPAD_R2: return "R2";
	case RETRO_DEVICE_ID_JOYPAD_L3: return "L3";
	case RETRO_DEVICE_ID_JOYPAD_R3: return "R3";
	case RETRO_DEVICE_ID_JOYPAD_START: return "Start";
	case RETRO_DEVICE_ID_JOYPAD_SELECT: return "Select";
	}
	return "none";
}

static void remap_defaults(void)
{
	for (int i = 0; i < INPUT_REMAP_N; i++)
		remap[i] = remap_default[i];
	remap_init = true;
}

const char *input_map_key_name(int i) { return i >= 0 && i < INPUT_REMAP_N ? remap_names[i] : ""; }
int input_map_get(int i) { if (!remap_init) remap_defaults(); return remap[i]; }
bool input_map_is_default(int i) { return input_map_get(i) == remap_default[i]; }

void input_map_set(int i, int id)
{
	if (!remap_init) remap_defaults();
	if (i >= 0 && i < INPUT_REMAP_N)
		remap[i] = id;
}

// step a button's PS1 assignment forward/back through the list
void input_map_cycle(int i, int dir)
{
	int cur = 0;
	for (int k = 0; k < N_PS1; k++)
		if (ps1_ids[k] == input_map_get(i))
			cur = k;
	input_map_set(i, ps1_ids[(cur + dir + N_PS1) % N_PS1]);
}

void input_map_reset(void)
{
	remap_defaults();
}

static const struct { u32 key; unsigned id; } map[] = {
	{ KEY_DUP,    RETRO_DEVICE_ID_JOYPAD_UP },
	{ KEY_DDOWN,  RETRO_DEVICE_ID_JOYPAD_DOWN },
	{ KEY_DLEFT,  RETRO_DEVICE_ID_JOYPAD_LEFT },
	{ KEY_DRIGHT, RETRO_DEVICE_ID_JOYPAD_RIGHT },
};

static bool ff_on;

void input_poll(void)
{
	hidScanInput();
	held = hidKeysHeld();
	down = hidKeysDown();
	// SELECT + R switches fast-forward on and off
	if (((down & KEY_R) && (held & KEY_SELECT)) || ((down & KEY_SELECT) && (held & KEY_R)))
		ff_on = !ff_on;
	hidCircleRead(&cpad);
	irrstCstickRead(&cstick);
	hidTouchRead(&touch);
}

u32 input_keys_down(void) { return down; }
u32 input_keys_held(void) { return held; }

// Hotkeys: SELECT + R toggles fast-forward, SELECT + L rewinds while held.
// While one is held those buttons don't reach the game.
static u32 hotkey_mask(void)
{
	if (!(held & KEY_SELECT) || !(held & (KEY_L | KEY_R)))
		return 0;
	return KEY_SELECT | (held & (KEY_L | KEY_R));
}

bool input_fast_forward(void) { return ff_on; }
void input_fast_forward_off(void) { ff_on = false; }
bool input_rewind(void) { return (held & (KEY_SELECT | KEY_L)) == (KEY_SELECT | KEY_L); }

static u16 joypad_mask(void)
{
	u16 mask = 0;
	u32 game_held = held & ~hotkey_mask();
	if (!remap_init)
		remap_defaults();
	for (int i = 0; i < INPUT_REMAP_N; i++)
		if ((game_held & remap_keys[i]) && remap[i] >= 0)
			mask |= 1 << remap[i];
	for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
		if (game_held & map[i].key)
			mask |= 1 << map[i].id;

	mask |= touch_mask();

	// circle pad as d-pad (libctru's KEY_CPAD_* use a small threshold, use our own)
	if (cpad.dy >  CPAD_DPAD_THRESHOLD) mask |= 1 << RETRO_DEVICE_ID_JOYPAD_UP;
	if (cpad.dy < -CPAD_DPAD_THRESHOLD) mask |= 1 << RETRO_DEVICE_ID_JOYPAD_DOWN;
	if (cpad.dx < -CPAD_DPAD_THRESHOLD) mask |= 1 << RETRO_DEVICE_ID_JOYPAD_LEFT;
	if (cpad.dx >  CPAD_DPAD_THRESHOLD) mask |= 1 << RETRO_DEVICE_ID_JOYPAD_RIGHT;
	return mask;
}

static int16_t axis(int v)
{
	// 3DS pads report about +/-156; scale to the libretro +/-32767 range
	int r = v * 32767 / 150;
	if (r > 32767) r = 32767;
	if (r < -32768) r = -32768;
	return (int16_t)r;
}

int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
	if (port != 0)
		return 0;

	switch (device & RETRO_DEVICE_MASK) {
	case RETRO_DEVICE_JOYPAD: {
		u16 mask = joypad_mask();
		if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
			return (int16_t)mask;
		return id < 16 ? (mask >> id) & 1 : 0;
	}
	case RETRO_DEVICE_ANALOG:
		if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT)
			return id == RETRO_DEVICE_ID_ANALOG_X ? axis(cpad.dx) : axis(-cpad.dy);
		if (index == RETRO_DEVICE_INDEX_ANALOG_RIGHT)
			return id == RETRO_DEVICE_ID_ANALOG_X ? axis(cstick.dx) : axis(-cstick.dy);
		return 0;
	}
	return 0;
}

// ---------------------------------------------------------------- touch
// Old 3DS has no ZL/ZR (the PS1's L2/R2): offer them as touch buttons on
// the bottom screen, with a small MENU button. On New 3DS the whole bottom
// screen opens the menu.

typedef struct { int x, y, w, h; const char *label; unsigned id; } tbutton_t;

#define TB_MENU 0xff
static const tbutton_t normal_buttons[] = {
	{   8,  60, 110, 110, "L2", RETRO_DEVICE_ID_JOYPAD_L2 },
	{ 202,  60, 110, 110, "R2", RETRO_DEVICE_ID_JOYPAD_R2 },
	{ 125,   6,  70,  30, "MENU", TB_MENU },
};

static const tbutton_t detail_buttons[] = {
	{ 8, 184, 110, 30, "L2", RETRO_DEVICE_ID_JOYPAD_L2 },
	{ 202, 184, 110, 30, "R2", RETRO_DEVICE_ID_JOYPAD_R2 },
	{ 125, 184, 70, 30, "MENU", TB_MENU },
};
#define tbuttons (g_cfg.show_fps == 2 ? detail_buttons : normal_buttons)
#define BUTTON_COUNT 3

bool touch_buttons_active(void)
{
	return !g_new3ds;
}

static const tbutton_t *touched(void)
{
	if (!(held & KEY_TOUCH))
		return NULL;
	for (size_t i = 0; i < BUTTON_COUNT; i++) {
		const tbutton_t *b = &tbuttons[i];
		if (touch.px >= b->x && touch.px < b->x + b->w && touch.py >= b->y && touch.py < b->y + b->h)
			return b;
	}
	return NULL;
}

bool menu_touched(void)
{
	if (!(down & KEY_TOUCH))
		return false;
	if (!touch_buttons_active())
		return true;
	const tbutton_t *b = touched();
	return b && b->id == TB_MENU;
}

static u16 touch_mask(void)
{
	const tbutton_t *b;
	if (!touch_buttons_active() || !(b = touched()) || b->id == TB_MENU)
		return 0;
	return 1 << b->id;
}

void touch_buttons_draw(void)
{
	const tbutton_t *hit = touched();
	for (size_t i = 0; i < BUTTON_COUNT; i++) {
		const tbutton_t *b = &tbuttons[i];
		u32 bg = b == hit ? C2D_Color32(70, 80, 120, 255) : C2D_Color32(28, 30, 42, 255);
		ui_rect(b->x, b->y, b->w, b->h, bg);
		float sz = b->id == TB_MENU || g_cfg.show_fps == 2 ? 0.45f : 0.8f;
		ui_text(b->x + b->w / 2 - (b->id == TB_MENU ? 18 : 12), b->y + b->h / 2 - (b->id == TB_MENU || g_cfg.show_fps == 2 ? 8 : 14),
			sz, C2D_Color32(150, 155, 175, 255), "%s", b->label);
	}
}
