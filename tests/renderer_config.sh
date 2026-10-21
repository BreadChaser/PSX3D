#!/bin/sh
# Host regression for the real frontend config/options code, without 3DS SDK.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat > "$work/3ds.h" <<'HEADER'
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef uint64_t u64;
HEADER
cat > "$work/citro2d.h" <<'HEADER'
typedef struct C3D_RenderTarget C3D_RenderTarget;
typedef struct C3D_Tex C3D_Tex;
typedef void *C2D_Font;
HEADER
${CC:-cc} -std=gnu11 -Wall -Wextra -Wno-format-truncation \
  -I "$work" -I "$repo/psx3d/source" -I "$repo/core/deps/libretro-common/include" \
  "$repo/tests/renderer_config.c" "$repo/psx3d/source/config.c" \
  "$repo/psx3d/source/options.c" "$repo/psx3d/source/gamecfg.c" "$repo/psx3d/source/presets.c" \
  -DPSX3D_DEFAULTS="\"$repo/psx3d/romfs/presets/defaults.ini\"" \
  "$repo/psx3d/source/clip.c" "$repo/psx3d/source/safefile.c" "$repo/tools/input-replay/mock-core.c" -lz -o "$work/test"
mkdir -p "$work/sdmc:/3ds/PSX3D/games"
cd "$work"
./test
