// Exercise every full-scale four-point corner at every Q12 phase, plus
// tiny/maximum chunks and transitions. Link the actual frontend resampler.
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../../psx3d/source/audio_resample.h"

int main(void)
{
	int16_t in[AUDIO_CHUNK*2], out[AUDIO_OUT*2];
	for (unsigned bits=0; bits<16; bits++) {
		for (unsigned phase=0; phase<4096; phase++) {
			audio_resampler rs = {0};
			for (unsigned p=0; p<3; p++) {
				rs.hist[p*2] = (bits & (1u<<p)) ? 32767 : -32768;
				rs.hist[p*2+1] = -1-rs.hist[p*2];
			}
			in[0] = (bits & 8) ? 32767 : -32768;
			in[1] = -1-in[0];
			rs.pos = phase/4096.0f;
			assert(audio_resample(&rs,in,1,1.0f,out)==1);
		}
	}
	audio_resampler rs = {0};
	uint32_t seed=123;
	for (unsigned i=0; i<AUDIO_CHUNK*2; i++) {
		seed=seed*1664525+1013904223;
		in[i]=(int16_t)(seed>>16);
	}
	const float steps[]={0.495f,0.98999f,0.99f,0.995f,1.0f,1.005f};
	const unsigned counts[]={1,13,735,1024};
	for (unsigned i=0; i<1200; i++) {
		size_t n=audio_resample(&rs,in,counts[i%4],steps[i%6],out);
		assert(n<=AUDIO_OUT);
		assert(rs.pos>=0 && rs.pos<1.006f);
	}
	return 0;
}
