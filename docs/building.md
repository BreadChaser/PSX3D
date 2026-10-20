# Building and testing

PSX3D includes its modified PCSX-ReARMed core in `core/`. Edit those files
directly. No submodules or patch regeneration are needed. The core originated
from libretro/pcsx_rearmed revision `c8816799b50388e61cfe237fe2cdbb7d8175f20a`.
Keep its GPL licence, AUTHORS and dependency notices when changing it.

## 3DS build

Install [devkitPro](https://devkitpro.org/wiki/Getting_Started), devkitARM,
`3ds-dev` and the 3DS curl, mbedTLS, libpng and zlib portlibs. CIA packaging also
needs [bannertool](https://github.com/Steveice10/bannertool) and
[makerom](https://github.com/3DSGuy/Project_CTR). Python 3 is used by the build.
The tested toolchain is devkitARM GCC 16.1.0, libctru 2.7.0, citro2d 1.7.0 and
citro3d 1.7.1.

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITARM="$DEVKITPRO/devkitARM"
export PATH="$DEVKITPRO/tools/bin:$DEVKITARM/bin:$PATH"
git clone https://github.com/BreadChaser/PSX3D.git
cd PSX3D
make
make cia BANNERTOOL=/path/to/bannertool MAKEROM=/path/to/makerom
```

Outputs are `psx3d/PSX3D.3dsx`, `psx3d/PSX3D.cia` and `psx3d/PSX3D.elf`.
Use `make clean` before changing toolchain or compiler flags. The default GPU
layout is `split`; alternative layouts are development comparisons, not user
settings. `SOURCE_DATE_EPOCH` fixes the embedded build timestamp for executable
comparisons. CIA packaging includes randomized ticket metadata, so compare the
3DSX or allocated ELF sections when checking executable identity.

`PSX3D_VERSION` in `psx3d/source/psx3d.h` controls the release version. The root
Makefile derives the CIA title counter; v0.4.0 uses **4099**. Do not lower that
counter when installing test builds over the release. The remote build identity is reported by `psx3d/source/devcontrol.c`.

## Host checks

Install a C compiler, Python 3, pkg-config, FreeType, Fontconfig, libpng, zlib,
OpenSSL, DejaVu fonts and ImageMagick (`magick`). From the repository root:

```sh
PSX3D_MOCKUPS=/tmp/psx3d-bottom bash psx3d/tests/host/run.sh
bash psx3d/tests/dev/run.sh
PSX3D_SETTINGS_OUTPUT=/tmp/psx3d-settings python3 tests/settings/run.py
bash tests/renderer_config.sh
bash tests/widescreen/run.sh
bash tests/presets/run.sh
bash psx3d/tests/covers/run.sh
python3 psx3d/tests/covers/worker.py
python3 psx3d/tests/vout/check.py
BUILD_DIR=/tmp/psx3d-clips bash tools/input-replay/check.sh
BUILD_DIR=/tmp/psx3d-gpu bash tools/gpu-compare/regress.sh
bash tools/pica-layout/check.sh
bash tools/frameskip/check.sh
python3 tools/frontend-overhead/library_check.py
python3 tests/gpu_thread_sim.py --sim /tmp/psx3d-thread-sim --sdk tools/input-replay/fake3ds
```

The thread test needs a fresh simulator directory; use `--jobs 2 --timeout 1200`
on a busy host. These checks use local substitutes and ASan/UBSan where applicable. Generated mockups
are offline renderings, not hardware screenshots; write them outside the source
tree. See [GPU comparisons](../tools/gpu-compare/README.md) for the independent
renderer oracles, [remote testing](dev-control.md) for hardware measurements and
[recording and replay](recorder.md) for deterministic clips.
The [HLE memory-card probes](../tools/hle-compat/README.md) exercise fragmented
save files and full-card handling using disposable cards and an optional real-BIOS oracle.
Per-game presets have their own format and checks: see [presets](presets.md).
Research round reports live in [docs/dev](dev/README.md).

## Code layout (Old 3DS)

The Old 3DS has 16 KB caches without L2, so code and data addresses matter for
speed. `psx3d/core-first.ld` links the emulator core first at a fixed address,
then the libraries, then the frontend, so frontend changes never move the core.
After a frontend or library change (with devkitARM and a built core):

```sh
bash tools/layout/check.sh                       # a grown frontend must not move the core
python3 tools/layout/compare.py old.elf new.elf  # two builds: what moved
```

Changes inside `core/` still move the core's later code and need an Old 3DS
A/B/A. See [core-first layout](dev/core-first.md).

## Emulation identity gates

The ARM Linux simulator requires an ARMv6 hard-float cross compiler, static
runtime libraries, QEMU, private game images, BIOS files and benchmark save
states. Local simulator support lives in the ignored `devtools/` directory;
it is not needed for a normal 3DS build. Prepare a separate source copy with
`tools/dynarec-speed/prepare.py --hash <destination>` and build it with
`tools/dynarec-speed/build.sh <destination>`. Never reuse another build's core
objects. The simulator relocates RAM and scratchpad to exercise the production
dynarec paths.

For a change that should preserve emulation, compare baseline and candidate
using the same runner, options and inputs: six states for 600 frames each,
per-frame draw hashes, CPU/RAM checkpoints and the complete serialized guest
state. Also compare 1,200-frame cold boots with a real BIOS and HLE separately.
Matching hashes establish identity for those inputs, not complete game
compatibility. Old 3DS CPU performance must be measured on hardware: its small
instruction cache makes code layout significant.

## Updating the performance chart

1. Run frameskip-off and auto benchmark batches with `fastbench.py`, using the
   same saved scenes and settings as the existing measurements.
2. Import each console's results:
   `tools/perfchart/perfchart.py --results <directory> --console old` (and `new`).
3. Commit `docs/performance.json`, both generated SVGs and the README table.

For a new release, first run `tools/perfchart/perfchart.py --version vX.Y.Z`.
`--set "Game name" old.off_speed=90 old.off_fps=27` can enter a separately
verified measurement; plain `tools/perfchart/perfchart.py` regenerates all
outputs. The importer reads game speed from `speed_pct` and shown pictures per
second from `top_count`, choosing off or auto from the benchmark request.
