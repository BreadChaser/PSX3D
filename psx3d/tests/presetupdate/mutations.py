#!/usr/bin/env python3
# Each deliberate break of presetupdate.c must make the host test fail.
import os, subprocess, sys, tempfile
root = os.path.abspath(os.path.join(os.path.dirname(__file__), '../../..'))
os.chdir(root)
src = open('psx3d/source/presetupdate.c').read()
# (The size check is not mutated: SHA256 already covers a short or long file.)
muts = {
 'no hash check':       ('|| strcmp(hex, m.sha256))', ')'),
 'no validation':       ('bool ok = game_presets_validate(builtin_dir, staging, staged_m.pack_version, staged_files);', 'bool ok = true;'),
 'any defaults':        ('if (app_defaults != m->defaults_version) return PU_DEFAULTS;', ''),
 'not newer accepted':  ('if (m->pack_version <= installed_pack)', 'if (m->pack_version < installed_pack)'),
 'no recovery':         ('if (!exists(live) && exists(previous) && !rename(previous, live))', 'if (0)'),
 'offline stamps':      ('finish("offline", false);', 'finish("offline", true);'),
 'no daily limit':      ('t - last_check >= DAY', 't - last_check >= 0'),
 'abort stamps':        ('if (r == PU_FETCH_ABORT) { remove(tmp); return; }   // paused', 'if (r == PU_FETCH_ABORT) { finish("x", true); return; }   // paused'),
 'version unchecked':   ('ok = !strcmp(vtext, want);', 'ok = true;'),
 'duplicates allowed':  ('for (int i = 0; i < count; i++) if (!strcmp(names[i], line)) ok = false;', ''),
 'defaults.ini allowed':('|| !strcmp(s, "defaults.ini")) return false;', ') return false;'),
 'trailing bytes':      ('ok = ok && have_version && fgetc(f) == EOF', 'ok = ok && have_version'),
}
tmp = tempfile.mkdtemp()
missed = 0
for name, (a, b) in muts.items():
    assert a in src, name
    open(tmp + '/presetupdate.c', 'w').write(src.replace(a, b, 1))
    cc = ['cc', '-std=gnu11', '-w', '-g', '-DPRESETUPDATE_TEST', '-include', 'stddef.h', '-Ipsx3d/tests/host',
          '-Ipsx3d/tests/dev', '-Ipsx3d/source', '-Icore/deps/libretro-common/include',
          'psx3d/tests/presetupdate/check.c', tmp + '/presetupdate.c', 'psx3d/source/presets.c',
          'psx3d/source/safefile.c', '-lcrypto', '-o', tmp + '/t']
    r = subprocess.run(cc, capture_output=True, text=True)
    if r.returncode:
        print(name, 'BUILD FAIL', r.stderr[:300]); missed += 1; continue
    t = subprocess.run([tmp + '/t', 'psx3d/romfs/presets', tmp + '/server', tmp + '/sd'], capture_output=True,
                       text=True, env=dict(os.environ, REPO=root))
    subprocess.run(['rm', '-rf', tmp + '/server', tmp + '/sd'])
    caught = t.returncode != 0
    print(f"{name:22s} {'caught' if caught else 'MISSED'}")
    missed += not caught
sys.exit(1 if missed else 0)
