#!/usr/bin/env python3
"""Utilisation of executed 32-byte lines of generated code (frames 301-600):
how many of a line's 8 words were executed, weighted by the line's I-misses."""
import re,sys,collections
from pathlib import Path
src=(Path(__file__).parent/'annotate.py').read_text()
SITES=['-']+eval(re.search(r'SITES = (\[.*?\])',src,re.S)[1])
names=['guest','state','cycle','mem','slow','branch','save','gte']
root=Path(sys.argv[1]); key=sys.argv[2]; d=root/f'{key}-600'
def load(dd):
    v={}
    for line in (dd/'cache.txt.detail').read_text().splitlines():
        p,w,*x=line.split(); v[int(p,16)]=(int(x[0]),int(x[1]))
    return v
b=load(root/f'{key}-300'); a=load(d)
lo=int(re.search(r'code arena ([0-9a-f]+)',(d/'run.log').read_text())[1],16)
tags=(d/'code.tags').read_bytes(); sites=(d/'code.sites').read_bytes()
ex={}; miss=collections.Counter()
for p,(n1,m1) in a.items():
    n0,m0=b.get(p,(0,0)); n=(n1-n0)/300
    if n>0: ex[p]=n
    miss[p&~31]+=(m1-m0)/300
lines=collections.defaultdict(list)
for p in ex: lines[p&~31].append(p)
hist=collections.Counter(); tot=0; dead=collections.Counter()
for L,ps in lines.items():
    k=len(ps); hist[k]+=miss[L]; tot+=miss[L]
    for w in range(8):
        p=L+4*w
        if p not in ex:
            i=(p-lo)//4
            if 0<=i<len(tags): dead[(names[tags[i]] if tags[i]<8 else '?',SITES[sites[i]])]+=miss[L]/8
print('lines executed %d, I-miss/vblank %.0f'%(len(lines),tot))
for k in range(1,9): print(' %d/8 words executed: %5.1f%% of misses'%(k,100*hist[k]/tot if tot else 0))
s=sum(hist[k]*k/8 for k in hist)/tot
print('miss-weighted utilisation %.1f%%'%(100*s))
print('unexecuted words in missed lines, by class/site (miss-weighted /8):')
for k,v in dead.most_common(15): print('  %7.0f %s'%(v,' '.join(k)))
