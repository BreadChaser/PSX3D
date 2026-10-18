# RTT round two: retain disjoint texture regions

Based on `ce16a3a` (`t3/rtt-no-readback`), implemented on `t3/rtt-round2`.
No console connection, install, remote input or push was performed. Hardware
acceptance belongs to the console worker. The simulator's `sotn` state is the
Old slot-2 Dracula fight, SHA256
`cb0e0e373a05d3676c5881afba5e50f7158e975633b20d38c037827d60c83342`.

Private evidence, scripts, logs, simulator binaries and build artifacts are in
`/home/tony/Development/PS1_3ds/devtools/rtt-round2/`.

## Cause of the remaining fight cost

Round one's cache kept one square for each `(depth, page, CLUT, window)` key.
When another sprite sampled outside that square, it waited for queued readers,
freed the square, allocated a larger square containing the **union**, and decoded
that union again. Scattered sprites with the same key grew from 32 to 128 to 256
texels. Their large, mostly unused squares filled the 4 MiB atlas, evicted each
other and repeated the growth/decodes next frame.

An instrumented replay of the same 60-frame fight capture attributes **every**
GPU synchronization as follows (counts include initial state upload):

| Reason | Round one | Region cache |
|---|---:|---:|
| Growing a cached square | 1546 | 0 |
| Texture-source readback | 59 | 59 |
| Upload staging capacity | 3 | 4 |
| CPU VRAM read / CPU copy fallback | 0 | 0 |
| CLUT readback | 0 | 0 |
| Eviction / invalidation / vertex capacity | 0 / 0 / 0 | 0 / 0 / 0 |
| Total | 1608 | 63 |

A representative steady frame was **28 growth waits + 1 texture readback**;
the candidate has **one texture readback**. The hardware's roughly 24.5 syncs per
frame samples a later/longer part of the fight. A separate 600-frame ARM run
also finds the growth problem throughout the scene. Evictions themselves did
not wait: growth had already retired their readers. State changes, fills and
ordinary uploads do not intrinsically wait; staging exhaustion can.
There are no GP0 80h copies in this fight capture. CPU reads and CPU-copy
fallbacks share the external readback entry point; their combined count is zero.

All **27,060** decoded tiles are **4-bit**, with no texture window. Repeated
large page/CLUT unions dominate: examples are page 8 / CLUT 15395 (1,680 tiles),
page 10 / CLUT 15521 (1,560), page 10 / CLUT 15527 (1,440) and page 10 /
CLUT 15528 (1,260). There were 2,326 square growths and 1,477 evictions.

CLUT cycling is a small part of this particular workload: only three cached
palette invalidations in the old cache over 60 frames. Retaining more regions
makes seven affected entries visible in the candidate. The two continuously
invalidated source regions are page 24 / CLUT 15488 and 15695, each losing two
32×32 decoded tiles per frame. A **6×45 upload at VRAM (513,257)** arrives every
host frame. Palette-row uploads `(0,246,256,1)` and `(0,247,256,1)` occur 15 and
20 times respectively. Whole textures are not being re-decoded each frame
because every palette animates: eviction/growth is the main cause.

## Change and switches

A key can now retain several independent squares. The normal lookup and bounds
check stay on the common path. A region miss searches that key's chain for a
covering square, or allocates a new tightly bounded square, while keeping old
squares and their queued GPU readers intact. There is no new atlas allocation
or per-entry metadata. Existing tile versions, palette-row versions, readback
rules and eviction lifetime barriers still apply to every retained square.
Fewer decodes also remove most texture-cache rebind batch endings.

The remaining single readback is retained; no dependency wait was suppressed.
Uploads already queue through the GPU staging path, so no additional upload
mechanism or palette shader was needed. This change does not promise one sync
per frame in games with multiple real indexed read-after-write dependencies
(CTR still has them).

Runtime switches, applied at a clean game load/relaunch:

| Switches | Behavior | Native `PICA_DEBUG` |
|---|---|---:|
| `rtt=new rttcache=new` | Precise RTT + retained regions (default) | 0 |
| `rtt=new rttcache=old` | Round-one single-square cache | 524288 |
| `rtt=old` | Original RTT and single-square cache | 262144 |

Put named switches after `gldbg=` if combining them; that token replaces the
whole bit field. Preserve other debug bits when adding/removing bit 524288.
Changing modes while a cache is live is unsupported. There is one new
mechanism and one independent A/B switch for it.

The stats line now includes reasons `cpu`, `tex`, `clut`, `evict`, `inval`,
`grow`, `verts`, `stage`, and `prev texture/upload`. Previous-submission waits
are counted separately from intra-frame `syncs`. Host-only `PICA_CACHE_TRACE`
records source/palette invalidations, decoded tile masks, growth/eviction and
CPU upload coordinates. These diagnostics and the larger frontend stats buffer
are included in patch 0002/the frontend change.

## Picture checks

Rebuilt the pristine round-one renderer and candidate against the same reference
harness. All **540/540 per-frame full-VRAM hashes** match, including offscreen
pixels and mask bits: six requested states plus SotN title, transition menu and
stable menu. Every selected frame 10/30/60 is byte-identical before/after.
P.E.Op.S. also replays/verifies the captured full-VRAM snapshots; the candidate's
reference image metrics are identical to baseline for every selected frame.
`games/final-results.json` records the recomputed metrics.

The atlas-placement change exposed an oracle precision defect: the old host
rasterizer's float interpolation chose different texels at exact integer UV
boundaries after an integer atlas translation (29–42 fight scanout pixels at
selected frames). Double precision alone did not fix every boundary. The harness
now selects nearest texels using integer edge numerators derived from the
renderer's quarter-pixel vertices. Both baseline and candidate were rebuilt
against that corrected oracle. Old stored reference metrics must not be compared
across the oracle change. This is mathematical translation invariance, not a
claim to emulate PICA's interpolation precision; console image checks remain
necessary.

The RTT probes compare all three runtime modes: exhaustive direct RGB5/mask,
native copies, staging rollover, CPU/GPU palette replacement and a new
four-frame scattered-region fixture with oblique polygons and source/CLUT
replacement. Scanout and complete VRAM match in all cases. ASan/UBSan and the
existing edge/presentation regression pass.

`SIM_CHECK_ATLAS_LIFETIME=1` retains sampled atlas words until submission ends,
so an unsafe CPU overwrite fails even though the fake GPU rasterizes immediately.
GPU writes to the atlas remain ordered. The deliberate removal of `texture_idle`
makes the palette fixture fail this guard. The normal candidate passes it and
the existing staging guard on all game captures. These guards cover observed
samples, not hardware cache prefetch/coherency.

## Counters and simulator costs

The table contains totals over the identical **60 host frames**, including
startup. Rebinds are the existing `ends: ... rebind` counter, not all texture
bind API calls. Small batch increases in some games come from different cache
warming/batch boundaries; primitive counts and output remain identical.

| Scene | Syncs | Readbacks | Tile decodes | Rebind endings | Batches |
|---|---:|---:|---:|---:|---:|
| SotN fight | 1608 → 63 | 59 → 59 | 27060 → 492 | 5257 → 220 | 5797 → 783 |
| Crash (`o3auto`) | 11 → 3 | 0 → 0 | 179 → 145 | 90 → 89 | 628 → 629 |
| Silent Hill | 16 → 3 | 0 → 0 | 252 → 123 | 109 → 118 | 8400 → 8409 |
| R4 | 345 → 3 | 0 → 0 | 10139 → 343 | 2193 → 707 | 2300 → 847 |
| CTR | 111 → 108 | 105 → 105 | 270 → 267 | 377 → 374 | 1137 → 1134 |
| GT2 | 25 → 3 | 0 → 0 | 594 → 381 | 615 → 617 | 8231 → 8239 |

The independent ARM interval (frames 301–600) gives SotN **25.43 → 1.00
syncs**, **1.00 → 1.00 readbacks**, **406.80 → 4.26 decodes**, **83.20 → 1.01
rebind endings**, and **91.84 → 10.24 batches per host frame**. Its growth-sync
count drops to zero. This interval matches the shape of the hardware finding
better than the short capture's startup totals. `arm-counters.json` also records
the five other games; their warmed work counters are effectively unchanged.

The steady fight frame after warming has **1 sync, 1 readback, 4 decodes,
0 evictions**, typically 1–3 rebind endings and about 10–12 batches. The 600-frame
run includes additional animation changes; see its raw interval counters.

| Scene | M instructions pristine → on | M cycles pristine → off → on | On vs pristine | On vs off |
|---|---:|---:|---:|---:|
| SotN fight | 4.81 → 1.46 | 8.82 → 8.88 → 2.80 | -68.3% | -68.5% |
| Crash | 2.49 → 2.52 | 3.80 → 3.85 → 3.89 | +2.4% | +1.0% |
| Silent Hill | 2.00 → 2.01 | 4.24 → 4.34 → 4.38 | +3.3% | +0.9% |
| R4 | 2.31 → 2.32 | 3.30 → 3.30 → 3.31 | +0.3% | +0.3% |
| CTR | 2.68 → 2.68 | 4.78 → 4.75 → 4.75 | -0.6% | +0.0% |
| GT2 | 3.28 → 3.28 | 5.63 → 5.71 → 5.72 | +1.6% | +0.2% |

The mechanism's same-binary off/on comparison separates cache behavior from
code placement. Against the pristine binary, small modeled cycle increases
remain in Crash/SH/R4/GT2, largest in SH; this is **not** a completed
no-performance-regression acceptance. Both instruction counts and counters
are provided so the console worker can distinguish extra CPU work from modeled
I-cache/layout effects. The common steady non-SotN workloads already had few or
no decodes/syncs after warming, so removing their startup growth cannot improve
that steady interval much. Hardware validation or further work is required for
the user's no-regression condition.


CPU-only fake citro3d, `-O2`, SMALL=1, original state/CUE/options. Separate
300/600-frame profiles are subtracted and divided by 300. These are whole
emulator instructions and modeled cycles per **host frame**. GPU execution and
queue waits are absent. Old 3DS instruction-cache behavior and layout sensitivity
require hardware checks; do not translate modeled cycle savings into FPS.

## Build and reproduction

The local simulator copy was `/tmp/rtt2-sim`; bulky traces were unpacked one at
a time, and the copy was removed after archiving the evidence. Original simulator
files and states were not modified. `sotn` is the fight; the old evidence's
`sotn` trace is the **title**, so use `sotn-fight.gputrace.gz` for this task.
Private `check-final.py`, build scripts, options, SDK and runner binaries preserve
exact commands. Never copy the old host-title state over `sim/states/sotn.st0`.

Initialize/patch the worktree as in `docs/gpu-rtt.md`. Regenerate patch 0002 with
`devtools/mk.sh`'s exact `git -C core -c diff.mnemonicPrefix=false -c
diff.noprefix=false diff -- Makefile Makefile.libretro frontend/libretro.c
libpcsxcore plugins` command **in this worktree**. Its ignored new core files
must be intent-to-add so that diff includes them. The global mk.sh hardcodes the
main checkout and must not be run unmodified from this worktree.
Reverse-apply check passes. Build with `source /home/tony/Development/PS1_3ds/devtools/env.sh`,
`make -j16`, then `make cia` with the documented bannertool/makerom paths.
3DSX and CIA pass; the existing executable-stack linker warning remains.
Artifact hashes are in private `artifacts/SHA256SUMS`.

Rebuild native replayers with `BUILD_DIR=/tmp/rtt2-sim REPLAY_ONLY=1
tools/gpu-compare/build.sh`. Run `tools/gpu-compare/rtt.py --replay
/tmp/rtt2-sim/replay-pica --output /tmp/rtt2-sim/rtt` under the simulator venv;
run `BUILD_DIR=/tmp/rtt2-sim/regress tools/gpu-compare/regress.sh` for sanitized
edge/presentation checks. Replay the archived traces with `GPU_HASH_VRAM=1
GPU_FRAMES=10,30,60 PICA_STATS=1 SIM_CHECK_STAGE_LIFETIME=1
SIM_CHECK_ATLAS_LIFETIME=1`, once with cache bit 524288 and once without it.

## Console worker handoff

Use the separate fast-loop worker's integration of this branch; these artifacts
do not include its control-server work. No console was contacted here.
Do not overwrite slot 2/0. Repeat clean loads in cache-off → on → off order,
keeping clock/audio/threads/frameskip fixed. Example commands after integration:

```sh
psx3dctl.py old load "Castlevania" state=2 renderer=gpu rtt=new rttcache=old
psx3dctl.py old load "Castlevania" state=2 renderer=gpu rtt=new rttcache=new
psx3dctl.py old stats
psx3dctl.py new load "Castlevania" state=0 renderer=gpu rtt=new rttcache=new
```

Use load, not a live `set gldbg`, to change the cache policy. If the control
worker's token parser does not yet accept `rttcache=`, use `gldbg=524288 rtt=new`
for off and `gldbg=0 rtt=new` for on at load (preserve any other needed bits).
Record at least 30 seconds from repeated state loads. Compare FPS, PMU cycles,
queue wait and interval-normalized counters, plus fresh shots/motion bursts of
Richter, Dracula/cape/fire, HUD bars/text and transparency. Target roughly one
sync/readback and four tile decodes per steady fight host frame; some animation
changes add decodes. Check both consoles' normal renderer ownership/thread mode.

Check SotN title/menu with `rtt=new` after booting without a state, including
transitions and the round-one white-block fix. Test `o3auto`, SH, R4, CTR and GT2
with their existing states; R4 should lose its cache-growth traffic, CTR retains
real feedback reads. Confirm frameskip off and auto-skip motion. Keep the runtime
switch to isolate any hardware layout/precision/coherency regression. Beating the
established Old software fight result (52.4 FPS off / 60 equivalent auto-skip)
and avoiding all picture/performance regressions remain hardware acceptance
conditions, not claims made from the simulator.
