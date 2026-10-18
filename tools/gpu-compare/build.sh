#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
build=${BUILD_DIR:-/tmp/correct-sim}
devtools=${PSX3D_DEVTOOLS:-/home/tony/Development/PS1_3ds/devtools}
source "$devtools/env.sh"
mkdir -p "$build"
if [[ ${REPLAY_ONLY:-0} != 1 ]]; then
# A separate copy keeps simulator flags and object files out of the 3DS build.
if [[ ! -d $build/core_capture ]]; then
    cp -a "$repo/core" "$build/core_capture"
    rm -f "$build/core_capture/.git"
    # The host capture uses P.E.Op.S. as an oracle; it is not shipped in core/.
    cp -a "$here/reference/plugins/dfxvideo" "$build/core_capture/plugins/"
    python3 - "$build/core_capture/Makefile.libretro" <<'PY'
import sys
p=sys.argv[1];s=open(p).read()
marker='else ifeq ($(platform), ctr)'
pos=s.index(marker)
s=s[:pos]+'''else ifeq ($(platform), ctrsim)
  CC = arm-linux-gnueabihf-gcc
  CXX = arm-linux-gnueabihf-g++
  AR = arm-linux-gnueabihf-ar
  LD = arm-linux-gnueabihf-ld
  OBJCOPY = arm-linux-gnueabihf-objcopy
'''+s[pos:]
# Locate the platform configuration (the second ctr branch).
pos=s.index(marker,s.index(marker)+len(marker))
s=s[:pos]+'''else ifeq ($(platform), ctrsim)
  TARGET := $(TARGET_NAME)_libretro_ctrsim.a
  CFLAGS += -DARM11 -march=armv6k -mtune=mpcore -mfloat-abi=hard -marm -mfpu=vfp
  CFLAGS += -fomit-frame-pointer -Wno-format -DHAVE_UNISTD_H -DPSX3D_SIM
  OBJS += deps/arm-mem/memcpymove-v6l.o deps/arm-mem/memset-v6l.o
  BUILTIN_GPU = peops
  USE_ASYNC_GPU = 0
  DYNAREC = ari64
  ARCH = arm
  STATIC_LINKING = 1
  HAVE_PHYSICAL_CDROM = 0
  fpic = -fPIC
'''+s[pos:]
open(p,'w').write(s)
PY
    make -C "$build/core_capture" -f Makefile.libretro platform=ctrsim clean > "$build/capture-build.log" 2>&1
fi
make -C "$build/core_capture" -f Makefile.libretro platform=ctrsim CFLAGS_OPT=-O2 -j"${JOBS:-8}" >> "$build/capture-build.log" 2>&1
arm-linux-gnueabihf-gcc -O2 -g -march=armv6k -mfloat-abi=hard -mfpu=vfp -marm \
    -I "$build/core_capture" -I "$build/core_capture/deps/libretro-common/include" \
    -o "$build/runner_capture" "$here/runner.c" "$here/capture.c" \
    "$build/core_capture/pcsx_rearmed_libretro_ctrsim.a" \
    -Wl,--wrap=renderer_do_cmd_list,--wrap=renderer_sync_ecmds,--wrap=renderer_update_caches \
    -static -lpthread -lm
fi
python3 - "$repo" "$build" <<'PY'
import sys,pathlib
s=(pathlib.Path(sys.argv[1])/'core/plugins/gpulib/gpu.c').read_text()
a=s.index('const unsigned char cmd_lengths[256]');b=s.index('};',a)+2
(pathlib.Path(sys.argv[2])/'commands.h').write_text(s[a:b]+'\n')
PY
# Assemble portable reference sources with the current shared headers outside
# the repository. Their original relative includes and copyright notices stay intact.
mkdir -p "$build/reference/plugins"
cp -a "$here/reference/plugins/dfxvideo" "$here/reference/plugins/gpu_neon" "$build/reference/plugins/"
ln -sfn "$repo/core/include" "$build/reference/include"
ln -sfn "$repo/core/frontend" "$build/reference/frontend"
ln -sfn "$repo/core/plugins/gpulib" "$build/reference/plugins/gpulib"
cc -O2 -g -fno-strict-aliasing -I "$repo/core" -I "$build" \
    "$here/replay.c" "$build/reference/plugins/dfxvideo/gpulib_if.c" -lm -o "$build/replay-peops"
cc -O2 -g -fno-strict-aliasing -DPSX3D_SIM -DPICA -DPICA_SIM_HASH -I "$repo/core" -I "$build" -I "$here" \
    "$here/replay.c" "$here/raster.c" "$repo/core/plugins/gpu_pica/gpu_pica.c" -lm -o "$build/replay-pica"
# Independent signed-dither oracle; uses the portable C/vector path on x86.
cc -O2 -g -fno-strict-aliasing -DGPU_COMPARE_ALT_REFERENCE -DTEXTURE_CACHE_4BPP -I "$repo/core" -I "$build" \
    "$here/replay.c" "$build/reference/plugins/gpu_neon/psx_gpu_if.c" \
    "$repo/core/plugins/gpulib/prim.c" -lm -o "$build/replay-neon"
echo "Built native replayers in $build"
