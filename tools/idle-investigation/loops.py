#!/usr/bin/env python3
"""Rank local backward branches by sampled guest cycles; emit reproducible MIPS evidence."""
import struct,sys
from pathlib import Path
p=Path(sys.argv[1]);d=p.read_bytes();h=struct.unpack('<524289Q',d[:4194312]);ram=d[4194312:]; total=sum(h)
w=struct.unpack('<524288I',ram)
rows=[]
for i,c in enumerate(w):
 op=c>>26
 if op not in (1,4,5,6,7):continue
 offset=(c&65535);offset=offset-65536 if offset&32768 else offset
 t=i+1+offset
 if not (0<=t<=i and i-t<96):continue
 cycles=sum(h[t:i+2]); calls=sum((x>>26 in (3,)) or (x>>26==0 and x&63==9) for x in w[t:i+2]);stores=sum(x>>26 in (40,41,42,43,46,50,58) for x in w[t:i+2]);gte=sum(x>>26 in (18,50,58) for x in w[t:i+2])
 if cycles:rows.append((cycles,t,i,calls,stores,gte))
seen=set()
for cy,t,i,ca,st,gt in sorted(rows,reverse=True):
 if any(t<=a<=i and t<=b<=i for a,b in seen):continue
 seen.add((t,i))
 print(f'{0x80000000+t*4:08x}..{0x80000000+i*4:08x} {100*cy/total:.3f}% sample {cy/600:.1f} cyc/frame calls={ca} stores={st} gte={gt}')
 if len(seen)>=30:break
