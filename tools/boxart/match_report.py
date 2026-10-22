#!/usr/bin/env python3
"""Box art match rate on real disc folders, before and after (F8).

Runs the real matching code (psx3d/source/boxart.c, through the host test binary's
--candidates and --serial modes) on every .cue file under the given folders and
compares it with the old rule (the picture must have exactly the file's name).

  match_report.py --check <host test binary> --covers psx3d/romfs/covers \
                  --games <folder> [--games <folder> ...] [--md report.md]

A game counts as "found" when a candidate is a picture the server has. "Same game and\nregion" compares the first found picture with the one found for the redump-named file\n(a Rev 1 file may get the plain edition's box: same game). The
variants rename every game the way players do: tags removed, scene style,
generic names, just the serial.
"""
import argparse
import os
import re
import subprocess
import sys


def run(check, covers, *args, stdin=None):
    return subprocess.run([check, covers, *args], input=stdin, capture_output=True, text=True, check=True).stdout


def sanitize(n):
    return re.sub(r'[&*/:`<>?\\|"]', '_', n)


def title_key(name):
    out, depth = [], 0
    for ch in name:
        if ch in '([':
            depth += 1
        elif ch in ')]':
            depth = max(0, depth - 1)
        elif depth == 0:
            out.append(ch)
    return ''.join(w for w in re.findall(r'[a-z0-9]+', ''.join(out).lower()) if w not in ('the', 'a', 'an'))


def region(name):
    m = re.findall(r'\(([^)]*)\)', name)
    for r in ('USA', 'Europe', 'Japan', 'World', 'Asia', 'Australia', 'Korea', 'Germany', 'France', 'Spain'):
        if any(r in x.split(', ') for x in m):
            return r
    return None


def same_game(a, b):
    return a is not None and b is not None and title_key(a) == title_key(b) and region(a) == region(b)


def games_in(folders):
    seen = {}
    for root_dir in folders:
        for dp, _dn, fn in os.walk(root_dir):
            for f in fn:
                if f.lower().endswith('.cue'):
                    key = f[:-4]
                    disc = re.search(r'\(Disc (\d+)\)', key)
                    if disc and disc.group(1) != '1':
                        continue    # discs of a game share disc 1's picture
                    seen.setdefault(os.path.join(dp, f), key)
    return seen


def variants(key):
    bare = re.sub(r'\s*\([^)]*\)', '', key).strip()
    return [
        ('as named (redump names)', key),
        ('tags removed', bare),
        ('scene style (lower case, underscores)', bare.lower().replace(' ', '_')),
        ('generic file name', 'Game'),
    ]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', required=True)
    ap.add_argument('--covers', required=True)
    ap.add_argument('--games', action='append', required=True)
    ap.add_argument('--md')
    a = ap.parse_args()

    names = set(open(os.path.join(a.covers, 'names.txt'), encoding='utf-8').read().split('\n')) - {''}
    games = games_in(a.games)
    serials = {}
    for path in games:
        s = run(a.check, a.covers, '--serial', path).strip()
        serials[path] = None if s == '-' else s

    rows = []
    out = []
    n = len(games)
    out.append('Games: %d (disc 1 of each .cue found), serial read from the disc: %d\n'
               % (n, sum(1 for s in serials.values() if s)))
    out.append('| Naming | Old rule (exact name) | New: name only | New: name + disc serial | Same game and region as the as-named run |')
    out.append('|---|---|---|---|---|')
    truth = {}
    for vi, (label, _) in enumerate(variants('x')):
        old = new_name = new_serial = agree = 0
        batch_name = []
        batch_serial = []
        keys = []
        for path, key in games.items():
            vkey = variants(key)[vi][1]
            keys.append((path, vkey))
            batch_name.append(vkey + '\t')
            batch_serial.append(vkey + '\t' + (serials[path] or ''))
        res_name = run(a.check, a.covers, '--candidates', stdin='\n'.join(batch_name) + '\n').splitlines()
        res_serial = run(a.check, a.covers, '--candidates', stdin='\n'.join(batch_serial) + '\n').splitlines()
        for (path, vkey), rn, rs in zip(keys, res_name, res_serial):
            if sanitize(vkey) in names:
                old += 1
            cn = rn.split('\t')[2:]
            cs = rs.split('\t')[2:]
            if any(c in names for c in cn):
                new_name += 1
            if any(c in names for c in cs):
                new_serial += 1
            first = next((c for c in cs if c in names), None)
            if vi == 0:
                truth[path] = first
            if same_game(first, truth.get(path)):
                agree += 1
            if vi == 0 and first is None:
                rows.append(games[path])
        pct = lambda x: '%d (%d%%)' % (x, round(100.0 * x / n))
        out.append('| %s | %s | %s | %s | %s |' % (label, pct(old), pct(new_name), pct(new_serial), pct(agree)))
    out.append('')
    if rows:
        out.append('Games with no picture on the server at all (as named): %d' % len(rows))
        for r in rows:
            out.append('- %s' % r)
    text = '\n'.join(out)
    print(text)
    if a.md:
        open(a.md, 'w').write(text + '\n')


if __name__ == '__main__':
    sys.exit(main())
