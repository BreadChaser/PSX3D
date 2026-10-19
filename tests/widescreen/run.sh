#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
 -I "$repo/psx3d/source" "$repo/tests/widescreen/check.c" "$repo/psx3d/source/widescreen.c" -o "$work/check"
(cd "$work" && ./check)
for file in "$repo"/psx3d/romfs/widescreen/*.ws; do
 [[ -e "$file" ]] || continue
 name=$(basename "$file" .ws)
 "$work/check" "$repo/psx3d/romfs/widescreen" "${name%-*}" "${name#*-}"
done
