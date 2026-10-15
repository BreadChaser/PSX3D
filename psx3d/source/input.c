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

static const struct { u32 key; unsigned id; } map[] = {
	{ KEY_A,      RETRO_DEVICE_ID_JOYPAD_A },
	{ KEY_B,      RETRO_DEVICE_ID_JOYPAD_B },
	{ KEY_X,      RETRO_DEVICE_ID_JOYPAD_X },
	{ KEY_Y,      RETRO_DEVICE_ID_JOYPAD_Y },
	{ KEY_L,      RETRO_DEVICE_ID_JOYPAD_L },
	{ KEY_R,      RETRO_DEVICE_ID_JOYPAD_R },
	{ KEY_ZL,     RETRO_DEVICE_ID_JOYPAD_L2 },
	{ KEY_ZR,     RETRO_DEVICE_ID_JOYPAD_R2 },
	{ KEY_START,  RETRO_DEVICE_ID_JOYPAD_START },
	{ KEY_SELECT, RETRO_DEVICE_ID_JOYPAD_SELECT },
	{ KEY_DUP,    RETRO_DEVICE_ID_JOYPAD_UP },
	{ KEY_DDOWN,  RETRO_DEVICE_ID_JOYPAD_DOWN },
	{ KEY_DLEFT,  RETRO_DEVICE_ID_JOYPAD_LEFT },
	{ KEY_DRIGHT, RETRO_DEVICE_ID_JOYPAD_RIGHT },
};

void input_poll(void)
{
	hidScanInput();
	held = hidKeysHeld();
	down = hidKeysDown();
	hidCircleRead(&cpad);
	irrstCstickRead(&cstick);
	hidTouchRead(&touch);
}

u32 input_keys_down(void) { return down; }
u32 input_keys_held(void) { return held; }

static u16 joypad_mask(void)
{
	u16 mask = 0;
	for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
		if (held & map[i].key)
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
static const tbutton_t tbuttons[] = {
	{   8,  60, 110, 110, "L2", RETRO_DEVICE_ID_JOYPAD_L2 },
	{ 202,  60, 110, 110, "R2", RETRO_DEVICE_ID_JOYPAD_R2 },
	{ 125,   6,  70,  30, "MENU", TB_MENU },
};

bool touch_buttons_active(void)
{
	return !g_new3ds;
}

static const tbutton_t *touched(void)
{
	if (!(held & KEY_TOUCH))
		return NULL;
	for (size_t i = 0; i < sizeof(tbuttons) / sizeof(tbuttons[0]); i++) {
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
	for (size_t i = 0; i < sizeof(tbuttons) / sizeof(tbuttons[0]); i++) {
		const tbutton_t *b = &tbuttons[i];
		u32 bg = b == hit ? C2D_Color32(70, 80, 120, 255) : C2D_Color32(28, 30, 42, 255);
		ui_rect(b->x, b->y, b->w, b->h, bg);
		float sz = b->id == TB_MENU ? 0.45f : 0.8f;
		ui_text(b->x + b->w / 2 - (b->id == TB_MENU ? 18 : 12), b->y + b->h / 2 - (b->id == TB_MENU ? 8 : 14),
			sz, C2D_Color32(150, 155, 175, 255), "%s", b->label);
	}
}
