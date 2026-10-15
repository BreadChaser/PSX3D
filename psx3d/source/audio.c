// Audio: one looping NDSP wave buffer used as a ring. We write ahead of the
// DSP's play cursor and nudge the playback rate (+/-0.5%) to keep the amount
// of buffered audio steady. That absorbs the tiny difference between the
// PS1's 59.94Hz and the 3DS screen's ~59.83Hz without crackles or resampling
// on the CPU. Already-played samples are zeroed so an underrun is silence
// instead of a stutter loop.
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "psx3d.h"

#define RING_FRAMES  8192         // ~186 ms at 44.1 kHz
#define TARGET_FILL  2048         // ~46 ms of latency
#define MAX_ADJUST   0.005

static int16_t *ring;             // stereo interleaved, linear memory
static ndspWaveBuf wbuf;
static bool active, started;
static u32 wpos, last_ppos;
static double base_rate;

// debug capture: what the core produced vs what the DSP actually read
#define CAP_FRAMES (44100 * 10)
static u32 *cap_core, *cap_played;
static u32 cap_core_n, cap_played_n;
static bool capturing;

// debug: replace the game's audio with a 440Hz tone to test playback alone
static bool test_tone;
static double tone_phase;

// per-interval diagnostics, read and reset by audio_stats()
static struct {
	u32 calls, frames_in, dropped, laps, min_fill, max_fill, starved_writes, repeats, nonzero;
	float rate;
} st = { .min_fill = ~0u };

bool audio_init(double sample_rate)
{
	if (R_FAILED(ndspInit())) {
		log_printf("ndspInit failed - is sdmc:/3ds/dspfirm.cdc present?\n");
		return false;
	}
	ring = linearAlloc(RING_FRAMES * 4);
	if (!ring) {
		ndspExit();
		return false;
	}
	memset(ring, 0, RING_FRAMES * 4);
	DSP_FlushDataCache(ring, RING_FRAMES * 4);

	base_rate = sample_rate;
	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	ndspChnReset(0);
	ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
	ndspChnSetRate(0, (float)base_rate);
	ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);

	float mix[12] = { 1.0f, 1.0f };
	ndspChnSetMix(0, mix);

	memset(&wbuf, 0, sizeof(wbuf));
	wbuf.data_pcm16 = ring;
	wbuf.nsamples = RING_FRAMES;
	wbuf.looping = true;

	wpos = TARGET_FILL;  // start with a cushion of silence
	last_ppos = 0;
	started = false;
	active = true;
	return true;
}

void audio_exit(void)
{
	if (!active)
		return;
	ndspChnWaveBufClear(0);
	ndspExit();
	linearFree(ring);
	ring = NULL;
	active = false;
}

void audio_pause(bool paused)
{
	if (active && started)
		ndspChnSetPaused(0, paused);
}

static void flush_range(u32 start, u32 frames)
{
	if (start + frames <= RING_FRAMES) {
		DSP_FlushDataCache(ring + start * 2, frames * 4);
	} else {
		u32 first = RING_FRAMES - start;
		DSP_FlushDataCache(ring + start * 2, first * 4);
		DSP_FlushDataCache(ring, (frames - first) * 4);
	}
}

// zero everything the DSP has played since last time
static void clear_played(u32 ppos)
{
	u32 n = (ppos - last_ppos + RING_FRAMES) % RING_FRAMES;
	u32 p = last_ppos;
	while (n--) {
		if (capturing && cap_played_n < CAP_FRAMES)
			cap_played[cap_played_n++] = ((u32 *)ring)[p];
		((u32 *)ring)[p] = 0;
		p = (p + 1) % RING_FRAMES;
	}
	if (ppos != last_ppos)
		flush_range(last_ppos, (ppos - last_ppos + RING_FRAMES) % RING_FRAMES);
	last_ppos = ppos;
}

static void capture_finish(void)
{
	FILE *f;
	capturing = false;
	if ((f = fopen(PSX3D_DIR "/audio_core.raw", "wb"))) {
		fwrite(cap_core, 4, cap_core_n, f);
		fclose(f);
	}
	if ((f = fopen(PSX3D_DIR "/audio_played.raw", "wb"))) {
		fwrite(cap_played, 4, cap_played_n, f);
		fclose(f);
	}
	log_printf("audio capture written: core %lu frames, played %lu frames\n",
		cap_core_n, cap_played_n);
	free(cap_core);
	free(cap_played);
	cap_core = cap_played = NULL;
}

bool audio_capture_start(void)
{
	if (capturing || !active)
		return false;
	cap_core = malloc(CAP_FRAMES * 4);
	cap_played = malloc(CAP_FRAMES * 4);
	if (!cap_core || !cap_played) {
		free(cap_core);
		free(cap_played);
		cap_core = cap_played = NULL;
		return false;
	}
	cap_core_n = cap_played_n = 0;
	capturing = true;
	log_printf("audio capture started\n");
	return true;
}

bool audio_test_tone_toggle(void)
{
	test_tone = !test_tone;
	return test_tone;
}

size_t audio_write(const int16_t *data, size_t frames)
{
	static int16_t tone_buf[4096];

	if (!active)
		return frames;

	if (test_tone && frames <= 2048) {
		for (size_t i = 0; i < frames; i++) {
			int16_t v = (int16_t)(8000 * sin(tone_phase));
			tone_buf[i * 2] = tone_buf[i * 2 + 1] = v;
			tone_phase += 2 * M_PI * 440.0 / 44100.0;
			if (tone_phase > 2 * M_PI)
				tone_phase -= 2 * M_PI;
		}
		data = tone_buf;
	}

	{
		// stair-stepping detector: share of samples identical to the previous one
		static u32 prev;
		const u32 *d = (const u32 *)data;
		for (size_t i = 0; i < frames; i++) {
			if (d[i]) {
				st.nonzero++;
				st.repeats += d[i] == prev;
			}
			prev = d[i];
		}
	}

	if (capturing) {
		for (size_t i = 0; i < frames && cap_core_n < CAP_FRAMES; i++)
			cap_core[cap_core_n++] = ((const u32 *)data)[i];
		if (cap_core_n >= CAP_FRAMES && cap_played_n >= CAP_FRAMES - 4096)
			capture_finish();
	}

	if (!started) {
		ndspChnWaveBufAdd(0, &wbuf);
		started = true;
	}

	u32 ppos = ndspChnGetSamplePos(0) % RING_FRAMES;
	clear_played(ppos);

	u32 fill = (wpos - ppos + RING_FRAMES) % RING_FRAMES;
	if (fill > RING_FRAMES - 256) {
		// the DSP lapped us (long stall) - restart from the play cursor
		wpos = (ppos + TARGET_FILL) % RING_FRAMES;
		fill = TARGET_FILL;
		st.laps++;
	}
	st.calls++;
	st.frames_in += frames;
	if (fill < st.min_fill) st.min_fill = fill;
	if (fill > st.max_fill) st.max_fill = fill;
	if (fill < 256)
		st.starved_writes++;

	size_t space = RING_FRAMES - 256 - fill;
	size_t n = frames < space ? frames : space;  // drop the rest on overflow
	st.dropped += frames - n;
	u32 start = wpos;
	for (size_t i = 0; i < n; i++) {
		((u32 *)ring)[wpos] = ((const u32 *)data)[i];
		wpos = (wpos + 1) % RING_FRAMES;
	}
	if (n)
		flush_range(start, n);

	// dynamic rate control around the target fill level
	double err = ((double)fill - TARGET_FILL) / TARGET_FILL;
	if (err > 1.0) err = 1.0;
	if (err < -1.0) err = -1.0;
	st.rate = (float)(base_rate * (1.0 + err * MAX_ADJUST));
	ndspChnSetRate(0, st.rate);

	return frames;
}

void audio_stats(char *buf, size_t len)
{
	if (!active) {
		snprintf(buf, len, "audio off");
		return;
	}
	snprintf(buf, len, "audio in=%lu/%lu calls fill=%lu..%lu (target %d) starved=%lu laps=%lu drop=%lu rate=%.0f repeat=%.1f%%",
		st.frames_in, st.calls, st.min_fill == ~0u ? 0 : st.min_fill, st.max_fill,
		TARGET_FILL, st.starved_writes, st.laps, st.dropped, st.rate,
		st.nonzero ? st.repeats * 100.0 / st.nonzero : 0.0);
	memset(&st, 0, sizeof(st));
	st.min_fill = ~0u;
}
