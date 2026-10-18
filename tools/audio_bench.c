// Host/ARM microbenchmark: 735 stereo input samples per NTSC emulated frame.
// Link with psx3d/source/audio_resample.c; args: step, frame count.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../psx3d/source/audio_resample.h"
int main(int argc, char **argv)
{
	float step = argc > 1 ? strtof(argv[1], NULL) : 1;
	int count = argc > 2 ? atoi(argv[2]) : 600;
	if (step < 0.495f || step > 1.005f || count < 1) return 1;
	int16_t in[735*2], out[AUDIO_OUT*2];
	for (int i = 0; i < 735*2; i++) in[i] = (int16_t)(i * 1979);
	audio_resampler rs = {0}, control = {0};
	uint32_t hash = 0;
	size_t samples = 0;
	struct timespec t0, t1;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (int i = 0; i < count; i++) {
		float selected = audio_resample_step(&control, step < 0.99f ? -1 : 0, 50,
			735, 44100, (735.0f / 44100) / step);
		// Measure a steady resampling ratio; retain the controller call.
		if (selected < 0) abort();
		size_t n = audio_resample(&rs, in, 735, step, out);
		samples += n;
		hash += (uint16_t)out[(i % n)*2];
	}
	clock_gettime(CLOCK_MONOTONIC, &t1);
	double ns = (t1.tv_sec-t0.tv_sec)*1e9 + t1.tv_nsec-t0.tv_nsec;
	printf("step %.3f: %.3f us/frame, %zu output frames, checksum %08x\n",
		step, ns/count/1000, samples, hash);
}
