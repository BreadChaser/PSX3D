#!/usr/bin/env python3
"""Paired private cache-model runs, plus output/capture traces. Never console I/O."""
import concurrent.futures as cf, json, os, pathlib, subprocess, sys
sim=pathlib.Path(sys.argv[1]).resolve(); dest=pathlib.Path(sys.argv[2]).resolve(); dest.mkdir(parents=True,exist_ok=True)
root=pathlib.Path(__file__).resolve().parents[2]
roms=pathlib.Path('/home/tony/Development/PS1_3ds/devtools/roms/psx')
games={'o3auto':'Crash Bandicoot (USA)','ctr':'CTR - Crash Team Racing (USA)',
       'gt2':'Gran Turismo 2 (USA) (Arcade Mode) (Rev 1)','sotn':'Castlevania - Symphony of the Night (USA)'}
qemu='/home/tony/Development/PS1_3ds/devtools/dl/qemu-full/build/qemu-arm'
base_env=dict(os.environ,SMALL='1')
def run(game,mode,variant,cache=False):
    title=games[game]; tag=f'{variant}_{game}_{mode}'; env=dict(base_env)
    if variant=='after': env['FRONTEND_NEW']='1'
    ini='o3hw_off.ini' if mode in ('off','paced_off') else 'o3hw_auto.ini'
    if mode=='auto': env['REQUEST_SKIP']='1'
    if mode=='rtfs': env.update(RTFS='1',LEVEL='3') # same selected 1/2 cadence
    if mode.startswith('paced'): env.update(PACED='1',WARMUP='300')
    if mode=='paced': env['RTFS']='1'
    if mode=='fs4': ini='o3hw_fs4.ini'
    if mode=='fs1': ini='o3hw_fs1.ini'
    binary=f'runner_{variant}'+('_hash' if '--hash' in sys.argv else '')
    if '--hash' in sys.argv: env['HASHEVERY']='1'
    if cache:
        for n in [300,600]:
            e=dict(env,CACHESIM_OUT=str(dest/f'{tag}_{n}.cache'))
            with (dest/f'{tag}_{n}.log').open('w') as log:
                subprocess.run([qemu,'-plugin',str(sim/'plugin/libcachesim.so'),str(sim/binary),str(roms/title/(title+'.cue')),str(n),str(sim/'states'/f'{game}.st0'),str(sim/ini)],env=e,cwd=sim,stdout=subprocess.DEVNULL,stderr=log,check=True)
        with (dest/f'{tag}.cycles').open('w') as out:
            subprocess.run(['python3',str(sim/'cprof.py'),str(sim/binary),str(dest/f'{tag}_300.cache'),str(dest/f'{tag}_600.cache'),'300','60'],stdout=out,env=env,check=True)
    else:
        with (dest/f'{tag}.log').open('w') as log:
            subprocess.run([qemu,str(sim/binary),str(roms/title/(title+'.cue')),'600',str(sim/'states'/f'{game}.st0'),str(sim/ini)],env=env,cwd=sim,stdout=subprocess.DEVNULL,stderr=log,check=True)
    print(tag,flush=True)
mode_list=['off','auto','rtfs'] if '--cache' in sys.argv else ['off','auto','rtfs','fs1','fs4'] if '--hash' in sys.argv else ['paced_off','paced_auto','paced']
with cf.ThreadPoolExecutor(max_workers=2) as pool:
    futures=[pool.submit(run,g,m,v,'--cache' in sys.argv) for g in games for m in mode_list for v in (['after'] if '--after-only' in sys.argv else ['before','after'])]
    for f in futures: f.result()
