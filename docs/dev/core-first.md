# Core-first link layout

The Old 3DS has 16 KB 4-way instruction and data caches and no L2. The cache set
of an address is its offset within a 4 KiB page (address mod 0x1000), and
heavy scenes touch far more code than fits. Moving hot code by a few hundred
bytes changes which functions evict each other: speed swings of 1–5% from
layout alone were measured many times (v0.4.0 final Crash −5%, dynarec round 4
Crash 2 −1.7%, gpu-stereo3 Crash 2 −1%).

Up to v0.4.1 the frontend (`psx3d/source`) linked in front of the core. A
padding object (`zz_core_align.s`) put the core's C code on a fixed page
offset, but only as long as the frontend stayed inside the padding: by the
presets round only 212 bytes were left, and an 8,000-byte dummy table in
`presets.c` held the core's constants in place. Every frontend branch then
risked moving the whole core by 4 KiB.

## Layout

`psx3d/core-first.ld` (based on devkitARM's `3dsx.ld`; selected by
`psx3d/core-first.specs`) puts the core (`pcsx_rearmed_libretro_ctr.a`:
dynarec with its assembly, GTE, PS1 memory and hardware, gpu_pica + gpulib,
dfsound, libretro glue) first in every section, then the libraries, then the
frontend:

| Section | Order | Core start |
|---|---|---|
| `.text` | crt0, core assembly, core C, library assembly, library C, frontend | 0x1000b0 (assembly), 0x1034f0 (C) |
| `.rodata` | core constants, library constants, core tables, library tables, core strings; then a page with the frontend's constants and all other strings | page offsets 0xdf8, 0x45c, 0x400 |
| `.data` | core, libraries, frontend (from a page start) | page offset 0x1c0 |
| `.bss` | core (`dynarec_local`, `psxRegs` first), library plain bss, core, library, frontend | page offsets 0x4a0, 0x058; frontend 0x658 |

Each block starts at a fixed offset within a page (`. = . + ((OFFSET - .) &
0xfff)`, at most 4095 bytes of padding; inside an output section `.` counts
from the section's start, so every section that pins starts on a page). The
offsets are those of v0.4.1 RC4, so every core and library code, constant,
data and bss symbol keeps RC4's cache set (`compare.py` against the RC4 ELF).
Only the core's strings (cold), the library strings and the frontend sit at
different offsets. The linker script ASSERTs the code addresses and the data
page offsets.

- **Code**: core and library code have fixed addresses; frontend growth only
  moves frontend code.
- **Constants, data, bss**: a segment starts on the page after the previous
  one, so frontend growth can move the core's data by whole pages. Its page
  offsets (cache sets) and internal layout never change.
- **Strings**: the linker merges mergeable string sections per output section.
  The core's strings get their own output section, otherwise frontend strings
  joining the shared pool would move the core's.
- **Libraries**: linking a library function the app did not use before
  (e.g. the frontend calls a new libm function) adds that library member and
  moves the library code after it, and possibly the next core data block by a
  whole page. The core's code does not move.

## What still moves code

- Changes in `core/` move the core's later code and need an Old 3DS A/B/A.
  `NDRC_LAYOUT_PAD` in `new_dynarec.c` stays: it pads the dynarec object so the
  code after it keeps its cache sets. Recompute it whenever `new_dynarec.c`'s
  code size changes (4096 minus the growth, see `docs/dev/dynarec-r4.md`).
- Frontend changes move later frontend code. The frontend has hot code too
  (`--wrap` of citro3d functions in `gpu_cache.c`, `renderqueue.c`, audio,
  video, input), so frontend changes to hot paths can still cost or gain up to
  about 1% on the Old 3DS. The first core-first build showed it: `presets.o`
  built `-O2` instead of `-Os` shifted the frontend code after it by 432 bytes,
  and Crash 2 lost 1.6%. `presets.o` keeps `-Os` so the frontend maps as it
  did in the measured presets build.

## Checks

```sh
source devtools/env.sh && make
bash tools/layout/check.sh                       # ~40 s
python3 tools/layout/compare.py old.elf new.elf  # any two builds
```

`check.sh` builds the frontend three times in a temporary copy against the
built core: as is, with ~6 KB more code + 6 KB constants + 2 KB data + 4 KB bss,
and the same plus a new libm function. `compare.py` requires every core (and,
without `--allow-lib-moves`, library) code symbol at the same address, each
core constant/data/bss block shifted by whole pages only, and every core code
word identical except calls and pointers to symbols that moved. Against a build
from before this layout (e.g. RC4) it reports how far each region moved and how
many symbols kept their cache set. A negative control (frontend linked in front
of the core, same grown frontend) fails: the core moves +4 KiB.

## Integrating a branch

1. Rebase or merge as usual. There is no padding to recompute and no frontend
   slack to watch.
2. `make`, then `bash tools/layout/check.sh` (the layout itself still holds).
3. `python3 tools/layout/compare.py <previous release ELF> psx3d/PSX3D.elf`:
   without `core/` changes every core symbol keeps its address and cache set.
4. Old 3DS A/B/A (heavy scenes, off + Auto) against the previous release for
   core changes (recompute `NDRC_LAYOUT_PAD` if `new_dynarec.c` changed) and
   for frontend changes to hot paths.
5. Do not reintroduce padding objects or dummy tables in the frontend.

## Hardware (Old 3DS, 2026-10-20)

13 heavy scenes, frameskip off and Auto, game speed %, A/B/A RC4 → candidate →
RC4 on the same session conditions (low NTR stream as left by other rounds).
Candidate vs the mean of the two RC4 runs:

| Scene | core-first-1 off | core-first-2 off | core-first-2 Auto |
|---|---:|---:|---:|
| Crash 1 | −0.68% | −0.10% | −0.15% |
| CTR | +0.02% | −0.02% | +0.36% |
| R4 | −0.11% | −0.69% | −0.11% |
| SotN | −0.36% | −0.61% | −0.02% |
| SH garage | −0.17% | −0.21% | −0.47% |
| SH alley | −0.40% | +0.29% | +0.12% |
| SH café | −0.68% | −0.09% | −0.10% |
| GT2 | +0.15% | −0.26% | −0.39% |
| Tekken 3 | −0.68% | −0.44% | −0.41% |
| FFVII | +0.01% | +0.00% | +0.01% |
| Klonoa | −0.68% | −0.37% | +0.51% |
| Crash 2 | −1.59% | −0.26% | +0.16% |
| NFS HS (5 s) | −1.21% | +0.06% | +0.21% |

core-first-1 (library constants after the core strings, `presets.o -O2`) lost
up to 1.6%; core-first-2 (this layout) is within ±0.7% everywhere, RC4 brackets
drifting ≤0.4%. CPU counters for core-first-2: instruction cache misses follow
the presets build's pattern; data-hazard stalls are still +0.4–1.6% in every
scene, a small data-side residue with no identified cause (core and library
data keep RC4's cache sets).
