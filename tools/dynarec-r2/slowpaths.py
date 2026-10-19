#!/usr/bin/env python3
"""Which guest loads/stores enter their slow stub, and with which fast-path guess."""
import re,struct,collections,subprocess,tempfile,sys
from pathlib import Path
src=(Path(__file__).parent/'annotate.py').read_text()
SITES=['-']+eval(re.search(r'SITES = (\[.*?\])',src,re.S)[1])
root=Path(sys.argv[1]); key=sys.argv[2]; d=root/f'{key}-600'
def load(dd):
    v={}
    for line in (dd/'cache.txt.detail').read_text().splitlines():
        p,w,*x=line.split(); v[int(p,16)]=int(x[0])
    return v
b=load(root/f'{key}-300'); a=load(d)
lo=int(re.search(r'code arena ([0-9a-f]+)',(d/'run.log').read_text())[1],16)
tags=(d/'code.tags').read_bytes(); sites=(d/'code.sites').read_bytes()
pcs=struct.unpack('<%dI'%len(tags),(d/'code.pcs').read_bytes()); ram=(d/'code.ram').read_bytes()
with tempfile.NamedTemporaryFile(suffix='.bin') as f:
    f.write((d/'code.bin').read_bytes()); f.flush()
    dis=subprocess.check_output(['arm-linux-gnueabihf-objdump','-D','-b','binary','-marm','--adjust-vma=%#x'%lo,f.name],text=True)
L={}
for l in dis.splitlines():
    m=re.match(r'\s*([0-9a-f]+):\s+[0-9a-f]+\s+(.*)',l)
    if m: L[int(m[1],16)]=m[2]
slow=collections.Counter(); handler=collections.Counter(); kind={}
for p,v in a.items():
    i=(p-lo)//4
    if not 0<=i<len(tags): continue
    n=(v-b.get(p,0))/300; t=L.get(p,''); s=SITES[sites[i]]
    if s in ('do_read_slow','do_write_slow') and t.startswith('lsr'): slow[(pcs[i],s[3:])]+=n
    if s in ('do_read_slow','do_write_slow') and t.startswith('bl') : handler[(pcs[i],s[3:])]+=n
    if tags[i]==3 and t.startswith('eor') and '528482304' in t: kind[pcs[i]]='spad'
    elif tags[i]==3 and t.startswith('cmp') and '#128, 18' in t: kind.setdefault(pcs[i],'ram')
tot=sum(slow.values()); print(key,'slow stub entries/vblank %.0f (calls into C handlers %.0f)'%(tot,sum(handler.values())))
byk=collections.Counter()
for (pc,s),n in slow.items(): byk[(kind.get(pc,'?'),s)]+=n
print('  by fast-path guess:',{'%s/%s'%k:int(v) for k,v in byk.most_common()})
for (pc,s),n in slow.most_common(12):
    w=struct.unpack_from('<I',ram,pc&0x1ffffc)[0]
    print('  %08x %6.0f %-5s %-10s op %02x base r%d imm %d handler %.0f'%(pc,n,kind.get(pc,'?'),s,w>>26,(w>>21)&31,(w&0xffff)-((w&0x8000)<<1),handler[(pc,s)]))
