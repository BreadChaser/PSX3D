#!/usr/bin/env python3
"""Positive/negative guest programs; compare complete state across event boundaries."""
import argparse,concurrent.futures,os,re,struct,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--sim',type=Path,default=Path('/tmp/idle-inv-sim'));p.add_argument('--frames',type=int,default=600);p.add_argument('--workers',type=int,default=3);args=p.parse_args();dst=args.sim/'synthetic';dst.mkdir(exist_ok=True)
q=Path('/home/tony/Development/PS1_3ds/devtools/dl/qemu-full/build/qemu-arm')
programs=[]
for count in (1,2,9,65,10000,0,-1):
 programs.append((f'delay-{count}',f''' li $a0,{count}
countdown:
 addiu $a0,$a0,-1
 bnez $a0,countdown
 nop
''',29,count>65 or count<=0))
for kind,target,body in [('pointer', '0x80020020',''),('io-pointer','0x1f801070',''),('stored-poll','0x80020020',' sw $zero,8($s0)')]:
 programs.append((kind,f''' lui $t1,{int(target,16)>>16}
 ori $t1,$t1,{int(target,16)&65535}
 sw $t1,0($s0)
 sw $zero,32($s0)
 sw $zero,0x1070($t0)
wait:
 lui $t0,0x1f80
 lw $v1,0($s0)
 lw $a2,0x1070($t0)
 lw $v0,0($v1)
 nop
{body}
 bnez $v0,wait
 nop
 andi $a2,$a2,64
 beqz $a2,wait
 nop
''',257,kind=='pointer'))

def exe(name,body,period):
 source=f''' .set noreorder
 .text
start:
 mtc0 $zero,$12
 lui $t0,0x1f80
 sw $zero,0x1074($t0)
 lui $s0,0x8002
 li $s1,0
 li $t1,{period}
 sw $t1,0x1128($t0)
 ori $t1,$zero,0x258
 sw $t1,0x1124($t0)
outer:
{body}
 addiu $s1,$s1,1
 sw $s1,4($s0)
 beq $zero,$zero,outer
 nop
'''
 s=dst/f'{name}.s';s.write_text(source);o=s.with_suffix('.o');b=s.with_suffix('.bin')
 subprocess.run(['llvm-mc','-triple=mipsel','-mcpu=mips1','-filetype=obj',s,'-o',o],check=True)
 subprocess.run(['llvm-objcopy','-O','binary','-j','.text',o,b],check=True)
 code=b.read_bytes();code+=b'\0'*(-len(code)%2048);h=bytearray(2048);h[:8]=b'PS-X EXE';struct.pack_into('<IIII',h,16,0x80010000,0,0x80010000,len(code));struct.pack_into('<II',h,48,0x801ffff0,0);out=s.with_suffix('.exe');out.write_bytes(h+code);return out

jobs=[(name,exe(name,body,period),should_skip) for name,body,period,should_skip in programs]
def run(j):
 name,program,should_skip=j;logs=[]
 for mode in ('off','on'):
  env=dict(os.environ,SMALL='1',STATEHASH='1')
  for key in ('IDLEPTR','IDLEDELAY','NOIDLE'):env.pop(key,None)
  if mode=='on':env.update(IDLEPTR='1',IDLEDELAY='1')
  out=dst/f'{name}-{mode}.log'
  with out.open('w') as f:subprocess.run([q,args.sim/'runner_probe',program,str(args.frames),'-',args.sim/'o3hw_off.ini'],cwd=args.sim,env=env,stdout=f,stderr=f,check=True)
  logs.append(out.read_text())
 def checks(s):return re.findall(r'^(?:METRIC|draw hash|state hash).*$',s,re.M)
 assert checks(logs[0])==checks(logs[1]),name+' output/state mismatch'
 skip=sum(int(x) for x in re.findall(fr'^IDLE {args.frames} \w+ \d+ (\d+)',logs[1],re.M))
 if should_skip:assert skip>0,name+' did not exercise acceleration'
 if name in ('io-pointer','stored-poll'):assert skip==0,name+' must fail closed'
 return {'name':name,'skipped_cycles':skip,'status':'pass','checks':checks(logs[1])}
with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:results=list(pool.map(run,jobs))
import json
(dst/'results.json').write_text(json.dumps(results,indent=2))
for r in results:print(r['name'],r['status'],r['skipped_cycles'])
