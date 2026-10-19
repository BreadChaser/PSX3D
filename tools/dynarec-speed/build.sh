#!/usr/bin/env bash
set -euo pipefail
source /home/tony/Development/PS1_3ds/devtools/env.sh
cd "$1"
hash_flags=$(cat hash.flags)
make -C core -f Makefile.libretro platform=ctrsim CTR_GPU=pica CFLAGS_OPT=-O2 \
    DEBUG_SYMS=1 ASSERTS=1 PICA_CFLAGS="-I$PWD/fake3ds -DPICA_LAYOUT_SPLIT $hash_flags" \
    -j4 > build.log 2>&1
arm-linux-gnueabihf-gcc -O2 -g -march=armv6k -mfloat-abi=hard -mfpu=vfp -marm -DPICA \
    -I fake3ds -I core -I core/include -I core/deps/libretro-common/include \
    -o runner runner.c core/pcsx_rearmed_libretro_ctrsim.a -static -lpthread -lm
