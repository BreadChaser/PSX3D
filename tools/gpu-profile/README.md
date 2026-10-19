# Silent Hill GPU profiling

These scripts use the private simulator and ARM11 cache model described by the
build and test documentation (`docs/building.md`). `sh` means **Silent Hill**. They do not connect to consoles.
See [performance](../../docs/performance.md) for hardware results and limitations.

`profile.py` instruments a **private simulator copy**, never the worktree core.
It adds GP0 command markers, records inclusive instructions/cache misses by
command type, and prints per-frame state/texture/batch counters. The marker
callbacks run in QEMU's host plugin. Normal game builds contain none of this
instrumentation. The generated plugin's `*.ctx` columns are: command hex,
instructions, I misses, read misses, store misses, modeled interlocks. Command
zero means outside the selected primitive commands; it includes emulation and
unselected GP0 commands. The regular `cprof.py` cache columns in this diagnostic
plugin are deliberately not populated; use the `.ctx` output instead.

```sh
source /home/tony/Development/PS1_3ds/devtools/env.sh
# First copy devtools/sim to a private location and point build_pica.sh at
# the worktree core. See docs/building.md for baseline/candidate builds.
# The private QEMU is 10.0.3: use its matching qemu-plugin.h, not a newer
# distro header. Its old build-tree symlink may no longer resolve.
curl -fsSL https://raw.githubusercontent.com/qemu/qemu/v10.0.3/include/qemu/qemu-plugin.h \
  -o /tmp/sh-sim/qemu-plugin.h
python3 tools/gpu-profile/profile.py --sim /tmp/sh-sim \
  --source core/plugins/gpu_pica/gpu_pica.c --plugin-include /tmp/sh-sim
cd /tmp/sh-sim
for frames in 300 600; do
  PROFILE_SYMBOLS="$PWD/profile-symbols" CACHESIM_OUT="profile_$frames.txt" SMALL=1 \
    /home/tony/Development/PS1_3ds/devtools/dl/qemu-full/build/qemu-arm \
    -plugin ./profile-plugin.so ./runner_profile \
    '/home/tony/Development/PS1_3ds/devtools/roms/psx/Silent Hill (USA)/Silent Hill (USA).cue' \
    "$frames" states/sh.st0 o3hw_off.ini > /dev/null 2>"profile_$frames.log"
done
```

Subtract the 300-frame `.ctx` totals from the 600-frame totals and divide by 300.
The cache estimate is instructions + 70×I misses + 70×read misses + 4×store
misses; interlocks are listed separately, matching the regular `cprof.py`
report's convention. Divide cycles by 268111.856 to express modeled milliseconds.
Markers/counters alter code layout, so these command-level costs locate work;
use **uninstrumented** `cbench.sh` binaries for the before/after table.

`profile.py` currently selects the seven command types exercised by Silent
Hill's supplied state. Add marker commands for other scenes. The `CHANGES`
fields count independent differences at nonempty batch boundaries: blend,
pass, mask, draw area, atlas rebind, texture mode. Page/CLUT fields count changes
between texture lookups. Multiple differences may occur at the same boundary.
The `PROFILE` line includes existing renderer counters, reset each host frame.

`summarize.py /tmp/sh-sim --after-prefix key` reads uninstrumented `cs_before_STATE_{300,600}.txt`
and `cs_key_STATE_{300,600}.txt` (the prefix is configurable) and prints precise per-frame totals. Optional
`cs_stp_correct-{base,batch}_{300,600}.txt` adds the Silent Hill STP test.
The small JSON files in `evidence/` retain the results used in the report.
Run `build_pica.sh` again after a diagnostic run to restore ordinary simulator
source/flags before collecting performance or hashes.
