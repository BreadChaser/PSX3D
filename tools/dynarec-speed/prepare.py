#!/usr/bin/env python3
"""Build a private ARMv6 simulator from this checkout, never shared core copies."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('destination', type=Path)
p.add_argument('--hash', action='store_true')
p.add_argument('--source', type=Path, help='read-only source tree; runner still comes from this checkout')
a = p.parse_args()
repo = Path(__file__).resolve().parents[2]
dev = Path('/home/tony/Development/PS1_3ds/devtools')
dst = a.destination.resolve()
assert dst != dev / 'sim'
dst.mkdir(parents=True, exist_ok=True)
subprocess.run(['rsync', '-a', '--delete', '--exclude=*.o', '--exclude=*.d',
                '--exclude=*.a', str((a.source or repo) / 'core') + '/', str(dst / 'core') + '/'], check=True)
for pat in ('*.o', '*.d', '*.a'):
    for f in (dst / 'core').rglob(pat):
        f.unlink()
f = dst / 'core/Makefile.libretro'
s = f.read_text()
old = (dev / 'sim/core_pica/Makefile.libretro').read_text()
stanzas = re.findall(r'else ifeq \(\$\(platform\), ctrsim\)\n(.*?)else ifeq \(\$\(platform\), ctr\)', old, re.S)
parts = s.split('else ifeq ($(platform), ctr)')
assert len(stanzas) == 2 and len(parts) == 3
s = parts[0] + ''.join('else ifeq ($(platform), ctrsim)\n' + stanza +
                       'else ifeq ($(platform), ctr)' + part for stanza, part in zip(stanzas, parts[1:]))
# Match the production cache capacity and renderer layout.
s = s.replace('CFLAGS += -DARM11 -DGPU_UNAI', 'CFLAGS += -DTARGET_SIZE_2=23 -DARM11 -DGPU_UNAI')
f.write_text(s)
f = dst / 'core/libpcsxcore/psxmem.c'
s = f.read_text().replace('ptr = psxMap(0x80000000, 0x00210000, 1, MAP_TAG_RAM);',
                         'ptr = MAP_FAILED; /* Linux simulator uses relocated RAM. */')
s = s.replace('psxMap(0x1f800000, 0x10000, 0, MAP_TAG_OTHER)',
              'psxMap(0x0fde0000, 0x10000, 0, MAP_TAG_OTHER)')
f.write_text(s)
f = dst / 'core/libpcsxcore/new_dynarec/new_dynarec.c'
f.write_text(f.read_text() + '\nunsigned sim_codegen_size(void) { return out - ndrc->translation_cache; }\n')
shutil.copytree(repo / 'tools/idle-investigation/fake3ds', dst / 'fake3ds', dirs_exist_ok=True)
for name in ('cprof.py', 'hz.py', 'hotblk.py', 'lineprof.py', 'o3hw_off.ini'):
    shutil.copy2(dev / 'sim' / name, dst / name)
for name in ('states', 'plugin'):
    f = dst / name
    if not f.exists():
        f.symlink_to(dev / 'sim' / name, target_is_directory=True)
shutil.copy2(repo / 'tools/dynarec-speed/runner.c', dst / 'runner.c')
(dst / 'hash.flags').write_text('-DPICA_SIM_HASH' if a.hash else '')
print(dst)
