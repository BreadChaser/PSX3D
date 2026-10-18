# GPU submission measurements (2026-10-17)

Hardware: Old 3DS, installed CIA, slot 2; New 3DS, Homebrew Launcher netload,
slot 0. Frameskip disabled, PMU events `0x11,0x00`, normal PS1 clock. Means of
the final 15 one-second samples. All measurements include NTR streaming.
Logs, screenshots, and experiment binaries are in ignored
`devtools/gpu_overlap/`; the original `30c62a9` binaries and captures are in
`devtools/gpu_overlap_baseline/`.

## Design

Gameplay enables overlap and targeted flushing by default. Diagnostic switches
`nogpuasync` and `nogpuflush` restore blocking recording and whole-heap flushing
respectively; `gpuasync` and `gpuflush` turn them back on. Menus use the
conservative path regardless of these switches.

An altered citro3d 1.7.1 render queue records into alternate command-list
regions while the previous frame runs. Its completion event is signaled after
screen swapping, so buffer reuse cannot race the completion callback. Only one
GX queue is submitted at a time. Renderer vertices use alternate halves of the
existing allocation. Texture invalidation, eviction, staging uploads, and
readbacks wait before overwriting data that earlier commands may still read.
The citro2d buffers are shared, so presentation waits before writing them.

Targeted flushing cleans the renderer's newly appended vertex suffix and
citro2d's referenced vertex/index ranges before submission. Texture decoding,
upload staging, and software-frame uploads already clean their own writes.
Each command list is cleaned locally with `svcStoreProcessDataCache`;
`GX_CMDLIST_FLUSH` at FrameEnd suppresses citro3d's implicit whole-heap flush.
The flag is cleared on the queued GX command because the local clean already
made the list visible. This avoids repeated cache-maintenance requests to GSP.
Menus retain whole-heap flushing.

The `heap flush` statistic times actual whole-heap GSP calls. `queue wait`
includes waits for readbacks as well as frame completion. The existing `wait`
field measures frame-begin time only. The `gpu` field samples citro3d's last
completed submission once per presented frame; it is not total GPU time when
readbacks split a frame into several submissions. Moving submissions or waits
changes what that sample includes.

## Experiments

Starting the GX queue at the renderer's existing early split points produced
correct pictures, but slowed Old 3DS Crash and GT2. Removed. Splitting command
lists alone does not start the queue: citro3d normally starts it at FrameEnd.

The first double-buffer build reduced frame-begin waits to 0.01 ms, but Old
Crash fell from 43.33 to 40.35 fps in a same-binary comparison. GT2 was unchanged
(28.60 to 28.46). CTR improved from 24.80 to 25.15. These results do not establish
the cause; CPU cycles and I-cache misses also changed. A subsequent revision
reused the normal bank whenever the preceding queue had already finished and
replaced per-draw vertex tracking with one append-only flush per submission.

Whole-heap flushing measured about 0.30 ms/frame for Old Crash and
0.84–0.93 ms/frame for Old CTR (including readback submissions). On New 3DS it
cost about 0.15–0.19 ms for Crash and 0.50–0.56 ms for CTR.

Within the revised binary, New CTR improved from 48.95 fps / 10.936M cycles to
54.83 fps / 10.157M cycles with overlap and targeted flushing. New Crash and
GT2 remained near their 60 fps cap. Screenshots of all three games were correct.

Code layout matters on Old 3DS: one intermediate control ran Crash at 43.33 fps,
another at 39.50. Compare switches in one binary, and compare the final binary
to the original build. Do not attribute all between-build gains to submission.

## Picture checks

`30c62a9` draws the game at the correct top-screen size, with no garbage or
quarter-size image. Opening the touch MENU preserved the supersampled menu.
Old and New Crash, CTR, and GT2 were captured during the submission experiments;
CTR's readback-heavy test scene remained correct.

## Final New 3DS check

| Game | FPS | M cycles/frame | Begin wait, ms | GPU sample, ms |
|---|---:|---:|---:|---:|
| Crash | 59.98 | 6.306 | 0.000 | 5.891 |
| CTR | 54.77 | 10.133 | 0.000 | 2.231 |
| GT2 | 59.54 | 8.790 | 0.000 | 6.167 |

Final screenshots: `n3_Crash_final.png`, `n3_CTR_final.png`,
`n3_GT2_final.png` in `devtools/gpu_overlap/`. All three exited normally back
to HOME. The game scenes and aspect ratios were correct. GT2's small variation
around the 60 fps cap also occurs in control runs.

## Old 3DS final comparison

The original `30c62a9` CIA was reinstalled immediately before the final CIA.
Each build ran Crash, then CTR, then GT2 with the same automated 45-second
capture procedure and slot-2 states. The baseline accepts neither new switch;
the `nogpuasync nogpuflush` arguments have no effect on that original build.

Values are before → after. The final build enables both optimizations.

| Game | FPS | M cycles/frame | Begin wait, ms | GPU sample, ms |
|---|---:|---:|---:|---:|
| Crash | 38.51 → 40.76 | 5.884 → 5.736 | 1.109 → 0.010 | 6.324 → 6.579 |
| CTR | 24.66 → 25.52 | 8.664 → 8.543 | 0.914 → 0.011 | 3.629 → 2.218 |
| GT2 | 28.13 → 28.74 | 8.473 → 8.396 | 0.698 → 0.010 | 6.128 → 6.377 |

FPS gains: Crash 5.9%, CTR 3.5%, GT2 2.2%. Final whole-heap flushing is zero
in all three gameplay runs. Final total queue waits are 0.005, 2.444, and
0.022 ms/frame respectively. CTR's necessary readback waits remain; the large
change in its GPU sample does not mean that total GPU work fell by that amount.

Captured pictures in `old_Crash_final.png`, `old_CTR_final.png`, and
`old_GT2_final.png` are correct. The original and final logs use the suffixes
`_before.log` and `_final.log`. No early queue-start path remains.

The final touch MENU was opened and resumed successfully. Its supersampled
layout and background fill remain correct (`old_final_menu.png` and
`old_final_menu_resume.png`). The CIA and 3dsx came from the same build.
