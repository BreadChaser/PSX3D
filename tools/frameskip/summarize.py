#!/usr/bin/env python3
"""Save compact, verifiable results from the private simulator runs."""
from pathlib import Path
import argparse
import hashlib
import json
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('sim', type=Path)
args = parser.parse_args()
sim = args.sim
out = Path(__file__).with_name('evidence')
out.mkdir(exist_ok=True)
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
result = {'hashes': {}, 'timings': {}, 'executables': {}}
for game in ['o3auto', 'ctr', 'gt2']:
    for skip in [1, 4]:
        files = [sim / f'hash_{game}_{skip}_{side}.log' for side in ['before', 'after']]
        traces = [re.findall(r'^frame \d+ hash [0-9a-f]+$', p.read_text(), re.M) for p in files]
        assert len(traces[0]) == 600 and traces[0] == traces[1], files
        result['hashes'][f'{game}_skip{skip}'] = {
            'matched_frames': 600,
            'draw_hash': re.search(r'draw hash (\w+)', files[1].read_text())[1],
            'trace_sha256': hashlib.sha256(('\n'.join(traces[0]) + '\n').encode()).hexdigest(),
            'logs_sha256': [digest(p) for p in files],
            'after': re.search(r'^SIM .*$', files[1].read_text(), re.M)[0]}
for prefix in ['verified', 'verified_slow3', 'retest']:
    for p in sorted(sim.glob(f'{prefix}_*.log')):
        s = p.read_text()
        match = re.search(r'ran 600 frames .* in ([\d.]+)s', s)
        if not match:
            continue
        seconds = float(match[1])
        result['timings'][p.name] = {
            'elapsed_seconds_rounded': seconds,
            'emulated_fps': round(600 / seconds, 3),
            'summary': re.search(r'^SIM .*$', s, re.M)[0],
            'log_sha256': digest(p)}
for p in sim.glob('runner_*'):
    if p.is_file() and p.read_bytes()[:4] == b'\x7fELF':
        result['executables'][p.name] = digest(p)
for p in sim.glob('model_*.txt'):
    (out / p.name).write_bytes(p.read_bytes())
(out / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
print('Six matched 600-vblank traces; results saved to', out)
