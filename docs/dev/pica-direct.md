# Direct GPU commands for the renderer's draws (pica-direct)

The GPU renderer (`core/plugins/gpu_pica`) draws through citro3d. On the Old 3DS a draw
batch cost ~26–28 µs before the renderer-cpu round and still ~16 µs after it, most of it
citro3d overhead (docs/dev/renderer-cpu.md); heavy scenes draw up to ~165 batches a frame.
`psx3d/source/picadirect.c` sends the hot draws as PICA200 commands directly and keeps
citro3d for everything else. It is always on.

Two rounds: `t3/pica-direct` measured it behind a test switch (sections 1, 4, 5: the switch
build, `picadirect=off|on|full`); `t3/pica-direct-final` made the `full` form always on and
deleted the switch (sections 2, 3, 6, 7). Evidence, simulator, hardware logs and CIAs are
outside Git in `/home/tony/Development/PS1_3ds/devtools/pica-direct/` and
`.../devtools/pica-direct-final/` (`REPORT.md` in each).

## Summary

- **What ships:** every dirty state block is written in full, citro3d's registers in
  citro3d's order, straight into its command buffer, then the 11 draw commands from a
  template. Only the calls are gone. The simulator with the real citro3d 1.7.1 shows the
  **same register writes, values, masks and order** as citro3d for every draw (19 states
  × 600 frames). On the Old and New consoles the VRAM is **byte-identical** to
  core-first-2 in GT2, R4, CTR and FFVII; in SH café and SH alley it differs only in
  the spots that also change between runs of the citro3d path (section 7).
- **Not shipped: skipping writes of unchanged registers** (the switch build's `on`). The
  register state at every draw was identical, yet the real PICA200 changed 2–48 pixels per
  frame at polygon edges (SH, GT2, R4, CTR, FFVII). `full` kept 85–95% of `on`'s gain:
  the cost was citro3d's calls, not the writes.
- **Speed** (switch build, Old 3DS, same binary, heavy scenes, `on` vs off): SH café +7.7%
  (frameskip off) / +10.0% (Auto), SH alley +7.0 / +6.9%, GT2 +4.9 / +5.1%, SH garage
  +4.1 / +3.6%, R4 +3.9 / +2.2%, Crash 2 +3.2 / +2.5%; `full` vs off SH café +6.3%, SH
  alley +6.2%, GT2 +4.4%, R4 +3.3%.
- **Always-on build vs core-first-2, Old 3DS A/B/A, 13 heavy scenes** (section 7): frameskip
  off SH café +6.2%, SH alley +5.4%, GT2 +5.1%, R4 +3.1%, SH garage +2.8%, Crash 2 +2.0%,
  CTR +1.0%, no scene slower; Auto SH café +8.5%, alley +5.7%, GT2 +4.8%, worst −0.27%
  (NFS, within the bracket's spread). New 3DS: every heavy scene stays at ~100%.

## 1. What citro3d does per draw

`C3D_DrawArrays` (citro3d 1.7.1) calls `C3Di_UpdateContext`, which sends every state block
whose dirty flag is set, in full, one libctru `GPUCMD_Add` call per register: the effect
block is 18 registers (14 calls, 34 words) whenever any of alpha test, blend function or
blend colour changed, a texture environment stage 5 registers, a texture unit 6 registers
plus a texture-cache clear; then both shader stages' uniforms (a 96-flag scan each;
renderer-cpu skips it when nothing is dirty); then 11 draw commands, again one call each.
The renderer changes blend/alpha state and texture environment 0 on nearly every batch,
so nearly every draw resends the whole effect block.

Real citro3d 1.7.1 and libctru 2.7.0 code in the simulator (section 3), frames 201–400,
Old 3DS cache model (`cprof.py`; it underestimates instruction-cache misses about 3× and
cannot predict layout, so these are not hardware speeds):

| Scene | draws/frame | command words/draw citro3d / on / full | draw-path instructions/draw citro3d / on / full | draw-path modeled cycles/draw citro3d / on / full | frame modeled Mcycles citro3d | Δ frame cycles on / full |
|---|---:|---:|---:|---:|---:|---:|
| heavy:crash | 7.1 | 85.2 / 59.7 / 89.9 | 2104 / 982 / 982 | 4886 / 4135 / 3759 | 2.91 | **-0.34%** / -0.34% |
| heavy:crash2 | 54.0 | 67.7 / 36.8 / 73.5 | 1778 / 574 / 593 | 2631 / 1440 / 1688 | 4.14 | **-1.69%** / -1.21% |
| heavy:ctr | 26.1 | 69.7 / 41.4 / 74.8 | 1764 / 652 / 690 | 3495 / 2570 / 2673 | 4.97 | **-0.60%** / -0.60% |
| heavy:ff7 | 46.9 | 67.1 / 38.0 / 72.7 | 1750 / 619 / 619 | 2917 / 1659 / 1945 | 3.01 | **-1.66%** / -0.66% |
| heavy:gt2 | 178.4 | 63.1 / 29.1 / 69.0 | 1710 / 505 / 510 | 2464 / 1022 / 1322 | 7.83 | **-3.70%** / -2.81% |
| heavy:klonoa | 15.0 | 74.2 / 54.9 / 77.9 | 1805 / 936 / 936 | 4479 / 3762 / 3762 | 4.10 | **-0.24%** / -0.24% |
| heavy:nfs | 9.9 | 76.1 / 57.2 / 80.2 | 2126 / 1113 / 1113 | 6239 / 5425 / 5425 | 6.97 | **+0.00%** / +0.00% |
| heavy:r4 | 117.2 | 63.8 / 31.8 / 69.7 | 1723 / 520 / 520 | 2354 / 1006 / 1257 | 4.74 | **-4.01%** / -3.16% |
| heavy:sh-alley | 182.1 | 63.4 / 28.8 / 69.4 | 1730 / 511 / 511 | 2516 / 1177 / 1413 | 5.99 | **-3.84%** / -3.17% |
| heavy:sh-cafe | 191.4 | 63.3 / 28.5 / 69.2 | 1724 / 502 / 507 | 2437 / 1078 / 1358 | 5.52 | **-5.25%** / -3.99% |
| heavy:sh-garage | 86.0 | 64.7 / 29.4 / 70.5 | 1755 / 535 / 535 | 2524 / 1184 / 1464 | 5.76 | **-2.26%** / -1.74% |
| heavy:sotn | 14.3 | 97.9 / 71.5 / 103.7 | 2311 / 1541 / 1541 | 6569 / 6569 / 6569 | 3.09 | **-0.32%** / +0.00% |
| heavy:tekken | 8.1 | 91.4 / 85.5 / 92.4 | 1988 / 1366 / 1366 | 5993 / 5993 / 5993 | 5.22 | **-0.19%** / +0.00% |
| std:o3auto | 14.5 | 77.7 / 50.3 / 82.7 | 1862 / 828 / 759 | 4066 / 3142 / 3327 | 3.75 | **-0.53%** / -0.53% |
| std:sh | 103.9 | 64.4 / 31.1 / 70.3 | 1752 / 520 / 520 | 2657 / 1445 / 1651 | 4.85 | **-2.89%** / -2.47% |
| std:r4 | 2.7 | 128.6 / 121.4 / 129.8 | 2247 / 1873 / 1873 | 9034 / 11041 / 11041 | 2.78 | **+0.00%** / +0.00% |
| std:ctr | 18.9 | 69.2 / 43.1 / 73.8 | 1694 / 741 / 741 | 3547 / 2412 / 2554 | 4.44 | **-0.45%** / -0.45% |
| std:gt2 | 81.1 | 64.2 / 29.8 / 70.0 | 1726 / 530 / 542 | 2511 / 1189 / 1453 | 6.06 | **-1.98%** / -1.49% |
| std:sotn | 14.3 | 97.9 / 71.5 / 103.7 | 2311 / 1541 / 1541 | 6569 / 6569 / 6569 | 3.03 | **+0.00%** / +0.00% |

"Draw path" is every citro3d/libctru function on the way (`C3D_DrawArrays`,
`C3Di_UpdateContext`, the bind functions, `GPUCMD_*`, the uniform wrapper) plus the new
code; scenes with few draws carry per-frame costs (frame buffer bind, presentation) in
their per-draw numbers. `full` sends more words than citro3d (single writes instead of
incremental ones) and is still almost as cheap on the CPU: the cost was the calls.

## 2. Design

`psx3d/source/picadirect.c`, frontend only, `--wrap=C3D_DrawArrays` at the final link.

- If citro3d's dirty flags contain only blocks the renderer changes between draws
  (viewport, scissor, effect, texture units and status, texture environment stages) and
  nothing else would make `C3Di_UpdateContext` send commands (light environment, fixed
  attributes, gas mode, a non-2D texture), those blocks are written straight into
  citro3d's command buffer, every register of each dirty block, in `C3Di_UpdateContext`'s
  order, as single register writes (citro3d uses incremental writes for some runs: same
  registers and values, other packing). The texture-cache clear citro3d issues with every
  texture rebind is part of that. The flags are cleared exactly as `C3Di_UpdateContext`
  clears them, uniforms go through citro3d's own code when any uniform is dirty, and the
  11 draw commands are copied from a template (same commands, same order).
- Any other dirty state (a new program, frame buffer or attribute layout, a restore after
  the HOME Menu, which marks everything dirty) and any draw that would not fit the command
  buffer's free room take the real `C3D_DrawArrays`. citro2d's draws use
  `C3D_DrawElements` and are not affected.
- Uniforms: one scan of the public dirty flags of both shaders replaces the two calls into
  gpu_cache.c's skip wrapper. Exact: loading shader constants (the only use of the
  wrapper's private flag) always marks the boolean uniforms dirty too.
- The core is not touched: the wrap applies at the final link, so core and library code
  keep their addresses (only the six `C3D_DrawArrays` call sites in gpu_pica now branch to
  the wrapper). `picadirect.o` links after every other frontend object (Makefile), so no
  existing frontend code moves either. Text +1.6 KB.
- The switch build also kept a copy of what the GPU was last sent (`--wrap` of
  `C3Di_UpdateContext` and `pica_game_begin`) to skip unchanged registers. With every
  dirty block written in full there is nothing to compare, so both wraps are gone.
- It depends on citro3d 1.7.1's context layout (`psx3d/vendor/citro3d/internal.h`, also
  used by renderqueue.c) and on `C3Di_UpdateContext`'s order: a citro3d update needs the
  simulator gate rerun.

## 3. Proof in the simulator

**Simulator with the real citro3d.** `devtools/pica-direct-final/model/` (first round:
`devtools/pica-direct/model/`): the renderer and core built for qemu-arm against the real
libctru/citro3d headers, linked with citro3d 1.7.1's own sources (identical to the
official v1.7.1 tag and to the installed headers), libctru 2.7.0's `gpu.c` and the
frontend's uniform wrapper as they are. Two runners from the same core: `runner_c3d`
(citro3d's own `C3D_DrawArrays`) and `runner_direct` (`picadirect.c` as it is, with the
3DS link's `--wrap`). Frames open, split and close like `renderqueue.c`; each frame ends
with a presentation draw shaped like `video_present_screens` (citro2d-like program,
attributes, texture environment, effect, scissor, `C3D_DrawElements`, then
`pica_state_lost()`).

**Checker** (`picachk.c`): interprets every submitted command list like the GPU's command
processor (masked, incremental and repeated writes) and at every draw hashes the complete
register file, both shaders' float uniforms as uploaded, a digest of all streamed uploads,
and the flush/invalidate/texture-cache-clear events since the previous draw. Since
pica-direct-final it also hashes the **write sequence** per frame: every register write
as (register, byte mask, value) in order, whatever the packing (a 16-byte padding
finalize does not count). Equal sequences mean the GPU receives the same writes in the
same order.

**Gate** (`gate.py --writes`): 13 heavy + 6 standard states × 600 frames, `runner_c3d` vs
`runner_direct`, per frame: draw count, every draw's register-state hash, the write
sequence, `PICA_SIM_HASH`, guest checkpoints: **19/19 identical, write sequences
included** (687,772 draws; the direct path sends 9% more command words, single instead
of incremental writes). Negative controls (`model/mut-final-*.txt`): leaving out the
texture-cache clear or the blend colour write changes the register state; leaving out
the gas depth-function write or re-sending the texture environment leaves the register
state the same and is caught only by the write sequence, at frame 0–1 in every scene
that exercises it. The switch build's `on` was of that second kind. Azahar (which
emulates the PICA200 from our command lists): Crash VRAM at frame 120 identical to
core-first-2.

What the state check cannot see is anything the real GPU does on a register *write*
beyond storing the value; section 5 found such an effect. The shipped path makes the same
writes as citro3d, so it does not depend on it.

## 4. Old 3DS speed (switch build)

Session `hw/old-1` (pica-direct-1): RC4 A1 → install → picadirect off / on / off → VRAM
dumps → RC4 A2; heavy slots 5–7, 3 s warm-up + 12 s (NFS 1 + 5 s), pmu 0,2; 130 runs,
0 failed, 0 near-black samples. Game speed % (100 = full speed). "on vs off" is the same
binary; "on vs RC4" includes this build's layout; "off vs RC4" is the layout cost alone.

Frameskip off:

| Scene | RC4 A1 | off b1 | **on** | off b2 | RC4 A2 | on vs off | on vs RC4 | off vs RC4 | pictures/s off → on | batches/s off → on |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Crash 1 | 86.39 | 85.84 | **86.28** | 85.66 | 86.73 | **+0.63%** | -0.32% | -0.94% | 20.3 → 20.5 | 595 → 597 |
| CTR | 77.95 | 78.07 | **78.68** | 77.95 | 77.87 | **+0.85%** | +0.98% | +0.13% | 23.3 → 23.4 | 796 → 800 |
| R4 | 80.07 | 79.63 | **82.65** | 79.46 | 80.07 | **+3.91%** | +3.23% | -0.66% | 24.2 → 25.0 | 3617 → 3706 |
| SotN | 97.68 | 97.75 | **98.15** | 97.80 | 97.80 | **+0.39%** | +0.42% | +0.03% | 59.3 → 59.3 | 583 → 585 |
| SH garage | 80.12 | 79.54 | **82.67** | 79.35 | 79.99 | **+4.06%** | +3.26% | -0.77% | 19.3 → 19.9 | 3617 → 3752 |
| SH alley | 69.58 | 68.85 | **73.63** | 68.78 | 69.72 | **+7.00%** | +5.72% | -1.20% | 21.0 → 22.4 | 6100 → 6541 |
| SH café | 70.53 | 69.67 | **75.00** | 69.68 | 70.53 | **+7.65%** | +6.34% | -1.22% | 16.5 → 17.7 | 6453 → 6918 |
| GT2 | 51.63 | 51.45 | **54.00** | 51.55 | 51.75 | **+4.86%** | +4.47% | -0.37% | 13.6 → 14.2 | 5102 → 5456 |
| Tekken 3 | 65.98 | 65.62 | **65.69** | 65.49 | 65.84 | **+0.21%** | -0.33% | -0.54% | 39.7 → 39.8 | 307 → 308 |
| FFVII | 99.99 | 100.07 | **99.97** | 99.97 | 99.98 | **-0.05%** | -0.01% | +0.04% | 15.1 → 15.1 | 2286 → 2284 |
| Klonoa | 86.85 | 86.25 | **86.94** | 86.13 | 86.74 | **+0.88%** | +0.17% | -0.70% | 52.3 → 52.6 | 1154 → 1163 |
| Crash 2 | 73.58 | 72.61 | **74.91** | 72.61 | 73.87 | **+3.18%** | +1.61% | -1.52% | 22.0 → 22.7 | 1429 → 1459 |
| NFS HS | 37.09 | 36.68 | **36.86** | 36.71 | 36.88 | **+0.44%** | -0.35% | -0.79% | 11.7 → 11.8 | 624 → 627 |

Game speed % (100 = full speed). 65 runs, 0 failed, 0 near-black samples.

Frameskip Auto:

| Scene | RC4 A1 | off b1 | **on** | off b2 | RC4 A2 | on vs off | on vs RC4 | off vs RC4 | pictures/s off → on | batches/s off → on |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Crash 1 | 97.39 | 96.85 | **97.91** | 96.92 | 97.09 | **+1.05%** | +0.69% | -0.36% | 14.4 → 14.6 | 401 → 402 |
| CTR | 88.86 | 88.65 | **89.85** | 88.75 | 89.06 | **+1.29%** | +0.99% | -0.29% | 15.2 → 15.2 | 516 → 515 |
| R4 | 88.48 | 87.72 | **89.61** | 87.59 | 88.06 | **+2.23%** | +1.52% | -0.70% | 17.3 → 18.3 | 2475 → 2638 |
| SotN | 100.02 | 100.00 | **99.95** | 99.96 | 99.99 | **-0.03%** | -0.06% | -0.02% | 53.5 → 53.8 | 541 → 542 |
| SH garage | 84.50 | 83.77 | **86.72** | 83.70 | 84.54 | **+3.57%** | +2.61% | -0.93% | 15.2 → 15.2 | 2849 → 2856 |
| SH alley | 81.53 | 80.37 | **86.03** | 80.52 | 81.30 | **+6.94%** | +5.66% | -1.19% | 15.1 → 15.1 | 4430 → 4433 |
| SH café | 73.28 | 72.03 | **79.25** | 72.06 | 73.13 | **+10.00%** | +8.25% | -1.59% | 15.2 → 15.2 | 5972 → 5961 |
| GT2 | 52.44 | 51.97 | **54.60** | 51.96 | 52.28 | **+5.07%** | +4.28% | -0.75% | 12.1 → 12.7 | 5003 → 5407 |
| Tekken 3 | 91.27 | 90.68 | **90.76** | 90.58 | 91.25 | **+0.14%** | -0.54% | -0.69% | 15.2 → 15.1 | 157 → 157 |
| FFVII | 99.98 | 99.98 | **100.09** | 99.98 | 99.98 | **+0.11%** | +0.10% | -0.01% | 15.2 → 15.1 | 2284 → 2280 |
| Klonoa | 100.01 | 99.92 | **99.68** | 99.71 | 99.74 | **-0.13%** | -0.19% | -0.06% | 15.3 → 15.2 | 843 → 849 |
| Crash 2 | 91.84 | 89.84 | **91.90** | 89.42 | 91.14 | **+2.53%** | +0.44% | -2.03% | 16.0 → 16.2 | 1060 → 1049 |
| NFS HS | 37.31 | 36.76 | **37.41** | 37.18 | 37.33 | **+1.17%** | +0.23% | -0.94% | 10.9 → 11.0 | 610 → 617 |

Game speed % (100 = full speed). 65 runs, 0 failed, 0 near-black samples.

Per renderer batch (frameskip off; a batch is one or two draws):

| Scene | ms per emulated frame off → on | saved ms/frame | batches/frame | saved µs per batch | model: draws/frame, saved µs per draw, saved ms/frame |
|---|---:|---:|---:|---:|---:|
| SH alley | 24.22 → 22.64 | 1.59 | 148 | 10.7 | 182, 5.0, 0.86 |
| SH café | 23.92 → 22.22 | 1.70 | 154 | 11.0 | 191, 5.1, 1.08 |
| SH garage | 20.98 → 20.16 | 0.82 | 76 | 10.8 | 86, 5.0, 0.49 |
| GT2 | 32.36 → 30.86 | 1.50 | 165 | 9.1 | 178, 5.4, 1.08 |
| R4 | 20.95 → 20.16 | 0.79 | 76 | 10.4 | 117, 5.0, 0.71 |
| Crash 2 | 22.95 → 22.25 | 0.71 | 33 | 21.5 | 54, 4.4, 0.26 |
| CTR | 21.36 → 21.18 | 0.18 | 17 | 10.6 | 26, 3.5, 0.11 |

The direct path saves **9–11 µs per batch** of the ~16 µs citro3d overhead, 1.4–2.7×
the model's saving (the model underestimates instruction-cache misses).

`full` against `on` and off (session `hw/old-3`, pica-direct-2, frameskip off, two runs
each):

| Scene | off | on | full | off | on | full | full vs off | on vs off | full vs on |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R4 | 79.65 | 82.31 | 82.35 | 79.62 | 82.42 | 82.18 | **+3.30%** | +3.43% | -0.13% |
| SH alley | 69.04 | 73.65 | 73.30 | 69.04 | 73.60 | 73.37 | **+6.22%** | +6.64% | -0.40% |
| SH café | 69.88 | 75.01 | 74.14 | 69.77 | 74.78 | 74.27 | **+6.27%** | +7.26% | -0.92% |
| GT2 | 51.59 | 54.20 | 53.86 | 51.56 | 54.25 | 53.78 | **+4.35%** | +5.14% | -0.75% |

The switch-off build is 0.4–1.5% slower than RC4 (Crash 2 −2.0% Auto) in the same
session while its own runs agree within 0.2–0.5%. Not the direct path (same binary as the
on runs): the switch parsing and status in `main.c`, `devswitch.c` and `devcontrol.c` move
`renderqueue.c`, `video.c` and the gpu_cache wrappers by 96–112 bytes. An always-on build
without that code keeps every code symbol of core-first-2 at its address; its A/B/A is
section 7.

## 5. Pictures on the console (switch build)

`vramdump=60` (whole PS1 VRAM with mask bits, emulated frame 60, frameskip off) per
load, then fetched. Differing pixels of 524,288:

| Scene | RC4: two runs | RC4: normal vs slowed | RC4 no overlap: normal vs slowed | RC4: overlap vs none | pd-1: off vs off | pd-1: off vs **on** | pd-2: off vs **on** (no overlap / overlap) | pd-2: off vs **full** (no overlap / overlap) |
|---|---|---|---|---|---|---|---|---|
| SH alley | identical | identical | identical | identical | identical | 21 px | 25 px / 21 px | identical / identical |
| SH café | identical | 2 px | 1 px | 1 px | — | 48 px | 40 px / 43 px | 3 px / 2 px |
| GT2 | identical | identical | identical | identical | identical | 8 px | 8 px / 9 px | identical / identical |
| R4 | identical | identical | identical | identical | — | 11 px | 11 px / 11 px | identical / identical |
| CTR | — | — | — | — | — | 2 px | 2 px / 2 px | identical / identical |
| FFVII | — | — | — | — | — | 30 px | — | — |
| Crash 2 | — | — | — | — | — | identical | — | — |
| SotN | — | — | — | — | — | identical | — | — |
| Tekken 3 | — | — | — | — | — | identical | — | — |

- The console is deterministic for a fixed binary and mode (run vs run identical). RC4
  under timing changes (renderer phase sampling slows it; GPU overlap off) keeps SH alley,
  GT2 and R4 identical; SH café has 1–2 pixels that move with timing in RC4 itself.
- `on` changes the same pixels in every session and with or without overlap (R4: the same
  11 pixels in all three captures): a systematic effect of skipped writes, at polygon edges
  (SH café chairs/sofa/door frame; SH alley pixels turning black). `full` is identical
  except SH café's timing pixels.
- What `on` leaves out (simulator, SH café, 120 frames): citro3d writes each of the depth
  map, culling, stencil, logic op, colour operation, blend colour, fragment shadow,
  early-depth and TEV0 source/colour registers 22,768 times (once per draw), `on` 199
  times (when citro3d itself draws); blend function and alpha test are still written
  when they change. Which of these the GPU
  needs rewritten is not known (possibly a write to a fragment-operation register orders
  the pixel pipeline between draws). Finding it would take one session with a per-register
  "always write" mask; it is worth at most the 0.1–0.9% between `on` and `full`.

## 6. Code size, layout

Always-on build (pica-direct-final-1) against core-first-2: `tools/layout/check.sh` PASS.
`compare.py`: core assembly/C and library code at the same addresses; core data, constants
and bss move by one whole page (cache sets kept); the only changed core code words are
the six `bl C3D_DrawArrays` → `bl __wrap_C3D_DrawArrays`. Every frontend code symbol,
including the hot `gpu_cache.c` wrappers, `renderqueue.c`, audio and video, keeps
core-first-2's address; frontend data moves by one whole page. Text +1,596 B (the switch
build: +3.4 KB, and its switch code moved hot frontend code by 96–112 B).

Cache model (`model/prof.py`, Old 3DS model, frames 201–400; not hardware speeds):
draw-path instructions per draw citro3d → always-on: SH café 1698 → 413, GT2 1682 → 415,
R4 1697 → 426, Crash 2 1760 → 482; frame cycles SH café −7.1%, GT2 −5.1%, R4 −5.3%,
Crash 2 −2.4% (the switch build's `full`: −4.0, −2.8, −3.2, −1.2%).

## 7. Always on: consoles (pica-direct-final-1)

Build `pica-direct-final-1` (CIA `0f3e8779…`, counter 4100) against core-first-2 (`e3015506…`),
heavy slots 5–7, 3 s warm-up + 12 s (NFS 1 + 5 s), `pmu=0,2`, player defaults.

**Old 3DS A/B/A** (session `hw/old-1`, 2026-10-21 00:28–01:06 UTC): core-first-2 A1 →
candidate B → core-first-2 A2, each off and Auto; 78 runs, 0 failed, 0 near-black
samples. Game speed % (100 = full speed); delta = B vs mean(A1, A2):

| Scene | off A1 | off B | off A2 | **off Δ** | Auto A1 | Auto B | Auto A2 | **Auto Δ** |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Crash 1 | 86.13 | 86.84 | 86.34 | **+0.70%** | 96.98 | 96.99 | 97.50 | **−0.26%** |
| CTR | 77.95 | 78.73 | 77.93 | **+1.01%** | 89.20 | 89.78 | 89.31 | **+0.58%** |
| R4 | 80.05 | 82.34 | 79.63 | **+3.13%** | 87.94 | 89.20 | 88.18 | **+1.30%** |
| SotN | 97.74 | 97.99 | 97.42 | **+0.42%** | 99.98 | 99.91 | 99.99 | **−0.08%** |
| SH garage | 80.06 | 82.35 | 80.13 | **+2.82%** | 84.09 | 86.35 | 84.19 | **+2.62%** |
| SH alley | 69.80 | 73.58 | 69.86 | **+5.37%** | 81.28 | 85.86 | 81.25 | **+5.65%** |
| SH café | 70.47 | 74.79 | 70.44 | **+6.16%** | 72.72 | 78.96 | 72.87 | **+8.46%** |
| GT2 | 51.70 | 54.23 | 51.53 | **+5.07%** | 52.07 | 54.76 | 52.40 | **+4.84%** |
| Tekken 3 | 65.74 | 65.87 | 65.81 | **+0.14%** | 90.86 | 90.80 | 90.99 | **−0.14%** |
| FFVII | 99.94 | 99.95 | 99.92 | **+0.02%** | 100.04 | 100.03 | 100.11 | **−0.05%** |
| Klonoa | 86.54 | 86.95 | 86.45 | **+0.53%** | 99.65 | 99.63 | 100.06 | **−0.23%** |
| Crash 2 | 73.86 | 75.32 | 73.77 | **+2.03%** | 89.91 | 91.71 | 89.91 | **+2.01%** |
| NFS HS | 37.30 | 37.46 | 37.21 | **+0.54%** | 37.57 | 37.53 | 37.69 | **−0.27%** |

Frameskip off, emulated-frame time saved per renderer batch: SH alley 1.19 ms / 148
batches = 8.1 µs, SH café 8.5 µs, GT2 9.5 µs, R4 8.7 µs, SH garage 7.4 µs, Crash 2
12.9 µs. The Auto rows below 0 are scenes already at or near full speed or with a wider
A1/A2 spread (Crash 1 0.53%, Klonoa 0.41%).

**New 3DS** (session `hw/new-1`, GPU worker thread as players have it): every heavy scene
99.8–100.1% on both builds, off and Auto (worst delta −0.03%); main-core ms per frame
lower (SH alley 6.28 → 5.81, SH café 5.49 → 5.05, GT2 10.26 → 9.93, frameskip off).
3D slider down; the candidate has no real-3D code (core-first base).

**Pictures** (`vramdump=60`, whole PS1 VRAM, same session as the base build):

| Scene | Old: core-first-2 vs candidate | New: core-first-2 vs candidate |
|---|---|---|
| GT2 | identical | identical |
| R4 | identical | identical |
| CTR | identical | identical |
| FFVII | identical | identical |
| SH café | 2nd runs identical; candidate's 1st run 2 px off its own 2nd | 1st runs identical; candidate's 2nd run 1 px off its own 1st |
| SH alley | 4 px (one spot, x 231–233) | identical |

SH café and SH alley each have a spot that changes between runs of the same build: SH
café's 1–3 pixels were already seen on RC4, and SH alley's spot (x 231–233, y 218–229 and
442) differs by 10 px between RC4 and the switch build's citro3d path in earlier sessions.
The candidate's SH alley dump is bit-identical to the switch build's citro3d-path (`off`)
dumps from session `pica-direct/hw/old-1`. Everything outside those spots is
byte-identical on both consoles.

## Reproduce

```sh
source devtools/env.sh; F=devtools/pica-direct-final
python3 $F/tools/prepare.py ~/.cache/pdf-sim --hash        # private core copy (not /tmp)
bash $F/model/build_c3d.sh ~/.cache/pdf-sim                 # runner_c3d + runner_direct
python3 $F/model/gate.py ~/.cache/pdf-sim <out> --writes    # 19 states x 600 frames
MUTATE=$F/model/mut-final-gasdepth.txt bash $F/model/build_c3d.sh ~/.cache/pdf-sim
python3 $F/model/gate.py ~/.cache/pdf-sim <out> --writes --frames 60 --candidate runner_direct_mut-final-gasdepth
python3 $F/tools/prepare.py ~/.cache/pdf-perf && bash $F/model/build_c3d.sh ~/.cache/pdf-perf
python3 $F/model/prof.py ~/.cache/pdf-perf <out>             # cache model, checker off
```

Console: `devtools/pica-direct-final/run-old-1.sh`, `run-new-1.sh`; `tools/vramcmp.py`
for the dumps (needs numpy: `devtools/gpu-audit/venv/bin/python`).
