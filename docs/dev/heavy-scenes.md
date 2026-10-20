# Heavy benchmark scenes — report (2026-10-19)

**Status:** scenes chosen and checked for determinism. Import tool ported and in a test build. **New 3DS measured** (off and auto) and restored to `release-v040-final2`.
**Old 3DS pending**: it was still busy with the ftpd game copy. `tools/old-waiter.sh` (log `hw-old.out`) starts the Old session automatically once the copy has finished and PSX3D is idle in the library on final2. That session installs the test build, imports, builds the batches, runs off then auto, and restores final2.

## Slot mapping (same on both consoles)

| Slot | Content |
|---|---|
| 5 | heaviest scene of each game (all 12 games; for Silent Hill: the garage) |
| 6 | Silent Hill alley (start of the game, following Cheryl) |
| 7 | Silent Hill café (the user's New slot 9, byte-identical copy) |

Old slot 2, New slot 0 and SotN slot 8 are untouched (load-only). The New keeps the user's café in slot 9 as well.
The imported states stay on the SD cards, so the baseline build can load them and run the batches.

## Selected scenes (simulator, Old cache model, frames 301–600, no input)

| Game | Selected scene | M cycles/frame | Candidates measured (M cycles) | 600-frame determinism |
|---|---|---:|---|---|
| Crash 1 | `crash_n3ds` (the New benchmark spot) | 3.98 | Old beach o3auto 3.62, attract demo 1 3.07, demo 2 2.86 | pass |
| CTR | attract-demo race (Crash Cove, AI karts) | 4.73 | old bench (stuck kart + Aku Aku) 4.64, demo start 4.16 | pass |
| GT2 Arcade | attract-demo replay race (city, cars) | **6.52** (+14%) | old bench 5.74, demo grid 5.21 | pass |
| R4 | attract-demo race (city / tunnel) | **5.51** (+98%) | old bench 2.79, demo start 3.39 | pass |
| SotN | prologue Dracula fight (unchanged) | 2.78 | only candidate; no cheap heavier scene found | pass |
| Silent Hill | garage (community save, Harry walking) | **5.36** | alley 5.10, café 4.80, alley earlier 4.78, opening street 4.40, hospital 4.25, child room 4.32 | pass |
| Tekken 3 / FFVII / X4 / Klonoa / Crash 2 / NFS | bench-pool accepted scenes | 7.53 / 2.69 / 4.46 / 4.83 / 4.02 / 10.17 | see `../bench-pool/REPORT.md` | pass (bench-pool) |

The racing demos keep racing for at least 35 s (GT2, R4); CTR's demo ends about 30 s after the state, so its window is short (3 s warmup + 12 s).
The states, SHA256s and determinism JSON are in `selected/<key>/`. Scenes and navigation scripts are in `scenes/`, models in `model/`.

## New 3DS results (heavy-scenes-2 / final2, GPU, `gputhread`, 3 s warmup + 12 s; NFS 1 + 5 s)

From `hw/new-gt/summary.md`. top = distinct pictures shown per second. The GPU-thread-off run (`hw/new/summary.md`) gives the same numbers.

| Scene | off speed % | off fps / top | auto speed % | auto top | emu ms | GPU ms | tex decodes/s | Dominant cost |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| Crash 1 | 100 | 60 / 25 | 100 | 25 | 5.1 | 3.5 | 26 | CPU emulation (light) |
| CTR demo | 100 | 60 / 28 | 100 | 28 | 8.7 | 0.3 | 203 | CPU emulation |
| GT2 demo | 100 | 60 / 25 | 100 | 25 | 10.6 | 1.9 | 268 | CPU emulation |
| **R4 demo** | **88.5** | 53 / 27 | 99.2 | 27 | 12.2 | 2.0 | **5325** | CPU: texture re-decode (1221 evictions/s) |
| SotN fight | 100 | 60 / 61 | 100 | 61 | 4.7 | 1.0 | 247 | CPU (light) |
| **Tekken 3** | **96.4** | 58 / 58 | 100 | 55 | 12.0 | 1.5 | **5531** | CPU: texture re-decode + 507 syncs/s |
| FFVII battle | 100 | 60 / 60 | 100 | **15** | 4.1 | 3.5 | 99 | light; auto over-skips (15 pictures/s at 100% speed) |
| Mega Man X4 | — | INVALID | — | — | 1.9 | — | 0 | hardware run shows the **weapon menu**, not Sky Lagoon (see the open issues) |
| Klonoa | 100 | 60 / 61 | 100 | 61 | 6.8 | 4.6 | 160 | CPU emulation |
| Crash 2 | 100 | 60 / 30 | 100 | 30 | 10.2 | 3.5 | 12 | CPU emulation |
| **NFS HS** | **48.1** | 29 / 23 | **47.7** | 15 | **32.8** | 3.4 | **13344** | CPU: texture re-decode thrash (3732 evictions/s), 15 audio underruns/s |
| SH garage | 100 | 60 / 24 | 100 | 24 | 6.0 | 2.6 | 0 | CPU emulation |
| SH alley | 100 | 60 / 30 | 100 | 30 | 6.5 | 4.1 | 0 | CPU emulation |
| SH café | 100 | 60 / 22 | 100 | 22 | 7.2 | 3.5 | 0.7 | CPU emulation (see below) |

GPU drawing never dominates on the New (≤ 5.6 ms of 16.7). Every slow scene is CPU-side texture-cache thrash in gpu_pica (decodes and evictions). The base "top" of 22–30 for most 3D games is the game's own 30/20 fps, not a loss.

**New 3DS scenes below 100% / native FPS:** NFS HS 48%, R4 demo 88.5% (off; auto 99%), Tekken 3 96.4% (off). FFVII auto shows only 15 pictures/s although the game runs at 100% (an Auto-frameskip over-skip case).

![New 3DS heavy scenes](../../devtools/heavy-scenes/new-heavy-sheet.png)

### Silent Hill café: the slowdown does not reproduce from a clean load
The user's live café stats (`new-sh-live/`): 37–40 fps, 62–67% speed, emu 22 ms, about **8,400 texture decodes/s** and 3,000 evictions/s.
The same state (slot 9, and the byte-identical copy in slot 7), loaded fresh, runs at 100% with under 1 decode/s. I tested it with the GPU thread forced on, rtt=new, no PMU, a 45 s window and slot 9 directly (`hw/new-gt/bench-cafeprobe`).
So the thrash depends on renderer state carried over from the earlier session, not on the scene itself. Likely candidates: the texture cache / RTT retained regions not being reset when a state is loaded on top of a running game, or memory fragmentation after a long session (live free_linear 7.6 MB).
→ Next step (sh-texcache): reproduce by playing into the café, or by loading slot 9 *while SH is already running*, rather than by a clean load.

## Batch files (for fastbench and tools/perfchart)
`batches/heavy-off.txt`, `heavy-auto.txt` (Old, generic), `heavy-new-off.txt`, `heavy-new-auto.txt` (New, `gputhread`), `heavy-cafeprobe.txt`.
The Old session writes `heavy-old-{off,auto}.txt`, filtered to the titles that imported (games missing from the Old library are left out).
The state numbers are literal (5/6/7), so no `{state}` substitution is needed.

## Test build
`artifacts-02/`: identity `heavy-scenes-2`, counter 4099, CIA SHA256 `5ea19252…d1d3`, source t3/heavy-scenes `bf40948`+identity commit.
Contents: main 7fbc386 + state-import (bench-pool 4180ddf/fba13c6/1765336, re-applied) changed to slots 5–7 for any game + the v041-fixes remote file API (ddaa7b1, for get-state).
Tools: `tools/session.py` (console session holding the lock), `tools/summary.py`, `tools/determinism.py`, `tools/old-waiter.sh`.

## Open issues
- **Mega Man X4** opens its weapon menu on hardware, although the simulator shows gameplay. Re-check by loading slot 5 manually, or pick another X4 scene.
- **Silent Hill café**: the thrash is session-dependent (above). The garage stays the SH standard on CPU cost; the café is kept as the user's reference.
- Only one run per scene (no repeats). Treat differences under ~2% as noise.
- SotN has only its existing fight scene. A heavier castle scene would need manual navigation.
- Old 3DS: everything is pending the waiter. If the library hasn't been rescanned, the new bench-pool games are missing and are skipped.
- The `pause_viewers` race in fastbench crashed one start (a process vanished during the /proc scan); a rerun worked.
