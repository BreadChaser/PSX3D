#!/bin/bash
# Host test of the crash-safe save files (memory cards, states, settings).
set -euo pipefail
repo_root=$(cd "$(dirname "$0")/../../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -DSAFEFILE_TEST \
  -I"$repo_root/psx3d/source" "$repo_root/psx3d/tests/save/check.c" \
  "$repo_root/psx3d/source/safefile.c" -o "$tmp/check"
"$tmp/check"
