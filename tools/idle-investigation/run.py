#!/usr/bin/env python3
import os,sys,subprocess
from pathlib import Path
os.chdir(os.environ.get('IDLE_SIM','/tmp/idle-inv-sim'))
games={'o3auto':'Crash Bandicoot (USA)','sh':'Silent Hill (USA)','r4':'R4 - Ridge Racer Type 4 (USA)','ctr':'CTR - Crash Team Racing (USA)','gt2':'Gran Turismo 2 (USA) (Arcade Mode) (Rev 1)','sotn':'Castlevania - Symphony of the Night (USA)'}
g=sys.argv[1];n=sys.argv[2]; label=sys.argv[3] if len(sys.argv)>3 else g
base=Path(os.environ.get('PSX3D_DEVTOOLS','/home/tony/Development/PS1_3ds/devtools')); cue=next((base/'roms/psx'/games[g]).glob('*.cue'))
cmd=[str(base/'dl/qemu-full/build/qemu-arm')]
e=os.environ.copy()
if e.get('CACHE'):cmd+=['-plugin','plugin/libcachesim.so'];e['CACHESIM_OUT']=f'cs_{label}_{n}.txt';e['BLKMAP']=f'bm_{label}_{n}.txt'
cmd+=['./'+e.get('RUNNER','runner_probe'),str(cue),n,'states/'+g+'.st0','o3hw_off.ini']
e['SMALL']='1';e['STATEHASH']='1'
with open(f'log_{label}_{n}.txt','w') as f: r=subprocess.run(cmd,env=e,stdout=f,stderr=subprocess.STDOUT)
print(label,n,r.returncode)
sys.exit(r.returncode)
