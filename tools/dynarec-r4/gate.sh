#!/usr/bin/env bash
# gate.sh <sim> <switch> <tag> [frames]: 600-frame hash timelines, heavy + standard states,
# against the same sim with switch 0 (cached as <sim>-s0); prints PASS/FAIL per set
set -e; T=$(cd "$(dirname "$0")" && pwd); D=/home/tony/Development/PS1_3ds/devtools/dynarec-r4
source /home/tony/Development/PS1_3ds/devtools/env.sh >/dev/null 2>&1
sim=$1; sw=$2; tag=$3; fr=${4:-600}; J=${JOBS:-6}; bs=${BASE:-0}
for set in heavy std; do
  flag=; [ $set = heavy ] && flag=--heavy
  base=$D/g/$sim-s$bs-$set-$fr
  [ -f $base/results.json ] || python3 $T/run.py $D/$sim $base $flag --switch $bs --frames $fr --jobs $J > $base.log 2>&1 &
  python3 $T/run.py $D/$sim $D/g/$sim-$tag-$set-$fr $flag --switch $sw --frames $fr --jobs $J > $D/g/$sim-$tag-$set-$fr.log 2>&1 &
done
wait
for set in heavy std; do
  printf "%-6s " $set; python3 $T/compare.py $D/g/$sim-s$bs-$set-$fr $D/g/$sim-$tag-$set-$fr --output $D/g/cmp-$sim-$tag-$set-$fr.json | tail -1
done
