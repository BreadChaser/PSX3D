# Audio callback and tile generation maps (Old 3DS)

Branch `t3/audio-tiles`, from `t3/core-first` (`6083ee9`). Evidence, CIAs and hardware logs
are outside Git in `/home/tony/Development/PS1_3ds/devtools/audio-tiles/` (`REPORT.md`). No push.

Two ideas from the DeepSeek performance review (`docs/dev/perf-review-ds.md` on
`t3/perf-review-ds`, §2.5 and §2.6), each measured on its own:

- **A: the frontend audio callback**, 0.54–0.82 ms per emulated frame in every heavy scene.
- **B: the renderer's 8x8 tile generation maps** (`fine_gen`, `fine_draw_gen`, 64 KB as u32).

## Summary

| Idea | Verdict | Old 3DS, 13 heavy scenes |
|---|---|---|
| A: write wave buffers back with `svcStoreProcessDataCache` instead of `DSP_FlushDataCache` | **Always on** (`eb9fdf8`) | vs core-first-2: off +0.5% to +2.1%, Auto up to +2.3%, no scene slower (scenes at 100% unchanged); callback 0.54–0.82 → 0.24–0.50 ms |
| A: Catmull-Rom weights shared by both channels (`afast=simd`) | Not integrated: output within one step of today's, needs the user's listening test | −0.08 to −0.15 ms only on the cubic path (scenes at or near full speed); slow scenes use the linear path (−0.01 ms) |
| A: bit-exact resampler rewrite (`afast=exact`) | Rejected | cubic path −15 to −28%, linear path slower |
| B: 16-bit tile generation maps, exact | **Rejected** (reverted) | vs the same build without it: SH garage −1.3%, SH alley −1.4%, Tekken 3 −1.5% (off); nothing faster |

Final build `audio-tiles-3` = core-first-2 plus one changed call; every function at the same
address as in core-first-2. CIA `devtools/audio-tiles/artifacts` (SHA256 `b576c44b…`,
counter 4100); a rebuild of `e90e87e` gives the identical 3DSX and ELF.

## A. Audio callback

### What the callback does

The review assumed the CPU resamples 44.1 kHz to the DSP's 32.7 kHz. It does not: the DSP's
polyphase filter does that (`ndspChnSetRate(44100)`, `NDSP_INTERP_POLYPHASE`). The CPU
resampler (`audio_resample.c`) only bends the rate a little:

- full speed: step 1 ± 0.5% to keep the queue at its target (the PS1's 59.94 Hz against the
  3DS screen's 59.83 Hz needs about 0.998): fixed-point Catmull-Rom ("cubic"), Q20/Q12;
- slow scenes with audio stretch: step below 0.99: linear interpolation in Q15.

Then `submit()` copies into 512-frame NDSP wave buffers; each full buffer is written back to
memory for the DSP and queued with `ndspChnWaveBufAdd`, one or two buffers per emulated frame
(735 input frames, more output while stretching).

### Where the time goes (aprof=on, Old 3DS)

Wall time per emulated frame, test build audio-tiles-1, 7 heavy scenes, frameskip off:

| Part | slow scenes (CTR, SH café, GT2, Tekken 3) | full speed (SotN, FFVII; Crash 1 mixed) |
|---|---:|---:|
| `DSP_FlushDataCache` (write-back) | **0.38–0.46 ms** | 0.34–0.40 ms |
| resampling | 0.14 ms (linear) | 0.27–0.33 ms (mostly cubic) |
| copy into the wave buffers | 0.03 ms | 0.03 ms |
| `ndspChnWaveBufAdd` | 0.007 ms | 0.007 ms |
| everything else | 0.03 ms | 0.03 ms |
| **whole callback** | **0.60–0.69 ms** | **0.69–0.79 ms** |

`DSP_FlushDataCache` is an IPC request to the DSP system module (command 0x13 with our
process handle); the module then performs the cache operation on our range. The round trip,
not the cache work, is the cost: the kernel call `svcStoreProcessDataCache` (which `gpu_cache.c`
already uses for GPU buffers) does the write-back in **0.05–0.07 ms**. The DSP only reads these
buffers, so a write-back is all it needs; the samples are the same.

### The resampler

Instructions per output frame (devkitARM GCC 16.1, `-O2`, from the disassembly):

| Path | today | afast=exact | afast=simd |
|---|---:|---:|---:|
| cubic (full speed) | ~65 (stack spills, literal loads) | ~63 | ~52 |
| linear (stretch) | ~25 | ~23 | ~20 |

- `afast=exact`: the same arithmetic bit for bit (both channels per pass, `SSAT`, the loop runs
  on the read position only). Measured: cubic path −15 to −28%, linear path 6–20% *slower*.
- `afast=simd`: the four Catmull-Rom weights (or two linear weights) are worked out once per
  output frame in Q15 and shared by both channels through `SMLAD`/`PKHBT`. Measured: cubic
  path −30 to −40% (0.27–0.33 → 0.17–0.20 ms), linear path −7 to −12% (0.14 → 0.13 ms).
  Output differs by rounding. On 10 s of core audio from five games (Crash 2, CTR, GT2, NFS,
  Tekken 3): cubic path at most 1 step off on 3.6–7.5% of samples, RMS difference −101.5 to
  −104.8 dBFS, and slightly *closer* to a double-precision reference than today's code
  (e.g. CTR −100.9 vs −100.4 dBFS); linear path identical except one GT2 sample. Full-scale
  noise: 98.3% identical, worst case 3 steps. Listening set in
  `devtools/audio-tiles/wav/` (`README.md`).
- Proof for both: a test (in `c2ba2ba`, `tests/audio_fast_test.c`) runs today's code and both
  variants on 43.4 M output frames (noise, full-scale extremes, wrapping ramps; random chunk
  sizes and steps), on x86 with ASan/UBSan and on ARMv6K in qemu. `exact` matched frame for
  frame including read position and history; `simd` matched frame counts and positions.

### Old 3DS results

Session `hw/old-1`, 2026-10-20 17:37–18:49 local, lock.old held throughout: core-first-2 (A1) →
audio-tiles-1 (B, switches) → audio-tiles-2 (C, tile maps) → audio-tiles-3 (D, write-back
always on) → core-first-2 (A2) → release-v041-rc4. 13 heavy scenes, 3 s warm-up, 12 s sample
(NFS 1 + 5 s), PMU on; Old with BootNTR loaded and the low NTR stream as left by other workers
(same for every build). 182 runs, none failed, no black samples. Six installs, each new
session in 38–41 s.

Game speed %, D = the final build, vs the mean of A1 and A2 (A1↔A2 at most 0.7% apart):

| Scene | off A1 / A2 | off D | **D vs A** | Auto A1 / A2 | Auto D | **D vs A** |
|---|---:|---:|---:|---:|---:|---:|
| Crash 1 | 86.0 / 86.4 | 87.1 | **+1.06%** | 97.1 / 97.1 | 97.5 | +0.40% |
| CTR | 78.0 / 77.9 | 79.0 | **+1.37%** | 89.1 / 89.0 | 90.9 | **+2.05%** |
| R4 | 79.7 / 79.6 | 80.8 | **+1.47%** | 87.8 / 88.2 | 88.7 | +0.86% |
| SotN | 97.5 / 98.2 | 98.4 | +0.56% | 100.0 / 100.0 | 99.9 | −0.07% (at 100%) |
| SH garage | 80.0 / 80.0 | 81.3 | **+1.62%** | 84.0 / 84.0 | 85.9 | **+2.31%** |
| SH alley | 69.8 / 69.8 | 70.5 | **+1.05%** | 81.6 / 81.3 | 83.0 | **+1.88%** |
| SH café | 70.6 / 70.5 | 71.3 | **+1.12%** | 73.1 / 73.1 | 74.3 | **+1.57%** |
| GT2 | 51.7 / 51.7 | 52.0 | +0.67% | 52.2 / 52.2 | 52.7 | +0.92% |
| Tekken 3 | 65.6 / 65.7 | 66.5 | **+1.27%** | 90.6 / 90.6 | 92.4 | **+1.96%** |
| FFVII | 100.0 / 100.0 | 100.0 | 0.00% | 100.0 / 100.0 | 100.1 | +0.13% |
| Klonoa | 86.6 / 86.5 | 88.2 | **+1.92%** | 99.4 / 99.7 | 99.6 | +0.02% |
| Crash 2 | 73.6 / 73.7 | 75.2 | **+2.07%** | 89.8 / 90.8 | 92.3 | **+2.20%** |
| NFS HS (5 s) | 36.8 / 37.0 | 37.1 | +0.50% | 37.4 / 37.3 | 37.6 | +0.76% |

Callback (`audio_ms`): A 0.54–0.82 ms; D 0.24–0.30 ms in the slow scenes (NFS 0.36 ms) and
0.37–0.50 ms in the scenes at or near full speed (cubic path).

Same binary (B), the switches against the default, run back to back per scene: with
`aflush=svc afast=simd` off +0.7% to +3.0% (FFVII at 100%: 0), Auto −0.2% to +2.5%; callback
0.23–0.34 ms. Against
D, the extra `afast=simd` is worth up to ~0.6% only where the cubic path runs (SotN off 99.0
vs 98.4, Klonoa off 88.7 vs 88.2); elsewhere within noise. B with switches off vs A: −0.9% to
+0.5% (Crash 2 off −0.9%, all others within ±0.6%): the 3 KB of test code in audio.c moved the
frontend code after it.

## B. Tile generation maps

### How hot the maps are

The replayer (`tools/gpu-compare`, native x86, the real `gpu_pica.c`) with a counter on every
read and write of the two maps, 19 traces × 600 frames:

| Trace | reads/frame | writes/frame | distinct 32-byte lines/frame | clock growth/frame |
|---|---:|---:|---:|---:|
| SotN (heavy = std) | 1,237 | 903 | 17.4 | 3.6 |
| CTR heavy / std | 864 / 427 | 867 / 865 | 22.6 / 12.9 | 1.0 |
| FFVII | 1,443 | 208 | 3.7 | 0.5 |
| Klonoa | 485 | 7 | 6.2 | 0.8 |
| SH garage / alley / std | 246 / 327 / 241 | 443 / 561 / 561 | 2.9 / 3.9 / 4.9 | 0.4–0.5 |
| GT2, R4, NFS | ≤ 7 | ≤ 4 | ≤ 1.7 | ≤ 0.1 |
| Crash 1, Crash 2, Tekken 3, SH café | 0 | 0 | 0 | 0.3–1.1 |

The maps are 64 KB, but a frame touches at most 23 lines (0.7 KB) of them, and four heavy
scenes don't use them at all (coarse 64x16 tracking until a GPU-drawn texture is read).
Halving the element size can save at most ~11 line fills per frame.

### Exact 16-bit maps (audio-tiles-2, `7060e42`, reverted in `a451272`)

The renderer only compares generations (`>`, `==`, max/min, non-zero), never subtracts them.
Before the clock would pass 0xff00, `gen_renumber()` replaced every stored generation (both tile
maps, the 64x16 block map, the CPU row map, every cache entry's `stamp`/`checked`, the texture
memo and the clock) by its rank among the values in use, zero staying zero. An order-preserving
map applied to all of them at once changes no decision; at most ~19.5k values exist. A 16 KB
static scratch kept the bss change at −16 KB, whole pages.

- Native replay, 19 traces × 600 frames: full-VRAM and draw hashes identical every frame with
  the real limit and with `PICA_GEN_LIMIT` 2000 and 40 (renumbering 8,796 times in SotN, 879 in
  CTR, 18 in Crash 1).
- ARM simulator, whole core with the dynarec, `PICA_SIM_HASH`, 6 standard + 13 heavy states ×
  600 frames: identical (Crash 2 heavy stops identically in both, as before).
- Layout: `gen_renumber` (520 bytes) lands among the renderer's cold functions and the 16-bit
  accesses add instructions (ARM `ldrh` has no scaled index), so 427 core functions move by
  4–752 bytes (`compare.py` FAIL by design).

Old 3DS, C vs B (identical audio code, only the maps differ): off SH garage −1.28%, SH alley
−1.39%, Tekken 3 −1.52%, the rest −0.6% to +0.5%; Auto SH garage −1.27%, Tekken 3
−1.03%. The code move costs speed and the maps had nothing to give back.

## Integration

- Take `eb9fdf8`; skip the "Test build identity" commits. The tile-map commit and its revert
  cancel out; `c2ba2ba` (the test switches) is removed again by `eb9fdf8` — it stays in history
  for `aprof` (callback split, six metric columns) and `afast=simd`.
- pacing-fix (`9a66395`, `b05b91a`): a three-way merge of `audio.c`, `audio_resample.c/.h` and
  the resampler test is clean (also with the test switches); the merged resampler passes both
  tests. Only build-identity lines conflict.
- `tools/layout/check.sh` passes; `compare.py` against core-first-2: core, libraries and every
  frontend function at the same address.
- Flag for the user's listening check: `afast=simd` (a quality change, WAVs ready) and, as a
  plain sanity check, the always-on write-back (same samples by design; a missed write-back
  would sound like crackling).
