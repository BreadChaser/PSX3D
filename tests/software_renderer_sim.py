#!/usr/bin/env python3
"""Compare main's software framebuffers with the dual renderer's software path.

Source devtools/env.sh first. Uses ARM/Linux qemu, never a console. Checks five
600-frame scenes with Old half-height/direct and New full-height/threaded
settings. Main sources are reconstructed from upstream plus its real patches.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile

repo = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--devtools', type=Path, default=Path.home() / 'Development/PS1_3ds/devtools')
p.add_argument('--sim', type=Path, default=Path('/tmp/software-renderer-sim'))
p.add_argument('--reference', default='ea8d8c9')
p.add_argument('--reuse', action='store_true')
a = p.parse_args()
sim, devtools = a.sim.resolve(), a.devtools.resolve()

def run(command, **kwargs):
    return subprocess.run(command, check=True, **kwargs)

if not a.reuse:
    sim.mkdir(parents=True)
    for name in ('states', 'fake3ds'):
        shutil.copytree(devtools / 'sim' / name, sim / name)
    sdk = sim / 'fake3ds/3ds.h'
    text = sdk.read_text()
    if 'GX_TextureCopy' not in text:
        text += '\n#include <string.h>\nstatic inline void GX_TextureCopy(const void *src, unsigned in_dim, void *dst, unsigned out_dim, unsigned size, unsigned flags) { memcpy(dst, src, size); }\n'
    sdk.write_text(text)
    sdk = sim / 'fake3ds/citro3d.h'
    text = sdk.read_text()
    for key, value in [('GPU_PREVIOUS',109), ('GPU_TEXTURE1',110), ('GPU_ADD_SIGNED',111)]:
        if key not in text: text += f'\n#define {key} {value}\n'
    sdk.write_text(text)
    template = (devtools / 'sim/core_pica/Makefile.libretro').read_text()
    runner = (devtools / 'sim/runner.c').read_text()
    runner = runner.replace('static void video(const void *data,',
        'static unsigned long long pixel_hash=1469598103934665603ull;\nstatic void video(const void *data,')
    runner = runner.replace('frames_shown++;', '''frames_shown++;
 if(data)for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w*2;x++)
  pixel_hash=(pixel_hash^((const unsigned char*)data)[y*pitch+x])*1099511628211ull;''')
    runner = runner.replace('retro_unload_game();',
        'fprintf(stderr,"pixel hash %016llx\\n",pixel_hash);\n\tretro_unload_game();')
    for label in ('main', 'release'):
        dest = sim / label
        core = dest / 'core'
        dest.mkdir()
        if label == 'main':
            env = dict(os.environ, GIT_INDEX_FILE=str(sim / 'reference-index'))
            run(['git','-C',str(repo/'core'),'read-tree','HEAD'], env=env)
            for patch in sorted((repo/'patches').glob('*.patch')):
                data = subprocess.check_output(['git','-C',str(repo),'show',a.reference+':patches/'+patch.name])
                run(['git','-C',str(repo/'core'),'apply','--cached','-'],env=env,input=data)
            tree = subprocess.check_output(['git','-C',str(repo/'core'),'write-tree'],env=env,text=True).strip()
            archive = subprocess.check_output(['git','-C',str(repo/'core'),'archive',tree])
            core.mkdir()
            with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
                tar.extractall(core, filter='data')
        else:
            shutil.copytree(repo/'core',core,symlinks=True,
                ignore=shutil.ignore_patterns('.git','*.o','*.a','*.d'))
        # Insert ONLY the simulator toolchain/platform blocks, leaving every
        # reference core source and its software renderer exactly as released.
        mk = core/'Makefile.libretro'
        text = mk.read_text()
        start = 0
        for _ in range(2):
            start = template.index('else ifeq ($(platform), ctrsim)\n',start)
            end = template.index('\nelse if',start+10)
            block = template[start:end]
            pos = text.index('else ifeq ($(platform), ctr)\n',
                text.index('# platform specific options') if _ else 0)
            text = text[:pos]+block+'\n'+text[pos:]
            start = end
        mk.write_text(text)
        code = runner
        if label == 'release':
            code = 'unsigned sim_c3d_draws;\nextern int (*pica_thread_call)(void (*)(void *), void *);\n'+code
            code = code.replace('retro_unload_game();',
                'if (sim_c3d_draws || pica_thread_call) abort();\n\tretro_unload_game();')
        (dest/'runner.c').write_text(code)
        command = ['make','-f','Makefile.libretro','platform=ctrsim',
            'CTR_GPU=unai' if label=='main' else 'CTR_GPU=dual','USE_ASYNC_GPU=1']
        with (dest/'build.log').open('w') as log:
            run(command+['clean'],cwd=core,stdout=log,stderr=log)
            run(command+['-j16','CFLAGS_OPT=-O2',f'PICA_CFLAGS=-I{sim}/fake3ds'],cwd=core,stdout=log,stderr=log)
            run(['arm-linux-gnueabihf-gcc','-O2','-g','-march=armv6k','-mfloat-abi=hard','-mfpu=vfp','-marm',
                '-I',str(core/'deps/libretro-common/include'),'-o',str(dest/'runner'),str(dest/'runner.c'),
                str(core/'pcsx_rearmed_libretro_ctrsim.a'),'-static','-lpthread','-lm'],stdout=log,stderr=log)

options = (devtools/'sim/o3hw_off.ini').read_text()
(sim/'old.ini').write_text(options)
(sim/'new.ini').write_text(options.replace('gpu_thread_rendering=disabled','gpu_thread_rendering=enabled'))
games = {'o3auto':'Crash Bandicoot (USA)','sh':'Silent Hill (USA)',
    'r4':'R4 - Ridge Racer Type 4 (USA)','ctr':'CTR - Crash Team Racing (USA)',
    'gt2':'Gran Turismo 2 (USA) (Arcade Mode) (Rev 1)'}

def check(case):
    label,state,model = case
    env = dict(os.environ,SWFB='1',**({'HALF3D':'1'} if model=='old' else {}))
    cue = next((devtools/'roms/psx'/games[state]).glob('*.cue'))
    path = sim/f'{label}-{state}-{model}.log'
    with path.open('w') as log:
        run([str(devtools/'dl/qemu-full/build/qemu-arm'),str(sim/label/'runner'),str(cue),'600',
            str(sim/'states'/f'{state}.st0'),str(sim/f'{model}.ini')],
            cwd=sim,env=env,stdout=log,stderr=log,timeout=300)
    text = path.read_text()
    hashed = re.search(r'pixel hash (\w+)',text)
    assert hashed,path
    if label=='release':
        assert ('gpu thread: software enabled' in text)==(model=='new'),path
    return label,state,model,hashed[1]

with ThreadPoolExecutor(max_workers=4) as pool:
    results = list(pool.map(check,[(label,state,model) for state in games
        for model in ('old','new') for label in ('main','release')]))
for state in games:
    for model in ('old','new'):
        cases = [(label,h) for label,s,m,h in results if s==state and m==model]
        print(state,model,cases,flush=True)
        assert len({h for _,h in cases})==1,(state,model)
(sim/'results.json').write_text(json.dumps(results,indent=2)+'\n')
