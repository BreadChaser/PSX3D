#!/usr/bin/env python3
"""Prepare a PRIVATE copy of devtools/sim for this checkout (never the original)."""
from pathlib import Path
import argparse
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('sim', type=Path)
args = parser.parse_args()
repo = Path(__file__).resolve().parents[2]
sim = args.sim.resolve()
if not (sim / 'core_pica/Makefile.libretro').is_file():
    parser.error('copy devtools/sim to a private directory first')
if sim == Path('/home/tony/Development/PS1_3ds/devtools/sim'):
    parser.error('refusing to modify the shared simulator')
platform_file = sim / 'core_pica/Makefile.libretro'
platforms = re.findall(r'else ifeq \(\$\(platform\), ctrsim\)\n(.*?)else ifeq \(\$\(platform\), ctr\)', platform_file.read_text(), re.S)
if len(platforms) != 2:
    parser.error('expected compiler and platform ctrsim stanzas in the supplied simulator')
subprocess.run(['rsync', '-a', '--exclude=.git', '--exclude=*.o', '--exclude=*.a', '--exclude=*.so',
                '--exclude=Makefile.libretro', str(repo / 'core') + '/', str(sim / 'core_pica') + '/'], check=True)
parts = (repo / 'core/Makefile.libretro').read_text().split('else ifeq ($(platform), ctr)')
assert len(parts) == 3
platform_file.write_text(parts[0] + ''.join('else ifeq ($(platform), ctrsim)\n' + stanza +
                          'else ifeq ($(platform), ctr)' + part for stanza, part in zip(platforms, parts[1:])))
# Complete the supplied no-op SDK for the current renderer; no real GPU waits.
p = sim / 'fake3ds/citro3d.h'
if 'GPU_PREVIOUS' not in p.read_text():
    p.write_text(p.read_text() + '\nenum { GPU_PREVIOUS=201, GPU_TEXTURE1=202, GPU_ADD_SIGNED=203 };\n')
p = sim / 'fake3ds/3ds.h'
if 'GX_TextureCopy' not in p.read_text():
    p.write_text(p.read_text() + '''\nstatic inline int GX_TextureCopy(u32 *src, u32 sd, u32 *dst, u32 dd, u32 n, u32 flags)
{ (void)sd; (void)dd; (void)flags; (void)src; (void)dst; (void)n; return 0; }
''')
# Use the runner patch exactly once, against the supplied stock runner.
if 'sim_frame_marker' not in (sim / 'runner.c').read_text():
    subprocess.run(['patch', '-d', str(sim), '-p1', '-i', str(repo / 'tools/frameskip/runner.patch')], check=True)
for name in ('irun.sh', 'cbench.sh'):
    p = sim / name
    p.write_text(p.read_text().replace('../dl/qemu-full', '/home/tony/Development/PS1_3ds/devtools/dl/qemu-full'))
p = sim / 'build_pica.sh'
p.write_text(p.read_text().replace('/home/tony/Development/PS1_3ds/core', str(repo / 'core')))
# The build script copies GPU files only, so rerun this after other core edits.
for suffix, policy, interval in [('auto', 'auto', 2), ('ff', 'fixed_interval', 3), ('fs4', 'fixed_interval', 4)]:
    text = (sim / 'o3hw_fs1.ini').read_text().replace('frameskip_type=fixed_interval', f'frameskip_type={policy}').replace('frameskip_interval=1', f'frameskip_interval={interval}')
    (sim / f'o3hw_{suffix}.ini').write_text(text)
print('Prepared', sim, 'from', repo)
