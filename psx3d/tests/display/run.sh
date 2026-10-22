#!/usr/bin/env bash
# display_fit (psx3d/source/display.c) on the host, with ASan/UBSan.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp -d "${TMPDIR:-/tmp}/psx3d-display.XXXXXX")
trap 'rm -rf "$out"' EXIT
cc -std=c99 -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all -g \
	"$here/check.c" "$here/../../source/display.c" -lm -o "$out/check"
"$out/check"
