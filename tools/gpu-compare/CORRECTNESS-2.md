# GPU colour and dithering follow-up

Branch `t3/gpu-correctness-2`, based on `bc44d30`. Simulator only; no console
access, deployment or push. The original five traces in `/tmp/correct-sim`
are reused through symlinks from `/tmp/correct2-sim`.

## Texture modulation

`texel_rgba` expanded five-bit components with bit replication. That is useful
for presentation, but PS1 texture modulation uses the original five-bit value:
`tex5 * vertex8 / 16` gives the eight-bit component before quantization.
Decode as `tex5 << 3` instead. Raw textures still retain their original five-bit
colours. The PICA `2 / 255` modulation approximates PS1 `1 / 128`, so boundary
values may still differ by one level; this is not a claim of exact arithmetic.

The new fixture checks
4/8/15-bit textures, raw mode, 0x40 modulation, CLUT selection and a same-address
CLUT upload after an entry has been cached. All pixels match P.E.Op.S. exactly.
In particular, white × 0x40 must be 15/31, not 16/31. Existing mask regressions
also pass, including exact RGB for subtract/quarter probes with the corrected
model. No extra draw, state change, texture memory or per-primitive work; three
bitwise OR/shift expansions per decoded texel are removed.

The harness previously rounded normalized colour to 31; PICA RGB5A1 instead
drops the low three bits of the eight-bit blend result. The model now does that
and expands framebuffer reads by bit replication. See Azahar's
[`Convert8To5`, `EncodeRGB5A1`](https://github.com/azahar-emu/azahar/blob/master/src/common/color.h)
and [framebuffer write](https://github.com/azahar-emu/azahar/blob/master/src/video_core/renderer_software/sw_framebuffer.cpp).
These support the model correction, but are not console measurements.

## Silent Hill jacket diagnosis

At frame 60, scanout pixel (145,120), VRAM (145,376), the decoded jacket sample
is RGB (49,33,24) with vertex colour approximately (91,97,104): the correct
reddish texture is selected. The final untextured additive fog polygon also
contributes colour (34,32,37). CLUT selection/invalidation checks pass.

P.E.Op.S. applies a **positive-only** dither (`Dither16`, threshold table
7,0,6,1 / 2,5,3,4 / 1,6,0,7 / 4,3,5,2), including after blending. At that pixel
it rounds the jacket's red and blue up and then raises them again with the fog.
The original PICA model instead produces RGB (65,57,49), versus reference
(82,57,65). This is principally quantization/dithering bias, not a swapped
channel or wrong CLUT. The actual PS1 signed matrix is −4..+3; the upstream
`gpu_neon/psx_gpu/psx_gpu_standard.c:dither_table` independently documents it.
Do not tune the renderer's palettes to match the reference's bias.

Full game comparisons and the GPU dither implementation are reported below as
verification completes. Modulation-only metrics against the unchanged original
reference are in metrics.json.

## GPU ordered dithering

The signed PS1 matrix is stored in a repeating 8×8 RGBA8 texture (the GPU's
minimum dimension), 256 bytes. Shader-generated texture coordinates use absolute
quarter-pixel VRAM position / 32; this anchors the matrix across draw offsets,
viewport/scissor changes and the polygon sampling translation. TEV stage 1 adds
`texture1 − 128` to RGB and preserves alpha, before the target drops low bits.
No extra vertex attribute, pixel CPU work, readback or rendering pass is added.

Only Gouraud untextured or modulated textured polygons are eligible. Raw
textures, flat untextured polygons, sprites, lines, fills and uploads bypass it.
`renderer_set_config` now honors existing `pcsx_rearmed_dithering` off/enabled/
force values. State is captured per batch, so changing an option while vertices
are pending does not change those vertices. Global and per-game overrides use
the frontend's existing storage. Existing defaults are retained: Old 3DS off,
New 3DS enabled. The option's help text now describes the GPU behaviour.

Fixtures independently assert every matrix position and repeat phase, E1 off,
force, raw/flat/sprite exclusions, an upload after a dithered batch, and option
changes while vertices are queued. These are specification checks; their
expected pixels do not come from the combiner implementation or P.E.Op.S.
Existing mask/STP fixtures still pass.

### Independent reference

The comparison tool also supports `--reference neon`: the upstream gpu_neon
renderer's **portable C/vector path** compiled natively, with the same trace,
not ARM NEON instructions. It has the signed matrix and performs quantization
before blending. `GPU_COMPARE_ALT_REFERENCE` explicitly disables the captured
P.E.Op.S. byte-for-byte VRAM assertion for this different oracle. Original
P.E.Op.S. replay continues to verify all 60 full VRAM snapshots per game.
The signed result closely agrees with this independent oracle, especially in
Silent Hill, rather than with P.E.Op.S.'s positive bias. This does not establish
hardware pixel accuracy: interpolation, blending rounding and clipping remain
approximations. In particular, our dither offset reaches the 5551 target after
PICA blending, while PS1 quantizes the source before blending; semi-transparent
cases can retain one-level errors.

## Flipped sprite wrapping

Split flipped sprites after `u + 1` / `v + 1` texels, then continue at 255,
just as forward spans continue at zero. Covers X, Y and combined flips,
repeated wraps over a full page, and clipping. No work is added per texel.
Extra vertices/draw submissions occur only on sprites that previously sampled
outside their texture region. Before
and after.

P.E.Op.S. segfaults on this fixture's flipped negative span, so the image's
reference explicitly uses direct modulo addressing of the known synthetic
texture. The independent oracle samples `(u ± dx) & 255`, `(v ± dy) & 255`;
our renderer instead recursively splits spans before atlas lookup. The old
renderer fails this assertion, the new renderer passes exactly. This is not a
claim of matching P.E.Op.S. on flipped wrapping.

## Metrics at all requested frames

MAE / large-error pixel %, where large is any channel >24. These are model
comparisons, not hardware measurements. Frames are host frames after state load.

### Original P.E.Op.S. oracle (historical baseline → final)

| Game | Frame | Before MAE / large % | After MAE / large % | Image |
|---|---:|---:|---:|---|
| o3auto | 10 | 8.777 / 10.747 | 9.893 / 10.979 | before, after |
| o3auto | 30 | 8.777 / 10.747 | 9.893 / 10.979 | before, after |
| o3auto | 60 | 4.372 / 4.588 | 5.393 / 4.692 | before, after |
| sh | 10 | 4.662 / 0.516 | 7.055 / 5.762 | before, after |
| sh | 30 | 4.672 / 0.502 | 7.068 / 5.716 | before, after |
| sh | 60 | 4.672 / 0.523 | 7.071 / 5.721 | before, after |
| r4 | 10 | 5.489 / 3.439 | 4.546 / 3.211 | before, after |
| r4 | 30 | 5.406 / 3.267 | 4.598 / 3.113 | before, after |
| r4 | 60 | 4.947 / 2.854 | 4.269 / 2.725 | before, after |
| ctr | 10 | 6.622 / 8.892 | 7.405 / 9.733 | before, after |
| ctr | 30 | 6.890 / 9.112 | 7.693 / 10.032 | before, after |
| ctr | 60 | 6.758 / 9.318 | 7.659 / 10.121 | before, after |
| gt2 | 10 | 7.078 / 8.311 | 6.312 / 8.523 | before, after |
| gt2 | 30 | 6.896 / 7.849 | 6.027 / 7.987 | before, after |
| gt2 | 60 | 7.295 / 9.141 | 6.355 / 8.984 | before, after |

### Signed gpu_neon C oracle (corrected-model baseline → final)

| Game | Frame | Before MAE / large % | After MAE / large % | Image |
|---|---:|---:|---:|---|
| o3auto | 10 | 9.276 / 10.038 | 7.638 / 9.261 | before, after |
| o3auto | 30 | 9.276 / 10.038 | 7.638 / 9.261 | before, after |
| o3auto | 60 | 5.136 / 3.997 | 3.345 / 3.121 | before, after |
| sh | 10 | 4.159 / 0.579 | 1.724 / 0.039 | before, after |
| sh | 30 | 4.165 / 0.587 | 1.727 / 0.031 | before, after |
| sh | 60 | 4.169 / 0.578 | 1.740 / 0.036 | before, after |
| r4 | 10 | 4.951 / 3.605 | 3.235 / 2.995 | before, after |
| r4 | 30 | 4.908 / 3.578 | 3.194 / 2.967 | before, after |
| r4 | 60 | 4.723 / 3.348 | 3.074 / 2.857 | before, after |
| ctr | 10 | 7.124 / 8.618 | 4.583 / 5.578 | before, after |
| ctr | 30 | 7.254 / 8.281 | 4.670 / 5.537 | before, after |
| ctr | 60 | 7.120 / 8.478 | 4.599 / 5.552 | before, after |
| gt2 | 10 | 7.718 / 8.943 | 5.762 / 8.466 | before, after |
| gt2 | 30 | 8.473 / 10.901 | 6.577 / 10.383 | before, after |
| gt2 | 60 | 7.822 / 11.573 | 6.191 / 10.740 | before, after |

The historical comparison includes the harness quantization correction. To
isolate it, use corrected-model baseline,
then modulation only, then
final against P.E.Op.S..
The dither and final sprite versions produce identical pixels in these traces.
The final build improves large-error scores against gpu_neon; GT2
still has substantial clipping/texture errors, and its MAE also reflects blend
rounding differences. P.E.Op.S. scores worsen with the signed pattern, especially
in Silent Hill; this is retained openly rather than tuning to that oracle.

## Submission and CPU cost

Frames 31–60, averages per **host** frame (not per drawn game frame).
Dither stage setups count calls obtaining stage 1 for initialization/setup,
including resets after uploads/base state invalidation, not just on/off changes.

| Game | Baseline draws | Final draws | Added draws | Dither stage setups | Historic 20 µs/draw estimate |
|---|---:|---:|---:|---:|---:|
| o3auto | 13.50 | 13.50 | 0.00 | 1.50 | 0.000 ms/host frame |
| sh | 358.50 | 358.50 | 0.00 | 3.50 | 0.000 ms/host frame |
| r4 | 40.17 | 48.03 | 7.87 | 29.27 | 0.157 ms/host frame |
| ctr | 31.77 | 32.67 | 0.90 | 7.17 | 0.018 ms/host frame |
| gt2 | 197.00 | 203.03 | 6.03 | 114.23 | 0.121 ms/host frame |

These are submission counts and a rough estimate, **not measured Old 3DS time**.
Texture modulation saves a few decode operations with no extra draw/state work.
Dithering adds a few integer tests per polygon and a byte compare in use_state,
one GPU vertex instruction (including uploads), one GPU texture sample and TEV
add per affected pixel. No new vertex bandwidth, CPU pixel loop, sync/readback,
or per-primitive texture allocation. Stage setup is additional CPU command work.
Stage 1 validity is tracked independently of stage 0, so STP mask passes do
not resend the dither registers or texture binding. Memory cost is 256 bytes of texture data plus its
small descriptor. Flipped wrapping adds splitting/vertices only to previously
wrong wrapping sprites; none of these game traces changes pixels or hashes.

Old 3DS's instruction-cache sensitivity prevents predicting FPS from host runs.
Keep Old 3DS off by default until measuring dither off/on on the same build,
frameskip off and identical states; inspect R4/GT2 submission cost first. Also
verify the signed pattern's phase/quantization, alpha/mask preservation and raw
sprite/UI colours on both consoles. New 3DS retains the existing enabled default.

## Draw hashes

60-frame native replay hashes (same canonical PICA hashing used by the ARM
simulator; these are not 600-frame emulation hashes):

| State | Corrected-model baseline | Modulation | Dither/final |
|---|---|---|---|
| o3auto | `4f056760a6786c63` | `f1f302c085da8d6b` | `14d1794bcf34e5cc` |
| sh | `2aead1eeecc9b3f9` | `d1d1f10561613235` | `d420883311b32c09` |
| r4 | `2c737b2d8a96f44b` | `0f997476460d65c7` | `4501e958eb309a8d` |
| ctr | `aaecb5426e3650e4` | `92543615ef403bec` | `445511492cbb4f0a` |
| gt2 | `e5c966de952eacbe` | `380481060e7ed6d2` | `ff49c4f0a74dd6f8` |

Modulation changes only the decoded sample bytes in the hash, not draw geometry
or batching. Dithering adds its enable bit to the existing pass word (bit 8),
and batch boundaries change when eligible/ineligible polygons alternate. The
model quantization change is not hashed because rasterization is outside the
core draw hash. Sprite-wrap produces no hash change on these five traces.
Exact trace SHA-256, draw counts and available hashes are in
manifest.json.

### Dither state cache

A follow-up preserves the applied stage-1 state across the STP pass, which only
changes stage 0. Stage-1 setup calls per host frame fall from 6.0→1.5 (Crash),
122.47→3.5 (SH), 14.73→7.17 (CTR), and 126.9→114.23 (GT2); R4 stays 29.27.
All 15 selected PICA PPMs and all five draw hashes are byte-identical before and
after this submission-only optimization. It adds one cached integer comparison
per applied batch and avoids redundant combiner/texture commands. Hardware time
is still unmeasured.

## Final validation and handoff

- Branch `t3/gpu-correctness-2` from local `t3/gpu-renderer-reference-harness`
  (`bc44d30`). Commits: `174c12b` modulation/model quantization, `43281a4`
  GPU dithering, `1378b05` flipped wrapping, `bf25d15` dither state cache.
  Every core change has its regenerated patch 0002 in its own commit.
- Five unchanged captured traces, 60 host frames each; original P.E.Op.S.
  reference replay verifies all 300 full VRAM snapshots. Images/metrics retain
  frames 10, 30 and 60 from both oracles. The gpu_neon wrapper flushes pending
  blocks before CPU VRAM uploads; all selected oracle images are unchanged by
  adding that explicit ordering step.
- Ten passing synthetic checks cover texture modulation/depth/CLUTs, the signed
  matrix/config transitions, wrapping, and seven mask/STP/blend/order cases.
  Sampling remains a diagnostic; mask-check + half blending remains a known
  failure, not a passing test. Quarter with mask-check remains approximate too.
- AddressSanitizer with leak detection passes the full five traces and the
  synthetic suite. The later state-cache change passes sanitizer fixtures,
  off/on/force fixtures, and all five output/hash identity comparisons.
- `source /home/tony/Development/PS1_3ds/devtools/env.sh; make -j8 cia ...`
  succeeds, including the shader, 3DSX and CIA. Files are `psx3d/PSX3D.3dsx`,
  `psx3d/PSX3D.cia`, and `psx3d/PSX3D.elf`; exact sizes and SHA-256 are in
  builds.json. No install performed.
- Fresh upstream archive + all repository patches reproduces every one of the
  23 modified core files byte-for-byte; reverse checks and `git diff --check`
  pass. Python compilation and shell syntax checks pass.
- No 3DS console connection, deployment, hardware measurement or push. PICA
  precision, cache coherence, clip behaviour, and Old 3DS time remain hardware
  validation items. The existing half/quarter mask-check shortcut is unchanged:
  an exact solution needs destination gating and blend weights together, which
  is not a cheap single-factor blend-state fix.

Local logs, selected raw images, binaries and the copied simulator are in
`/tmp/correct2-sim`. Existing traces remain in `/tmp/correct-sim`, symlinked here
without duplication. Unselected PPMs and our extracted patch-test source are
removed after validation to keep shared tmpfs use modest. Capture saves no new
proprietary traces. The updated comparer emits selected PPMs only and checks
write errors, after a shared tmpfs quota was reached during one comparison;
affected images were regenerated and final metrics use complete images.

Interactive off/on gallery shows each game's frame
60 against the signed oracle. Both off/on renders use the corrected modulation;
this isolates dither's visual change from texture conversion. The table uses
the corrected-model original renderer as the before baseline. Matrix pattern
adds fine stipple instead of broad colour bands; in Silent Hill the signed
reference and our dithered jacket agree far more closely than the original
P.E.Op.S. jacket does. Quantization can alter the apparent tint at low light.
