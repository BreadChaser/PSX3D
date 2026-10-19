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
p.add_argument('--annotate', action='store_true')
p.add_argument('--spad', default='0x13400000', help='scratchpad host address for --map 3ds (console before dynr2: 0x0fddf000)')
p.add_argument('--stats', action='store_true', help='Count dynarec events (STAT_PRINT) and cache clears')
p.add_argument('--map', choices=('3ds', 'linux'), default='3ds', help='3ds: RAM 0x13000000, scratchpad 0x13400000 as the v0.4.0+ console build maps them')
p.add_argument('--ref', help='Use an immutable Git core instead of working files')
a = p.parse_args()
repo = Path(__file__).resolve().parents[2]
dev = Path('/home/tony/Development/PS1_3ds/devtools')
dst = a.destination.resolve()
assert dst != dev / 'sim'
dst.mkdir(parents=True, exist_ok=True)
subprocess.run(['rsync', '-a', '--delete', '--exclude=*.o', '--exclude=*.d',
                '--exclude=*.a', str(repo / 'core') + '/', str(dst / 'core') + '/'], check=True)
if a.ref:
    shutil.rmtree(dst/'core')
    import io, tarfile
    archive = subprocess.check_output(['git','-C',str(repo),'archive',a.ref,'core'])
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        tar.extractall(dst, filter='data')
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
s = s.replace('# PSX3D: the 3DS core', 'CFLAGS += -DTARGET_SIZE_2=23\n\t# PSX3D: the 3DS core', 1)
assert 'TARGET_SIZE_2=23\n\t# PSX3D' in s
f.write_text(s)
f = dst / 'core/libpcsxcore/psxmem.c'
s = f.read_text().replace('ptr = psxMap(0x80000000, 0x00210000, 1, MAP_TAG_RAM);',
                         'ptr = MAP_FAILED; /* Linux simulator uses relocated RAM. */')
s = s.replace('psxMap(0x1f800000, 0x10000, 0, MAP_TAG_OTHER)',
              'psxMap(0x76000000, 0x10000, 0, MAP_TAG_OTHER)')
f.write_text(s)
if a.map == '3ds':
    s = f.read_text().replace('psxMap(0x77000000,', 'psxMap(0x13000000,').replace('psxMap(0x76000000,', 'psxMap(%s,' % a.spad)
    f.write_text(s)
# Correctness-only event/exception digest; no generated-code instrumentation.
if a.hash:
    f = dst / 'core/libpcsxcore/psxevents.c'
    s = f.read_text()
    needle = '\t\t\tregs->interrupt &= ~(1u << irq);'
    assert needle in s
    s = s.replace(needle, needle + '\n\t\t\t{ extern void sim_irq_event(unsigned); sim_irq_event(irq); }')
    f.write_text(s)
    f = dst / 'core/libpcsxcore/r3000a.c'
    s = f.read_text()
    needle = 'void psxException(u32 cause, enum R3000Abdt bdt, psxCP0Regs *cp0) {'
    assert needle in s
    f.write_text(s.replace(needle, needle + '\n\t{ extern void sim_irq_event(unsigned); sim_irq_event(0x100 | cause | (bdt << 16)); }'))
f = dst / 'core/libpcsxcore/new_dynarec/new_dynarec.c'
if a.stats:
    t = f.read_text()
    t = t.replace('//#define STAT_PRINT', '#define STAT_PRINT')
    t = t.replace('  __clear_cache(start, end);', '  __clear_cache(start, end); stat_clear_range++; if (len > 2048) stat_clear_full++;')
    f.write_text(t)
f.write_text(f.read_text() + '\nunsigned sim_codegen_size(void) { return out - ndrc->translation_cache; }\n')
shutil.copytree(repo / 'tools/idle-investigation/fake3ds', dst / 'fake3ds', dirs_exist_ok=True)
fp = dst / 'fake3ds/citro3d.h'
if 'GPU_PREVIOUS' not in fp.read_text():
    fp.write_text(fp.read_text() + '\nenum { GPU_PREVIOUS=201, GPU_TEXTURE1=202, GPU_ADD_SIGNED=203 };\n')
fp = dst / 'fake3ds/3ds.h'
if 'GX_TextureCopy' not in fp.read_text():
    fp.write_text(fp.read_text() + '''\nstatic inline int GX_TextureCopy(u32 *src, u32 sd, u32 *dst, u32 dd, u32 n, u32 flags)
{ (void)sd; (void)dd; (void)flags; (void)src; (void)dst; (void)n; return 0; }
''')
for name in ('cprof.py', 'hz.py', 'hotblk.py', 'lineprof.py', 'o3hw_off.ini'):
    shutil.copy2(dev / 'sim' / name, dst / name)
for name in ('states', 'plugin'):
    f = dst / name
    if not f.exists():
        f.symlink_to(dev / 'sim' / name, target_is_directory=True)
shutil.copy2(repo / 'tools/dynarec-r2/runner.c', dst / 'runner.c')
(dst / 'hash.flags').write_text('-DPICA_SIM_HASH' if a.hash else '')
print(dst)

if a.annotate:
    subprocess.run(['python3', str(repo/'tools/dynarec-r2/annotate.py'), str(dst)], check=True)
