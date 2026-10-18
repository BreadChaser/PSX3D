// Frontend-only resampling; no SPU state or DSP playback-rate changes.
#ifndef AUDIO_RESAMPLE_H
#define AUDIO_RESAMPLE_H

#include <stddef.h>
#include <stdint.h>

// A chunk at the minimum step (0.495) fits comfortably in 4096 output frames.
#define AUDIO_CHUNK 1024
#define AUDIO_OUT 4096

typedef struct {
	int16_t hist[6];
	float pos;
	float slow;
	float deficit;
	float window_seconds;
	size_t window_frames;
} audio_resampler;

// Estimate the game's production rate in quarter-second windows. Queue
// pressure enables slowdown; a slew limit makes pitch changes gradual.
float audio_resample_step(audio_resampler *rs, float queue_error,
	int stretch_percent, size_t frames, float sample_rate, float seconds);
size_t audio_resample(audio_resampler *rs, const int16_t *in, size_t frames,
	float step, int16_t *out);

#endif
