#include "../../core/plugins/gpulib/psx3d_frameskip.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

/* A game picture spans heavy+idle vblanks, not two skip opportunities. */
static void scene(const char *name, double base, double render, unsigned period,
                  unsigned max_skip, unsigned *last)
{
  psx3d_smart_controller c = {0};
  psx3d_skip_state s = {.max_skips=max_skip, .denominator=120};
  unsigned drawn=0, skipped=0, intervals=0, previous=0, underruns=0;
  double queue=70, clock=0, sum=0, square=0;
  unsigned skip=0;
  for (unsigned v=0; v<3600; v++) {
    s.numerator=c.numerator ? c.numerator : 120;
    if (v%period==0) skip=psx3d_skip_decide(&s);
    double work=(v%period==0) ? base+(skip?0:render) : 2;
    clock+=work;
    double deadline=(v+1)*1000.0/60;
    if(clock<deadline) clock=deadline;
    queue=70+deadline-clock;
    if(queue<0) underruns++;
    psx3d_smart_observe(&c,work,1000.0/60,queue<0?0:(unsigned)(queue*50/70),max_skip);
    if(v>=1800 && v%period==0) {
      if(skip) skipped++;
      else { drawn++; if(previous) { double gap=v-previous; sum+=gap; square+=gap*gap; intervals++; } previous=v; }
    }
    assert(s.streak<=max_skip);
  }
  *last=c.numerator;
  double mean=sum/intervals;
  printf("%s numerator=%u drawn=%u skipped=%u underruns=%u interval_sd=%.3f vblanks\n",
    name,c.numerator,drawn,skipped,underruns,sqrt(square/intervals-mean*mean));
  if(base+render+2<=2*1000.0/60 && period==2) assert(skipped==0);
}
int main(void) {
  unsigned n;
  scene("30fps fits despite heavy-vblank lateness",20,10,2,2,&n); assert(n==120);
  scene("30fps overloaded",20,20,2,2,&n); assert(n>40 && n<120);
  scene("60fps fits",3,8,1,2,&n); assert(n==120);
  scene("60fps overloaded",8,12,1,2,&n); assert(n>40 && n<120);
  for(unsigned fraction=40; fraction<=120; fraction++) {
    psx3d_skip_state s={.max_skips=2,.denominator=120,.numerator=fraction};
    unsigned draws=0;
    for(unsigned i=0;i<1200;i++) draws+=!psx3d_skip_decide(&s);
    assert(draws==10*fraction);
  }
  psx3d_smart_controller c={0};
  for(unsigned i=0;i<600;i++) psx3d_smart_observe(&c,22,1000.0/60,50,2);
  assert(c.numerator==40);
  for(unsigned i=0;i<1800;i++) psx3d_smart_observe(&c,7,1000.0/60,50,2);
  assert(c.numerator==120); /* scene recovery cannot stay stuck skipping */
  puts("PASS: pair budget, cadence counts/caps, scene recovery");
}
