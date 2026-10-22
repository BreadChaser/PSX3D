#!/usr/bin/env python3
"""Compare psx3d/source/theme.c with the UI mock-up's palettes (devtools/ui-mock/js/theme.js).

  check-mock.py [path/to/ui-mock/js/theme.js]

Builds a host program that dumps every theme of theme.c, evaluates the mock's THEMES with node,
and compares names and every token the console uses. Exit status 1 on any difference."""
import json, os, pathlib, re, subprocess, sys, tempfile
root = pathlib.Path(__file__).resolve().parents[2]
mock = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else '/home/tony/Development/PS1_3ds/devtools/ui-mock/js/theme.js')
js = mock.read_text()
i, j = js.index('const THEMES = {'), js.index('const BASE_THEME')
node = subprocess.run(['node', '-e', js[i:j] + '\nconsole.log(JSON.stringify(THEMES))'], capture_output=True, text=True, check=True)
mock_themes = json.loads(node.stdout)

# theme.c field -> mock token
SCALAR = {'menu_bg': 'menuBg', 'header_title': 'headerTitle', 'header_right': 'headerRight', 'hl_glow': 'hlGlow',
    'hl_top': 'hlTop', 'hl_bottom': 'hlBottom', 'value': 'value', 'section': 'section', 'section_rule': 'sectionRule',
    'muted': 'muted', 'white': 'white', 'hint_lib': 'hintLib', 'hint_menu': 'hintMenu', 'message': 'message',
    'error': 'error', 'scrollbar': 'scrollbar', 'help_bg': 'helpBg', 'help_bar': 'helpBar', 'help_title': 'helpTitle',
    'help_impact': 'helpImpact', 'help_text': 'helpText', 'slow_bg': 'slowBg', 'slow_text': 'slowText',
    'game_title': 'gameTitle', 'game_sub': 'gameSub', 'pause_veil': 'pauseVeil', 'touch_face': 'touchFace',
    'touch_border': 'touchBorder'}
work = pathlib.Path(tempfile.mkdtemp())
dump = ['#include <stdio.h>', '#include "psx3d.h"', '#include "theme.c"', 'psx3d_config g_cfg;',
    'static void c(const char *k, u32 v) { printf("\\"%s\\":[%u,%u,%u,%u],", k, v & 255, (v >> 8) & 255, (v >> 16) & 255, v >> 24); }',
    'int main(void) { printf("["); for (int i = 0; i < theme_count(); i++) { const theme_t *t = theme_get(i);',
    ' printf("{\\"name\\":\\"%s\\",\\"id\\":\\"%s\\",", t->name, t->id);']
for f, tok in SCALAR.items(): dump.append(f' c("{tok}", t->{f});')
dump.append(' for (int k = 0; k < 4; k++) { char n[16]; sprintf(n, "accent%d", k); c(n, t->accent[k]); }')
dump.append(' printf("\\"rowDim\\":[%d,%d,%d,255],\\"rowLit\\":[%d,%d,%d,255]}%s", t->row_dim[0], t->row_dim[1], t->row_dim[2], t->row_lit[0], t->row_lit[1], t->row_lit[2], i + 1 < theme_count() ? "," : "");')
dump.append('} printf("]\\n"); return 0; }')
(work / 'dump.c').write_text('\n'.join(dump))
inc = ['-I', str(root / 'psx3d/tests/host'), '-I', str(root / 'psx3d/source'), '-I', str(root / 'core/deps/libretro-common/include')]
subprocess.run(['cc', '-std=gnu11', '-w', *inc, '-o', str(work / 'dump'), str(work / 'dump.c')], check=True)
raw = subprocess.run([str(work / 'dump')], capture_output=True, text=True, check=True).stdout.replace(',}', '}')
console = json.loads(raw)
problems = []
if [t['name'] for t in console] != list(mock_themes):
    problems.append(f'names/order: console {[t["name"] for t in console]} != mock {list(mock_themes)}')
for t in console:
    m = mock_themes.get(t['name'])
    if not m: continue
    for k, v in t.items():
        if k in ('name', 'id'): continue
        if m.get(k) != v: problems.append(f'{t["name"]}: {k} console {v} != mock {m.get(k)}')
for p in problems: print('FAIL', p)
print('themes match the mock-up: %d palettes, %d tokens each' % (len(console), len(console[0]) - 2) if not problems else '%d differences' % len(problems))
sys.exit(1 if problems else 0)
