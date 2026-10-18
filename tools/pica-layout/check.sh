#!/usr/bin/env bash
# Host coverage for the paths moved out of renderer hot functions.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
out=${PICA_LAYOUT_CHECK_OUT:-/home/tony/Development/PS1_3ds/devtools/pica-layout/host-check}
mkdir -p "$out"
cd "$repo"
python3 - "$out" <<'PY'
from pathlib import Path
import sys
s=Path('core/plugins/gpulib/gpu.c').read_text()
a=s.index('const unsigned char cmd_lengths[256]');b=s.index('};',a)+2
out=Path(sys.argv[1]);(out/'commands.h').write_text(s[a:b]+'\n')
(out/'commands.c').write_text('#include "commands.h"\n')
PY
for layout in baseline split; do
 flags=()
 if [[ $layout == split ]]; then flags+=(-DPICA_LAYOUT_SPLIT); fi
 cc -O1 -g -fno-strict-aliasing -fsanitize=address,undefined \
  -DPSX3D_SIM -DPICA_SIM_HASH "${flags[@]}" -I core -I "$out" -I tools/gpu-compare \
  tools/pica-layout/commands-test.c tools/gpu-compare/raster.c \
  core/plugins/gpu_pica/gpu_pica.c "$out/commands.c" -lm -o "$out/commands-$layout"
 "$out/commands-$layout" > "$out/commands-$layout.log"
done
cmp "$out/commands-baseline.log" "$out/commands-split.log"
cc -O1 -g -fno-strict-aliasing -fsanitize=address,undefined \
 -DPSX3D_SIM -DPICA_LAYOUT_SPLIT -I core -I "$out" -I tools/gpu-compare \
 tools/gpu-compare/regress.c tools/gpu-compare/raster.c \
 core/plugins/gpu_pica/gpu_pica.c "$out/commands.c" -lm -o "$out/regress"
"$out/regress" > "$out/regress.log"
echo 'line command/timing/pixels and polygon/presentation sanitizer checks: PASS'
