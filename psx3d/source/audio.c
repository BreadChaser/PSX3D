// Audio: a chain of small NDSP wave buffers. The DSP plays each one start to
// finish and marks it done; we only ever refill buffers it has finished, so
// the CPU and DSP never touch the same memory at the same time. (A single
// looping ring written just ahead of the DSP's reported position clicked
// audibly on a pure test tone.)
//
// The PS1 runs at 59.94Hz and the 3DS screen at ~59.83Hz, so the amount of
// queued audio slowly drifts. A 4-point Hermite resampler on the CPU speeds
// playback up or down by at most 0.5% to keep the queue at its target; the
// DSP's own rate is never changed, because changing it makes the DSP click.
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "psx3d.h"

#define NBUF         16
#define BUF_FRAMES   512            // 11.6 ms each, 186 ms in total
#define TARGET_QUEUE 3072           // ~70 ms of queued audio
#define MAX_ADJUST   0.005

static int16_t *bufmem;             // NBUF * BUF_FRAMES stereo frames, linear memory
static ndspWaveBuf wb[NBUF];
static int cur;                     // buffer being filled
static u32 cur_fill;                // frames written into it so far
static bool active;
static double base_rate;
static double avg_queue;            // smoothed queue level for rate control

// resampler state: last three input frames and the fractional read position
static float rs_hist[3][2];
static float rs_pos;

// debug capture: what the core produced vs what we handed to the DSP
#define CAP_FRAMES (44100 * 30)
#define CAP_OUT_FRAMES (32728 * 35)   // DSP output runs at ~32728Hz; margin so it never wraps
static u32 *cap_core, *cap_played, *cap_out;
static ndspWaveBuf cap_out_wb;
static u32 cap_core_n, cap_played_n;
static bool capturing;

// debug: replace the game's audio with a 440Hz tone to test playback alone
static bool test_tone;
static double tone_phase;

// per-interval diagnostics, read and reset by audio_stats()
static struct {
	u32 calls, frames_in, dropped, underruns, min_q, max_q, repeats, nonzero;
	u64 max_gap;
	float rate;
} st = { .min_q = ~0u };

static bool buf_free(int i)
{
	return wb[i].status == NDSP_WBUF_FREE || wb[i].status == NDSP_WBUF_DONE;
}

bool audio_init(double sample_rate)
{
	if (R_FAILED(ndspInit())) {
		log_printf("ndspInit failed - is sdmc:/3ds/dspfirm.cdc present?\n");
		return false;
	}
	bufmem = linearAlloc(NBUF * BUF_FRAMES * 4);
	if (!bufmem) {
		ndspExit();
		return false;
	}
	memset(bufmem, 0, NBUF * BUF_FRAMES * 4);

	base_rate = sample_rate;
	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	ndspChnReset(0);
	// polyphase is the DSP's best resampler (44.1kHz -> its 32.7kHz output)
	ndspChnSetInterp(0, NDSP_INTERP_POLYPHASE);
	ndspChnSetRate(0, (float)base_rate);
	ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);

	float mix[12] = { 1.0f, 1.0f };
	ndspChnSetMix(0, mix);

	memset(wb, 0, sizeof(wb));
	for (int i = 0; i < NBUF; i++) {
		wb[i].data_pcm16 = bufmem + i * BUF_FRAMES * 2;
		wb[i].nsamples = BUF_FRAMES;
	}
	cur = 0;
	cur_fill = 0;
	avg_queue = TARGET_QUEUE;
	memset(rs_hist, 0, sizeof(rs_hist));
	rs_pos = 0;
	active = true;

	// start with a cushion of silence so the first frames don't underrun
	for (int i = 0; i < TARGET_QUEUE / BUF_FRAMES; i++) {
		DSP_FlushDataCache(wb[i].data_pcm16, BUF_FRAMES * 4);
		ndspChnWaveBufAdd(0, &wb[i]);
	}
	cur = TARGET_QUEUE / BUF_FRAMES;
	return true;
}

void audio_exit(void)
{
	if (!active)
		return;
	ndspChnWaveBufClear(0);
	ndspExit();
	linearFree(bufmem);
	bufmem = NULL;
	active = false;
}

void audio_pause(bool paused)
{
	if (active)
		ndspChnSetPaused(0, paused);
}

// frames queued for the DSP and not yet played
static u32 queued_frames(void)
{
	u32 q = 0;
	for (int i = 0; i < NBUF; i++)
		if (wb[i].status == NDSP_WBUF_QUEUED || wb[i].status == NDSP_WBUF_PLAYING)
			q += BUF_FRAMES;
	u32 played = ndspChnIsPlaying(0) ? ndspChnGetSamplePos(0) : 0;
	return q > played ? q - played : 0;
}

static void capture_finish(void)
{
	FILE *f;
	capturing = false;
	ndspSetCapture(NULL);
	if ((f = fopen(PSX3D_DIR "/audio_dspout.raw", "wb"))) {
		fwrite(cap_out, 4, cap_out_wb.offset, f);
		fclose(f);
	}
	if ((f = fopen(PSX3D_DIR "/audio_core.raw", "wb"))) {
		fwrite(cap_core, 4, cap_core_n, f);
		fclose(f);
	}
	if ((f = fopen(PSX3D_DIR "/audio_played.raw", "wb"))) {
		fwrite(cap_played, 4, cap_played_n, f);
		fclose(f);
	}
	log_printf("audio capture written: core %lu frames, played %lu frames, dsp out %lu frames\n",
		cap_core_n, cap_played_n, cap_out_wb.offset);
	free(cap_core);
	free(cap_played);
	free(cap_out);
	cap_core = cap_played = cap_out = NULL;
}

bool audio_capture_start(void)
{
	if (capturing || !active)
		return false;
	cap_core = malloc(CAP_FRAMES * 4);
	cap_played = malloc(CAP_FRAMES * 4);
	cap_out = malloc(CAP_OUT_FRAMES * 4);
	if (!cap_core || !cap_played || !cap_out) {
		free(cap_core);
		free(cap_played);
		free(cap_out);
		cap_core = cap_played = cap_out = NULL;
		return false;
	}
	cap_core_n = cap_played_n = 0;
	// the DSP's final mixed output, i.e. what reaches the speakers
	memset(&cap_out_wb, 0, sizeof(cap_out_wb));
	cap_out_wb.data_pcm16 = (s16 *)cap_out;
	cap_out_wb.nsamples = CAP_OUT_FRAMES;
	ndspSetCapture(&cap_out_wb);
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
	size_t len, n = 0;

	if (frames > 4096)
		frames = 4096;
	len = frames + 3;
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

// append frames to the current buffer, submitting buffers as they fill up
static void submit(const u32 *frames, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		if (cur_fill == 0 && !buf_free(cur)) {
			st.dropped += n - i;   // every buffer is queued: we're far ahead
			return;
		}
		((u32 *)wb[cur].data_pcm16)[cur_fill++] = frames[i];
		if (capturing && cap_played_n < CAP_FRAMES)
			cap_played[cap_played_n++] = frames[i];
		if (cur_fill == BUF_FRAMES) {
			DSP_FlushDataCache(wb[cur].data_pcm16, BUF_FRAMES * 4);
			ndspChnWaveBufAdd(0, &wb[cur]);
			cur = (cur + 1) % NBUF;
			cur_fill = 0;
		}
	}
}

size_t audio_write(const int16_t *data, size_t frames)
{
	static int16_t tone_buf[4096 * 2];
	static int16_t out[4096 * 2];

	if (!active)
		return frames;

	if (test_tone && frames <= 4096) {
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
	{
		// longest time between two writes
		static u64 last_t;
		u64 t = svcGetSystemTick();
		if (last_t && t - last_t > st.max_gap)
			st.max_gap = t - last_t;
		last_t = t;
	}

	if (capturing) {
		for (size_t i = 0; i < frames && cap_core_n < CAP_FRAMES; i++)
			cap_core[cap_core_n++] = ((const u32 *)data)[i];
		if (cap_core_n >= CAP_FRAMES && cap_played_n >= CAP_FRAMES - 4096)
			capture_finish();
	}

	u32 q = queued_frames() + cur_fill;
	st.calls++;
	st.frames_in += frames;
	if (q < st.min_q) st.min_q = q;
	if (q > st.max_q) st.max_q = q;
	if (!ndspChnIsPlaying(0))
		st.underruns++;   // the DSP ran out of buffers (it restarts on the next one)

	// steer by a slow average: the queue level saws by a frame of audio per write
	avg_queue += ((double)q - avg_queue) * 0.02;
	double err = (avg_queue - TARGET_QUEUE) / TARGET_QUEUE;
	if (err > 1.0) err = 1.0;
	if (err < -1.0) err = -1.0;
	float step = (float)(1.0 + err * MAX_ADJUST);   // input frames per output frame
	st.rate = (float)(base_rate * step);

	size_t nout = resample((const int16_t *)data, frames, step, out, sizeof(out) / 4);
	submit((const u32 *)out, nout);
	return frames;
}

// how full the queue is, 0-100, with the target level at 50
unsigned audio_occupancy(void)
{
	if (!active)
		return 50;
	u32 q = queued_frames() + cur_fill;
	u32 pct = q * 50 / TARGET_QUEUE;
	return pct > 100 ? 100 : pct;
}

void audio_stats(char *buf, size_t len)
{
	if (!active) {
		snprintf(buf, len, "audio off");
		return;
	}
	snprintf(buf, len, "audio in=%lu/%lu calls queue=%lu..%lu (target %d) underruns=%lu drop=%lu rate=%.0f repeat=%.1f%% maxgap=%.1fms",
		st.frames_in, st.calls, st.min_q == ~0u ? 0 : st.min_q, st.max_q,
		TARGET_QUEUE, st.underruns, st.dropped, st.rate,
		st.nonzero ? st.repeats * 100.0 / st.nonzero : 0.0,
		st.max_gap / (double)CPU_TICKS_PER_MSEC);
	memset(&st, 0, sizeof(st));
	st.min_q = ~0u;
}
