# Renderer CPU work removal

> Historical report: submodule/patch instructions apply to its original commit.
> Current builds use the committed `core/` directory; see [vendor-core.md](vendor-core.md).

Rebased onto `gpu-renderer` commit `9b4dc04`, retaining only the renderer CPU
change from combined stage `166af68` (original `9e8f279`). Patch 0002 was
regenerated from the merged real core source with `devtools/mk.sh`; the native
3DSX/CIA build passed. Nothing was pushed.

Old 3DS hardware verification, slot 2, normal clock, frameskip off, last 15
one-second samples with NTR streaming:

| Game | Plain 9b4dc04 FPS | This change FPS | M cycles/frame | Emu ms |
|---|---:|---:|---:|---:|
| Crash Bandicoot | 40.63 | 40.60 | 5.768 | 21.84 |
| Crash Team Racing | 25.53 | 26.23 | 8.304 | 34.90 |
| Gran Turismo 2 | 28.83 | 28.77 | 8.376 | 31.61 |

A same-binary CTR control with `gldbg=131072` measured 25.32 FPS / 8.604M
cycles / 36.23 ms; enabled runs before and after it both measured 26.23 FPS
(8.304M / 34.90 ms and 8.302M / 34.94 ms). The +3.61% same-binary gain exceeds
run noise without relying on between-build code layout. Crash and GT2 remain
within noise of baseline. Screenshots show no new corruption; existing Crash
edge geometry defects remain. This is not full GPU renderer correctness approval.

Artifacts/logs/screenshots and the final plain-baseline recheck are recorded in
`devtools/combined_results.md` and `devtools/combined/`. The CIA SHA256 is
`4d7dc218ac138bbaf524c6c9ae9414da465d3fc44b2e957a31d18ac1c0ee8f70`.

The simulator measurements below were originally collected against `30c62a9`;
they describe work removed, not a prediction of Old 3DS cache/layout speed.

## Changes

- Validate only the cached texture square's VRAM blocks when there is no
  texture window. Previously a write anywhere on the 256-row page could
  invalidate a 32-row square, and validity checks scanned all 16 block rows.
  Windowed textures still check the entire page; CLUT checks are unchanged.
  Atlas growth drops the old entry, so the narrower check cannot preserve
  stale tiles when a square expands. Existing block stamping handles wrapping.
- Construct the decode request's tile mask once per row, rather than shifting
  a 64-bit bit for every requested tile. The column mask is identical on every
  row. This also removes work on cache hits that need no decoding.
- Convert staging uploads two pixels at a time with packed 32-bit arithmetic,
  and store each adjacent Morton pair with one 32-bit store. Compute the
  vertical Morton component once per row. `memcpy` accepts a source aligned
  only to 16 bits; an odd final pixel retains the scalar path. The source and
  destination formats, row inversion, cache flushes and submission are unchanged.
- Materialize deferred GP0 fills with 32-bit stores. GP0 fill coordinates and
  widths are multiples of 16 pixels. Split horizontal wrapping once per fill,
  removing the per-pixel wrap calculation and halving the store/loop count.

## Investigation and rejected changes

`lineprof.py` and `hotblk.py` were run on the requested functions. The supplied
Crash hotspot list was not reproduced by this checkout's standard 300..600
host-frame window: `texture_for` starts at about 114k instructions/frame, and
`push_to_target`, `do_vram_io`, sprites, width and height updates have no
steady-state work in that Crash window. The polygon path is inlined into
`renderer_do_cmd_list`. Startup uploads are measured separately below.

- The texture key already masks page blend/flip bits, ignores the CLUT at
  16-bit depth and ignores inactive texture windows. Its last-result cache
  misses on alternating textures; that alone does not mean texture redecoding.
  Baseline Crash has about 779 lookups/frame and 383 bucket-search entries/frame.
  No key redesign or additional lookup cache was introduced.
- Stage halfword stores do not allocate cache lines on the ARM11 model. Many
  store misses are consequently writes through to memory, rather than evidence
  that the stage needs a different traversal. Pair stores remove half of the
  pixel stores directly; no cache warming or reordered tile traversal was added.
- `update_height`/`update_width` already run on GP1 display-register writes and
  frontend centering changes, not per primitive. Identical GP1 writes are
  already suppressed. No state-update migration was justified.
- `do_vram_io` already uses row `memcpy` through `do_vram_line`, with a separate
  masked-copy path and stereo mirror support. There is no pixel conversion to
  optimize in the normal path. Its row bookkeeping and synchronization remain.
- Sprite clipping/quad emission, fill draw-state setup, and polygon code layout
  were left alone. No specialized primitive implementation, function ordering,
  or other layout rewrite was attempted.

## Method

The simulator was copied to `/tmp/renderer-sim`. Its `build_pica.sh` copies
`gpu_pica/*.[ch]`, `gpulib/gpu.c`, and `gpulib/vout_pl.c` from this worktree.
Only the copied scripts were edited; QEMU paths were made absolute because
`../dl` does not exist beside the temporary copy. The tool environment is
`/home/tony/Development/PS1_3ds/devtools/env.sh`.

Non-HASH `-O2` binaries were retained as `runner_before` and `runner_final`.
Each game uses its matching cue/state, `o3hw_off.ini`, and `SMALL=1`:

```sh
GAME='<matching game cue>' ./cbench.sh before_<state> runner_before states/<state>.st0 o3hw_off.ini SMALL=1
GAME='<matching game cue>' ./cbench.sh final_<state> runner_final states/<state>.st0 o3hw_off.ini SMALL=1
```

`cprof` subtracts counters at frame 300 from frame 600 and divides by 300.
Its cycle model is instructions + 70*I-misses + 70*read-misses + 4*store-misses.
These are total core/runner instructions and modelled cycles per host frame,
not renderer-only cycles or hardware measurements. Drawn frames are roughly
half the host frames in these fixtures. Raw counters and reports are retained
in `/tmp/renderer-sim/cs_{before,final}_<state>_{300,600}.txt` and
`{before,final}_<state>.prof`.

## Validation

`HASH=-DPICA_SIM_HASH ./build_pica.sh`, followed by
`HENV=SMALL=1 ./hashall.sh runner_hash_final`, ran all five states for 600 frames.
Sorted baseline/final output compares equal:

| State | Baseline and final draw hash |
|---|---|
| o3auto (Crash) | `6ba7ef2f6e06cf04` |
| sh | `244abb4a61d295c3` |
| r4 | `6e1a6f9b64542f09` |
| ctr | `497b85e24ef6ff72` |
| gt2 | `7b3a81441ad9b8aa` |

The simulator draw hash is the existing command/vertex/texture-sample check,
not a real PICA raster-output comparison. Additional direct checks covered
all 65,536 pixel values in four pairings (262,144 packed conversion cases),
all Morton positions for widths 256/512/1024 and 512 rows, all 1,296 tile
rectangles, and all 4,096 aligned fill ranges including horizontal wrapping.
All passed. These checks are retained in the private simulator copy.

`source /home/tony/Development/PS1_3ds/devtools/env.sh && make -j12` passes and
builds `psx3d/PSX3D.3dsx`; it was not deployed. The regenerated patch passes
`git -C core apply --reverse --check <absolute-path-to-patch-0002>` and the
outer diff passes `git diff --check`.

## Startup upload profile

A separate cache-model run loads the Crash state and runs one host frame.
This exercises the full VRAM staging upload; it is not a steady-state
per-frame result. `cprof` attributes the following work to `push_to_target`:

- Instructions: 12.081M -> 4.742M (about 61% fewer).
- Store misses: 524.3k -> 262.2k (about 50% fewer).
- Modelled time at 268 MHz: 61.52 -> 30.25 ms.

The profiles are `/tmp/renderer-sim/upload_{before,final}.prof`. The copies
of `lineprof` and `hotblk` results are `lines_*.txt` and `blocks_*.txt`.

## Steady-state results

Millions per host frame, using the same binaries, options and 300..600 window
for both versions. Percentages are reductions (positive is less work).

| State | Instructions before → after (M) | Reduction | Model cycles before → after (M) | Reduction |
|---|---:|---:|---:|---:|
| o3auto | 2.29857 → 2.28672 | 0.52% | 3.56010 → 3.53348 | 0.75% |
| sh | 2.05763 → 2.05343 | 0.20% | 4.26171 → 4.24670 | 0.35% |
| r4 | 2.78310 → 2.74679 | 1.30% | 4.02068 → 3.95648 | 1.60% |
| ctr | 2.89652 → 2.74156 | 5.35% | 5.15230 → 4.97581 | 3.43% |
| gt2 | 3.18318 → 3.17474 | 0.27% | 5.58222 → 5.52122 | 1.09% |

CTR's materialized deferred fills fall from 185k to 65k instructions/frame
and from 18.4k to 9.2k store misses/frame. Its `decode_rect` work falls from
165k to 136k instructions/frame. R4's `decode_rect` falls from 253k to 218k.
Crash's `texture_for` falls from 114k to 108k, and `decode_rect` from 47k to 41k.

A separate non-HASH 600-frame R4 run confirms fewer renderer operations,
with the same 167,952 primitives:

- Tile decodes: 41,589 → 38,348.
- Texture invalidations: 4,126 → 3,137.
- Batches: 12,889 → 11,694.
- GPU synchronizations: 3,116 → 3,007.
- Evictions: unchanged at 1,743; uploads unchanged at 4.

Stats logs are `/tmp/renderer-sim/r4_{before,final}_stats.log`. The lower
invalidation count is from validating the cached square rather than the entire
page. The instruction reductions remove actual work, but the modelled cycle
reductions still depend on simulated cache behaviour. No hardware FPS or cycle
improvement is claimed.

Implementation commit: `4e5f2a1` (`Remove redundant texture validation and packed
VRAM upload work`). The renderer changes are carried in regenerated
`patches/0002-psx3d-widescreen-and-stereo-depth.patch`; the upstream core submodule
revision is unchanged. The outer repository's modified `core` status is expected
because that submodule contains the applied patches.
