#!/usr/bin/env python3
"""Guest pcs with the most executed state traffic (register file loads/stores) per vblank."""
import re,sys,struct,collections,subprocess,tempfile
from pathlib import Path
src=(Path(__file__).parent/'annotate.py').read_text()
SITES=['-']+eval(re.search(r'SITES = (\[.*?\])',src,re.S)[1])
root=Path(sys.argv[1]); key=sys.argv[2]; top=int(sys.argv[3]) if len(sys.argv)>3 else 20
d=root/f'{key}-600'
def load(dd):
    v={}
    for line in (dd/'cache.txt.detail').read_text().splitlines():
        p,w,*x=line.split(); v[int(p,16)]=int(x[0])
    return v
b=load(root/f'{key}-300'); a=load(d)
lo=int(re.search(r'code arena ([0-9a-f]+)',(d/'run.log').read_text())[1],16)
tags=(d/'code.tags').read_bytes(); sites=(d/'code.sites').read_bytes(); pcs=struct.unpack('<%dI'%len(tags),(d/'code.pcs').read_bytes())
c=collections.Counter(); ks=collections.defaultdict(collections.Counter)
for p,n1 in a.items():
    i=(p-lo)//4
    if not 0<=i<len(tags) or tags[i]!=1: continue
    n=(n1-b.get(p,0))/300
    if n<=0: continue
    c[pcs[i]&~0x3f]+=n; ks[pcs[i]&~0x3f][SITES[sites[i]]]+=n
tot=sum(c.values()); print(key,'state insns/vblank %.0f'%tot)
for g,n in c.most_common(top): print('  %08x %7.0f %s'%(g,n,dict(ks[g].most_common(4))))
