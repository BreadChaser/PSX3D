#!/usr/bin/env bash
# Compiles and runs tests/c3d_abi/check.c with an ARM Linux cross compiler under qemu-user.
# -fshort-enums gives it devkitARM's enum size, so the assertions about the real citro3d
# structures hold. Skips (exit 0) without arm-linux-gnueabihf-gcc and qemu-arm(-static): CI
# does not have them; the assertions themselves are checked by every 3DS build.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
cc=${ARM_LINUX_CC:-arm-linux-gnueabihf-gcc}
qemu=$(command -v qemu-arm-static || command -v qemu-arm || true)
sdk=${DEVKITPRO:-/opt/devkitpro}/libctru/include
if ! command -v "$cc" >/dev/null || [[ -z $qemu || ! -d $sdk ]]; then
	echo "SKIPPED c3d_abi: needs $cc, qemu-arm and the libctru headers (ARM_LINUX_CC, DEVKITPRO)"
	exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"$cc" -std=gnu11 -Wall -Wextra -Werror -static -fshort-enums -D__3DS__ \
	-I"$repo/tests/c3d_abi/stub" -I"$repo/psx3d/source" -I"$sdk" "$repo/tests/c3d_abi/check.c" -o "$work/check"
"$qemu" "$work/check"
