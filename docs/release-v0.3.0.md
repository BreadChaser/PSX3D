# PSX3D v0.3.0 integration report

Local branch **release/v0.3.0**, based on GPU renderer **9b4dc04**. No push,
deployment or console access was performed. The final CIA title version is
3072 and the application version is 0.3.0.

**Software is the factory default on both Old and New 3DS**, including games
without a profile. **Settings → Renderer** is a normal global/per-game choice:
**Software / GPU (experimental)**. A game must be closed and reopened to apply
it. The same choice remains in Core options; no debug flag is required.
`renderer=sw|gpu` remains a startup testing override. No profile enables GPU.

## Source groups

Each source was squashed separately with its branch named in the commit,
except the actual main/audio ancestry merges. Group 9 contains main, bottom
screen and the reviewed duplicate main audio implementation. Every numbered
group passed a 3DSX build, CIA build, and all five 600-frame draw-hash runs.

| Group | Source commits/range | Release commit | Result |
|---|---|---|---|
| 1 flicker | t3/fix-gpu-polygon-flicker: cec36d0, d6158db, a389c8c, 0fa3e99 | 078b372 | Edge coordinates and prepare-before-present/held scanout |
| 2 reference | t3/gpu-renderer-reference-harness: 7d03c47, 89392c5, 250bae5; bc44d30 evidence | 9a945c4 | Sampling, additive transparency, STP masks |
| 3 correctness | t3/gpu-correctness-2: bc44d30..0693635 (174c12b, 43281a4, 1378b05, bf25d15, 0693635) | ac79859 | Modulation, dithering, flipped sprite wrap, cached dither state |
| 4 batching | t3/silent-hill-gpu-batching: 9b041c4, fe27a7d, 6080868 | b646ddc | Disjoint additive ordering and mask/upload barriers |
| 5 worker | t3/thread-gpu-command-processing: 7f00719 only | 5251df3 | New PICA worker on core 2, synchronous frontend owner callbacks |
| 6 dual | t3/dual-renderer-on-head: cf851e6, 49be903, de1ab9d | 54908ba | Runtime choice, lazy buffers, VRAM reservation, Software defaults |
| 7 statistics | t3/detailed-fps-stats-panel: 1b7dcd4 | 9003ab2 | Off / FPS / Detailed |
| 8 audio/memory | t3/port-o3ds-audio-stretch: 27bb0f9, 7b12dc2 | afab7b7 | One resampler; Old heap/cache/state budget; fs3 test switch |
| 9 frontend | main ea8d8c9; t3/bottom-screen-main ea8d8c9..00d6f78 (08296b5..00d6f78 series); t3/audio-stretch-main b74aabb | 2b154ee, 37ecad8, 0f113b9 | Main history, README/screenshots, slots/presets/controls/hotkeys/reset; keep GPU-port audio |
| 10 keeper | t3/o3ds-keep: 9b4dc04..ef000a3, renderer-only stage 166af68 | 3f72ec6 | CPU texture/cache/upload/fill trimming only |

Final integration fixes gate DMA-chain coalescing to PICA, retain software's
original presentation policies, complete preset selection and UI coexistence,
and add combined regression coverage. These are release integration work,
not another source optimization group.

## Conflict resolutions and exclusions

All core source deltas were reconstructed from their branch patches and
three-way-applied to **real core sources**. Conflicts were resolved there;
0002 was regenerated with the mk.sh path list and git diff flags, after
`git -C core add -N` for new files. No patch text was hand-merged and no core
gitlink commit was changed. Applying both patches to upstream reconstructs the
working sources exactly.

- Sampling/flicker/STP changes preserve vertex double-buffer banks, clean
  ranges, frame sequences and texture/readback fences. Edge clamping and the
  half-pixel phase survive together.
- Batching carries dither state in deferred effects and state keys. Actual
  emitted semi-transparent primitives set batch_semi; untextured tags and
  mask-check/upload/vertex-wrap barriers preserve STP ordering.
- The worker was based on de1ab9d, which already contained selector commits.
  Only 7f00719's own diff was applied at group 5; selector commits arrived once
  at group 6. The flicker prepare-present callback was added to the owner RPC.
- Frontend conflicts combined audio, renderer and new control config fields,
  pacing/FF, PMU/stat counters and lifecycle hooks. Detailed replaces the touch
  layout with a visible MENU button and footer cycling. Its hidden touch
  controls/stick/actions are suppressed; FPS shares the normal layout. Every
  video/menu/bottom draw uses the synchronous PICA owner callback when active;
  UI text/buffers are touched after the frontend worker fence.
- Preset renderer precedence is switch > explicit game choice > this console's
  SOFTWARE/GPU field > global choice. DEFAULT inherits global. Identification
  occurs after GPU_init, so a changed curated choice triggers one bounded
  startup reload before emulation. Disc swaps never switch the active renderer.
  Game reset restores its preset/global choice; global reset restores Software.
- Keeper upload code retains the newer staging lifetime fence, bank limit and
  dither-combiner reset, plus direct local readback calls on the owner. Packed
  conversion/fill stores and narrower cached-square checks are the only keeper
  algorithms imported.
- Software parity checks exposed a GT2 Old half-height difference caused by
  GPUdmaChain coalescing inherited from the GPU base. Gate that optimization to
  PICA to restore main's command boundaries and exact software framebuffer
  checksums. GPU 1x presentation, overlap and targeted cleaning are also gated
  to PICA. Software keeps Old half-height polygons, supersampled presentation,
  and New threaded rendering. PICA initialization/dithering/owner hooks remain
  inactive when software is selected.

Harness 85423f1 was already imported as cec36d0: retained once. The main audio
resampler and its test are byte-identical to the GPU-port implementation;
0f113b9 records the main audio branch ancestry with the existing implementation
kept. No duplicate resampler was added.

**Excluded:** every other t3/o3ds-combined* stage (readback, GTE assembly,
predictive frameskip/held-present/lazy-open, XA decode, SPU DMA); the named
shrink-dynarec-code-size, ndrc-code-layout, dynarec-io-fast-paths,
profile-ps1-event-costs, reduce-spu-register-mixing, optimize-* and
gte-color-assembly branches. Base GPU-renderer work already present at
9b4dc04 remains. Audio/memory stays for functionality, independently of the
speed-only keeper decision.

The external bench result for the isolated keeper was CTR 26.23 FPS versus
25.32 using its same-binary reference switch (**+3.61%**), with enabled repeat
26.23; source-only versus plain baseline was about +2.7%, Crash/GT2 unchanged.
Evidence: ignored devtools/combined_results.md. This is evidence for the keeper
on its tested base, not a measured speed claim for the complete release.

## Draw hashes by group

PICA direct mode, frameskip off, SMALL=1, 600 frames per supplied state. The
simulator records vertices, sampled texels and draw state, not hardware pixels.
3DSX and CIA builds passed on every row. Machine-readable results and final
artifact checksums: [integration.json](release-validation/integration.json).

| Group | o3auto (Crash) | sh | r4 | ctr | gt2 |
|---|---|---|---|---|---|
| g00-baseline | `6ba7ef2f6e06cf04` | `244abb4a61d295c3` | `6e1a6f9b64542f09` | `497b85e24ef6ff72` | `7b3a81441ad9b8aa` |
| g01-flicker | `4a529fb5902bf62c` | `244abb4a61d295c3` | `6e1a6f9b64542f09` | `497b85e24ef6ff72` | `7b3a81441ad9b8aa` |
| g02-reference | `48989c3302708cf3` | `8e6077a52921c944` | `2951a4139d244c49` | `270757ba223ed57e` | `5311e04c816032ab` |
| g03-correctness | `c15806a28d454f0d` | `e872eaaee0bae722` | `e487455510196c6e` | `8777284ca4cbe02b` | `1d9f1be23a1f0803` |
| g04-batching | `c15806a28d454f0d` | `b923eed57d0caa7e` | `e487455510196c6e` | `fae4a770f87ae68a` | `0cbb9338cde01ef3` |
| g05-worker | `c15806a28d454f0d` | `b923eed57d0caa7e` | `e487455510196c6e` | `fae4a770f87ae68a` | `0cbb9338cde01ef3` |
| g06-selector | `c15806a28d454f0d` | `b923eed57d0caa7e` | `e487455510196c6e` | `fae4a770f87ae68a` | `0cbb9338cde01ef3` |
| g07-stats | `c15806a28d454f0d` | `b923eed57d0caa7e` | `e487455510196c6e` | `fae4a770f87ae68a` | `0cbb9338cde01ef3` |
| g08-audio-memory | `c15806a28d454f0d` | `b923eed57d0caa7e` | `e487455510196c6e` | `fae4a770f87ae68a` | `0cbb9338cde01ef3` |
| g09-frontend | `c15806a28d454f0d` | `b923eed57d0caa7e` | `e487455510196c6e` | `fae4a770f87ae68a` | `0cbb9338cde01ef3` |
| g10-renderer-cpu | `c15806a28d454f0d` | `b923eed57d0caa7e` | `e487455510196c6e` | `a5ab0221e0484e96` | `0cbb9338cde01ef3` |
| final | `c15806a28d454f0d` | `b923eed57d0caa7e` | `e487455510196c6e` | `a5ab0221e0484e96` | `0cbb9338cde01ef3` |

Group 1 changes Crash's missing edge geometry. Group 2 changes sampling/STP in
all five scenes; group 3 changes modulation/dither/wrap. Group 4 changes the
ordered stream in SH/CTR/GT2 because disjoint additive draws are deferred;
independent scanout and full-VRAM/mask ordering fixtures match. Groups 5–9
leave direct hashes unchanged. Group 10 changes only CTR: all vertex/state/
texel multisets match, and a captured 60-frame CTR replay matches every full
VRAM/mask hash plus scanouts 10/30/60, confirming legal reordered work rather
than a detected raster change.

## Final validation

- Clean final 3DSX and CIA build, version 0.3.0, CIA title version 3072. Existing
  path-truncation/unused-code warnings and executable-stack linker warning
  remain. No new build failure remains.
- Host controls/config/presets/reset/hotkeys tests pass under ASan/UBSan,
  including real input code and Detailed hidden-control/menu/footer checks.
  Renderer config tests cover normal visibility, both software defaults,
  per-game/global isolation, preset precedence and reset.
- Audio resampler sanitizer checks and stats formatter/cache checks pass.
  GPU coordinate/held-scanout sanitizer regressions pass.
- Native renderer fixture assertions pass modulation/CLUT invalidation, signed
  dithering, option changes, flipped wrap and additive/STP masks. Polygon
  sampling still differs from P.E.Op.S. on 8.80% of fixture pixels; the known
  mask-check + half-blend approximation differs on 5.33%. Both are retained
  comparison results, not exact-raster passes.
  Batch ordering fixtures disjoint/overlap/capacity/area-mask-upload/vertex-wrap
  all match both scanout and full VRAM/mask bits against the prebatch renderer.
- PICA worker matrix: five games × off/on/repeated-on/stress × 600 frames,
  five live toggles and six save/load round trips per stress case, with queue
  assertions. Every frame matches normalized triangle/state/texel content.
  Ordered hashes can differ because drains split shared solid/half batches and
  legally reorder disjoint additives; all ordered hashes remain recorded in
  [gpu-worker.json](release-validation/gpu-worker.json). The content comparison
  preserves triangle topology and normalizes half to opaque only for uniformly
  alpha-255 triangles. It does not prove real PICA raster/cache correctness.
- **Software parity:** reconstructed main ea8d8c9 software versus actual dual
  default software; all five 600-frame framebuffer checksums match for Old
  half-height/direct and New full-height/threaded configurations. New runs log
  the software worker; Old runs do not. Candidate asserts zero PICA draws and
  a NULL PICA owner hook. [Pixel results](release-validation/software-pixels.json).
- Patches apply cleanly from upstream; regenerated sources match; diff checks
  pass. Main ea8d8c9 and b74aabb are ancestors of the release branch.

Reproduction (no hardware): source devtools/env.sh; `make -j16`; then
`make cia BANNERTOOL=/home/tony/Development/PS1_3ds/devtools/tools/bannertool/output/linux-x86_64/bannertool MAKEROM=/home/tony/Development/PS1_3ds/devtools/tools/Project_CTR/makerom/bin/makerom`.
Run `bash psx3d/tests/host/run.sh`, `bash tests/renderer_config.sh`,
`python3 tests/gpu_thread_sim.py --sim /tmp/release-thread-check`, and
`python3 tests/software_renderer_sim.py --sim /tmp/release-software-check`.
Simulator scripts copy only the needed core/SDK/states; delete your own copies
when finished. Game data and devtools are local, ignored prerequisites.
The host UI script renders mockups; tool-version raster metadata may differ.

Local final artifacts are psx3d/PSX3D.3dsx, PSX3D.cia and PSX3D.elf:

- `PSX3D.3dsx`: `0744e2f2ce1b0c8599fc124591272719d652531ab0f8d2f3d6027f7b9cf907d2`
- `PSX3D.cia`: `874e6a8914601217f38d47b385d2f82be43302e89536f9ae98e85d856a04cfff`
- `PSX3D.elf`: `e5b30b79ae70b4a175e17df952d74eb4a7cc613ce37fe81f33856605c79f261c`

Every frontend key, per-game key, preset field, registered core key (including
locks/hidden keys), startup switch and active debug bit is listed in the
[settings inventory](release-v0.3.0-settings.md).

## Known issues and open risks

GPU remains experimental: residual clipping/coverage, PS1/PICA interpolation,
mask-check half/quarter blend approximations and game-specific readback/stereo
limits remain. Source fixture results are not blanket game correctness approval.
Software real 3D remains debug-only. The complete release binary has no new
hardware performance/stability verdict; instruction-cache layout changes matter
especially on Old 3DS, and fake GPU tests cannot validate GX scheduling/cache
coherency or citro3d thread affinity on hardware.

**Old 3DS save-state thumbnails may be missing (“No preview”).** An external
combined-branch check successfully saved Crash reference slot 2 into spare
slot 9 and loaded it; state data/gameplay worked without a thumbnail. This is
one memory-budget acceptance result, not a final release or long-session test.
Cross-renderer state loads, low-memory repeated transitions, profile-triggered
startup reload and actual blank-screen/backlight/OS lifecycle need hardware
acceptance. Audio smoothing lowers pitch and can still underrun below half
speed or during long I/O stalls. No curated profile enables GPU or promises
untested Old FF performance.

## Hardware acceptance checklist (not executed)

Use both renderers on **both console families**. Coordinate console ownership
before testing; preserve reference slots, use a spare slot, and capture a fresh
screenshot after each input step. This report grants no deployment performed
by the integration worker.

| Console / renderer | Reference to compare |
|---|---|
| Old / Software | main v0.2.0 behavior, half-height 3D, full-height HUD/2D, no renderer worker; no new FPS target |
| New / Software | main v0.2.0 behavior, full-height and original software renderer thread; no PICA owner hook |
| Old / GPU experimental | 9b4dc04 slot 2, frameskip off: Crash **40.7**, CTR **25.5**, GT2 **28.7 FPS** |
| New / GPU experimental + worker | slot 0, skip off: SH **~58.5**, CTR **~55.8**, Crash **~58.7–60 FPS** |

1. Fresh config and unknown game must select Software on Old/New. Confirm the
   normal global and per-game renderer labels, reopen-to-apply behavior,
   persistence, global/game resets and testing-switch precedence. Verify no
   seeded profile opts into GPU. With a temporary test profile only, verify
   per-console choice and bounded startup reload; remove it afterward.
2. For each renderer/console check Crash, SH, R4, CTR, GT2 gameplay, motion,
   edge polygons, fog/flashlight/STP, HUDs, track textures, 24-bit movies and
   loading/fade transitions. Software must match v0.2.0 appearance; Old half/full
   polygon resolution must preserve full-height text/2D. Check stereo/depth,
   smoothing, 4:3/stretch/widescreen and left/right screen sizes.
3. Benchmark GPU skip-off references at the same normal clock, slot, slider,
   streaming conditions and compiler. Record the last 15 one-second samples
   after warmup, repeat back-to-back. On New compare worker off/on/on/off using
   the same binary; verify core-2/PICA enable logs. Old must never create a
   PICA worker, even with gputhread. Compare keeper on/off/on with
   gldbg=131072, and batching with gldbg=65536; real hardware judges speed.
4. Test default auto, fixed and disabled frameskip, especially holding a skipped
   picture without black flashes. Check default-policy gameplay speed and
   responsiveness separately from skip-off benchmark FPS. fs3 stays test-only.
5. Exercise touch slots/remaps/stick, empty/one/two/four-button layouts, New
   left/right placement, serial profile changes and feature gates. Cycle
   Off/FPS/Detailed: compact footer shares controls; Detailed hides their touch
   targets but MENU and footer remain reachable. Physical/hotkey inputs work.
6. FF toggle/hold and permitted rewind: no stuck action after menus, HOME,
   sleep, disc changes or capability revocation; FF sound muted. Test sound
   smoothing off/on on slow Old scenes and full-speed New scenes, NDSP/CSND
   if needed; compare core audio capture within the same SPU-thread mode.
7. Save/load repeatedly into a spare slot, autosave/continue, reset, disc swap
   and alternating games/renderers. Save in GPU/load in Software and vice versa.
   Verify position and actual restored picture; record missing thumbnails
   separately. Stress Old memory over multiple saves/game switches and a long
   session; watch OOM/LUT-map/state allocation errors.
8. In every renderer/console combination test menu/resume, covers/music after
   game exit, backlight idle/wake, HOME, sleep/wake, app exit and CIA/HBL launch.
   Check PICA drawing stays on its owner and software rendering is unaffected
   by GPU-only switches. Never press HOME during a netload transfer.
