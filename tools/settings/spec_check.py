#!/usr/bin/env python3
"""Spec-matches-menu check (docs/settings.md section 7).

  spec_check.py <menu dump from tests/settings/spec.c> <docs/settings.md> <defaults.ini>

Compares the real menu, walked in eight contexts ({old,new} x {library, game
without preset, game with tested preset, game whose widescreen is off}), with
the settings-menu, settings-home and section 2.3 header blocks of the spec;
checks that every core option is classified in the section 3.1 table, that no
forced key appears in the menu, and that defaults.ini lists exactly the spec's
settings and forced values. Exit status 1 with the differences on a mismatch.
"""
import re
import sys

dump_path, doc_path, defaults_path = sys.argv[1:4]
doc = open(doc_path).read()
problems = []


def block(name):
    m = re.search(r'```%s\n(.*?)```' % name, doc, re.S)
    if not m:
        sys.exit(f'spec: no {name} block in {doc_path}')
    return [[c.strip() for c in line.split('|')] for line in m.group(1).strip().split('\n')]


MENU = block('settings-menu')
HOME = {ctx: [x.strip() for x in labels.split(',')] for ctx, labels in block('settings-home')}
FORCED = {row[0]: {'old': row[1], 'new': row[2]} for row in block('settings-forced')}
HEADER = dict(re.findall(r'^\| (In a game \*\*with\*\*|In a game \*\*without\*\*|Library)[^|]*\| [^"]*"\*?\*?([^"]+?)\*?\*?"', doc, re.M))
HEADER = {'tested': HEADER.get('In a game **with**'), 'game': HEADER.get('In a game **without**'),
          'lib': HEADER.get('Library')}
if None in HEADER.values():
    problems.append('spec: section 2.3 header table not found')
# The fixture preset (tests/settings/fixtures/presets/tested.ini) sets these values.
FIXTURE = {'tested': {'nostalls': 'On'}}

dump = [line.rstrip('\n').split('\t') for line in open(dump_path) if line.strip()]
options = [d[1] for d in dump if d[0] == 'OPTION']
contexts = sorted({d[0] for d in dump if d[0] != 'OPTION'})
expected_contexts = [f'{c}-{g}' for c in ('new', 'old') for g in ('game', 'lib', 'tested', 'wsoff')]
if contexts != expected_contexts:
    problems.append(f'contexts {contexts} != {expected_contexts}')


def expand(values):
    out = []
    for v in values.split(', '):
        m = re.fullmatch(r'(\d+) … (\d+)', v)
        out += [str(n) for n in range(int(m.group(1)), int(m.group(2)) + 1)] if m else [v]
    return out


def same_cycle(a, b):
    return len(a) == len(b) and any(a[i:] + a[:i] == b for i in range(len(a)))


for ctx in contexts:
    console, kind = ctx.split('-')
    scope = 'lib' if kind == 'lib' else 'game'
    rows = [d for d in dump if d[0] == ctx and d[1] == 'ROW']
    home = next((d[2] for d in dump if d[0] == ctx and d[1] == 'HOME'), '')
    want_home = ', '.join(HOME['library' if scope == 'lib' else 'game'])
    if home != want_home:
        problems.append(f'{ctx}: home "{home}" != spec "{want_home}"')
    header = next((d[2] for d in dump if d[0] == ctx and d[1] == 'HEADER'), '')
    want_header = HEADER['lib' if kind == 'lib' else 'tested' if kind == 'tested' else 'game']
    if header != want_header:
        problems.append(f'{ctx}: Advanced header "{header}" != spec "{want_header}"')
    want = []
    for page, label, key, values, old, new, consoles, rscope, layer in MENU:
        if consoles not in ('both', console):
            continue
        if rscope == 'lib' and scope != 'lib' or rscope == 'game' and scope != 'game':
            continue
        if rscope == 'tested' and kind != 'tested':
            continue
        vals = expand(values) if values != '-' else ['-']
        if key == 'screen' and kind == 'game':
            vals = [v for v in vals if v != 'Widescreen']   # no widescreen fix in this fixture
        default = old if console == 'old' else new
        default = FIXTURE.get(kind, {}).get(key, default)
        marker = 'P' if key in FIXTURE.get(kind, {}) else '-'
        want.append((page, label, key, vals, default, marker))
    got = [(d[2], d[3], d[4], d[5].split(', ') if d[5] != '-' else ['-'], d[6], d[7]) for d in rows]
    gl = [(g[0], g[1], g[2]) for g in got]
    wl = [(w[0], w[1], w[2]) for w in want]
    if gl != wl:
        import difflib
        diff = '\n'.join('      ' + l for l in difflib.unified_diff([' | '.join(x) for x in wl],
                         [' | '.join(x) for x in gl], 'spec', 'menu', lineterm='', n=0))
        problems.append(f'{ctx}: rows differ (page | label | key):\n{diff}')
        continue
    for (page, label, key, vals, default, marker), (_, _, _, gvals, gcur, gmark) in zip(want, got):
        if vals != ['-'] and not same_cycle(vals, gvals):
            problems.append(f'{ctx}: {page}/{label} values {gvals} != spec {vals}')
        if default != '-' and gcur != default:
            problems.append(f'{ctx}: {page}/{label} default "{gcur}" != spec "{default}"')
        if gmark != marker:
            problems.append(f'{ctx}: {page}/{label} marker "{gmark}" != spec "{marker}"')

# Every core option the core registers on 3DS has a fate in the section 3.1 table.
table = re.search(r'### 3\.1 Core options.*?\n(\|.*?)\n\n', doc, re.S).group(1)
patterns = []
for line in table.split('\n')[2:]:
    names = [n.strip() for n in line.split('|')[1].split(',')]
    for name in names:   # "screen_centering, _x, _y": suffixes of the first name
        patterns.append(names[0] + name if name.startswith('_') else name)
for key in options:
    short = key[13:] if key.startswith('pcsx_rearmed_') else key
    if not any(re.fullmatch(re.escape(p).replace(r'\*', '.*'), short) for p in patterns):
        problems.append(f'core option {short} has no fate in docs/settings.md section 3.1')
menu_keys = {d[4] for d in dump if d[0] != 'OPTION' and d[1] == 'ROW'}
for key in FORCED:
    if key in menu_keys:
        problems.append(f'forced {key} appears in the menu')

# defaults.ini lists the spec's settings and exactly the spec's forced values.
sections, section = {}, None
for raw in open(defaults_path):
    line = re.split('[;#]', raw, maxsplit=1)[0].strip()
    if line.startswith('['):
        section = line[1:-1]; sections[section] = {}
    elif line and section:
        k, v = (x.strip() for x in line.split('=', 1)); sections[section][k] = v
spec_keys = {row[2] for row in MENU} - {'-', '>'}
for console in ('old', 'new'):
    if set(sections.get(console, {})) != spec_keys:
        problems.append(f'defaults.ini [{console}] keys differ from the spec: '
                        f'missing {sorted(spec_keys - set(sections.get(console, {})))}, '
                        f'extra {sorted(set(sections.get(console, {})) - spec_keys)}')
    forced = sections.get('forced.' + console, {})
    want = {k: v[console] for k, v in FORCED.items()}
    if forced != want:
        problems.append(f'defaults.ini [forced.{console}] != spec settings-forced: '
                        f'{sorted(set(forced.items()) ^ set(want.items()))}')

for p in problems:
    print('FAIL', p)
print(f'settings spec: {len(contexts)} contexts, {sum(1 for d in dump if d[0] != "OPTION" and d[1] == "ROW")} rows, '
      f'{len(options)} core options: {"FAIL" if problems else "PASS"}')
sys.exit(1 if problems else 0)
