# Texture atlas thrash (Silent Hill café, NFS, R4, Tekken 3)

Status 2026-10-19: fixed in `t3/sh-texcache`; New 3DS measured; Old 3DS A/B/A pending.

## Symptom

New 3DS, v0.4.0, GPU renderer: Silent Hill in the café (and the alley) ran at
15 pictures/s and ~64% speed in the user's play session. The renderer CPU did about
8,800 tile decodes, 3,100 atlas evictions and 800 GPU syncs per second, all syncs
caused by eviction. The same café save state loaded fresh ran at 100% with almost no
decoding. Fresh states of other games showed the same signature: Need for Speed High
Stakes 38.6% speed (10,900 decodes/s), R4 4,000/s and Tekken 3 4,400/s with 420 syncs/s.

## Root cause

All textures live in one 1024×1024 RGBA8 atlas of 32×32 cells. Each texture
(page, CLUT, depth, window) got a power-of-two square (32, 64, 128 or 256 texels)
covering the drawn texture coordinates. When no aligned free block was left,
`take_square()` evicted the least recently used square **of the same size**, and
only fell back to clearing smaller squares when there was no square of that size at all.

1. **Frozen size classes (session degradation).** Once the atlas was full, every
   eviction replaced a square with one of the same size, so each size's share of the
   atlas never changed again. Silent Hill's boot (Konami logo, intro) leaves 12–15
   full 256×256 pages in the atlas within ~2,000 frames, 960 of the 1,024 cells, and
   nothing ever uses them again. Later 3D scenes need many 64- and 128-texel squares,
   and they thrash among the 18 or so slots of their sizes: every frame re-decodes
   its textures, and evicting a square that the current frame still reads forces a
   full GPU sync. The stale pages are never chosen. Loading a benchmark state skips
   the boot, which is why the café ran fine when loaded fresh, and why the user's play
   session degraded. Simulator: after 13,000 frames of SH boot, FF7 and CTR traffic,
   the café re-decodes ~205 tiles, evicts 73 squares and syncs 13 times per frame,
   indefinitely (`atlas cells 1024 … entries 11 5 2 15`).
2. **Rounding waste (fresh thrash).** A request spanning 3×2 tiles got a 4×4 square.
   R4's atlas held ~1,000 cells for ~420 decoded tiles, NFS ~1,000 for ~650, so busy
   scenes overflowed the atlas even fresh. (In Tekken's case 1. also applied.)

## Fix (commit 82af2ef)

- Entries take exactly the cells of the requested tile rectangle (1–8 cells each
  way) instead of a power-of-two square. The texture-to-atlas mapping was already a
  pure translation, so only the size bookkeeping changes (`cw`, `ch`).
- Free space is found with a per-row occupancy bitmap (large rectangles from the
  bottom, small ones from the top, so large free areas stay together).
- When nothing is free, the window whose newest occupant is oldest is reused,
  across all sizes, preferring windows that no queued draw reads. A sync is only
  needed when every candidate is in use by the frame being drawn.
- Correctness is unchanged: entries are still validated by the same VRAM/CLUT
  generations, evicted entries are dropped with the same texmemo epoch bump, and an
  in-flight victim still waits for the GPU before its cells are reused.
- `texevict=old` restores the release allocator (latched at game start), for A/B.
- The stats line now reports occupancy: `atlas cells N (decoded) recent N (decoded)
  entries <by size> win N` (`recent` = cells of entries looked up since the previous
  frame started).

Cost: `.text` +1.7 KB in total, mostly the cold allocator (`take_rect` 1,036 B,
`pica_stats_local` +244 B); the hot `texture_for_impl` grows 560→596 B. BSS +8.3 KB
(row maxima for the window search, row bitmaps). No extra GPU memory.

## Simulator (native trace replay, gpu_pica.c with the fake GPU)

Decodes / evictions / syncs per frame, `texevict=old` → `new`:

| Trace | old | new |
|---|---|---|
| SH café after a 13,000-frame session (frames 13,401–14,028) | 205 / 73 / 13 | 0.0 / 0 / 0 |
| NFS frames 1–300 | 261 / 75 / 0.34 | 37 / 7.2 / 0.10 |
| NFS frames 301–600 | 0.16 / 0.05 / 0 | 0.10 / 0.04 / 0 |
| Tekken 3 (RTT auto) frames 301–600 | 86 / 10 / 9.0 | 2.1 / 0 / 0.04 |
| R4 frames 301–600 | 69 / 17 / 0.25 | 1.1 / 0.3 / 0.0 |

## Gates

- Identity: 11 traces (Crash o3auto, SH, R4, CTR, GT2, SotN, plus NFS, Tekken,
  R4 attract, SH café, SH alley), 600 frames each, RTT auto and forced new, the
  baseline replayer (7dbf384) against the candidate (`devtools/sh-texcache/gate2.txt`):
  - **every frame's full-VRAM hash** (all 1024×512 pixels incl. mask bits) is identical
    with `texevict=new`;
  - `texevict=old` reproduces the baseline exactly, draw hash included;
  - the long 14,028-frame session (SH boot, FF7, CTR, café) has identical VRAM on
    every frame too.
- The `PICA_SIM_HASH` draw hash itself differs in 18 of the 22 cases, and this is
  expected: it is placement-dependent. It hashes vertices in emission order, which
  shifts when decode rebinds or eviction syncs end batches at other points. It also
  records the entry found at each vertex's atlas coordinate; a vertex on an entry's
  exclusive edge falls into the neighbouring cell, whose owner depends on placement.
  Compared as multisets of the hashed records, the vertices are identical except for
  records on a 32-texel cell edge (`gate2-vertices.txt`,
  `tools/vertex_multiset.py`). The GPU renders the same texels: VRAM is identical.
- Guarded fixtures pass: RTT palette/regions, texmemo
  eviction/growth/collision/reset, mask/STP fixtures (the documented
  `known-mask-half-blend` failure is unchanged), batching ordering vs baseline.
- Host suites: `psx3d/tests/dev`, `tools/gpu-compare/regress.sh` (ASan/UBSan),
  `tests/renderer_config.sh` pass.

## New 3DS hardware (2026-10-19, same CIA, `texevict=old` → `new`, frameskip Auto)

| Scene | speed % | pictures shown/s | decodes/s | syncs/s | emu ms |
|---|---|---|---|---|---|
| NFS slot 5, first 6 s | 38.7 → **55.4** | 12.0 → 16.8 | 10,932 → 4,298 | 15.6 → 8.6 | 128 → 118 |
| NFS slot 5, 25 s | 83.9 → **91.2** | 27.2 → **37.7** | 7,909 → 924 | 34.7 → 23.6 | 37.4 → 30.1 |
| R4 slot 5 | 99.3 → 100 | 27.4 → **30.3** | 3,993 → 179 | 15.6 → 0 | 9.1 → 5.1 |
| Tekken 3 slot 5 | 100 | 55.8 → **60.5** | 4,449 → 99 | 420 → 1.3 | 11.4 → 6.7 |
| SH café after cold boot (slot 7) | 100 | 22.0 → 22.1 | 791 → 0.4 | 66 → 0 | 9.85 → 7.11 |
| SH café fresh / alley fresh | 100 | unchanged | ~0 | 0 | unchanged |

The release build (final2) measured the same as `texevict=old` in every scene.
The cold boot reproduces the degradation on the console: 13 stale 256×256 pages,
only 6 slots for the café's 128-texel squares. The New absorbs that mild case at
100%; the user's longer play session had piled up much more (8,800 decodes/s, 64%).
NFS's remaining slowness is mostly CPU emulation; its race start still overflows
the atlas briefly (working set ~800 of 1,024 cells, few duplicates).

## Old 3DS

Pending (console busy with the user's ftpd game copy). Plan, batches and driver in
the ignored `devtools/sh-texcache/` (PENDING-HARDWARE.md).

## Follow-ups

- More atlas capacity would remove NFS's race-start overflow. A second atlas page
  is possible only on the New (~6.6 MB free in game; the Old has ~2.3 MB).
- R4 keeps 300–600 duplicated cells from texture-window variants of the same page;
  a window-aware key could share them.
- Once the Old A/B/A passes: the allocator is a net win on both consoles, so delete
  `texevict` and the old path (`take_square`, `square_for`, `active[]`).
