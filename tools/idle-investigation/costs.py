#!/usr/bin/env python3
"""Attribute QEMU hot blocks to guest PCs; same cache model as sim/cprof.py."""
import sys,bisect,collections,json
from pathlib import Path
sim=Path(sys.argv[1]); label=sys.argv[2]
mapping={}; starts=[]; ends=[]
for line in (sim/f'bm_{label}_600.txt').read_text().splitlines():
 a,v=line.split();a=int(a,16)
 if v=='e':ends.append(a);mapping[a]=None
 elif v.startswith('-'):starts.append(a)
 else:mapping[a]=int(v,16)
host=sorted(mapping);values=[mapping[a] for a in host]; tc_lo=min(starts); tc_hi=max(ends)
def read(n):
 d={}
 for line in (sim/f'cs_{label}_{n}.txt').read_text().splitlines():
  x=line.split();d[int(x[0],16),int(x[1])]=[int(y) for y in x[2:]]
 return d
a,b=read(300),read(600);total=[0]*5;by=collections.defaultdict(lambda:[0.,0.,0.]);entries=collections.Counter()
for (pc,n),v in b.items():
 av=a.get((pc,n),[0]*5);d=[v[j]-av[j] for j in range(5)]
 if d[0]<=0:continue
 total[0]+=n*d[0]
 for j in range(1,5):total[j]+=d[j]
 # Attribute each ARM instruction, distributing cache penalties in its TB.
 for addr in range(pc,pc+n*4,4):
  j=bisect.bisect_right(host,addr)-1
  if j<0 or values[j] is None:continue
  guest=values[j]
  # Only addresses inside a translated block, not ordinary native functions.
  if not (tc_lo<=addr<tc_hi):continue
  by[guest][0]+=d[0];by[guest][1]+=(70*d[1]+70*d[2]+4*d[3])/n;by[guest][2]+=d[4]/n
  if addr==host[j]:entries[guest]+=d[0]
rows=[]
for pc,v in by.items():rows.append({'pc':f'{pc:08x}','instructions':v[0]/300,'cycles':sum(v[:2])/300,'interlocks':v[2]/300,'entries':entries[pc]/300})
rows.sort(key=lambda r:-r['cycles'])
res={'label':label,'instructions':total[0]/300,'cycles':(total[0]+70*total[1]+70*total[2]+4*total[3])/300,'interlocks':total[4]/300,'guest':rows}
(sim/f'{label}.json').write_text(json.dumps(res,indent=2))
print(label,'ins/frame',round(res['instructions']),'model cycles/frame',round(res['cycles']))
for r in rows[:30]:print(r['pc'],round(r['cycles']),round(r['entries'],1))
