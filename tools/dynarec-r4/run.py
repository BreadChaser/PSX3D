#!/usr/bin/env python3
"""Offline six-state / real-BIOS comparison and cache-model driver."""
import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import re
import subprocess

DEV = Path('/home/tony/Development/PS1_3ds/devtools')
GAMES = dict(o3auto='Crash Bandicoot (USA)', sh='Silent Hill (USA)',
             r4='R4 - Ridge Racer Type 4 (USA)', ctr='CTR - Crash Team Racing (USA)',
             gt2='Gran Turismo 2 (USA) (Arcade Mode) (Rev 1)',
             sotn='Castlevania - Symphony of the Night (USA)')
HEAVY = dict(crash='Crash Bandicoot (USA)', crash2='Crash Bandicoot 2 - Cortex Strikes Back (USA)',
             ctr='CTR - Crash Team Racing (USA)', ff7='Final Fantasy VII (USA) (Disc 1)',
             gt2='Gran Turismo 2 (USA) (Arcade Mode) (Rev 1)', klonoa='Klonoa - Door to Phantomile (USA)',
             nfs='Need for Speed - High Stakes (USA)', r4='R4 - Ridge Racer Type 4 (USA)',
             **{'sh-alley': 'Silent Hill (USA)', 'sh-cafe': 'Silent Hill (USA)', 'sh-garage': 'Silent Hill (USA)'},
             sotn='Castlevania - Symphony of the Night (USA)', tekken='Tekken 3 (USA)')
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('sim', type=Path)
p.add_argument('out', type=Path)
p.add_argument('--mode', choices=('profile', 'states', 'boot'), default='states')
p.add_argument('--runner', default='runner')
p.add_argument('--frames', type=int, default=600)
p.add_argument('--jobs', type=int, default=2)
p.add_argument('--switch', default='0')
p.add_argument('--interpreter', action='store_true')
p.add_argument('--hle', action='store_true', help='boot mode with the built-in HLE BIOS (the shipping configuration)')
p.add_argument('--detail', action='store_true')
p.add_argument('--game', help='One state key or exact boot directory name (comma list allowed)')
p.add_argument('--heavy', action='store_true', help='heavy benchmark scenes (HLE BIOS, console-like sound options)')
p.add_argument('--respec', choices=('save','load'), help='keep the respec memo per state in <out>/../respec-<key>.bin')
p.add_argument('--options', help='options ini (default o3hw_off.ini; heavy: bench-pool/hle.ini)')
a = p.parse_args()
a.sim = a.sim.resolve()
a.out = a.out.resolve()
a.out.mkdir(parents=True, exist_ok=True)
options = Path(a.options or (DEV / 'bench-pool/hle.ini' if a.heavy else a.sim / 'o3hw_off.ini')).read_text()
if a.mode == 'boot':
    options = options.replace('pcsx_rearmed_bios=auto', 'pcsx_rearmed_bios=HLE' if a.hle else 'pcsx_rearmed_bios=auto')
if a.interpreter:
    options = options.replace('pcsx_rearmed_drc=enabled', 'pcsx_rearmed_drc=disabled')
(a.out / 'options.ini').write_text(options)
games = (HEAVY if a.heavy else GAMES) if a.mode != 'boot' else {d.name: d.name for d in sorted((DEV / 'roms/psx').iterdir()) if d.is_dir()}
if a.game:
    games = {g: games[g] for g in a.game.split(',')}
tasks = [(k, n) for k in games for n in ([300, 600] if a.mode == 'profile' else [a.frames])]

def run(item):
    key, n = item
    work = a.out / f'{key}-{n}'
    work.mkdir(exist_ok=True)
    cue = sorted((DEV / 'roms/psx' / games[key]).glob('*.cue'))[0]
    env = dict(os.environ, SMALL='1', DYNR3=a.switch, CODEOUT=str(work/'code'))
    if a.respec:
        env['RESPEC_' + a.respec.upper()] = str(a.out.parent / f'respec-{key}.bin')
    cmd = [str(DEV / 'dl/qemu-full/build/qemu-arm')]
    if a.mode == 'profile':
        env['CACHESIM_OUT'] = str(work / 'cache.txt')
        plugin = DEV / ('dynarec-r2/libcachesim-detail.so' if a.detail else 'sim/plugin/libcachesim.so')
        if a.detail:
            nm=subprocess.check_output(['arm-linux-gnueabihf-nm','-S',str(a.sim/a.runner)],text=True)
            lo,size=re.search(r'([0-9a-f]+) ([0-9a-f]+) [bB] ndrc_bss',nm).groups()
            env.update(DETAIL_LO=hex(int(lo,16)), DETAIL_HI=hex(int(lo,16)+int(size,16)))
        cmd += ['-plugin', str(plugin)]
    else:
        env.update(CHECKPOINTS='1', HASHEVERY='1', STATEOUT=str(work / 'state.bin'))
    if a.mode == 'boot' and not a.hle:
        env['BIOSDIR'] = str(DEV / 'bios')
    state = '-' if a.mode == 'boot' else str(DEV / 'heavy-scenes/selected' / key / 'scene.st0') if a.heavy else str(DEV / 'sim/states' / (key + '.st0'))
    cmd += [str(a.sim / a.runner), str(cue), str(n), state, str(a.out / 'options.ini')]
    (work / 'command.json').write_text(json.dumps(dict(command=cmd, env={k:v for k,v in env.items() if k in ('SMALL', 'DYNR3','RESPEC_LOAD','RESPEC_SAVE','BIOSDIR','CHECKPOINTS','HASHEVERY','CACHESIM_OUT','STATEOUT')}), indent=2))
    with (work / 'run.log').open('w') as log:
        r = subprocess.run(cmd, cwd=work, env=env, stdout=log, stderr=log, timeout=1800)
    log = (work / 'run.log').read_text(errors='replace')
    if r.returncode:
        # recorded, not fatal: a game that fails identically in both builds
        # is still a valid comparison (see compare.py)
        print(f'{key} {n}: exit {r.returncode}, see {work}', flush=True)
        result = dict(game=key, frames=n, exit=r.returncode,
                      hashes=[l for l in log.splitlines() if l.startswith(('checkpoint ', 'irq trace ', 'frame ', 'draw hash ', 'state hash '))])
        (work / 'result.json').write_text(json.dumps(result, indent=2))
        return result
    result = dict(game=key, frames=n, exit=0,
                  hashes=[l for l in log.splitlines() if l.startswith(('checkpoint ', 'irq trace ', 'frame ', 'draw hash ', 'state hash '))],
                  generated_bytes=int(re.search(r'generated bytes (\d+)',log)[1]))
    (work / 'result.json').write_text(json.dumps(result, indent=2))
    print(key, n, result['generated_bytes'], flush=True)
    return result

with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
    results = list(ex.map(run, tasks))
(a.out / 'results.json').write_text(json.dumps(results, indent=2))
if a.mode == 'profile':
    for k in games:
        for script in ('cprof.py', 'hz.py'):
            with (a.out / (k + '-' + script + '.txt')).open('w') as out:
                subprocess.run(['python3', str(a.sim / script), str(a.sim / a.runner),
                                str(a.out / f'{k}-300/cache.txt'), str(a.out / f'{k}-600/cache.txt'), '300', '80'],
                               stdout=out, check=True)
