// Pitch-preserving time stretch for slow emulation (frontend only).
//
// When the game makes less sound per second than the speakers play, the
// sound has to get longer. Resampling it (playing it slower) lowers the pitch;
// instead this repeats short pieces of the sound itself, at normal speed. Each
// jump goes back by about AUDIO_TSM_JUMP frames to the spot that best matches
// what follows (cross-correlation, so the waveform lines up in phase) and
// crossfades over AUDIO_TSM_FADE frames. Between jumps the input is copied
// unchanged, so at speed 1.0 the output is exactly the input.
#ifndef AUDIO_TSM_H
#define AUDIO_TSM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AUDIO_TSM_FADE 256          // crossfade/match length: 5.8 ms
#define AUDIO_TSM_JUMP 441          // jump back once 10 ms of slack built up
#define AUDIO_TSM_SEEK 224          // +-5 ms around it: one 100 Hz period
#define AUDIO_TSM_HIST 1024         // input kept behind the read position
#define AUDIO_TSM_BUF 4096          // frames; input arrives in <= 1024 pieces

typedef struct {
	int16_t buf[AUDIO_TSM_BUF * 2]; // stereo input, [0, end)
	int p, end;                     // read position, end of input
	int debt;                       // Q16 input frames read ahead of the speed
	int speed;                      // Q16 input frames per output frame, <= 1
	int jumps;                      // statistics: jumps made
} audio_tsm;

void audio_tsm_reset(audio_tsm *t);
// Speed 1.0 (65536) stops stretching once the last crossfade has been played.
static inline void audio_tsm_speed(audio_tsm *t, int speed_q16)
{
	t->speed = speed_q16 > 65536 ? 65536 : speed_q16 < 16384 ? 16384 : speed_q16;
}
// True while output differs from plain input: stretching, or held-back input.
static inline bool audio_tsm_busy(const audio_tsm *t)
{
	return t->speed < 65536 || t->p < t->end;
}
// Append n <= 1024 input frames.
void audio_tsm_put(audio_tsm *t, const int16_t *in, size_t n);
// Full speed: the caller plays these n <= 1024 frames itself; keep them only
// as history, so a later stretch can jump back right away.
static inline void audio_tsm_pass(audio_tsm *t, const int16_t *in, size_t n)
{
	audio_tsm_put(t, in, n);
	t->p = t->end;
}
// Produce up to max output frames (max >= AUDIO_TSM_FADE); 0 when the
// buffered input is used up (stretching keeps AUDIO_TSM_FADE frames back).
size_t audio_tsm_get(audio_tsm *t, int16_t *out, size_t max);

#endif
