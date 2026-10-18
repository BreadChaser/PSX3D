#!/bin/bash
set -euo pipefail
repo_root=$(cd "$(dirname "$0")/../../.." && pwd)
test_tmp=$(mktemp -d)
trap 'rm -rf "$test_tmp"' EXIT
export PSX3D_MOCKUPS="$repo_root/psx3d/docs/bottom-screen"
export PSX3D_TITLE_FONT="$repo_root/psx3d/tests/host/Michroma-Regular.ttf"
export PSX3D_BODY_FONT=$(fc-match -f '%{file}' 'DejaVu Sans')
cat > "$test_tmp/fonts.conf" <<EOF
<?xml version="1.0"?><!DOCTYPE fontconfig SYSTEM "fonts.dtd"><fontconfig><include>/etc/fonts/fonts.conf</include><dir>$repo_root/psx3d/tests/host</dir></fontconfig>
EOF
export FONTCONFIG_FILE="$test_tmp/fonts.conf"
cc -std=gnu11 -Wall -Wextra -Wno-format-truncation -Wno-sign-compare \
  -fsanitize=address,undefined -g \
  -I"$repo_root/psx3d/tests/host" -I"$repo_root/core/deps/libretro-common/include" \
  $(pkg-config --cflags freetype2) "$repo_root/psx3d/tests/host/check.c" $(pkg-config --libs freetype2) -lm -o "$test_tmp/check"
(cd "$test_tmp" && ./check)
for name in fps-old-full fps-new-stacked fps-new-left fps-blank new3ds-one-r3 new3ds-left-one-r3 stick-one-button stick-two-rows stick-two-diagonal stick-two-columns bottom-l2r2-only controls-empty-slots bottom-l2r2-triangle controls-bound-slot bottom-l2r2-triangle-stick bottom-one-button bottom-two-buttons bottom-three-buttons bottom-four-buttons bottom-blank bottom-idle bottom-curated-full side-one-button side-one-button-lit side-two-buttons new3ds-left new3ds-l3r3 new3ds-left-remapped new3ds-one-button old-side-one-button new3ds-symbols new3ds-actions controls-actions bottom-symbols controls-symbols bottom-navigation reset-global reset-game controls-old-unlisted controls; do
  magick -background none "$PSX3D_MOCKUPS/$name.svg" "$PSX3D_MOCKUPS/$name.png"
done
