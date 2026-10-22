#!/usr/bin/env bash
# Synthetic MIPS programs against the real ARM11 dynarec/interpreter. No games/BIOS.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
qemu=$(command -v qemu-arm-static || command -v qemu-arm || true)
if ! command -v arm-linux-gnueabihf-gcc >/dev/null || [[ -z $qemu ]]; then
 echo 'SKIP core-microtests: install gcc-arm-linux-gnueabihf and qemu-user'
 exit 77
fi
ulimit -c 0
work=${CORE_TEST_WORK:-$(mktemp -d)}
mkdir -p "$work"
[[ -n ${CORE_TEST_WORK:-} ]] || trap 'rm -rf "$work"' EXIT
python3 "$repo/tests/core/prepare.py" "$work"
fakes=$repo/tools/dynarec-speed/fake3ds
make -C "$work/core" -f Makefile.libretro platform=ctrsim CTR_GPU=pica CFLAGS_OPT=-O2 \
 DEBUG_SYMS=1 ASSERTS=1 PICA_CFLAGS="-I$fakes -DPICA_LAYOUT_SPLIT" -j"${JOBS:-4}" > "$work/build.log" 2>&1 || { tail -80 "$work/build.log"; exit 1; }
for suite in memory gte determinism; do
 arm-linux-gnueabihf-gcc -O2 -g -march=armv6k -mfloat-abi=hard -mfpu=vfp -marm -DPICA -DDYNAREC_MICROTEST \
  -I"$fakes" -I"$work/core" -I"$work/core/include" -I"$work/core/deps/libretro-common/include" \
  -o "$work/$suite" "$repo/tools/dynarec-speed/runner.c" "$repo/tests/core/$suite.c" \
  "$work/core/pcsx_rearmed_libretro_ctrsim.a" -static -lpthread -lm
 (cd "$work" && "$qemu" "./$suite" synthetic.exe 1 - "$repo/tests/core/options.ini")
done
