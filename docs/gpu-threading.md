# New 3DS PICA command worker

Implemented on top of `de1ab9d` (which includes `9b4dc04` and the subsequent
runtime renderer selector). No console was contacted during implementation.
See [GPU submission](gpu-submission.md) for the command/vertex banks, texture
lifetime fences, targeted GPU cache cleaning, and measurement caveats.

## Ownership

PICA reuses gpulib's existing `gpu_async.c` command ring and `PCSXRT_GPU`
thread. On New 3DS, `pcsxr_sthread_create` pins that thread to **core 2**.
Command parsing for PS1 timing, status-register mirrors and frameskip stays
on the emulation thread; texture decoding, primitive/vertex construction,
citro3d state recording, GPU submissions and readbacks run on the worker.

The worker also runs `video_present`, including citro2d overlays, both eyes,
and the bottom screen. The emulation thread submits a synchronous callback
and waits until it returns. Menu presentation uses the same callback. Thus
there is one CPU owner of the global citro3d/GX recording context throughout
threaded gameplay. There is no parallel frontend recording and no second
citro3d context. The existing GSP completion callback remains unchanged.
Initialization before thread startup and resource destruction after its join
remain on the main thread. Runtime disable drains and joins before returning
GPU ownership to the main thread; enabling hands ownership back while idle.

The callback mailbox runs only after a full command-ring drain and renderer
flush. Arguments may be on the caller's stack because completion is synchronous.
Nested hooks on the worker execute locally (`sthread_isself`); internal renderer
calls use direct local functions, without thread dispatch in primitive or
texture loops. Failure to allocate/create the thread leaves the normal direct
path active and logs `gpu thread init failed`.

## Synchronization boundaries

- gpulib still owns the PS1 command lengths, timing, frameskip decisions,
  command buffering, and CPU status mirrors. Reading GPU status does not need
  to wait for physical 3DS rendering: gpulib already computed its visible bits.
- CPU VRAM transfers settle deferred fills on the worker before reading or
  replacing bytes. GPU-to-CPU transfers perform the existing rectangle readback
  there before `gp0` or `GPUreadData[Mem]` consumes CPU VRAM. Uploads either use
  gpulib's copied DMA payload or wait for the worker before main-thread writes;
  cache updates/uploads then execute on the owner.
- PICA VRAM-to-VRAM copies leave the async parser and use gpu.c's existing
  settle/readback/copy/update sequence. The software copy path is unchanged.
- PICA vblank/flip uses a **full** ring fence, not software's partial scanout
  fence or delayed flip. Offscreen drawing shares the same GPU context.
- Save drains the ring, settles fills and reads GPU drawing back before VRAM
  is copied. Restore drains before replacing state and does not leave queued
  ecmd work reading VRAM while the loader writes it. For threaded PICA,
  `GPUfreeze(2)` uploads restored VRAM **after** `LoadState` reads its bytes;
  the original unthreaded/Old 3DS load path remains in place.
- Reset drains before changing renderer registers. Option changes drain before
  stopping/starting the worker. Shutdown drains, clears the callback hook,
  signals exit under the queue mutex, joins, and only then frees queue/VRAM.
- `pica_picture`, stats, probe, frame hooks and VRAM dumps use owner callbacks.
  The frontend fences after `retro_run` before stereo controls, menus, UI text
  buffer reuse and stats. Snapshot/thumbnail access also fences; its existing
  software-framebuffer source is unchanged (this adds no PICA thumbnail feature).
- Presentation completes on the worker before main-thread configuration or UI
  work resumes. Its state, slider, config and draw-callback arguments are stable
  for that call. GPU completion waits protecting textures/staging and the
  double-buffer submission lifetime rules remain required and unchanged.

## Memory ordering

The queue is single producer/single consumer. Command payload is copied before
a release store publishes `pos_added`; the consumer uses an acquire load.
`pos_used` and `idle` are published/read the same way so ring reuse and idle
fast paths observe completed writes. Mutex/condition operations order mailbox
arguments, results, full drains and stop/join. The worker may flush when the
ring temporarily empties, but that does not change command order.

New 3DS normal CPU memory is coherent between these cores (shared L2 and
MPCore coherency); command-ring/CPU-VRAM handoffs need ordering, not per-packet
GSP IPC cache flushes. This does **not** make GPU memory coherent. The existing
vertex, decoded-texture, staging and command-list cleans still run on the CPU
that wrote them. Existing readback invalidations and GPU lifetime waits remain.
No backdoor, counter-register write or new cache-maintenance syscall is used.

## Controls and Old 3DS

New 3DS defaults to threading for either renderer. `nogputhread` in launch
arguments or `sdmc:/3ds/PSX3D/switches.txt` disables it; remove it or use
`gputhread` to restore the default. These switches are read at application
startup; relaunch to change them. They are independent of `nogpuasync` (GPU
submission overlap) and `nogpuflush` (targeted GPU cache cleaning).

The existing option test override also works:
`o:gpu_thread_rendering=disabled` / `o:gpu_thread_rendering=enabled`.
The `o:` parser already supplies the `pcsx_rearmed_` prefix: do **not** include
that prefix in the switch. Actual libretro updates to
`pcsx_rearmed_gpu_thread_rendering` also drain and toggle safely mid-game;
there is no new visible menu setting.

Old 3DS options force disabled even with an enable override, and the PICA core
independently checks `APT_CheckNew3DS` before creating a worker. It keeps the
original single-thread command/primitive/texture loops, GPU submission policy,
VRAM size and state-load behavior. No queue operations or per-primitive thread
checks run there. Frontend boundaries have a NULL-hook check per frame/call.
Code layout still changes between builds, so unchanged Old 3DS FPS must be
verified on hardware; the simulator cannot establish that performance claim.

## Local verification

Both `make -j16` (3DSX) and `make -j16 cia` pass with the toolchain in
`devtools/env.sh` and the specified bannertool/makerom. Patch 0002 contains all
core changes, including the new PICA thread header. Existing frontend renderer
configuration tests cover New/Old defaults, `nogputhread`, and option overrides.

The simulator runs real ARM/Linux pthreads under qemu-arm with fake citro3d.
It checks vertices, state and decoded texture samples, not final raster pixels,
GX execution, CPU cache behavior or frame time. The same executable is used
for off, on, a second on run, and a stress run. Stress toggles the actual core
option every 100 frames and saves/restores six times while a frame is open.
Queue assertions are enabled. Each case runs 600 frames from the same state
with frameskip off. All four modes matched, including the six save/restore
round trips and five live enable/disable transitions:

- Crash (`o3auto`): `6ba7ef2f6e06cf04`
- Silent Hill (`sh`): `244abb4a61d295c3`
- CTR (`ctr`): `497b85e24ef6ff72`
- GT2 (`gt2`): `7b3a81441ad9b8aa`

The assertion-enabled full matrix and logs are in
`/tmp/n3thread-sim-verified`; console output is
`/tmp/n3thread-sim-verified-results.txt`.

Reproduce without touching the consoles:

```sh
source /home/tony/Development/PS1_3ds/devtools/env.sh
bash tests/renderer_config.sh
python3 tests/gpu_thread_sim.py --sim /tmp/n3thread-sim-check
```

The script copies the PICA simulator, fake SDK and states from ignored
`devtools/sim`, installs this worktree's PICA/gpulib sources, enables
`USE_ASYNC_GPU=1` and `PICA_SIM_HASH`, and cleans before building. It adds the
frontend drain to the runner and uses `o3hw_off.ini` with only the thread option
changed. Use a fresh directory initially; `--reuse` reruns an already prepared
copy. Logs and hashes stay in that directory. The initial manual copy/run is
also retained at `/tmp/n3thread-sim`.

## Release v0.3.0 combined regression

Software is the default on both consoles; the PICA owner hook is installed
only for an opted-in GPU game. Software retains its original New 3DS renderer
thread and never initializes the PICA atlas, dithering resources or worker
presentation hook. Both menus and gameplay bottom callbacks go through the
same synchronous video_present owner dispatch when PICA is active.

The combined renderer adds legal additive reordering and solid/half batch
sharing. Consequently worker drains and save-state flushes can change the
ordered draw checksum without changing primitives. The updated regression
also compares **each frame's triangle topology, state, positions and sampled
texels** as a multiset, normalizing BL_HALF to BL_OPAQUE only for triangles
whose vertex alpha is uniformly 255. It checks all five scenes including R4,
with off/on/repeated-on and five toggles plus six save/load round trips. This
is a command-content check, not raster, cache or hardware validation. Independent
native raster ordering/mask fixtures and CTR captured replays cover batching.
See [the release report](release-v0.3.0.md) for the final results.

## Hardware acceptance (not yet run)

Reserve the consoles with the benchmark worker before starting. Use this
worktree's final **same binary** for on/off, New 3DS **slot 0**, normal clock,
frameskip off, GPU renderer, same slider position, same NTR streaming conditions.
Keep async submission and targeted flushing enabled in both runs. Clear any
conflicting `switches.txt` overrides, especially `nogputhread` before the on run.

1. Start with New 3DS at HOME, HBL icon selected, live NTR stream. Set `F` to
   this worktree's 3DSX so `try.sh` does not silently deploy the main checkout.
   Use the screenshot-checked `devtools/n3pmu.sh`; stop on any failed reference
   check and never press HOME during a transfer. The script runs roughly 40 s.
2. For each of `Silent_Hill`, `Crash_Team_Racing`, `Crash_Bandicoot`,
   `Gran_Turismo_2`, `Ridge_Racer_Type_4`, run off/on/on/off (save the log and
   screenshot immediately after each run because the next invocation overwrites
   them). Example, from this worktree:

   ```sh
   task_repo=$PWD
   task_devtools=/home/tony/Development/PS1_3ds/devtools
   F="$task_repo/psx3d/PSX3D.3dsx" GAME=Silent_Hill ST=0 \
     "$task_devtools/n3pmu.sh" 11,00 renderer=gpu nogputhread
   # Archive n3_Silent_Hill_11_00.log and n3_game.png with an off1 suffix.
   F="$task_repo/psx3d/PSX3D.3dsx" GAME=Silent_Hill ST=0 \
     "$task_devtools/n3pmu.sh" 11,00 renderer=gpu gputhread
   # Archive with an on1 suffix; repeat on2 and off2 for all five games.
   ```

3. Verify on logs contain `threadCreate: pcsxt 2 core 2` and
   `gpu thread: PICA enabled`; off logs must not contain the latter. Compare
   the **last 15** one-second samples: FPS, emu ms/frame, main-core M cycles/frame,
   late frames, presentation time, queue waits, readbacks, primitive counts and
   screenshots. `pmu_end` remains around `retro_run` on the main thread: worker
   cycles are intentionally excluded, while emu time includes the final drain.
   A lower main-core cycle count alone does not prove faster wall-clock speed.
4. Check transparency in Silent Hill, CTR's readback-heavy track picture, GT2
   and R4 textures/geometry, and Crash animation/input/audio. Capture each scene
   and compare to off. Check stereo slider low/high, bottom-screen overlay and
   menu pause/resume. Exercise 24-bit movies and loading transitions too.
5. In each game save to a **spare slot**, load slot 0, reload the spare slot,
   repeat, and compare pictures. Check rewind and fast-forward, fixed/auto
   frameskip, reset, closing the game and opening a different renderer/game.
   Repeat menu/restore and HOME/sleep/wake before testing clean application exit.
6. Install the final CIA once the netload runs pass; repeat Silent Hill/CTR
   and menu/save/load/HOME checks from the HOME-launched app. For Old 3DS,
   compare final CIA with the prior CIA on the same slot-2 scenes; verify no
   PICA worker even with `gputhread`/an enabled option override. Check menu and
   game picture sizes. This is a regression check, not permission to start it
   during the other worker's console reservation.

Expected benefit: overlap PICA CPU work with PS1 emulation. Silent Hill's
12.3 ms / 8.0M main-core baseline should move toward the 8.5 ms / 5.4M software
emulation cost, potentially recovering 60 FPS; this is a target, not a measured
result. CTR may improve less because required GPU readbacks serialize work.
Crash is already capped at 60 FPS, so look for CPU headroom rather than higher
FPS. Queue parsing/copies, RPC wakeups, main/worker imbalance and sharing core 2
with CD/audio helper fallback can limit gains. The fake GPU cannot validate
real citro3d ownership, scheduling, cache coherence or stability; hardware
acceptance is still required before calling the feature benchmarked.
