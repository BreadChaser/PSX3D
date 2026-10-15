// Audio: one looping NDSP wave buffer used as a ring. We write ahead of the
// DSP's play cursor and nudge the playback rate (+/-0.5%) to keep the amount
// of buffered audio steady. That absorbs the tiny difference between the
// PS1's 59.94Hz and the 3DS screen's ~59.83Hz without crackles or resampling
// on the CPU. Already-played samples are zeroed so an underrun is silence
// instead of a stutter loop.
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
		((u32 *)ring)[p] = 0;
		p = (p + 1) % RING_FRAMES;
	}
	if (ppos != last_ppos)
		flush_range(last_ppos, (ppos - last_ppos + RING_FRAMES) % RING_FRAMES);
	last_ppos = ppos;
}

size_t audio_write(const int16_t *data, size_t frames)
{
	if (!active)
		return frames;

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
	}

	size_t space = RING_FRAMES - 256 - fill;
	size_t n = frames < space ? frames : space;  // drop the rest on overflow
	u32 start = wpos;
	for (size_t i = 0; i < n; i++) {
		((u32 *)ring)[wpos] = ((const u32 *)data)[i];
		wpos = (wpos + 1) % RING_FRAMES;
	}
	if (n)
		flush_range(start, n);

	// dynamic rate control around the target fill level
	double err = ((double)fill + n - TARGET_FILL) / TARGET_FILL;
	if (err > 1.0) err = 1.0;
	if (err < -1.0) err = -1.0;
	ndspChnSetRate(0, (float)(base_rate * (1.0 + err * MAX_ADJUST)));

	return frames;
}
