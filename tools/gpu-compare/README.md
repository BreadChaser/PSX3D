# GPU visual comparison

The capture runs P.E.Op.S. on the ARM simulator, records consumed GP0 packets,
E1-E6 state, CPU VRAM uploads and scanout rectangles, then replays the *same*
trace through native x86 P.E.Op.S. and `gpu_pica.c`. The latter uses a small
software implementation of the citro3d calls and the actual decoded atlas.
This is faster than rasterizing inside qemu and keeps GPU readback errors from
changing the game's subsequent command stream. `GPU_VERIFY=1` embeds reference
VRAM at every frame; the native reference replay must match all 524288 words.

Prerequisites: C compiler, Python with Pillow/numpy, ARM Linux cross compiler
and qemu from the project's devtools. No console connection is used.

```sh
# Follow the project instructions: initialize/patch core first.
git submodule update --init core && make patch
cp -a /home/tony/Development/PS1_3ds/devtools/sim /tmp/correct-sim
tools/gpu-compare/build.sh
python3 -m venv /tmp/correct-sim/venv
/tmp/correct-sim/venv/bin/pip install pillow numpy
/tmp/correct-sim/venv/bin/python tools/gpu-compare/compare.py \
    --frames 10 30 60 --output /tmp/correct-sim/before
# After editing/building the renderer: replay the exact old traces.
/tmp/correct-sim/venv/bin/python tools/gpu-compare/compare.py \
    --frames 10 30 60 --reuse-traces --output /tmp/correct-sim/after
```

`BUILD_DIR` and `PSX3D_DEVTOOLS` configure the build script. The Python command
has matching `--build`/`--devtools` arguments and `--states sh o3auto` filtering.
The build script copies the worktree's core to a separate `core_capture` once;
remove that copy to pick up core changes affecting capture. Replayers always
build from the current worktree. Raw `.gputrace` files and `.ppm` frames live
under `/tmp/correct-sim`; retain them for deterministic A/B replay.

Each selected frame produces reference/PICA PNGs, an absolute RGB difference
amplified three times, and a labeled three-panel comparison. `metrics.json`
reports mean absolute channel error (0–255), differing pixel percentage, and
pixels with an error greater than 24 in any channel. The latter is useful for
separating large errors from dithering/quantization noise, not a pass threshold.

Synthetic mask, blend and ordering probes can run without ROMs or states:

```sh
/tmp/correct-sim/venv/bin/python tools/gpu-compare/fixtures.py \
    --check --output /tmp/correct-sim/fixtures
python3 tools/gpu-compare/inspect_trace.py /tmp/correct-sim/sh.gputrace
```

`--check` verifies exact reference RGB for the selected mask cases, including
the subtract/quarter probes after correcting the target quantization model. `known-mask-half-blend` deliberately documents a
remaining failure and is not counted as a passing regression. The high-frequency
polygon probe likewise visualizes residual PS1/PICA interpolation differences.
See [REPORT.md](REPORT.md) for game images, fixes and benchmark priorities.

Limits: the fake GPU models this shader's affine interpolation, nearest tiled
texture lookup, TEV stages (including the signed dither add), alpha tests and blend factors. It does not emulate
PICA precision, memory/cache races, command processor, shader execution or the
hardware clipping bugs. RGBA5551 conversion drops the low three bits of the 8-bit blend result, as
also modeled by Azahar; verify actual PICA interpolation/TEV precision on
hardware before using single-level RGB errors as evidence.
These are raw PS1 scanout images, not 3DS final-screen centering/scaling. The
trace records the display rectangle at the end of each host frame; it does not
model a real CRT, mid-frame changes or the frontend's delayed presentation.
24-bit scanout uses CPU VRAM, matching PICA's existing movie fallback.

Capture is at the gpulib renderer boundary. VRAM copies are recorded as the
resulting CPU upload; this validates their destination upload, not independently
the copy command or GPU-to-CPU synchronization. The x86 renderer's own texture
readbacks still execute against the fake render target. Proprietary game states
and traces are deliberately not checked in.

## Flicker investigation

[The flicker report](../../docs/gpu-flicker.md) records the coordinate/presentation
fixes and hardware handoff. The independent host regressions run without game
files, with AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
BUILD_DIR=/tmp/flicker-sim bash tools/gpu-compare/regress.sh
python3 tools/gpu-compare/inspect-polygons.py /tmp/flicker-sim/o3auto.gputrace \
    --frames 56 59 60 --output /tmp/flicker-sim/primitives.json
```

`runner_capture` accepts `INPUT_MASK` (libretro joypad bits, e.g. 16 = Up) or
`INPUT_SCRIPT`, a text file of `first-frame last-frame hex-mask` intervals.
Intervals are inclusive, frame numbers start at 1, and overlapping masks are
ORed. Without these variables the runner still sends no input. The simulator
runner explicitly applies the configured GTE widescreen scale because the
production option reader is guarded by `_3DS`.

For long captures, `GPU_VERIFY=1 GPU_VERIFY_EVERY=60` embeds a full reference
snapshot every 60 frames; the default remains every frame. Native replayers
accept `FRAME_EVERY`, `FRAME_FIRST` and `FRAME_LAST` to limit **image output**;
all commands and reference snapshots are still replayed/verified. Old replay
binaries ignore these variables: rebuild before long captures to avoid saving
thousands of PPMs on the shared tmpfs.

## Colour/dither follow-up

[CORRECTNESS-2.md](CORRECTNESS-2.md) contains the subsequent modulation, GPU
dithering and flipped-wrap fixes, independent oracle comparisons, all requested
frame metrics, CPU/submission cost estimates, build hashes and hardware notes.
P.E.Op.S. has positive-only dither bias; `--reference neon` selects the upstream
gpu_neon **portable C** renderer's signed-dither oracle. Only the original
P.E.Op.S. replay asserts equality to captured full-VRAM snapshots.

For native replays without rebuilding/copying the capture core:

```sh
BUILD_DIR=/tmp/correct2-sim REPLAY_ONLY=1 tools/gpu-compare/build.sh
/tmp/correct-sim/venv/bin/python tools/gpu-compare/compare.py \
    --build /tmp/correct2-sim --reference neon --frames 10 30 60 \
    --reuse-traces --output /tmp/correct2-sim/final
```

`--pica /path/to/old-replayer` selects a prior renderer for deterministic A/B.
The comparer sets `GPU_FRAMES=10,30,60` in new replayers so only selected PPMs
are written; all commands and full-reference verification still execute.
`GPU_DITHER=0`/`2` test disabled/force config; changing the reference config
intentionally skips equality to enabled-dither capture snapshots. The synthetic
trace extension tag 7 is one uint32 config value, used only to test option changes
with pending batches. Traces from capture do not contain it.

The new sprite-wrap fixture uses explicitly labeled independent modulo
addressing because P.E.Op.S. crashes on a flipped negative span. No game data,
traces or full VRAM dumps are added to the repository.
