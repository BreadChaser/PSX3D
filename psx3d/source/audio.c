// Audio: a chain of small NDSP wave buffers. The DSP plays each one start to
// finish and marks it done; we only ever refill buffers it has finished, so
// the CPU and DSP never touch the same memory at the same time. (A single
// looping ring written just ahead of the DSP's reported position clicked
// audibly on a pure test tone.)
//
// The PS1 runs at 59.94Hz and the 3DS screen at ~59.83Hz, so the amount of
// queued audio slowly drifts. A fixed-point cubic resampler on the CPU speeds
// playback up or down by at most 0.5% to keep the queue at its target. When
// emulation falls behind, optional frontend stretching slows sound further; the
// DSP's own rate is never changed, because changing it makes the DSP click.
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "psx3d.h"
#include "statsview.h"
#include "audio_resample.h"

#define NBUF         16
#define BUF_FRAMES   512            // 11.6 ms each, 186 ms in total
#define TARGET_QUEUE 3072           // ~70 ms of queued audio

static int16_t *bufmem;             // NBUF * BUF_FRAMES stereo frames, linear memory
static ndspWaveBuf wb[NBUF];
static int cur;                     // buffer being filled
static u32 cur_fill;                // frames written into it so far
static bool active;
static double base_rate;
static double avg_queue;            // smoothed queue level for rate control

// resampler state: last three input frames and the fractional read position
static audio_resampler rs;
static u64 stretch_tick;

// debug capture: what the core produced vs what we handed to the DSP
#define CAP_FRAMES (44100 * 30)
#define CAP_OUT_FRAMES (32728 * 35)   // DSP output runs at ~32728Hz; margin so it never wraps
static u32 *cap_core, *cap_played, *cap_out;
static ndspWaveBuf cap_out_wb;
static u32 cap_core_n, cap_played_n;
static bool capturing;
static u32 hash, hash_left;   // test: checksum of the core's next frames

void audio_hash_start(u32 frames)
{
	hash = 2166136261u;
	hash_left = frames;
}

// debug: replace the game's audio with a 440Hz tone to test playback alone
static bool test_tone;
static double tone_phase;

// per-interval diagnostics, read and reset by audio_stats()
static struct {
	u32 calls, frames_in, dropped, underruns, min_q, max_q, repeats, nonzero;
	u64 max_gap;
	float rate;
} st = { .min_q = ~0u };

// No DSP firmware: CSND instead (csnd_out.c). A ring per channel; the
// position is reckoned from the clock, so "queued" is frames written minus
// frames that should have played by now.
#define RING 8192                   // frames, ~186 ms
static bool use_csnd;
static s16 *ring_l, *ring_r;
static int chn_l, chn_r;
static double csnd_rate;            // the rate CSND really plays at
static u64 csnd_t0;                 // tick at which frame 0 played
static u64 write_pos;               // frames written so far
static u64 pause_t;

static u64 csnd_played(void)
{
	u64 t = pause_t ? pause_t : svcGetSystemTick();
	return (u64)((double)(t - csnd_t0) * csnd_rate / SYSCLOCK_ARM11);
}

static bool csnd_start(double sample_rate)
{
	if (!csnd_acquire())
		return false;
	chn_l = csnd_channel(0);
	chn_r = csnd_channel(1);
	ring_l = linearAlloc(RING * 2);
	ring_r = linearAlloc(RING * 2);
	if (chn_l < 0 || chn_r < 0 || !ring_l || !ring_r) {
		if (ring_l) linearFree(ring_l);
		if (ring_r) linearFree(ring_r);
		ring_l = ring_r = NULL;
		csnd_release();
		return false;
	}
	memset(ring_l, 0, RING * 2);
	memset(ring_r, 0, RING * 2);
	GSPGPU_FlushDataCache(ring_l, RING * 2);
	GSPGPU_FlushDataCache(ring_r, RING * 2);
	csnd_rate = csnd_real_rate((u32)(sample_rate + 0.5));
	// both channels start in the same command batch, so they stay in step
	csnd_setup(chn_l, true, (u32)(sample_rate + 0.5), 1.0f, -1.0f, ring_l, RING * 2);
	csnd_setup(chn_r, true, (u32)(sample_rate + 0.5), 1.0f, 1.0f, ring_r, RING * 2);
	if (R_FAILED(csndExecCmds(true))) {
		linearFree(ring_l);
		linearFree(ring_r);
		ring_l = ring_r = NULL;
		csnd_release();
		return false;
	}
	csnd_t0 = svcGetSystemTick();
	pause_t = 0;
	write_pos = TARGET_QUEUE;       // a cushion of silence, like the NDSP path
	log_printf("audio: no DSP firmware, using CSND (channels %d/%d, %.1f Hz)\n", chn_l, chn_r, csnd_rate);
	return true;
}

static void csnd_stop(void)
{
	CSND_SetPlayState(chn_l, 0);
	CSND_SetPlayState(chn_r, 0);
	csndExecCmds(true);
	linearFree(ring_l);
	linearFree(ring_r);
	ring_l = ring_r = NULL;
	csnd_release();
}

static void csnd_submit(const u32 *frames, size_t n)
{
	u64 played = csnd_played();
	if (write_pos < played + 64) {   // ran dry: carry on a little ahead of the player
		if (statsview_collecting()) st.underruns++;
		write_pos = played + 512;
	}
	if (write_pos + n > played + RING - 256) {   // far ahead: drop rather than overwrite
		if (statsview_collecting()) st.dropped += n;
		return;
	}
	for (size_t i = 0; i < n; i++) {
		u32 f = frames[i];
		u32 at = (write_pos + i) % RING;
		ring_l[at] = (s16)(f & 0xffff);
		ring_r[at] = (s16)(f >> 16);
		if (capturing && cap_played_n < CAP_FRAMES)
			cap_played[cap_played_n++] = f;
	}
	// flush what was written (it may wrap around the end of the ring)
	u32 a = write_pos % RING, len = n;
	u32 first = len > RING - a ? RING - a : len;
	GSPGPU_FlushDataCache(ring_l + a, first * 2);
	GSPGPU_FlushDataCache(ring_r + a, first * 2);
	if (len > first) {
		GSPGPU_FlushDataCache(ring_l, (len - first) * 2);
		GSPGPU_FlushDataCache(ring_r, (len - first) * 2);
	}
	write_pos += n;
}

static bool buf_free(int i)
{
	return wb[i].status == NDSP_WBUF_FREE || wb[i].status == NDSP_WBUF_DONE;
}

bool audio_init(double sample_rate)
{
	use_csnd = false;
	if (sw_csnd || R_FAILED(ndspInit())) {
		if (!sw_csnd)
			log_printf("ndspInit failed (no sdmc:/3ds/dspfirm.cdc)\n");
		base_rate = sample_rate;
		avg_queue = TARGET_QUEUE;
		memset(&rs, 0, sizeof(rs));
		stretch_tick = 0;
		use_csnd = active = csnd_start(sample_rate);
		return active;
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
	memset(&rs, 0, sizeof(rs));
	stretch_tick = 0;
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
	if (use_csnd) {
		csnd_stop();
		active = false;
		return;
	}
	ndspChnWaveBufClear(0);
	ndspExit();
	linearFree(bufmem);
	bufmem = NULL;
	active = false;
}

void audio_pause(bool paused)
{
	stretch_tick = 0; // start a fresh supply estimate after a menu/suspend
	rs.window_frames = 0;
	rs.window_seconds = rs.deficit = rs.slow = 0;
	if (!active)
		return;
	if (use_csnd) {
		if (paused && !pause_t) {
			pause_t = svcGetSystemTick();
		} else if (!paused && pause_t) {
			csnd_t0 += svcGetSystemTick() - pause_t;   // the clock stood still meanwhile
			pause_t = 0;
		}
		CSND_SetPlayState(chn_l, !paused);
		CSND_SetPlayState(chn_r, !paused);
		csndExecCmds(true);
		return;
	}
	ndspChnSetPaused(0, paused);
}

// frames queued for the DSP and not yet played
static u32 queued_frames(void)
{
	if (use_csnd) {
		u64 played = csnd_played();
		return write_pos > played ? (u32)(write_pos - played) : 0;
	}
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
	if (capturing || !active || use_csnd)
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

// append frames to the current buffer, submitting buffers as they fill up
static void submit(const u32 *frames, size_t n)
{
	if (use_csnd) {
		csnd_submit(frames, n);
		return;
	}
	for (size_t at = 0; at < n; ) {
		if (!cur_fill && !buf_free(cur)) {
			if (statsview_collecting()) st.dropped += n - at;
			return;
		}
		size_t count = BUF_FRAMES - cur_fill;
		if (count > n - at) count = n - at;
		memcpy((u32 *)wb[cur].data_pcm16 + cur_fill, frames + at, count * 4);
		if (capturing && cap_played_n < CAP_FRAMES) {
			size_t captured = count;
			if (captured > CAP_FRAMES - cap_played_n) captured = CAP_FRAMES - cap_played_n;
			memcpy(cap_played + cap_played_n, frames + at, captured * 4);
			cap_played_n += captured;
		}
		cur_fill += count;
		at += count;
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
	static int16_t out[AUDIO_OUT * 2];

	// Hash/capture the core output before test tones or frontend resampling.
	if (hash_left) {
		for (size_t i = 0; i < frames && hash_left; i++, hash_left--)
			hash = (hash ^ ((const u32 *)data)[i]) * 16777619u;
		if (!hash_left)
			log_printf("test: audio hash %08lx\n", hash);
	}
	if (capturing) {
		for (size_t i = 0; i < frames && cap_core_n < CAP_FRAMES; i++)
			cap_core[cap_core_n++] = ((const u32 *)data)[i];
		if (cap_core_n >= CAP_FRAMES && cap_played_n >= CAP_FRAMES - 4096)
			capture_finish();
	}

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

	if (sw_stats || sw_prof || log_linked()) {
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
	if (statsview_collecting()) {
		// longest time between two writes
		static u64 last_t;
		if (!st.calls) last_t = 0;
		u64 t = svcGetSystemTick();
		if (last_t && t - last_t > st.max_gap)
			st.max_gap = t - last_t;
		last_t = t;
	}

	u32 q = queued_frames() + (use_csnd ? 0 : cur_fill);
	if (statsview_collecting()) {
		st.calls++;
		st.frames_in += frames;
		if (q < st.min_q) st.min_q = q;
		if (q > st.max_q) st.max_q = q;
		if (!use_csnd && !ndspChnIsPlaying(0))
			st.underruns++;   // DSP ran out of buffers; next buffer restarts it
	}

	// steer by a slow average: the queue level saws by a frame of audio per write
	avg_queue += ((double)q - avg_queue) * 0.02;
	u64 tick = svcGetSystemTick();
	float seconds = stretch_tick ? (tick - stretch_tick) / (float)SYSCLOCK_ARM11 : 0;
	stretch_tick = tick;
	float err = (float)((avg_queue - TARGET_QUEUE) / TARGET_QUEUE);
	float step = audio_resample_step(&rs, err, g_cfg.audio_stretch, frames, (float)base_rate, seconds);
	st.rate = (float)(base_rate * step);

	// The core may deliver a large batch after a worker sync. Stretching a
	// 4096-frame batch can exceed the old output buffer; consume it in chunks.
	for (size_t at = 0; at < frames; ) {
		size_t count = frames - at;
		if (count > AUDIO_CHUNK) count = AUDIO_CHUNK;
		size_t nout = audio_resample(&rs, data + at * 2, count, step, out);
		submit((const u32 *)out, nout);
		at += count;
	}
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
