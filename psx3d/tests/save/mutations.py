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
 'rotate damaged': ('		if (cur == GOOD || (cur == WEAK && read_good(bak, fmt, NULL) != GOOD)) {', '		if (1) {'),
 'weak pushes good backup': ('		if (cur == GOOD || (cur == WEAK && read_good(bak, fmt, NULL) != GOOD)) {', '		if (cur > 0) {'),
 'weak preferred': ('		if (r == WEAK && weak == SAFE_NONE)\n			weak = s;', '		if (r == WEAK) { info->from = (safe_source_t)s; info->weak = true; return buf; }'),
 'no deep check': ('	if (ok && fmt->deep && !(whole && fmt->deep(whole, fmt->size)))', '	if (0)'),
}
card_muts={
 'shadow failed repair': ('	srm_dirty = r == CARD_UNSAVED;', '	srm_dirty = false;'),
 'no retry delay': ('	if (srm_dirty && osGetTime() < srm_retry_at)', '	if (0)'),
 'tolerant card': ('	if (!card) {\n		if (info.failed)', '	if (!card) {\n		FILE *f = fopen(path, "rb"); if (f) { fread(data, 1, size, f); fclose(f); }\n		if (info.failed)'),
 'card 2 in place': ('	str[0] = 0;               // the core', '	(void)0;               // the core'),
 'directory unchecked': ('	for (int f = 0; f < 16; f++) {\n		u8 x = 0;', '	for (int f = 0; f < 1; f++) {\n		u8 x = 0;'),
 'damaged plays on': ('	srm_locked = srm_trouble != CARD_TROUBLE_NONE;\n	srm_dirty = r == CARD_UNSAVED;   // never', '	srm_locked = srm_trouble == CARD_TROUBLE_MEMORY;\n	srm_dirty = r == CARD_UNSAVED;   // never'),
 'no pin before format': ('	if (history_add(h->group, HIST_CARD_PIN, h->ext, old, size))', '	if (0)'),
 'no session copy': ('	history_add(h->group, HIST_CARD_SESSION, h->ext, data, size);', '	(void)0;'),
 'later day copy kept': ('			twin_day |= e->kind == HIST_CARD_DAY && j > i && day_of(all[j].when) == day_of(e->when);', '			twin_day |= e->kind == HIST_CARD_DAY && j < i && day_of(all[j].when) == day_of(e->when);'),
 'ring unbounded': ('			if (++ring > HIST_RING)', '			if (0)'),
 'budget drops the newest': ('				if (!b->e.path[0] || (keep && !strcmp(b->e.path, keep)))', '				if (!b->e.path[0])'),
 'file time in name ignored': ('		e->when = (time_t)when;', '		e->when = 0;'),
 'put back keeps nothing': ('	history_add(t.h->group, HIST_CARD_PIN, t.h->ext, t.mem, t.size);   // the card as it is now', '	(void)0;'),
}
tmp = tempfile.mkdtemp()
missed = 0
hist=open('psx3d/source/history.c').read()
def run(name, safefile, cards_src, hist_src=None):
    global missed
    open(tmp+'/safefile.c','w').write(safefile)
    open(tmp+'/cards.c','w').write(cards_src)
    open(tmp+'/history.c','w').write(hist_src or hist)
    shutil.copy('psx3d/source/safefile.h', tmp+'/safefile.h')
    flags=['cc','-std=gnu11','-w','-g','-DSAFEFILE_TEST','-DHIST_BUDGET=12000','-include','psx3d/tests/save/host.h','-I'+tmp,'-Ipsx3d/tests/host','-Ipsx3d/source','-Icore/deps/libretro-common/include']
    caught = False
    for test, extra in (('check', []), ('cards', [tmp+'/cards.c', tmp+'/history.c']),
                        ('history', [tmp+'/history.c'])):
        r=subprocess.run(flags+['psx3d/tests/save/'+test+'.c',tmp+'/safefile.c',*extra,'-o',tmp+'/t'],capture_output=True,text=True)
        if r.returncode: print(name,'BUILD FAIL',r.stderr[:300]); missed += 1; return
        caught |= subprocess.run([tmp+'/t'],capture_output=True,text=True).returncode != 0
    print(f"{name:24s} {'caught' if caught else 'MISSED'}")
    missed += not caught
for name,(a,b) in card_muts.items():
    if a in hist:   # the backups (history.c)
        run(name, src, cards, hist.replace(a,b))
        continue
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
 'tail unchecked': ('	return check_tail(p, size, SPU_SIZE_AT + sizeof(spu) + spu);', '	return STATE_OK;'),
 'pads unchecked': ('		if (saved < PAD_MIN || saved > size - pos)\n			return STATE_DAMAGED;', ''),
 'no crc check': ('	if (t && !crc_ok)\n		return STATE_DAMAGED;', ''),
 'other build layout': ('	if (t && t->tail && t->tail != tail_expected())\n		return STATE_VERSION;', ''),
 'respSize unchecked': ('			if (resp > sizeof(((PadDataS *)0)->rxData) || resp_old > sizeof(((PadDataS *)0)->rxData))\n				return STATE_DAMAGED;', ''),
 'learn from a loaded state': ('	} else\n		// the block list elsewhere: the records before it have other sizes\n		for (size_t i = at; i + sizeof(magic) <= size && i < at + TAIL_SCAN; i++)\n			if (!memcmp(p + i, magic, sizeof(magic)))\n				return STATE_VERSION;', '	} else\n		for (size_t i = at; i + sizeof(magic) <= size && i < at + TAIL_SCAN; i++)\n			if (!memcmp(p + i, magic, sizeof(magic))) {\n				state_tail_measured = i - at;\n				return check_tail(p, size, at);\n			}'),
 'disc index kept patched': ('	if (fixed == 0) {\n		char *p = (char *)data', '	if (0) {\n		char *p = (char *)data'),
 'other disc loads': ('	if (fixed == -2) {\n		state_last_result = STATE_OTHER_DISC;\n		return false;\n	}', ''),
 'hle not restored': ('		Config.HLE = hle;   // the game is restarted', '		(void)hle;   // the game is restarted'),
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
