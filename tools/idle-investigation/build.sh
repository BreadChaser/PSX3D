#!/bin/bash
set -euo pipefail
source "${PSX3D_DEVTOOLS:-/home/tony/Development/PS1_3ds/devtools}/env.sh"
sim=${IDLE_SIM:-/tmp/idle-inv-sim}
mode=${MODE:-probe}
case "$mode" in probe|fast) ;; *) echo "MODE must be probe or fast" >&2; exit 2;; esac
cd "$sim"
extra=
defines=-DINV_FAST
if [ "$mode" = probe ]; then extra=-DPICA_SIM_HASH; defines=; fi
# make does not track command-line flags as dependencies.
rm -f core/plugins/gpu_pica/gpu_pica.o
make -C core -f Makefile.libretro platform=ctrsim CTR_GPU=pica CFLAGS_OPT=-O2 DEBUG_SYMS=1 ASSERTS=1 \
 PICA_CFLAGS="-I$sim/fake3ds $extra" -j8 > "build-$mode.log" 2>&1
arm-linux-gnueabihf-gcc -O2 -g -march=armv6k -mfloat-abi=hard -mfpu=vfp -marm -DPICA $defines \
 -I fake3ds -I core/include -I core/deps/libretro-common/include \
 -o "runner_$mode" runner.c core/pcsx_rearmed_libretro_ctrsim.a -static -lpthread -lm
