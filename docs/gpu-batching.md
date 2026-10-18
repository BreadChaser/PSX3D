# Silent Hill additive batching

Implementation branch `t3/silent-hill-gpu-batching`, based on `9b4dc04`.
`sh` means **Silent Hill**. No consoles were contacted, no deployment or push
was performed, and no approximate/per-game cheap mode was added.

Silent Hill's supplied scene repeatedly alternates ordinary geometry with
additive effects. A bounded queue groups additive polygons across later
**disjoint** opaque/half-blended polygons. On host frames 31–60 this reduces
239.03 to 141.90 draw calls (−40.6%). With the reference branch's STP and other
two correctness fixes, it reduces 358.50 to 213.33 calls (−40.5%).

## Profile: what actually costs work

Source: ARM simulator, supplied `states/sh.st0`, `o3hw_off.ini`, `SMALL=1`.
Profile window: cumulative 600 frames minus cumulative 300, divided by 300.
Games draw on about every other host frame; all counts/times below are **per
host frame**, not per rendered/game frame. Capture/replay uses a separate
60-frame reference trace and therefore has slightly different averages.

The diagnostic command markers attribute inclusive CPU instructions/cache cost
through nested renderer functions to each GP0 type. They add overhead and alter
layout. The following costs locate work; use the uninstrumented table below
for A/B results. Citro3d/GPU calls are stubs in the ARM simulator, so these times
**omit real draw/state submission cost**. Modeled cycles use the existing
`cprof.py` formula and 268 MHz. They are not hardware times.

| GP0 | Primitive in this scene | Commands/frame | Modeled CPU ms/frame |
|---|---|---:|---:|
| `3e` | Gouraud textured additive quad | 123.00 | 1.804 |
| `38` | Gouraud untextured opaque quad | 123.50 | 0.925 |
| `26` | Flat textured additive triangle (snow) | 123.44 | 0.511 |
| `3c` | Gouraud textured opaque quad | 79.64 | 0.381 |
| `34` | Gouraud textured opaque triangle | 23.71 | 0.079 |
| `3a` | Gouraud untextured additive quad | 12.50 | 0.067 |
| `02` | VRAM fill | 0.50 | 0.020 |

There are **no sprite/rectangle or line commands** in this Silent Hill trace.
The small particles use polygons, so a sprite-only specialization would miss
this workload. The existing direct polygon path excludes additive drawing;
it builds/clips/finishes these particles through `draw_poly_slow`/`emit_tris`.
Before the change, uninstrumented exclusive costs include about 206k
instructions/frame in `emit_tris`, 73k in `draw_poly_slow`, and 243k in
`renderer_do_cmd_list` (includes inlined direct polygon processing). Clipping
also calls `lrintf` about 58k instructions/frame. Existing source-line and
hot-block profilers were used to inspect this path.

Steady-window counters:

| Event | Per host frame | Meaning |
|---|---:|---|
| Emitted primitives | 356.22 | After culling/clipping |
| Batches/draws | 232.27 | Steady 300–600 window; replay 31–60 is 239.03 |
| Blend flips at nonempty boundaries | 228.42 | Dominant batch breaker |
| Mask-setting flips | 9.00 | Often coincide with a blend flip |
| Draw-area changes | 0.50 | Alternating framebuffers |
| Atlas rebind flags at boundaries | 1.50 | Newly decoded texels |
| Pass / texture-combiner / atlas-object flips | 0 | All use shared atlas and modulation |
| Texture-page changes between lookups | 102.78 | Already absorbed by shared atlas |
| CLUT changes between lookups | 149.40 | Already absorbed by shared atlas |
| `texture_for` calls / key hits / key misses | 348.78 / 348.78 / 0 | Warm working set fits |
| Invalidations / tile decodes / evictions | 1.50 / 2.00 / 0 | No cache thrash |
| GPU readbacks / syncs | 1.00 / 1.00 | Texture-source coherence remains required |
| CPU uploads | 0 | After state-load warmup |
| VRAM fills | 0.50 | Deferred CPU clears already avoid most work |

Independent boundary fields can differ together; do not sum them as mutually
exclusive reasons. Over reference frames 1–60 there were 21,311 texture
lookups, 27 new keys, 87 invalidations and 368 tile decodes: startup accounts
for the cold misses. These findings rule out another atlas merger or a
texture-cache resize as the primary fix for this scene.

## Implementation and ordering

* Additive polygons inside the drawing area now use the existing direct
  vertex-writing path. This removes general triangle reconstruction, clipping
  checks, bounding-box rescans, temporary copies and per-vertex finishing.
  A raw/untextured additive polygon retains the same colour/texel/alpha values.
* The common repeated-state test compares the four adjacent byte fields
  (blend, texture mode, pass, mask) as one word. It retains area-generation and
  atlas-rebind checks. No new renderer variant or per-type clone is added.
* Up to 32 additive primitives / 192 vertices wait in a cached staging queue
  (2,724 bytes in the 3DS build). Later ordinary polygons cross them only if
  their inclusive screen-space bounding boxes do not intersect any queued
  primitive. Original order within each group is retained. No overlap test or
  staging copy is needed when there are no queued effects and blending is
  ordinary opaque/half.
* E6 mask force/check disables deferral; area changes, actual slow-path draws,
  queue capacity, command-list splits, uploads, readbacks, texture reuse and
  frame presentation drain the queue. Texture lifetimes are marked both when
  queued and when copied into the linear vertex buffer. The current ordinary
  state is restored after draining because allocation can split/synchronize
  after its caller has already selected a state.
* Queue vertices live in ordinary cached memory. The code never reads the
  uncached/write-only linear vertex buffer to move a primitive. Consecutive
  additive primitives that are already in the ordinary additive batch keep
  the existing path.

`gldbg=65536` (simulator: `PICA_DEBUG=65536`) disables only deferred grouping.
It retains the direct additive path and word state test, and is useful for
isolating queue cost on hardware. It does not skip visual work. Default
behaviour draws every primitive. No nearly-invisible effect was dropped.

An earlier offscreen-cull experiment changed GT2 replay pixels and was removed.
The more conservative batching described here preserves both scanout and full
VRAM output in the tests below. Host cache-layout gains are not a reason to
ship any of these changes without the Old 3DS check.

## Simulator measurements

The baseline and candidate use the same copied simulator core, compiler,
options, game/state and `SMALL=1`. Both binaries are **uninstrumented** `-O2`
ARMv6 builds. `cbench.sh` measures 300→600 host frames. Instructions and cycles
are millions per host frame. The estimate is instructions + 70×I misses +
70×D-read misses + 4×store misses, matching `cprof.py`; it omits real citro3d
submission and is sensitive to layout. Precise counters are in
measurements.json.

| Game | Instructions before → after | Modeled cycles before → after | Instruction change |
|---|---:|---:|---:|
| Silent Hill | 2.0585 → 2.0138 | 4.3322 → 4.3017 | -2.17% |
| Crash (`o3auto`) | 2.3036 → 2.3244 | 3.6228 → 3.5941 | +0.90% |
| CTR | 2.8988 → 2.9062 | 5.2497 → 5.1832 | +0.25% |
| GT2 | 3.1850 → 3.1775 | 5.7056 → 5.6069 | -0.23% |

The remaining small instruction increases in Crash and CTR are explicit
tradeoffs of the queue dispatch/common path. Modeled total cycles decrease in
all four scenes, but that cannot establish no regression on a 16 KB-I-cache
Old 3DS. In particular, Crash's draw count is unchanged, so check it on hardware
before accepting the common-path overhead.

The equally STP-corrected Silent Hill A/B run is **2.0971 → 2.0391M instructions** and **4.3097 → 4.2877M modeled cycles** per host frame. The actual draw/state submission savings are absent from this stub-GPU measurement.

## Picture and lifetime validation

The reference harness from `t3/gpu-renderer-reference-harness` was copied into
`tools/gpu-compare`. Its P.E.Op.S. captures are unchanged and native reference
replay verifies all 60 embedded full-VRAM snapshots/game. The candidate uses
the same GPU command streams; it does not recapture after optimization.

* Four games × 60 host frames: **zero changed scanout pixels and zero changed
  full-VRAM hashes**, including all mask bits/offscreen pixels, compared with
  `9b4dc04` at the same correctness level.
* The same four × 60 checks pass with all three correctness fixes integrated:
  polygon sampling, transparent-additive mask preservation, and `250bae5` STP.
  The queue coalesces their extra mask draws. This confirms no new differences
  from P.E.Op.S.; it does not claim to fix the existing renderer errors.
* Replay draw calls/frame, frames 31–60:

| Game | Baseline → batching | With three correctness fixes, baseline → batching |
|---|---:|---:|
| Silent Hill | 239.03 → 141.90 | 358.50 → 213.33 |
| Crash (`o3auto`) | 9.00 → 9.00 | 13.50 → 13.50 |
| CTR | 22.70 → 22.70 | 31.77 → 32.00 |
| GT2 | 132.43 → 88.47 | 197.00 → 131.33 |

The tiny CTR STP-window increase is retained openly; the whole 1–60 trace is
33.20→32.75 calls/frame. Per-game defaults do not hide it.

* Five synthetic streams compare candidate vs baseline scanout and complete
  VRAM: disjoint effects, overlapping effects, >32 queued primitives, area/
  mask/upload barriers, and >16,384 emitted vertices. All match, both with and
  without the three correctness fixes.
* Existing exact mask/order regressions pass on the integrated candidate.
  The documented half-blend+mask-check approximation and quarter-rounding
  difference remain. Baseline P.E.Op.S. error metrics at frames 10/30/60 are
  unchanged byte-for-byte by batching.
* AddressSanitizer replay (including leak checks) passes all four game traces.
* All five 600-frame draw hashes match baseline with grouping disabled.
  Default grouping changes vertex submission order in Silent Hill/CTR/GT2,
  as expected; Crash and R4 retain their hashes. Raster/VRAM comparisons above
  validate reordered output.

| State | Baseline / grouping disabled | Default grouping |
|---|---|---|
| `sh` | `244abb4a61d295c3` | `b625b8b02c4dc7bb` |
| `o3auto` | `6ba7ef2f6e06cf04` | `6ba7ef2f6e06cf04` |
| `r4` | `6e1a6f9b64542f09` | `6e1a6f9b64542f09` |
| `ctr` | `497b85e24ef6ff72` | `08e4caf07cfbe842` |
| `gt2` | `7b3a81441ad9b8aa` | `e2e7098c8fcb245a` |

The optional [stp-integration.patch](../tools/gpu-compare/stp-integration.patch)
reproduces the tested combination on a private copy of the candidate core.
It is **not applied to patch 0002**. It also preserves the submission/double
buffer/cache-flush changes which the older reference branch lacks. Integrating
STP later requires keeping `batch_semi` separate while effects are staged,
setting it when the effect batch is emitted, and retaining the untextured
alpha/dummy-texel tags on the new direct path.

`source /home/tony/Development/PS1_3ds/devtools/env.sh && make -j16` succeeds.
`make -j16 cia` also succeeds with the documented bannertool/makerom paths;
local `.3dsx`, `.elf` and `.cia` outputs are in this worktree's `psx3d/`.
Patch 0002 was regenerated in this worktree with the `mk.sh` diff paths,
`--abbrev=7`, and `diff.mnemonicPrefix=false` / `diff.noprefix=false`; new core
files were made visible with `git add -N`. Reverse check and application to a
fresh upstream archive reproduce the renderer byte-for-byte. No submodule
commit is needed: core changes remain represented by patch 0002.

## Console-owner benchmark

1. **Silent_Hill first, Old and New 3DS, frameskip off**, same supplied state,
   clock/preset and compiler for `9b4dc04` vs candidate. Capture FPS, cycles,
   instructions, I-cache misses, emulation ms, renderer command/state/draw ms,
   draw calls and total GPU/queue wait. Run back-to-back more than once.
2. Repeat candidate with `gldbg=65536` to isolate queue savings from the direct
   path. Compare the optional STP-integrated candidate with its equally
   STP-corrected baseline; compare correct pictures at the same settings.
3. Check snow, fog, flashlight/character halo and mask-dependent effects while
   moving, not just a still image. Keep the known pre-STP halo separate from
   a batching regression. Confirm menus and resumed games as usual.
4. Run Crash, CTR and GT2 (Old slot 2 / New slot 0 per project guidance),
   especially Crash's unchanged draw count and CTR's readbacks. Judge the
   remaining instruction/cache tradeoff on **Old 3DS**, then retain or revert
   based on those results. No hardware speedup is claimed here.

## Reproduction and retained data

[tools/gpu-profile/README.md](../tools/gpu-profile/README.md) documents the
inclusive command profiler and precise uninstrumented summarizer.
[tools/gpu-compare/README.md](../tools/gpu-compare/README.md) documents replay,
full-VRAM hashes, ordering fixtures and optional correctness integration.
JSON evidence retains counters, draw calls and every failed-frame list (empty).
The private run directory is
`/home/tony/Development/PS1_3ds/devtools/sh-batching`; `/tmp/sh-sim` is a symlink
there because the original tmpfs copy hit quota. Cleanup reduced this task's
private data from about 1.7 GB to 153 MB. Intermediate images, duplicate
cores and old binaries were removed after validation; small results/logs and
selected picture comparisons are retained. Existing traces in
`/tmp/correct-sim` are reused read-only and are not deleted by this task.
