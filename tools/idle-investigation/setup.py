#!/usr/bin/env python3
"""Prepare a small, private copy of the supplied simulator and this worktree core."""
import argparse,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--sim',type=Path,default=Path('/tmp/idle-inv-sim'));p.add_argument('--devtools',type=Path,default=Path('/home/tony/Development/PS1_3ds/devtools'));p.add_argument('--mode',choices=('probe','fast'),default='probe');args=p.parse_args()
root=Path(__file__).resolve().parents[2];here=Path(__file__).resolve().parent;dst=args.sim;dst.mkdir(parents=True,exist_ok=True)
subprocess.run(['rsync','-a','--exclude=.git','--exclude=*.o','--exclude=*.a','--exclude=*.d',str(root/'core')+'/',str(dst/'core')+'/'],check=True)
# Source timestamps can go backwards when changing measurement mode.
for pattern in ('*.o', '*.a', '*.d'):
 for artifact in (dst/'core').rglob(pattern):artifact.unlink()
for f in ('hotblk.py','cprof.py','idis.py','ip.py','o3hw_off.ini','pcsx-card2.mcd'):
 shutil.copy2(args.devtools/'sim'/f,dst/f)
for d in ('states','plugin'):shutil.copytree(args.devtools/'sim'/d,dst/d,dirs_exist_ok=True)
shutil.copytree(here/'fake3ds',dst/'fake3ds',dirs_exist_ok=True)
shutil.copy2(here/'runner.c',dst/'runner.c')
# Reuse the supplied Linux ARM build configuration, with current source files.
f=dst/'core/Makefile.libretro';s=f.read_text();old=(args.devtools/'sim/core_idle/Makefile.libretro').read_text();spos=0
for _ in range(2):
 a=old.index('else ifeq ($(platform), ctrsim)',spos);b=old.index('else ifeq ($(platform), ctr)',a);idx=s.index('else ifeq ($(platform), ctr)',spos);s=s[:idx]+old[a:b]+s[idx:];spos=idx+len(old[a:b])+len('else ifeq ($(platform), ctr)')
f.write_text(s)
f=dst/'core/libpcsxcore/psxmem.c';s=f.read_text().replace('ptr = psxMap(0x80000000, 0x00210000, 1, MAP_TAG_RAM);','#ifndef PSX3D_SIM\n ptr = psxMap(0x80000000, 0x00210000, 1, MAP_TAG_RAM);\n#else\n ptr = MAP_FAILED;\n#endif');f.write_text(s)
f=dst/'core/libpcsxcore/new_dynarec/new_dynarec.c';s=f.read_text();old=(args.devtools/'sim/core_idle/libpcsxcore/new_dynarec/new_dynarec.c').read_text();a=old.index('#ifdef PSX3D_SIM\n  { // host');b=old.index('  block_info_finish',a);idx=s.rindex('  block_info_finish(&st, block, beginning, out);');s=s[:idx]+old[a:b]+s[idx:];f.write_text(s)
if args.mode=='probe':
 f=dst/'core/libpcsxcore/psxevents.c';s=f.read_text();old=(args.devtools/'sim/core_idle/libpcsxcore/psxevents.c').read_text();a=old.index('#ifdef PSX3D_SIM',old.index('void irq_test'));b=old.index('static void gen_interupt_',a);s=s.replace('static void gen_interupt_',old[a:b]+'static void gen_interupt_',1);s=s.replace('PROF_BEGIN(PROF_IRQ);','#ifdef PSX3D_SIM\n if (idleprof_last != 1) idleprof_sample(cp0TOpsxRegs(cp0));\n#endif\n PROF_BEGIN(PROF_IRQ);',1);f.write_text(s)
 subprocess.run(['python3',here/'instrument.py',dst/'core'],check=True)
print(f'Prepared {args.mode} sources in {dst}; build with MODE={args.mode} {here}/build.sh')
