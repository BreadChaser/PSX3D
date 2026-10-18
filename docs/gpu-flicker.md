# Crash edge polygons and GPU presentation flicker

> Historical report: submodule/patch instructions apply to its original commit.
> Current builds use the committed `core/` directory; see [vendor-core.md](vendor-core.md).

Implementation branch: `t3/fix-gpu-polygon-flicker`, based on `9b4dc04`.
Only harness commit `85423f1` was imported (as `cec36d0`); none of the reference
branch's sampling or mask fixes was imported. No console access or push occurred.

Renderer commits:

* `d6158db`: preserve polygon shape across draw-offset overflow; clamp fast-path
  target-edge vertices consistently with the existing slow path.
* `a389c8c`: finish PICA work before Citro2D; reset scissor; snapshot held frames.

## Polygon cause and evidence

`draw_poly` independently wrapped every coordinate **after** adding the draw
offset. Crash's two horizontal VRAM buffers use offsets `(256,120)` and
`(768,120)`. A legitimate triangle can cross X=1023 in the second buffer.
Wrapping its vertices separately changes its shape and makes the subsequent
1023-wide/511-tall size test reject it.

An actual textured Gouraud triangle (`0x34`, packet word 78 in the record between
host frame markers 55 and 56) has raw vertices
`(219,-62),(241,-130),(418,-65)`. With offset `(768,120)`, they should be
`(987,58),(1009,-10),(1186,55)`: width 199, height 68. The old renderer instead
produces `(987,58),(1009,-10),(-862,55)` and rejects width **1871**.
The affected right buffer is displayed in frames 58–59. Frame 60 displays the
other buffer. Another triangle at packet word 72 turns X=1024 into -1024 and
fails with a false width of 2045.

The fix keeps coordinates linear through the extent check. In the slow path,
each triangle then gets one common wrapped offset based on its lowest X/Y,
matching UNAI/neon. Quad halves are handled separately. Actual oversized
triangles remain rejected. CPU draw-area clipping and its half-open bounds
remain in place; the GPU never receives far-offscreen geometry.

Fast-path vertices at exactly X=1024/Y=512 now get the existing slow-path
quarter-pixel upper clamp (1023.75/511.75). The source already documents a
hardware failure at these exact boundaries. This consistency change cannot be
validated by the fake GPU's hardware precision model; it produced **zero pixel
changes in all 600 five-game replay images** compared with the offset-only fix.

Images are **P.E.Op.S. reference | 9b4dc04 PICA | fixed PICA**:


The same bug also drops **screen-wide** quads: during level entry, `0x2a` at
packet word 728 in host-frame interval 327 contains raw
`(-256,-120),(256,-120),(-256,120),(256,120)`, offset `(768,120)`.
Its expected rectangle is `(512,0)-(1024,240)`; independently wrapping X=1024
rejects both triangles with width 1536. The missing fade affects both screen
edges, visible at frame 330. A cold-boot title fade fails similarly at frame
1440, without loading any state.


Full primitive coordinates, area, record byte offsets and packet offsets are in
o3auto,
level entry,
and cold boot.
The parser is [inspect-polygons.py](../tools/gpu-compare/inspect-polygons.py).
Trace frame markers describe current scanout; a drawing interval can populate
the opposite framebuffer for a later marker. They are not primitive timestamps.
An interactive image comparison
switches between the three diagnostic scenes.

**Left-edge limit:** the stationary and moving `o3auto` traces did not reproduce
localized left terrain dropout. Level-entry/boot changes on the left include
missing full-screen fades, not proof of a separate left-edge clipping defect.
Both-edge synthetic tests translate the same crossing triangles between both
framebuffers and verify identical pixels. The concurrently updated hardware
notes report that disabling the widescreen squeeze removes inspected left-edge
popping in both software and GPU, while GPU right-edge loss remains. That is
evidence of a second contributor: game-side culling exposed by the wider view.
This change does not change the screen-mode default or the game's visibility
rules. Retest the user's exact scene with widescreen off and on.

## Black-frame fixes

Previously `video_present` prepared Citro2D, reset blend/alpha, marked PICA state
lost, cleared/bound the top target, then called `pica_picture`. That query began
with `flush()`. A pending final PS1 batch could therefore call `apply_base` and
replace the **target, shader and vertex layout after Citro2D preparation**.
`GPUupdateLace` has early returns that leave such work pending, particularly
under frameskip. The leaked PS1 scissor can also exclude the rotated top target.

`video_present` now calls `pica_prepare_present` before Citro2D/target setup.
It emits the final PS1 batch and invalidates the GPU texture cache before UI
sampling. `pica_picture` only queries the texture and UVs; both stereo calls are
safe. Presentation explicitly disables scissor and retains the existing alpha
and blending reset. The existing queue-completion and C2D buffer waits stay.

There was also no actual held picture: `pica_flip` retained only VRAM
coordinates, although skipped frames still execute large fills and CPU uploads.
At each drawn flip with frameskip enabled, it now queues a GPU copy into a
private texture **before subsequent skipped-frame writes**. This is a genuine
snapshot, with no CPU pixel copy or GPU wait. Only the scanout's aligned 8-row
tile bands are copied. A state load invalidates it; RGB24 still uses CPU video;
display blanking still returns black. The luminance probe samples the selected
picture, including the held texture (still asynchronously).

Host tests establish the ordering/lifetime faults and their fixes. They do not
measure the actual 3DS scanout or prove that every reported black frame has
been eliminated. Hardware notes before this fix include Old Crash auto-skip
8/125 black received frames on dual and 12/125 on plain `9b4dc04`, versus zero
in the corresponding skip-off controls. Silent Hill dual auto-skip was 31/155.

## Verification

Five fresh 120-host-frame traces were captured with a full reference VRAM
snapshot every frame. Native P.E.Op.S. matched **all 600 full-VRAM snapshots**.
The exact traces were replayed before/after the renderer changes, including
every output frame, rather than only selected screenshots.

| State/frame | MAE before → fixed | Pixels with any RGB error >24 before → fixed |
|---|---:|---:|
| o3auto / 59 | 10.696 → 6.393 | 14.638% → 8.636% |
| o3auto / 60 | 6.393 → 6.393 | 8.629% → 8.629% |
| level entry / 330 | 26.993 → 8.405 | 79.267% → 14.820% |
| cold boot / 1440 | 35.315 → 0.513 | 69.596% → 0.997% |

Silent Hill, R4, CTR and GT2 are **byte-identical in all 120 output frames each**.
Crash changes 58/120 images, in the affected right-buffer phases; the stationary
trace's leftmost quarter stays byte-identical in all frames. Across that trace,
431,288 pixel/frame samples move below the >24 threshold and 3,538 move above it.
Restored textured polygons retain the existing interpolation/dither/mask errors;
therefore this is not a claim of zero new differing pixels inside their coverage
or a pixel-perfect match to P.E.Op.S. The reference branch has separate fixes
for some of those residual errors, deliberately excluded here.

Additional exact-stream A/B replays used:

* `o3auto`, 600 frames holding Up, normal GTE scale; outputs every 30 frames.
* The same 600-frame path with widescreen Q16=52429; confirmed changed GP0
  coordinates, not merely an unused option. No new localized left loss was
  established; right-loss fixes remain visible.
* `crash_n3ds`, 600 frames from level loading through Crash's beach landing;
  outputs every 30 frames, including frame 330 above.
* Two cold-boot runs of 6000 frames: logo/title → world-map, and logo/title →
  introductory cutscene. Input scripts and configuration are in the manifest.
  These do not claim a complete cold-boot playthrough of the first level.

Those runs verify full reference VRAM every 60 frames and replay all commands;
the image sampling interval is 30. Trace hashes/counts and exact image comparison
counts are in manifest.json
and metrics.json.

Draw hashes, 600 frames, `o3hw_off.ini`, with `PICA_SIM_HASH`:

| State | 9b4dc04 | Fixed |
|---|---|---|
| o3auto | 6ba7ef2f6e06cf04 | 4a529fb5902bf62c |
| sh | 244abb4a61d295c3 | 244abb4a61d295c3 |
| r4 | 6e1a6f9b64542f09 | 6e1a6f9b64542f09 |
| ctr | 497b85e24ef6ff72 | 497b85e24ef6ff72 |
| gt2 | 7b3a81441ad9b8aa | 7b3a81441ad9b8aa |

Crash changes because previously rejected polygons are emitted and exact
target-edge vertices are clamped. With only the offset fix its hash was
39cffb0e352c8028; the extra clamp changes vertex records without changing these
120 host raster images. The other four hashes remain exact matches.

`BUILD_DIR=/tmp/flicker-sim bash tools/gpu-compare/regress.sh` passes AddressSanitizer
and UndefinedBehaviorSanitizer, including cleanup. It tests left/right crossings
in both buffers, negative overflow/common X/Y offsets, the exact 1023/511 legal
limits versus 1024/512 rejection, quad halves, fast target edges, final-batch
handoff, stereo queries, skipped clears in both vertical buffers, state-load
invalidation, display blanking and RGB24 routing. The baseline fails the
right-buffer crossing assertion. This is an independent behavior test, not
comparison of two copies of the decode function.

`make -j16` and `make -j16 cia` succeeded using the provided environment,
bannertool and makerom. Artifacts are this worktree's `psx3d/PSX3D.3dsx` and
`psx3d/PSX3D.cia`; SHA-256 is in the manifest. A clean upstream core archive plus
all patches reproduces **every source file** in the patched core byte for byte.
`git diff --check` passes. The locally modified core is expected; patch 0002 is
the committed source of truth.

## Console worker handoff

Test these renderer commits on the same frontend/core baseline, regenerating
0002 if merging into a newer dual-renderer branch. Do not overwrite its later
renderer-selection/VRAM-reservation changes. Keep clock, sound/GPU threading,
stereo, crop/stretch and benchmark switches identical for each A/B pair.

1. **Geometry, skip off, widescreen squeeze off:** Crash, N. Sanity Beach,
   both consoles; Old slot 2 / New slot 0, plus a fresh New Game. Watch the
   ship/foliage/ground on the right and both upper corners while standing near
   the first crates and running forward/back. Check the landing/fade from the
   loading screen, title fade, and both terrain edges. Alternating missing
   triangles and alternating missed fades should disappear. Capture upright
   frames; host X=512/0 are the alternating PS1 source buffers, not screen sides.
2. **Widescreen control:** repeat the same movement with the squeeze enabled,
   then disabled. Separate shared game-culling edges from GPU-only loss; record
   the actual screen mode and renderer in the log. Any remaining left-localized
   mesh dropout with squeeze off needs a new scene/trace, not relabeling the
   full-screen fade result as proof that all left geometry is fixed.
3. **Black flashes:** Old Crash slot 2 and Silent Hill slot 2, GPU auto-frameskip
   on, same 15-second warm-up and 30-second complete-JPEG capture procedure as
   `devtools/flicker_results.md`. Repeat skip off. Check held pictures during
   repeated skips and fades; source picture should remain stable except genuine
   game transitions. Repeat on New and the CTR scene that previously flashed.
   Check `pic gpu/none/cpu`, selected-picture luminance and top-frame probes;
   count actual received images, not one-second probes.
4. **State/UI transitions:** load a state while holding a skipped picture,
   open/close the menu, return to the library, and exercise stereo left/right
   eyes. Check borders/UI on both screens for leaked scissor/shader state.
5. **Old CPU cost:** benchmark the five usual states, skip off and auto-skip.
   The geometry correction necessarily emits missing work; the fast-path clamp
   adds a few integer operations per vertex. The held texture adds **1 MiB
   linear memory**, no extra VRAM allocation, and one queued copy per drawn flip
   only while frameskip is enabled (480 KiB for a 240-row scanout, up to 1 MiB).
   There is no new per-frame CPU pixel copy, readback or queue wait. Measure FPS,
   cycles, GPU time and memory; host timings cannot predict Old 3DS I-cache cost.

No hardware pass or performance claim is made by this implementation report.
