#!/usr/bin/env python3
"""Existing fastbench with replay-aware host deadlines (the console owns the window)."""
import os
import sys
sys.path.insert(0, os.environ.get('PSX3D_HOST_TOOLS', '/home/tony/Development/PS1_3ds/devtools'))
import fastbench
from psx3dctl import ControlError
original_bench_text = fastbench.bench_text

def replay_bench_text(path, console):
    runs = original_bench_text(path, console)
    result = []
    for run in runs:
        words=run.split()
        if any(w.startswith('replay=') for w in words):
            if any(w.startswith('state=') for w in words):
                raise ControlError('replay= and state= are mutually exclusive')
            # seconds is only a host timeout budget for a replay; every emulated
            # frame is still sampled. Allow slow 60 s clips at 1/10 speed.
            words=[w for w in words if not w.startswith(('warmup=','seconds='))]
            words += ['warmup=0','seconds=900']
        result.append(' '.join(words))
    return result
fastbench.bench_text = replay_bench_text
if __name__=='__main__':
    fastbench.main()
