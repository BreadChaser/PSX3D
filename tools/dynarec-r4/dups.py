#!/usr/bin/env python3
"""How many separate translations of each executed guest instruction run per frame."""
import re,sys,struct,collections
from pathlib import Path
root=Path(sys.argv[1]); key=sys.argv[2]; d=root/f'{key}-600'
def load(dd):
    v={}
    for line in (dd/'cache.txt.detail').read_text().splitlines():
        p,w,*x=line.split(); v[int(p,16)]=(int(x[0]),int(x[1]))
    return v
b=load(root/f'{key}-300'); a=load(d)
lo=int(re.search(r'code arena ([0-9a-f]+)',(d/'run.log').read_text())[1],16)
tags=(d/'code.tags').read_bytes(); pcs=struct.unpack('<%dI'%len(tags),(d/'code.pcs').read_bytes())
arms=collections.defaultdict(list); n_arm=collections.Counter(); m_arm=collections.Counter()
for p,(n1,m1) in a.items():
    i=(p-lo)//4
    if not 0<=i<len(tags) or not pcs[i] or tags[i]==4: continue
    n0,m0=b.get(p,(0,0)); n=(n1-n0)/300
    if n<=0: continue
    arms[pcs[i]].append(p)
copies=collections.Counter(); bytes_by=collections.Counter()
for g,ps in arms.items():
    ps.sort(); c=1
    for x,y in zip(ps,ps[1:]):
        if y-x>512: c+=1
    copies[c]+=1
tot=sum(copies.values())
print(key,'guest pcs executed',tot,' copies histogram',dict(sorted(copies.items())),' mean %.2f'%(sum(k*v for k,v in copies.items())/tot))
