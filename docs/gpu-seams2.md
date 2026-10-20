# Black cracks at polygon edges (gpu_pica), round 2

2026-10-20. Branch `t3/gpu-seams2` from main `78592e1` (= v0.4.1 source).
Evidence (not committed): `devtools/gpu-seams2/`, report images in
`devtools/gpu-seams2/report/`, hardware VRAM dumps in `devtools/gpu-seams2/hw/`.

User report (New 3DS, v0.4.1, Silent Hill): black cracks "at the edge of
buildings and meshes" that move around the scene, and a floor that is
"missing some gradient smoothing".

## Summary

- **Cracks: found and fixed.** 1:1 textured PS1 polygons sample their
  texture exactly on texel edges. The 3DS GPU's interpolated texture
  coordinate lands a hair *below* such an edge, so the lookup takes the
  texel to the left or above. Inside a polygon that only shifts the texture
  by a texel; along its edges it reads a texel *outside* the polygon: an
  opaque black border texel (black specks beside Silent Hill's snow, black
  lines), or a neighbouring texture (light dotted seam down a pillar,
  coloured dots along Tekken 3's hills). Which edges are hit depends on the
  exact coordinates, so the cracks crawl as the camera moves.
  The vertex shader now adds 1/64 texel before the lookup. No CPU code
  changes: every function of the executable keeps its v0.4.1 address.
- **Floor banding: a setting.** The tested New 3DS has
  `core.pcsx_rearmed_dithering=disabled` saved in `config.ini` (from earlier
  tests). With it, Silent Hill's ground breaks into magenta/green bands; with
  dithering on (the default since v0.4.1) it is smooth. Players with a fresh
  config are not affected. The user's file was only read, not changed.

## Floor banding = dithering off

New 3DS, v0.4.1, Silent Hill slot 0, same load with the saved setting and
with `o:dithering=enabled` (`report/dither-off-on.png`, top off, bottom on).
To fix it on that console: Settings → Display → PS1 colour shading, or reset
that setting. This matches `docs/gpu-seams.md` (round 1).

## Finding the cracks

Real hardware has to be the judge here: the reference model
(`tools/gpu-compare`, P.E.Op.S. oracle) computes texture coordinates exactly
and showed no cracks. So the PS1 VRAM was dumped on the New 3DS
(`vramdump=60`, frameskip off, 4:3, dithering on) for the same save states
the simulator replays, and compared pixel by pixel with the model and the
reference at the same frame. Hardware and model agree closely (mean channel
error 1.2–1.8) except for a few hundred pixels per scene that only the
hardware has:

| Silent Hill scene | hardware-only pixels (error > 90) |
|---|---|
| street (slot 0) | 158 |
| alley (slot 6) | 131 |
| café (slot 7) | 67 |
| garage (slot 5) | 8 |

They are black specks attached to snowflakes, short black vertical lines,
a light dotted vertical seam down the café pillar and a line along the
street sign's edge (`cmp/*-hwdiff.png`).

The snowflakes are 3×3-pixel textured triangles with 1:1 texture
coordinates (u 0..3, v 112..115). Giving the model a texture coordinate
error of −0.004 texel (`RASTER_UV_BIAS=-0.004`) reproduces the hardware's
black-and-white flakes exactly; +0.004 does not. So the hardware's
coordinate sits slightly below exact texel edges.

Why: polygons are drawn shifted by half a pixel so that the GPU samples
pixel centres where the PS1 samples whole pixel positions. There a 1:1
texture coordinate is a whole texel number. The shader hands the GPU
`u / atlas width` as a 24-bit float; near the right half of the 1024-texel
atlas one float step is 1/128 texel, and the interpolated value rounds
below the edge. Nearest filtering then floors to the previous texel.

## Fix

`pica.v.pica`: `tc = (uv + 1/64) / size` instead of `uv / size`. 1/64 texel
is two float steps at the largest atlas coordinates. One more shader
instruction per vertex; sprites and rectangles already sample texel centres
and do not change. `gpu_pica.c` is untouched.

The 1/64 comes from a hardware sweep. A test build (`gpu-seams2-1`, bias
from `gldbg` bits 26–30) was dumped with bias 0, 1, 2, 4, 8 and 16 /256
texel on three Silent Hill scenes:

| bias (/256 texel) | 0 | 1 | 2 | 4 | 8 | 16 |
|---|---|---|---|---|---|---|
| street: hardware-only pixels | 158 | 114 | 28 | **0** | 0 | 0 |
| alley | 131 | 88 | 15 | **1** | 3 | 0 |
| café | 67 | 32 | 20 | **20** | 20 | 20 |
| street: error > 90 vs PS1 reference | 210 | 172 | 91 | **70** | 71 | 71 |
| alley | 770 | 727 | 667 | **656** | 664 | 689 |
| café | 1382 | 1365 | 1359 | **1360** | 1350 | 1359 |

4/256 = 1/64 is the smallest bias that clears all of them and the best match
to the reference. The café's remaining 20 are moving snow outside the window.

## Results on hardware

New 3DS, VRAM at frame 60, v0.4.1 vs `gpu-seams2-2`, same states:

| Scene | hardware vs model, error > 90 | hardware vs PS1 reference, error > 90 | mean error vs reference |
|---|---|---|---|
| SH street | 158 → 0 | 210 → 70 | 7.06 → 6.95 |
| SH alley | 132 → 1 | 771 → 656 | 7.51 → 7.39 |
| SH café | 65 → 20 | 1381 → 1360 | 8.72 → 8.69 |
| SH garage | 8 → 0 | 105 → 96 | 7.87 → 7.87 |
| Tekken 3 | 750 → 13 | 5471 → 4943 | 5.47 → 5.20 |
| Crash 2 | 216 → 13 | 3547 → 3380 | 6.75 → 6.64 |
| R4 | 62 → 1 | 796 → 772 | 4.37 → 4.36 |
| GT2 | 882 → 755 | 3630 → 3596 | 6.36 → 6.31 |

| SotN Dracula fight (slot 5) | 3252 → 2 | 3255 → 2 | 4.74 → 0.03 |
| Klonoa (slot 5) | 169 → 24 | 5330 → 5183 | 8.64 → 8.49 |

Every scene moved closer to the PS1 reference, and the hardware now matches
the model nearly pixel for pixel (GT2's remainder is one patch of road, not
edges). SotN is the clearest case: its 2D HUD and sprites are 1:1 polygons,
and v0.4.1 drew the HUD glyphs one texel off ("REST ×00" came out as broken
"×99"-like digits, `report/sotn-hud-before-after.png`); the candidate is
exact. CTR's racing frame does not line up with the model in time; its
before/after difference is 2,958 single pixels, all along polygon edges.
Crash 1: the standard slot 0 frame 60 is unchanged; the heavy scene at frame
400 changes 3,621 scattered texels in the sand and short edge segments (no
time-aligned model frame, so not scored against the reference).

Before/after crops (5×, from the dumps): `report/alley-before-after.png`
(snow specks), `cafe-before-after.png` (pillar seam), `sotn-hud-before-after.png`, `street-before-after.png`
(sign edge), `tekken-before-after.png` (coloured dots along the hills),
`gt2-before-after.png`. With the user's own settings (widescreen, dithering
off): `report/user-{alley,cafe,street}-before-after.png`.

## Old 3DS speed

A/B/A on the Old 3DS: v0.4.1 → `gpu-seams2-2` → v0.4.1, the 13 heavy
scenes, frameskip off and Auto, 78 runs, all ok, no black samples
(`hw/old-aba1`, table `results-old-aba1-old.md`). Every scene/mode is
within ±0.4% of the v0.4.1 mean except NFS Auto (+1.0%, A drift 1.3%);
emulation ms per frame unchanged. The executable's 5,205 code symbols are at
the same addresses as v0.4.1 (same section sizes); the shader binary grows
by 24 bytes in `.rodata`.

## Model results

- Draw hashes: `core/` is byte-identical to main, so the 600-frame ARM
  `PICA_SIM_HASH` draw hashes cannot change. Replayed draw hashes (120-frame
  traces) checked identical on sh, o3auto, ctr, gt2, r4, tekken, sh-cafe.
- The model mirrors the shader (exact `floor(u + 1/64)`), so model images
  change slightly. Audit corpus (73 runs, 6,159 samples, `audit_ab.py`):
  - exact model (no interpolator error, the fix's worst case): 2,094 samples
    change, raw > 24 pixels +107,322 on 8.28 M in those samples (+1.3%);
    worst Wipeout XL, Driver, Crash 2.
  - with the hardware-like −0.004 texel error in both builds: 2,676 change,
    2,329 better / 335 worse, raw > 24 pixels −5.97 M (SotN, THPS2, MGS,
    Tomb Raider, Rayman gain most).
  The hardware results above lie on the hardware-like side.
- New fixture `texel-edges` (`fixtures.py --check`): a 1:1 quad mesh over
  an opaque black texel border, replayed with `RASTER_UV_BIAS=-0.004`. It
  matches the reference exactly; with the shader bias cancelled it shows
  2,048 wrong pixels (64 black).

## Other crack causes checked

Round 1's clip-rounding crack (edges crossing the drawing-area border at a
half-pixel tie) is real but rare and was not seen in these hardware frames;
its fix stays rejected for Old 3DS speed. T-junction gaps, fill-rule
differences and quad diagonals did not show up as hardware-only pixels: the
hardware matched the exact model everywhere except at texel edges.

## Limits

- 1/64 is a fixed margin from measured scenes. A coordinate that the GPU
  rounds down by more than 1/64 texel would still flip; none was seen.
- The bias moves samples whose exact coordinate is within 1/64 below a
  texel edge up by one texel. In the exact model that is the +1.3% above;
  on hardware those samples were already unstable.
