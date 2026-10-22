#!/usr/bin/env bash
# Every host check that needs no console, game image, BIOS or private file (CI runs this).
# They build the real frontend/core sources with ASan/UBSan against small fakes of the
# 3DS SDK; they do not run on the 3DS and cannot certify GPU, SOC or APT timing, or the
# real worker threads. What each one executes is described in docs/building.md.
#
# Usage: bash tools/build/host-tests.sh [name-filter]
# Needs: cc, python3, pkg-config, FreeType, libpng, libcurl, zlib, OpenSSL, fonts-dejavu,
# ImageMagick (`magick` or `convert`). Logs go to $HOST_TEST_OUT (default: a temp dir).
set -uo pipefail
cd "$(dirname "$0")/../.."
out=${HOST_TEST_OUT:-$(mktemp -d "${TMPDIR:-/tmp}/psx3d-host-tests.XXXXXX")}
mkdir -p "$out"
if ! command -v magick >/dev/null 2>&1; then   # ImageMagick 6 only ships `convert`
	mkdir -p "$out/bin"
	printf '#!/bin/sh\nexec convert "$@"\n' > "$out/bin/magick"
	chmod +x "$out/bin/magick"
	export PATH="$out/bin:$PATH"
fi
filter=${1:-}
passed=0
failed=()
skipped=()
# Exit 77 is a missing prerequisite, never a pass. Local opt-out is explicit.
allow_skips=${HOST_TEST_ALLOW_SKIPS:-0}

# The gcc-12 AddressSanitizer in the pinned image crashes at random (an endless stream of
# "AddressSanitizer:DEADLYSIGNAL") on kernels with high-entropy ASLR (vm.mmap_rnd_bits=32:
# Ubuntu 24.04 runners, recent desktop kernels). Run the tests without ASLR where that is
# allowed. Docker's default seccomp profile refuses it; there the workflow lowers
# vm.mmap_rnd_bits on the host instead.
norandom=()
if setarch "$(uname -m)" -R true 2>/dev/null; then norandom=(setarch "$(uname -m)" -R); fi

run() { # run <name> <command...>
	local name=$1 start=$SECONDS
	shift
	[[ -z $filter || $name == *$filter* ]] || return 0
	# A crashing sanitizer handler can loop forever: cap the log (100 MB) instead of the disk.
	if (ulimit -f 204800; "${norandom[@]}" "$@") > "$out/$name.log" 2>&1; then
		passed=$((passed + 1))
		printf 'PASS %3ds  %s\n' $((SECONDS - start)) "$name"
	else
		local rc=$?
		if [[ $rc == 77 ]]; then
			skipped+=("$name")
			printf "SKIP %3ds  %s\n" $((SECONDS - start)) "$name"
			cat "$out/$name.log"
			return 0
		fi
		failed+=("$name")
		printf 'FAIL %3ds  %s\n' $((SECONDS - start)) "$name"
		tail -n 40 "$out/$name.log" | sed 's/^/     | /'
	fi
}

# Frontend code against a fake SDK: menus, settings, input, config, presets, widescreen, clips.
run frontend-ui      env PSX3D_MOCKUPS="$out/mockups" bash psx3d/tests/host/run.sh
run settings-ui      env PSX3D_SETTINGS_OUTPUT="$out/settings" python3 tests/settings/run.py
run settings-spec    env SPEC_DUMP="$out/settings-menu.tsv" bash tests/settings/spec.sh
run renderer-config  bash tests/renderer_config.sh
run widescreen       bash tests/widescreen/run.sh
run presets          bash tests/presets/run.sh
run presets-update   bash psx3d/tests/presetupdate/run.sh
run statsview        bash -c 'cc -std=c99 -Wall -Wno-unused-parameter -Wno-unused-function -fsanitize=address,undefined -g tests/statsview/check.c -o "$0/statsview" && "$0/statsview" > /dev/null' "$out"
run audio-resampler  bash -c 'cc -std=gnu11 -Wall -Wextra -fsanitize=address,undefined -g tests/audio_resample_test.c psx3d/source/audio_resample.c psx3d/source/audio_tsm.c -lm -o "$0/audio-resampler" && "$0/audio-resampler"' "$out"
run input-replay     env BUILD_DIR="$out/clips" bash tools/input-replay/check.sh
run frameskip        bash tools/frameskip/check.sh
# Developer server/updater protocol and the cover downloader worker.
run dev-server       bash psx3d/tests/dev/run.sh
run covers           bash psx3d/tests/covers/run.sh
run library          bash psx3d/tests/library/run.sh
run save-safety      bash psx3d/tests/save/run.sh
run save-names       bash psx3d/tests/saveid/run.sh
run c3d-abi          bash tests/c3d_abi/run.sh
run covers-worker    python3 psx3d/tests/covers/worker.py
run core-microtests bash tests/core/run.sh
run title-version   python3 tests/title_version/check.py
# Core/renderer/video code extracted from the sources and run on the host.
run video-scanout    python3 psx3d/tests/vout/check.py
run video-teardown   python3 tools/frontend-overhead/library_check.py
run gpu-reference    env BUILD_DIR="$out/gpu" bash tools/gpu-compare/regress.sh
run pica-layout      env PICA_LAYOUT_CHECK_OUT="$out/pica-layout" bash tools/pica-layout/check.sh

echo
echo "$passed passed, ${#failed[@]} failed, ${#skipped[@]} skipped (logs: $out)"
[[ ${#failed[@]} -eq 0 ]] || { echo "failed: ${failed[*]}"; exit 1; }

if (( ${#skipped[@]} )) && [[ $allow_skips != 1 ]]; then
 echo "skipped suites fail this run: ${skipped[*]} (local opt-out: HOST_TEST_ALLOW_SKIPS=1)" >&2
 exit 1
fi
