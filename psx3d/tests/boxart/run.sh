#!/bin/bash
# Host test of box art matching and the memory of missing pictures
# (psx3d/source/boxart.c, run for real on the shipped name table), with
# AddressSanitizer and UndefinedBehaviorSanitizer.
set -euo pipefail
repo_root=$(cd "$(dirname "$0")/../../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cc -std=gnu11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -I"$repo_root/psx3d/tests/host" -I"$repo_root/psx3d/source" \
  "$repo_root/psx3d/tests/boxart/check.c" "$repo_root/psx3d/source/boxart.c" \
  "$repo_root/psx3d/source/safefile.c" -o "$tmp/check"
cd "$tmp"
ASAN_OPTIONS=detect_leaks=1 "$tmp/check" "$repo_root/psx3d/romfs/covers" "$@"
