/* Shared by gpulib, frontend and the timing harness. No platform dependencies. */
#ifndef PSX3D_FRAMESKIP_H
#define PSX3D_FRAMESKIP_H
#include <stdint.h>
#include <string.h>

typedef struct {
  unsigned enabled, level, phase, streak, max_skips;
  unsigned numerator, denominator;
  unsigned timing, spu_main_ticks, spu_wait_ticks;
  unsigned gpu_main_ticks;
  unsigned phase_ticks[8], render_prims;
  unsigned spu_stolen;
  unsigned flips_drawn, flips_skipped, words_drawn, words_skipped, pictures;
  unsigned class_timing, dma_ticks;
  /* Append developer policy state without moving existing hot counters. */
  unsigned smart;
  double budget_debt, frame_budget, picture_credit;
} psx3d_skip_state;
extern psx3d_skip_state psx3d_skip;
/* Written by the SPU worker, read atomically; keep out of main-thread snapshots. */
extern unsigned psx3d_spu_worker_ticks, psx3d_spu_worker_items;

/* Rational cadences spread draws evenly: D, DDDS, DDS, DS, DSS, DSSS, DSSSS.
 * Phase changes only at a game flip, never at a host vblank. */
static inline unsigned psx3d_skip_draw(unsigned level)
{
  static const unsigned n[] = {1, 3, 2, 1, 1, 1, 1, 1};
  return n[level < 8 ? level : 7];
}
static inline unsigned psx3d_skip_period(unsigned level)
{
  static const unsigned n[] = {1, 4, 3, 2, 3, 4, 5, 6};
  return n[level < 8 ? level : 7];
}
static inline unsigned psx3d_skip_decide(psx3d_skip_state *s)
{
  if (s->smart) {
    unsigned skip = s->budget_debt > 0 && s->streak < s->max_skips &&
      s->picture_credit < 1000.0 / 15;
    if (skip) s->streak++; else s->streak = 0;
    if (!skip) {
      s->picture_credit -= 1000.0 / 15;
      if (s->picture_credit < 0) s->picture_credit = 0;
    }
    return skip;
  }
  unsigned period = s->denominator ? s->denominator : psx3d_skip_period(s->level);
  s->phase += s->denominator ? s->numerator : psx3d_skip_draw(s->level);
  unsigned skip = s->phase < period && s->streak < (s->max_skips ? s->max_skips : 4);
  if (!skip) { s->phase %= period; s->streak = 0; }
  else s->streak++;
  return skip;
}

typedef struct {
  double average, previous, previous_fraction, slope;
  unsigned samples, age, level, max_level;
  double window_work;
  unsigned window_count;
  double class_work[3], class_average[3], picture_fraction;
  unsigned class_count[3], class_samples[3];
  unsigned dense_numerator;
} psx3d_skip_controller;

/* Work excludes pacing sleep. Average spans heavy/light vblank pairs. A
 * measured slope predicts the cost of the next denser cadence before trying
 * it. Until both costs are known, recovery probes are spaced two seconds
 * apart. Slow down promptly, recover conservatively; no audio-driven jitter. */
static inline void psx3d_skip_observe(psx3d_skip_controller *c,
    double work, double budget)
{
  if (work > budget * 8) { unsigned cap = c->max_level; memset(c, 0, sizeof(*c)); c->max_level = cap; return; }
  c->average += (work - c->average) / 16;
  c->samples++;
  c->age++;
  if (c->samples < 32 || c->age < 32) return;
  double fraction = (double)psx3d_skip_draw(c->level) / psx3d_skip_period(c->level);
  if (c->previous_fraction && c->previous_fraction != fraction) {
    double slope = (c->average - c->previous) / (fraction - c->previous_fraction);
    if (slope > 0 && slope < budget * 8) c->slope = slope;
  }
  unsigned level = c->level;
  if (c->average > budget * 1.02 && level < (c->max_level ? c->max_level : 6)) level++;
  else if (level && c->age >= 120 && c->average < budget * .94) {
    double denser = (double)psx3d_skip_draw(level - 1) / psx3d_skip_period(level - 1);
    if ((c->slope && c->average + c->slope * (denser - fraction) < budget * .97)
        || !c->slope || (c->age >= 240 && c->average < budget * .70)) level--;
  }
  if (level != c->level) {
    c->previous = c->average;
    c->previous_fraction = fraction;
    c->level = level;
    c->age = 0;
  }
}
/* Whole 24-vblank windows cover complete 30fps DDDS, DDS and DS periods.
 * EWMA peaks from heavy/idle pairs otherwise cause false overload decisions.
 * Keep this separate from the original observer for same-binary hardware A/B. */
static inline void psx3d_skip_observe_window(psx3d_skip_controller *c,
    double work, double budget)
{
  if (work > budget * 8) { unsigned cap = c->max_level; memset(c, 0, sizeof(*c)); c->max_level = cap; return; }
  c->window_work += work;
  if (++c->window_count < 24) return;
  c->average = c->window_work / c->window_count;
  c->samples += c->window_count;
  c->age += c->window_count;
  c->window_work = 0;
  c->window_count = 0;
  if (c->age < 48) return;
  double fraction = (double)psx3d_skip_draw(c->level) / psx3d_skip_period(c->level);
  if (c->previous_fraction && c->previous_fraction != fraction) {
    double slope = (c->average - c->previous) / (fraction - c->previous_fraction);
    if (slope > 0 && slope < budget * 8) c->slope = slope;
  }
  unsigned level = c->level;
  if (c->average > budget * .995 && level < (c->max_level ? c->max_level : 6)) level++;
  else if (level && c->age >= 120 && c->average < budget * .97) {
    double denser = (double)psx3d_skip_draw(level - 1) / psx3d_skip_period(level - 1);
    if ((c->slope && c->average + c->slope * (denser - fraction) < budget * .985)
        || !c->slope || (c->age >= 240 && c->average < budget * .70)) level--;
  }
  if (level != c->level) {
    c->previous = c->average;
    c->previous_fraction = fraction;
    c->level = level;
    c->age = 0;
  }
}
/* Predict the cost of a denser cadence from actual drawing/skipping/idle
 * vblanks. Keeping these populations separate avoids a stale cross-scene
 * slope locking a light scene into a sparse cadence. Runtime opt-in only. */
static inline void psx3d_skip_observe_model(psx3d_skip_controller *c,
    double work, double budget, unsigned kind)
{
  if (work > budget * 8) { unsigned cap = c->max_level; memset(c, 0, sizeof(*c)); c->max_level = cap; return; }
  if (kind > 2) kind = 2;
  c->class_work[kind] += work;
  c->class_count[kind]++;
  c->window_work += work;
  if (++c->window_count < 24) return;
  c->average = c->window_work / c->window_count;
  double pictures = (double)(c->class_count[0] + c->class_count[1]) / c->window_count;
  c->picture_fraction = c->samples ? c->picture_fraction * .75 + pictures * .25 : pictures;
  for (unsigned k = 0; k < 3; k++) {
    if (c->class_count[k]) {
      double mean = c->class_work[k] / c->class_count[k];
      c->class_average[k] = c->class_samples[k] ? c->class_average[k] * .75 + mean * .25 : mean;
      c->class_samples[k] += c->class_count[k];
    }
    c->class_work[k] = 0;
    c->class_count[k] = 0;
  }
  c->samples += c->window_count;
  c->age += c->window_count;
  c->window_work = 0;
  c->window_count = 0;
  if (c->age < 48) return;
  unsigned level = c->level;
  if (c->average > budget * .995 && level < (c->max_level ? c->max_level : 6)) level++;
  else if (level && c->age >= 120 && c->average < budget * .97 &&
      c->class_samples[0] >= 4 && c->class_samples[1] >= 4) {
    double fraction = (double)psx3d_skip_draw(level-1) / psx3d_skip_period(level-1);
    double predicted = c->picture_fraction * (c->class_average[1] + fraction *
        (c->class_average[0] - c->class_average[1])) +
        (1 - c->picture_fraction) * c->class_average[2];
    if (predicted < budget * .97) level--;
  }
  if (level != c->level) { c->level = level; c->age = 0; }
}
/* A 60-picture accumulator fills the gaps between the coarse rational levels.
 * It permits adjacent gaps only, while preserving the explicit maximum skip
 * streak. Class costs include frontend work but exclude pacing sleep. */
static inline void psx3d_skip_observe_dense(psx3d_skip_controller *c,
    double work, double budget, unsigned kind)
{
  if (work > budget * 8) { unsigned cap = c->max_level; memset(c, 0, sizeof(*c)); c->max_level = cap; return; }
  if (kind > 2) kind = 2;
  c->class_work[kind] += work;
  c->class_count[kind]++;
  c->window_work += work;
  if (++c->window_count < 24) return;
  c->average = c->window_work / c->window_count;
  double pictures = (double)(c->class_count[0] + c->class_count[1]) / c->window_count;
  c->picture_fraction = c->samples ? c->picture_fraction * .75 + pictures * .25 : pictures;
  for (unsigned k=0; k<3; k++) {
    if (c->class_count[k]) {
      double mean = c->class_work[k] / c->class_count[k];
      c->class_average[k] = c->class_samples[k] ? c->class_average[k] * .75 + mean * .25 : mean;
      c->class_samples[k] += c->class_count[k];
    }
    c->class_work[k] = 0; c->class_count[k] = 0;
  }
  c->samples += c->window_count; c->age += c->window_count;
  c->window_count = 0; c->window_work = 0;
  if (c->age < 48) return;
  unsigned current = c->dense_numerator ? c->dense_numerator : 60;
  unsigned target = current;
  unsigned minimum = c->max_level == 7 ? 10 : 12;
  double slope = c->picture_fraction * (c->class_average[0] - c->class_average[1]);
  if (c->class_samples[0] >= 4 && c->class_samples[1] >= 4 && slope > .1) {
    double floor = c->picture_fraction * c->class_average[1] +
        (1-c->picture_fraction) * c->class_average[2];
    double optimal = (budget * .98 - floor) * 60 / slope;
    target = optimal <= minimum ? minimum : optimal >= 60 ? 60 : (unsigned)optimal;
  } else if (c->average > budget * .995) {
    target = current > minimum + 4 ? current-4 : minimum;
  } else if (c->age >= 120 && c->average < budget * .96) target = 60;
  // The measured deadline wins if stale class estimates predict too much.
  if (c->average > budget * .995 && target >= current && current > minimum)
    target = current-1;
  if (target < current && c->average > budget * .985) current = target;
  else if (target > current && c->age >= 120 && c->average < budget * .98)
    current += target-current > 4 ? 4 : target-current;
  if (current != (c->dense_numerator ? c->dense_numerator : 60)) {
    c->dense_numerator = current; c->age = 0;
  }
}
/* Actual work, excluding the frontend's pacing sleep. Two vblanks of credit
 * let a 30fps heavy/idle pair share its budget, and absorb scheduler jitter.
 * At each GAME flip, drawing is admitted unless previous work owes time.
 * Skipped drawing repays exactly the time it saves; there is no guessed
 * render-cost slope, coarse level or audio/late-vblank threshold. A separate
 * wall-time accumulator admits at least 15 pictures/s whenever game flips
 * allow it, accepting slower emulation when that floor cannot fit at 100%.
 * Surplus credit carries over to spread fractional picture cadences evenly. */
static inline void psx3d_smart_observe(psx3d_skip_state *s,
    double work, double budget, double wall)
{
  if (!s->frame_budget) s->budget_debt = -2 * budget;
  s->frame_budget = budget;
  s->picture_credit += wall;
  if (s->picture_credit > 2000.0 / 15) s->picture_credit = 2000.0 / 15;
  if (work > budget * 8) return; // blocking load/menu, not a scene cost
  s->budget_debt += work - budget * .99; // reserve for scheduler/loop tails
  if (s->budget_debt < -2 * budget) s->budget_debt = -2 * budget;
  if (s->budget_debt > 4 * budget) s->budget_debt = 4 * budget;
}
#endif
