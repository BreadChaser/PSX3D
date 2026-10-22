#!/usr/bin/env python3
# Each deliberate break of saveid.c must make the save-name suite fail.
import os, subprocess, sys, tempfile
root = os.path.abspath(os.path.join(os.path.dirname(__file__), '../../..'))
os.chdir(root)
src = open('psx3d/source/saveid.c').read()
muts = {
 'size ignored': ('		return r->size == cur.size || (serial[0] && rec_has(r, serial));', '		return true;'),
 'serial rescue gone': ('		return r->size == cur.size || (serial[0] && rec_has(r, serial));', '		return r->size == cur.size;'),
 'disc 1 serial ignored': ('	if (serial[0] && cur.first && r->first[0] && strcmp(r->first, serial))', '	if (0)'),
 'always adopt': ('			mine = !(cur.twins && serial[0] && cur.first && card_of_others(serial) &&', '			mine = !(0 && cur.twins && serial[0] && cur.first && card_of_others(serial) &&'),
 'never last claimant': ('				aliases(NULL, NULL) < cur.twins);', '				1);'),
 'no record written': ('	if (dirty && rec_write(name, r))', '	if (0 && rec_write(name, r))'),
 'no alias before load': ('		aliases(alias_by_size, cur.name);', '		(void)0;'),
 'browser ignores library': ('		const game_t *g = library_game_of(path);', '		const game_t *g = NULL;'),
 'no playlist lookup': ("			if ((!strcasecmp(e, \".m3u\") || (here && !strcasecmp(e, \".cue\"))) && list_each(g->discs[d], names_want, &f))", '			if (0)'),
 'no disc 1 sibling': ('			if (disc_no(b) > 1) {', '			if (0) {'),
 'cue size not data': ('		if (!strcasecmp(e, ".m3u") || !strcasecmp(e, ".cue")) {', '		if (!strcasecmp(e, ".m3u")) {'),
 'taken alias reused': ('				continue;   // taken by yet another game', '				;'),
 'case-sensitive names': ('		if (strcasecmp(g->cover_key, cur.base) || !g->ndiscs)', '		if (strcmp(g->cover_key, cur.base) || !g->ndiscs)'),
 'fallback unrecorded': ('			*created = !r.ok;\n			rec_claim(final, &r, serial);\n		}\n		log_printf', '		}\n		log_printf'),
 'unknown size is same': ('	if (serial[0] && r->nserials)   // a size unknown: the serials still tell\n		return rec_has(r, serial);', ''),
 'owner settings before load': ('	saveid_provisional = r.ok && r.size && cur.size && r.size != cur.size && !strcasecmp(cur.name, cur.base);', '	saveid_provisional = false;'),
 'unwritten record silent': ('		saveid_unrecorded = true;   // the next game', '		(void)0;   // the next game'),
 'fake serial is real': ('	if (n != 9 || (in && *in) || !strcmp(out, "SLUS99999"))', '	if (n != 9 || (in && *in))'),
}
tmp = tempfile.mkdtemp()
missed = 0
for name, (a, b) in muts.items():
    assert a in src, name
    os.makedirs(tmp + '/src', exist_ok=True)
    open(tmp + '/src/saveid.c', 'w').write(src.replace(a, b))
    r = subprocess.run(['cc', '-std=gnu11', '-w', '-g', '-I' + tmp + '/src', '-Ipsx3d/tests/host',
        '-Icore/deps/libretro-common/include', '-Ipsx3d/source', 'psx3d/tests/saveid/check.c',
        'psx3d/source/safefile.c', '-o', tmp + '/t'], capture_output=True, text=True)
    if r.returncode:
        print(name, 'BUILD FAIL', r.stderr[:300]); missed += 1; continue
    caught = subprocess.run([tmp + '/t'], capture_output=True, text=True).returncode != 0
    print(f"{name:26s} {'caught' if caught else 'MISSED'}")
    missed += not caught
print(f"saveid mutations: {len(muts) - missed}/{len(muts)} caught")
sys.exit(1 if missed else 0)
