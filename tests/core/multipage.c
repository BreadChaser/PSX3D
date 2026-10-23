// Three-page guest block: changing its tail must retire its cached start too.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "libpcsxcore/r3000a.h"
#include "libpcsxcore/new_dynarec/new_dynarec.h"
#include "libpcsxcore/psxdma.h"
#include "libpcsxcore/plugins.h"

#define START 0x8005ffc0u
#define WORDS 1400
#define END 0x80070000u
static uint32_t dma_word;
static unsigned dma_calls;
static void dma_read(unsigned short *p, int size, unsigned int cycle)
{
    (void)cycle;
    assert(size == 2);
    memcpy(p, &dma_word, 4);
    dma_calls++;
}
static void execute(psxRegisters initial, R3000Acpu *cpu, uint32_t pc)
{
    psxRegs = initial;
    psxRegs.pc = pc;
    psxCpu = cpu;
    unsigned n = 0;
    do {
        cpu->ExecuteBlock(&psxRegs, EXEC_CALLER_BOOT);
        assert(++n < 100);
    } while (psxRegs.pc != END);
}
void dynarec_microtest(void)
{
    psxRegisters initial = psxRegs;
    uint32_t *code = (void *)(initial.ptrs.psxM + (START & 0x1fffff));
    uint32_t *writer = (void *)(initial.ptrs.psxM + 0x72000);
    initial.cycle = initial.subCycle = initial.stop = initial.branching = initial.branchSeen = initial.interrupt = 0;
    initial.dloadReg[0] = initial.dloadReg[1] = 0;
    initial.CP0.n.SR = 0;
    initial.next_interupt = 0x10000000;
    for (unsigned i=0; i<20; i++) initial.event_cycles[i] = 0x10000000;
    initial.GPR.r[0] = 0;
    initial.GPR.r[31] = END;
    ndrc_g.hacks = ndrc_g.hacks_pergame = 0;
    unsigned failures = 0, cases = 0;
    SPUreadDMAMem saved_dma = SPU_readDMAMem;
    SPU_readDMAMem = dma_read; // deterministic SPU payload, real production DMA4 dispatch/Clear
    const unsigned offsets[] = {8, 0x80, 0x1080, (WORDS-3)*4};
    const uint32_t mirrors[] = {0, 0x80000000, 0xa0000000, 0x80200000};
    for (unsigned dma=0; dma<2; dma++)
    for (unsigned m=0; m<4; m++)
    for (unsigned o=0; o<4; o++) {
        unsigned target = offsets[o]/4;
        for (unsigned i=0; i<WORDS; i++) code[i] = 0x26100001; // addiu s0,s0,1
        code[target] = 0x24020007; // addiu v0,zero,7
        code[WORDS-2] = 0x03e00008; code[WORDS-1] = 0; // jr ra; nop
        writer[0] = 0xac410000; // sw at,0(v0)
        writer[1] = 0x03e00008; writer[2] = 0;
        psxRegs = initial; psxCpu = &psxRec;
        new_dynarec_clear_full();
        psxRec.Notify(R3000ACPU_NOTIFY_AFTER_LOAD, NULL);
        execute(initial, &psxRec, START);
        assert(psxRegs.GPR.r[2] == 7);
        extern unsigned multipage_length(unsigned);
        assert(multipage_length(START) == WORDS*4);
        uint32_t addr = ((START + offsets[o]) & 0x1fffff) | mirrors[m];
        dma_word = 0x24020009;
        if (dma) {
            psxRegs = initial; psxCpu = &psxRec;
            psxDma4(addr, 0x10001, 0x01000200);
        } else {
            psxRegisters w = initial;
            w.GPR.r[1] = dma_word; w.GPR.r[2] = addr;
            execute(w, &psxRec, 0x80072000);
        }
        assert(code[target] == dma_word);
        // Do not reset/notify the dynarec here: its old cached block is the subject.
        execute(initial, &psxRec, START);
        psxGPRRegs got = psxRegs.GPR;
        execute(initial, &psxInt, START);
        psxInt.Notify(R3000ACPU_NOTIFY_BEFORE_SAVE, NULL);
        if (memcmp(&got, &psxRegs.GPR, sizeof(got))) {
            printf("FAIL %s mirror=%08x target=%08x: expected v0=%u got=%u\n",
                   dma ? "DMA4" : "store", mirrors[m], START+offsets[o], psxRegs.GPR.r[2], got.r[2]);
            failures++;
        }
        cases++;
    }
    SPU_readDMAMem = saved_dma;
    assert(dma_calls == cases/2);
    printf("multipage: %u/%u interpreter comparisons pass (5600-byte, three-page block; emitted SW and DMA4; four mirrors)\n", cases-failures, cases);
    fflush(stdout);
    assert(!failures);
}
