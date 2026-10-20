# Old 3DS renderer CPU work in the heavy scenes

Branch `t3/renderer-cpu`, based on `release/v0.4.1-candidate` RC3 (`612ea15`, docs `aa3eeff`).
Evidence, simulator copies, hardware logs and CIAs are outside Git in
`/home/tony/Development/PS1_3ds/devtools/renderer-cpu/`. No push.

Goal: make the GPU renderer's CPU-side work cheaper on the Old 3DS in the heavy
benchmark scenes (slots 5–7), where dynarec round 3 measured the renderer path at
25–60% of the frame. Target: at least 5% game speed on several CPU-bound heavy
scenes, no heavy scene more than 1% slower. Output must stay identical.

## Summary

**Final build against RC3 on the Old 3DS, all 13 heavy scenes (RC3 A1/A2 bracket vs two final runs):**

| Scene | frameskip off | Auto |
|---|---:|---:|
| Crash 2 | **+49.9%** (49.5 → 74.2%) | **+83.2%** (49.8 → 91.2%) |
| CTR | **+27.8%** (61.1 → 78.1%) | **+28.1%** (69.8 → 89.4%) |
| SH café | **+14.8%** | **+21.0%** |
| SH alley | **+14.3%** | **+18.3%** |
| SH garage | **+10.3%** | **+11.7%** |
| GT2 | **+10.1%** | **+10.0%** |
| R4 | **+9.5%** | **+5.1%** |
| NFS HS | **+8.2%** | **+9.3%** |
| Tekken 3 | +3.3% | +1.3% |
| Klonoa | +2.4% | −0.1% |
| Crash 1 | +0.5% | +0.6% |
| FFVII | 0.0% (at 100%) | +0.1% |
| SotN | −0.4% (at 98%) | 0.0% (at 100%) |

Game speed %; 104 runs in these brackets (143 in the session), 0 failed, 0 near-black samples; RC3 A1/A2 spread ≤0.4%
(off), ≤0.9% (Auto). The worst case, SotN −0.44% with frameskip off, is within the
~1% gate. The target (≥5% on several CPU-bound heavy scenes) is met in eight
scenes; Tekken 3, whose cost is purely per primitive, gains 3%.

Six changes, all removing work the renderer should not have been doing: draw
batches split by effects (A, B), a per-draw citro3d scan (C), an expensive atlas
eviction search (E), a false render-to-texture dependency in CTR that forced a GPU
wait 2–3 times a frame (F), and no-op tile bookkeeping walks (G). Final CIA
`devtools/renderer-cpu/artifacts/` (identity `renderer-cpu-5`, title version 4100,
CIA SHA256 `5f3b4d6266136d17db54bf018698b9dfb352a086a7ea8d737937056ded6b760f`,
executable source `ceec41f`).

All changes keep the picture identical: full VRAM (all 1024×512 pixels with mask
bits) is the same on every frame of 19 traces × 600 frames (13 heavy + 6 standard
states), and the emulated machine is unaffected (guest RAM/registers/cycles/IRQ
timeline and the final serialized state identical in all 19 states; 63 HLE cold
boots identical). The changes are always on; the test switch is gone. Code size
+3.2 KB `.text`, `.bss` unchanged.

## 1. Where the renderer's time goes

### Hardware (RC3, Old 3DS, frameskip off, profiling switches on)

`gldbg=8388608` samples the renderer phases (1/16 calls, inclusive estimates;
the sampling itself slows the game by up to ~14%, so read the columns as
shares), `pmurender=on` counts PMU cycles inside the command-list function.
Per emulated frame (`hw/prof-rc3/`):

| Scene | fps | renderer path ms | primitives | of which: textures | decode+readback | GPU-wait reads | state | draw calls | renderer Mcycles | batches/frame | syncs/frame |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Crash 1 | 47.5 | 5.0 | 4.9 | 0.9 | 0.1 | 0.0 | 0.1 | 0.3 | 1.46 | 8 | 0.0 |
| CTR | 33.2 | 12.7 | 12.9 | 5.6 | 4.5 | 4.0 | 0.2 | 1.3 | 2.74 | 42 | 2.2 |
| R4 | 37.5 | 9.4 | 8.7 | 1.6 | 0.4 | 0.0 | 0.3 | 3.1 | 2.48 | 116 | 0.0 |
| SotN | 53.7 | 9.7 | 8.3 | 3.9 | 3.2 | 2.4 | 0.1 | 0.5 | 2.02 | 10 | 1.0 |
| SH garage | 39.5 | 6.6 | 6.5 | 0.7 | 0.0 | 0.0 | 0.3 | 2.7 | 2.01 | 103 | 0.0 |
| SH alley | 33.0 | 12.5 | 12.1 | 1.2 | 0.0 | 0.0 | 0.6 | 5.2 | 3.43 | 188 | 0.0 |
| SH café | 33.4 | 11.8 | 11.8 | 0.6 | 0.0 | 0.0 | 0.6 | 5.3 | 3.34 | 198 | 0.0 |
| GT2 | 26.2 | 18.3 | 18.0 | 2.1 | 0.5 | 0.2 | 0.7 | 6.1 | 4.66 | 212 | 0.3 |
| Tekken 3 | 29.7 | 10.5 | 10.0 | 2.0 | 0.4 | 0.0 | 0.1 | 0.3 | 2.89 | 8 | 0.1 |
| FFVII | 54.7 | 5.9 | 5.7 | 0.9 | 0.3 | 0.0 | 0.2 | 1.2 | 1.25 | 42 | 0.0 |
| Klonoa | 45.9 | 7.5 | 7.3 | 1.9 | 0.5 | 0.0 | 0.1 | 0.6 | 2.26 | 23 | 0.0 |
| Crash 2 | 27.5 | 19.4 | 5.1 | 1.0 | 0.1 | 0.0 | 0.9 | 9.0 | 3.50 | 343 | 0.3 |
| NFS | 27.0 | 11.3 | 10.6 | 5.8 | 1.9 | 0.0 | 0.1 | 0.6 | 3.97 | 16 | 0.0 |

"draw calls" is the time in `C3D_DrawArrays` plus the mask pass, "GPU-wait
reads" the read-backs of GPU drawing that the CPU needs (each one a full GPU
synchronisation). Reading it:

- **Draw calls dominated the batching games**: Crash 2 drew 343 batches a frame
  (9.0 ms), SH alley/café ~190 (5.2–5.3 ms), GT2 212 (6.1 ms), R4 116 (3.1 ms).
  On hardware one batch cost **~26–28 µs** (7,000+ cycles), far more than the
  ~60 command words it writes.
- **CTR**: 2.2–3.3 GPU syncs a frame for texture read-backs, ~4 ms waiting.
- **NFS**: 5.8 ms in texture lookups, mostly the atlas eviction search (below).
- Tekken 3: 1,070 primitives a frame in 8 batches; its cost is the
  per-primitive path (~2,700 renderer cycles per primitive).

### Simulator (cache model, frames 301–600)

`tools/run.py --mode profile --heavy` on a private simulator built from this
worktree. The model's citro3d is a stub, so draw calls cost nothing there; the
model therefore shows the renderer at only 2–6 ms (15–25%) and misses the
largest hardware cost. It is still exact about *what* the renderer does, which
is how the causes below were found (instrumented private replayers, never the
worktree source).

### Why so many batches

Every batch end was attributed to its cause (instrumented replay):

- **Crash 2**: runs of quarter-blend primitives (B + F/4) are drawn in two
  passes (opaque texels, then the blended ones). Each primitive switched pass 0
  → pass 1 → pass 0: two batches per primitive, 15.5k state changes per 120
  frames.
- **SH, R4, GT2**: additive effects alternate with ordinary primitives. A queue
  already let additive primitives wait for later disjoint ones, but only when
  the game left the E6 mask setting at 0; these games set it, so every additive
  primitive ended a batch, and the queue was flushed by the next primitive with
  any mask setting.
- Every additive batch also needed a second "mask pass" draw.
- Each draw spent ~1,000 instructions in citro3d's uniform update (below).

## 2. What was built

Each change was first behind a bit of a temporary `rcpu=N` switch (startup,
remote `load`/batch, status) for same-binary A/B, then made always on with the
switch and the old paths deleted (`6cb146c`).

| | Change | Where it helps |
|---|---|---|
| A | **Blended passes wait for later disjoint primitives.** Additive primitives also defer when E6 *sets* the mask bit (a per-pixel write, kept in the queued state); only mask *checking* needs the original order. The blended pass 1 of subtract/quarter primitives uses the same queue. The queue is no longer flushed by any later primitive with a mask setting, only by overlap, mask checking, area change or a different queued state. Entries of the new kinds stay pairwise disjoint, so nothing new assumes that blending commutes; plain additive entries may overlap each other as before. | Crash 2 batches 373 → 34 a frame, R4/CTR −30%, SH −21…−27%, GT2 −23% |
| B | **Additive batches write the mask bit in their one pass.** The alpha an additive batch blends with is already 0 for opaque texels and 1 for STP texels, i.e. the PS1 mask bit; with no untextured primitive in the batch (those tag alpha 0 for the mask pass) it is written directly and the second draw is skipped. | SH, GT2 |
| C | **citro3d's per-draw uniform update is skipped when nothing is dirty.** `C3D_DrawArrays` updates the uniforms of both shader stages on every draw, scanning 100 dirty flags each a byte at a time. A `--wrap=C3D_UpdateUniforms` returns early when no float/int/bool uniform and no shader constant can be dirty; the two citro3d functions that set its private shader-constant flag are wrapped to note it. The GPU command stream is unchanged. | every draw; SH, GT2, R4, Crash 2, NFS |
| E | **Cheaper atlas eviction search.** The victim window ("whose newest occupant is oldest") was found by looking up each cell's owner `cw` and `ch` times per window. Now each entry in use is read once into a cell grid, then power-of-two sliding maxima over columns and rows, then the same first-smallest window. NFS spent ~9 ms a frame (modeled) in it after loading. Same victims, so even the draw hash is unchanged. | NFS, GT2 |
| F | **Texture squares avoid GPU-drawn rows above them.** CTR keeps 4-bit textures in VRAM rows 216–247, right below its 216-row framebuffer. Squares start at 32-row boundaries, so the square was 192–255 and took in framebuffer rows the GPU redraws every frame: the texture was invalidated every frame and each decode read the framebuffer back with a full GPU wait (3.3 a frame). When rows between the 32-row origin and the request contain GPU drawing, the square starts at the request's 8-row tile instead (if it still ends inside the page). The texture memo's bounds are counted from the square's origin (unchanged for 32-aligned squares). | CTR syncs 3.31 → 0.02 a frame, decodes 4.4 → 1.1 |
| G | **The known drawn rectangle grows.** GPU-drawn tile bookkeeping is skipped for primitives inside one remembered rectangle, but it was only replaced by a bigger one, so double-buffered games (and anything outside the first big primitive) walked already-marked tiles 170–370 times a frame. It now grows to the union with a just-marked rectangle when the union is all marked; otherwise a second rectangle grows the same way and takes over when as large (the other framebuffer). Only the shortcut changes. | GT2, CTR, FF7, SH, R4, NFS: those walks → 0 |

## 3. Correctness

- **Full VRAM every frame** (`tools/gpu-compare` native replay with the fake
  rasteriser, `GPU_HASH_VRAM`): 13 heavy + 6 standard traces captured with
  P.E.Op.S. from the HLE heavy states and the standard states, 600 frames:
  **19/19 identical to RC3 on every frame** for each candidate and the final
  build (`/tmp/rcpu-sim/gate/`, copied to `gate/`). The `PICA_SIM_HASH` draw
  hash differs by design where draws were reordered (A) or squares moved (F);
  E and G leave even the draw hash unchanged, and the final build's draw hashes
  equal those of the all-on candidate.
- **Emulated machine unaffected**: full emulator (hash simulator), RC3 vs final,
  6 standard + 13 heavy states × 600 frames: guest RAM, GPRs, CP2, PC, cycle
  counter and IRQ/exception trace identical at every 60-frame checkpoint, and
  the final serialized states (which settle and read back all GPU drawing) are
  **byte-identical in 19/19**. Only the simulator's lazily read-back CPU-side
  VRAM copy differs mid-run (F reads back fewer rows).
- **HLE cold boots**: 63 game directories with the built-in HLE BIOS, 1,200 frames:
  guest timeline identical in **63/63**; serialized state byte-identical in 61/63. The
  other two (Brave Fencer Musashi, Tekken 3) differ only inside the VRAM section, where
  this simulator's stub GPU leaves placeholder pixels; both boots were captured and
  replayed through the rasterising replay: **full VRAM identical on all 1,200 frames**.
- Host suites pass: `psx3d/tests/dev`, `tools/gpu-compare/regress.sh`
  (ASan/UBSan), `tests/renderer_config.sh`, `tools/pica-layout/check.sh`,
  `psx3d/tests/vout/check.py` (`host/`).
- C cannot be checked by the simulators (their citro3d is a stub). It returns
  early only when the real function would emit nothing and change nothing; the
  link map shows every call of the three real functions goes through the
  wrappers.

## 4. Hardware

Old 3DS only; the New 3DS was marked unavailable for the whole round and was not
contacted. All sessions: lock held, heavy scenes slots 5–7, frameskip off
(Auto in the final session), 3 s warm-up + 12 s (NFS 1 + 5 s), PMU 0,2,
installed CIA self-updates, Old left on RC3 in the library and verified by
status and screenshot. Driver `hw.py`, batches `batches/`, results `hw/*/`.

### Same-binary A/B (one CIA, switch toggled; game speed %)

Session 1 (`renderer-cpu-1`, rcpu 0 → 3 = A+B; r1 = A alone):

| Scene | r0a | r3a | r1 | r3b | r0b | base mean | cand mean | Δ % | pictures/s base → cand |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Crash 1 | 85.70 | 85.98 | 85.85 | 85.94 | 85.91 | 85.80 | 85.96 | **+0.18** | 20.3 → 20.5 |
| CTR | 60.43 | 61.26 | 60.66 | 61.34 | 60.56 | 60.50 | 61.30 | **+1.32** | 18.2 → 18.3 |
| R4 | 72.38 | 76.88 | 76.69 | 76.96 | 71.98 | 72.18 | 76.92 | **+6.57** | 21.9 → 23.4 |
| SotN | 98.63 | 98.60 | 98.35 | 98.62 | 98.86 | 98.74 | 98.61 | **-0.14** | 59.7 → 59.6 |
| SH garage | 71.79 | 76.52 | 74.12 | 76.63 | 71.97 | 71.88 | 76.58 | **+6.53** | 17.3 → 18.5 |
| SH alley | 59.77 | 64.18 | 61.28 | 64.14 | 59.68 | 59.72 | 64.16 | **+7.43** | 18.0 → 19.5 |
| SH café | 60.64 | 66.39 | 62.33 | 66.27 | 60.51 | 60.58 | 66.33 | **+9.50** | 14.3 → 15.7 |
| GT2 | 46.54 | 48.95 | 47.97 | 48.94 | 46.44 | 46.49 | 48.95 | **+5.28** | 12.6 → 13.1 |
| Tekken 3 | 56.44 | 56.35 | 56.35 | 56.38 | 56.37 | 56.41 | 56.37 | **-0.07** | 34.3 → 34.3 |
| FFVII | 99.95 | 99.98 | 100.04 | 99.99 | 99.99 | 99.97 | 99.99 | **+0.02** | 15.2 → 15.1 |
| Klonoa | 83.93 | 83.94 | 83.90 | 83.88 | 84.01 | 83.97 | 83.91 | **-0.07** | 50.8 → 50.8 |
| Crash 2 | 48.00 | 71.82 | 71.68 | 72.06 | 48.19 | 48.10 | 71.94 | **+49.57** | 15.0 → 22.0 |
| NFS | 33.83 | 34.26 | 34.22 | 34.35 | 34.23 | 34.03 | 34.31 | **+0.82** | 11.0 → 11.2 |

Near-black samples: 0; failed runs: 0.

Session 2 (`renderer-cpu-2`, rcpu 3 → 7 = +C):

| Scene | r3a | r7a | r3b | r7b | base mean | cand mean | Δ % | pictures/s base → cand |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Crash 1 | 86.10 | 86.56 | 86.00 | 86.65 | 86.05 | 86.61 | **+0.65** | 20.3 → 20.5 |
| CTR | 61.33 | 61.87 | 61.39 | 61.70 | 61.36 | 61.78 | **+0.69** | 18.3 → 18.4 |
| R4 | 77.10 | 79.18 | 76.63 | 78.99 | 76.87 | 79.08 | **+2.88** | 23.3 → 24.0 |
| SotN | 98.52 | 98.52 | 98.58 | 98.46 | 98.55 | 98.49 | **-0.06** | 59.7 → 59.5 |
| SH garage | 76.57 | 78.97 | 76.73 | 79.04 | 76.65 | 79.00 | **+3.07** | 18.5 → 19.1 |
| SH alley | 64.37 | 67.53 | 64.41 | 67.53 | 64.39 | 67.53 | **+4.89** | 19.7 → 20.6 |
| SH café | 66.45 | 69.91 | 66.49 | 69.91 | 66.47 | 69.91 | **+5.17** | 15.7 → 16.6 |
| GT2 | 48.97 | 50.66 | 49.02 | 50.67 | 49.00 | 50.66 | **+3.40** | 13.1 → 13.4 |
| Tekken 3 | 55.88 | 56.04 | 56.03 | 56.07 | 55.96 | 56.06 | **+0.18** | 34.0 → 34.1 |
| FFVII | 99.95 | 99.99 | 99.99 | 99.99 | 99.97 | 99.99 | **+0.01** | 15.2 → 15.2 |
| Klonoa | 83.88 | 84.74 | 83.96 | 84.61 | 83.92 | 84.67 | **+0.90** | 50.8 → 51.2 |
| Crash 2 | 72.49 | 74.18 | 72.58 | 74.15 | 72.53 | 74.16 | **+2.24** | 22.1 → 22.6 |
| NFS | 33.71 | 34.42 | 34.01 | 34.61 | 33.86 | 34.51 | **+1.93** | 10.9 → 11.2 |

Near-black samples: 0; failed runs: 0.

Session 3 (`renderer-cpu-3`, rcpu 7 → 31 = +E+F):

| Scene | r7a | r31a | r7b | r31b | base mean | cand mean | Δ % | pictures/s base → cand |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Crash 1 | 86.15 | 86.13 | 86.10 | 86.22 | 86.12 | 86.17 | **+0.06** | 20.4 → 20.4 |
| CTR | 61.85 | 75.31 | 61.87 | 75.41 | 61.86 | 75.36 | **+21.83** | 18.4 → 22.4 |
| R4 | 79.08 | 79.37 | 78.79 | 79.28 | 78.94 | 79.32 | **+0.49** | 23.9 → 24.0 |
| SotN | 98.66 | 98.66 | 98.16 | 98.28 | 98.41 | 98.47 | **+0.06** | 59.5 → 59.5 |
| SH garage | 79.00 | 79.08 | 79.12 | 78.92 | 79.06 | 79.00 | **-0.08** | 19.1 → 19.1 |
| SH alley | 67.69 | 67.68 | 67.73 | 67.73 | 67.71 | 67.70 | **-0.01** | 20.8 → 20.7 |
| SH café | 69.27 | 69.30 | 69.52 | 69.36 | 69.40 | 69.33 | **-0.10** | 16.4 → 16.4 |
| GT2 | 50.49 | 50.68 | 50.44 | 50.74 | 50.46 | 50.71 | **+0.50** | 13.4 → 13.5 |
| Tekken 3 | 56.40 | 56.35 | 56.20 | 56.29 | 56.30 | 56.32 | **+0.03** | 34.2 → 34.3 |
| FFVII | 99.95 | 99.98 | 100.06 | 100.04 | 100.00 | 100.01 | **+0.01** | 15.2 → 15.2 |
| Klonoa | 85.65 | 85.71 | 85.65 | 85.76 | 85.65 | 85.73 | **+0.10** | 51.9 → 51.9 |
| Crash 2 | 73.30 | 73.64 | 73.45 | 73.38 | 73.37 | 73.51 | **+0.18** | 22.3 → 22.5 |
| NFS | 34.14 | 37.72 | 34.27 | 37.35 | 34.20 | 37.54 | **+9.76** | 11.0 → 12.0 |

Near-black samples: 0; failed runs: 0.

Final session, `renderer-cpu-4`, rcpu 31 → 63 = +G (one pair):

| Scene | g31 | g63 | base mean | cand mean | Δ % | pictures/s base → cand |
|---|---:|---:|---:|---:|---:|---:|
| Crash 1 | 84.20 | 84.23 | 84.20 | 84.23 | **+0.04** | 19.9 → 19.8 |
| CTR | 74.49 | 77.16 | 74.49 | 77.16 | **+3.58** | 22.1 → 23.0 |
| R4 | 79.20 | 80.11 | 79.20 | 80.11 | **+1.14** | 24.0 → 24.4 |
| SotN | 98.35 | 98.60 | 98.35 | 98.60 | **+0.25** | 59.6 → 59.5 |
| SH garage | 78.88 | 79.97 | 78.88 | 79.97 | **+1.39** | 19.1 → 19.3 |
| SH alley | 68.01 | 69.50 | 68.01 | 69.50 | **+2.18** | 20.9 → 21.0 |
| SH café | 70.00 | 70.06 | 70.00 | 70.06 | **+0.09** | 16.6 → 16.6 |
| GT2 | 51.09 | 52.00 | 51.09 | 52.00 | **+1.77** | 13.5 → 13.7 |
| Tekken 3 | 55.76 | 55.71 | 55.76 | 55.71 | **-0.10** | 34.1 → 33.9 |
| FFVII | 99.99 | 99.98 | 99.99 | 99.98 | **-0.00** | 15.2 → 15.2 |
| Klonoa | 84.08 | 86.81 | 84.08 | 86.81 | **+3.24** | 51.0 → 52.6 |
| Crash 2 | 71.46 | 71.68 | 71.46 | 71.68 | **+0.32** | 21.8 → 21.9 |
| NFS | 37.48 | 37.68 | 37.48 | 37.68 | **+0.56** | 11.8 → 11.8 |

Near-black samples: 0; failed runs: 0.

### Final build against RC3

Session `hw/final/`, lock held throughout: RC3 A1 (off, Auto) → `renderer-cpu-4`
(G pair above) → final `renderer-cpu-5` off, Auto, profile, off, Auto → RC3 A2 (off,
Auto) → library verified (`final-status.json`, `final.png`). "base" = mean of RC3
A1/A2, "cand" = mean of the two final runs. This includes code-layout effects
(+3.2 KB `.text`).

Frameskip off:

| Scene | a1-off | f-off | f2-off | a2-off | base mean | cand mean | Δ % | pictures/s base → cand |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Crash 1 | 86.07 | 86.45 | 86.38 | 85.94 | 86.00 | 86.41 | **+0.48** | 20.4 → 20.5 |
| CTR | 61.14 | 78.18 | 78.05 | 61.09 | 61.11 | 78.12 | **+27.82** | 18.4 → 23.2 |
| R4 | 73.61 | 80.52 | 80.49 | 73.40 | 73.51 | 80.50 | **+9.52** | 22.4 → 24.4 |
| SotN | 98.45 | 98.08 | 98.11 | 98.61 | 98.53 | 98.10 | **-0.44** | 59.5 → 59.4 |
| SH garage | 72.99 | 80.60 | 80.55 | 73.17 | 73.08 | 80.58 | **+10.26** | 17.7 → 19.5 |
| SH alley | 61.40 | 70.12 | 70.20 | 61.40 | 61.40 | 70.16 | **+14.27** | 18.9 → 21.3 |
| SH café | 61.74 | 70.89 | 71.01 | 61.87 | 61.81 | 70.95 | **+14.80** | 14.7 → 16.9 |
| GT2 | 46.96 | 51.81 | 51.84 | 47.15 | 47.06 | 51.83 | **+10.14** | 12.7 → 13.7 |
| Tekken 3 | 54.60 | 56.36 | 56.33 | 54.51 | 54.56 | 56.34 | **+3.27** | 33.1 → 34.3 |
| FFVII | 99.99 | 99.98 | 99.99 | 99.96 | 99.97 | 99.98 | **+0.01** | 15.2 → 15.2 |
| Klonoa | 85.01 | 86.68 | 87.08 | 84.75 | 84.88 | 86.88 | **+2.36** | 51.5 → 52.7 |
| Crash 2 | 49.50 | 74.25 | 74.23 | 49.58 | 49.54 | 74.24 | **+49.85** | 15.1 → 22.6 |
| NFS | 34.23 | 37.16 | 36.99 | 34.30 | 34.27 | 37.07 | **+8.19** | 11.1 → 11.8 |

Near-black samples: 0; failed runs: 0.

Frameskip Auto:

| Scene | a1-auto | f-auto | f2-auto | a2-auto | base mean | cand mean | Δ % | pictures/s base → cand |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Crash 1 | 96.91 | 97.07 | 97.31 | 96.36 | 96.63 | 97.19 | **+0.57** | 14.4 → 14.4 |
| CTR | 70.12 | 89.35 | 89.52 | 69.50 | 69.81 | 89.43 | **+28.11** | 15.1 → 15.1 |
| R4 | 84.18 | 88.48 | 88.69 | 84.37 | 84.27 | 88.59 | **+5.12** | 15.7 → 17.8 |
| SotN | 100.02 | 99.95 | 100.01 | 99.98 | 100.00 | 99.98 | **-0.02** | 54.1 → 54.2 |
| SH garage | 76.10 | 85.00 | 84.86 | 75.99 | 76.04 | 84.93 | **+11.69** | 15.2 → 15.2 |
| SH alley | 69.64 | 82.43 | 82.63 | 69.88 | 69.76 | 82.53 | **+18.30** | 15.2 → 15.1 |
| SH café | 61.23 | 74.28 | 74.02 | 61.37 | 61.30 | 74.15 | **+20.96** | 14.6 → 15.2 |
| GT2 | 47.75 | 52.48 | 52.50 | 47.69 | 47.72 | 52.49 | **+10.00** | 11.4 → 12.3 |
| Tekken 3 | 69.24 | 70.14 | 70.25 | 69.28 | 69.26 | 70.19 | **+1.34** | 15.2 → 15.2 |
| FFVII | 100.06 | 100.10 | 100.08 | 99.97 | 100.01 | 100.09 | **+0.08** | 15.1 → 15.1 |
| Klonoa | 99.57 | 99.45 | 99.68 | 99.73 | 99.65 | 99.56 | **-0.09** | 15.2 → 15.2 |
| Crash 2 | 49.61 | 91.55 | 90.90 | 49.96 | 49.78 | 91.22 | **+83.24** | 14.3 → 16.2 |
| NFS | 34.52 | 37.58 | 37.61 | 34.24 | 34.38 | 37.59 | **+9.34** | 10.5 → 11.0 |

Near-black samples: 0; failed runs: 0.

Renderer path after the change (same profiling switches as §1, `hw/final/fprof`;
NFS here samples later frames than the 1 + 5 s benchmark):

| Scene | renderer path ms | draw calls ms | GPU-wait reads ms | textures ms | renderer Mcycles | batches/frame | syncs/frame |
|---|---:|---:|---:|---:|---:|---:|---:|
| Crash 1 | 5.0 → 5.0 | 0.3 → 0.2 | 0.0 → 0.0 | 0.9 → 0.9 | 1.46 → 1.41 | 8 → 7 | 0.0 → 0.0 |
| CTR | 12.7 → 6.7 | 1.3 → 0.4 | 4.0 → 0.1 | 5.6 → 1.4 | 2.74 → 1.71 | 42 → 19 | 2.2 → 0.0 |
| R4 | 9.4 → 7.2 | 3.1 → 1.5 | 0.0 → 0.0 | 1.6 → 1.7 | 2.48 → 1.90 | 116 → 82 | 0.0 → 0.0 |
| SotN | 9.7 → 9.8 | 0.5 → 0.3 | 2.4 → 2.4 | 3.9 → 5.3 | 2.02 → 2.06 | 10 → 10 | 1.0 → 1.0 |
| SH garage | 6.6 → 4.7 | 2.7 → 1.1 | 0.0 → 0.0 | 0.7 → 0.7 | 2.01 → 1.38 | 103 → 75 | 0.0 → 0.0 |
| SH alley | 12.5 → 9.3 | 5.2 → 2.4 | 0.0 → 0.0 | 1.2 → 1.2 | 3.43 → 2.48 | 188 → 148 | 0.0 → 0.0 |
| SH café | 11.8 → 8.7 | 5.3 → 2.4 | 0.0 → 0.0 | 0.6 → 0.6 | 3.34 → 2.41 | 198 → 154 | 0.0 → 0.0 |
| GT2 | 18.3 → 13.7 | 6.1 → 2.6 | 0.2 → 0.2 | 2.1 → 2.4 | 4.66 → 3.73 | 212 → 156 | 0.3 → 0.3 |
| Tekken 3 | 10.5 → 10.3 | 0.3 → 0.2 | 0.0 → 0.0 | 2.0 → 2.2 | 2.89 → 2.85 | 8 → 8 | 0.1 → 0.1 |
| FFVII | 5.9 → 4.9 | 1.2 → 0.7 | 0.0 → 0.0 | 0.9 → 0.9 | 1.25 → 1.05 | 42 → 38 | 0.0 → 0.0 |
| Klonoa | 7.5 → 6.7 | 0.6 → 0.4 | 0.0 → 0.0 | 1.9 → 1.8 | 2.26 → 2.02 | 23 → 23 | 0.0 → 0.0 |
| Crash 2 | 19.4 → 10.4 | 9.0 → 0.7 | 0.0 → 0.0 | 1.0 → 1.0 | 3.50 → 2.15 | 343 → 37 | 0.3 → 0.5 |
| NFS | 11.3 → 9.2 | 0.6 → 0.5 | 0.0 → 0.0 | 5.8 → 3.6 | 3.97 → 2.30 | 16 → 16 | 0.0 → 0.0 |

## 5. Tried and not kept

- **Exact triangle overlap instead of bounding boxes** for the additive queue
  (would let more effects wait): only 2–8% of the remaining overlap flushes in
  SH/R4/GT2 were bounding-box-only; the rest are real ordering dependencies.
  Not built.
- **Settling only the read part of a pending screen clear**: CTR settles ~15k
  fill pixels a frame on average; not worth the splitting logic.
- **Per-primitive path rewrites** (Tekken 3): not attempted; earlier rounds
  (`pica-inv-*`, `o3ds60`) found such rewrites lose on the Old's 16 KB I-cache.
- **Direct GPU command emission for draws** (bypassing citro3d's generic context
  update): even after A–C a batch still costs ~16 µs on hardware, so this is the
  next lever for SH/GT2/R4, but the command stream cannot be verified by the
  simulators here; left for a round with a real-citro3d command comparison.

## 6. Open points

- **New 3DS check pending** (console unavailable): every change removes work
  and leaves the picture identical, so no slowdown is expected; run the heavy
  gate there before release.
- What is left in the renderer path is mostly per-primitive work (decode, clipping,
  vertex packing: Tekken 3, GT2) and ~16 µs of citro3d overhead per remaining draw
  batch (SH ~150, GT2 ~155, R4 ~80 a frame, genuine ordering dependencies). SotN keeps
  one real render-to-texture read a frame.
- Shared tooling fix: `devtools/fastbench.py` crashed when a process exited
  while it scanned `/proc` (it stopped my first session); the `stat` moved
  inside the existing `try` (backup `fastbench.shared-backup.py`).
