// Frontend-only resampling; no SPU state or DSP playback-rate changes.
#ifndef AUDIO_RESAMPLE_H
#define AUDIO_RESAMPLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Drift correction changes the step by at most 0.5%, so a chunk's output
// fits easily in AUDIO_OUT frames.
#define AUDIO_CHUNK 1024
#define AUDIO_OUT 4096
// Saved setting: audio_stretch is 0 (off) or this value (on); any nonzero
// value means on. Kept at 5 so v0.4.x/v0.5.0 configuration files still match.
#define AUDIO_STRETCH_MAX 5
// When the game runs slow, sound is made up to this many percent longer
// without changing its pitch (audio_tsm.c). Slower than that it runs dry.
#define AUDIO_TSM_MAX 60

typedef struct {
	int16_t hist[6];
	float pos;
	float slow;
	float deficit;
	float window_seconds;
	size_t window_frames;
} audio_resampler;

// Estimate the game's production rate in quarter-second windows. Queue
// pressure enables stretch (rs->slow, the share of output that is repeated
// sound); the returned step is only the +-0.5% drift correction.
// queue_now: the queue right now, as a share of its target.
float audio_resample_step(audio_resampler *rs, float queue_error, float queue_now,
	int stretch_percent, size_t frames, float sample_rate, float seconds);
size_t audio_resample(audio_resampler *rs, const int16_t *in, size_t frames,
	float step, int16_t *out);
#endif
