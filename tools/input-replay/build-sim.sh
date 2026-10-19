#!/usr/bin/env bash
# Private simulator built from this checkout; never edits shared devtools/sim.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
dev=${PSX3D_DEVTOOLS:-/home/tony/Development/PS1_3ds/devtools}
source "$dev/env.sh"
renderer=${SIM_RENDERER:-pica}
build=${BUILD_DIR:-$dev/input-replay/sim}
mkdir -p "$build"
python3 - "$repo" "$build" "$dev" <<'PY'
from pathlib import Path
import shutil,sys,os,subprocess
repo,build,dev=map(Path,sys.argv[1:])
core=build/'core_pica'
# Own source copy, fresh each time. Compiler outputs are isolated as well.
if core.exists(): shutil.rmtree(core)
shutil.copytree(repo/'core',core,ignore=shutil.ignore_patterns('*.o','*.a','*.d','.git'))
if os.environ.get('BASELINE')=='1':
    (core/'frontend/libretro.c').write_bytes(subprocess.check_output(['git','show','9417930:core/frontend/libretro.c'],cwd=repo))
s=(core/'Makefile.libretro').read_text(); marker='else ifeq ($(platform), ctr)'
pos=s.index(marker)
s=s[:pos]+'''else ifeq ($(platform), ctrsim)
  CC = arm-linux-gnueabihf-gcc
  CXX = arm-linux-gnueabihf-g++
  AR = arm-linux-gnueabihf-ar
  LD = arm-linux-gnueabihf-ld
  OBJCOPY = arm-linux-gnueabihf-objcopy
'''+s[pos:]
pos=s.index(marker,s.index(marker)+len(marker))
s=s[:pos]+'''else ifeq ($(platform), ctrsim)
  TARGET := $(TARGET_NAME)_libretro_ctrsim.a
  CFLAGS += -DARM11 -DGPU_UNAI_USE_FLOATMATH -DGPU_UNAI_USE_FLOAT_DIV_MULTINV
  CFLAGS += -march=armv6k -mtune=mpcore -mfloat-abi=hard -marm -mfpu=vfp
  CFLAGS += -fomit-frame-pointer -Wno-format -DHAVE_UNISTD_H -DPSX3D_SIM
  OBJS += deps/arm-mem/memcpymove-v6l.o deps/arm-mem/memset-v6l.o
  CTR_GPU ?= pica
  BUILTIN_GPU = $(CTR_GPU)
  USE_ASYNC_GPU = 0
  DYNAREC = ari64
  ARCH = arm
  STATIC_LINKING = 1
  HAVE_PHYSICAL_CDROM = 0
  fpic = -fPIC
'''+s[pos:]
(core/'Makefile.libretro').write_text(s)
shutil.copytree(repo/'tools/input-replay/fake3ds',build/'fake3ds',dirs_exist_ok=True)
PY
make -C "$build/core_pica" -f Makefile.libretro platform=ctrsim CTR_GPU="$renderer" \
  CFLAGS_OPT=-O2 PICA_LAYOUT_CFLAGS=-DPICA_LAYOUT_SPLIT \
  PICA_CFLAGS="-I$build/fake3ds -DPICA_SIM_HASH" -j"${JOBS:-6}" > "$build/build.log" 2>&1
if [[ ${BASELINE:-0} == 1 ]]; then
  arm-linux-gnueabihf-gcc -O2 -g -march=armv6k -mfloat-abi=hard -mfpu=vfp -marm -DPICA \
    -I "$build/fake3ds" -I "$build/core_pica/deps/libretro-common/include" \
    -o "$build/runner_pica" "$dev/sim/runner.c" "$build/core_pica/pcsx_rearmed_libretro_ctrsim.a" -static -lpthread -lm
  exit
fi
defs=()
if [[ $renderer == pica ]]; then defs=(-DPICA); fi
arm-linux-gnueabihf-gcc -O2 -g -march=armv6k -mfloat-abi=hard -mfpu=vfp -marm "${defs[@]}" \
  -I "$build/fake3ds" -I "$build/core_pica/deps/libretro-common/include" -I "$repo/psx3d/source" \
  -o "$build/runner_pica" "$repo/tools/input-replay/runner.c" "$repo/psx3d/source/clip.c" \
  "$build/core_pica/pcsx_rearmed_libretro_ctrsim.a" -static -lpthread -lm
