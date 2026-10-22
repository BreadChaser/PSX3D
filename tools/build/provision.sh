#!/usr/bin/env bash
# Adds to the pinned devkitPro image what the image lacks: the host packages the host
# tests need and the CIA tools (makerom, bannertool), built from the commits in
# toolchain.lock. Runs as root inside the image: Dockerfile and container.sh both call it.
# The apt packages float with Debian 12 security updates; they only affect the host tests,
# never the 3DS binary (that comes from the pinned devkitPro packages).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
lock=${1:-$here/toolchain.lock}
prefix=${PSX3D_TOOLS:-/opt/psx3d-tools}
export DEBIAN_FRONTEND=noninteractive

apt-get -o APT::Sandbox::User=root update -qq
apt-get -o APT::Sandbox::User=root install -y --no-install-recommends \
	gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf libc6-dev-armhf-cross qemu-user rsync ca-certificates fontconfig fonts-dejavu-core imagemagick libcurl4-openssl-dev \
	libfreetype-dev libpng-dev librsvg2-bin libssl-dev pkg-config zlib1g-dev
rm -rf /var/lib/apt/lists/*

src=$(mktemp -d)
trap 'rm -rf "$src"' EXIT
mkdir -p "$prefix/bin"
tool() { # tool <name>: clone the repository at the locked commit
	read -r url rev < <(awk -v n="$1" '$1 == "tool" && $2 == n { print $3, $4 }' "$lock")
	[[ -n ${rev:-} ]] || { echo "toolchain.lock has no tool $1" >&2; exit 1; }
	git clone -q "$url" "$src/$1"
	git -C "$src/$1" checkout -q "$rev"
}

tool makerom
make -C "$src/makerom/makerom" deps -j"$(nproc)" >/dev/null
make -C "$src/makerom/makerom" -j"$(nproc)" >/dev/null
install -m 755 "$src/makerom/makerom/bin/makerom" "$prefix/bin/makerom"

tool bannertool
git -C "$src/bannertool" submodule update --init -q   # buildtools, pinned by the gitlink
make -C "$src/bannertool" -j"$(nproc)" >/dev/null
install -m 755 "$src/bannertool/output/linux-x86_64/bannertool" "$prefix/bin/bannertool"

# Both exit non-zero when run without arguments; this only shows that they start.
"$prefix/bin/makerom" -help 2>&1 | head -n 1 || true
"$prefix/bin/bannertool" 2>&1 | head -n 1 || true
