#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../psx3d/source/audio_resample.h"
#include "../psx3d/source/audio_tsm.h"

// Stereo sine at freq Hz, sample i of the endless stream.
static int16_t sine(double freq, long i) { return (int16_t)lrint(12000 * sin(2 * M_PI * freq / 44100 * i)); }

// Feed len frames of a sine through the stretch at a fixed speed in pieces of
// 1..735 frames; return the output (caller frees) and its length.
static int16_t *stretch_sine(double freq, long len, double speed, long *outlen)
{
	static audio_tsm t;
	audio_tsm_reset(&t);
	audio_tsm_speed(&t, (int)(speed * 65536 + 0.5));
	long cap = (long)(len / speed) + 8192, n = 0;
	int16_t *res = malloc(cap * 4), in[1024 * 2], out[AUDIO_OUT * 2];
	for (long at = 0; at < len; ) {
		long m = 1 + (at * 7919) % 735;
		if (m > len - at) m = len - at;
		for (long i = 0; i < m; i++)
			in[i * 2] = in[i * 2 + 1] = sine(freq, at + i);
		audio_tsm_put(&t, in, m);
		size_t got;
		while ((got = audio_tsm_get(&t, out, AUDIO_OUT))) {
			assert(n + (long)got <= cap);
			memcpy(res + n * 2, out, got * 4);
			n += got;
		}
		at += m;
	}
	*outlen = n;
	return res;
}

int main(void)
{
	audio_resampler rs = {0};
	// Full-speed production must never trigger stretch, including a short
	// queue dip. Sustained 60% production stretches by the missing 40%.
	for (int i = 0; i < 3600; i++)
		assert(audio_resample_step(&rs, -0.6f, -0.6f + 1, AUDIO_TSM_MAX, 735, 44100, 1.0f/60) > 0.996f);
	assert(rs.slow == 0);
	float step = 1;
	for (int i = 0; i < 1200; i++)
		step = audio_resample_step(&rs, -0.6f, -0.6f + 1, AUDIO_TSM_MAX, 735, 44100, 1.0f/36);
	// The pitch path only corrects drift; the stretch takes the shortfall
	// (0.4) plus a fifth of the queue error to refill the queue.
	assert(fabsf(step - 0.997f) < 0.0001f);
	assert(fabsf(rs.slow - 0.52f) < 0.01f);
	// Never more than the limit, and off means off.
	for (int i = 0; i < 1200; i++)
		audio_resample_step(&rs, -1, -1 + 1, AUDIO_TSM_MAX, 735, 44100, 1.0f/12);
	assert(fabsf(rs.slow - AUDIO_TSM_MAX * 0.01f) < 0.001f);
	assert(audio_resample_step(&rs, -1, -1 + 1, 0, 735, 44100, 1.0f/36) == 0.995f);
	assert(rs.slow == 0);
	// Bursty worker deliveries preserve the measured production rate.
	audio_resampler a = {0}, b = {0};
	for (int i = 0; i < 1200; i++) {
		audio_resample_step(&a, -0.6f, -0.6f + 1, AUDIO_TSM_MAX, 735, 44100, 1.0f/36);
		audio_resample_step(&b, -0.6f, -0.6f + 1, AUDIO_TSM_MAX, 367, 44100, 1.0f/36);
		audio_resample_step(&b, -0.6f, -0.6f + 1, AUDIO_TSM_MAX, 368, 44100, 0);
	}
	assert(fabsf(a.slow - b.slow) < 0.002f);
	// A queue that stays full cuts the stretch below the shortfall (0.4 - 0.5
	// * 0.5), and
	// full-speed supply ends it.
	for (int i = 0; i < 1200; i++)
		audio_resample_step(&a, 0.5f, 0.5f + 1, AUDIO_TSM_MAX, 735, 44100, 1.0f/36);
	assert(fabsf(a.slow - 0.15f) < 0.01f);
	for (int i = 0; i < 1200; i++)
		audio_resample_step(&a, 0, 0 + 1, AUDIO_TSM_MAX, 735, 44100, 1.0f/60);
	assert(a.slow == 0);
	// An almost empty queue at full supply (a stall) stretches by 30%.
	audio_resampler c = {0};
	for (int i = 0; i < 600; i++)
		audio_resample_step(&c, -0.8f, 0.3f, AUDIO_TSM_MAX, 735, 44100, 1.0f/60);
	assert(fabsf(c.slow - 0.3f) < 0.001f);

	int16_t input[AUDIO_CHUNK*2];
	// Full-scale alternating samples exercise the signed interpolation bound.
	for (int i = 0; i < AUDIO_CHUNK; i++) {
		input[2*i] = i & 1 ? INT16_MIN : INT16_MAX;
		input[2*i+1] = -input[2*i]-1;
	}
	struct { int16_t out[AUDIO_OUT*2]; uint32_t guard; } dst;
	for (int mode = 0; mode < 2; mode++) {
		const float steps[] = {0.995f, 1.005f};
		memset(&rs, 0, sizeof(rs));
		size_t total_in = 0, total_out = 0;
		for (int i = 0; i < 1200; i++) {
			size_t n = i % AUDIO_CHUNK + 1;
			dst.guard = 0x12345678;
			size_t got = audio_resample(&rs, input, n, steps[mode], dst.out);
			assert(got < AUDIO_OUT && dst.guard == 0x12345678);
			assert(rs.pos >= 0 && rs.pos < 1.006f);
			total_in += n; total_out += got;
		}
		// Fixed-point step rounding is at most 1/65536 input sample/output.
		assert(fabs((double)total_out * steps[mode] - total_in) < total_out/65536.0 + 2);
	}

	// Stretch at speed 1.0 is the input, exactly.
	{
		long n;
		int16_t *o = stretch_sine(440, 44100, 1.0, &n);
		assert(n == 44100);
		for (long i = 0; i < n; i++)
			assert(o[i * 2] == sine(440, i) && o[i * 2 + 1] == o[i * 2]);
		free(o);
	}
	// Slower speeds: the output is input/speed long, the pitch stays (zero crossings per second), and
	// no sample step is steeper than the sine's own steepest step.
	const double speeds[] = {0.95, 0.9, 0.75, 0.6, 0.4};
	const double freqs[] = {110, 220, 440, 1000};
	for (int f = 0; f < 4; f++)
		for (int s = 0; s < 5; s++) {
			long len = 44100 * 3, n;
			int16_t *o = stretch_sine(freqs[f], len, speeds[s], &n);
			double want = len / speeds[s];
			// Input read ahead of the speed (< one jump) is not yet stretched.
			assert(fabs(n - want) < (AUDIO_TSM_JUMP + AUDIO_TSM_SEEK + AUDIO_TSM_FADE) / speeds[s]);
			long zc = 0;
			double slope = 12000 * 2 * M_PI * freqs[f] / 44100, worst = 0;
			for (long i = 1; i < n; i++) {
				zc += (o[i * 2 - 2] < 0) != (o[i * 2] < 0);
				double d = fabs((double)o[i * 2] - o[i * 2 - 2]);
				if (d > worst) worst = d;
			}
			double hz = zc / 2.0 / (n / 44100.0);
			assert(fabs(hz - freqs[f]) < freqs[f] * 0.01 + 1);
			assert(worst <= slope * 1.05 + 4);
			free(o);
		}
	// Going back to full speed ends exactly on the input again.
	{
		static audio_tsm t;
		audio_tsm_reset(&t);
		int16_t in[735 * 2], out[AUDIO_OUT * 2];
		long at = 0;
		for (int frame = 0; frame < 300; frame++) {
			audio_tsm_speed(&t, frame < 150 ? 45000 : 65536);
			for (int i = 0; i < 735; i++)
				in[i * 2] = in[i * 2 + 1] = sine(330, at + i);
			at += 735;
			audio_tsm_put(&t, in, 735);
			size_t got;
			while ((got = audio_tsm_get(&t, out, AUDIO_OUT)))
				if (frame > 152)
					for (size_t i = 0; i < got; i++)
						assert(out[(got - 1) * 2] == sine(330, at - 1) && out[i * 2] == out[i * 2 + 1]);
			if (frame > 152)
				assert(!audio_tsm_busy(&t));
		}
	}
	puts("audio resampler: OK");
}
