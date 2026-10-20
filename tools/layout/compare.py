#!/usr/bin/env python3
"""compare.py BASE.elf NEW.elf [--allow-lib-moves]: did the core (and the libraries) keep
their layout in NEW? See psx3d/core-first.ld and docs/dev/core-first.md.
If BASE predates core-first.ld (a release up to v0.4.1), it only reports how far each
region moved and how many symbols kept their cache set.

Regions come from the __psx3d_* symbols in BASE. Symbols are matched by name (names that
occur once in both ELFs). Rules:
  core code (assembly, C) ..... every symbol at the same address
  libraries' code ............. same addresses (with --allow-lib-moves: reported only)
  core rodata / data / bss .... one shift for the whole region, a whole number of 4 KiB pages
                                (cache sets unchanged)
Then the core's code words are compared. A word may differ only as a call/branch to, or a
pointer to, the same symbol (or the same place in a core region) that moved.
Prints a report and exits 1 on any failure."""
import bisect, collections, struct, subprocess, sys

TOOLS = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-'
PAGE = 0x1000


def load(path):
    syms = collections.defaultdict(list)
    exact = collections.defaultdict(set)  # address -> every name there, linker symbols too
    linker = {}  # linker-defined (absolute) symbols such as __end__
    sized = []  # (addr, name) of functions and objects, for "nearest symbol"
    for line in subprocess.check_output([TOOLS + 'nm', '-n', '-S', path], text=True).splitlines():
        p = line.split()
        if len(p) == 4:
            addr, _, kind, name = p
            sized.append((int(addr, 16), name))
        elif len(p) == 3:
            addr, kind, name = p
        else:
            continue
        if name.startswith('$') or kind in 'aU':
            continue
        exact[int(addr, 16)].add(name)
        if kind == 'A':
            linker[name] = int(addr, 16)
            continue
        syms[name].append(int(addr, 16))
    secs = []
    for line in subprocess.check_output([TOOLS + 'readelf', '-SW', path], text=True).splitlines():
        p = line.replace('[ ', '[').split()
        if len(p) > 7 and p[0].startswith('[') and p[2] == 'PROGBITS' and int(p[3], 16):
            secs.append((int(p[3], 16), int(p[4], 16), int(p[5], 16), p[1]))
    data = open(path, 'rb').read()
    sized.sort()
    return dict(syms=syms, exact=exact, linker=linker, secs=secs, data=data, sized=sized, keys=[a for a, _ in sized])


def word(elf, addr):
    for va, off, size, _ in elf['secs']:
        if va <= addr and addr + 4 <= va + size:
            return struct.unpack_from('<I', elf['data'], off + addr - va)[0]
    return None


def in_rodata(elf, addr):
    return any(va <= addr < va + size and name.startswith('.rodata') for va, _, size, name in elf['secs'])


def nearest(elf, addr):
    i = bisect.bisect_right(elf['keys'], addr) - 1
    return (elf['sized'][i][1], addr - elf['sized'][i][0]) if i >= 0 else None


def one(elf, name):
    v = elf['syms'].get(name)
    return v[0] if v else None


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    allow_lib = '--allow-lib-moves' in sys.argv
    if len(args) != 2:
        sys.exit(__doc__)
    a, b = load(args[0]), load(args[1])
    # BASE from before core-first.ld (e.g. a release): take the regions from NEW and
    # only report how the core moved.
    cross = one(a, '__psx3d_core_text_start') is None
    ref = b if cross else a
    if one(ref, '__psx3d_core_text_start') is None:
        sys.exit('neither ELF was built with psx3d/core-first.ld')
    S = lambda n: one(ref, n)
    regions = [  # name, start, end, rule (in ref's addresses)
        ('core code (assembly)', S('__psx3d_core_text_start'), S('__psx3d_core_asm_end'), 'fixed'),
        ('core code (C)', S('psx3d_core_align') + 0x4f0, S('__psx3d_core_text_end'), 'fixed'),
        ('library code', S('__psx3d_libs_text_start'), S('__psx3d_libs_text_end'), 'lib'),
        ('core rodata', S('__psx3d_core_rodata_start'), S('__psx3d_core_rodata_end'), 'page'),
        ('core data', S('__psx3d_core_data_start'), S('__psx3d_core_data_end'), 'page'),
        ('core bss', S('__psx3d_core_bss_start'), S('__psx3d_core_bss_end'), 'page'),
    ]
    unique = {n: v[0] for n, v in a['syms'].items() if len(v) == 1 and len(b['syms'].get(n, [])) == 1}
    if cross:  # region bounds in BASE's addresses, by the region's most common shift
        moved_regions = []
        for name, lo, hi, rule in regions:
            d = collections.Counter(b['syms'][n][0] - ad for n, ad in unique.items()
                                    if lo <= b['syms'][n][0] < hi).most_common(1)
            d = d[0][0] if d else 0
            moved_regions.append((name, lo - d, hi - d, rule))
        regions = moved_regions
        print('BASE has the old layout: report only (same cache set = shift is whole pages)')
    fail = False
    shift = {}
    for name, lo, hi, rule in regions:
        deltas = collections.Counter(b['syms'][n][0] - ad for n, ad in unique.items() if lo <= ad < hi)
        count = sum(deltas.values())
        if not deltas:
            print(f'FAIL {name}: no symbols matched'); fail = True; continue
        d, _ = deltas.most_common(1)[0]
        shift[name] = (lo, hi, d if len(deltas) == 1 else None, d)
        if cross:
            sets = sum(c for x, c in deltas.items() if x % PAGE == 0)
            print(f'     {name:22} {hi - lo:8d} B {count:5d} symbols  main shift {d:+#x}, '
                  f'{sets} in the same cache set, {len(deltas)} distinct shifts')
            continue
        if rule == 'fixed' or (rule == 'lib' and not allow_lib):
            ok = list(deltas) == [0]
        elif rule == 'lib':
            ok = True
        else:
            ok = len(deltas) == 1 and d % PAGE == 0
        moved = count - deltas.get(0, 0)
        detail = 'all at the same address' if list(deltas) == [0] else \
            f'shift {d:+#x} ({d // PAGE:+d} pages)' if len(deltas) == 1 else \
            f'{moved} of {count} moved, shifts {sorted(deltas)[:6]}'
        print(f"{'ok  ' if ok else ('note' if rule == 'lib' else 'FAIL')} {name:22} {hi - lo:8d} B {count:5d} symbols  {detail}")
        fail |= not ok

    # Core code words: identical, or a reference to something that moved.
    def same_place(ta, tb):
        if a['exact'][ta] & b['exact'][tb]:  # e.g. __end__
            return True
        for _, (rlo, rhi, d, _) in shift.items():
            if d is not None and rlo <= ta < rhi:
                return tb - ta == d
        na, nb = nearest(a, ta), nearest(b, tb)
        return na is not None and na == nb

    kinds = collections.Counter()
    bad = []
    for name in ('core code (assembly)', 'core code (C)'):
        lo, hi, _, d = shift[name]
        for addr in range(lo, hi, 4):
            wa, wb = word(a, addr), word(b, addr + d)
            if wa is None or wb is None:
                continue
            if wa == wb:
                kinds['identical'] += 1
                continue
            if (wa >> 25) & 7 == 5 and (wa ^ wb) >> 24 == 0:  # B/BL, same condition and kind
                off = lambda w, at: at + 8 + ((((w & 0xffffff) ^ 0x800000) - 0x800000) << 2)
                if same_place(off(wa, addr), off(wb, addr + d)):
                    kinds['call/branch to a moved symbol'] += 1
                    continue
            elif same_place(wa, wb):
                kinds['pointer to a moved symbol'] += 1
                continue
            # A constant computed from a linker symbol that moved (&__end__ - 0x100000).
            if any(n in b['linker'] and b['linker'][n] - v == wb - wa and abs(wa - v) <= 0x100000
                   for n, v in a['linker'].items()):
                kinds['offset from a moved linker symbol'] += 1
                continue
            if cross and in_rodata(a, wa) and in_rodata(b, wb):
                kinds['pointer to a string (the core strings were pooled with the frontend\'s)'] += 1
                continue
            kinds['other'] += 1
            bad.append((addr, d))
    total = sum(kinds.values())
    print(f"{'ok  ' if not bad else 'FAIL'} core code words      {total} compared: " +
          ', '.join(f'{v} {k}' for k, v in kinds.most_common()))
    for addr, d in bad[:10]:
        print(f'     {addr:#x} {nearest(a, addr)} {word(a, addr):#010x} -> {word(b, addr + d):#010x}')
    fail |= bool(bad)
    print('FAIL' if fail else 'PASS')
    sys.exit(1 if fail else 0)


if __name__ == '__main__':
    main()
