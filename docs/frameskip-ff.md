# Predictive real-time frameskip (post-v0.3.0 experiment)

Base: `10a5ac8` (`t3/fps-in-layout`). Branch: `t3/frameskip-ff`.
Nothing was deployed, no console was contacted, and nothing was pushed. This is
not a v0.3.0 release change. Hardware acceptance is still required.

## What the fast-forward observation proves

At a nominal 60 emulated vblanks/s:

| Observed mode | Emulated vblanks/s | Host time per emulated vblank |
|---|---:|---:|
| Normal, 80–85% | 48–51 | 20.83–19.61 ms |
| Fast forward, 139% | 83.4 | 11.99 ms |
| Real-time target | 60 | 16.67 ms |

Normal therefore spends **7.62–8.84 ms more per emulated vblank**. Its throughput
is 39–42% below FF. FF's complete workload has approximately **4.68 ms/vblank
of headroom** relative to real time. That is useful evidence; it is not a
measurement of normal-speed SPU or renderer cost in isolation.

The relevant paths are `emu_run_game()` and `options_fast_forward()`:

* FF bypasses clock sleep, executes four `retro_run()` calls per outer loop,
  forces fixed skip interval 3, and presents once for those four calls. Normal
  executes/presents once per emulated vblank, even if the picture is unchanged.
  The GPU frame can remain open across FF's extra calls, amortizing submissions.
* Both audio callbacks discard FF samples before `audio_write()`. This removes
  frontend sample diagnostics, Hermite resampling/stretching, copying and DSP
  submission. **It does not turn off the core SPU mixer**, XA handling or worker
  synchronization: `SPUasync()` still calls `do_samples()`, and `snd_feed()` still
  invokes the callback. No FF-specific lower-quality mixing is selected.
* Gameplay `video_present()` has **no explicit vblank wait**. The clock paces
  normal gameplay; `gspWaitForVBlank()` is only in the menu/library wrapper.
  GPU frame begin, previous-buffer retirement and readbacks can wait. Existing
  `wait` is GPU-begin time, not a vsync sleep, and can be inside `retro_run()`.
* Neither NDSP nor CSND output waits for audio queue space. They drop excess
  samples instead. The literal audio-buffer-space wait is **zero** in both
  modes. DSP submission/cache work costs CPU time; SPU worker semaphores can
  block. These are different costs.

Normal auto passes `occ < 20 || late` to `abuf_status_cb(true, occ, ...)`, with
`late` detected after the preceding frame exceeded its clock deadline by 1/8
slot. `retro_run()` latches `fskip_force`; `decide_frameskip()` consumes it at a
GP1(05) display flip. This delay depends on the game's phase. The Old 3DS default
limit is two skipped **game pictures**, not two emulated vblanks. In the three
states here games draw at about 30 pictures per 60 vblanks: half the vblanks have
no polygons to remove. A saturated limit 2 draws roughly 10 pictures/s at real
time; FF's interval 3 draws roughly 7.5 at real time (10.4 at 139% speed).

`frameskip.allow` and `do_cmd_list_skip()` must remain authoritative: drawing
into the displayed area, required offscreen texture work, VRAM transfers and
readbacks cannot simply be discarded. State words, command lengths, polyline
termination and GPU busy timing still have to run. GT2 is an important example:
a skipped game picture can still require GPU draws and readbacks.

The old `skip` statistic was a count of NULL libretro video callbacks, **not GPU
skips**. The PICA 16-bit output path itself produces NULL callbacks. It is now
called `dupe` in the log. Do not infer drawn fps from it or from primitive batches.

## Existing hardware evidence, without contacting hardware

Last 15 steady samples of the already saved `devtools/o3fs_*_0x11_0x00.log` files:

| Game | Emulated vblanks/s | Core wall time | Present wall time | GPU-begin wait |
|---|---:|---:|---:|---:|
| Crash | 50.53 | 17.30 ms | 1.281 ms | 0.011 ms |
| CTR | 30.29 | 30.81 ms | 1.133 ms | 0.010 ms |
| GT2 | 30.46 | 30.61 ms | 1.139 ms | 0.011 ms |

These are historical builds/scenes, not paired FF/new-policy measurements on
10a5ac8. Crash's 19.79 ms wall interval consists of about 17.30 ms core, 1.28 ms
present, and 1.21 ms outside those brackets (other frontend work, pacing and
logging). Even removing 75% of 1.28 ms only saves about **0.96 ms**: redundant
composition alone does not explain the observed 7.6–8.8 ms gap. A large explicit
vsync wait and an audio queue-space wait are ruled out by the implementation;
renderer duty cycle, submission batching, frontend audio and unmeasured worker
sync remain to be separated in a paired run. The SPU mixer itself is not an FF
saving. GPU work time and GPU wait are not additive to core/present wall time.

The earlier investigation (`t3/improve-frameskip-performance:docs/frameskip.md`)
found only ~1–2% modeled CPU benefit from prediction/held presentation alone.
This implementation additionally controls the accepted picture cadence directly,
including 1-of-4/1-of-5 when needed. It does not claim that avoiding presentation
alone makes the emulation floor faster, or that every game can reach 60.

## Design and A/B switch

Add **`rtfs`** to `sdmc:/3ds/PSX3D/switches.txt`, with GPU renderer and Frameskip
set to **auto**. Remove `rtfs` for the legacy policy in the same binary. Fixed,
threshold, disabled, `noskip`, rewind and FF retain their original policy. The
feature is opt-in on either console; no defaults or persisted settings change.
The experimental cap of four pictures belongs to this policy and supersedes
auto's legacy interval limit; the old menu interval still applies without it.

The frontend measures all work after pacing sleep, through emulation, worker
sync, composition, frontend housekeeping and diagnostic writes. A 1/16 moving
average spans heavy/light vblanks. Sustained overload steps through these draw
fractions: **1, 3/4, 2/3, 1/2, 1/3, 1/4, 1/5**. At least 32 emulated frames
separate reductions; recovery requires 120 frames and headroom. The cost change
between cadences estimates the extra cost of a higher draw fraction, so recovery
is predicted before increasing work. An unknown slope gets a bounded recovery
probe. A stale slope cannot trap a newly light scene in skipping: after 240
frames below 70% budget, a recovery probe is permitted even with a slope. A 2% overload tolerance avoids reacting to moving-average phase ripple;
recovery requires 6% measured headroom and predicts against a 97% budget.

At each real display flip, an integer phase accumulator spreads draws evenly.
Steady 1/2 is DSDS; 2/3 is DDSDDS (cyclic rotations are equivalent). The maximum
consecutive skipped-picture count is four, including during cadence changes.
Audio/late signals do not randomly override that sequence. The existing gpulib
safety checks still execute. When the core floor is too expensive at 1/5, the
policy stays capped and runs slow rather than dropping audio or changing PS1
clock/logic timing.

The clock still issues one normally paced emulated vblank per slot; skipping
never bypasses sleep or runs extra emulated frames. Small deadline debt can be
recovered, as before; debt beyond three slots is discarded. Menus, suspend/resume,
long stalls and FF/rewind transitions reset the estimate. FF icons are explicitly
repainted when removed.

State-only renderer lists no longer open a GPU frame under this policy. Game
pictures are retained on the LCD until the snapshot changes, with UI, slider,
blanking and movies forcing repaint when required. A display flip on an idle
vblank **opens a GPU frame to copy the correct scanout bank**. The visible
snapshot has its own serial; offscreen VRAM changes submit through `video_hold()`
without recomposing/swapping the LCD targets. Required work is always submitted,
including readbacks and uploads. GPU worker queries/hold operations dispatch to
that worker; no renderer state is read while it is processing commands.

Audio writes/mixing quality/stretch settings are unchanged. Old CIA syscore SPU
mixing and the existing steal/fallback logic remain enabled under the existing
rules (30% allowance; no worker on Old HBL with no allowance). There is no new
thread scheduling or mixing optimization without hardware evidence to justify it.

## Diagnostics

`stats` adds a `cadence` log line once per sample interval:

* `ff`, `emu`, `game`: FF flag, actual `retro_run` count, and game speed. FF has
  four emulated frames per outer loop; the old `fps` line counts outer loops.
* `drawn/skipped`: GP1 flip decisions; safety checks may still draw required work.
  `drawfps` counts completed PICA output-picture callbacks per host second.
  `present/held`: LCD composition calls versus retained presentations.
* `level/avg`: selected cadence index and average work ms.
* `run/compose/pace/audio/spu/sync/worker`: ms **per emulated vblank**. `audio` is
  frontend audio callback work (not a queue wait). `spu` is main-thread
  `do_samples()` inclusive time; `sync` is its semaphore-wait subset. `worker`
  is parallel channel-mixing wall time and item count; `steal` counts items
  mixed by the main thread. Worker counters use relaxed atomics.
* `words`: words sent to renderer/skip parser, including state words. These are
  not primitive/picture counts. Existing GPU-begin/queue waits are nested in
  `run/compose`; never add them twice. Worker time overlaps main execution.

Timers are conditional on `stats`, `prof` or a linked logger. Audio's existing
queue/underrun/drop/maxgap diagnostics remain. Statistics/SD writes perturb
performance; compare identical logging settings, then play with them disabled.
The existing on-screen detailed Draw/skip row still derives from libretro
callback duplication; use the new log's `drawfps` for this hardware experiment.

## Results and limits

Final uninstrumented-for-hashing, paced QEMU checks use 300 warmup + 600 measured
vblanks. The reference is a separately compiled 10a5ac8 core. No wall-clock
speedup is expected on a host already able to emulate at real time:

| State | Reference emulated/drawn fps | Final repeat emulated/drawn fps | Present calls / 600, reference → new |
|---|---:|---:|---:|
| Crash `o3auto` | ~60 / 29.9 | ~60 / 30 | 600 → 300 |
| CTR | ~60 / 30 | ~60 / 30 | 600 → 300 |
| GT2 | ~60 / 30 | 59.94 / 29.97 | 600 → 300 |

All produce **441,000 stereo audio frames per 600 vblanks**. The native host is
fast enough to select level 0 in the final repeat. This checks the speed ceiling
and that presentation holding does not accelerate the game. It does not prove
an Old 3DS lock at real time. The fake GPU cannot measure LCD pacing, submission
waits, audio-device work or syscore scheduling.

An earlier paired host run had an outlier: Crash new 57.86 emulated / 23.53 drawn
fps versus reference 60 / 29.9. CTR and GT2 held 60/30. The repeat returned Crash/CTR to 60/30 and GT2 to 59.94/29.97. Artificial 3× slow-host trials were also mixed: Crash ~60→60,
CTR 59.94→60.18, GT2 59.29→55.71. That last regression is retained in the evidence,
not discarded as a win. GT2 still drew required GPU work on all 300 drawing
vblanks while the selected cadence reached 1/5. Host wall-time scaling amplifies
JIT/scheduler noise, and cadences can temporarily reduce after a stall. The
controller's stale-slope recovery was subsequently bounded and covered by a
native test. These trials are limit/stability evidence, not hardware forecasts.
An earlier 4× trial with the erroneous CPU snapshot copy is excluded. The final
4× Crash repeat reached 59.17 emulated / 10.85 drawing-vblanks per second with
110 presentations/600 vblanks; its last selected level was 2. These artificial
wall-time-scaled runs are sensitive to host activity and are not a reproducible
substitute for the deterministic load fixtures or hardware measurement.

For exact cadence correctness, compare original versus changed rendering at
**the same** fixed cadence, including the new maximum of four skipped pictures:

| State | Fixed skip 1: identical hash | Fixed skip 4: identical hash | Present calls, skip 1 / skip 4 |
|---|---|---|---:|
| Crash | `3f422f8417cb6985` | `5a0482519a931e5b` | 151 / 61 |
| CTR | `63ce7b5db9a28cb2` | `66e02565ffc6d472` | 151 / 61 |
| GT2 | `9613d1919fd668b4` | `8c4782ba3398b28c` | 151 / 61 |

**All 600 per-vblank cumulative hashes match in each of the six comparisons**,
not merely the last hash. Reference presentation calls are 600 in every case.
The approximately 150/60 accepted game pictures correspond to **15/6 drawn fps
at real-time pacing** (one startup present is included in 151/61). GT2 opens 301
GPU frames even at skip 4, because offscreen work remains necessary; it presents
only 61. A GPU-open/draw-batch count is therefore not visible fps.

A deterministic native controller test uses alternating heavy/idle vblanks:
10 ms unavoidable + 10 ms full-draw average settles at 1/2 (15 ms work, 15 drawn
fps); 12+12 settles at 1/3 (16 ms, 10 drawn fps). An impossible 20+10 floor
settles at the four-skip cap (22 ms work, at most 45.45 emulated vblanks/s before
other overhead). Stable fixtures make zero cadence changes over the final 1,200
frames. All seven ratios and the skip cap are checked, plus long-stall reset,
load reduction and recovery from a slope polluted by a transient stall.
ASan/UBSan and the native 3DS `.3dsx` build pass. A fresh patch application
matches all 28 represented core files; patch reverse-check also passes.

Crash cache-model profile, vblanks 300..599 (no draw hashing):

| Core workload | Model cycles/vblank | Equivalent model ms at 268 MHz | Renderer command parser/draw CPU | SPU sample loop / channel mix / ADPCM decode |
|---|---:|---:|---:|---:|
| Legacy auto, continuous underrun, max 2 | 2.93 M | 10.9 | 0.91 ms | 0.43 / 0.19 / 0.16 ms |
| New fixed steady 1/5 diagnostic | 2.80 M | 10.5 | 0.55 ms | 0.43 / 0.19 / 0.16 ms |
| FF, fixed skip 3, present every four | 2.84 M | 10.6 | 0.69 ms | 0.43 / 0.19 / 0.16 ms |

The model is instructions + 70×I misses + 70×D read misses + 4×store misses,
excluding interlock cycles. These **are modeled CPU costs**, not wall timings
or predicted 3DS fps. The fake SDK charges zero GPU wait/presentation/audio
output time. SPU finishing (~0.07 ms), ADSR (~0.10–0.11 ms) and other SPU work
are additional; the three named components are not the entire SPU subtotal.
The unchanged mixing workload supports the source-level FF finding. The ~4%
modeled total reduction versus already saturated auto is modest; it does not
explain away the observed hardware gap. The new hardware counters are required
to attribute that gap exactly. No claim of a measured hardware gain is made.

Small evidence and trace digests live in `tools/frameskip/evidence/`; full private
logs, binaries and simulator configuration are archived in ignored
`devtools/frameskip_ff_20261018/`. Temporary simulator and baseline source copies
were removed after archiving. Re-run `tools/frameskip/summarize.py <private-sim>`
to verify traces and regenerate the small evidence. Hash builds and no-hash timing
builds are deliberately separate.

Risks needing hardware acceptance: 16 KB instruction-cache/code-layout effects;
GT2/CTR offscreen-rendering floors; delayed cost-estimate recovery after real load
changes; six-picture/s output at the cap; snapshot/blanking/movie/stereo behavior;
GPU-worker and syscore scheduling; and actual audio underruns. The audio sample
count verifies production, not audible continuity at the speakers. The on-screen
legacy Draw/skip row also remains unsuitable for measuring GPU pictures.

## Simulator method and reproduction

The supplied simulator was privately copied to `/tmp/ffskip-sim`, excluding old
logs, binaries, other renderer cores and core dumps (about 150 MB during testing).
The shared `devtools/sim` and consoles were untouched. `prepare-sim.py` synchronizes
this worktree's real core, retaining only the ARM/Linux platform stanzas. The
baseline is the `10a5ac8` patch applied to the submodule's upstream commit, not a
stale pre-release simulator core. Both builds use GCC 14.2, ARMv6, `-O2`, SMALL=1,
the supplied HLE states/configuration, and the same runner/SDK.

The fake SDK's `GX_TextureCopy` must be a **no-op**, like its other GPU calls.
Making it a CPU `memcpy` falsely charges the CPU for GPU snapshot bandwidth and
can substantially distort the result. Early runs with that mistake were excluded
from final wall-time measurements. Hash comparisons are unaffected: the fake GPU
has no rasterized picture to copy. This harness checks the draw stream, not LCD
pixels, mask/depth contents or real GPU synchronization.

```sh
# Private, small copy. Never run these against the shared sim directory.
sim_src=/home/tony/Development/PS1_3ds/devtools/sim
mkdir -p /tmp/ffskip-sim
rsync -a --exclude='*.core' --exclude='cs_*' --exclude='bm_*' \
  --exclude='ip_*' --exclude='tb*' --exclude='log_*' --exclude='runner*' \
  --exclude=core --exclude=core_gles --exclude=core_idle --exclude=idletest \
  "$sim_src/" /tmp/ffskip-sim/
cp "$sim_src/runner.c" /tmp/ffskip-sim/runner.c
python3 tools/frameskip/prepare-sim.py /tmp/ffskip-sim
source /home/tony/Development/PS1_3ds/devtools/env.sh
/tmp/ffskip-sim/build_pica.sh
# PACED uses actual CLOCK_MONOTONIC deadlines; WARMUP excludes startup/JIT.
INI=o3hw_auto.ini /tmp/ffskip-sim/irun.sh runner_pica o3auto 600 PACED=1 WARMUP=300 RTFS=1
# Repeat with ctr and gt2. Omit RTFS for the instrumented legacy path.
# Native controller tests include steady load, overload floor and recovery:
tools/frameskip/check.sh
# Hash builds are for correctness, not wall-time performance:
HASH=-DPICA_SIM_HASH /tmp/ffskip-sim/build_pica.sh
INI=o3hw_fs1.ini /tmp/ffskip-sim/irun.sh runner_pica o3auto 600 HASHEVERY=1
INI=o3hw_fs1.ini /tmp/ffskip-sim/irun.sh runner_pica o3auto 600 HASHEVERY=1 RTFS=1 LEVEL=3
```

`LEVEL=3` is a simulator-only fixed 1/2 cadence override, used to compare exactly
the same selected frames; it is not a product switch. Compare every `frame ...
hash` line, not just the final cumulative hash. `REQUEST_SKIP=1` exercises the
legacy auto cap. `WORK_SCALE=N` artificially delays each host frame by N times
its measured work; this is a slow-host stress test, not a 3DS cycle model.
`CSV=1` enables per-vblank timing/command-word traces.

To build the exact reference, unpack `git -C core archive HEAD` into a private
scratch core, apply both patches from `git show 10a5ac8:patches/<name>`, then copy
its `plugins/gpulib/gpu.c`, `plugins/gpu_pica/gpu_pica.c`, and
`plugins/dfsound/spu.c` over those three files in the prepared simulator. These
are the only core implementation files changed here (plus the new header).
Build `core_pica` directly with the same make flags from `build_pica.sh`, and link
the same runner as `runner_before`. Do not run `build_pica.sh` for this step: it
would copy the changed GPU sources over the reference. Preserve separate
`runner_before`/`runner_after` executables, and separate hash/non-hash binaries.
The evidence summary records hashes of these binaries and compared frame traces.

For normal product build/patch regeneration, **do not execute the installed
`devtools/mk.sh`**, which changes directory to the primary checkout:

```sh
git -C core add -N libpcsxcore/psx3d_depth.h libpcsxcore/psx3d_prof.h \
  plugins/gpu_pica plugins/gpulib/renderer_dispatch.c plugins/gpulib/psx3d_frameskip.h
git -C core -c diff.mnemonicPrefix=false -c diff.noprefix=false diff \
  -- Makefile Makefile.libretro frontend/libretro.c libpcsxcore plugins \
  > patches/0002-psx3d-widescreen-and-stereo-depth.patch
source /home/tony/Development/PS1_3ds/devtools/env.sh
make -j16
```

## Old 3DS hardware acceptance — ONLY when release testing frees the consoles

These are future steps; none was run for this branch.

1. Reserve the Old 3DS rig with the release owner. Preserve its installed build,
   configuration, switches and library/save-state files. Slots 0/2 are read-only;
   use slot 8/9 for a separate save/load smoke test. Use the verified screenshot
   workflow and shared transfer lock for installation. Never start a second
   viewer on UDP 8001, never send blind inputs, never press HOME during a transfer.
2. Build the CIA in **this** worktree with `make -j16 cia` and the existing
   `devtools/tools/.../bannertool` and `.../makerom` paths from the developer notes. Archive
   CIA/3dsx/ELF, `git rev-parse HEAD`, SHA256 sums, patch hash and build log together.
   Install only once. Compare policy A/B in that identical binary, so code layout
   and toolchain do not confound the switch comparison. Retain 30% syscore time.
3. Set GPU renderer, auto frameskip, default PS1 clock, existing audio stretch
   setting (normally on), slider down, FPS panel off and the same touch layout.
   Record those settings. Confirm Old slot 2 loads into the expected scene with
   a fresh screenshot, rather than assuming the game launch worked.
4. From verified ftpd-open state, the existing **`devtools/o3fs.sh`** is o3pmu
   without `noskip`. Do not substitute o3pmu, which disables frameskip. For each
   game run A/B/B/A, ~70 s each, archiving each log immediately before the next
   invocation overwrites it. Confirm Gran Turismo 2's exact library title first.

   ```sh
   # Each invocation drives the console. Start only with the rig ready.
   GAME=Crash_Bandicoot ST=2 devtools/o3fs.sh 0x11,0x00 renderer=gpu
   # Archive devtools/o3fs_Crash_Bandicoot_0x11_0x00.log as Crash_A1.log now.
   GAME=Crash_Bandicoot ST=2 devtools/o3fs.sh 0x11,0x00 renderer=gpu rtfs
   # Archive as Crash_B1.log; repeat B2 then A2, preserving all logs.
   # Repeat exactly for GAME=Crash_Team_Racing and GAME=Gran_Turismo_2.
   ```

   The helper checks reference screens and must stop on a mismatch. Its references
   may predate the release UI: inspect/update references through the established
   checked workflow before using it, never bypass a failed check.
5. Discard at least the first 10 s (load, OSD, cadence settling). Aggregate the
   final 30+ intervals **weighted by their elapsed host time**. Report emulated
   vblanks/s, `game` %, `drawfps`, cadence level, flip decisions and presentations
   held. Also report emulation/composition/pacing/audio/SPU/sync/worker ms per
   emulated vblank, GPU queue waits, audio underruns/drops and max gap. PMU only
   brackets the final `retro_run()` of FF; do not compare its FF cycles/frame to
   normal as if it covered all four calls.
6. In a separate diagnostic Crash session, log 20 s normal, hold the existing FF
   control for 20 s, release for 20 s. Verify each input by a fresh screenshot and
   confirm the `ff` field changes. Use `emu / host_seconds`, not the old outer-loop
   `fps` line, for 139% comparisons. Compare per-emulated-frame costs and actual
   picture output. This supplies the missing paired attribution of the gap.
7. If SPU `sync` or main mixing is substantial, repeat A/B with `nosputhread`,
   using otherwise identical settings. Report worker items/steals; do not raise
   the syscore allowance. Existing notes show ~3% gains in Crash/GT2 and little in
   CTR, which forces frequent sound synchronizations. For audio checksum checks,
   compare `acap` repeats with the **same** thread mode: the upstream worker's
   checksum legitimately differs from single-thread output.
8. Play each game for several minutes, listening for continuous music/effects,
   pitch changes, crackle or regular audio gaps. Record whether gameplay is
   actually advancing at the measured speed. Watch the cadence: steady DSDS or
   DDS is preferable to intermittent bursts; reject frequent level oscillation,
   black flashes, torn/stale pictures or unacceptable 6–10 fps presentation.
9. Check menu return, load a protected state, a spare-slot save/load, FF release,
   suspend/resume, blank/unblank, movies (24-bit path), FPS/OSD disappearance,
   touch controls and slider changes. Test Crash double buffering and CTR/GT2
   render-to-texture effects. Verify `noskip`, fixed frameskip, software renderer
   and the legacy path still behave normally. New GPU worker dispatch needs its
   own subsequent console smoke test; the Old run cannot validate it.
10. Clear and download switches to verify zero bytes, restore the release setup
    as agreed with its owner, and archive final screenshots/logs. Do not push or
    fold this experiment into v0.3.0.

Hardware acceptance target is sustained ~100% game speed without audio underruns,
a cadence that is comfortable to watch, no corruption on drawn pictures, and
no speed above the normal clock target over a sustained window. Where the
emulation/offscreen-rendering floor prevents this at the cap, report the limit;
do not quietly mute sound, lower PS1 clock or increase skip depth indefinitely.

## Window controller hardware experiment

`rtfs=old` explicitly selects the original EWMA controller. `rtfs=new` selects
an experimental observer using nonoverlapping 24-vblank work windows. Bare
`rtfs` retains the original controller while hardware acceptance is pending.
The rational picture phase/cap, game clock and audio path are identical.
A 24-vblank window covers complete 30fps DDDS/DDS/DS/DSS/DSSS patterns, avoiding
false overload decisions at the high point of an EWMA heavy/idle ripple.
Windows at the four-skip floor may contain partial periods; that level retains
the same cap. Density reductions require48vblanks; recovery120 plus predicted
headroom, with a bounded stale-slope probe. The measured mean must fit99.5% of
the frame budget to absorb the observer/outer-loop work.

Native alternating-load regression:9ms mandatory +11ms full-draw mean settles
at15pictures/s with original EWMA, although20pictures/s fits16.33ms/vblank.
The new window observer holds the20-picture cadence without late steady-state
changes. This is a controller fixture result, not a hardware speed claim.
`rtfslevel=0..6` remains available to measure feasible fixed cadences separately.
