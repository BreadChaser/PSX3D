# GPU reference comparison and fixes

Worktree: `t3/gpu-renderer-reference-harness`, starting at `30c62a9`.
No console access, deployment, push or public posting was performed.

## What is available

[README.md](README.md) contains build/run commands and the model's limitations.
The harness captures the ARM P.E.Op.S. renderer's consumed GPU commands, restored
VRAM, subsequent uploads, E1–E6 state and display rectangles. Native P.E.Op.S.
and the actual worktree `gpu_pica.c` replay exactly that stream. The fake
citro3d now rasterizes the submitted triangles, samples the actual decoded
atlas and applies the submitted texture combiner, alpha tests and blend state.
This gives real visual differences without running rasterization inside qemu.
Reference-generated streams also avoid fake-GPU readback errors changing what
the game submits before the comparison.

All **60 complete reference VRAM snapshots per game**, not just selected
screenshots, matched the native reference replay byte for byte. The report
images select host frames **10, 30 and 60** after state load. These games often
draw at half the host refresh rate; these are not 60 distinct game pictures.
Some Crash snapshots repeat while the character is idle. Frames 1–2 can still
show only the restored image, which is why they are not the diagnostic samples.
Capture uses the checked-in options derived from the provided simulator preset,
with reference dithering enabled. The runner found no BIOS files and selected
built-in HLE. Trace hashes and configuration are in [manifest.json](evidence/manifest.json).

Each checked-in comparison contains **reference | PICA model | absolute RGB
difference amplified 3×**. Individual reference/PICA/diff PNGs are also generated
under `/tmp/correct-sim/{baseline,polygon-phase,stp-mask}/<state>/`. The checked-in
[gallery](gallery.html) compares the original and final images at frame 60.

## Commits

| Commit | Result | Evidence |
|---|---|---|
| `85423f1` | Reproducible capture/replay and rasterizing fake citro3d | [baseline images](evidence/before/) |
| `7d03c47` | Align polygon interpolation to the PS1 integer pixel sampling position, through a half-pixel translation on PICA; keep rectangles/uploads unchanged | [before](evidence/before/o3auto-0060.png), [after](evidence/polygon-phase/o3auto-0060.png), [high-frequency probe before](evidence/polygon-phase/fixture-before.png), [after](evidence/polygon-phase/fixture-phase.png) |
| `89392c5` | Reject fully transparent additive texels before they erase a destination mask bit | [checker before](evidence/additive-mask/before.png), [after](evidence/additive-mask/after.png) |
| `250bae5` | Write texture STP into the framebuffer mask; preserve E6 force/check semantics and ordering between mask-checking primitives | [Silent Hill before](evidence/polygon-phase/sh-0060.png), [after](evidence/stp-mask/sh-0060.png), [checker before](evidence/stp-mask/fixture-transparent-add-mask-before.png), [after](evidence/stp-mask/fixture-transparent-add-mask-after.png) |

Each renderer fix includes a regenerated `patches/0002`. New core files must be
visible to `git diff` (`git -C core add -N ...`) before regenerating. The existing
`devtools/mk.sh` hardcodes the main checkout, so the equivalent diff command was
run **in this worktree**, with `diff.mnemonicPrefix=false`, `diff.noprefix=false`
and `--abbrev=7`. A fresh upstream archive plus all patches reproduces the final
`gpu_pica.c` exactly. The submodule remains locally modified by design.

## Measured differences

Frame 60, with identical commands before/after. MAE is mean absolute RGB-channel
error on a 0–255 scale. “Large error” means any channel differs by more than 24;
it reduces the influence of one-level quantization/dithering differences, but
does not certify visual correctness.

| State / game | MAE before → after | Large-error pixels before → after | Final image |
|---|---:|---:|---|
| `o3auto` / Crash | 6.39 → 4.37 | 8.63% → 4.59% | [image](evidence/stp-mask/o3auto-0060.png) |
| `sh` / Silent Hill | 8.25 → 4.67 | 11.84% → 0.52% | [image](evidence/stp-mask/sh-0060.png) |
| `r4` / Ridge Racer Type 4 | 6.68 → 4.95 | 6.67% → 2.85% | [image](evidence/stp-mask/r4-0060.png) |
| `ctr` / CTR | 8.37 → 6.76 | 12.46% → 9.32% | [image](evidence/stp-mask/ctr-0060.png) |
| `gt2` / Gran Turismo 2 | 8.70 → 7.29 | 12.67% → 9.14% | [image](evidence/stp-mask/gt2-0060.png) |

All three sample frames per game are in [baseline](evidence/before/),
[polygon fix](evidence/polygon-phase/) and [final](evidence/stp-mask/).
Their `metrics.json` files retain unrounded values. These are **model images**;
they cannot verify PICA hardware precision, cache coherence or clipping quirks.
In particular, many pixels still differ by a small amount, and CTR/GT2 retain
substantial texture/edge errors. This is not a pixel-accurate renderer yet.

## Error classes and responsible code

Line numbers refer to the final patched `core/plugins/gpu_pica/gpu_pica.c`.

| Class | Evidence / coverage | Responsible code / status |
|---|---|---|
| Polygon UV/colour interpolation shifted at pixel centres | Crash sand/boxes, R4 building edges, GT2 wall/road and CTR ground all improve | `draw_poly` vertex packing and `emit_tris` quarter-pixel conversion; fixed the common sample-position mismatch |
| Rounded clipping intersections / residual affine and coverage differences | GT2 wall and road, CTR kart/ground, oblique high-frequency probe still disagree | `clip_area` rounds XY, UV and colour to integers before rasterization; PS1 fixed-point interpolation/coverage still differs from the PICA model |
| Missing STP → destination mask | Silent Hill's bright rectangular character halo disappears | `apply_state` formerly wrote alpha only from E6 force; now opaque batches emit STP directly and `flush` adds a GPU mask pass when RGB blending consumes alpha |
| Transparent additive texels erasing masks | Synthetic checker turns green through transparent holes before the fix | `apply_state`, `BL_ADD` alpha test; fixed |
| Mask force on additive opaque texels, plus mask ordering | Synthetic forced-add and two-overlapping-texture probes | New mask pass and `use_state` boundary when E6 check is on; fixed in tested cases |
| Half/quarter blend while E6 mask-check is enabled | [explicit remaining failure](evidence/remaining/mask-half-blend.png): reference blends red over blue, PICA replaces it | Existing destination-alpha shortcut in `apply_state`'s `d->mask & 2` block treats these modes like solid drawing; still approximate |
| Dithering and 5-bit quantization | Fine stipple/banding remains in all games; all five traces request polygon dithering | `renderer_set_config` ignores dithering; no ordered-dither stage in `apply_state`; fake GPU rounds RGBA5551, which is also a model precision assumption |
| Wrong texture depth / CLUT selection | Crash exercises 4/8-bit; other game traces exercise 4-bit. Colours/layout broadly agree; no independent wrong-CLUT class was established | `texture_for`, `decode_rect`, `texel_rgba`; not changed. Synthetic probes exercise 15-bit textures |
| Texture window | R4 has 7,083 textured commands with a window enabled over this trace | `decode_rect` implements the bit-mask window; no gross window failure established. P.E.Op.S.'s general window emulation has its own limits |
| Sprite/rectangle edges and wrapping | R4 and GT2 include rectangles; mask fixtures use sprites. Flipped sprites crossing 0/255 are not covered | `sprite` splits only unflipped wrap; flipped wrap remains a code-inspection risk |
| Lines | R4/CTR include lines; no isolated line fix was attempted | `draw_line` turns lines into one-pixel quads, not an exact PS1 line rasterizer |
| Draw area / clipping | Normal drawing areas are exercised; extreme offscreen PICA behaviour cannot be verified here | `set_area`, `clip_area`, `emit_tris`; CPU clipping still exists to work around hardware edge behaviour |
| Display offset / centering | Raw scanout deliberately excludes final 3DS placement. SH is 320×224 and CTR 512×216; stretching them to 240 rows is not the core's border placement | `pica_picture` returns a cropped VRAM rectangle; `psx3d/source/video.c:draw_game` fills a fixed display rectangle. Centering remains unfinished |
| 15/24-bit and interlace | All 300 game frames are 15-bit scanout; no movie or interlace transition covered | `pica_picture` routes RGB24 to CPU; `renderer_set_interlace` is a no-op. No claim of 24-bit/interlace correctness from this run |
| VRAM copy / readback | Replay handles CPU uploads and PICA texture readbacks. Capture records gpulib's copy result as an upload | `readback_part` and `push_to_target`, plus gpulib copy handling. GPU-side copies remain a performance feature; this trace boundary does not independently validate original copy commands |

Exact feature counts are in [coverage.json](evidence/coverage.json), reproducible
with `inspect_trace.py`. Blend modes 0/1/2 are exercised by the game set; mode 3
(quarter-add) is covered by a synthetic probe only. No input was injected, so
these are checks of the supplied scenes, not full-game compatibility tests.

## Old 3DS CPU cost and benchmark handoff

The sampling fix adds a few operations per polygon vertex, including preserving
the existing upper-edge clamp. It adds no texture decode, readback or draw call.
The transparent-additive fix changes alpha-test state only. Both can still
perturb the Old 3DS's instruction-cache layout, so host time is not a hardware
performance prediction.

STP writes stay on the GPU: no CPU framebuffer rasterization, no new texture
allocation, and no per-texel decode work. Opaque-only batches write colour and
mask together in one draw. Batches containing blending need a second draw with
RGB preserved, and mask-checking primitives must finish their mask write before
the next primitive. This adds CPU command/state submission and is the main
benchmark risk.

Average `C3D_DrawArrays` calls per **host frame**, frames 31–60, before and after
the STP fix (not per rendered/game frame, and not milliseconds):

| Game | Before | After | Added |
|---|---:|---:|---:|
| Crash | 9.0 | 13.5 | 4.5 |
| Silent Hill | 239.0 | 358.5 | 119.5 |
| R4 | 39.7 | 40.2 | 0.5 |
| CTR | 22.7 | 31.8 | 9.1 |
| GT2 | 132.4 | 197.0 | 64.6 |

Source data: [draw-calls.json](evidence/stp-mask/draw-calls.json). Prior project
measurements put a draw submission near 20 µs on Old 3DS; that makes SH/GT2
worth benchmarking first, but multiplying that historic number is **not** a
measured slowdown. Compare this branch with `89392c5` at the same state and
frameskip settings. `gldbg=32768` skips the separate mask pass for a submission
cost comparison, but retains the opaque-path and ordering changes; it is not
an exact recreation of the old build and deliberately breaks the blended STP
effect. `PICA_DEBUG=32768` does the equivalent in native replay.

## Remaining work, ranked

1. **Reduce remaining sample/clip errors in CTR/GT2/Crash.** Retain fractional
   clipped UV/colour/position and match PS1 edge/texture rounding. Avoid a large
   per-primitive CPU slow path; consider a compact fixed-point vertex format.
2. **Ordered dithering and correct colour quantization.** Prefer a small GPU
   dither texture/TEV stage using pixel position. Preserve raw sprites/uploads
   and per-command dither rules. The fake GPU's quantization must be checked
   against hardware before claiming one-level RGB agreement.
3. **Exact mask-check + half/quarter blending.** The included repro remains
   wrong. A stencil/extra GPU pass or destination sampling solution needs a
   measured memory/submission budget; current destination-alpha factors cannot
   represent all required products in one blend operation.
4. **Frontend centering/borders and scanout wrapping.** Extend the harness with
   actual `pica_picture` and frontend placement, including partial-height,
   oversized and wrapped display areas. Present screenshots at the real 3DS
   dimensions rather than inferring placement from raw VRAM.
5. **Uncovered hardware/game cases:** flipped sprite wrap, 15-bit/CLUT edge
   addressing, RGB24 movies, interlace, display-off transitions, wrapping fills
   and independent VRAM-copy/readback sequences. P.E.Op.S. itself clips some
   wrapping fill cases; use gpu_neon or a hardware-verified oracle for those.
6. **GPU-side copies / STP submission optimization.** Performance opportunities,
   subject to Old 3DS benchmarks; do not replace correctness with a state-based
   heuristic that enables masks only after the game first checks them.

## Validation and retained artifacts

* 5 states × 60 host frames captured from the current patched core and verified
  against native P.E.Op.S. full VRAM; identical traces reused for all revisions.
* 15 selected game comparisons per revision, PNGs and unrounded metrics saved.
* Six synthetic exact-RGB mask/order checks and one quarter-blend mask-survival
  check pass. Quarter-add still differs by one 5-bit colour step in that probe.
  The high-frequency sampling probe and known half-mask repro are reported as
  remaining differences, not counted as passing tests.
* AddressSanitizer replay of all five traces passed, including leak checking
  after adding explicit renderer cleanup on normal replay completion.
* `source /home/tony/Development/PS1_3ds/devtools/env.sh && make -j8` succeeded,
  producing the worktree's `psx3d/PSX3D.3dsx`. No install or device test occurred.
* Patch reverse-check and fresh-upstream patch application passed;
  `git diff --check` passed.

Large local artifacts are in `/tmp/correct-sim`: the five `.gputrace` files,
`core_capture`, capture/replay binaries, PPMs, detailed logs and sanitizer runs.
Do not delete the traces before a follow-up comparison. They contain game
content and were not committed.
