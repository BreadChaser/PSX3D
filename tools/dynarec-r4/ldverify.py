#!/usr/bin/env python3
"""Simulator-only self-check for LDTMP (load delay rename): ldverify.py <sim dir>.
The MFC2/CFC2 also stores the old value to a shadow word; the branch that
reads LDTMP traps (UDF, SIGILL) when its register differs from the shadow.
Prints each rename and, at exit, how many renamed branches ran."""
import sys
from pathlib import Path
d = Path(sys.argv[1]) / 'core/libpcsxcore/new_dynarec'
p = d / 'linkage_offsets.h'; s = p.read_text()
old = '#define LO_dynarec_local_size\t(LO_mini_ht + PTRSZ*32*2)'
assert old in s
s = s.replace(old, '#define LO_ldshadow\t\t(LO_mini_ht + PTRSZ*32*2)\n#define LO_ldcount\t\t(LO_ldshadow + 4)\n#define LO_dynarec_local_size\t(LO_ldcount + 4)')
p.write_text(s)
p = d / 'new_dynarec.c'; s = p.read_text()
old = '''    else
      emit_mov(s, t);
  }
'''
assert s.count(old) == 1
s = s.replace(old, old + '''  if (dops[i].rt2 == LDTMP)
    emit_writeword(get_reg_w(i_regs->regmap, LDTMP), (u_char *)&dynarec_local + LO_ldshadow);
''')
old = '''      ds = assemble(&st, i, &regs[i], cinfo[i].ccadj);'''
assert s.count(old) == 1
s = s.replace(old, '''      if (dops[i].rs1 == LDTMP || dops[i].rs2 == LDTMP) {
        int h = get_reg(regs[i].regmap, LDTMP);
        void *j;
        assert(h >= 0);
        emit_readword((u_char *)&dynarec_local + LO_ldshadow, HOST_TEMPREG);
        emit_cmp(HOST_TEMPREG, h);
        j = out;
        emit_jeq(0);
        output_w32(0xe7f000f0); // udf: LDTMP lost between its load and the branch
        set_jump_target(j, out);
        emit_readword((u_char *)&dynarec_local + LO_ldcount, HOST_TEMPREG);
        emit_addimm(HOST_TEMPREG, 1, HOST_TEMPREG);
        emit_writeword(HOST_TEMPREG, (u_char *)&dynarec_local + LO_ldcount);
      }
''' + old)
old = '''  dops[i-1].rs1 = r;
  dops[i-1].rt2 = LDTMP;'''
assert s.count(old) == 1
s = s.replace(old, old + '''
  printf("ld delay rename @%08x\\n", pc);''')
s += '''
#include <stdlib.h>
static void sim_ldcount_dump(void) { printf("ld delay renamed branches executed %u\\n", *(u_int *)((u_char *)dynarec_local + LO_ldcount)); }
__attribute__((constructor)) static void sim_ldcount_init(void) { atexit(sim_ldcount_dump); }
'''
p.write_text(s)
print('ok')
