#!/usr/bin/env python3
"""Check and render real menu functions offline with the existing host renderer.
Desktop body font approximates the 3DS font; these are not console screenshots.
Run from the repo root: python3 tests/settings/run.py
"""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
out = Path(os.environ.get('PSX3D_SETTINGS_OUT', root / 'docs/settings'))
out.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    library = (root / 'psx3d/source/libui.c').read_text()
    game = (root / 'psx3d/source/emu.c').read_text()
    # Keep production tables, filters, formatters and drawing unchanged.
    extracted = library[library.index('enum { S_DISPLAY'):library.index('static void settings_run(void)')]
    extracted += game[game.index('enum { M_RESUME'):game.index('static bool game_menu_open;')]
    (tmp / 'settings_extract.h').write_text(extracted)
    flags = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'freetype2'], text=True).split()
    subprocess.run(['cc', '-std=gnu11', '-g', '-Wall', '-Wextra', '-Wno-unused-function',
                    '-Wno-format-truncation', '-Wno-sign-compare', '-fsanitize=address,undefined',
                    '-I'+str(tmp), '-I'+str(root/'psx3d/tests/host'), '-I'+str(root/'psx3d/source'),
                    '-I'+str(root/'core/deps/libretro-common/include'),
                    str(root/'tests/settings/check.c'), *flags, '-lm', '-o', str(tmp/'check')], check=True)
    env = dict(os.environ, PSX3D_MOCKUPS=str(out),
               PSX3D_TITLE_FONT=str(root/'psx3d/tests/host/Michroma-Regular.ttf'),
               PSX3D_BODY_FONT=subprocess.check_output(['fc-match', '-f', '%{file}', 'DejaVu Sans'], text=True))
    subprocess.run([str(tmp/'check')], cwd=tmp, env=env, check=True)
    subprocess.run(['cc', '-std=gnu11', '-g', '-Wall', '-Wextra', '-fsanitize=address,undefined',
                    '-I'+str(root/'psx3d/tests/host'), '-I'+str(root/'psx3d/source'),
                    '-I'+str(root/'core/deps/libretro-common/include'),
                    str(root/'tests/settings/rewind.c'), '-o', str(tmp/'rewind')], check=True)
    subprocess.run([str(tmp/'rewind')], cwd=tmp, check=True)
with tempfile.TemporaryDirectory() as fonts:
    conf = Path(fonts)/'fonts.conf'
    conf.write_text('<?xml version="1.0"?><!DOCTYPE fontconfig SYSTEM "fonts.dtd"><fontconfig><include>/etc/fonts/fonts.conf</include><dir>' + str(root/'psx3d/tests/host') + '</dir></fontconfig>')
    os.environ['FONTCONFIG_FILE'] = str(conf)
    for svg in sorted(out.glob('*.svg')):
        subprocess.run(['magick', '-background', 'none', str(svg), '-strip', str(svg.with_suffix('.png'))], check=True)
print('Wrote offline settings mockups to docs/settings/')
