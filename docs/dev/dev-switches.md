# Developer switches and diagnostic paths

Audited against v0.5.0 for F11 (2026-10-22). These are private development
interfaces, not player settings. `main.c` parses startup words from argv and
`sdmc:/3ds/PSX3D/switches.txt`; `devswitch.c` validates the subset accepted by
remote `load`, `set` and batch lines. Remote overrides are restored before a
local launch. Unknown retired words are rejected remotely and ignored at startup.

## Runtime inventory

| Words | Purpose / disposition |
| --- | --- |
| `remote`, `token=`, `bench`, `bench-exit=home`, `update=` | On-card authorization, startup batch/exit and self-update handoff; retained remote loop. Never put the token in logs. |
| `game=`, `state0`, `state=0..9` | Select benchmark game/state. Protected Old slot 2 and New slot 0 are load-only. `game=` is also the remote load selector. |
| `seconds=`, `warmup=`, `shot=` | Batch envelope, consumed by devcontrol; not emulation options. |
| `record`, `record=on/off`, `record_seconds=1..60`, `replay=N` | Deterministic input clip capture/replay (`clip.c`); retained for correctness workers. |
| `stats`, `prof`, `pmu=AA,BB`, `pmuclass=on/off`, `pmurender=on/off` | Frame statistics, CPU/event counters, renderer-phase/class attribution. In use by old-speed, renderer-icache and dynarec workers. |
| `vramdump=N`, `acap`, `autosavetest=N` | Exact GPU VRAM dump, core audio checksum, periodic-save test into spare slot 9. In use by renderer/audio/save workers. |
| `frameskip=off/on/auto`, `noskip`, `fs1`, `fs3`, `ff=on/off` | Per-run frameskip and fast-forward comparisons; smart production frameskip is always used for Auto. |
| `o:KEY=VALUE`, `clock=N`, `cdra=N` | Up to eight core overrides, PS1 clock and CD read-ahead overrides. Tuning and microbenchmark interfaces. |
| `wsbundle=on/off`, `wsscreen=4:3/stretch/wide` | Bundle correctness and presentation comparisons (game-tuning and GPU fixtures). |
| `slider=N`, `stereo=0/1/2`, `o3ds` | Eye-capture/3D and console-path diagnostics; `o3ds` alone does not emulate Old clock/cache. Used by stereo/issue4 workers. |
| `nonet`, `csnd`, `nosound`, `noirrst`, `noaa`, `nolcd`, `nocovers`, `minimal` | Startup subsystem isolation (minimal combines the six `no*` flags); fallback audio and library/cover diagnostics. |
| `nogputhread`, `gputhread`, `nosputhread`, `gpuasync`, `nogpuasync`, `gpuflush`, `nogpuflush`, `aa` | Worker/cache ownership, synchronization and Old supersampling isolation. Retained for hardware fault investigation. |
| `presets_url=` | Startup numeric-LAN preset-update test endpoint; retained by preset-update tests/work. |
| `renderer=gpu` | Compatibility no-op for current heavy batches and remote clients. GPU is the only renderer. `renderer=sw` removed. |
| `idle=on/off` | Reference comparison for production idle skipping; retained by dynarec-mem/split and tuning. |
| `rtt=old/new/auto`, `rttcache=old/new` | Reference RTT and region-cache modes; retained while renderer-icache/MGS work is open and by `tools/gpu-compare/rtt.py`. Reload before switching cache modes. |
| `submitstate=old/new`, `submitclip=old/new`, `submittex=old/new` | Submission reference paths; retained for renderer-icache's gpu_pica baseline and tracked regress/replay fixtures (`PICA_SUBMIT`). |
| `pica_prims_old/fast/copy/clip/round` | Primitive reference paths; retained for the same open renderer source/layout and `primitive-bench.c`/regress/replay fixtures (`PICA_PRIMS`). |
| `texmask=old/new`, `texmemo=old/new`, `skipgate=old/new` | Texture/skip reference oracles in tracked regress/replay fixtures and the open renderer-icache baseline. Remove together with those oracles after the renderer work closes. |
| `gldbg=N` | Stable diagnostic bit ABI, enumerated below. Unknown historical bits do nothing in this branch. |

Removed families: `frontend=old/new` and its always-redraw path;
`audio=old/new` and float Hermite; `fsmart=old/new`, `rtfs`,
`rtfs=old/new/model/dense`, `rtfscap=`, `rtfslevel=` and the obsolete predictive
controller in the game loop; `showright`/`sbs` (write-only variables);
`idleptr=on/off` and `idledelay=on/off` (rejected research, no accepted gain).
The dormant **core** pointer/countdown implementations remain off: open
`t3/dynarec-mem` and `t3/dynarec-split` change that translation unit and its
layout pin. This cleanup does not remove their code underneath them.

The branch/brief audit used `devtools/briefs/{common,old-speed-common,QUEUE}.md`,
`t3/renderer-icache`, `t3/dynarec-mem`, `t3/dynarec-split`, `t3/movie-speed`,
`t3/game-tuning`, `t3/issue4`, `t3/multiplayer`, `t3/save-safety3`, and
`t3/ui-impl`. The retained renderer controls still have executable test callers;
they are deliberately deferred, not asserted to be dead. No new switches are
introduced for an accepted net improvement.

## Renderer bits

`core/plugins/gpu_pica/gpu_pica_debug.h` owns the numeric ABI. Values stay fixed
for private workers; all production bit tests now use names.

| Value / `PICA_DEBUG_` name | Diagnostic |
| --- | --- |
| 32 `VRAM` | Show full VRAM instead of display rectangle |
| 1024 `NO_DRAW` | CPU renderer cost without GPU draws |
| 2048 `SKIP_COMMANDS` | Parse command stream without drawing |
| 4096 `NO_PRIMITIVES` | Decode primitives without emitting triangles |
| 8192 `NO_SPLIT` | Suppress command-list split diagnostic |
| 16384 `FLIP_READBACK` | Readback row orientation comparison |
| 32768 `NO_MASK_PASS` | Suppress extra mask/blend pass |
| 65536 `NO_MERGE` | Disable batch merging |
| 131072 `REFERENCE_CACHE` | Reference fill/texture/upload paths |
| 262144 `RTT_OLD` | Original RTT path |
| 524288 `NO_REGION_CACHE` | Reference single-region cache |
| 1048576 `RTT_NEW` | Force new RTT before detection |
| 4194304 `EAGER_FRAME` | Start frame while parsing commands |
| 8388608 `PHASE_TIMING` | Renderer phase timing |
| 16777216 `PACK_COPY` | Primitive packed-copy experiment |
| 33554432 | Retired: the coarse drawn-area comparison is always on (renderer round two) |

## Other debug paths and build controls

- `devnet.c`/`devcontrol.c`: authenticated remote status, logs/files, load/set,
  batch/stats, screenshots (including eyes), UI/UI-key, state import/export,
  replay and self-update. Kept: this is the worker control plane. Server scope
  and transport policy are separate from F11.
- `clip.c`, `benchstate.c`, `devupdate.c`: recorded input/digests, synthetic
  state transfer and private-LAN CIA install; kept for remote verification.
- `PICA_SIM_HASH`, `PSX3D_SIM`, `PICA_TIMERS`: simulator checksums, cache lifetime
  diagnostics/`PICA_DUPS`, and optional renderer timers; shipping builds omit
  simulator diagnostics. `PICA_DEBUG`, `PICA_SUBMIT`, `PICA_PRIMS`, `TEXMASK`,
  `TEXMEMO`, `SIM_*` and `GPU_*` environment variables belong to host fixtures.
- `PICA_LAYOUT` production build is fixed to `split`; rejected `baseline`,
  `section`, `os` and per-build layout generation are removed from Makefiles.
  Private host layout experiments remain under tools/.
- `SAFEFILE_TEST`, `PRESETUPDATE_TEST`, fake SDK headers: host-test injection,
  absent from releases. `THUMB_URL` and `PRESETS_URL` are test endpoint macros.
- `SDK_CHECK=0` is the explicit SDK-upgrade escape hatch; `EXTRA_DEFS` supplies
  private compile definitions. `PSX3D_PS1_OVERCLOCK_PRESETS` is shipping policy.
- `PSX3D_BUILD` defaults to `PSX3D_VERSION` (release status `build: "0.5.0"`).
  A private build can use `EXTRA_DEFS='-DPSX3D_BUILD=\"name\"'` after removing
  stale frontend objects. `settings.build` stays a string; protocol/version
  keys and legacy status keys remain compatible with existing remote tools.
- `NDRC_LAYOUT_PAD=1908` stays: core-first isolates frontend growth, not changes
  inside new_dynarec.c. Removing it shifts later hot core functions by 1908 B.
  The COP decoder fix changes one literal and preserves every core code address.
