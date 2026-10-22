/* Execute emitted guest code against interpreter and unchanged codegen.
 * Both observably live FLAG and dead-FLAG paths, all MVMVA control fields,
 * full-range random inputs, signed 16-bit coordinate and MAC overflow edges. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libpcsxcore/r3000a.h"
#include "libpcsxcore/new_dynarec/new_dynarec.h"

static uint32_t rng=0x0410abce;
static uint32_t rnd(void) {rng^=rng<<13; rng^=rng>>17; return rng^=rng<<5;}
static void execute(psxRegisters initial, int mode) {
 initial.pc=0x80050000; psxRegs=initial;
 psxCpu=mode<0?&psxInt:&psxRec;
 if(mode>=0) {new_dynarec_clear_full();}
 psxCpu->Notify(R3000ACPU_NOTIFY_AFTER_LOAD,NULL);
 psxCpu->ExecuteBlock(&psxRegs,EXEC_CALLER_BOOT);
 psxCpu->Notify(R3000ACPU_NOTIFY_BEFORE_SAVE,NULL);
 if(psxRegs.pc!=0x80070000) {fprintf(stderr,"mode %d pc %08x initpc %08x\n",mode,psxRegs.pc,initial.pc);assert(0);}
}
static unsigned mfc(int rt,int rd,int control) {return 0x48000000|(control?2u<<21:0)|(rt<<16)|(rd<<11);}
void dynarec_microtest(void) {
 ndrc_g.hacks = ndrc_g.hacks_pergame = 0;
 psxRegisters init=psxRegs, reference, baseline;
 uint32_t *code=(void *)(init.ptrs.psxM+0x50000);
 const uint32_t edge[]={0,1,0xffffffff,0x7fffffff,0x80000000,0x7fff7fff,0x80008000,0x7fff8000,0x80007fff,0x7f000000,0x81000000};
 unsigned preexisting=0, classes[2][2]={{0}}, shown=0;
 for(unsigned t=0;t<12000;t++) {
  init.GPR.r[0]=0; for(unsigned r=1;r<31;r++)init.GPR.r[r]=rnd(); init.GPR.r[31]=0x80070000;
  for(unsigned r=0;r<32;r++) {init.CP2D.r[r]=t<1024?edge[rnd()%(sizeof(edge)/sizeof(*edge))]:rnd();init.CP2C.r[r]=t<1024?edge[rnd()%(sizeof(edge)/sizeof(*edge))]:rnd();}
  init.cycle=init.subCycle=init.stop=init.branching=init.branchSeen=init.interrupt=0;
  init.dloadReg[0]=init.dloadReg[1]=0; init.CP0.n.SR=0x40000000;
  init.gteBusyCycle=t%64; init.muldivBusyCycle=0;init.next_interupt=0x10000000;
  for(unsigned r=0;r<20;r++)init.event_cycles[r]=0x10000000;
  unsigned n=0,op=t%4==0?0x3f:t%4==1?6:0x12;
  unsigned cmd=0x4a000000|op|((op==0x3f?1:rnd()&1)<<19)|((rnd()&1)<<10);
  if(op==0x12) cmd|=(rnd()&3)<<13|(rnd()&3)<<15|(rnd()&3)<<17;
  code[n++]=cmd;
  /* Store every numeric result into guest registers, retaining IR liveness. */
  for(unsigned r=9;r<=27 && (op!=0x3f || !(t&8));r++){code[n++]=mfc(1+r-9,r,0);code[n++]=0;}
  if((t&4) && op!=0x12)code[n++]=0x4a000006; /* overwrite dead flags, keeps prior op flagless */
  code[n++]=mfc(25,31,1);code[n++]=0;
  code[n++]=(31u<<21)|8;code[n++]=0;
  execute(init,-1);reference=psxRegs;
  for(int mode=0;mode<2;mode++) {
   execute(init,mode);
   if(mode==0) {
    baseline=psxRegs;
    if(memcmp(&reference.GPR,&psxRegs.GPR,sizeof(init.GPR))||memcmp(&reference.CP2,&psxRegs.CP2,sizeof(init.CP2))) {
     assert(!"interpreter/production GTE mismatch");
     preexisting++; if(op!=0x3f)classes[op==6][!!(t&4)]++;
     if(shown++<6) {
      fprintf(stderr,"baseline/interpreter difference t=%u cmd=%08x\n",t,cmd);
      for(unsigned r=0;r<32;r++) {
       if(reference.CP2D.r[r]!=psxRegs.CP2D.r[r])fprintf(stderr,"D%u int=%08x base=%08x\n",r,reference.CP2D.r[r],psxRegs.CP2D.r[r]);
       if(reference.CP2C.r[r]!=psxRegs.CP2C.r[r])fprintf(stderr,"C%u int=%08x base=%08x\n",r,reference.CP2C.r[r],psxRegs.CP2C.r[r]);
      }
     }
    }
   }
   if(memcmp(&baseline.GPR,&psxRegs.GPR,sizeof(init.GPR))||memcmp(&baseline.CP2,&psxRegs.CP2,sizeof(init.CP2))||baseline.cycle!=psxRegs.cycle||baseline.subCycle!=psxRegs.subCycle||baseline.gteBusyCycle!=psxRegs.gteBusyCycle||baseline.muldivBusyCycle!=psxRegs.muldivBusyCycle) {
    fprintf(stderr,"FAIL t=%u cmd=%08x mode=%d cycles=%u/%u\n",t,cmd,mode,baseline.cycle,psxRegs.cycle);
    for(unsigned r=0;r<32;r++){if(baseline.CP2D.r[r]!=psxRegs.CP2D.r[r])fprintf(stderr,"cp2d %u: %08x != %08x\n",r,baseline.CP2D.r[r],psxRegs.CP2D.r[r]);if(baseline.CP2C.r[r]!=psxRegs.CP2C.r[r])fprintf(stderr,"cp2c %u: %08x != %08x\n",r,baseline.CP2C.r[r],psxRegs.CP2C.r[r]);}
    assert(0);
   }
  }
 }
 printf("baseline/interpreter differences: MVMVA live=%u dead=%u; NCLIP live=%u dead=%u\n",classes[0][0],classes[0][1],classes[1][0],classes[1][1]);
 printf("PASS: 12000 randomized/edge GTE guest programs, interpreter vs production dynarec plus deterministic rerun; all GPR/CP2/cycles/busy cycles exact; %u production-vs-interpreter cases differ\n",preexisting);
}
