#!/usr/bin/env python3
"""Executed generated instructions per vblank by (site, mnemonic); optional site filter."""
import re,struct,collections,subprocess,tempfile,sys
from pathlib import Path
src=(Path(__file__).parent/'annotate.py').read_text()
SITES=['-']+eval(re.search(r'SITES = (\[.*?\])',src,re.S)[1])
root=Path(sys.argv[1]); key=sys.argv[2]; want=sys.argv[3] if len(sys.argv)>3 else None
d=root/f'{key}-600'
def load(dd):
    v={}
    for line in (dd/'cache.txt.detail').read_text().splitlines():
        p,w,*x=line.split(); v[int(p,16)]=(int(x[0]),int(x[1]))
    return v
b=load(root/f'{key}-300'); a=load(d)
lo=int(re.search(r'code arena ([0-9a-f]+)',(d/'run.log').read_text())[1],16)
sites=(d/'code.sites').read_bytes()
with tempfile.NamedTemporaryFile(suffix='.bin') as f:
    f.write((d/'code.bin').read_bytes()); f.flush()
    dis=subprocess.check_output(['arm-linux-gnueabihf-objdump','-D','-b','binary','-marm','--adjust-vma=%#x'%lo,f.name],text=True)
L={}
for l in dis.splitlines():
    m=re.match(r'\s*([0-9a-f]+):\s+[0-9a-f]+\s+(\S+)\s*(.*)',l)
    if m: L[int(m[1],16)]=(m[2],m[3])
c=collections.Counter(); mi=collections.Counter()
for p,(n1,m1) in a.items():
    i=(p-lo)//4
    if not 0<=i<len(sites): continue
    s=SITES[sites[i]]
    if want and s!=want: continue
    n0,m0=b.get(p,(0,0)); n=(n1-n0)/300
    if n<=0: continue
    mn,ops=L.get(p,('?',''))
    ops2=ops if ops.startswith('sp, sp, #0') else re.sub(r'#-?(0x)?[0-9a-f]+','#I',re.sub(r'\br\d+\b|\b(ip|lr|fp|sl)\b','R',ops))
    if mn[0]=='b' and ops.startswith('0x'): ops2='T'
    k=(s,mn,ops2) if want else (s,'','')
    c[k]+=n; mi[k]+=(m1-m0)/300
for k,v in c.most_common(40): print('%9.0f %6.0f  %s'%(v,mi[k],' '.join(k)))
