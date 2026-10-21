#!/usr/bin/env bash
# Preset data: file checks, then identical-to-v0.4.1 and version-precedence host tests.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
python3 "$repo/tools/presets/check.py"
cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -include stddef.h \
 -I "$repo/psx3d/tests/host" -I "$repo/psx3d/source" -I "$repo/tests/presets" \
 -I "$repo/core/deps/libretro-common/include" \
 "$repo/tests/presets/identical.c" "$repo/psx3d/source/presets.c" "$repo/psx3d/source/widescreen.c" -o "$work/identical"
"$work/identical" "$repo/psx3d/romfs/presets" "$repo/psx3d/romfs/widescreen" "$repo/tests/presets/v041-ws-models.txt" "$work"

python3 "$repo/tests/presets/validation.py" "$work/identical"
