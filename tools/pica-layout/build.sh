#!/usr/bin/env bash
# Builds layout variants without contacting consoles. Tool paths may be overridden.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
dev=${PSX3D_DEVTOOLS:-/home/tony/Development/PS1_3ds/devtools}
out=${PICA_LAYOUT_OUT:-$dev/pica-layout}
source "$dev/env.sh"
mkdir -p "$out"
exec 9>"$out/build.lock"
flock 9
cd "$repo"
for layout in baseline split section os; do
 mkdir -p "$out/$layout"
 make -j"${JOBS:-12}" cia PICA_LAYOUT="$layout" \
  BANNERTOOL="${BANNERTOOL:-$dev/tools/bannertool/output/linux-x86_64/bannertool}" \
  MAKEROM="${MAKEROM:-$dev/tools/Project_CTR/makerom/bin/makerom}" > "$out/$layout/build.log" 2>&1
 cp psx3d/PSX3D.{cia,3dsx,elf} psx3d/build/PSX3D.map core/plugins/gpu_pica/gpu_pica.o "$out/$layout/"
 cp core/plugins/gpulib/gpu.o "$out/$layout/gpulib-gpu.o"
 "$DEVKITARM/bin/arm-none-eabi-readelf" -lSW psx3d/PSX3D.elf > "$out/$layout/elf-layout.txt"
 "$DEVKITARM/bin/arm-none-eabi-nm" -nS psx3d/PSX3D.elf > "$out/$layout/symbols.txt"
 sha256sum "$out/$layout"/PSX3D.{cia,3dsx,elf} > "$out/$layout/SHA256SUMS"
 echo "$layout built"
done
