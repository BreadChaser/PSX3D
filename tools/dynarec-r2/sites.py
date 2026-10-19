#!/usr/bin/env python3
"""Executed generated instructions/I-misses per vblank by (class tag, emitting function site)."""
import collections, re, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
src = (Path(__file__).parent/'annotate.py').read_text()
SITES = ['-'] + eval(re.search(r'SITES = (\[.*?\])', src, re.S)[1])
names = ['guest','state','cycle','mem','slow','branch','save','gte']
root = Path(sys.argv[1]); key = sys.argv[2]; want = sys.argv[3] if len(sys.argv) > 3 else None
def load(d):
    v = {}
    for line in (d/'cache.txt.detail').read_text().splitlines():
        p, w, *x = line.split(); v[int(p,16)] = list(map(int, x))
    return v
b = load(root/f'{key}-300'); a = load(root/f'{key}-600'); d = root/f'{key}-600'
lo = int(re.search(r'code arena ([0-9a-f]+)', (d/'run.log').read_text())[1], 16)
tags = (d/'code.tags').read_bytes(); sites = (d/'code.sites').read_bytes()
c = collections.defaultdict(lambda: [0, 0, 0]); bysite = collections.defaultdict(lambda: [0, 0, 0])
for p, v in a.items():
    i = (p-lo)//4
    if not 0 <= i < len(tags): continue
    n = (v[0]-b.get(p,[0]*5)[0])/300; m = (v[1]-b.get(p,[0]*5)[1])/300
    k = (names[tags[i]], SITES[sites[i]])
    if n > 0: c[k][2] += 4
    c[k][0] += n; c[k][1] += m
    s = bysite[SITES[sites[i]]]; s[0] += n; s[1] += m; s[2] += 4 if n > 0 else 0
print('by site:')
for k, v in sorted(bysite.items(), key=lambda x: -x[1][0])[:30]: print('  %-24s %9.0f ins %6.0f im %7d bytes' % (k, *v))
print('by class/site:')
for k, v in sorted(c.items(), key=lambda x: -x[1][0])[:45]:
    if want is None or k[0] == want: print('  %-7s %-24s %9.0f ins %6.0f im %7d bytes' % (k[0], k[1], *v))
