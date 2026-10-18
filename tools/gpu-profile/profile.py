#!/usr/bin/env python3
"""Build a diagnostic simulator runner and attribute inclusive CPU cost to GP0 types.

Uses the project's private ARM11 cache model; never edits the worktree core.
Run with the ARM Linux toolchain on PATH. See README.md.
"""
import argparse
from pathlib import Path
import re
import shlex
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--sim', type=Path, required=True)
p.add_argument('--devtools', type=Path, default=Path('/home/tony/Development/PS1_3ds/devtools'))
p.add_argument('--source', type=Path, required=True)
p.add_argument('--plugin-include', type=Path, required=True, help='Directory containing the header matching the private QEMU build')
a = p.parse_args()
sim, dev = a.sim.resolve(), a.devtools.resolve()
commands = [0x02, 0x26, 0x34, 0x38, 0x3a, 0x3c, 0x3e]
s = a.source.read_text()
markers = 'static unsigned long commands[256], changes[6], page_changes, clut_changes;\n' + '\n'.join(f'void __attribute__((noipa)) pprof_{cmd:02x}(void) {{ commands[{cmd}]++; asm volatile("nop" ::: "memory"); }}' for cmd in [0]+commands)
s = s.replace('#include <math.h>', '#include <math.h>\n'+markers)
enter = 'switch(cmd) {' + ''.join(f'case 0x{cmd:02x}: pprof_{cmd:02x}(); break;' for cmd in commands) + 'default: pprof_00(); break;}'
s = s.replace('\t\tswitch (cmd) {', '\t\t'+enter+'\n\t\tswitch (cmd) {')
s = s.replace('\n\t}\nbreakloop:', '\n\t\tpprof_00();\n\t}\nbreakloop:\n\tpprof_00();')
s = s.replace('\tst_why[at != cur.atlas', '\tif(vpos != vstart) { changes[0] += blend != cur.blend; changes[1] += pass != cur.pass; changes[2] += mask != cur.mask; changes[3] += ps.area_gen != cur_area_gen; changes[4] += atlas[at].rebind; changes[5] += texmode != cur.texmode; }\n\tst_why[at != cur.atlas')
s = s.replace('\ttcache *c = have_last ? last_tex : NULL;', '\tstatic unsigned pg_prev=~0u, cl_prev=~0u;\n\tpage_changes += pg != pg_prev; clut_changes += cl != cl_prev; pg_prev=pg; cl_prev=cl;\n\ttcache *c = have_last ? last_tex : NULL;')
# A few counters answer questions the per-function cache profile cannot.
s = s.replace('static tcache *last_tex;', 'static unsigned long lookups, hits, invalid;\nstatic tcache *last_tex;')
s = s.replace('\tint depth = (page >> 7) & 3;', '\tlookups++;\n\tint depth = (page >> 7) & 3;')
s = s.replace('\tif (c && !cache_valid(c)) {', '\tif(c) hits++;\n\tif (c && !cache_valid(c)) {\n\t\tinvalid++;')
# Print the existing renderer counters with an adequate buffer after each frame.
s = s.replace('void pica_frame_ended(void) { frame_open = false; }', '''void pica_frame_ended(void) { frame_open = false;
 static unsigned f; char b[2048]; pica_stats(b,sizeof(b));
 fprintf(stderr,"PROFILE %u lookup=%lu hit=%lu invalid=%lu %s\\n",++f,lookups,hits,invalid,b);
 fprintf(stderr,"CHANGES"); for(int i=0;i<6;i++) fprintf(stderr," %lu",changes[i]);
 fprintf(stderr," page=%lu clut=%lu\\n",page_changes,clut_changes);
 fprintf(stderr,"COMMANDS"); for(int i=1;i<256;i++) if(commands[i]) fprintf(stderr," %02x=%lu",i,commands[i]); fprintf(stderr,"\\n");
 memset(changes,0,sizeof(changes)); memset(commands,0,sizeof(commands)); page_changes=clut_changes=0;
 lookups=hits=invalid=0;
}''')
(sim/'core_pica/plugins/gpu_pica/gpu_pica.c').write_text(s)
subprocess.run(['make','CFLAGS_OPT=-O2','DEBUG_SYMS=1','-f','Makefile.libretro','platform=ctrsim','CTR_GPU=pica',f'PICA_CFLAGS=-I{sim / "fake3ds"}','-j16'], cwd=sim/'core_pica', check=True, stdout=subprocess.DEVNULL)
subprocess.run(['arm-linux-gnueabihf-gcc','-O2','-g','-march=armv6k','-mfloat-abi=hard','-mfpu=vfp','-marm','-DPICA','-I',str(sim/'fake3ds'),'-I',str(sim/'core_pica/deps/libretro-common/include'),'-o',str(sim/'runner_profile'),str(sim/'runner.c'),str(sim/'core_pica/pcsx_rearmed_libretro_ctrsim.a'),'-static','-lpthread','-lm'],check=True)
nm=subprocess.check_output(['arm-linux-gnueabihf-nm',str(sim/'runner_profile')],text=True)
(sim/'profile-symbols').write_text('\n'.join(f'{int(name[-2:],16)} {pc}' for pc,name in re.findall(r'^(\w+) T (pprof_\w+)$',nm,re.M)))
plugin=(dev/'sim/plugin/cachesim.c').read_text()
plugin=plugin.replace('static GMutex lock;', '''static GMutex lock;
static unsigned ctx;
static uint64_t pcs[256], counts[256][5];
''')
plugin=plugin.replace('uint32_t nlines, hz;', 'uint32_t kind, nlines, hz;')
plugin=plugin.replace('    t->execs++;', '''    if (t->kind) ctx = t->kind == 256 ? 0 : t->kind;
    t->execs++;
    counts[ctx][0] += t->n;
    counts[ctx][4] += t->hz;''')
plugin=plugin.replace('t->imiss += access_cache(&icache, t->lines[i], 1);', 'counts[ctx][1] += access_cache(&icache, t->lines[i], 1);')
plugin=plugin.replace('t->wmiss += access_cache(&dcache, (uint32_t)vaddr, 0);', 'counts[ctx][3] += access_cache(&dcache, (uint32_t)vaddr, 0);')
plugin=plugin.replace('t->rmiss += access_cache(&dcache, (uint32_t)vaddr, 1);', 'counts[ctx][2] += access_cache(&dcache, (uint32_t)vaddr, 1);')
plugin=plugin.replace('        t->pc = pc;', '        t->pc = pc;\n        for(unsigned i=0;i<256;i++) if(pcs[i] && pcs[i]==pc) t->kind=i?i:256;')
plugin=plugin.replace('    fclose(f);', '''    fclose(f);
    char path[4096]; snprintf(path,sizeof(path),"%s.ctx",out?out:"cachesim.txt");
    f=fopen(path,"w");
    for(unsigned i=0;i<256;i++) if(counts[i][0]) fprintf(f,"%x %llu %llu %llu %llu %llu\\n",i,
      (unsigned long long)counts[i][0],(unsigned long long)counts[i][1],(unsigned long long)counts[i][2],
      (unsigned long long)counts[i][3],(unsigned long long)counts[i][4]);
    fclose(f);''')
plugin=plugin.replace('    tbs = g_hash_table_new', '''    FILE *f=fopen(getenv("PROFILE_SYMBOLS"),"r"); unsigned kind; unsigned long long pc;
    while(fscanf(f,"%u %llx",&kind,&pc)==2) pcs[kind]=pc;
    fclose(f);
    tbs = g_hash_table_new''')
(sim/'profile-plugin.c').write_text(plugin)
header=a.plugin_include.resolve()
subprocess.run(['cc','-O2','-fPIC','-shared','-I',str(header),*shlex.split(subprocess.check_output(['pkg-config','--cflags','glib-2.0'],text=True)),str(sim/'profile-plugin.c'),'-o',str(sim/'profile-plugin.so'),*shlex.split(subprocess.check_output(['pkg-config','--libs','glib-2.0'],text=True))],check=True)
print('Built runner_profile and profile-plugin.so. Set PROFILE_SYMBOLS='+str(sim/'profile-symbols'))
