// Differential guest programs: compare GPR/RAM at the first taken branch.
// Timing is deliberately excluded: the interpreter and dynarec schedule it
// differently. These programs have no IO, exceptions or unresolved load delay.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "libpcsxcore/r3000a.h"
#include "libpcsxcore/new_dynarec/new_dynarec.h"

static uint32_t random_state = 0x31734bfe;
static uint32_t random32(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    return random_state ^= random_state << 5;
}
void dynarec_microtest(void)
{
    psxRegisters initial = psxRegs;
    psxGPRRegs expected;
    unsigned char data[1024], want[1024];
    uint32_t *code = (uint32_t *)(psxRegs.ptrs.psxM + 0x50000);
    for (unsigned test = 0; test < 2000; test++) {
        unsigned n = 0;
        for (unsigned i = 0; i < sizeof(data); i++) data[i] = random32();
        for (unsigned i = 1; i < 34; i++) initial.GPR.r[i] = random32();
        initial.GPR.r[0] = 0;
        initial.GPR.r[28] = 0x80060000;
        initial.GPR.r[31] = 0x80070000;
        initial.pc = 0x80050000;
        initial.cycle = 0;
        initial.stop = initial.branching = initial.branchSeen = 0;
        initial.interrupt = 0;
        initial.CP0.n.SR = 0;
        initial.subCycle = 0;
        initial.gteBusyCycle = initial.muldivBusyCycle = 0;
        for (unsigned i = 0; i < 20; i++) initial.event_cycles[i] = 0x10000000;
        for (unsigned i = 0; i < 64; i++) {
            unsigned rs = 1 + random32()%27, rt = 1 + random32()%27;
            unsigned rd = 1 + random32()%27, op = random32()%14;
            switch (op) {
            case 0: code[n++] = (9u<<26)|(rs<<21)|(rt<<16)|(random32()&0xffff); break;
            case 1: case 2: case 3:
                code[n++] = ((op+11)<<26)|(rs<<21)|(rt<<16)|(random32()&0xffff); break;
            case 4: case 5: case 6:
                code[n++] = (rt<<16)|(rd<<11)|((random32()&31)<<6)|(op==4?0:op-3); break;
            case 7: case 8: case 9: case 10: case 11:
                code[n++] = (rs<<21)|(rt<<16)|(rd<<11)|(op==7?0x21:op==8?0x23:op==9?0x24:op==10?0x25:0x26); break;
            case 12:
                code[n++] = (0x23u<<26)|(28u<<21)|(rt<<16)|(random32()&0x3fc);
                code[n++] = 0; // resolve the architectural load delay
                break;
            default:
                code[n++] = (0x2bu<<26)|(28u<<21)|(rt<<16)|(random32()&0x3fc); break;
            }
        }
        code[n++] = (31u<<21)|8; // jr ra
        code[n++] = 0;
        initial.next_interupt = 0x10000000;
        initial.dloadReg[0] = initial.dloadReg[1] = 0;
        psxRegs = initial;
        memcpy(psxRegs.ptrs.psxM + 0x60000, data, sizeof(data));
        psxCpu = &psxInt;
        psxInt.Notify(R3000ACPU_NOTIFY_AFTER_LOAD, NULL);
        psxInt.ExecuteBlock(&psxRegs, EXEC_CALLER_BOOT);
        psxInt.Notify(R3000ACPU_NOTIFY_BEFORE_SAVE, NULL);
        expected = psxRegs.GPR;
        memcpy(want, psxRegs.ptrs.psxM + 0x60000, sizeof(want));
        assert(psxRegs.pc == 0x80070000);
        psxRegs = initial;
        memcpy(psxRegs.ptrs.psxM + 0x60000, data, sizeof(data));
        psxCpu = &psxRec;
        new_dynarec_clear_full();
        psxRec.Notify(R3000ACPU_NOTIFY_AFTER_LOAD, NULL);
        psxRec.ExecuteBlock(&psxRegs, EXEC_CALLER_BOOT);
        psxRec.Notify(R3000ACPU_NOTIFY_BEFORE_SAVE, NULL);
        if (memcmp(&expected, &psxRegs.GPR, sizeof(expected)) ||
            memcmp(want, psxRegs.ptrs.psxM + 0x60000, sizeof(want)) || psxRegs.pc != 0x80070000) {
            fprintf(stderr, "microtest mismatch case %u pc %08x\n", test, psxRegs.pc);
            for (unsigned i=0;i<34;i++) if(expected.r[i]!=psxRegs.GPR.r[i])
                fprintf(stderr,"r%u expected %08x got %08x\n",i,expected.r[i],psxRegs.GPR.r[i]);
            assert(0);
        }
    }
    puts("2000 randomized guest ALU/load/store programs: interpreter GPR/RAM/PC identical");
}
