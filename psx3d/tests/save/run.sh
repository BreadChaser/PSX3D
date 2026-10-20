#!/bin/bash
# Host test of the crash-safe save files (memory cards, states, settings),
# with injected faults, plus mutations that each must make it fail.
set -euo pipefail
repo_root=$(cd "$(dirname "$0")/../../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -DSAFEFILE_TEST \
  -I"$repo_root/psx3d/source" "$repo_root/psx3d/tests/save/check.c" \
  "$repo_root/psx3d/source/safefile.c" -o "$tmp/check"
cc -std=gnu11 -Wall -Wextra -Werror -Wno-format-truncation -fsanitize=address,undefined -g -DSAFEFILE_TEST \
  -include "$repo_root/psx3d/tests/save/host.h" -I"$repo_root/psx3d/tests/host" -I"$repo_root/psx3d/source" \
  -I"$repo_root/core/deps/libretro-common/include" "$repo_root/psx3d/tests/save/cards.c" \
  "$repo_root/psx3d/source/cards.c" "$repo_root/psx3d/source/safefile.c" -o "$tmp/cards"
"$tmp/cards"
cc -std=gnu11 -O1 -Wall -Wextra -Wno-sign-compare -Werror -fsanitize=address,undefined -g \
  -I"$repo_root/core/include" "$repo_root/psx3d/tests/save/states.c" -o "$tmp/states"
"$tmp/states"
python3 "$repo_root/psx3d/tests/save/mutations.py"
"$tmp/check"
cc -std=gnu11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  "$repo_root/psx3d/tests/save/thumb.c" -o "$tmp/thumb"
"$tmp/thumb"
