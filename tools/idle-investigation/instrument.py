#!/usr/bin/env python3
"""Apply simulator-only measurement hooks to an already patched core copy."""
import sys
from pathlib import Path
root = Path(sys.argv[1])
p = root/'libpcsxcore/new_dynarec/new_dynarec.c'
s = p.read_text()
s = s.replace('int psx3d_idle_skip = 1;', '''struct sim_idle_counter { unsigned pc, kind, cycles, batches, iterations; };
struct sim_idle_counter sim_idle[512];
unsigned sim_idle_n;
static struct sim_idle_counter *sim_idle_slot(unsigned pc, unsigned kind)
{
  unsigned j;
  for (j = 0; j < sim_idle_n; j++)
    if (sim_idle[j].pc == pc && sim_idle[j].kind == kind) return &sim_idle[j];
  assert(sim_idle_n < 512);
  sim_idle[j].pc = pc; sim_idle[j].kind = kind; sim_idle_n++;
  return &sim_idle[j];
}
static void sim_idle_record(struct sim_idle_counter *p, unsigned cycles, unsigned n)
{
  p->cycles += cycles; p->batches++; p->iterations += n;
}
static void sim_idle_add(struct sim_idle_counter *p, int cycles, int n, int tmp)
{
  (void)tmp;
  output_w32(0xe92d500f); // push r0-r3,r12,lr (8-byte stack alignment)
  emit_movimm((unsigned)p, 0); emit_movimm(cycles, 1); emit_movimm(n, 2);
  emit_far_call(sim_idle_record);
  output_w32(0xe8bd500f);
}
int psx3d_idle_skip = 1;''',1)
s = s.replace('emit_addimm(cc, step, cc);', 'emit_addimm(cc, step, cc);\n    sim_idle_add(sim_idle_slot(cinfo[i].ba, 0), step, n, tmp);',1)
s = s.replace('  x1 += k * d;', '''  { struct sim_idle_counter *p = sim_idle_slot(0, 1);
    p->cycles += k*d; p->batches++; p->iterations += k; }
  x1 += k * d;''',1)
s = s.replace('  emit_addimm(cc, cycles, cc);', '''  emit_addimm(cc, cycles, cc);
  sim_idle_add(sim_idle_slot(cinfo[i].ba, 2), cycles, 16, tmp);''',1)
s = s.replace('    emit_addimm(r, -n, r);\n    emit_addimm(cc, step, cc);', '    emit_addimm(r, -n, r);\n    emit_addimm(cc, step, cc);\n    sim_idle_add(sim_idle_slot(cinfo[i].ba, 3), step, n, -1);',1)
p.write_text(s)
if '--ndrc-only' in sys.argv: sys.exit(0)
p=root/'libpcsxcore/psxevents.c';s=p.read_text();s=s.replace('\tregs->next_interupt = c + min;', '''#ifdef PSX3D_SIM
 { extern int idleprof_period; if (idleprof_period && min > idleprof_period) min = idleprof_period; }
#endif
\tregs->next_interupt = c + min;''');p.write_text(s)
p=root/'libpcsxcore/psxbios.c';s=p.read_text();s=s.replace('static void hleA0() {', '''unsigned sim_bios_calls[3][256];
unsigned long long sim_bios_cycles[3][256];
unsigned sim_wait_calls, sim_test_calls, sim_wait_jump;
static void hleA0() {''',1)
for name in ('WaitEvent','TestEvent'):
 s=s.replace(f'static void psxBios_{name}() {{',f'extern unsigned sim_wait_calls, sim_test_calls, sim_wait_jump;\nstatic void psxBios_{name}() {{\n sim_{"wait" if name == "WaitEvent" else "test"}_calls++;',1)
s=s.replace('psxRegs.cycle = psxRegs.next_interupt;', '{ sim_wait_jump += psxRegs.next_interupt - psxRegs.cycle; psxRegs.cycle = psxRegs.next_interupt; }',1)
for family in range(3):
 name='ABC'[family]
 for suffix in ('','t'):
  a=s.index(f'static void hle{name}0{suffix}() {{'); b=s.index('\n}',a)
  part=s[a:b]
  part=part.replace('\n\tu32 call =','\n unsigned sim_enter = psxRegs.cycle;\n\tu32 call =',1)
  # Include actual HLE function cost, excluding final branch test IRQ handling.
  part=part.replace('\tpsxBranchTest(&psxRegs);',f'\tsim_bios_calls[{family}][call]++;\n sim_bios_cycles[{family}][call] += psxRegs.cycle - sim_enter;\n\tpsxBranchTest(&psxRegs);')
  s=s[:a]+part+s[b:]
p.write_text(s)
