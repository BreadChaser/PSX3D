# Renderer instruction-cache work

The target is 25% less renderer CPU time on the Old 3DS, with identical VRAM
and pictures. No change is accepted from simulator speed alone. This round
starts at `0e1ca70d` (v0.5.0 rc4), branch `t3/renderer-icache`.

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

## Experiments prepared offline

These are candidates, not accepted speed improvements. Each production build and
source snapshot is retained in the ignored evidence directory.

| Experiment | ARM production size | Measured instruction footprint |
|---|---|---|
| Command-word strides in the shared polygon decoder | polygon 2,868 → 2,852 B | less than 1% fewer fetched lines |
| Reuse decoded vertices for packed words, with strides | polygon 2,868 → 2,784 B | about 1–2% fewer fetched lines |
| Fuse the finishing loops in emit_tris, with the above | emit_tris 2,060 → 2,048 B | not yet mapped or gated |
| Load CLUT/page outside the vertex loop, with strides/in-place packing | polygon size in artifacts-packet | instructions fall ~2–3%; Tekken fetched lines grow 0.6%, set aside |
| Compact clipping, independently of the above | clip_tris 1,416 → 1,204 B | table below |

The current candidate is compact clipping only. Alternate two scratch buffers
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
frontend-growth/new-library layout proof pass. Full 19-trace VRAM/draw hashes
and all 68 HLE cold boots (all currently available directories) have identical
timelines and serialized states. Full VRAM and final draw hashes match rc4 on all 19 native traces × 600
frames (11,400 full-VRAM comparisons). Azahar boots the library and loads
Crash slot 0 through the shared silent headless wrapper. The
rc4-to-candidate layout comparison records 414 C symbols shifted by up to
208 bytes after clip_tris; assembly, library code, constants, tables, strings,
data and bss retain their addresses. This expected core-code movement is a
hardware A/B/A risk; no padding objects are added to hide it.

## Hardware coordination

Do not start until `psx3d-oldqueue-20261022` finishes. Always hold
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
match (same baseline Crash 2 failure); full VRAM, boots and sanitizers are still
in progress. This candidate is independent of compact clipping.
