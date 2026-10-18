# Normal-play frontend overhead

Branch `t3/frontend-overhead`, based on `b5411a1` (`t3/frameskip-ff`).
Implementation: `79137a8`. No console contact or push. This is an implementation
handoff for the console worker; hardware performance and speaker acceptance
remain unmeasured.

The simulator shows a modest CPU saving, **not evidence that normal play now
matches fast-forward**. LCD composition is absent from the fake SDK, so its
largest potential hardware saving cannot be measured here. Existing evidence in
[frameskip-ff.md](frameskip-ff.md) still governs interpretation of the FF gap.

## Changes

* Retain the game LCD picture in **every normal frameskip mode**, including off,
  legacy auto, fixed/threshold, software and `rtfs`, when the source is unchanged.
  GPU callbacks use the existing snapshot/VRAM serial and display configuration;
  software and RGB24 callbacks use pending uploads. NULL libretro video callbacks
  alone are not a GPU change signal. RGB24 remains conservative: always present.
* Split top/bottom composition. A bottom-only stats, touch or OSD update does not
  clear/bind/draw/transfer either top target. A fully held frame opens no frontend
  C3D frame and runs no C2D UI. Required renderer offscreen work still submits via
  `video_hold()`; renderer drawing/readbacks can still open frames themselves.
  Lazy command-list opening and snapshotting on idle display flips now apply
  outside `rtfs` as well. No renderer primitive/skip policy changes.
* Keep the existing emulated-frame clock deadlines, debt bound and sleeps.
  Gameplay had no explicit `gspWaitForVBlank`; menus retain theirs. No extra
  emulated frames, lower PS1 clock, audio muting or pacing bypass is introduced.
* Clear transient UI text storage only when a screen is repainted. Touch-layout
  and backlight/capability checks run on shown frames (and touch transitions or
  the initial timeout); input polling, touch/menu detection and audio queue
  feedback remain per emulated frame. Software depth/stereo work remains where
  required. Stats formatting remains once a second; its wall-clock query is now
  gated by the already-read system tick.
* Static OSD text redraws on replacement, appearance and disappearance. Its
  lifetime counts outer gameplay loops independently of redraws. Bottom stats
  invalidation is a few scalar comparisons, with no HID reads, text parsing,
  GPU commands or target submission when clean. It is not literally zero CPU
  instructions. Input polling still processes touch gestures every vblank.
* Normal audio uses **integer cubic Hermite**, Q20 phase/Q12 weights, preserving
  the old kernel/history delay and clamping. Exact unity/integer phase uses
  `memcpy`. Queue target, rate controller and DSP polyphase conversion stay the
  same. The `step < 0.99` slow-stretch implementation is unchanged. NDSP buffer
  filling now copies contiguous blocks, with the same ownership, submission,
  capture bounds and full-queue drop behavior. CSND submission is unchanged.

A linear candidate was rejected before the final implementation: its 10 kHz
amplitude was 13,468 for a 16,000 input (about -1.5 dB), materially different from
the existing cubic response. The final cubic gives 15,312 versus the reference's
15,312 at the same phase. Only final cubic results are in the evidence files.

## Cost counters and remote merge

`stats` emits an additional line once per interval:

```
frontend emu N elapsed Nms top N bottom N present Xms audio Xms misc Xms pace Xms
```

All costs are **per actual emulated vblank**, including all four FF calls in the
denominator. `present` brackets composition or held offscreen submission;
`audio` brackets frontend `audio_write()` in either libretro callback and is
**nested in `run`**. `misc` includes queue/option feedback, presentation-change
queries, UI invalidation, backlight and save checks. Reporting/SD-write cost is
charged to the following interval so it is visible. It excludes `aptMainLoop()`
and the final cadence observer/timer bookkeeping. Pacing is separate; never add
GPU or SPU nested waits again. Existing `cadence` and PMU lines remain available.
PMU still brackets only the last FF `retro_run`, not its four-frame loop.

`emu_frontend_stats()` in `psx3d/source/psx3d.h` exposes the last completed interval
as `const emu_frontend_sample *` for the remote `stats` command. Access on the main
thread; collect with `stats`/`prof`/linked logging enabled for audio/misc timers.
The branch contains no remote server. The console worker can serialize this
structure when merging `t3/fast-test-loop`. Count top and bottom independently:
`presented` counts submissions touching either LCD; `held` can still submit
required offscreen work.

**Actual Old 3DS present/audio-write/misc costs before/after are pending.** The
historical Crash present measurement is ~1.28 ms/vblank, not a paired measurement
of this binary. Simulator GPU transfers/waits/DSP submissions cost zero; it
cannot replace those hardware brackets or validate a 2.4 ms frontend hypothesis.

## Simulator results

Private simulator at `devtools/frontend-overhead/sim`; shared simulator and
consoles untouched. Same ARMv6 GCC 14.2 `-O2`, SMALL=1, states and no-op SDK for
both sides. The runner calls the **real old/new frontend resampler** at a fixed
1.001 rate, computes the same core-audio checksum, and models the real changed
presentation decisions. It does not compile the full 3DS UI/NDSP/main loop.
Core audio mixing remains enabled in both. Hashing and performance binaries are
separate. Cost window is vblanks 300..599 from paired 300/600 runs.

Model = instructions + 70×I misses + 70×D read misses + 4×store misses, excluding
interlock cycles; numbers are modeled CPU costs, not measured ARM11 cycles or a
hardware speed prediction. Old 3DS code-layout/I-cache effects remain material.

| State | Frameskip off, M cycles before → after | Saturated legacy auto, before → after | `rtfs` fixed 1/2 cadence, before → after |
|---|---:|---:|---:|
| Crash `o3auto` | 3.84 → 3.77 | 3.12 → 3.06 | 3.31 → 3.24 |
| CTR | 5.14 → 5.07 | 4.24 → 4.18 | 4.42 → 4.36 |
| GT2 | 5.69 → 5.63 | 4.99 → 4.94 | 5.18 → 5.12 |
| SotN | 9.78 → 9.73 | 4.61 → 4.56 | 6.02 → 5.97 |

Auto requests continuous underrun to exercise its existing two-picture cap.
`rtfs LEVEL=3` is a simulator-only override for repeatable selected frames, not a
product setting. Native paced runs separately exercise the actual controller.
This isolates policy from host scheduler variation and checks both modes.

Standalone 735-stereo-frame audio model (same flags, warm window):

| Piece | Before | After |
|---|---:|---:|
| Resampling, step 1.001 | ~0.42 ms | ~0.25 ms |
| Resampling, exact unity | ~0.43 ms | ~0.01 ms |
| Existing stretch, step 0.75 | ~0.10 ms | ~0.10 ms |
| LCD present / NDSP submission / loop UI | SDK stubs / absent | SDK stubs / absent |

Mixed core+audio savings differ from isolated-kernel savings because of cache
and code layout. Full per-function profiles and precise cycles are in
`tools/frontend-overhead/evidence/{cycles,audio-cost}.json`.

All **600 cumulative draw-stream hashes match in each of 20 paired cases**:
four states × off, saturated auto, `rtfs` same cadence, fixed skip 1, fixed skip 4.
All paired core audio hashes match; every 600-vblank run produces **441,000 stereo
frames**. This verifies renderer draw/state/texel commands, not actual LCD pixels
or synchronization. In normal off mode Crash/CTR/GT2 composition falls from
600 to 301 calls (one startup picture); SotN has new output every vblank, so it
retains 600. Under saturated auto the 30-picture/s games present about 101 times
instead of 600. Existing `rtfs` already retained presentations; its saving here
comes mainly from audio. CTR/GT2 offscreen work remains authoritative.

Paced runs use 300 warmup + 600 measured vblanks, with off, native auto and native
`rtfs`, no artificial load. Each reaches approximately 60 emulated vblanks/s;
controller `rtfs` remains at level 0 on this fast host. See `evidence/paced.json`
for work/sleep split and counts. More time becomes pacing sleep rather than
faster gameplay. Host wall times vary with JIT and concurrent test load; these
are speed-ceiling checks, not 3DS VSync/DSP proof.

## Offline audio comparison

Input: synthetic stereo 440 Hz and 10 kHz tones (16,000 peak), and ten seconds of
**actual core audio captured in the private simulator** from Crash `o3auto`
(441,000 stereo frames). This is not a new hardware speaker capture. Its SHA256,
format and full results live in `evidence/audio.json`; raw game audio remains in
ignored `devtools/frontend-overhead/crash.raw`. No game assets are committed.

At correction steps 0.995, 1.000, 1.001 and 1.005:

* Unity is bit-identical to the current frontend. Slow stretch at 0.495, 0.75,
  0.98 is bit-identical, including randomized full-scale input.
* At **matching Q20 input positions**, final cubic vs float Hermite differs by
  at most **1 PCM unit** on both 440 Hz and the entire Crash capture (RMS ~0.34
  and ~0.28 respectively), and at most 7 units on 10 kHz (RMS ~2.24).
* 440 Hz at step 1.001: THD -113.4 → -113.6 dB, THD+N -80.6 → **-88.9 dB**.
  The old float phase accumulator has a tiny chunk-dependent effective-rate
  error; the new integer accumulator reduces that error. Tones at 0.995/1.005
  give ~-88.9 dB THD+N. Unity gives -91.8 dB on both.
* 10 kHz at step 1.001: matching-phase reference/new amplitudes 15,312.108 /
  15,312.104; THD+N -26.4414 / -26.4413 dB. That interpolation response already
  exists in the current cubic kernel. Integer coefficients preserve it.
* **Unaligned direct comparisons are also recorded**, not hidden: over ten
  seconds of Crash at 1.001, old/new emit 440,561 / 440,560 frontend frames,
  max deviation 4,145 and RMS 310.37. Over one second of 440 Hz, max 172 / RMS
  69.51. These include cumulative time/phase drift from replacing the float
  accumulator, not just interpolation error. Rate quantization is <0.5 ppm;
  the unchanged queue feedback still steers the output rate.
* New output is identical across 1024-frame chunks versus callbacks of
  1/13/735/1024 frames. Alternating normal/stretch ratios with full-scale random
  stereo samples pass bounds checks. No hard drop/duplicate correction or phase
  reset is introduced. This removes a source of clicks, but cannot certify
  speaker continuity or absence of queue underruns without hardware listening.

THD fits harmonics 2..10 after removing the fundamental; THD+N includes all
residual interpolation/quantization noise. Metrics describe the CPU output,
before the unchanged DSP 44.1→32.7 kHz polyphase stage. The matched-phase oracle
uses the original float Hermite with the new phase positions, separating kernel
error from rate error. Direct original comparisons remain the primary raw data.

## Validation and artifacts

* 12,000 per-vblank draw hashes and all 20 paired core audio hashes match.
* Native real cadence tests pass under ASan/UBSan.
* Actual `video_present_screens`/`video_hold` bodies compiled with a call-counting
  SDK fixture pass: idle hold, bottom-only update, pending transfer, offscreen
  submission and stereo overlays. This checks call gating, not real GPU fences.
* Actual NDSP `submit` body passes partial-buffer, buffer wrap, full-queue drop,
  retired-buffer reuse and capture-bound checks under ASan/UBSan.
* Real audio resampler passes ASan/UBSan at all 16 full-scale four-point
  corners × 4096 fractional phases, maximum/tiny chunks and correction/stretch
  transitions (`tools/frontend-overhead/audio_extremes.c`).
* Fresh patches match all 29 modified/new core files byte-for-byte; patch reverse
  check and `git diff --check` pass.
* `source /home/tony/Development/PS1_3ds/devtools/env.sh && make -j16` and
  `make -j16 cia BANNERTOOL=... MAKEROM=...` pass. Existing path-truncation,
  unused core helpers and executable-stack linker warnings remain.

Final binaries are `psx3d/PSX3D.{3dsx,cia,elf}`, archived with implementation
commit and SHA256 sums in `devtools/frontend-overhead/artifacts/`.

* 3DSX: `662ade297ee8154ac5350fe3e605cb034412954b1ee4318dd5e39069abda1b6f`
* CIA: `f98ad9bee58d56b4841eaa950c302f69bf222626da0a3394c6ba21195b638a21`

## Reproduction

Use a private simulator copy as in [frameskip-ff.md](frameskip-ff.md), keeping
`GX_TextureCopy` a no-op. Run `tools/frameskip/prepare-sim.py <private-sim>` against
this patched worktree, then apply `tools/frontend-overhead/runner.patch` to its
already-frameskip-patched runner. The fixture deliberately links real frontend
audio, rather than the supplied runner's no-op audio callback.

For the baseline, archive the upstream core into a **separate Git-initialized
scratch directory** and apply both patches from `git show b5411a1:patches/...`.
Copy its `gpu_pica.c` into the private simulator and build the core directly
using `build_pica.sh`'s make flags, retaining the simulator's platform stanzas.
Do not invoke the shared `devtools/mk.sh`; it changes to the primary checkout.
The only core change here is three lines in `gpu_pica.c`.

Extract baseline frontend audio with:

```sh
git show b5411a1:psx3d/source/audio_resample.c > <private>/audio_before.c
```

Link each core with the matching audio via
`tools/frontend-overhead/build_sim.sh <sim> <audio.c> runner_before|runner_after`.
Build separate hash-core variants with `PICA_CFLAGS="-I<sim>/fake3ds
-DPICA_SIM_HASH"`, linking `runner_before_hash`/`runner_after_hash`. Preserve
no-hash libraries separately; `make` does not notice a flags-only change, so
remove the selected private renderer object or change its source timestamp when
switching hashing. The regular `build_pica.sh` overwrites GPU sources: do not use
it when building the baseline.

```sh
source /home/tony/Development/PS1_3ds/devtools/env.sh
python3 tools/frontend-overhead/run_matrix.py <sim> <private>/cache --cache
python3 tools/frontend-overhead/run_matrix.py <sim> <private>/hashes --hash
python3 tools/frontend-overhead/run_matrix.py <sim> <private>/paced
python3 tools/frontend-overhead/summarize.py <private>
python3 tools/frontend-overhead/audio_check.py <private>/audio_before.c \
    <private>/crash.raw <evidence-dir>
python3 tools/frontend-overhead/submit_check.py
python3 tools/frontend-overhead/present_check.py
tools/frameskip/check.sh
```

`AUDIO_CAPTURE=<absolute-path>` on one private `runner_after` Crash run captures
the pre-resampler input. `FRONTEND_NEW=1` selects the new frontend presentation
policy; its absence models the baseline. Runner checksum work is identical in
both variants. The scripts need the existing private ROM/state paths from
the developer notes. No console or network control is performed by these tools.

## Hardware handoff and risks

The console worker should merge the implementation with `t3/fast-test-loop`,
expose `emu_frontend_stats()` in remote `stats`, and use the established checked
console workflow. **Do not interpret the simulator as hardware acceptance.**

1. Archive baseline `b5411a1` and changed artifacts, configs, switches and logs.
   On Old 3DS use GPU, default PS1 clock, existing 30% syscore allowance, identical
   stereo/FPS/touch/audio-stretch settings, and protected **slot 2** for Crash,
   CTR, GT2 and SotN. Never overwrite it; saves use spare slot 8/9.
2. In each scene compare baseline/new/new/baseline, ~70 seconds per run, discarding
   initial OSD/cadence warmup. Run frameskip off, normal legacy auto and `rtfs`.
   Record actual game speed, drawn-picture fps, top/bottom/present/held counts,
   `run/present/audio/misc/pace`, audio queue/underruns/drops/maxgap, GPU queue
   waits and per-emulated-frame cycles. Avoid logging to SD for final play tests.
3. Within the same build hold FF for ~20 seconds between normal segments, then
   release. Confirm `ff` and actual emulated count. Compare normal vs FF game
   speed using emulated vblanks / elapsed seconds; the outer-loop FPS and PMU
   bracket do not cover all four FF frames. The normal ceiling must stay near
   100%, never sustain FF-like acceleration. A reduction in `present/audio/misc`
   must show where any gain came from; do not infer it from FF alone.
4. Listen for several minutes in each game, including normal/stretch transitions,
   FF release, menu return and suspend/resume. Confirm no new crackle/pitch jump,
   queue underruns, stale/black pictures or bad cadence. Compare `acap` core hashes
   with identical SPU threading mode; upstream threaded/unthreaded hashes differ.
5. Check top retention while only bottom stats/touch/OSD changes, OSD replacement
   and expiry, slider/stereo changes, display blanking, movies, protected state
   load and spare-slot save/load. Crash bank flips and CTR/GT2/SotN offscreen
   rendering/readbacks are essential. Software and New 3DS GPU-worker ownership
   need separate smoke checks; Old 3DS cannot validate them.

Remaining risks: real 3DS command scheduling/target swaps, worker synchronization,
16 KB instruction-cache layout effects, DSP/CSND buffer behavior and underruns,
mode transitions, and real speaker continuity. The existing renderer change
query is conservative for offscreen writes without an active held snapshot and
for movies: it may still present unnecessarily. Required work and correctness
take priority over suppressing those calls. SotN's expensive offscreen emulation
floor and existing GPU defects are not fixed here. There is no claim of a locked
60 fps or of the hardware FF gap being closed.

## Integrated hardware diagnostic controls (t3/o3ds60)

Remote `load`/batch accepts `frontend=old|new` to force eager/held LCD composition,
`audio=old|new` to choose float/integer Hermite, `rtfs` for predictive auto skipping,
and `ff=on|off`. Remote `ff on|off` changes FF outside a batch. `ff=on` tests must
omit explicit frameskip overrides: those overrides intentionally take precedence
in the option system. FF uses the ordinary four-core-call path and mutes frontend
audio. PMU now brackets **all** four calls, and its denominator counts emulated
vblanks. The existing outer-loop FPS/present counters still count one per FF
loop; use `speed_pct` and `front_*` costs for normal/FF comparisons.

Remote samples/TSV also expose `front_run_ms`, `front_present_ms`, `audio_ms`,
`loop_ms`, `pace_ms`, top/bottom/held counts, `ff`, SPU main/wait ms, skip level,
drawn/skipped flip counts, audio underruns/drops/gap, and `pica_cpu_ms`. Costs
are per emulated vblank. The GPU CPU bracket includes command parsing/rendering
and nested readback waits; audio is nested in run; SPU wait is nested in SPU.
These values must not be added as independent top-level times.

Timings collect in memory in explicitly opted-in remote mode, without enabling
per-second SD logging. Screenshot requests happen after sampling. Old hardware
is the performance judge. Streamed preliminary data is diagnostic only; fresh
published data requires console NTR streams off, verified by the release worker.
The immutable original release baseline is kept at ignored
`devtools/release_final/baseline-2d851cf/PSX3D.cia`. All reports/evidence remain in
ignored `devtools/o3ds60/` in the main checkout, including per-batch before/status
JSON, TSV, top PNGs and sampled audio/PICA history. No speaker listening is
claimed from a remote test.

## Sampled renderer phases and PMU categories

Developer switch `gldbg=8388608` enables phase timing exported as remote numeric
columns: list (inclusive renderer command parser), primitive (polygon/rectangle
inclusive), texture lookup (inclusive decode), decode (including already-decoded
checks), state application, C3D batch draw/mask submission, readback, and fast
polygon final vertex packing. Hot phases independently sample1/16 calls using
the high bits of an LCG; command-list and readback boundaries are exact. Values
subtract the calibrated cost of one timer read and scale sampled ticks by16.
Nested times must not be added. Parse/control residual is list minus primitive;
vertex packing is only the common fast polygon path, not clipping or sprites.
These are statistical estimates: rare cache misses can be undersampled and
independent inclusive estimates need not obey exact nesting in a short sample.
Use paired profiling-off/on measurements to quantify perturbation. Disabled
phase columns are unavailable, not zero. No console-side SD stats writes are
required.

PMU additionally groups actual `retro_run` CPU cycles into three disjoint buckets:
any emitted renderer primitive; no emitted primitive but skipped GPU command
words; neither (idle/state-only). Mixed drawing/skipping belongs to drawing.
This explicitly separates heavy/light vblanks from the previous average per
emulated vblank. These buckets describe main-thread direct GPU execution on Old
3DS; they are not New3DS worker cycles or pictures. FF buckets include all4calls.
The counters use only the existing kernel performance-counter service.


## Stream-free Old 3DS follow-up (2026-10-18)

All current published follow-up runs require the private `NTR_OFF_VERIFIED`
marker after both consoles rebooted without BootNTR. The passive UDP monitor
continues to verify silence; no screen-stream viewer is restarted. Ordinary
remote end-of-run screenshots are the only picture capture.

Same-binary Crash presentation/audio A/B: eager/float42.380fps, held/integer
45.452fps (+7.25%); composition1.903→0.275ms per emulated vblank. Original
release2d851cf45.403fps versus candidate45.452fps is essentially flat, so do
not attribute the fixed-layout gain to the complete cross-binary change.
CTR same-binary28.308→29.969fps (+5.87%), composition1.797→0.283ms; original
release30.051fps. Runtime choices and exact per-piece evidence remain available.

Crash actual main-thread CPU populations are7.648M per primitive-generating
vblank and3.276M per idle vblank (unprofiled). At30 game pictures/60 PS1
vblanks, their10.924M pair exceeds the268MHz real-time8.94M pair budget.
The off average5.6M was never a drawn-picture cost; an auto average containing
some drawing is not a pure skipped-picture cost. Full30picture playback still
needs about18% less CPU work plus frontend savings. The sampled phase profiler
cost~3.1% on hardware. Inclusive primitive8.324ms and texture1.997ms dominate;
state.072ms, C3D submission.336ms and readback0 are smaller in Crash.

The complete24-vblank controller (`rtfs=new`) avoids EWMA heavy/light ripple.
In one same-binary Crash run original11.07→window12.66distinctpictures/s
(+14.4%), both100% speed and zero sampled audio underruns. Its recovery after
transient load remains conservative; an opt-in actual-cost-population model is
under test (`rtfs=model`). Bare `rtfs` retains the original policy. Higher skip
cap is explicitly diagnostic: `rtfscap=5` permits1-of-6 cadence, while the default
cap4 remains1-of-5. `rtfslevel=0..7` freezes cadence; level7 requires cap5.
No emulated clock reduction or audio muting is used to meet normal-speed goals.

Fresh CTR original legacy auto64.26% game speed, candidate66.28%; predictive
70.28%, muted ordinary FF70.60%. PICA reduction alone cannot make this scene
real time. Continuous audio acceptance fails in the slow modes; no remote
speaker listening is claimed. All experiments, exact CIA hashes, screenshots,
raw TSV and per-game limits are in ignored `devtools/o3ds60/results.md`.
