/* Wrap the private ARM simulator's retro_run; no host clock is a 3DS clock. */
#include "../../core/plugins/gpulib/psx3d_frameskip.h"
#include <stdio.h>
#include <stdlib.h>
void __real_retro_run(void);
static unsigned frame;
void __wrap_retro_run(void)
{
  const char *fraction=getenv("FS_FRACTION");
  if(fraction) {
    psx3d_skip.enabled=1;
    psx3d_skip.max_skips=2;
    psx3d_skip.numerator=strtoul(fraction,NULL,10);
    psx3d_skip.denominator=120;
  }
  unsigned drawn=psx3d_skip.flips_drawn, skipped=psx3d_skip.flips_skipped;
  unsigned pictures=psx3d_skip.pictures;
  __real_retro_run();
  if(getenv("FS_TRACE")) fprintf(stderr,"skiptrace %u drawn=%u skipped=%u pictures=%u phase=%u streak=%u\n",
    frame++,psx3d_skip.flips_drawn-drawn,psx3d_skip.flips_skipped-skipped,
    psx3d_skip.pictures-pictures,psx3d_skip.phase,psx3d_skip.streak);
}

/* Count/hash the core's actual audio across all tested skip cadences. */
#include "libretro.h"
static retro_audio_sample_batch_t sink;
static unsigned long long audio_frames;
static uint64_t audio_hash=1469598103934665603ULL;
static size_t capture(const int16_t *data,size_t frames)
{
  audio_frames+=frames;
  for(size_t i=0;i<frames*2;i++) audio_hash=(audio_hash^(uint16_t)data[i])*1099511628211ULL;
  return sink(data,frames);
}
static void audio_report(void) { fprintf(stderr,"skip-audio frames=%llu hash=%016llx\n",audio_frames,(unsigned long long)audio_hash); }
void __real_retro_set_audio_sample_batch(retro_audio_sample_batch_t);
void __wrap_retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
  sink=cb;
  __real_retro_set_audio_sample_batch(capture);
  atexit(audio_report);
}
