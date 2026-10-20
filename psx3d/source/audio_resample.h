// Frontend-only resampling; no SPU state or DSP playback-rate changes.
#ifndef AUDIO_RESAMPLE_H
#define AUDIO_RESAMPLE_H

#include <stddef.h>
#include <stdint.h>

// A chunk at the minimum step plus one repeat fits easily in 4096 output frames.
#define AUDIO_CHUNK 1024
#define AUDIO_OUT 4096
// Audio stretch slows sound (and lowers its pitch) by at most this many
// percent when the game runs slow: just under a semitone (5.9%), enough that
// slow scenes don't sound frozen. Up to 50% made voices and music sound deep.
// The rest of a shortfall is filled by audio_repeat_tail() at normal pitch.
#define AUDIO_STRETCH_MAX 5
// audio_repeat_tail() plays the last AUDIO_REPEAT frames (11.6 ms) again,
// joined by an AUDIO_FADE-frame crossfade so the seam doesn't click.
#define AUDIO_REPEAT 512
#define AUDIO_FADE 64

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
// Append the last AUDIO_REPEAT of n frames once more (out needs room for
// n + AUDIO_REPEAT). Pitch stays; returns the new count, or n if too short.
size_t audio_repeat_tail(int16_t *out, size_t n);

#endif
