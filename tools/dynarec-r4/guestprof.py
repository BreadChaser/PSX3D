#!/usr/bin/env python3
"""Guest-level profile from the annotated simulator (frames 301-600, per vblank):
guest instruction executions per pc, ARM instructions and I-misses charged to them,
grouped into hot regions (contiguous executed guest code)."""
import re,sys,struct,collections
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
from mdis import dis
root=Path(sys.argv[1]); key=sys.argv[2]; top=int(sys.argv[3]) if len(sys.argv)>3 else 20
d=root/f'{key}-600'
def load(dd):
    v={}
    for line in (dd/'cache.txt.detail').read_text().splitlines():
        p,w,*x=line.split(); v[int(p,16)]=(int(x[0]),int(x[1]))
    return v
b=load(root/f'{key}-300'); a=load(d)
lo=int(re.search(r'code arena ([0-9a-f]+)',(d/'run.log').read_text())[1],16)
tags=(d/'code.tags').read_bytes()
pcs=struct.unpack('<%dI'%len(tags),(d/'code.pcs').read_bytes()); ram=(d/'code.ram').read_bytes()
gx=collections.Counter(); arm=collections.Counter(); mis=collections.Counter()
for p,(n1,m1) in a.items():
    i=(p-lo)//4
    if not 0<=i<len(tags) or not pcs[i]: continue
    n0,m0=b.get(p,(0,0)); n=(n1-n0)/300; m=(m1-m0)/300
    if n<=0: continue
    g=pcs[i]; arm[g]+=n; mis[g]+=m
    if tags[i]!=4: gx[g]=max(gx[g],n)
tot=sum(gx.values()); print('%s guest insns/vblank %.0f, generated ARM %.0f, I-miss %.0f'%(key,tot,sum(arm.values()),sum(mis.values())))
# regions: runs of guest pcs with gaps <= 0x40
reg=[]; cur=None
for g in sorted(gx):
    if cur and g-cur[1]<=0x40: cur[1]=g; cur[2].append(g)
    else:
        cur=[g,g,[g]]; reg.append(cur)
rows=[]
for s,e,gs in reg: rows.append((sum(gx[g] for g in gs),sum(arm[g] for g in gs),sum(mis[g] for g in gs),s,e))
rows.sort(reverse=True)
for gi,ai,mi,s,e in rows[:top]:
    print('%08x-%08x guest %7.0f (%4.1f%%) arm %7.0f miss %5.0f'%(s,e,gi,100*gi/tot,ai,mi))
