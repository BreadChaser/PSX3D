# Old 3DS quick wins (v0.5.1)

Three small changes from the Old 3DS time breakdown (`old-speed` plan, items
1–3), always on, no switches, plus better heavy benchmark scenes for Crash 1
and NFS. Old 3DS A/B/A (rc4 → quickwins-1 → rc4, 13 heavy scenes, frameskip off and
Auto): Crash 2 +9.8% off / +6.1% Auto, SH café +3.0% / +4.7%, GT2 +3.2% / +3.2%,
Crash 1 +1.7%, R4 +1.5% / +1.6%, the others within ±0.4%; core audio checksums
identical. Full table in the worker report (`devtools/quickwins/REPORT.md`).

## What changed

1. **Pacer keeps three frames of lateness** (`psx3d/source/emu.c`,
   `PACE_DEBT`). When a frame ends more than the catch-up window behind, the
   deadline used to be reset to *now*, forgiving all lateness. Scenes that
   alternate heavy and light frames below full speed (R4, Silent Hill,
   Crash 2) then slept on the light frames although the game was still
   behind. The reset now keeps three frames of debt. After a slow stretch the
   game catches up at most 50 ms; nothing runs faster than the PS1 in a scene
   that is slow throughout. Guest results are unchanged (only when frames are
   shown).

2. **Twice the GPU vertex buffer** (`core/plugins/gpu_pica/gpu_pica.c`).
   The buffer is allocated last in `pica_game_begin`, at 2 x 16,384 vertices
   when at least 3 MB of linear memory stays free afterwards (Real 3D's right
   eye and the pause snapshot need about that much), else at the old size; a
   failed large allocation falls back to the old size. Each frame's half now
   holds 16,384 vertices, so Crash 2 and GT2 stop filling it and waiting for
   the GPU (`SY_VERTICES` syncs). Same draws in the same order. The game start
   log line reports the vertex count (`pica_vertices`).

3. **Silent-voice reverb on the sound thread** (`core/plugins/dfsound/spu.c`).
   With every voice silent, only the reverb tail is left to mix, and that was
   always done on the main core (Silent Hill café). Such items now go to the
   sound thread like any other, unless CD audio feeds the reverb
   (`CTRL_CDREVERB`): the thread adds CD after the reverb, so that case stays
   on the main core. Audio output is the same: the simulator with the threaded
   mixer gives byte-identical core audio for SH café/alley/garage, CTR, NFS and
   Crash 2, and on the Old the `acap` checksums of SH café/alley/garage,
   Crash 2 and CTR match rc4 exactly. SH café main-core sound time
   0.87 → 0.16 ms per frame.

## Benchmark scenes

The 13-scene heavy batches are unchanged. Two scenes measured a load:

- **Crash 1 slot 5** starts on the "LOADING N. Sanity Beach" screen and the
  level intro; gameplay starts ~720 frames in. New **slot 6** is the same spot
  840 frames later (steady gameplay from the first frame).
- **NFS slot 5** is measured while the texture cache refills after the load,
  and with no input the car hits a cliff after ~600 frames. New **slot 6** is
  the same race driven through the town (scripted input in the simulator),
  where the car rolls to a stop in the street with no input; the batch uses a
  10 s warmup so the cache is full again before measuring. On the Old rc4 runs
  it at 50.7% (slot 5: 35.8%, mostly the refill). The car stands still, so it
  is a steady town scene, not a racing load.

Both new states were made with `bench-pool/runner-scene` (HLE BIOS) and are
deterministic over 600/900 frames. Batches: `devtools/quickwins/batches/`.

## Layout

Core code moved slightly: `do_samples` +8 B (everything after it +8 B),
`pica_frame_started_local` +12 B (hot draw code after it +20 B),
`pica_game_begin` +88 B. `tools/layout/check.sh` passes. Judge on the Old
(A/B/A in the report).
