# Building and testing

PSX3D includes its modified PCSX-ReARMed core in `core/`. Edit those files
directly. No submodules or patch regeneration are needed. The core originated
from libretro/pcsx_rearmed revision `c8816799b50388e61cfe237fe2cdbb7d8175f20a`.
Keep its GPL licence, AUTHORS and dependency notices when changing it.

## 3DS build

The release toolchain is pinned in [`tools/build/toolchain.lock`](../tools/build/toolchain.lock):
the `devkitpro/devkitarm` container image by digest, the 24 devkitPro packages
installed with the toolchain (devkitARM r68-1 with GCC 16.1.0, libctru 2.7.0, citro2d
1.7.0, citro3d 1.7.1-2, the curl, mbedTLS, libpng and zlib portlibs and the asset tools;
each by version and by a hash of the installed files) and the commits of the CIA tools,
makerom and bannertool. Python 3 is used by the build.

### Pinned build (what CI and releases use)

```sh
git clone https://github.com/BreadChaser/PSX3D.git
cd PSX3D
tools/build/container.sh make -j8                  # psx3d/PSX3D.3dsx and .elf
tools/build/container.sh make cxi                  # + PSX3D.cia and PSX3D.cxi
tools/build/container.sh bash tools/build/ci.sh all    # everything CI runs
```

`container.sh` runs the command in [`tools/build/Dockerfile`](../tools/build/Dockerfile)
(the pinned image plus makerom, bannertool and the host-test packages) with the
checkout at `/work`; `makerom` and `bannertool` are already on `PATH`. It uses Docker
when the daemon is usable. Without Docker it needs only [bubblewrap](https://github.com/containers/bubblewrap)
and unprivileged user namespaces: it unpacks the same image by digest from Docker Hub
(`tools/build/rootfs.py` verifies every blob), provisions it once with
`tools/build/provision.sh` into `~/.cache/psx3d-build/` and runs the command in a
sandbox. `PSX3D_CONTAINER=docker|bwrap` forces one engine. The first run downloads
about 540 MB and unpacks 1.5 GB.

Pinned: the image, all devkitPro packages (checked by `tools/build/toolchain.py check`,
which `ci.sh toolchain` runs first) and the CIA tool commits. Not pinned: the Debian 12
packages the host tests use (FreeType, libpng, ImageMagick, ...), which float with Debian
security updates and never reach the 3DS binary.

### Build with your own devkitPro

Install [devkitPro](https://devkitpro.org/wiki/Getting_Started), devkitARM, `3ds-dev` and
the 3DS curl, mbedTLS, libpng and zlib portlibs, and for the CIA
[bannertool](https://github.com/Steveice10/bannertool) and
[makerom](https://github.com/3DSGuy/Project_CTR). Use the versions in `toolchain.lock`.

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITARM="$DEVKITPRO/devkitARM"
export PATH="$DEVKITPRO/tools/bin:$DEVKITARM/bin:$PATH"
make
make cia BANNERTOOL=/path/to/bannertool MAKEROM=/path/to/makerom
```

Before compiling anything, `make` runs `tools/build/toolchain.py check --abi` (the
`sdk-check` target) and stops if `libcitro3d.a`, `libcitro2d.a`, `libctru.a`, the
libctru/citro3d/citro2d headers or the size of citro3d's `__C3D_Context` differ from the
lock. `SDK_CHECK=0 make` skips that, for testing a new SDK (see
[Upgrading the SDK](#upgrading-the-sdk)). `python3 tools/build/toolchain.py check` verifies
every locked package.

Outputs are `psx3d/PSX3D.3dsx`, `psx3d/PSX3D.cia`, `psx3d/PSX3D.cxi` (with `make cxi`)
and `psx3d/PSX3D.elf`. Use `make clean` before changing toolchain or compiler flags. The
default GPU layout is `split`; alternative layouts are development comparisons, not user
settings.

`PSX3D_VERSION` in `psx3d/source/psx3d.h` controls the release version. The root
Makefile derives the CIA title counter; v0.4.0 uses **4099**. Do not lower that
counter when installing test builds over the release. The remote build identity is reported by `psx3d/source/devcontrol.c`.

### The private citro3d ABI

`psx3d/source/renderqueue.c` replaces citro3d's render queue and writes into its private
`C3D_Context` (`psx3d/vendor/citro3d/internal.h` is a copy from citro3d 1.7.1);
`gpu_cache.c` wraps private `C3Di_` functions and skips `C3D_UpdateUniforms` using the
dirty-flag layout. A different citro3d can compile and still corrupt the context, so three
checks guard it (`psx3d/source/c3d_abi.h`):

1. **Compile time:** static assertions on `sizeof(C3D_Context)`, the offsets
   renderqueue.c uses, the public types embedded in it and the uniform counts.
2. **Make time:** the lock comparison above, including the library's own size for
   `__C3D_Context` (`nm -S`) against `C3D_ABI_CONTEXT_SIZE`.
3. **Startup:** `C3Di_RenderQueueInit` checks the live context (active flag, 16-byte aligned
   command buffer of `VIDEO_CMDBUF_BYTES`, 32-entry GX queue) before using it. On a mismatch
   it logs `citro3d private ABI mismatch (...)` and `video_init()` fails, so the app exits
   instead of drawing with a corrupt context. `tests/c3d_abi/run.sh` exercises this check
   on the real structure layout (needs an ARM Linux cross compiler and qemu-user; skipped
   otherwise). citro3d has no version macro, so the hashes and sizes are the version check.

### Reproducible builds

Set `SOURCE_DATE_EPOCH` (`ci.sh` uses the commit time); the core and the clip metadata
log `__DATE__ __TIME__`. Then two builds of one commit, in the container and with a
local devkitPro of the same package versions, in different directories, produce:

- identical loaded ELF sections (`python3 tools/build/elf-identity.py psx3d/PSX3D.elf`),
- a byte-identical `PSX3D.3dsx` and `PSX3D.cxi` (the executable container inside the CIA).

Not identical: the `.elf` file itself (its debug info records the build directory) and the
`.cia`, whose ticket and TMD carry random data (about 600 bytes; the header, certificates,
meta and the whole content section are identical). Compare 3DSX, CXI or the section hashes.

## Continuous integration

[`.github/workflows/build.yml`](../.github/workflows/build.yml) runs on pushes to `main`,
pull requests and by hand. It needs no secrets and no game images. In
`tools/build/container.sh` it runs `tools/build/ci.sh` for: the toolchain lock check, the
3DSX/ELF/CIA/CXI build (uploaded with section hashes and `SHA256SUMS`), the core layout
check (`tools/layout/check.sh`) and the host tests. The same steps run locally with
`tools/build/container.sh bash tools/build/ci.sh all`.

The host tests (`make host-tests`, or `bash tools/build/host-tests.sh [filter]`) build
the real sources against small fakes of the 3DS SDK, with ASan/UBSan, and run them on the
host:

| Check | Runs |
|---|---|
| `frontend-ui` (`psx3d/tests/host`) | ui, options, config, presets, clip code; draws the bottom-screen mockups offline |
| `settings-ui` (`tests/settings`) | settings menu navigation and drawing, rewind start/stop rules |
| `renderer-config`, `widescreen`, `presets` | config/options/gamecfg code; widescreen bundles including every shipped `.ws`; preset files and v0.4.1 equivalence (`tools/presets/check.py`) |
| `statsview`, `audio-resampler`, `frameskip` | the stats formatter, audio resampler and frameskip controller |
| `input-replay` | the clip codec, input, analog and checkpoint handling |
| `dev-server` (`psx3d/tests/dev`) | developer server lifecycle, a loopback TCP transport and the updater guards (including the tracked Python control client and benchmark driver) |
| `covers`, `covers-worker` | the cover PNG decoder on generated images and the download worker against a loopback HTTP server |
| `library`, `save-safety`, `save-names` | library scanner, crash-safe save/state/card files with backups, save-name ownership (`psx3d/tests/{library,save,saveid}`) |
| `core-microtests` | real ARMv6 dynarec vs interpreter, load-delay/memory/SMC/GTE programs and HLE determinism; qemu-arm, no ROM/BIOS (`tests/core/README.md`) |
| `c3d-abi` | the private citro3d structure layout PSX3D relies on |
| `video-scanout`, `video-teardown` | `vout_flip` from `core/frontend/libretro.c` and `video_game_end` from `video.c`, extracted and run with fakes |
| `gpu-reference`, `pica-layout` | `gpu_pica.c` against the independent software rasteriser; baseline and split layouts must draw identically |

They cannot certify SOC, GPU or APT timing or the real worker threads. Not part of CI
because they need private files or a console: the PC simulator (`tests/gpu_thread_sim.py`,
`tools/dynarec-speed`), the HLE memory-card probes with a real BIOS, the emulation identity
gates, `tools/perfchart` and all hardware measurements. The sanitizer builds use the image's
GCC 12, whose AddressSanitizer crashes at random on kernels with high-entropy ASLR
(`vm.mmap_rnd_bits=32`, e.g. Ubuntu 24.04 runners); the workflow lowers it to 28 and
`host-tests.sh` runs the tests with `setarch -R` where allowed.

## Host checks

`make host-tests` runs the list above. Install a C compiler, Python 3, pkg-config,
FreeType, Fontconfig, libpng, libcurl, zlib, OpenSSL, DejaVu fonts, librsvg and ImageMagick
(`magick` or `convert`) or use the container. The individual commands, from the repository
root:

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
bash tests/c3d_abi/run.sh                   # needs an ARM Linux cross compiler and qemu-user
python3 tests/gpu_thread_sim.py --sim /tmp/psx3d-thread-sim --sdk tools/input-replay/fake3ds
```

The thread test (private simulator, not in CI) needs a fresh simulator directory; use
`--jobs 2 --timeout 1200` on a busy host. These checks use local substitutes and ASan/UBSan
where applicable. Generated mockups
are offline renderings, not hardware screenshots; write them outside the source
tree. See [GPU comparisons](../tools/gpu-compare/README.md) for the independent
renderer oracles, [remote testing](dev-control.md) for hardware measurements and
[recording and replay](recorder.md) for deterministic clips.
The [HLE memory-card probes](../tools/hle-compat/README.md) exercise fragmented
save files and full-card handling using disposable cards and an optional real-BIOS oracle.
Per-game presets have their own format and checks: see [presets](presets.md).
Research round reports live in [docs/dev](dev/README.md).

## Upgrading the SDK

An SDK upgrade is a deliberate change, made in one commit with the lock:

1. Install the new packages or pick the new image digest. `SDK_CHECK=0 make` builds with
   them until the lock is updated.
2. Replace the `image` line of `toolchain.lock` and the `FROM` of `tools/build/Dockerfile`
   (CI checks that they agree) and the `package` and `abi` lines with the output of
   `python3 tools/build/toolchain.py packages` and `... abi`.
3. If citro3d changed: update `psx3d/vendor/citro3d/internal.h` from its source, merge
   its `renderqueue.c` changes into `psx3d/source/renderqueue.c`, check the wrapped
   `C3Di_` functions in `gpu_cache.c`, then update the constants in `c3d_abi.h` (the static
   assertions say which one; `nm -S libcitro3d.a` gives the context size). Re-read
   `C3D_Init` in the new library: the startup check mirrors what it stores.
4. Build, run `ci.sh all`, compare the ELF with the previous release
   (`python3 tools/layout/compare.py`) and measure on both consoles (Old 3DS A/B/A):
   compiler and library changes move code.

## Code layout (Old 3DS)

The Old 3DS has 16 KB caches without L2, so code and data addresses matter for
speed. `psx3d/core-first.ld` links the emulator core first at a fixed address,
then the libraries, then the frontend, so frontend changes never move the core.
After a frontend or library change (with devkitARM and a built core):

```sh
bash tools/layout/check.sh                       # a grown frontend must not move the core
python3 tools/layout/compare.py old.elf new.elf  # two builds: what moved
```

Changes inside `core/`, and frontend changes to hot paths, still need an Old
3DS A/B/A. See [core-first layout](dev/core-first.md).

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
   `tools/perfchart/perfchart.py --results <directory> --console old` (the New 3DS gets
   the one-line `new_note` in `docs/performance.json`).
3. Commit `docs/performance.json`, both generated SVGs and the README table.

For a new release, first run `tools/perfchart/perfchart.py --version vX.Y.Z`.
`--set "Game name" old.off_speed=90 old.off_fps=27` can enter a separately
verified measurement; plain `tools/perfchart/perfchart.py` regenerates all
outputs. The importer reads game speed from `speed_pct` and shown pictures per
second from `top_count`, choosing off or auto from the benchmark request.

Host suites returning 77 are reported as SKIP and fail the aggregate run.
For a deliberate local partial run only, set `HOST_TEST_ALLOW_SKIPS=1`; CI
forces this to 0 and installs the ARM Linux cross compiler and qemu-user.
