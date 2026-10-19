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
