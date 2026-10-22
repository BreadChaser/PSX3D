#include <math.h>
#include <stdbool.h>
#include <string.h>
#include "audio_resample.h"

float audio_resample_step(audio_resampler *rs, float err, float now, int percent,
	size_t frames, float sample_rate, float seconds)
{
	if (err > 1.0f) err = 1.0f;
	if (err < -1.0f) err = -1.0f;
	if (percent < 0) percent = 0;
	if (percent > AUDIO_TSM_MAX) percent = AUDIO_TSM_MAX;
	if (seconds < 0) seconds = 0;
	// SPU worker callbacks can be bunched together. Measure their combined
	// input over time, rather than mistaking each short gap for full speed.
	rs->window_frames += frames;
	rs->window_seconds += seconds;
	if (rs->window_seconds >= 0.25f) {
		float supplied = rs->window_frames / (sample_rate * rs->window_seconds);
		float deficit = supplied < 1.0f ? 1.0f - supplied : 0;
		rs->deficit += (deficit - rs->deficit) * 0.25f;
		rs->window_frames = 0;
		rs->window_seconds = 0;
	}
	float want = 0;
	// Only a half-empty queue starts stretching. Supply measurements alone
	// should never change the sound while the full-speed cushion is healthy.
	// Once stretching, follow the measured shortfall and steer the queue back
	// to its target (pitch no longer depends on it, so no need to hold back).
	// The supply estimate lags by about a second, so a queue above its
	// target right now cuts the stretch harder than one below raises it.
	if (percent && (err < -0.5f || rs->slow > 0) && rs->deficit > 0.02f) {
		float e = now - 1 > err ? now - 1 : err;
		want = rs->deficit - e * (e > 0 ? 0.5f : 0.2f);
	}
	// Under a third of the cushion left right now: stretch enough to rebuild
	// it, as after a short stall or before the supply estimate has caught
	// up, whatever the measured supply says; all the way when nearly empty.
	if (percent && now < 1.0f / 3 && want < 0.3f)
		want = 0.3f;
	if (percent && now < 1.0f / 6)
		want = 1;
	if (want > percent * 0.01f)
		want = percent * 0.01f;
	// Pitch stays the same, so the stretch may grow quickly, and at once
	// when the queue is nearly empty. While the queue is below its target it shrinks by at most 50
	// percentage points per wall-clock second, so a long I/O stall's gap
	// doesn't end it on the next callback.
	if (seconds > 0.1f) seconds = 0.1f;
	float d = want - rs->slow;
	if (d > seconds * 2 && now >= 1.0f / 3) d = seconds * 2;
	if (d < -seconds * 0.5f && now < 1) d = -seconds * 0.5f;
	rs->slow = percent ? rs->slow + d : 0;
	return 1.0f + err * 0.005f;
}

size_t audio_resample(audio_resampler *rs, const int16_t *in, size_t frames,
	float step, int16_t *out)
{
	int16_t src[(AUDIO_CHUNK + 3) * 2];
	size_t n = 0;
	// The caller splits long batches, so no input or fractional phase is lost.
	memcpy(src, rs->hist, sizeof(rs->hist));
	memcpy(src + 6, in, frames * 4);
	// Normal drift correction: Q20 phase gives <0.5 ppm step
	// quantization; integer Hermite preserves high frequencies which a
	// linear kernel would attenuate. Doubled coefficients and Q12 weights
	// need only 32-bit multiplies (largest product < 12*32768*4096).
	// Keep the same two-sample history delay and the existing slow path.
	const int one = 1 << 20;
	int pos = (int)(rs->pos * one);
	int delta = (int)(step * one + 0.5f);
	if (delta == one && !(pos & (one - 1))) {
		int k = pos >> 20;
		n = frames > (size_t)k ? frames - k : 0;
		memcpy(out, src + (k + 1) * 2, n * 4);
		pos += n * one;
	} else {
		while ((size_t)(pos >> 20) < frames) {
			int k = pos >> 20, t = (pos & (one - 1)) >> 8;
			for (int c = 0; c < 2; c++) {
				int xm = src[k * 2 + c], a = src[(k + 1) * 2 + c];
				int b = src[(k + 2) * 2 + c], x2 = src[(k + 3) * 2 + c];
				int c1 = b - xm, c2 = 2*xm - 5*a + 4*b - x2;
				int c3 = x2 - xm + 3*(a - b);
				int v = ((c3*t + 2048) >> 12) + c2;
				v = ((v*t + 2048) >> 12) + c1;
				v = a + ((v*t + 4096) >> 13);
				if (v > 32767) v = 32767;
				if (v < -32768) v = -32768;
				out[n * 2 + c] = v;
			}
			n++;
			pos += delta;
		}
	}
	rs->pos = (pos - (int)(frames * one)) / (float)one;
	memcpy(rs->hist, src + frames * 2, sizeof(rs->hist));
	return n;
}
