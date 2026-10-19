#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
devtools=${PSX3D_DEVTOOLS:-/home/tony/Development/PS1_3ds/devtools}
build=${BUILD_DIR:-$devtools/hle-compat/repro}
source "$devtools/env.sh"
# gpu-compare copies this checkout's core into the private build directory.
BUILD_DIR="$build" "$repo/tools/gpu-compare/build.sh"
arm-linux-gnueabihf-gcc -O2 -g -march=armv6k -mfloat-abi=hard -mfpu=vfp -marm \
 -I "$build/core_capture" -I "$build/core_capture/deps/libretro-common/include" \
 -o "$build/runner-hle" "$here/runner.c" "$repo/tools/gpu-compare/capture.c" \
 "$build/core_capture/pcsx_rearmed_libretro_ctrsim.a" \
 -Wl,--wrap=renderer_do_cmd_list,--wrap=renderer_sync_ecmds,--wrap=renderer_update_caches \
 -static -lpthread -lm
clang --target=mipsel-none-elf -march=mips1 -msoft-float -mno-abicalls -fno-pic -G0 \
 -c "$here/start.S" -o "$build/card-start.o"
for n in 1 2 3; do
 for action in keep delete; do
  extra=()
  [[ $action != delete ]] || extra=(-DDELETE_TEST)
  clang --target=mipsel-none-elf -march=mips1 -msoft-float -mno-abicalls -fno-pic -G0 \
   -O2 -ffreestanding -DBLOCKS="$n" "${extra[@]}" -c "$here/card-probe.c" -o "$build/card-probe.o"
  ld.lld -m elf32ltsmip -T "$here/card.ld" --oformat=binary \
   "$build/card-start.o" "$build/card-probe.o" -o "$build/card-$n-$action.bin"
 done
done
python3 - "$build" <<'PY'
import struct,sys
from pathlib import Path
for p in Path(sys.argv[1]).glob('card-*.bin'):
 b=p.read_bytes();b+=bytes(-len(b)%2048)
 h=bytearray(2048);h[:8]=b'PS-X EXE'
 struct.pack_into('<IIII',h,0x10,0x80010000,0,0x80010000,len(b))
 struct.pack_into('<I',h,0x30,0x801fff00)
 p.with_suffix('.exe').write_bytes(h+b)
PY
printf 'Built HLE audit runner and BIOS test executables in %s\n' "$build"
