#!/usr/bin/env bash
# mk.sh <name> [hash|perf|ann] [--ref REF]: private simulator from this worktree's core
set -euo pipefail
D=/home/tony/Development/PS1_3ds/devtools/dynarec-r4; T=$(cd "$(dirname "$0")" && pwd)
name=$1; kind=${2:-perf}; shift 2 || true
args=()
case $kind in hash) args+=(--hash);; ann) args+=(--annotate);; esac
rm -rf "$D/$name"
python3 "$T/prepare.py" "$D/$name" "${args[@]}" "$@" > "$D/$name.log" 2>&1
ASSERTS=0 bash "$T/build.sh" "$D/$name" >> "$D/$name.log" 2>&1 || { echo "BUILD FAILED, see $D/$name.log"; exit 1; }
ls -la "$D/$name/runner" | awk '{print $5, $9}'
