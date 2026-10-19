#include "../../core/plugins/gpulib/psx3d_frameskip.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void scene(const char *name, double base, double render, unsigned period,
                  unsigned max_skip)
{
  psx3d_skip_state s = {.smart=1, .max_skips=max_skip};
  unsigned drawn=0, skipped=0, intervals=0, previous=0, underruns=0;
  double queue=70, clock=0, sum=0, square=0;
  unsigned skip=0;
  const double budget=1000.0/60;
  for (unsigned v=0; v<3600; v++) {
    if (v%period==0) skip=psx3d_skip_decide(&s);
    double work=(v%period==0) ? base+(skip?0:render) : 2;
    clock+=work;
    double deadline=(v+1)*budget;
    if(clock<deadline) clock=deadline;
    queue=70+deadline-clock;
    if(queue<0) underruns++;
    psx3d_smart_observe(&s,work,budget);
    if(v>=1800 && v%period==0) {
      if(skip) skipped++;
      else { drawn++; if(previous) { double gap=v-previous; sum+=gap; square+=gap*gap; intervals++; } previous=v; }
    }
    assert(s.streak<=max_skip);
  }
  assert(!underruns);
  double mean=sum/intervals;
  double ideal=(budget*period-base-2*(period-1))/render;
  if(ideal>1)ideal=1;
  assert(fabs(drawn-(drawn+skipped)*ideal)<=2);
  printf("%s shown=%.2f/s ideal=%.2f/s drawn=%u skipped=%u underruns=%u interval_sd=%.3f vblanks\n",
    name,drawn/30.0,60.0/period*ideal,drawn,skipped,underruns,sqrt(square/intervals-mean*mean));
  if(ideal==1) assert(!skipped);
}
int main(void) {
  scene("30fps fits despite heavy-vblank lateness",20,10,2,4);
  scene("30fps overloaded",20,20,2,4);
  scene("95% speed with render-dominated work",1,32.1,2,4);
  scene("60fps fits",3,8,1,4);
  scene("60fps overloaded",8,12,1,4);
  /* Existing rational diagnostics still spread each exact density correctly. */
  for(unsigned fraction=40; fraction<=120; fraction++) {
    psx3d_skip_state s={.max_skips=2,.denominator=120,.numerator=fraction};
    unsigned draws=0;
    for(unsigned i=0;i<1200;i++) draws+=!psx3d_skip_decide(&s);
    assert(draws==10*fraction);
  }
  psx3d_skip_state s={.smart=1,.max_skips=4};
  for(unsigned i=0;i<120;i++) {psx3d_skip_decide(&s);psx3d_smart_observe(&s,22,1000.0/60);}
  assert(s.budget_debt>0);
  for(unsigned i=0;i<12;i++) {psx3d_skip_decide(&s);psx3d_smart_observe(&s,7,1000.0/60);}
  for(unsigned i=0;i<600;i++) {assert(!psx3d_skip_decide(&s));psx3d_smart_observe(&s,7,1000.0/60);}
  puts("PASS: minimum skips, pair budget, audio reserve, exact diagnostic cadences/caps, scene recovery");
}
