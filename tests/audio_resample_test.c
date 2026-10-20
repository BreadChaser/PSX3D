#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../psx3d/source/audio_resample.h"

int main(void)
{
	audio_resampler rs = {0};
	// Full-speed production must never trigger slowdown, including a short
	// queue dip. Sustained 60% production slows sound by the 3% limit only.
	for (int i = 0; i < 3600; i++)
		assert(audio_resample_step(&rs, -0.75f, 50, 735, 44100, 1.0f/60) > 0.996f);
	assert(rs.slow == 0);
	float step = 1;
	for (int i = 0; i < 1200; i++)
		step = audio_resample_step(&rs, -1, 50, 735, 44100, 1.0f/36);
	assert(fabsf(step - (0.995f - AUDIO_STRETCH_MAX * 0.01f)) < 0.002f);
	assert(audio_resample_step(&rs, -1, 0, 735, 44100, 1.0f/36) == 0.995f);
	assert(rs.slow == 0);
	// Bursty worker deliveries preserve the measured production rate.
	audio_resampler a = {0}, b = {0};
	for (int i = 0; i < 1200; i++) {
		audio_resample_step(&a, -1, 50, 735, 44100, 1.0f/36);
		audio_resample_step(&b, -1, 50, 367, 44100, 1.0f/36);
		audio_resample_step(&b, -1, 50, 368, 44100, 0);
	}
	assert(fabsf(a.slow - b.slow) < 0.002f);
	// A full queue suppresses slowdown even when production was slow.
	for (int i = 0; i < 1200; i++)
		audio_resample_step(&rs, 0.2f, 50, 735, 44100, 1.0f/36);
	assert(rs.slow == 0);

	int16_t input[AUDIO_CHUNK*2];
	// Full-scale alternating samples exercise the signed interpolation bound.
	for (int i = 0; i < AUDIO_CHUNK; i++) {
		input[2*i] = i & 1 ? INT16_MIN : INT16_MAX;
		input[2*i+1] = -input[2*i]-1;
	}
	struct { int16_t out[AUDIO_OUT*2]; uint32_t guard; } dst;
	for (int mode = 0; mode < 4; mode++) {
		const float steps[] = {0.495f, 0.7f, 0.995f, 1.005f};
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
	// Constant signal stays constant across tiny and large chunk boundaries.
	memset(&rs, 0, sizeof(rs));
	for (int i = 0; i < AUDIO_CHUNK*2; i++) input[i] = 12345;
	for (int i = 0; i < 100; i++) {
		size_t n = audio_resample(&rs, input, i & 1 ? AUDIO_CHUNK : 1, 0.6f, dst.out);
		for (size_t k = i > 1 ? 0 : 4; k < n; k++)
			assert(dst.out[k*2] == 12345 && dst.out[k*2+1] == 12345);
	}
	puts("audio resampler: OK");
}
