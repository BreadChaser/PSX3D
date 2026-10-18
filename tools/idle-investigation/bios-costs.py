#!/usr/bin/env python3
"""Exclusive native psxbios.c cost from cprof-format cache counts and ELF debug info.

Usage: bios-costs.py ELF CACHE_300 CACHE_600
Requires ARM binutils on PATH; model matches cprof.py (interlocks separate).
"""
import bisect
import json
import subprocess
import sys
from pathlib import Path

elf, first, last = sys.argv[1:]
symbols = []
for line in subprocess.check_output(
        ['arm-linux-gnueabihf-nm', '-n', '-S', '--defined-only', elf], text=True).splitlines():
    p = line.split()
    if len(p) == 4 and p[2].lower() in ('t', 'w'):
        symbols.append((int(p[0], 16), int(p[1], 16)))
addresses = [s[0] for s in symbols]
files = subprocess.check_output(
    ['arm-linux-gnueabihf-addr2line', '-e', elf, *[hex(a) for a in addresses]],
    text=True).splitlines()
bios = {i for i, name in enumerate(files) if 'psxbios.c:' in name}


def read(path):
    return {(int(p[0], 16), int(p[1])): [int(x) for x in p[2:]]
            for p in (line.split() for line in Path(path).read_text().splitlines())}


a, b = read(first), read(last)
instructions = cycles = 0
for (pc, length), v in b.items():
    i = bisect.bisect_right(addresses, pc) - 1
    if i not in bios or pc >= symbols[i][0] + symbols[i][1]:
        continue
    d = [v[j] - a.get((pc, length), [0] * 5)[j] for j in range(5)]
    if d[0] <= 0:
        continue
    instructions += length * d[0]
    cycles += length * d[0] + 70 * d[1] + 70 * d[2] + 4 * d[3]
print(json.dumps({'native_psxbios_instructions_per_frame': instructions / 300,
                  'native_psxbios_modeled_cycles_per_frame': cycles / 300}, indent=2))
