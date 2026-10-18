#include <assert.h>
#include <stdio.h>
#include "../../core/plugins/gpulib/psx3d_frameskip.h"

static void cadence(void)
{
  for (unsigned level = 0; level < 8; level++) {
    psx3d_skip_state s = {.level = level, .max_skips = level == 7 ? 5 : 4};
    unsigned drawn = 0, run = 0, longest = 0;
    for (int i = 0; i < 600; i++) {
      if (psx3d_skip_decide(&s)) { run++; if (run > longest) longest = run; }
      else { drawn++; run = 0; }
    }
    assert(longest <= (level == 7 ? 5 : 4));
    assert(drawn == 600 * psx3d_skip_draw(level) / psx3d_skip_period(level));
  }
}

static void load(double floor, double extra, unsigned expected, int window)
{
  psx3d_skip_controller c = {0};
  psx3d_skip_state s = {0};
  unsigned draws = 0, changes = 0, old = 0;
  int skip = 0;
  double total = 0;
  for (unsigned i = 0; i < 3600; i++) {
    s.level = c.level;
    if (!(i & 1)) skip = psx3d_skip_decide(&s);
    // Real games: a drawing vblank alternates with an idle one.
    double work = floor + (!(i & 1) && !skip ? extra * 2 : 0);
    (window ? psx3d_skip_observe_window : psx3d_skip_observe)(&c, work, 1000.0 / 60);
    if (i >= 2400) {
      draws += !(i & 1) && !skip;
      total += work;
      changes += c.level != old;
    }
    old = c.level;
  }
  printf("floor %.1f extra %.1f level %u changes %u cost %.2f draw %.2f fps\n",
      floor, extra, c.level, changes, total / 1200, draws / 20.0);
  assert(c.level == expected);
  assert(changes == 0); // stable load must produce a stable cadence
}
int main(void)
{
  setvbuf(stdout, NULL, _IONBF, 0);
  cadence();
  for (int window=0; window<2; window++) {
    load(6, 5, 0, window);
    load(10, 10, 3, window);
    load(12, 12, 4, window);
    load(20, 10, 6, window);
  }
  load(9, 11, 3, 0); // EWMA ripple overskips a feasible 20-picture cadence
  load(9, 11, 2, 1);
  psx3d_skip_controller c = {.average=20, .level=5, .age=80};
  psx3d_skip_observe(&c, 1000, 16.67);
  assert(c.samples == 0 && c.level == 0);
  // A substantial load reduction must return to full drawing.
  c = (psx3d_skip_controller){.average=22, .level=6, .slope=10, .samples=500};
  psx3d_skip_state state = {0};
  for (unsigned i=0; i<1200; i++) {
    state.level = c.level;
    psx3d_skip_observe(&c, psx3d_skip_decide(&state) ? 4 : 8, 16.67);
  }
  assert(c.level == 0);
  // A slope polluted by a transient stall must not trap a light scene in skip.
  c = (psx3d_skip_controller){.average=9, .level=3, .slope=100, .samples=500};
  for (unsigned i=0; i<2000; i++) psx3d_skip_observe(&c, 8, 16.67);
  assert(c.level == 0);
  // Window policy must also recover after scene changes and isolated stalls.
  c = (psx3d_skip_controller){.average=22, .level=6, .slope=10, .samples=500};
  for (unsigned i=0; i<1600; i++) {
    state.level = c.level;
    psx3d_skip_observe_window(&c, psx3d_skip_decide(&state) ? 4 : 8, 16.67);
  }
  assert(c.level == 0);
  c = (psx3d_skip_controller){.average=9, .level=3, .slope=100, .samples=500};
  for (unsigned i=0; i<2000; i++) psx3d_skip_observe_window(&c, 8, 16.67);
  assert(c.level == 0);
  psx3d_skip_observe_window(&c, 1000, 16.67);
  assert(c.samples == 0 && c.level == 0 && c.window_count == 0);
  // Changes of cadence retain the cap, even with an old phase/streak.
  state = (psx3d_skip_state){.level=6, .streak=4};
  assert(psx3d_skip_decide(&state) == 0);
  // Actual class costs should retain the densest feasible stable cadence.
  c = (psx3d_skip_controller){0};
  state = (psx3d_skip_state){0};
  for (unsigned i=0; i<3600; i++) {
    state.level = c.level;
    unsigned kind = (i & 1) ? 2 : psx3d_skip_decide(&state) ? 1 : 0;
    double costs[] = {31, 10, 9};
    psx3d_skip_observe_model(&c, costs[kind], 16.67, kind);
  }
  assert(c.level == 2); // 9.5 + 10.5 * 2/3 = 16.5ms, within overload margin.
  // A transient expensive scene must not leave a stale recovery slope.
  for (unsigned i=0; i<3600; i++) {
    state.level = c.level;
    unsigned kind = (i & 1) ? 2 : psx3d_skip_decide(&state) ? 1 : 0;
    double costs[] = {18, 8, 6};
    psx3d_skip_observe_model(&c, costs[kind], 16.67, kind);
  }
  assert(c.level == 0);

  // Every fixed rational has exact density and adjacent-length gaps only.
  for (unsigned n=12; n<=60; n++) {
    state = (psx3d_skip_state){.numerator=n, .denominator=60, .max_skips=4};
    unsigned count=0, gap=0, mingap=60, maxgap=0;
    for (unsigned i=0; i<6000; i++) {
      gap++;
      if (!psx3d_skip_decide(&state)) {
        count++;
        if (gap<mingap) mingap=gap;
        if (gap>maxgap) maxgap=gap;
        gap=0;
      }
    }
    assert(count==100*n && maxgap<=mingap+1 && maxgap<=5);
  }
  // A load needing a fraction between 1/3 and 1/2 must use that spare time.
  c = (psx3d_skip_controller){0}; state = (psx3d_skip_state){.max_skips=4};
  unsigned dense_draws=0; double dense_work=0;
  for (unsigned i=0; i<7200; i++) {
    state.numerator=c.dense_numerator ? c.dense_numerator : 60; state.denominator=60;
    unsigned kind=(i&1) ? 2 : psx3d_skip_decide(&state) ? 1 : 0;
    double costs[]={34, 13, 10};
    psx3d_skip_observe_dense(&c, costs[kind], 16.67, kind);
    if (i>=6000) { dense_draws+=kind==0; dense_work+=costs[kind]; }
  }
  assert(c.dense_numerator>20 && c.dense_numerator<30);
  assert(dense_draws>200 && dense_work/1200<16.67);
  // A light scene must recover all pictures; impossible loads retain the cap.
  for (unsigned i=0; i<7200; i++) {
    state.numerator=c.dense_numerator ? c.dense_numerator : 60;
    unsigned kind=(i&1) ? 2 : psx3d_skip_decide(&state) ? 1 : 0;
    double costs[]={12, 8, 6}; psx3d_skip_observe_dense(&c,costs[kind],16.67,kind);
  }
  assert(c.dense_numerator==60);
  for (unsigned i=0; i<7200; i++) {
    state.numerator=c.dense_numerator ? c.dense_numerator : 60;
    unsigned kind=(i&1) ? 2 : psx3d_skip_decide(&state) ? 1 : 0;
    double costs[]={45, 25, 23}; psx3d_skip_observe_dense(&c,costs[kind],16.67,kind);
  }
  assert(c.dense_numerator==12);
  puts("controller: PASS");
}
