#!/usr/bin/env bash
# Offline arithmetic and interpreter tests; no console/network access.
set -euo pipefail
source /home/tony/Development/PS1_3ds/devtools/env.sh
repo=$(cd "$(dirname "$0")/../.." && pwd)
sim=$(realpath "$1")
out=$(realpath -m "$2")
mkdir -p "$out"
arm-linux-gnueabihf-objcopy --globalize-symbol=gteNCLIP_nf \
    "${NCLIP_ORACLE_CORE:-$sim/core}/libpcsxcore/gte_nf.o" "$out/gte_nf_test.o"
arm-linux-gnueabihf-gcc -O2 -g -march=armv6k -mfloat-abi=hard -mfpu=vfp -marm \
    -ffunction-sections -fno-strict-aliasing -I "$sim/core" -I "$sim/core/include" -I "$sim/fake3ds" \
    "$repo/tools/dynarec-speed/nclip-test.c" "$sim/core/libpcsxcore/gte_arm.o" \
    "$out/gte_nf_test.o" -Wl,--gc-sections -static -lm -o "$out/nclip-test"
/home/tony/Development/PS1_3ds/devtools/dl/qemu-full/build/qemu-arm \
    "$out/nclip-test" > "$out/nclip.log" 2>&1
arm-linux-gnueabihf-gcc -O2 -g -march=armv6k -mfloat-abi=hard -mfpu=vfp -marm \
    -DPICA -DDYNAREC_MICROTEST -I "$sim/fake3ds" -I "$sim/core" \
    -I "$sim/core/include" -I "$sim/core/deps/libretro-common/include" \
    "$repo/tools/dynarec-speed/runner.c" "$repo/tools/dynarec-speed/microtest.c" \
    "$sim/core/pcsx_rearmed_libretro_ctrsim.a" -static -lpthread -lm -o "$sim/runner-micro"
mkdir -p "$out/micro-run"
cd "$out/micro-run"
SMALL=1 DYNAREC_SPEED=15 \
    /home/tony/Development/PS1_3ds/devtools/dl/qemu-full/build/qemu-arm \
    "$sim/runner-micro" \
    '/home/tony/Development/PS1_3ds/devtools/roms/psx/Crash Bandicoot (USA)/Crash Bandicoot (USA).cue' \
    600 /home/tony/Development/PS1_3ds/devtools/sim/states/o3auto.st0 \
    "$sim/o3hw_off.ini" > "$out/micro.log" 2>&1
