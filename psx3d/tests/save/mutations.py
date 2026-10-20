#!/usr/bin/env python3
# Each deliberate break of safefile.c / cards.c must make the host suites fail.
import os, shutil, subprocess, sys, tempfile
root = os.path.abspath(os.path.join(os.path.dirname(__file__), '../../..'))
os.chdir(root)
src=open('psx3d/source/safefile.c').read()
cards=open('psx3d/source/cards.c').read()
muts={
 'repair tmp by rewrite': ('		return rename(src, path) == 0;\n	}\n	if (from != SAFE_BACKUP)', '		return write_tmp(src, data, fmt->size) && rename(src, path) == 0;\n	}\n	if (from != SAFE_BACKUP)'),
 'stat ignores tmp': ('found = read_good(other, fmt, NULL) > 0 && stat(other, &st) == 0;', 'found = false;'),
 'delete damaged': ('	return rename(path, bad) == 0;\n}', '	return remove(path) == 0;\n}'),
 'no rescue': ('	if (keep_backup && !rescue_tmp(path, tmp, bak, fmt))', '	if (0 && !rescue_tmp(path, tmp, bak, fmt))'),
 'tolerant size': ('bool ok = fmt->head <= HEAD_MAX && file_size(path) == (long)fmt->size;', 'bool ok = fmt->head <= HEAD_MAX && file_size(path) > 0;'),
 'no head check': ('	if (ok && fmt->valid && fmt->head)', '	if (0)'),
 'unchecked close': ('	if (fclose(f))\n		ok = false;\n	return ok && file_size(tmp)', '	fclose(f);\n	return ok && file_size(tmp)'),
 'rotate damaged': ('		if (!fmt || read_good(path, fmt, NULL) > 0) {', '		if (1) {'),
}
card_muts={
 'shadow failed repair': ('	srm_dirty = r == CARD_UNSAVED;', '	srm_dirty = false;'),
 'no retry delay': ('	if (srm_dirty && osGetTime() < srm_retry_at)', '	if (0)'),
 'tolerant card': ('	if (!card) {\n		if (info.failed)', '	if (!card) {\n		FILE *f = fopen(path, "rb"); if (f) { fread(data, 1, size, f); fclose(f); }\n		if (info.failed)'),
 'card 2 in place': ('	str[0] = 0;               // the core', '	(void)0;               // the core'),
}
tmp = tempfile.mkdtemp()
missed = 0
def run(name, safefile, cards_src):
    global missed
    open(tmp+'/safefile.c','w').write(safefile)
    open(tmp+'/cards.c','w').write(cards_src)
    shutil.copy('psx3d/source/safefile.h', tmp+'/safefile.h')
    flags=['cc','-std=gnu11','-w','-g','-DSAFEFILE_TEST','-include','psx3d/tests/save/host.h','-I'+tmp,'-Ipsx3d/tests/host','-Ipsx3d/source','-Icore/deps/libretro-common/include']
    caught = False
    for test, extra in (('check', []), ('cards', [tmp+'/cards.c'])):
        r=subprocess.run(flags+['psx3d/tests/save/'+test+'.c',tmp+'/safefile.c',*extra,'-o',tmp+'/t'],capture_output=True,text=True)
        if r.returncode: print(name,'BUILD FAIL',r.stderr[:300]); missed += 1; return
        caught |= subprocess.run([tmp+'/t'],capture_output=True,text=True).returncode != 0
    print(f"{name:24s} {'caught' if caught else 'MISSED'}")
    missed += not caught
for name,(a,b) in card_muts.items():
    assert a in cards, name
    run(name, src, cards.replace(a,b))
for name,(a,b) in muts.items():
    assert a in src, name
    run(name, src.replace(a,b), cards)
state=open('psx3d/source/statefile.c').read()
state_muts={
 'unbounded read': ('	if (s->refused || len > s->len - s->pos) {\n		s->refused = true;\n		return -1;\n	}\n	memcpy(buf', '	memcpy(buf'),
 'no spu check': ('	if (spu <= SPU_MIN || spu > SPU_MAX || spu > size - SPU_SIZE_AT - sizeof(spu))', '	if (0)'),
 'tail error ignored': ('	if (stream_refused) {   // a record', '	if (0) {   // a record'),
 'negative seek': ('(offs < 0 ? (size_t)-(offs + 1) >= base :', '(offs < 0 ? false :'),
}
os.makedirs(tmp+'/x/y', exist_ok=True)
os.symlink(os.path.join(root, 'core'), tmp+'/core')
for name,(a,b) in state_muts.items():
    assert a in state, name
    open(tmp+'/x/y/statefile.c','w').write(state.replace(a,b))
    r=subprocess.run(['cc','-std=gnu11','-O1','-w','-g','-fsanitize=address,undefined','-Icore/include','-Ipsx3d/source',
        '-DSTATEFILE_C="'+tmp+'/x/y/statefile.c"','psx3d/tests/save/states.c','-o',tmp+'/s'],capture_output=True,text=True)
    if r.returncode: print(name,'BUILD FAIL',r.stderr[:300]); missed += 1; continue
    caught = subprocess.run([tmp+'/s'],capture_output=True,text=True).returncode != 0
    print(f"{name:24s} {'caught' if caught else 'MISSED'}")
    missed += not caught
shutil.rmtree(tmp)
print(f"mutations: {len(muts)+len(card_muts)+len(state_muts)}, missed {missed}")
sys.exit(missed != 0)
