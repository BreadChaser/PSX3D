#!/usr/bin/env bash
# perf.sh <sim> <switch> <out> <games,...>: modeled per-vblank cost (frames 301-600) of heavy scenes
set -e; T=$(cd "$(dirname "$0")" && pwd); D=/home/tony/Development/PS1_3ds/devtools/dynarec-r4
source /home/tony/Development/PS1_3ds/devtools/env.sh >/dev/null 2>&1
python3 $T/run.py $D/$1 $D/$3 --mode profile --heavy --switch $2 --game $4 --jobs ${JOBS:-10} ${RESPEC:+--respec $RESPEC} > $D/$3.log 2>&1 || true
for k in ${4//,/ }; do printf '%-10s %s\n' $k "$(head -1 $D/$3/$k-cprof.py.txt 2>/dev/null)"; done
