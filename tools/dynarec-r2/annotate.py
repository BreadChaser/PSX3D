#!/usr/bin/env python3
"""Add emission metadata to a PRIVATE simulator core; never generated calls."""
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
f = root / 'core/libpcsxcore/new_dynarec/new_dynarec.c'
s = f.read_text()
metadata = r'''
static unsigned char sim_tags[1 << 21], sim_sites[1 << 21];
static int sim_site;
static void sim_site_restore(int *old) { sim_site = *old; }
#define SIM_SITE(t) int sim_olds __attribute__((cleanup(sim_site_restore))) = sim_site; sim_site = (t)
static unsigned sim_pcs[1 << 21], sim_guest;
static int sim_tag;
static void sim_tag_restore(int *old) { sim_tag = *old; }
#define SIM_SCOPE(t) int sim_old __attribute__((cleanup(sim_tag_restore))) = sim_tag; sim_tag = (t)
'''
s = s.replace('#include "assem_arm.c"', metadata + '\n#include "assem_arm.c"')
s = s.replace('  st.start = addr;', '  st.start = addr; sim_guest = addr;')
# stubs belong to the instruction that created them
assert '  for(i = 0; i < stubcount; i++)\n  {\n' in s
s = s.replace('  for(i = 0; i < stubcount; i++)\n  {\n', '  for(i = 0; i < stubcount; i++)\n  {\n    sim_guest = st.start + (u_int)stubs[i].a*4;\n', 1)
s = s.replace('    speculate_register_values(&st, i);', '    sim_guest = st.start + i*4;\n      speculate_register_values(&st, i);')
s += r'''
void sim_codegen_dump(const char *path) {
  char name[1024]; FILE *f; unsigned n = out - ndrc->translation_cache;
  fprintf(stderr, "code arena %08x %u\n", (unsigned)ndrc->translation_cache, n);
  snprintf(name, sizeof(name), "%s.bin", path); f=fopen(name,"wb"); assert(f);
  fwrite(ndrc->translation_cache,1,n,f); fclose(f);
  snprintf(name, sizeof(name), "%s.tags", path); f=fopen(name,"wb"); assert(f);
  fwrite(sim_tags,1,n/4,f); fclose(f);
  snprintf(name, sizeof(name), "%s.sites", path); f=fopen(name,"wb"); assert(f);
  fwrite(sim_sites,1,n/4,f); fclose(f);
  snprintf(name, sizeof(name), "%s.pcs", path); f=fopen(name,"wb"); assert(f);
  fwrite(sim_pcs,4,n/4,f); fclose(f);
}
'''
f.write_text(s)

# Nested scopes give emitted words one exclusive owner. State and cycle
# operations override the surrounding guest/memory context.
groups = {
  1: ['emit_loadreg', 'emit_storereg', 'emit_readword', 'emit_writeword', 'emit_readbyte'],
  2: ['do_cc', 'do_ccstub', 'gte_check_stall', 'gte_update_stall', 'multdiv_prepare', 'multdiv_update'],
  3: ['address_generation', 'emit_fastpath_cmp_jump', 'load_assemble', 'store_assemble',
      'loadlr_assemble', 'storelr_assemble', 'do_store_smc_check', 'do_invstub'],
  4: ['do_readstub', 'do_read_slow', 'do_writestub', 'do_write_slow', 'inline_readstub',
      'inline_writestub', 'do_unalignedwritestub'],
  5: ['rjump_assemble', 'ujump_assemble', 'cjump_assemble', 'sjump_assemble'],
  6: ['save_regs', 'restore_regs', 'save_regs_all', 'restore_regs_all'],
  7: ['c2op_assemble', 'cop2_assemble', 'c2ls_assemble'],
}
SITES = ['wb_register','wb_valid','wb_invalidate','load_regs','loop_preload','load_consts',
  'load_all_consts','wb_dirtys','wb_needed_dirtys','load_all_regs','load_needed_regs','load_regs_entry',
  'store_regs_bt','load_regs_bt','do_ccstub','do_cc','save_regs','restore_regs','cop2_assemble',
  'c2ls_assemble','c2op_assemble','load_assemble','store_assemble','do_readstub','do_writestub',
  'rjump_assemble','cjump_assemble','sjump_assemble','ujump_assemble','address_generation',
  'emit_extjump','do_store_smc_check','do_invstub','inline_readstub','inline_writestub','do_read_slow',
  'do_write_slow','loadlr_assemble','storelr_assemble','multdiv_assemble','alu_assemble','imm16_assemble',
  'shiftimm_assemble','mov_assemble','cop0_assemble','syscall_assemble','intcall_assemble',
  'pagespan_assemble','wb_consts','emit_fastpath_cmp_jump','gte_check_stall','cop2_do_stall_check','do_overflowstub',
  'do_alignmentstub','do_unalignedwritestub','ds_assemble','do_preload_rhash','emit_far_call']
for name in ('new_dynarec.c', 'assem_arm.c'):
  f = root / 'core/libpcsxcore/new_dynarec' / name
  s = f.read_text()
  if name == 'assem_arm.c':
    s = s.replace('  *((u_int *)out)=word;',
      '  unsigned si = (out - ndrc->translation_cache)/4;\n'
      '  assert(si < sizeof(sim_tags)); sim_tags[si]=sim_tag; sim_sites[si]=sim_site; sim_pcs[si]=sim_guest;\n'
      '  *((u_int *)out)=word;')
  for tag, funcs in groups.items():
    for func in funcs:
      pattern = r'(^static [^;{}]*\b' + func + r'\([^;{}]*\)\s*\{)'
      # Function definitions only, not prototypes or calls.
      s, count = re.subn(pattern, lambda m: m[0] + '\n  SIM_SCOPE(%d);' % tag, s, flags=re.M)
  for site, func in enumerate(SITES, 1):
    pattern = r'(^static [^;{}]*\b' + func + r'\([^;{}]*\)\s*\{)'
    s, count = re.subn(pattern, lambda m: m[0] + '\n  SIM_SITE(%d);' % site, s, flags=re.M)
  f.write_text(s)
