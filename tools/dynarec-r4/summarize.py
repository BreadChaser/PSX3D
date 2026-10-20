#!/usr/bin/env python3
"""Summarize immutable cache traces; modeled cycles are not hardware cycles."""
from pathlib import Path
import argparse
import bisect
import collections
import json
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('runner', type=Path)
p.add_argument('directory', type=Path)
a = p.parse_args()
symbols = []
for line in subprocess.check_output(['arm-linux-gnueabihf-nm','-n','-S','--defined-only',str(a.runner)], text=True).splitlines():
    s = line.split()
    if len(s)==4 and s[2] in 'tTwWbBdD': symbols.append((int(s[0],16), int(s[1],16), s[3]))
symbols.sort()
addresses = [s[0] for s in symbols]
def name(pc):
    i = bisect.bisect_right(addresses,pc)-1
    return symbols[i][2] if i>=0 and pc<symbols[i][0]+symbols[i][1] else '[translated code]'
def trace(f):
    d = {}
    for line in f.read_text().splitlines():
        pc,n,e,im,rm,wm,hz=line.split()
        d[(int(pc,16),int(n))]=list(map(int,(e,im,rm,wm,hz)))
    return d
results={}
for b in sorted(a.directory.glob('*-600/cache.txt')):
    g=b.parent.name[:-4]
    before=trace(a.directory/(g+'-300/cache.txt'))
    after=trace(b)
    attrib=collections.defaultdict(lambda:[0]*5)
    for (pc,n),v in after.items():
        base=before.get((pc,n),[0]*5)
        delta=[v[i]-base[i] for i in range(5)]
        delta[0]*=n
        dest=attrib[name(pc)]
        for i in range(5):dest[i]+=delta[i]/300
    def record(v):
        ins,im,rm,wm,hz=v
        return dict(instructions=ins,imiss=im,rmiss=rm,wmiss=wm,interlocks=hz,
                    cycles=ins+70*im+70*rm+4*wm+hz)
    totals=record([sum(v[i] for v in attrib.values()) for i in range(5)])
    totals['generated_bytes']=json.loads((b.parent/'result.json').read_text())['generated_bytes']
    totals['functions']={k:record(v) for k,v in sorted(attrib.items(),key=lambda x:-x[1][0])}
    results[g]=totals
    print(g, 'instructions=%.0f cycles=%.0f imiss=%.0f generated=%d'%(totals['instructions'],totals['cycles'],totals['imiss'],totals['generated_bytes']))
(a.directory/'summary.json').write_text(json.dumps(results,indent=2))
