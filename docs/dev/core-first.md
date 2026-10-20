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
`psx3d/core-first.specs`) puts the core
(`pcsx_rearmed_libretro_ctr.a`: dynarec with its assembly, GTE, PS1 memory and
hardware, gpu_pica + gpulib, dfsound, libretro glue) first in every section,
then the libraries, then the frontend:

| Section | Order | Core start |
|---|---|---|
| `.text` | crt0, core assembly, core C, library assembly, library C, frontend | 0x1000b0 (assembly), 0x1034f0 (C) |
| `.rodata` | core constants, core strings, library constants, frontend constants and other strings | page offset 0xdf8 / 0x45c |
| `.data` | core, libraries, frontend | page offset 0x1c0 |
| `.bss` | core (`dynarec_local`, `psxRegs` first), libraries, frontend | page offset 0x4a0 / 0x058 |

Each block starts at a fixed offset within a page (`. = . + ((OFFSET - .) &
0xfff)`, at most 4095 bytes of padding). The offsets are those of v0.4.1 RC4,
so the core and the libraries keep RC4's cache sets; the frontend starts at its
RC4 page offset too. The linker script ASSERTs the code addresses and the data
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
  moves the library code after it. The core does not move.

## What still moves the core

Changes in `core/` itself move the core's later code and need an Old 3DS A/B/A.
`NDRC_LAYOUT_PAD` in `new_dynarec.c` stays: it pads the dynarec object so that
the code after it keeps its cache sets. Recompute it whenever `new_dynarec.c`'s
code size changes (4096 minus the growth, see `docs/dev/dynarec-r4.md`).

Frontend wrappers that the core calls on hot paths (`--wrap` of citro3d
functions, GPU submission in `renderqueue.c`) are frontend code and move with
frontend changes, as before.

## Checks

```sh
source devtools/env.sh && make
bash tools/layout/check.sh                       # ~40 s
python3 tools/layout/compare.py old.elf new.elf  # any two builds
```

`check.sh` builds the frontend three times in a temporary copy against the
built core: as is, with ~6 KB more code + 6 KB constants + 2 KB data + 4 KB bss,
and the same plus a new libm function. `compare.py` requires every core (and,
without `--allow-lib-moves`, library) code symbol at the same address, the core's
constants/data/bss shifted by whole pages only, and every core code word
identical except calls and pointers to symbols that moved. Against a build from
before this layout (e.g. RC4) it reports how far each region moved and how many
symbols kept their cache set.

## Integrating a branch

1. Rebase or merge as usual. Frontend-only branches need no layout work: no
   padding to recompute, no slack to watch.
2. `make`, then `bash tools/layout/check.sh` (the layout itself still holds).
3. `python3 tools/layout/compare.py <previous release ELF> psx3d/PSX3D.elf`:
   if `core/` did not change, every core symbol keeps its address and cache set.
   If it did, Old 3DS A/B/A (heavy scenes, off + Auto) against the previous
   release, as for any core change; recompute `NDRC_LAYOUT_PAD` if
   `new_dynarec.c` changed.
4. Do not reintroduce padding objects or dummy tables in the frontend.

## Hardware

Measured in `devtools/core-first/` (ignored); summary in the round report.
