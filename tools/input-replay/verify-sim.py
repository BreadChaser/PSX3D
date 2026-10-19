#!/usr/bin/env python3
"""Offline record/replay matrix. Never connects to a console."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import re
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--dev',type=Path,default=Path('/home/tony/Development/PS1_3ds/devtools'))
p.add_argument('--out',type=Path)
p.add_argument('--jobs',type=int,default=2)
p.add_argument('--frames',type=int,default=600)
p.add_argument('--options',type=Path,help='core options; default: devtools/sim/o3hw_off.ini')
p.add_argument('--all-renderers',action='store_true')
p.add_argument('--idle',action='store_true')
a=p.parse_args(); dev=a.dev; out=a.out or dev/'input-replay/matrix'; out.mkdir(parents=True,exist_ok=True)
games={'o3auto':'Crash Bandicoot (USA)','sh':'Silent Hill (USA)','r4':'R4 - Ridge Racer Type 4 (USA)', 'ctr':'CTR - Crash Team Racing (USA)', 'gt2':'Gran Turismo 2 (USA) (Arcade Mode) (Rev 1)','sotn':'Castlevania - Symphony of the Night (USA)'}
options=a.options or dev/'sim/o3hw_off.ini'
opts=out/'auto.ini'; opts.write_text(options.read_text().replace('pcsx_rearmed_frameskip_type=disabled','pcsx_rearmed_frameskip_type=auto'))
qemu=dev/'dl/qemu-full/build/qemu-arm'; gpu=dev/'input-replay/sim/runner_pica'; sw=dev/'input-replay/sim-sw/runner_pica'
def game(key):
    folder=dev/'roms/psx'/games[key]; cue=next(folder.glob('*.cue'))
    base=out/key
    def run(label,runner=gpu,**env):
        ini=opts if label.endswith('auto') else options
        cmd=[str(qemu),str(runner),str(cue),str(a.frames),str(dev/f'sim/states/{key}.st0'),str(ini)]
        # Each run has isolated shared card files, no tests mutate source cards.
        cwd=out/(key+'-runtime');cwd.mkdir(exist_ok=True)
        with (out/f'{key}-{label}.log').open('w') as f:
            cp=subprocess.run(cmd,cwd=cwd,env={**os.environ,'SMALL':'1',**env},stdout=f,stderr=subprocess.STDOUT,timeout=400)
        text=(out/f'{key}-{label}.log').read_text()
        match=re.search(r'first_divergent=(\d+)',text)
        return {'mode':label,'returncode':cp.returncode,'first_divergent':int(match[1]) if match else None,'draw_hash':(re.findall(r'draw hash (\w+)',text) or [None])[-1]}
    rows=[run('record',CLIP_RECORD=str(base),CLIP_INPUT='1')]
    if rows[-1]['returncode']: return key,rows
    for label in ('replay1','replay2','replay-auto'):
        rows.append(run(label,CLIP_REPLAY=str(base),CLIP_SKIP='1' if label.endswith('auto') else ''))
    if sw.exists():
        # Cross-renderer runs diagnose guest-visible GPU readback differences.
        # They are not an identity requirement for the recorder.
        cross=run('cross-sw-replay',runner=sw,CLIP_REPLAY=str(base))
        cross['diagnostic']=True; rows.append(cross)
        if a.all_renderers:
            swbase=str(base)+'-sw'
            rows.append(run('sw-record',runner=sw,CLIP_RECORD=swbase,CLIP_INPUT='1'))
            for label in ('sw-replay1','sw-replay2','sw-replay-auto'):
                rows.append(run(label,runner=sw,CLIP_REPLAY=swbase,CLIP_SKIP='1' if label.endswith('auto') else ''))
    if a.idle: rows.append(run('idle-off',CLIP_REPLAY=str(base),CLIP_IDLE='0'))
    print(key,json.dumps(rows),flush=True)
    return key,rows
with ThreadPoolExecutor(max_workers=a.jobs) as pool: results=dict(pool.map(game,games))
(out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
raise SystemExit(any(r['returncode'] or r['first_divergent'] != 0 for rows in results.values() for r in rows if not r.get('diagnostic')))
