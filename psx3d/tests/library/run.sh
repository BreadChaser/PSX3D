#!/bin/bash
# Host test of the game library scanner (psx3d/source/library.c, run for real
# on fixture folders), with AddressSanitizer + LeakSanitizer.
set -euo pipefail
repo_root=$(cd "$(dirname "$0")/../../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cc -std=gnu11 -Wall -Wextra -Werror -Wno-format-truncation -fsanitize=address,undefined -g \
  -I"$repo_root/psx3d/tests/host" -I"$repo_root/core/deps/libretro-common/include" \
  -I"$repo_root/psx3d/source" "$repo_root/psx3d/tests/library/check.c" \
  "$repo_root/psx3d/source/safefile.c" -o "$tmp/check"
ASAN_OPTIONS=detect_leaks=1 "$tmp/check"
