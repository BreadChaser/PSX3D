// Input: 3DS buttons sit in the same physical places as the PS1 face buttons
// (A=Circle, B=Cross, X=Triangle, Y=Square), so the mapping is 1:1.
// The circle pad doubles as the d-pad for digital games and is also the
// left analog stick; the New 3DS C-stick is the right stick.
#include "psx3d.h"

static u32 held, down;
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
}

u32 input_keys_down(void) { return down; }
u32 input_keys_held(void) { return held; }

static u16 joypad_mask(void)
{
	u16 mask = 0;
	for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
		if (held & map[i].key)
			mask |= 1 << map[i].id;

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
