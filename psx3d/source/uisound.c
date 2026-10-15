// Interface sounds and the library music (see tools/make_sounds.py).
// NDSP channel 0 belongs to the game; the UI uses 1 (music) and 2-3 (effects).
// Without DSP firmware the same sounds go through CSND (csnd_out.c): the
// game takes the first two CSND channels, music the next two (left/right),
// effects the two after that.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psx3d.h"

#define CH_MUSIC 1
#define CH_SFX0  2
#define SFX_RATE 32000.0f

typedef struct { s16 *data; u32 frames; } clip_t;

static clip_t clips[SFX_COUNT], music;
static s16 *music_l, *music_r;     // CSND: the music split into two mono channels
static ndspWaveBuf sfx_wb[2], music_wb;
static int next_sfx;
static bool ok, csnd, music_on;
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
	if (csnd)
		GSPGPU_FlushDataCache(c->data, size);
	else
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

// CSND plays mono only: split the interleaved music once
static void split_music(void)
{
	music_l = linearAlloc(music.frames * 2);
	music_r = linearAlloc(music.frames * 2);
	if (!music_l || !music_r) {
		if (music_l) linearFree(music_l);
		if (music_r) linearFree(music_r);
		music_l = music_r = NULL;
		return;
	}
	for (u32 i = 0; i < music.frames; i++) {
		music_l[i] = music.data[i * 2];
		music_r[i] = music.data[i * 2 + 1];
	}
	GSPGPU_FlushDataCache(music_l, music.frames * 2);
	GSPGPU_FlushDataCache(music_r, music.frames * 2);
}

void uisound_init(void)
{
	static const char *const names[SFX_COUNT] = { "move", "select", "back", "launch" };
	csnd = false;
	if (sw_csnd || R_FAILED(ndspInit())) {
		if (!csnd_acquire()) {
			log_printf("sound: no NDSP and no CSND, UI is silent\n");
			return;
		}
		csnd = true;
	}
	log_printf("sound: UI uses %s\n", csnd ? "CSND (no DSP firmware)" : "NDSP");
	for (int i = 0; i < SFX_COUNT; i++) {
		char path[64];
		snprintf(path, sizeof(path), "romfs:/sfx/%s.pcm", names[i]);
		load(path, &clips[i], 1);
	}
	if (load("romfs:/sfx/music.pcm", &music, 2) && csnd)
		split_music();
	ok = true;
}

void uisound_exit(void)
{
	if (!ok)
		return;
	if (csnd) {
		for (int i = 2; i < 6; i++)
			if (csnd_channel(i) >= 0)
				CSND_SetPlayState(csnd_channel(i), 0);
		csndExecCmds(true);
	} else {
		for (int ch = CH_MUSIC; ch <= CH_SFX0 + 1; ch++)
			ndspChnWaveBufClear(ch);
	}
	for (int i = 0; i < SFX_COUNT; i++)
		if (clips[i].data)
			linearFree(clips[i].data);
	if (music.data)
		linearFree(music.data);
	if (music_l) linearFree(music_l);
	if (music_r) linearFree(music_r);
	music_l = music_r = NULL;
	if (csnd)
		csnd_release();
	else
		ndspExit();
	ok = false;
}

void sfx_play(int which)
{
	if (!ok || !g_cfg.sounds || which < 0 || which >= SFX_COUNT || !clips[which].data)
		return;
	if (csnd) {
		int chn = csnd_channel(4 + next_sfx);
		next_sfx ^= 1;
		if (chn < 0)
			return;
		csnd_setup(chn, false, (u32)SFX_RATE, 0.8f, 0.0f, clips[which].data, clips[which].frames * 2);
		csndExecCmds(false);
		return;
	}
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
	if (!ok || !music.data || (csnd && !music_l))
		return;
	on = on && g_cfg.music;
	if (on && !music_on) {
		if (csnd) {
			int l = csnd_channel(2), r = csnd_channel(3);
			if (l < 0 || r < 0)
				return;
			csnd_setup(l, true, (u32)SFX_RATE, 0.0f, -1.0f, music_l, music.frames * 2);
			csnd_setup(r, true, (u32)SFX_RATE, 0.0f, 1.0f, music_r, music.frames * 2);
			csndExecCmds(true);
		} else {
			setup_channel(CH_MUSIC, true, 0.0f);
			memset(&music_wb, 0, sizeof(music_wb));
			music_wb.data_pcm16 = music.data;
			music_wb.nsamples = music.frames;
			music_wb.looping = true;
			ndspChnWaveBufAdd(CH_MUSIC, &music_wb);
		}
		music_vol = 0.0f;
		music_target = 0.45f;
	} else if (!on && music_on) {
		if (csnd) {
			CSND_SetPlayState(csnd_channel(2), 0);
			CSND_SetPlayState(csnd_channel(3), 0);
			csndExecCmds(true);
		} else {
			ndspChnWaveBufClear(CH_MUSIC);
		}
	}
	music_on = on;
}

// call once per UI frame: eases the music volume
void music_update(void)
{
	if (!ok || !music_on)
		return;
	float before = music_vol;
	music_vol += (music_target - music_vol) * 0.03f;
	if (csnd) {
		// only send volume changes that are audible: each is a CSND command
		if ((int)(before * 200) != (int)(music_vol * 200)) {
			csnd_set_volume(csnd_channel(2), music_vol, -1.0f);
			csnd_set_volume(csnd_channel(3), music_vol, 1.0f);
			csndExecCmds(false);
		}
		return;
	}
	float mix[12] = { music_vol, music_vol };
	ndspChnSetMix(CH_MUSIC, mix);
}
