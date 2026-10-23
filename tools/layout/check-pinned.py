#!/usr/bin/env python3
"""Per-frame frontend objects pinned in core-first.ld: every function of them must keep its
page offset (I-cache set) between two ELFs.  check-pinned.py BASE.elf NEW.elf BUILD_DIR"""
import collections, re, subprocess, sys
NM = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-nm'
base, new, build = sys.argv[1:4]
ld = open(build + '/../core-first.ld').read()
objs = sorted(set(re.findall(r'\b([a-z_0-9]+)\.o\((?:\.text)', ld)) - {'crtbegin'})
# Local C names can repeat (e.g. statsview.c and cheats.c both have parse).
# Retain all definitions instead of silently comparing the last unrelated one.
def syms(elf):
    out = collections.defaultdict(set)
    for l in subprocess.run([NM, elf], capture_output=True, text=True).stdout.splitlines():
        p = l.split()
        if len(p) == 3 and p[1] in 'tT': out[p[2]].add(int(p[0], 16))
    return out
def defined(obj):
    out = set()
    for l in subprocess.run([NM, f'{build}/{obj}.o'], capture_output=True, text=True).stdout.splitlines():
        p = l.split()
        if len(p) == 3 and p[1] in 'tT': out.add(p[2])
    return out
b, n = syms(base), syms(new)
bad = total = 0
for o in objs:
    names = defined(o)
    # Partial shared-UI objects: only explicitly selected per-frame primitives
    # belong to this block; the rest remain movable menu code.
    if o in ('ui', 'libui'):
        names &= set(re.findall(r'\b' + o + r'\.o\(\.text\.([a-z_0-9.]+)\)', ld))
    moved = [s for s in names if s in b and s in n and not ({v & 0xfff for v in b[s]} & {v & 0xfff for v in n[s]})]
    same = [s for s in names if s in b and s in n and b[s] & n[s]]
    total += len(names)
    print(f'{"ok  " if not moved else "MOVED"} {o:16s} {len(names):4d} functions, {len(same)} at the same address, {len(moved)} other page offset')
    for s in moved[:5]: print(f'      {s} {sorted(b[s])} -> {sorted(n[s])}')
    bad += len(moved)
print('PASS' if not bad else f'FAIL {bad}')
sys.exit(1 if bad else 0)
