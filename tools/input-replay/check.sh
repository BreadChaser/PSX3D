#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
build=${BUILD_DIR:-/home/tony/Development/PS1_3ds/devtools/input-replay/host-tests}
mkdir -p "$build"
cc -std=gnu11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -I "$repo/psx3d/source" -I "$repo/core/deps/libretro-common/include" \
  "$repo/tools/input-replay/codec-test.c" "$repo/tools/input-replay/mock-core.c" \
  "$repo/psx3d/source/clip.c" -lz -o "$build/codec-test"
(cd "$(mktemp -d "$build/run-XXXXXX")" && "$build/codec-test")
