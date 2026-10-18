#include <math.h>
#include <string.h>
#include "audio_resample.h"

float audio_resample_step(audio_resampler *rs, float err, int percent,
	size_t frames, float sample_rate, float seconds)
{
	if (err > 1.0f) err = 1.0f;
	if (err < -1.0f) err = -1.0f;
	if (percent < 0) percent = 0;
	if (percent > 50) percent = 50;
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
	// Only a half-empty queue starts slowdown. Supply measurements alone
	// should never bend pitch while the normal full-speed cushion is healthy.
	if (percent && (err < -0.5f || rs->slow > 0) && rs->deficit > 0.02f && err < 0) {
		want = rs->deficit;
		if (err < -0.5f) want += 0.01f; // slowly rebuild a depleted cushion
		float limit = percent * 0.01f;
		if (want > limit) want = limit;
	}
	// Limit pitch movement to five percentage points per wall-clock second.
	// A long I/O stall must not cause a sudden jump on the next callback.
	if (seconds > 0.1f) seconds = 0.1f;
	float limit = seconds * 0.05f;
	float d = want - rs->slow;
	if (d > limit) d = limit;
	if (d < -limit) d = -limit;
	rs->slow = percent ? rs->slow + d : 0;
	return 1.0f + err * 0.005f - rs->slow;
}

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

size_t audio_resample(audio_resampler *rs, const int16_t *in, size_t frames,
	float step, int16_t *out)
{
	int16_t src[(AUDIO_CHUNK + 3) * 2];
	size_t n = 0;
	// The caller splits long batches, so no input or fractional phase is lost.
	memcpy(src, rs->hist, sizeof(rs->hist));
	memcpy(src + 6, in, frames * 4);
	if (step < 0.99f) {
		// Stretch can double the output. Linear interpolation in Q15 keeps
		// that cheaper than the normal Hermite drift correction. The largest
		// product is 65535*32767 + 16384, which fits a signed 32-bit int.
		int pos = (int)(rs->pos * 32768.0f);
		int delta = (int)(step * 32768.0f + 0.5f);
		while ((size_t)(pos >> 15) < frames) {
			int k = pos >> 15, t = pos & 32767;
			for (int c = 0; c < 2; c++) {
				int a = src[(k + 1) * 2 + c], b = src[(k + 2) * 2 + c];
				out[n * 2 + c] = a + (((b - a) * t + 16384) >> 15);
			}
			n++;
			pos += delta;
		}
		rs->pos = (pos - (int)(frames * 32768)) / 32768.0f;
	} else {
		while ((size_t)(int)rs->pos < frames) {
			int k = (int)rs->pos;
			float t = rs->pos - k;
			for (int c = 0; c < 2; c++)
				out[n * 2 + c] = clamp16(hermite(src[k * 2 + c], src[(k + 1) * 2 + c],
					src[(k + 2) * 2 + c], src[(k + 3) * 2 + c], t));
			n++;
			rs->pos += step;
		}
		rs->pos -= frames;
	}
	memcpy(rs->hist, src + frames * 2, sizeof(rs->hist));
	return n;
}
