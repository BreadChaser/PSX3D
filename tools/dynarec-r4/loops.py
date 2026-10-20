#!/usr/bin/env python3
"""Hot guest loops (backward branches) with their per-vblank iteration counts."""
import re,sys,struct,collections
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
from mdis import dis
root=Path(sys.argv[1]); key=sys.argv[2]; top=int(sys.argv[3]) if len(sys.argv)>3 else 10
show=len(sys.argv)>4
d=root/f'{key}-600'
def load(dd):
    v={}
    for line in (dd/'cache.txt.detail').read_text().splitlines():
        p,w,*x=line.split(); v[int(p,16)]=int(x[0])
    return v
b=load(root/f'{key}-300'); a=load(d)
lo=int(re.search(r'code arena ([0-9a-f]+)',(d/'run.log').read_text())[1],16)
tags=(d/'code.tags').read_bytes()
pcs=struct.unpack('<%dI'%len(tags),(d/'code.pcs').read_bytes()); ram=(d/'code.ram').read_bytes()
gx=collections.Counter()
for p,n1 in a.items():
    i=(p-lo)//4
    if 0<=i<len(tags) and pcs[i] and tags[i]!=4: gx[pcs[i]]=max(gx[pcs[i]],(n1-b.get(p,0))/300)
W=lambda g: struct.unpack_from('<I',ram,g&0x1ffffc)[0]
tot=sum(gx.values()); rows=[]
for g,n in gx.items():
    w=W(g); op=w>>26
    if op in (1,4,5,6,7):
        t=g+4+((w&0xffff)-((w&0x8000)<<1))*4
        if t<=g and g-t<0x400:
            body=sum(gx.get(t+4*k,0) for k in range((g-t)//4+2))
            rows.append((body,g,t,gx.get(t,0)))
rows.sort(reverse=True); print('%s: guest %.0f/vblank'%(key,tot))
for body,g,t,n in rows[:top]:
    print('  loop %08x..%08x body-execs %7.0f (%4.1f%%) iters %6.0f len %d'%(t,g,body,100*body/tot,gx[g],(g-t)//4+2))
    if show:
        for k in range((g-t)//4+2): print('      %6.0f %08x %s'%(gx.get(t+4*k,0),t+4*k,dis(W(t+4*k),t+4*k)))
