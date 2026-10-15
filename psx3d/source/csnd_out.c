// Sound without the DSP firmware: the older CSND service plays PCM straight
// from memory with no firmware at all. Used only when NDSP can't start
// (no dspfirm.cdc and none could be copied from the HOME Menu).
//
// CSND plays mono channels, so stereo is two channels panned left and
// right, each looping over its own ring buffer. This libctru doesn't report
// how far a channel has played, so the position is worked out from the
// clock: the CSND sample timer and the CPU tick come from the same crystal.
#include <string.h>
#include "psx3d.h"

#define CSND_CLOCK 67027964.0     // CSND sample timer clock (Hz)

static int refs;
static bool ok;

bool csnd_acquire(void)
{
	if (refs++ == 0)
		ok = R_SUCCEEDED(csndInit());
	if (!ok)
		refs--;
	return ok;
}

void csnd_release(void)
{
	if (refs > 0 && --refs == 0 && ok) {
		csndExit();
		ok = false;
	}
}

// the n-th channel this app may use, or -1
int csnd_channel(int n)
{
	for (int c = 0; c < 32; c++)
		if (csndChannels & BIT(c))
			if (n-- == 0)
				return c;
	return -1;
}

// the rate a channel really plays at for a requested rate
double csnd_real_rate(u32 rate)
{
	u32 timer = CSND_TIMER(rate);
	if (timer < 0x42) timer = 0x42;
	if (timer > 0xffff) timer = 0xffff;
	return CSND_CLOCK / timer;
}

// set up (not start) a looping or one-shot 16-bit channel
void csnd_setup(int chn, bool loop, u32 rate, float vol, float pan, const s16 *data, u32 bytes)
{
	u32 timer = CSND_TIMER(rate);
	if (timer < 0x42) timer = 0x42;
	if (timer > 0xffff) timer = 0xffff;
	u32 flags = SOUND_FORMAT_16BIT | (loop ? SOUND_REPEAT : SOUND_ONE_SHOT) |
		SOUND_ENABLE | SOUND_CHANNEL(chn) | (timer << 16);
	u32 phys = osConvertVirtToPhys(data);
	u32 v = CSND_VOL(vol, pan);
	CSND_SetChnRegs(flags, phys, phys, bytes, v, v);
	if (loop)
		CSND_SetBlock(chn, 1, phys, bytes);
}

void csnd_set_volume(int chn, float vol, float pan)
{
	u32 v = CSND_VOL(vol, pan);
	CSND_SetVol(chn, v, v);
}
