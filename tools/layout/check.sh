#!/usr/bin/env bash
# tools/layout/check.sh: proves that frontend changes cannot move the emulator core
# (psx3d/core-first.ld). Builds the frontend in a temporary copy against the already
# built core three times: as is; with ~6 KB more code, 6 KB more constants, 2 KB more
# data and 4 KB more bss; and the same plus a libm function the app does not use yet.
# tools/layout/compare.py then checks every core symbol and core code word.
# Usage: source devtools/env.sh; make; bash tools/layout/check.sh [keep-dir]
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
[ -f "$ROOT/core/pcsx_rearmed_libretro_ctr.a" ] || { echo "build the core first (make)"; exit 2; }
WORK=${1:-$(mktemp -d /tmp/psx3d-layout.XXXXXX)}
mkdir -p "$WORK"

build() { # build <name> [extra C source]
	local dir=$WORK/$1
	rm -rf "$dir"; mkdir -p "$dir"
	tar -C "$ROOT" --exclude=psx3d/build --exclude='psx3d/PSX3D.*' --exclude=psx3d/romfs/gfx -cf - psx3d | tar -C "$dir" -xf -
	ln -s "$ROOT/core" "$dir/core"
	[ -z "${2:-}" ] || printf '%s\n' "$2" > "$dir/psx3d/source/aaa_layout_grow.c"
	make -s -C "$dir/psx3d" > "$dir/build.log" 2>&1 || { tail -20 "$dir/build.log" >&2; exit 2; }
	echo "$dir/psx3d/PSX3D.elf"
}

grow_source() { # grow_source [extra statement]
	echo '// Layout check only: makes the frontend bigger; never shipped.'
	echo 'const char layout_grow_rodata[6144] = { 1 };'
	echo 'char layout_grow_data[2048] = { 1 };'
	echo 'char layout_grow_bss[4096];'
	echo 'void layout_grow_code(volatile int *p)'
	echo '{'
	for i in $(seq 0 249); do echo "	p[$((i % 61))] += p[$(((i * 7) % 59))] ^ $((i * 2654435761 % 65521));"; done
	echo '}'
	echo '__attribute__((constructor)) static void layout_grow(void)'
	echo '{'
	echo '	layout_grow_code((volatile int *)layout_grow_bss);'
	echo '	layout_grow_data[1] = layout_grow_rodata[layout_grow_bss[0]];'
	echo "	${1:-}"
	echo '}'
}

cd "$ROOT"
base=$(build base)
grown=$(build grown "$(grow_source)")
libm=$(build libm "$(grow_source '{ extern float erfcf(float); layout_grow_bss[2] = (char)erfcf(layout_grow_bss[3]); }')")

size() { /opt/devkitpro/devkitARM/bin/arm-none-eabi-size -A "$1" | awk '$1 ~ /^\.(text|rodata.frontend|data|bss)$/ {printf "%s %d  ", $1, $2}'; }
echo "base:  $(size "$base")"
echo "grown: $(size "$grown")"
status=0
echo "== frontend grown (code, constants, data, bss)"
python3 "$ROOT/tools/layout/compare.py" "$base" "$grown" || status=1
echo "== frontend grown and a new libm function linked (libraries may move, the core may not)"
python3 "$ROOT/tools/layout/compare.py" --allow-lib-moves "$base" "$libm" || status=1
[ -n "${1:-}" ] || rm -rf "$WORK"
exit $status
