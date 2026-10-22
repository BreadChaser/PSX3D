#!/usr/bin/env python3
"""Build the box art name table (psx3d/romfs/covers/{names.txt,serials.idx}).

Inputs (kept outside the repository, see tools/boxart/README.md):
  --dat      libretro-database "Sony - PlayStation.dat" (redump names + serials)
  --listing  HTML index of https://thumbnails.libretro.com/Sony%20-%20PlayStation/Named_Boxarts/

names.txt    every box art file the thumbnail server has (name without .png,
             as stored on the server), sorted bytewise, one per line.
serials.idx  fixed 18 byte lines "SLUS00694 0012345\\n": a disc serial (letters
             and digits only, space padded to 9) and the byte offset of its
             picture's line in names.txt. Sorted by serial, so the console can
             binary search it without loading it.

A serial whose own redump name has no picture points at the best sibling (same
title, other region or revision), picked with the same rules as the console's
fuzzy match (boxart_pick_sibling in psx3d/source/boxart.c).
"""
import argparse
import html
import re
import sys
import urllib.parse

SANITIZE = re.compile(r'[&*/:`<>?\\|"]')
REGIONS = ["USA", "Europe", "Japan", "World", "Asia", "Australia", "Korea", "Germany", "France", "Spain",
           "Italy", "Netherlands", "Sweden", "Brazil"]


def tags(name):
    return re.findall(r'\(([^)]*)\)', name)


def title_key(name):
    """Same as boxart_title_key(): letters and digits of the name outside (...) and [...], lower case,
    articles dropped, words sorted out of the way ("Legend of Dragoon, The" == "The Legend of Dragoon")."""
    out = []
    depth = 0
    for ch in name:
        if ch in '([':
            depth += 1
        elif ch in ')]':
            depth = max(0, depth - 1)
        elif depth == 0:
            out.append(ch)
    words = re.findall(r'[a-z0-9]+', ''.join(out).lower())
    words = [w for w in words if w not in ('the', 'a', 'an')]
    return ''.join(words)


def rank(candidate, want_tags):
    """Smaller is better. want_tags: the tags of the name we are matching ([] for a bare serial)."""
    t = tags(candidate)
    score = 0
    region = next((r for r in REGIONS if any(r in x.split(', ') for x in t)), None)
    want_region = next((r for r in REGIONS if any(r in x.split(', ') for x in want_tags)), None)
    if want_region:
        score += 0 if region == want_region else 10
    score += REGIONS.index(region) if region else len(REGIONS)
    want_disc = next((x for x in want_tags if x.startswith('Disc ')), None)
    disc = next((x for x in t if x.startswith('Disc ')), None)
    if want_disc:
        score += 0 if disc == want_disc else 100
    else:
        score += 0 if disc in (None, 'Disc 1') else 100
    # prefer plain editions over betas, demos and re-releases
    score += 1000 * sum(1 for x in t if re.match(r'(Beta|Demo|Proto|Sample|Promo|Kiosk|Preview)', x))
    score += 1 if any(x.startswith('Rev') for x in t) else 0
    score += 0.01 * len(candidate)
    return score


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dat', required=True)
    ap.add_argument('--listing', required=True)
    ap.add_argument('--out', required=True, help='romfs/covers directory')
    a = ap.parse_args()

    listing = open(a.listing, encoding='utf-8').read()
    names = sorted({urllib.parse.unquote(html.unescape(m))
                    for m in re.findall(r'href="([^"?][^"]*)\.png"', listing)},
                   key=lambda s: s.encode('utf-8'))
    have = set(names)

    by_title = {}
    for n in names:
        by_title.setdefault(title_key(n), []).append(n)

    dat = open(a.dat, encoding='utf-8').read()
    games = re.findall(r'game \(\n\tname "(.*?)"\n(?:\tregion "(.*?)"\n)?(?:\tserial "(.*?)"\n)?', dat)

    serial_name = {}
    direct = sibling = missing = 0
    for name, _region, serial in games:
        for s in re.split(r',\s*', serial):
            s = re.sub(r'[^A-Z0-9]', '', s.upper())
            if not s or len(s) > 9:
                continue
            pic = SANITIZE.sub('_', name)
            if pic in have:
                direct += 1
            else:
                sibs = by_title.get(title_key(name), [])
                if not sibs:
                    missing += 1
                    continue
                pic = min(sibs, key=lambda c: rank(c, tags(name)))
                sibling += 1
            serial_name.setdefault(s, pic)

    offsets = {}
    pos = 0
    with open(a.out + '/names.txt', 'wb') as f:
        for n in names:
            offsets[n] = pos
            b = (n + '\n').encode('utf-8')
            f.write(b)
            pos += len(b)
    with open(a.out + '/serials.idx', 'wb') as f:
        for s in sorted(serial_name):
            f.write(('%-9s %07d\n' % (s, offsets[serial_name[s]])).encode('ascii'))
    print('names %d (%d bytes), serials %d: %d own picture, %d sibling picture, %d without'
          % (len(names), pos, len(serial_name), direct, sibling, missing), file=sys.stderr)


if __name__ == '__main__':
    main()
