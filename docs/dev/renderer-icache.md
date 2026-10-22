# Renderer instruction-cache work

The target is 25% less renderer CPU time on the Old 3DS, with identical VRAM
and pictures. No change is accepted from simulator speed alone. This round
starts at `0e1ca70d` (v0.5.0 rc4), branch `t3/renderer-icache`.

Compact clipping is the first accepted step: Old A/B/A gains GT2+1.18% off /
+1.12% Auto, SH café+1.60% /+1.36%, Klonoa+1.68% off. Every heavy scene stays
within the ~1% no-regression gate. All 13 hardware full-VRAM dumps are exact.
This is well below the 25% renderer target, which remains open. Final handoff
is exact shipped rc4 at title 5139, library idle, config unchanged.

The first measurement is a per-primitive instruction-line map, rather than
another specialized polygon implementation. Earlier specialized/leaner draw
code and hot-function ordering lost on hardware. The existing common inside-area
polygon path already writes directly to the vertex buffer; a second decoder
beside it would increase the instruction working set.

## Measurement

Ignored evidence and scripts: `devtools/renderer-icache/`. `tools/pathmap.c` is a
QEMU plugin: on entry to the polygon, rectangle, line and fill handlers it reads
the opcode and return address. Until that return it records every fetched
32-byte instruction line, including callees. It reports the mean number of
distinct lines and instructions per primitive, plus each line's frequency.
It observes frames 301–600 of seven heavy states; it does not instrument the
renderer source. Line fetches include predicated instructions, as the CPU must
fetch those too. This is a working-set map, not a cache-miss or hardware-time
prediction. Simulator citro3d remains stubbed; draw submission costs need PMU.

Examples from the initial map (GT2): flat textured quad 116.4 distinct lines /
1,737 instructions; flat untextured quad 77.0 / 771; flat textured triangle
126.2 / 1,491. Polygon decoding/packing itself accounts for 35–54 lines on the
common formats, the texture memo for 13–15, state selection for about 6–7.
Clipped polygons also fetch the existing slow decoder, emit/clip paths and
rounding helpers. Full per-format/function and source-line tables are in
`map-base/` and `map-cliploop/` (`summary.md`, `summary.json`, `lines.tsv`).

The weighted function map (`map-base/function-weighted.md`) puts the shared
polygon decoder at 42.51/112.88 lines in GT2, 51.16/104.36 in Tekken,
50.37/108.81 in CTR and 49.21/121.33 in R4. Clipping itself accounts for
7.98, 2.06, 5.63 and 6.22 lines respectively. Even removing all clipping
would leave most of the footprint. These boundaries include draw-handler
callees, but exclude caller-side gpulib dispatch, frontend work and real
citro3d code; the hardware renderer PMU and whole-frame speed gates cover
costs the simulator map omits.

## Experiments prepared offline

Compact clipping is accepted below; the other experiments remain offline. Each production build and
source snapshot is retained in the ignored evidence directory.

| Experiment | ARM production size | Measured instruction footprint |
|---|---|---|
| Command-word strides in the shared polygon decoder | polygon 2,868 → 2,852 B | less than 1% fewer fetched lines |
| Reuse decoded vertices for packed words, with strides | polygon 2,868 → 2,784 B | about 1–2% fewer fetched lines |
| Fuse the finishing loops in emit_tris, with the above | emit_tris 2,060 → 2,048 B | not yet mapped or gated |
| Load CLUT/page outside the vertex loop, with strides/in-place packing | polygon size in artifacts-packet | instructions fall ~2–3%; Tekken fetched lines grow 0.6%, set aside |
| Compact clipping, independently of the above | clip_tris 1,416 → 1,208 B | table below |

The accepted source change is compact clipping only. Alternate two scratch buffers
between clipping planes instead of copying every output polygon back; wrap the
last edge with a conditional instead of integer modulo; share the four signed
16-bit coordinate interpolation operations in one loop. Fixed-size memcpy reads
and writes keep object access valid without treating separate struct members
as an array. Plane order, edge order, arithmetic, rounding, colours and the
final coordinate clamp are preserved.

Weighted mean distinct 32-byte fetched instruction lines and instructions per
primitive (frames 301–600, including callees):

| Scene | rc4 lines | candidate lines | rc4 instructions | candidate instructions |
|---|---:|---:|---:|---:|
| GT2 | 112.88 | 111.51 | 1,396.3 | 1,368.1 |
| Tekken 3 | 104.36 | 103.96 | 1,029.0 | 1,017.8 |
| SH alley | 117.43 | 116.56 | 1,293.2 | 1,267.5 |
| CTR | 108.81 | 107.64 | 1,268.1 | 1,247.0 |
| R4 | 121.33 | 120.10 | 1,420.0 | 1,390.1 |
| Klonoa | 158.35 | 155.27 | 2,164.0 | 2,078.5 |

Crash 2 hits the baseline simulator assertion in psxbios.c near frame 575 in
repeated runs; exclude it from this comparison. The guest-state gates record
both baseline and candidate exits and their preceding timelines explicitly.
Native trace replay does not have this limitation.

The map supports a small first step, not a 25% renderer-time claim. The common
inside-area path already bypasses clipping, so most primitives cannot benefit
from this particular change. Further work needs to shorten shared decoding,
texture lookup and state/effect handling without duplicating the existing path.

## Validation and layout

The six standard and twelve completed heavy guest-state runs have identical
draw/guest timelines and serialized states; Crash 2 fails identically on both
with the known simulator assertion. Both renderer sanitizer suites and the
frontend-growth/new-library layout proof pass. All 68 HLE cold boots (all
currently available directories) have identical guest timelines and serialized
states. Full VRAM and final draw hashes match rc4 on all 19 native traces × 600
frames (11,400 full-VRAM comparisons). Azahar boots the library and loads
Crash slot 0 through the shared silent headless wrapper. The
rc4-to-candidate layout comparison records 414 C symbols shifted by up to
208 bytes after clip_tris; assembly, library code, constants, tables, strings,
data and bss retain their addresses. This expected core-code movement is a
hardware A/B/A risk; no padding objects are added to hide it.

## Hardware coordination

Do not start until `psx3d-oldqueue-20261022`, `psx3d-x6-v050` and the first
quickwins session (`psx3d-quickwins-old`) finish. Always hold
`devtools/fastloop/lock.old`, re-check `OLD_UNAVAILABLE`, and start only with
rc4 idle in the library. Every test/restore needs a title version higher than
the installed one. Finish on the exact `artifacts-rc4` ELF, library idle, with
the final title version recorded in the report. Do not contact the New 3DS.

## Other offline candidates

Moving texture memo miss/update work to a cold helper reduces the fetched-line
footprint by only 0.4–1.6%, without reducing instructions (Klonoa adds 0.6%).
It is saved under `map-memosplit/`; no correctness or hardware result is claimed.

The known coarse rectangle shortcut already exists behind gldbg bit 33554432.
Enabling it unconditionally avoids the slow tracker call for fully valid bounds
that are already covered. The fine tracker is unchanged. Compared with rc4,
weighted fetched lines are GT2 112.88 → 111.89, Tekken 104.36 → 101.37,
SH alley 117.43 → 117.80, CTR 108.81 → 107.72, R4 121.33 → 120.91,
Klonoa 158.35 → 154.15. Instruction counts barely change: the gain sought is
fewer function lines/calls, not less bookkeeping. The production core C symbols
move by at most 16 bytes; other regions keep their addresses. Guest-state gates
match (same baseline Crash 2 failure); all 19 native full-VRAM/draw traces,
68 HLE boots and both sanitizer suites pass. This candidate is independent of
compact clipping.


Compiling the shared polygon function for size (`optimize("Os")`) makes its
production body 2,868 → 1,816 bytes, but outlining helpers increases the fetched
footprint by 3–4% and instructions by 4–7% in most scenes. GT2 grows from 112.88
to 116.80 lines and Tekken from 104.36 to 108.53. Adding `flatten` keeps helper
calls inline but still grows GT2 to 114.85 lines and Tekken to 107.87. Both
experiments are rejected offline, without a hardware speed claim. A smaller
function body is not sufficient when its callers fetch more code overall.


Combining the texture memo's generation/epoch/key/window equality tests with
bitwise XOR/OR also fails the footprint screen: GT2 112.88 →112.50 lines,
but Tekken104.36 →105.40, R4 121.33 →122.21 and CTR108.81 →109.07. Klonoa
improves158.35 →155.97; instructions barely change. That mixed result does not
justify a separate hardware install (`gpu-memokey.c`, `map-memokey/`).

## Accepted hardware step (2026-10-22)

One compact-clipping source change, always on; no specialized decoder or
hot-function ordering. Old-only A/B/A: rc4 title 5134 → candidate 5135 → rc4 title 5139.
All 102 speed/PMU runs are ok with zero black samples. Full-VRAM dumps at
frame 60 match all 1024×512 pixels including mask bits in all 13 heavy scenes.
No code is accepted from simulator timings.

| Heavy scene | game speed Δ off | game speed Δ Auto |
|---|---:|---:|
| Crash 1 (5) | +0.51% | -0.26% |
| CTR (5) | +0.04% | -0.10% |
| R4 (5) | +0.41% | +0.02% |
| SotN (5) | +0.59% | -0.01% |
| SH garage (5) | +0.19% | +0.11% |
| SH alley (6) | +0.48% | +0.77% |
| SH café (7) | +1.60% | +1.36% |
| GT2 (5) | +1.18% | +1.12% |
| Tekken 3 (5) | +0.54% | +0.52% |
| FFVII (5) | -0.29% | -0.03% |
| Klonoa (5) | +1.68% | -0.00% |
| Crash 2 (5) | +0.69% | -0.11% |
| NFS HS (5) | +0.15% | -0.11% |

Renderer cost per primitive, candidate versus mean rc4 A1/A2 (pmu=0,8,
pmurender=on, prof; PMU samples 1/16 command lists):

| Scene | cycles Δ | I-cache misses Δ | instructions Δ |
|---|---:|---:|---:|
| SH alley (6) | -2.47% | -8.00% | -1.78% |
| GT2 (5) | -3.49% | -11.81% | -1.88% |
| Tekken 3 (5) | -1.17% | -2.86% | -1.66% |
| Klonoa (5) | -5.40% | -10.49% | -4.08% |

The report devtools/renderer-icache/REPORT.md includes all A1/B/A2 game-speed,
renderer per-frame, I-starved and instruction measurements, package hashes and
rejections. Raw evidence is hw/cliploop-1/. PICA wall timers include waits and
preemption; fixed wall-time windows can change primitive mix. Off baseline
spread is ≤0.28% except capped FFVII 0.70% and NFS's loading window 1.02%; Auto
maximum 0.73%. The simulator map omits caller dispatch/frontend and real citro3d;
further structural measurement there remains useful. New hardware checks are
pending before release; this session intentionally contacted only the Old.

Restore 5136 was refused in AM's read/write loop; the healthy candidate kept
running. The SD log contains no AM result; the host client discarded that JSON
field, so the specific cause is unknown. A fresh 5138 recovery initially picked
up the shared checkout's old RomFS because makerom's RSF RootPath is relative
to cwd. Status defaults 0/pack 0 and fallback-defaults logs exposed the mistake;
its invalid A2 was excluded. Coordinator authorized 5139 from the release
worktree: its entire NCCH payload is byte-identical to shipped rc4, and all 2044
RomFS file hashes match candidate 5135/original 5136. Correct defaults 1/pack 3 and
stable fresh A2 confirm recovery. Always verify assets as well as the exact ELF
and ticket/TMD title versions when repacking. No unavailable marker was written.

Accepted source commit: `f61f49e2` (compact clipping only).
