#!/usr/bin/env python3
"""Compare complete hash timelines and serialized output from two offline runs."""
import argparse
import hashlib
import json
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('baseline', type=Path)
p.add_argument('candidate', type=Path)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
def read(path):
    return {r['game']: r for r in json.loads((path / 'results.json').read_text())}
b, c = read(a.baseline), read(a.candidate)
assert b.keys() == c.keys(), 'incomplete/different game set'
rows = []
for game, ref in b.items():
    new = c[game]
    if ref.get('exit') or new.get('exit'):
        rows.append(dict(game=game, frames=ref['frames'], baseline_exit=ref.get('exit'), candidate_exit=new.get('exit'),
                         timeline_equal=ref['hashes'] == new['hashes'] and ref.get('exit') == new.get('exit'),
                         serialized_equal=ref.get('exit') == new.get('exit'), crashed=True))
        continue
    assert sum(s.startswith('frame ') for s in ref['hashes']) == ref['frames'], 'missing draw checks'
    assert sum(s.startswith('checkpoint ') for s in ref['hashes']) == (ref['frames'] + 59)//60, 'missing state checkpoints'
    assert any(s.startswith('draw hash ') for s in ref['hashes']), 'missing final draw hash'
    assert any(s.startswith('state hash ') for s in ref['hashes']), 'missing final state hash'
    assert ref['frames'] == new['frames']
    if any(s.startswith('irq trace ') for s in ref['hashes']):
        assert sum(s.startswith('irq trace ') for s in ref['hashes']) == (ref['frames']+59)//60
        assert sum(s.startswith('irq trace ') for s in new['hashes']) == (ref['frames']+59)//60

    def state(root, r):
        return (root / f"{game}-{r['frames']}" / 'state.bin').read_bytes()
    before, after = state(a.baseline, ref), state(a.candidate, new)
    assert before, 'empty serialization'
    row = dict(game=game, frames=ref['frames'],
               timeline_equal=ref['hashes'] == new['hashes'],
               frame_draw_checks=sum(s.startswith('frame ') for s in ref['hashes']),
               checkpoints=sum(s.startswith('checkpoint ') for s in ref['hashes']),
               serialized_bytes=len(before), serialized_equal=before == after,
               baseline_sha256=hashlib.sha256(before).hexdigest(),
               candidate_sha256=hashlib.sha256(after).hexdigest())
    rows.append(row)
result = dict(baseline=str(a.baseline.resolve()), candidate=str(a.candidate.resolve()),
              passed=all(r['timeline_equal'] and r['serialized_equal'] for r in rows), games=rows)
a.output.write_text(json.dumps(result, indent=2) + '\n')
print(f"{len(rows)} games ({sum(1 for r in rows if r.get('crashed'))} fail identically in both): {'PASS' if result['passed'] else 'FAIL'}")
raise SystemExit(0 if result['passed'] else 1)
