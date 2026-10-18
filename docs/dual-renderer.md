# Dual renderer implementation and verification

Based on `gpu-renderer` commit `30c62a9`. Both gpulib renderers are included in
one frontend. GPU remains the default. No console was contacted or modified;
no changes were pushed.

## Selection

Set `debug=1` in `sdmc:/3ds/PSX3D/config.ini`, then open **Settings → Core
options → Renderer** in the library or in-game menu. Values are **GPU** and
**Software**. Close and reopen the game to apply a change; reset and state load
do not switch a running game's renderer. The option's help explains this.

- Library changes set the global default (`renderer=gpu|sw` in `config.ini`).
- In-game changes immediately save `renderer=gpu|sw` in
  `sdmc:/3ds/PSX3D/games/<game>.ini`, alongside screen/rewind/control settings.
  The global default is restored on leaving the game. This field can be used
  by future profiles; no profile system is introduced here.
- `renderer=sw` or `renderer=gpu` in `switches.txt` (or launch arguments) wins
  over both settings without changing either saved value. Remove the switch
  to test menu selection. The log reports the renderer actually started.
- Without `debug=1`, the setting is hidden; saved choices still apply.

The root `GPU=pica` / `GPU=unai` frontend build modes were removed deliberately.
Passing `GPU=` now gives an explicit migration error. Use plain `make` and the
runtime setting. The core still supports `CTR_GPU=pica`, `unai`, and `dual`;
the frontend always requires `dual`. Clean once when updating an existing
single-renderer checkout, since make does not track changes to compiler flags.

## Interface and lifecycle

`core/Makefile` prefixes the nine `renderer_*` entry points of each renderer
with target-specific preprocessor definitions (`pica_renderer_*` and
`unai_renderer_*`). There is one copy of gpulib, its `GPU*` entry points, and
its CPU VRAM. `plugins/gpulib/renderer_dispatch.c` owns the public renderer
interface. Selection is accepted only before `GPUinit`/after `GPUshutdown`;
an attempted live change returns failure.

Dispatch uses direct conditional calls/tail branches at command-list and
renderer-control boundaries. There is no selection test or indirect call per
primitive. PICA's stereo stubs are omitted in dual builds; software supplies
the stereo hooks, with stereo activation blocked for PICA. gpulib guards
PICA-specific deferred fills/readbacks/scanout and enables its worker only for
Software. Software retains the New 3DS GPU worker and Old 3DS main-thread,
half-height polygon rendering. The async declarations now have C linkage and
the software adapter sees `USE_ASYNC_GPU`, so its stereo synchronization
actually waits for the worker before changing/freeing its buffers.

Frontend presentation, frame callbacks, statistics and VRAM dump behavior use
the active renderer, independently of pending settings. All `PSX3D_PICA`
conditionals in `psx3d/source` are gone. PICA's shader, render target, atlas,
staging texture and vertices are initialized for GPU games and freed after
waiting for GPU completion. Software right-eye presentation buffers are lazy.

Game restart provides a fresh `GPUinit` for the selected renderer. Save states
retain the existing renderer-independent gpulib registers/VRAM format. PICA
state loads discard stale deferred fills, uploads, readback ownership and the
last texture shortcut, then queue the loaded VRAM for upload. Software stereo
buffers are released at shutdown, and frontend pending frames and stereo
activation are cleared between games. Partial PICA initialization also cleans
up its allocations.

## Code size and memory

Production devkitARM GCC 16.1, root `make -j16`, `arm-none-eabi-size` on the ELF:

| ELF section (bytes) | `30c62a9` GPU-only | Dual | Increase |
|---|---:|---:|---:|
| text (includes read-only data) | 1,931,672 | 2,306,688 | 375,016 |
| data | 94,432 | 101,816 | 7,384 |
| BSS | 21,379,568 | 21,400,328 | 20,760 |
| total | 23,405,672 | 23,808,832 | 403,160 |

The process image grows about 394 KiB. Most of the software renderer's lookup
tables are read-only data counted in text. Its two coarse depth maps account
for 19,212 bytes of BSS; there is no second base VRAM allocation. The common
VRAM mapping remains 2,101,248 bytes including overdraw/guard space.

Renderer-specific large allocations (payloads; allocator/driver overhead is
additional):

| Allocation | Before | Dual |
|---|---|---|
| PICA PS1 render target | 1 MiB VRAM from app startup to exit | 1 MiB VRAM only during GPU games |
| PICA atlas / staging / vertices | 4 MiB / 256 KiB / 192 KiB during games | Same sizes, GPU games only |
| Software stereo flag ring | 256 KiB static in software-only builds | 256 KiB heap only while software real 3D is active |
| Software second VRAM | 2,105,344 bytes, lazy | Same size, lazy; released at shutdown |
| Software frontend right-eye linear buffer / texture | 1 MiB each from startup in software-only builds | 1 MiB each only when real 3D is requested; released at game end |
| Async software command queue | About 128 KiB plus bookkeeping, when enabled | Same, Software only; disabled on Old 3DS |

The common frontend still reserves a 1 MiB citro3d command buffer, as the old
GPU build did (software-only previously used 256 KiB). Both renderers share
the frontend's RGB565 frame buffers/texture, also needed for GPU movie output.
GPU mode gains no inactive software VRAM, ring, worker queue or right-eye
texture allocation. Software mode gains no PICA render target or atlas.

Old 3DS heaps continue to be sized from the memory remaining after the process
image, with the existing 8 MiB mapping reserve and linear heap minimum. Thus
the approximately 394 KiB image increase consumes available memory rather
than requiring another renderer's multi-megabyte buffers. These are ELF and
allocation-size checks, not measured console free-memory readings; actual
Old 3DS memory headroom and allocation success still need hardware validation.

Production compiler object sections for `renderer_do_cmd_list`, `emit_tris`,
`decode_rect`, `texture_for`, `sprite`, `fill_rect`, `draw_line` and
`draw_poly_slow` were compared with `30c62a9`: all eight have byte-identical
instructions/data (the command-list symbol is renamed). The direct list
wrapper adds one renderer-selection boundary; gpulib also tests async/renderer
state outside the primitive loop. The larger binary changes layout, so Old
3DS I-cache performance cannot be established by simulator timing. A console
benchmark is still required; no hardware speed claim is made.

## Verification

- Clean dual 3DSX build passed. CIA packaging passed with the specified
  bannertool and makerom. Artifacts are `psx3d/PSX3D.3dsx` and
  `psx3d/PSX3D.cia` in this worktree.
- Both patches applied to a clean archive of the upstream core; patch 0002
  also passes reverse-apply checking. No changes to the submodule revision.
- `tests/renderer_config.sh` compiles the production config/options/gamecfg
  files on the host using small SDK type stubs. It passes GPU default, Old
  3DS half-height default, debug visibility, option cycling, persistence,
  per-game override, global restoration, and v0/v1 option re-registration.
- Simulator was copied to `/tmp/dualrender-sim`. The requested parent core was
  reconstructed using the committed patches at `30c62a9`; both builds used
  the same simulator platform and Linux `psxmem` adaptations, `-O2`, fake
  citro3d, `PICA_SIM_HASH`, `SMALL=1`, `o3hw_off.ini`, and 600 frames.

| State | `30c62a9` GPU draw hash | Dual GPU draw hash |
|---|---|---|
| Crash `o3auto.st0` | `6ba7ef2f6e06cf04` | `6ba7ef2f6e06cf04` |
| Gran Turismo 2 `gt2.st0` | `7b3a81441ad9b8aa` | `7b3a81441ad9b8aa` |

- Software completes 600 Crash frames with `HALF3D=1`. Its final RGB565-derived
  PPM is byte-identical to a `30c62a9` unai-only simulator build.
- A single-process cycle test completed GPU → Software → GPU → Software,
  600 frames each, passing the preceding renderer's serialized state to the
  next run. Every run also serialized/unserialized its own state and ran
  another 60 frames. Attempts to change selection while the GPU was live
  were rejected.
- Software with the async worker and real stereo enabled completed 600
  frames plus a state round trip and 60 further frames.

Simulator runners and full logs remain under `/tmp/dualrender-sim`:
`runner_dual.c`, `runner_cycle.c`, `runner_stereo.c`,
`baseline-exact-{crash,gt2}.log`, `dual-crash.log`, `dual-final-gt2.log`,
`baseline-software-crash.log`, `software-crash.log`, `software-stereo.log`,
and `cycle.log`. The runner selects the backend before `retro_init`; software
runs skip PICA allocation/frame callbacks. Fake citro3d verifies command output
and lifecycle, not actual PICA rendering, shader compilation or 3DS allocation.

## Rebuilding patch 0002 without private tooling

All core changes remain under the existing regeneration paths. No extra core
path needs adding to `devtools/mk.sh`. New files must be visible to `git diff`
through intent-to-add (never commit the patched submodule):

```sh
git -C core add -N libpcsxcore/psx3d_depth.h libpcsxcore/psx3d_prof.h \
  plugins/gpu_pica/gpu_pica.c plugins/gpu_pica/gpu_pica.h \
  plugins/gpulib/renderer_dispatch.c
git -C core -c diff.mnemonicPrefix=false -c diff.noprefix=false diff -- \
  Makefile Makefile.libretro frontend/libretro.c libpcsxcore plugins \
  > patches/0002-psx3d-widescreen-and-stereo-depth.patch
make -j16
```

## Console owner checks (Old and New 3DS)

1. Launch the same game/state with each renderer, confirm the logged selection
   and compare the picture, movies, display blanking, audio and input. On Old
   3DS, check Software's half/full 3D resolution option and frameskip.
2. Enable debug options; change Renderer in-game, confirm it keeps rendering
   normally until closed, reopen and confirm the new choice. Check that a
   second game still inherits the global default. Repeat the library default
   change and `switches.txt` override; remove the override afterward.
3. Save under GPU, reopen under Software and load it; save under Software,
   reopen under GPU and load it. Check manual slots, autosave/continue, reset,
   and repeated loads. Use a separate manual slot to preserve reference states.
4. Repeatedly alternate games/renderers, open/close menus, return to the
   library, and check covers/music, smoothing, screen modes and the 3D slider.
   Exercise software real 3D on New 3DS, including toggling it during play.
5. Watch allocation failures/free memory on Old 3DS and benchmark GPU mode
   against `30c62a9` with identical state/options. The hot renderer instructions
   are unchanged, but the larger binary's I-cache layout must be judged there.

## GPU submission HEAD integration

`t3/dual-renderer-on-head` rebases the renderer selection onto `9b4dc04`.
GPU mode retains double-buffer callbacks, readback/texture waits and targeted
cache maintenance. The frontend reserves then releases a contiguous 1 MiB
VRAM hole during screen-target allocation: allocating the supersampled screens
first otherwise leaves no suitable block for the lazy PICA target on hardware.
The failed pre-fix New 3DS launch and subsequent successful Crash run are saved
in the local test artifacts. Hardware measurements are in
`devtools/swgpu_results.md` (ignored).
