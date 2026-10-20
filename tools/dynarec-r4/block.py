#!/usr/bin/env python3
"""Annotated generated code around a guest pc range: block.py root key lo hi"""
import re,sys,struct,subprocess,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
from mdis import dis
src=(Path(__file__).parent/'annotate.py').read_text()
SITES=['-']+eval(re.search(r'SITES = (\[.*?\])',src,re.S)[1])
names=['guest','state','cycle','mem','slow','branch','save','gte']
root=Path(sys.argv[1]); key=sys.argv[2]; glo=int(sys.argv[3],16); ghi=int(sys.argv[4],16)
d=root/f'{key}-600'
def load(dd):
    v={}
    for line in (dd/'cache.txt.detail').read_text().splitlines():
        p,w,*x=line.split(); v[int(p,16)]=(int(x[0]),int(x[1]))
    return v
b=load(root/f'{key}-300'); a=load(d)
lo=int(re.search(r'code arena ([0-9a-f]+)',(d/'run.log').read_text())[1],16)
tags=(d/'code.tags').read_bytes(); sites=(d/'code.sites').read_bytes()
pcs=struct.unpack('<%dI'%len(tags),(d/'code.pcs').read_bytes()); ram=(d/'code.ram').read_bytes()
idx=[i for i in range(len(tags)) if glo<=pcs[i]<=ghi and (lo+4*i) in a and a[lo+4*i][0]-b.get(lo+4*i,(0,0))[0]>0]
if not idx: sys.exit('none')
i0=min(idx); i1=max(idx)
code=(d/'code.bin').read_bytes()
with tempfile.NamedTemporaryFile(suffix='.bin') as f:
    f.write(code[i0*4:(i1+1)*4]); f.flush()
    dis_=subprocess.check_output(['arm-linux-gnueabihf-objdump','-D','-b','binary','-marm','--adjust-vma=%#x'%(lo+4*i0),f.name],text=True)
L={}
for l in dis_.splitlines():
    m=re.match(r'\s*([0-9a-f]+):\s+[0-9a-f]+\s+(.*)',l)
    if m: L[int(m[1],16)]=m[2]
lastg=None
for i in range(i0,i1+1):
    p=lo+4*i; n=(a.get(p,(0,0))[0]-b.get(p,(0,0))[0])/300
    g=pcs[i]
    if g!=lastg and g:
        w=struct.unpack_from('<I',ram,g&0x1ffffc)[0]; print('            ; %08x  %s'%(g,dis(w,g))); lastg=g
    print('%8.0f %-6s %-20s %s'%(n,names[tags[i]] if tags[i]<8 else '?',SITES[sites[i]][:20],L.get(p,'?')))
