#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
build=${BUILD_DIR:-/tmp/correct-sim}
mkdir -p "$build"
python3 - "$repo" "$build" <<'PY'
import sys,pathlib
s=(pathlib.Path(sys.argv[1])/'core/plugins/gpulib/gpu.c').read_text()
a=s.index('const unsigned char cmd_lengths[256]');b=s.index('};',a)+2
(pathlib.Path(sys.argv[2])/'commands.h').write_text(s[a:b]+'\n')
PY
# gpu_pica imports command lengths from gpulib; provide them separately.
printf '#include "commands.h"\n' > "$build/regress-commands.c"
cc -O1 -g -fno-strict-aliasing -fsanitize=address,undefined -DPSX3D_SIM -DGPU_PICA \
    -I "$repo/core" -I "$build" -I "$here" \
    "$here/regress.c" "$here/raster.c" "$repo/core/plugins/gpu_pica/gpu_pica.c" \
    "$repo/core/plugins/gpulib/vout_pl.c" "$build/regress-commands.c" -lm -o "$build/regress"
"$build/regress"
