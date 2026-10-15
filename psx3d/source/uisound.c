// Interface sounds and the library music (see tools/make_sounds.py).
// NDSP channel 0 belongs to the game; the UI uses 1 (music) and 2-3 (effects).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psx3d.h"

#define CH_MUSIC 1
#define CH_SFX0  2
#define SFX_RATE 32000.0f

typedef struct { s16 *data; u32 frames; } clip_t;

static clip_t clips[SFX_COUNT], music;
static ndspWaveBuf sfx_wb[2], music_wb;
static int next_sfx;
static bool ok, music_on;
static float music_vol, music_target;

static bool load(const char *path, clip_t *c, int channels)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	c->data = linearAlloc(size);
	if (!c->data || fread(c->data, 1, size, f) != (size_t)size) {
		fclose(f);
		return false;
	}
	fclose(f);
	c->frames = size / (2 * channels);
	DSP_FlushDataCache(c->data, size);
	return true;
}

static void setup_channel(int ch, bool stereo, float vol)
{
	ndspChnReset(ch);
	ndspChnSetInterp(ch, NDSP_INTERP_POLYPHASE);
	ndspChnSetRate(ch, SFX_RATE);
	ndspChnSetFormat(ch, stereo ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
	float mix[12] = { vol, vol };
	ndspChnSetMix(ch, mix);
}

void uisound_init(void)
{
	static const char *const names[SFX_COUNT] = { "move", "select", "back", "launch" };
	if (R_FAILED(ndspInit()))
		return;   // no dspfirm.cdc: silent UI
	for (int i = 0; i < SFX_COUNT; i++) {
		char path[64];
		snprintf(path, sizeof(path), "romfs:/sfx/%s.pcm", names[i]);
		load(path, &clips[i], 1);
	}
	load("romfs:/sfx/music.pcm", &music, 2);
	ok = true;
}

void uisound_exit(void)
{
	if (!ok)
		return;
	for (int ch = CH_MUSIC; ch <= CH_SFX0 + 1; ch++)
		ndspChnWaveBufClear(ch);
	for (int i = 0; i < SFX_COUNT; i++)
		if (clips[i].data)
			linearFree(clips[i].data);
	if (music.data)
		linearFree(music.data);
	ndspExit();
	ok = false;
}

void sfx_play(int which)
{
	if (!ok || !g_cfg.sounds || which < 0 || which >= SFX_COUNT || !clips[which].data)
		return;
	int ch = CH_SFX0 + next_sfx;
	next_sfx ^= 1;
	setup_channel(ch, false, 0.8f);
	ndspWaveBuf *wb = &sfx_wb[ch - CH_SFX0];
	memset(wb, 0, sizeof(*wb));
	wb->data_pcm16 = clips[which].data;
	wb->nsamples = clips[which].frames;
	ndspChnWaveBufAdd(ch, wb);
}

// start (fading in) or stop the library music
void music_play(bool on)
{
	if (!ok || !music.data)
		return;
	on = on && g_cfg.music;
	if (on && !music_on) {
		setup_channel(CH_MUSIC, true, 0.0f);
		memset(&music_wb, 0, sizeof(music_wb));
		music_wb.data_pcm16 = music.data;
		music_wb.nsamples = music.frames;
		music_wb.looping = true;
		ndspChnWaveBufAdd(CH_MUSIC, &music_wb);
		music_vol = 0.0f;
		music_target = 0.45f;
	} else if (!on && music_on) {
		ndspChnWaveBufClear(CH_MUSIC);
	}
	music_on = on;
}

// call once per UI frame: eases the music volume
void music_update(void)
{
	if (!ok || !music_on)
		return;
	music_vol += (music_target - music_vol) * 0.03f;
	float mix[12] = { music_vol, music_vol };
	ndspChnSetMix(CH_MUSIC, mix);
}
