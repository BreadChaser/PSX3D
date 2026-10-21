#!/bin/bash
# Host test of the automatic preset update: manifest and version rules,
# damaged downloads, validation failure, a cut-off install and its recovery,
# and the once-a-day timing. Uses the real preset parser and tools/presets/pack.py.
set -euo pipefail
repo_root=$(cd "$(dirname "$0")/../../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -DPRESETUPDATE_TEST -include stddef.h \
  -I"$repo_root/psx3d/tests/host" -I"$repo_root/psx3d/tests/dev" -I"$repo_root/psx3d/source" \
  -I"$repo_root/core/deps/libretro-common/include" \
  "$repo_root/psx3d/tests/presetupdate/check.c" "$repo_root/psx3d/source/presetupdate.c" \
  "$repo_root/psx3d/source/presets.c" "$repo_root/psx3d/source/safefile.c" -lcrypto -o "$tmp/check"
REPO="$repo_root" "$tmp/check" "$repo_root/psx3d/romfs/presets" "$tmp/server" "$tmp/sd"
python3 "$repo_root/psx3d/tests/presetupdate/mutations.py"
