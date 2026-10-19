# GPU-only rendering

Branch `t3/remove-sw`, based on main `9417930`. Software rendering is removed by
user decision. Neither source nor builds include `gpu_unai` or dual dispatch.
Evidence is in the main checkout's ignored `devtools/remove-sw/` directory.
Nothing was pushed or published.

## Inventory and implementation

- Root make builds `CTR_GPU=pica`; ctr core builds select PICA. Deleted
  `core/plugins/gpu_unai/`, its build rules, namespaced interface and
  `renderer_dispatch.c`. `psx3d_renderer_is_pica()` is constant true. Generic
  gpulib queue predicates therefore compile down to their GPU branch.
- Deleted the renderer config field, setter/resolver, global/per-game menu
  rows, restart-after-serial-identification logic, and the preset renderer
  column/enums. All six SotN regional profiles retain their capabilities and
  use GPU. Existing `renderer=sw` config/game keys are ignored without error.
- Deleted unai core registration, callbacks/settings, Old fast-lighting
  default, user help and stats lighting display. Legacy unai overrides may
  remain in INI files, but options_get returns NULL and nothing consumes them.
- Deleted half-height Software drawing settings and the unai right-eye
  conversion/uploads. Retained RGB24 CPU staging, movie presentation and
  thumbnail sampling: these also serve GPU movies. GPU thumbnails retain
  the existing tiled-scanout readback path.
- `renderer_is_gpu()` now describes whether GPU game buffers are active;
  guards around it in video/cache code remain necessary while library/UI
  frames run without a loaded game. Stats retain GPU activity counters.
- Remote/startup `renderer=gpu` works; legacy `renderer=sw` is accepted and
  **ignored**, so old psx3dctl/fastbench batches run GPU. Remote status reports
  `gpu` while playing. Host client tools need no parser changes. Updated
  developer documentation and regression shims; deleted Software-only simulator
  test and ported the GPU-thread test to the vendored core.
- README states GPU-only policy, marks Software numbers as historical and
  updates credits. GPL LICENSE/core COPYING and PCSX-ReARMed credits remain.

## Stereo integration contract

Stored New 3DS `stereo=2` survives config load/save, including without debug.
It displays and renders as **Screen depth**, the existing GPU-supported mode.
The menu offers Off/Screen depth until GPU real 3D lands. Old hardware continues
its existing stereo lock. Core entry points in `psx3d_depth.h`, PICA's stereo
hooks, and frontend `stereo_init/prepare/draw/exit` remain intact for
`t3/gpu-stereo` to implement.

`stereo.c` draws a depth mesh independently of unai's rasterizer, but depends
on the core's depth-map producer (`psx3d_depth_get`) and overlay API. PICA
currently supplies NULL/no-op hooks, so preserving the mesh alone cannot give
real 3D. Its interface remains available; the removed unai per-eye presentation
must be replaced by the stereo worker's GPU eye textures/geometry.

## User-visible losses and mitigations

- No Software fallback for inaccurate GPU scenes. Report affected games;
  improvements now belong in gpu_pica. Historical benchmark figures do not
  establish pixel accuracy.
- Unai transparency/lighting/old-renderer and half-height controls disappear.
  GPU already supplies its own lighting/transparency and draws full-height.
- Software-specific centering controls disappear. GPU centering/interlace
  limitations remain documented in README.
- Experimental Software real 3D is unavailable until GPU stereo integration.
  Existing mode 2 falls back to screen depth without destroying its stored
  value. The normal 3D slider, UI parallax and screen depth remain.

## Validation

Main and candidate were built with devkitARM GCC16.1, split PICA layout,
version0.3.1/title counter3076. Candidate remote identity is `remove-sw-1-split`.
The pristine main build uses an archived 9417930 tree in the evidence directory.

Six 600-frame ARM simulator runs match **every one of 3600 per-frame draw
hashes**, with final hashes:

| State | Draw hash (main = candidate) |
|---|---|
| Crash o3auto | c15806a28d454f0d |
| Silent Hill | 2ec86777a402338c |
| R4 | 97b524f3c526c8cd |
| CTR | c97e59920c262b7a |
| GT2 | 42140bec85154fa8 |
| SotN | 221910a7e8aeb7a1 |

The simulator was copied privately and rebuilt from each tree's vendored core;
shared simulator sources were not changed. See `setup-sim.py`, `run-hashes.py`
and `hash-comparison.json`. These checks establish unchanged GPU command output,
not hardware speed or complete picture accuracy.

Host validation includes legacy config migration, both-console settings and
preset visibility, developer transport/batches/updater, controls and reset
coverage, stats formatter, and actual presentation/held-frame/stereo call counts.
ASan/UBSan are used by the developer/UI/presentation suites. GPU-thread lifecycle
validation is recorded separately in `thread-tests-final.log`:
all20 direct/worker/repeated-worker/toggle+save runs finish600frames. The
aggregate content-equivalence assertion **fails for R4 stress**. A pristine
main9417930 PICA build reproduces this same off-versus-toggle/save divergence
at frame52, just after the first frame50 save/reload. See
`baseline-thread/result.json` and `baseline-thread-stress.log`. This is an
existing stress-path limitation, not a passing test; it is outside this removal.
Ordinary R4 off/on/on2 draw hashes match. Ordered CTR/GT2 worker hashes can
change with batch splits; the mandatory main/candidate direct GPU hashes above
are unchanged. No fix to save-state/thread semantics is included.

## Size (final package)

| Measure | Main | GPU only | Delta |
|---|---:|---:|---:|
| .text | 2,146,052 | 1,789,684 | −356,368 bytes (−16.61%) |
| .rodata | 329,324 | 313,540 | −15,784 bytes |
| .data | 114,404 | 108,044 | −6,360 bytes |
| .bss | 13,088,264 | 13,067,328 | −20,936 bytes |
| ELF file | 6,376,508 | 5,790,916 | −585,592 bytes |
| CIA file | 8,623,040 | 8,475,584 | −147,456 bytes |

The ELF includes debug sections; it is not resident-memory usage. CIA packaging
includes alignment/compression. No hardware speed claim follows from size alone.
See `size.json` and the raw section listings. The final source is1406b61; documentation-only commits follow it.

## Hardware status

**Pending, not accepted as a performance change.** Old sessions were refused
because other workers held `lock.old` (first settings-ui, then the combined benchmark session). New sessions were refused because
`fastloop/NEW_UNAVAILABLE` says the sh-garbage install probe timed out. No
console connection, install, screenshot, input, or state write was performed
by this test. Thus no A/B/A fps, New screenshots, restoration, or final
console identity is claimed. Existing states and console ownership were untouched.

Hardware package: immutable CIA/3DSX/ELF and SHA256SUMS under
`devtools/remove-sw/artifacts/`, `four.txt` and `hardware.py`, plus
`PENDING-HARDWARE.md`. Run only after the lock/availability/handoff rules permit.
Old: exact RC2 → candidate → exact RC2, Crash/CTR/GT2/SH, GPU/off, slot2,
warmup5/sample20s/PMU0,2. Require no game slower by more than approximately1%
versus the bracketing RC2 mean. New: library/settings/SotN gameplay and game
settings screenshots on GPU, slot0 load-only. Restore exact RC2 on both and
verify library/remote idle with `shot both`. SotN8 is load-only; no save tests
are needed for this removal. No HOME/FTP/InputRedirection/reboot/app-exit.

The code-size removal cannot have an off switch in a combined binary: its
layout effect requires comparing actual RC2 and candidate binaries. This follows
the task's explicit removal and A/B/A requirement and the product decision to
remove old paths, rather than retaining dual rendering for a development switch.

## Commits

- `316046b`: remove unai and dual dispatch/build linkage.
- `9005b3d`: remove frontend/config/preset selection and Software presentation.
- `6848ad9`: remove obsolete help and port validation fixtures.
- `026809a`: force the ctr core to PICA even with legacy build arguments.
- `1406b61`: preserve existing CPU movie scanout alignment.

The final CIA SHA256 is `e88a9a61fd45c80e4eab6ff303214a394c5d57a5a47ce151e3303280ff0284c3`.
The package preserves counter3076; do not use an earlier intermediate build.
