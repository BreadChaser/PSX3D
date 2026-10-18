#!/usr/bin/env python3
"""Check successful game runs and compare all state/output checkpoints.

Usage: compare.py SIM GAME FRAMES [PREFIX]
Logs must be log_PREFIX_{off,on}_GAME_FRAMES.txt (default PREFIX=final).
"""
import json
import re
import sys
from pathlib import Path

sim, game, frames = Path(sys.argv[1]), sys.argv[2], int(sys.argv[3])
prefix = sys.argv[4] if len(sys.argv) > 4 else 'final'
logs = [(sim / f'log_{prefix}_{mode}_{game}_{frames}.txt').read_text()
        for mode in ('off', 'on')]
checks = [re.findall(r'^(?:METRIC|draw hash|state hash).*$', s, re.M) for s in logs]
for s in logs:
    assert re.search(rf'^ran {frames} frames ', s, re.M), 'incomplete run'
    for label in ('METRIC', 'draw hash', 'state hash'):
        assert re.search(r'^' + label, s, re.M), 'missing ' + label
assert checks[0] == checks[1], 'output/state mismatch'
print(json.dumps({'game': game, 'frames': frames, 'status': 'identical',
                  'checks': checks[1]}, indent=2))
