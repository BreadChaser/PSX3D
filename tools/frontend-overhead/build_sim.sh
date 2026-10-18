#!/bin/bash
# Link the shared runner with a selected real frontend kernel and current core.
set -euo pipefail
cd "$(dirname "$0")/../.."
sim=$(realpath "$1"); source_file=$(realpath "$2"); binary=$3
source /home/tony/Development/PS1_3ds/devtools/env.sh
arm-linux-gnueabihf-gcc -O2 -g -march=armv6k -mfloat-abi=hard -mfpu=vfp -marm -DPICA \
  -I "$PWD/psx3d/source" -I "$sim/fake3ds" -I "$sim/core_pica/deps/libretro-common/include" \
  -o "$sim/$binary" "$sim/runner.c" "$source_file" \
  "$sim/core_pica/pcsx_rearmed_libretro_ctrsim.a" -static -lpthread -lm
