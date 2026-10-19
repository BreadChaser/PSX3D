#!/usr/bin/env python3
"""Run real PS-X executables against HLE and an optional user-supplied BIOS."""
import argparse,json,os,re,struct,subprocess
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('--build',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--bios',type=Path);ap.add_argument('--qemu',default='qemu-arm-static');args=ap.parse_args()
build=args.build.resolve();root=args.out.resolve();root.mkdir(parents=True,exist_ok=True)
def checksum(b,o):
 b[o+127]=0
 for x in b[o:o+127]:b[o+127]^=x

def seed(occupied):
 b=bytearray(131072);b[:2]=b'MC';checksum(b,0)
 for i in range(1,16):
  o=i*128;b[o]=0xa0;b[o+8:o+10]=b'\xff\xff'
  if i in occupied:
   b[o]=0x51;struct.pack_into('<I',b,o+4,8192);name=f'KEEP{i}'.encode();b[o+10:o+10+len(name)]=name;b[i*8192:(i+1)*8192]=bytes([0xa0+i])*8192
  checksum(b,o)
 for i in range(16,36):
  o=i*128;b[o:o+4]=b'\xff'*4;b[o+8:o+10]=b'\xff\xff'
 return b

cases=[('empty1',1,[]),('empty2',2,[]),('fragmented2',2,[2]),('fragmented3',3,[2,4]),('insufficient',2,list(range(2,16))),('full',1,list(range(1,16))),('last',1,list(range(1,15)))]
results=[];failed=False
for action in ['keep','delete']:
 for name,n,occupied in cases:
  cards=[]
  for bios in ['hle','real'] if args.bios else ['hle']:
   d=root/action/name/bios;d.mkdir(parents=True,exist_ok=True);original=seed(occupied);card=d/'card.mcd';card.write_bytes(original)
   if bios=='real':
    link=d/'ps-22a.bin'
    if not link.exists():link.symlink_to(args.bios.resolve())
   opts=d/'options.ini';opts.write_text(f'pcsx_rearmed_bios={"HLE" if bios=="hle" else "auto"}\npcsx_rearmed_spu_thread=disabled\npcsx_rearmed_memcard1=libretro\npcsx_rearmed_memcard2=disabled\npcsx_rearmed_cd_readahead=0\npcsx_rearmed_drc_thread=disabled\n')
   env=os.environ.copy();env.update(PROBE_RAM='1',CARD=str(card))
   with (d/'run.log').open('w') as f:
    r=subprocess.run([args.qemu,str(build/'runner-hle'),str(build/f'card-{n}-{action}.exe'),'3000','-',str(opts)],cwd=d,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=120)
   log=(d/'run.log').read_text();magic=re.findall(r'PROBERAM (\w+) \w+ \w+ \w+ (\w+)',log)
   v={int(k):int(x,16)for k,x in re.findall(r'PROBERES (\d+) (\w+)',log)}
   b=card.read_bytes();cards.append(b);damage=[i for i in occupied if b[128*i:128*(i+1)]!=original[128*i:128*(i+1)] or b[8192*i:8192*(i+1)]!=original[8192*i:8192*(i+1)]]
   expect_failure=len(occupied)+n>15
   ok=r.returncode==0 and bool(magic) and magic[-1][0]=='deadbeef' and not damage
   if expect_failure:ok=ok and magic[-1][1]=='ffffffff' and b[:0x1f80]+b[0x2000:]==original[:0x1f80]+original[0x2000:]
   else:
    ok=ok and magic[-1][1]=='2' and v.get(5)==n*8192 and v.get(8)==n*8192 and v.get(9)==0 and v.get(12)==n*8192
    if n>1:ok=ok and v.get(16)==256 and v.get(17)==0 and v.get(18)==256 and v.get(19)==0
    if action=='delete':ok=ok and v.get(13)==1
   row=dict(case=name,action=action,bios=bios,ok=bool(ok),damaged_blocks=damage,results=v);results.append(row);failed|=not ok;print(json.dumps(row),flush=True)
  if len(cards)==2:
   # BIOS writes an extra backup MC header into the final sector of block 0.
   same=cards[0][:0x1f80]+cards[0][0x2000:]==cards[1][:0x1f80]+cards[1][0x2000:]
   failed|=not same
   results.append(dict(case=name,action=action,oracle_card_match=same))
(root/'results.json').write_text(json.dumps(results,indent=2)+'\n')
raise SystemExit(int(failed))
