# GPU render-to-texture: implementation and hardware handoff

> Historical report: submodule/patch instructions apply to its original commit.
> Current builds use the committed `core/` directory; see [vendor-core.md](vendor-core.md).

Implementation commit `9bc61b4` on `t3/rtt-no-readback`, based on console build `10a5ac8` (`t3/fps-in-layout`).
No consoles were contacted and nothing was pushed. This is a host-verified
candidate, not a hardware acceptance result. Beating software in SotN and fixing
the reported menu white blocks / fight HUD striping remain hardware acceptance
conditions. The synchronous reference model does not reproduce those defects.

## What the game actually reads

The private evidence directory is
`/home/tony/Development/PS1_3ds/devtools/rtt-no-readback/`.
It contains raw states/traces, selected images, logs, simulator profiles and
build artifacts; proprietary game data is excluded from Git.

Initially no SotN simulator state existed. `runner_capture` booted the USA CUE
with HLE BIOS for 1800 host frames, saved a title state, then captured 60 frames.
Starting from that state, START (`1 3 8` input-script interval) plus 180 warmup
frames reached the menu transition; another 300 reached the stable menu.
Those host states are named `sotn-title`, `sotn-menu`, `sotn-menu-stable`.
The private test scripts use `sotn` as an alias for the **host title**, not the
later hardware fight state. The user's Old slot-2 state was copied unchanged
as `sotn-fight.st0`; SHA256
`cb0e0e373a05d3676c5881afba5e50f7158e975633b20d38c037827d60c83342`.
No state on either console was changed.

All quantities below describe 60 host-frame captures, not seconds:

| Scene | Sampled formats | Principal old dirty dependencies (VRAM words) |
|---|---|---|
| SotN title | 4-bit and 8-bit CLUT; no direct 15-bit | 60 CLUT reads `(512,241,256,1)`, 60 texture reads `(512,0,64,256)`; 4-bit regions near `(544,384..448)` |
| SotN menu transition | 4-bit and 8-bit CLUT; no direct 15-bit | 62 CLUT reads `(256,241,256,1)`; 26 each texture `(384,128,8,32)`, CLUT `(16,250,16,1)` and `(96,249,16,1)` |
| SotN stable menu | same | 90, 30, 30, 30 respectively; CPU uploads palette row `(256,240,256,1)` every host frame |
| SotN Dracula fight | 4-bit only | 90 texture reads `(512,192,8,32)`; 60 each `(512,160,32,64)`, `(536,96,8,32)`, `(528,96,16,96)`, `(528,0,32,224)`, `(544,224,8,32)`; no dirty CLUT reads |
| CTR | 4-bit only | 128 texture and 15 CLUT reads: strips `(496,192,8,64)` / `(384,192,8,64)` 30 each, `(328,192,8,64)` / `(512,256,32,128)` 15 each; CLUT x560,y432/434/440,width16 |

The old path's 300 title, 140 transition-menu, 180 stable-menu and 539 fight
readbacks **all converted zero words different from CPU VRAM**. These were false
or unchanged dependencies, rather than useful framebuffer feedback. The coarse
64×16 map and inclusive maximum primitive bounds implicated neighbouring texture
pages and palettes. Title/menu display alternates y=0/256; fight alternates
x=0/256. Menu drawing ends at x384,y240, while several dependencies start just
outside it. The row-240 upload also falsely invalidated row-241 palettes even
with an 8×8 map. These captures show no screen-copy/blur direct-texture effect.

CTR has genuine feedback: 68/143 old readbacks were entirely unchanged;
75,450 of 276,480 converted words differed from CPU VRAM. Some requests still
conservatively touch unchanged 8×8 tiles. Explicit GP0 80h capture hooks found
**zero VRAM copies** in title, menu, fight and CTR captures; no speedup is credited
to the new GPU-copy path in these scenes.

## Implementation

* GPU writes use native 8×8 dirty/version maps. Rectangle maximum edges are
  exclusive; polygon bounds remain conservative and are clamped to the drawing
  area. CPU palette row versions, intersected conservatively with tile versions,
  separate adjacent palette rows. GPU palette writes still invalidate through
  their native-tile generations. Cache validation retains unaffected decoded
  32×32 atlas tiles instead of discarding an entire cached square.
* Indexed textures and CLUTs read back only dirty native tiles intersecting
  the requested rectangle. Adjacent tiles share one cache-invalidation call.
  Readbacks copy/convert 8×8 pixels, so an unaligned request is rounded outward
  to native tiles. Consecutive reads with no intervening GPU writes share a
  completed sync; synchronization cannot move past an actual read-after-write.
  This does not force one sync per frame when real dependencies intervene.
* Non-windowed direct 15-bit textures with GPU-written sources decode on the GPU
  from the RGBA5551 VRAM target into the existing RGBA8 atlas render target.
  Append-only vertices and GPU command ordering retain old readers. TEV stages
  recover `rgb5 << 3`; separate alpha processing distinguishes transparent zero,
  opaque colour and STP=128, including black+STP. Snapshots track subsequent
  GPU overwrites while CPU VRAM is still stale. Windows, unavailable target
  metadata and other unsupported conditions keep the CPU path. No extra atlas
  image is allocated; tracking adds about 68 KiB of static memory.
* Bounded, non-overlapping, 8-pixel-aligned GP0 80h copies with E6 mask=0 copy
  native GPU tile bands in one strided queued `GX_TextureCopy`. Wrapped, overlapping,
  partial-tile, mask and second-VRAM cases retain CPU fallback. GPU copies route
  synchronously to the renderer owner thread and defer CPU destination updates
  until a CPU reader needs them.
* Uploads now flush deferred primitives and reserve vertex capacity **before**
  assigning staging rows. Previously a later flush/vertex rollover could reset
  `stage_y` after reservation, letting the next upload overwrite a pending one.
  The fix applies to both RTT modes. A delayed-reader guard fails deterministically
  when this fix is reverted, and passes with it. This is a confirmed lifetime bug;
  its responsibility for the reported hardware flicker is **not established**.

The default is `rtt=new`. `rtt=old` in `switches.txt` restores the old RTT map,
readback/invalidation and CPU-copy path in the same executable. It keeps the
upload lifetime fix. Put it after any `gldbg=` token (the bit is 262144). Relaunch
for A/B; changing the bit in the middle of a game is unsupported. Native replay
uses `PICA_DEBUG=262144` / `0`.

## Picture and counter verification

All 60 per-frame full VRAM hashes match base `10a5ac8` for the five original
states and four SotN cases: **540/540 frames**, including offscreen data/mask bits.
Frames 10/30/60 through P.E.Op.S. show exactly the same reference metrics before
and after for the five original states plus SotN title, transition menu and fight.
Existing PS1/PICA rasterization and blend differences remain. SotN title has
0% pixels with channel error >24; fight has 0.06348%, 0.06348%, 0.05046% at the
three selected frames (MAE 0.05336, 0.05395, 0.06734 /255). The severe white blocks
and HUD streaks in `devtools/sotn_title` are absent in both baseline and candidate
host rasterizations; host image equality cannot certify their hardware repair.

Counters are totals over the same 60 host frames; initial state-upload work is
included. `syncs` also includes texture eviction/upload pressure, not only RTT.

| Scene | Batches old→new | Tile decodes old→new | Syncs old→new | Readbacks old→new | Rebind endings old→new |
|---|---:|---:|---:|---:|---:|
| Crash | 628→628 | 179→179 | 11→11 | 0→0 | 90→90 |
| Silent Hill | 8532→8400 | 368→252 | 76→16 | 60→0 | 167→109 |
| R4 | 2812→2300 | 11012→10139 | 593→345 | 216→0 | 2728→2193 |
| CTR | 1295→1137 | 981→270 | 149→111 | 143→105 | 520→377 |
| GT2 | 8235→8231 | 648→594 | 79→25 | 54→0 | 616→615 |
| SotN title | 900→140 | 14001→445 | 304→4 | 300→0 | 780→20 |
| SotN menu transition | 941→680 | 6172→1924 | 152→15 | 140→4 | 782→521 |
| SotN stable menu | 1318→967 | 10160→2877 | 188→8 | 180→0 | 1198→847 |
| SotN fight | 6560→5797 | 27900→27060 | 2148→1608 | 539→59 | 6050→5257 |

The exhaustive direct-texture probe checks all 65,536 RGB5+mask values, repeated
source overwrites, window fallback and rollover. Its readbacks fall 99→1
(the deliberate CPU fallback), syncs 197→4, CPU decodes 289→65, with 224 tiles
GPU-decoded; full native VRAM and scanout are identical. Native GPU copy, CPU/GPU
palette replacement, adjacent-row isolation and staging-rollover probes also
match. ASan/UBSan pass these four probes and the existing edge/presentation
regressions. `SIM_CHECK_STAGE_LIFETIME=1` passes menu/fight replays; the deliberately
reverted upload ordering fails the stage probe.

Draw hashes remain identical for Crash, menu transition, stable menu and fight.
They change for SH, R4, CTR, GT2 and title: this diagnostic hashes emitted vertex
order/state transitions and samples representative atlas-edge texels, rather
than final pixels. Fewer flush barriers let the existing disjoint additive queue
remain deferred longer. The SH record audit has 126,840 vertices in each run and
an identical multiset of all eight semantic vertex fields, in different order.
CTR, GT2 and title retain identical ordered vertex records, but emit different
state transitions. R4 retains identical position/colour multisets; five edge
records resolve to a different neighbouring atlas entry in `sim_vertex`'s
heuristic boundary lookup. That diagnostic does not identify the texture bound
to the draw. Full per-frame VRAM equality is the authoritative output check. Private
`hash-explain/` contains the record audit for each changed game.

## ARM simulator cost

CPU-only fake citro3d, `-O2`, SMALL=1, matching CUE and state/options. Separate
300/600-frame runs, profiles subtracted and divided by 300 to exclude startup.
These are whole emulator instructions and modeled cycles per **host** frame,
not PICA execution times or predicted console FPS. Qemu underestimates Old 3DS
instruction-cache effects; code layout can change hardware results.

| Scene | M instructions old→new | M modeled cycles old→new | Cycle change |
|---|---:|---:|---:|
| Crash | 2.40→2.49 | 3.68→3.81 | +3.5% |
| Silent Hill | 2.05→2.00 | 4.35→4.25 | -2.3% |
| R4 | 2.77→2.31 | 3.99→3.29 | -17.5% |
| CTR | 2.80→2.68 | 4.98→4.79 | -3.8% |
| GT2 | 3.24→3.28 | 5.62→5.62 | +0.0% |
| SotN title | 3.60→0.92 | 5.82→1.46 | -74.9% |
| SotN menu transition | 2.09→0.76 | 3.96→1.81 | -54.3% |
| SotN fight | 5.39→4.81 | 9.67→8.82 | -8.8% |

Crash/GT2 incur tracking overhead; the switch is retained for hardware checks.
Fight still decodes ~450 tiles and evicts ~26 entries per host frame: atlas
pressure remains substantial after eliminating most false readback dependencies.
No claim is made that the hardware performance goal is already satisfied.

## Build and reproduction

Current builds use the committed core directly: `source devtools/env.sh &&
make -j16`, then `make cia` with the specified bannertool/makerom paths.
There is no patch setup/regeneration step. At the original report commit,
both 3DSX and CIA built and the old patch series passed reverse checking.
The existing upstream executable-stack linker warning remains. Final artifact SHA256:

* 3DSX: `7867633ff1fec435038a7fa7f3292ef25639f431b6c92a1b4c57adb2f4561d29`
* CIA: `34c6c5eb596b0617d9dd0c8f60ddb2d9a0b15e19c5705df138af0a592275323f`


See `tools/gpu-compare/README.md` for boot capture, explicit copy records,
classification and synthetic commands. Private `hashcheck/results.json`,
`classify-final/results.json`, `perf-{old,new6}-*.summary`, selected comparisons,
replayers, private simulator SDK/scripts, build logs and artifact SHA256 manifest
are archived with the evidence. Bulky raw traces/PPMs are gzip-compressed; unpack them before replay. The `/tmp/rtt-sim` working copy is removed after
archiving; restore traces/states there or override the paths to reproduce.

## Hardware acceptance procedure — for the console worker

1. Use the archived final CIA on Old 3DS and final 3DSX/CIA on New 3DS. Back up
   existing switches/configs and logs. Serialize transfers with the existing
   transfer lock; verify every input step by fresh screenshot. Do not change
   slots 2/0, press HOME during transfer, or create another NTR viewer.
2. Test each scene as **old → new → old** in the same executable. Relaunch for
   every mode. Keep renderer, frameskip, CPU clock, thread/audio settings and
   NTR quality fixed. Use `stats noskip renderer=gpu pmu=0,2 rtt=old` or `rtt=new`.
   Use the full game title with underscores if selecting `game=`.
3. SotN title/menu: boot normally, **omit `state=`**. Wait for the title to settle,
   log at least 30 seconds, then press START once after checking the screenshot.
   Wait for the main menu to settle and record 30 seconds. Compare median FPS,
   cycles/frame, queue wait, and interval-normalized syncs/readbacks/decodes/
   rebind endings/batches. Historical Old baselines are title GPU24.88 vs SW33.92,
   menu GPU33.73 vs SW48.63. Compare against those established software numbers.
4. Capture complete timestamped menu bursts with identical NTR settings. Check
   menu text for white rectangles (old 48/101 sampled frames), black frames
   (old 4/101 including transition), fades and alternating banks. Keep transition
   samples separate from stable menu. Zero sampled corrupt frames is necessary;
   inspect live motion too because NTR samples only a few frames per second.
5. SotN fight: `game=Castlevania_-_Symphony_of_the_Night state=2` on Old and
   `state=0` on New. Independently saved states have different combat timing;
   compare old/new **within each console**. Record 30 seconds from repeated loads,
   no gameplay input. Compare HUD text/health bars, Richter, Dracula cape edges,
   transparency, black frames and the counters above. Existing Old GPU diagnostic
   ~11.7 FPS is not a controlled baseline; do not treat its comparison to SW49.7
   as a new benchmark matrix. Take matching-state screenshots and a motion burst.
6. CTR: `game=CTR_-_Crash_Team_Racing state=2` on Old, `state=0` on New. Repeat
   the same 30-second old/new/old cycle, inspect track/HUD/kart/transparency and
   compare FPS plus readbacks/decodes/rebinds. Expect some genuine indexed RTT
   readbacks to remain. Test New's normal GPU worker; if it differs from direct
   mode, repeat with `nogputhread` for diagnosis without changing the baseline.
7. Smoke-check the other five states with fixed settings, especially Crash/GT2
   for tracking/layout regressions. Restore original switches/configs afterwards.
   Accept only when SotN beats its established software FPS in the requested
   scenes, CTR improves, no new picture/black-frame defects appear, and the
   reported SotN menu/HUD defects are absent. If defects persist, retain bursts
   and use `rtt=old/new` plus `nogpuasync` as separate diagnostic runs; do not
   describe this candidate as a complete picture fix.

Hardware-only risks still requiring that pass: PICA TEV rounding and render-target
cache barriers on the GPU decode path; queued GX native-copy behavior; New worker
ownership; asynchronous texture/staging/cache interactions absent from the raster
model; and Old 3DS instruction-cache layout sensitivity. The native-copy fixture
uses the fake GX memory copy and is not hardware validation.

## Round-two continuation

[Disjoint-region caching and sync/decode profiling](gpu-rtt-round2.md) continues
this work from `ce16a3a`. It retains the round-one behavior behind `rttcache=old`
and documents the candidate, reference-harness precision correction, six-game
costs and console-worker acceptance steps.
