// Differential guest programs: compare GPR/RAM at the first taken branch.
// Interpreter timing is excluded: its scheduler differs. Switch off/on
// guest timing is compared exactly. These programs have no IO, exceptions or unresolved load delay.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "libpcsxcore/r3000a.h"
#include "libpcsxcore/new_dynarec/new_dynarec.h"

extern unsigned psx3d_dynr2;
// all experiments off, all on, all on with every group guard failing
static const unsigned variants[] = {0, 7, 0x107};
#define NVAR (sizeof(variants)/sizeof(variants[0]))

static uint32_t random_state = 0x31734bfe;
static uint32_t random32(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    return random_state ^= random_state << 5;
}
static void run_block(psxRegisters *initial, uint32_t pc)
{
    psxRegs = *initial;
    psxRegs.pc = pc;
    psxCpu = &psxRec;
    psxRec.ExecuteBlock(&psxRegs, EXEC_CALLER_BOOT);
    assert(psxRegs.pc == initial->GPR.r[31]);
}

static void stress_guards_smc(psxRegisters initial)
{
    uint32_t *target=(uint32_t *)(initial.ptrs.psxM+0x54000);
    uint32_t *writer=(uint32_t *)(initial.ptrs.psxM+0x55000);
    uint32_t *reader=(uint32_t *)(initial.ptrs.psxM+0x56000);
    initial.GPR.r[31]=0x80070000;
    initial.GPR.r[0]=0;
    initial.cycle=initial.subCycle=initial.stop=initial.branching=initial.branchSeen=initial.interrupt=0;
    initial.dloadReg[0]=initial.dloadReg[1]=0;
    initial.next_interupt=0x10000000;
    initial.CP0.n.SR=0;
    for(unsigned i=0;i<20;i++) initial.event_cycles[i]=0x10000000;
    unsigned off_cycles[2000];
    for (unsigned variant=0;variant<NVAR;variant++) {
        psx3d_dynr2=variants[variant];
        psxRegs=initial;
        psxCpu=&psxRec;
        new_dynarec_clear_full();
        psxRec.Notify(R3000ACPU_NOTIFY_AFTER_LOAD,NULL);
        target[0]=0x24020000; // addiu v0,zero,0
        target[1]=(31u<<21)|8; target[2]=0;
        writer[0]=(0x2bu<<26)|(2u<<21)|(1u<<16); // sw r1,0(r2)
        writer[1]=(31u<<21)|8; writer[2]=0;
        reader[0]=(0x23u<<26)|(28u<<21)|(3u<<16); // lw v1,0(gp)
        reader[1]=0; reader[2]=(31u<<21)|8; reader[3]=0;
        // Reuse compiled readers as the base alternates across scratchpad/RAM
        // and a scratchpad mirror. Only the first execution can speculate it.
        const uint32_t bases[]={0x1f800000,0x80060000,0x9f800000,0x00060000,0xa0060000};
        for(unsigned i=0;i<2000;i++) {
            initial.GPR.r[28]=bases[i%5];
            *(uint32_t *)initial.ptrs.psxH=0xdead1234;
            *(uint32_t *)(initial.ptrs.psxM+0x60000)=0xcafe5678;
            run_block(&initial,0x80056000);
            assert(psxRegs.GPR.r[3]==(i%5==0 || i%5==2 ? 0xdead1234 : 0xcafe5678));
            // Write a compiled target through emitted guest SW; no host Clear
            // and no notification between the write and target execution.
            initial.GPR.r[1]=0x24020000|i;
            initial.GPR.r[2]=0x80054000;
            run_block(&initial,0x80055000);
            run_block(&initial,0x80054000);
            assert(psxRegs.GPR.r[2]==i);
            if(!variant) off_cycles[i]=psxRegs.cycle;
            else assert(off_cycles[i]==psxRegs.cycle);
        }
    }
    const uint32_t edges[]={0x1f7ffffc,0x1f800000,0x1f8003fc,0x1f800400,
        0x1f800ffc,0x1f801000,0x9f800000,0x80060000};
    uint32_t off_values[sizeof(edges)/sizeof(edges[0])];
    unsigned edge_cycles[sizeof(edges)/sizeof(edges[0])];
    for(unsigned variant=0;variant<NVAR;variant++) {
        psx3d_dynr2=variants[variant];
        psxRegs=initial; psxCpu=&psxRec;
        new_dynarec_clear_full();
        psxRec.Notify(R3000ACPU_NOTIFY_AFTER_LOAD,NULL);
        initial.GPR.r[28]=0x1f800000;
        run_block(&initial,0x80056000); // compile as scratchpad before edge tests
        for(unsigned i=0;i<sizeof(edges)/sizeof(edges[0]);i++) {
            initial.GPR.r[28]=edges[i];
            run_block(&initial,0x80056000);
            if(!variant) { off_values[i]=psxRegs.GPR.r[3]; edge_cycles[i]=psxRegs.cycle; }
            else { assert(off_values[i]==psxRegs.GPR.r[3]); assert(edge_cycles[i]==psxRegs.cycle); }
        }
    }
    puts("8 cached range edges: below scratchpad, architectural/end-of-map boundaries, I/O, mirror and RAM: off/on identical");
    puts("2000 repeated emitted-SW SMC invalidations and cached scratchpad/RAM/mirror guard transitions: off/on values and cycles identical");
}
// Runs of loads/stores through one base register (group guards), compiled
// once with one base and then executed with bases whose range crosses the
// scratchpad window, I/O, the end of RAM and mirrors: interpreter equality.
static void stress_groups(psxRegisters initial)
{
    uint32_t *code=(uint32_t *)(initial.ptrs.psxM+0x57000);
    unsigned n=0;
    for (unsigned k=0;k<16;k++) {
        code[n++]=(0x23u<<26)|(28u<<21)|((3+k)<<16)|(k*4); // lw r3+k,4k(gp)
        if (k&1) code[n++]=(0x29u<<26)|(28u<<21)|(1u<<16)|(k*4+2); // sh r1,4k+2(gp)
    }
    code[n++]=0;
    code[n++]=(31u<<21)|8; code[n++]=0;
    initial.GPR.r[31]=0x80070000;
    initial.GPR.r[1]=0x5a5a;
    initial.cycle=initial.subCycle=initial.stop=initial.branching=initial.branchSeen=initial.interrupt=0;
    initial.dloadReg[0]=initial.dloadReg[1]=0;
    initial.next_interupt=0x10000000;
    initial.CP0.n.SR=0;
    for(unsigned i=0;i<20;i++) initial.event_cycles[i]=0x10000000;
    // first base decides the compiled speculation; later ones reuse the block
    const uint32_t first[]={0x1f800100,0x80060000};
    const uint32_t bases[]={0x1f800100,0x1f800fd0,0x1f800fc0,0x1f7fffe0,0x80060000,
        0x801fffd0,0x801fffc0,0x00060000,0x9f800100,0x1f800000,0x80000000,0x801fff00};
    static unsigned char ram_save[0x200000], spad_save[0x10000];
    memcpy(ram_save, initial.ptrs.psxM, sizeof(ram_save));
    memcpy(spad_save, initial.ptrs.psxH, sizeof(spad_save));
    unsigned cases=0, preexisting=0;
    for (unsigned f=0;f<2;f++)
    for (unsigned b=0;b<sizeof(bases)/sizeof(bases[0]);b++) {
        psxGPRRegs want; static unsigned char want_ram[0x200000], want_spad[0x10000];
        unsigned want_cycle=0;
        for (int v=-1; v<(int)NVAR; v++) {
            memcpy(initial.ptrs.psxM, ram_save, sizeof(ram_save));
            memcpy(initial.ptrs.psxH, spad_save, sizeof(spad_save));
            for (unsigned i=0;i<0x100;i++) ((uint32_t *)(initial.ptrs.psxM+0x60000))[i]=0x11110000+i;
            for (unsigned i=0;i<0x100;i++) ((uint32_t *)initial.ptrs.psxH)[i]=0x22220000+i;
            for (unsigned i=0;i<0x400;i++) ((uint32_t *)initial.ptrs.psxM)[0x7fc00/4+i]=0x33330000+i;
            psxRegs=initial;
            if (v<0) {
                psxRegs.GPR.r[28]=bases[b];
                psxRegs.pc=0x80057000;
                psxCpu=&psxInt;
                psxInt.Notify(R3000ACPU_NOTIFY_AFTER_LOAD,NULL);
                psxInt.ExecuteBlock(&psxRegs,EXEC_CALLER_BOOT);
                want=psxRegs.GPR;
                memcpy(want_ram,psxRegs.ptrs.psxM,sizeof(want_ram));
                memcpy(want_spad,psxRegs.ptrs.psxH,sizeof(want_spad));
                continue;
            }
            psx3d_dynr2=variants[v];
            psxCpu=&psxRec;
            new_dynarec_clear_full();
            psxRec.Notify(R3000ACPU_NOTIFY_AFTER_LOAD,NULL);
            psxRegisters warm=psxRegs;
            warm.GPR.r[28]=first[f];
            run_block(&warm,0x80057000); // compile with this speculation
            psxRegisters run=initial;
            memcpy(initial.ptrs.psxM, ram_save, sizeof(ram_save));
            memcpy(initial.ptrs.psxH, spad_save, sizeof(spad_save));
            for (unsigned i=0;i<0x100;i++) ((uint32_t *)(initial.ptrs.psxM+0x60000))[i]=0x11110000+i;
            for (unsigned i=0;i<0x100;i++) ((uint32_t *)initial.ptrs.psxH)[i]=0x22220000+i;
            for (unsigned i=0;i<0x400;i++) ((uint32_t *)initial.ptrs.psxM)[0x7fc00/4+i]=0x33330000+i;
            run.GPR.r[28]=bases[b];
            run_block(&run,0x80057000);
            int differs = memcmp(&want,&psxRegs.GPR,sizeof(want)) || memcmp(want_ram,psxRegs.ptrs.psxM,sizeof(want_ram))
                || memcmp(want_spad,psxRegs.ptrs.psxH,sizeof(want_spad));
            if (v==0 && differs) {
                // the unchanged dynarec already differs from the interpreter
                // here: from now on the reference is the unchanged dynarec
                want=psxRegs.GPR;
                memcpy(want_ram,psxRegs.ptrs.psxM,sizeof(want_ram));
                memcpy(want_spad,psxRegs.ptrs.psxH,sizeof(want_spad));
                printf("note: dynarec off != interpreter for base %08x (pre-existing)\n", bases[b]);
                differs = 0; preexisting++;
            }
            if (differs) {
                fprintf(stderr,"group mismatch first %08x base %08x variant %x\n",first[f],bases[b],variants[v]);
                for (unsigned i=0;i<34;i++) if(want.r[i]!=psxRegs.GPR.r[i])
                    fprintf(stderr,"r%u expected %08x got %08x\n",i,want.r[i],psxRegs.GPR.r[i]);
                assert(0);
            }
            if (v==0) want_cycle=psxRegs.cycle;
            else assert(want_cycle==psxRegs.cycle);
            cases++;
        }
    }
    memcpy(initial.ptrs.psxM, ram_save, sizeof(ram_save));
    memcpy(initial.ptrs.psxH, spad_save, sizeof(spad_save));
    printf("%u group-guard runs across window/I/O/RAM-end/mirror bases after scratchpad and RAM speculation: values identical to the unchanged dynarec (and to the interpreter except %u pre-existing cases), cycles identical across variants\n", cases, preexisting);
}

// A store compiled with scratchpad speculation (no inline SMC check with
// dynr2_spsmc) later overwrites compiled code through its slow stub.
static void stress_spad_smc(psxRegisters initial)
{
    uint32_t *target=(uint32_t *)(initial.ptrs.psxM+0x58000);
    uint32_t *writer=(uint32_t *)(initial.ptrs.psxM+0x59000);
    initial.GPR.r[31]=0x80070000;
    initial.cycle=initial.subCycle=initial.stop=initial.branching=initial.branchSeen=initial.interrupt=0;
    initial.dloadReg[0]=initial.dloadReg[1]=0;
    initial.next_interupt=0x10000000;
    initial.CP0.n.SR=0;
    for(unsigned i=0;i<20;i++) initial.event_cycles[i]=0x10000000;
    for (unsigned variant=0;variant<NVAR;variant++) {
        psx3d_dynr2=variants[variant];
        psxRegs=initial; psxCpu=&psxRec;
        new_dynarec_clear_full();
        psxRec.Notify(R3000ACPU_NOTIFY_AFTER_LOAD,NULL);
        target[0]=0x24020000; target[1]=(31u<<21)|8; target[2]=0;
        // sw r1,0(r2); sw r1,4(r3); jr ra
        writer[0]=(0x2bu<<26)|(2u<<21)|(1u<<16);
        writer[1]=(0x2bu<<26)|(3u<<21)|(1u<<16)|4;
        writer[2]=(31u<<21)|8; writer[3]=0;
        initial.GPR.r[2]=0x1f800010; initial.GPR.r[3]=0x1f800020;
        run_block(&initial,0x80059000); // compiled as scratchpad stores
        for (unsigned i=0;i<500;i++) {
            run_block(&initial,0x80058000);
            assert(psxRegs.GPR.r[2]==(i ? i-1 : 0));
            initial.GPR.r[1]=0x24020000|i;
            initial.GPR.r[2]=0x80058000; initial.GPR.r[3]=(i&1) ? 0x80060000-4 : 0x80058000-4;
            run_block(&initial,0x80059000);
            initial.GPR.r[2]=0x1f800010; initial.GPR.r[3]=0x1f800020;
            run_block(&initial,0x80058000);
            assert(psxRegs.GPR.r[2]==i);
        }
    }
    puts("500 code overwrites by stores compiled for the scratchpad, through their slow stubs: invalidated in every variant");
}

void dynarec_microtest(void)
{
    psxRegisters initial = psxRegs;
    psxGPRRegs expected;
    unsigned char data[1024], want[1024];
    uint32_t *code = (uint32_t *)(psxRegs.ptrs.psxM + 0x50000);
    for (unsigned test = 0; test < 6000; test++) {
        unsigned n = 0;
        for (unsigned i = 0; i < sizeof(data); i++) data[i] = random32();
        for (unsigned i = 1; i < 34; i++) initial.GPR.r[i] = random32();
        initial.GPR.r[0] = 0;
        int scratch = test >= 2000;
        initial.GPR.r[28] = scratch ? 0x1f800000 : 0x80060000;
        unsigned char *memory = scratch ? psxRegs.ptrs.psxH : psxRegs.ptrs.psxM + 0x60000;
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
                { const unsigned ops[] = {0x20,0x21,0x23,0x24,0x25,0x22,0x26};
                unsigned k = test < 2000 ? 2 : random32()%7;
                unsigned mask = k==2 ? 0x3fc : k==1 || k==4 ? 0x3fe : 0x3ff;
                code[n++] = (ops[k]<<26)|(28u<<21)|(rt<<16)|(random32()&mask); }
                code[n++] = 0; // resolve the architectural load delay
                break;
            default:
                { unsigned k = test < 2000 ? 2 : random32()%3;
                unsigned mask = k==0 ? 0x3ff : k==1 ? 0x3fe : 0x3fc;
                code[n++] = ((0x28u+k+(k==2))<<26)|(28u<<21)|(rt<<16)|(random32()&mask); } break;
            }
        }
        code[n++] = (31u<<21)|8; // jr ra
        code[n++] = 0;
        initial.next_interupt = 0x10000000;
        initial.dloadReg[0] = initial.dloadReg[1] = 0;
        psxRegs = initial;
        memcpy(memory, data, sizeof(data));
        psxCpu = &psxInt;
        psxInt.Notify(R3000ACPU_NOTIFY_AFTER_LOAD, NULL);
        psxInt.ExecuteBlock(&psxRegs, EXEC_CALLER_BOOT);
        psxInt.Notify(R3000ACPU_NOTIFY_BEFORE_SAVE, NULL);
        expected = psxRegs.GPR;
        memcpy(want, memory, sizeof(want));
        assert(psxRegs.pc == 0x80070000);
        unsigned cycle_off = 0;
        for (unsigned variant=0; variant<NVAR; variant++) {
        psx3d_dynr2 = variants[variant];
        psxRegs = initial;
        memcpy(memory, data, sizeof(data));
        psxCpu = &psxRec;
        new_dynarec_clear_full();
        psxRec.Notify(R3000ACPU_NOTIFY_AFTER_LOAD, NULL);
        psxRec.ExecuteBlock(&psxRegs, EXEC_CALLER_BOOT);
        psxRec.Notify(R3000ACPU_NOTIFY_BEFORE_SAVE, NULL);
        if (memcmp(&expected, &psxRegs.GPR, sizeof(expected)) ||
            memcmp(want, memory, sizeof(want)) || psxRegs.pc != 0x80070000) {
            fprintf(stderr, "microtest mismatch case %u pc %08x\n", test, psxRegs.pc);
            for (unsigned i=0;i<34;i++) if(expected.r[i]!=psxRegs.GPR.r[i])
                fprintf(stderr,"r%u expected %08x got %08x\n",i,expected.r[i],psxRegs.GPR.r[i]);
            assert(0);
        }
        if (!variant) cycle_off = psxRegs.cycle;
        else assert(cycle_off == psxRegs.cycle);
        }
    }
    stress_guards_smc(initial);
    stress_groups(initial);
    stress_spad_smc(initial);
    puts("6000 randomized guest ALU/RAM/scratchpad byte/half/word/LWL/LWR programs: interpreter GPR/memory/PC identical; dynarec off/on cycles identical");
}
