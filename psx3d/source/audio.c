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
#define TARGET_FILL  3072         // ~70 ms of latency
#define MAX_ADJUST   0.005

static int16_t *ring;             // stereo interleaved, linear memory
static ndspWaveBuf wbuf;
static bool active, started;
static u32 wpos, last_ppos;
static double base_rate;
static double avg_fill;   // smoothed buffer level for rate control

// resampler state: last three input frames and the fractional read position
static float rs_hist[3][2];
static float rs_pos;

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
	// polyphase is the DSP's best resampler (44.1kHz -> its 32.7kHz output);
	// linear adds audible grit on high frequencies
	ndspChnSetInterp(0, NDSP_INTERP_POLYPHASE);
	ndspChnSetRate(0, (float)base_rate);
	ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);

	float mix[12] = { 1.0f, 1.0f };
	ndspChnSetMix(0, mix);

	memset(&wbuf, 0, sizeof(wbuf));
	wbuf.data_pcm16 = ring;
	wbuf.nsamples = RING_FRAMES;
	wbuf.looping = true;

	wpos = TARGET_FILL;  // start with a cushion of silence
	avg_fill = TARGET_FILL;
	memset(rs_hist, 0, sizeof(rs_hist));
	rs_pos = 0;
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

// Cubic (Hermite) resampler for the small rate corrections. Keeps the last
// three input frames between calls so the output is continuous.

static inline float hermite(float xm1, float x0, float x1, float x2, float t)
{
	float c1 = 0.5f * (x1 - xm1);
	float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
	float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
	return ((c3 * t + c2) * t + c1) * t + x0;
}

static inline int16_t clamp16(float v)
{
	if (v > 32767.0f) return 32767;
	if (v < -32768.0f) return -32768;
	return (int16_t)lrintf(v);
}

static size_t resample(const int16_t *in, size_t frames, float step, int16_t *out, size_t max_out)
{
	static float src[(4096 + 3) * 2];
	size_t len = frames + 3, n = 0;

	if (frames > 4096)
		frames = 4096, len = frames + 3;
	for (int i = 0; i < 3; i++) {
		src[i * 2] = rs_hist[i][0];
		src[i * 2 + 1] = rs_hist[i][1];
	}
	for (size_t i = 0; i < frames; i++) {
		src[(i + 3) * 2] = in[i * 2];
		src[(i + 3) * 2 + 1] = in[i * 2 + 1];
	}
	// interpolate between src[k+1] and src[k+2] with neighbours src[k], src[k+3]
	while (n < max_out) {
		int k = (int)rs_pos;
		if ((size_t)k + 3 >= len)
			break;
		float t = rs_pos - k;
		for (int c = 0; c < 2; c++)
			out[n * 2 + c] = clamp16(hermite(src[k * 2 + c], src[(k + 1) * 2 + c],
				src[(k + 2) * 2 + c], src[(k + 3) * 2 + c], t));
		n++;
		rs_pos += step;
	}
	rs_pos -= frames;
	for (int i = 0; i < 3; i++) {
		rs_hist[i][0] = src[(len - 3 + i) * 2];
		rs_hist[i][1] = src[(len - 3 + i) * 2 + 1];
	}
	return n;
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
		avg_fill = TARGET_FILL;
		st.laps++;
	}
	st.calls++;
	st.frames_in += frames;
	if (fill < st.min_fill) st.min_fill = fill;
	if (fill > st.max_fill) st.max_fill = fill;
	if (fill < 256)
		st.starved_writes++;

	// Dynamic rate control, done on the CPU: the DSP rate never changes
	// (changing it makes the DSP click). Steer by a slow average of the fill
	// level - the raw level saws by a frame of audio on every write.
	avg_fill += ((double)fill - avg_fill) * 0.02;
	double err = (avg_fill - TARGET_FILL) / TARGET_FILL;
	if (err > 1.0) err = 1.0;
	if (err < -1.0) err = -1.0;
	float step = (float)(1.0 + err * MAX_ADJUST);   // input frames per output frame
	st.rate = (float)(base_rate * step);

	static int16_t out[4096 * 2];
	size_t nout = resample((const int16_t *)data, frames, step, out, sizeof(out) / 4);

	size_t space = RING_FRAMES - 256 - fill;
	size_t n = nout < space ? nout : space;  // drop the rest on overflow
	st.dropped += nout - n;
	u32 start = wpos;
	for (size_t i = 0; i < n; i++) {
		((u32 *)ring)[wpos] = ((const u32 *)out)[i];
		wpos = (wpos + 1) % RING_FRAMES;
	}
	if (n)
		flush_range(start, n);

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
