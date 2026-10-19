/* cachesim: Old 3DS ARM11 L1 cache model for qemu-arm.
 * I-cache and D-cache: 16KB, 4-way, 32-byte lines, round-robin replacement.
 * D-cache is write-back, allocates on read misses only (a store that misses
 * goes to memory through the write buffer).
 * Per translation block: executions, instructions, I-cache misses, D-cache
 * read misses, store misses. Dumped at exit to $CACHESIM_OUT as
 *   pc ninsns execs imiss rmiss wmiss hazard
 * hazard: ARM11 interlock cycles (results needed before they are ready), from a
 * static model of each block (see hazard_model), times the executions.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <qemu-plugin.h>
QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

#define WAYS 4
#define LINE 32
#define SETS (16384 / LINE / WAYS)   /* 128 */

typedef struct { uint32_t tag[SETS][WAYS]; uint8_t valid[SETS][WAYS]; uint8_t next[SETS]; } cache_t;
static cache_t icache, dcache;

typedef struct { uint32_t pc, word, hz; uint64_t imiss, rmiss, wmiss; void *tb; } detail_t;
static uint32_t detail_lo, detail_hi;
typedef struct { detail_t *detail; uint64_t pc, n, execs, imiss, rmiss, wmiss; uint32_t nlines, hz; uint32_t lines[64]; } tb_t;
static GHashTable *tbs;
static GMutex lock;

/* returns 1 on miss; allocate says whether a miss fills the line */
static inline int access_cache(cache_t *c, uint32_t addr, int allocate)
{
    uint32_t line = addr / LINE, set = line % SETS, tag = line / SETS;
    for (int w = 0; w < WAYS; w++)
        if (c->valid[set][w] && c->tag[set][w] == tag)
            return 0;
    if (allocate) {
        int w = c->next[set];
        c->tag[set][w] = tag;
        c->valid[set][w] = 1;
        c->next[set] = (w + 1) % WAYS;
    }
    return 1;
}


/* ARM11 interlock model. Each instruction issues one cycle after the previous
 * one (plus its stalls); a register has a ready cycle. Results: ALU next cycle,
 * loads and VFP->core moves 3 (next instruction waits 2), multiplies 3, long
 * multiplies 4, VFP arithmetic 4 (divide/sqrt 15). "Early" operands (load/store
 * base and offset, shifted operands, shift amounts) are needed a cycle sooner.
 * Registers 0-15 core, 16-47 VFP single registers (d = two singles). */
static int rdy[48];
static int now_c, stall_c;
static void use(int r, int early)
{
    int need = rdy[r] + early;
    if (need > now_c) { stall_c += need - now_c; now_c = need; }
}
static void def(int r, int lat) { if (r != 15) rdy[r] = now_c + lat; }
static int sreg(uint32_t v, int dbit) { return 16 + ((v << 1) | dbit); }   /* Sd: Vd:D */
static int dreg(uint32_t v) { return 16 + 2 * (v & 15); }
static void usev(int r, int dbl) { use(r, 0); if (dbl) use(r + 1, 0); }
static void defv(int r, int dbl, int lat) { def(r, lat); if (dbl) def(r + 1, lat); }

static void hazard_insn(uint32_t i)
{
    uint32_t cond = i >> 28;
    int rn = (i >> 16) & 15, rd = (i >> 12) & 15, rs = (i >> 8) & 15, rm = i & 15;
    if (cond == 15) return;                       /* pld, blx imm, ... */
    switch ((i >> 25) & 7) {
    case 0:
        if ((i & 0x0ff000f0) == 0x01200010 || (i & 0x0ff000f0) == 0x01200030) { use(rm, 0); return; } /* bx/blx */
        if ((i & 0x0fc000f0) == 0x00000090) {     /* mul/mla */
            use(rm, 0); use(rs, 0); if (i & (1 << 21)) use(rd, 0); def(rn, 3); return;
        }
        if ((i & 0x0f8000f0) == 0x00800090) {     /* long multiplies */
            use(rm, 0); use(rs, 0); if (i & (1 << 21)) { use(rd, 0); use(rn, 0); }
            def(rd, 4); def(rn, 4); return;
        }
        if ((i & 0x0f900090) == 0x01000080) {     /* smlaxy etc. */
            use(rm, 0); use(rs, 0); if (((i >> 21) & 3) != 3) use(rd, 0); def(rn, 2); return;
        }
        if ((i & 0x0fff0ff0) == 0x016f0f10) { use(rm, 0); def(rd, 1); return; }  /* clz */
        if ((i & 0x0fb00ff0) == 0x01000090) { use(rn, 1); use(rm, 0); def(rd, 3); return; } /* swp */
        if ((i & 0x0f900ff0) == 0x01000000 || (i & 0x0fb0f000) == 0x0120f000) { use(rm, 0); def(rd, 1); return; } /* mrs/msr */
        if ((i & 0x90) == 0x90 && (i & 0x60)) {   /* ldrh/strh/ldrsb/ldrd/strd */
            int l = (i >> 20) & 1, imm = (i >> 22) & 1, dw = !l && (i & 0x40);
            use(rn, 1); if (!imm) use(rm, 1);
            if (l) def(rd, 3);
            else if ((i & 0x60) == 0x40) { def(rd, 3); def(rd + 1, 3); }   /* ldrd */
            else { use(rd, 0); if (dw) use(rd + 1, 0); }
            if (!((i >> 24) & 1) || ((i >> 21) & 1)) def(rn, 1);
            return;
        }
        /* fall through: data processing, register operand */
        {
            int op = (i >> 21) & 15, shifted = (i & 0xff0) != 0;
            if (i & 0x10) { use(rs, 1); use(rm, 1); }
            else use(rm, shifted);
            if (op != 13 && op != 15) use(rn, 0);
            if (op < 8 || op > 11) def(rd, (i & 0x10) ? 2 : 1);
            return;
        }
    case 1: {                                     /* data processing immediate */
        int op = (i >> 21) & 15;
        if ((i & 0x0fb00000) == 0x03200000) return;   /* msr imm / hints */
        if (op != 13 && op != 15) use(rn, 0);
        if (op < 8 || op > 11) def(rd, 1);
        return;
    }
    case 3:
        if (i & 0x10) {                               /* media: sxt/uxt/rev/usat/... */
            if ((i & 0x0f8000f0) == 0x06800070 || (i & 0x0f800070) == 0x06a00070 || 1) {
                use(rm, 0); if (rn != 15) use(rn, 0); def(rd, 1);
            }
            return;
        }
        /* fall through: load/store with register offset */
    case 2: {
        int l = (i >> 20) & 1;
        use(rn, 1); if (i & (1 << 25)) use(rm, 1);
        if (l) def(rd, 3); else use(rd, 0);
        if (!((i >> 24) & 1) || ((i >> 21) & 1)) def(rn, 1);
        return;
    }
    case 4: {                                     /* ldm/stm */
        int l = (i >> 20) & 1, n = 0;
        use(rn, 1);
        for (int r = 0; r < 16; r++)
            if (i & (1 << r)) {
                if (l) def(r, 3 + n / 2); else use(r, 0);
                n++;
            }
        if (i & (1 << 21)) def(rn, 1);
        return;
    }
    case 5:
        if (i & (1 << 24)) def(14, 1);            /* bl */
        return;
    case 6: {                                     /* vldr/vstr/vldm/vstm (cp10/11) */
        int cp = (i >> 8) & 15, dbl = cp == 11, l = (i >> 20) & 1;
        if (cp != 10 && cp != 11) return;
        if ((i & 0x0fe00000) == 0x0c400000) {     /* vmov 2 core <-> vfp */
            if (l) { def(rd, 3); def(rn, 3); } else { use(rd, 0); use(rn, 0); }
            return;
        }
        int v = dbl ? dreg(((i >> 22) & 1) << 4 | rd) : sreg(rd, (i >> 22) & 1);
        int cnt = dbl ? (i & 0xff) / 2 : (i & 0xff);
        use(rn, 1);
        if (((i >> 24) & 3) == 1 && !((i >> 21) & 1) && 0) {}
        if (((i >> 23) & 3) == 2 && ((i >> 21) & 1) == 0 && (i >> 24) & 1) cnt = 1;   /* vldr/vstr */
        if (cnt < 1) cnt = 1;
        for (int k = 0; k < cnt && v < 48; k++, v += dbl ? 2 : 1) {
            if (l) defv(v, dbl, 3 + k / 2); else usev(v, dbl);
        }
        if ((i >> 21) & 1) def(rn, 1);
        return;
    }
    case 7: {
        int cp = (i >> 8) & 15, dbl = cp == 11;
        if ((i >> 24) & 1) return;                /* svc */
        if (cp != 10 && cp != 11) { if ((i & 0x10) && ((i >> 20) & 1)) def(rd, 2); return; }
        if (i & 0x10) {                           /* vmov core<->single, vmrs/vmsr */
            int l = (i >> 20) & 1;
            if ((i & 0x0fef0f10) == 0x0ee10a10) { if (l) def(rd, 2); else use(rd, 0); return; }
            int sv = sreg(rn, (i >> 7) & 1);
            if (dbl) sv = dreg(((i >> 7) & 1) << 4 | rn);
            if (l) { use(sv, 0); def(rd, 3); } else { use(rd, 0); def(sv, 2); }
            return;
        }
        /* data processing: Vd, Vn, Vm */
        int D = (i >> 22) & 1, N = (i >> 7) & 1, M = (i >> 5) & 1;
        int vd = dbl ? dreg(D << 4 | rd) : sreg(rd, D);
        int vn = dbl ? dreg(N << 4 | rn) : sreg(rn, N);
        int vm = dbl ? dreg(M << 4 | rm) : sreg(rm, M);
        int opc = ((i >> 20) & 0xb), lat = 4;
        if (opc == 0xb) {                         /* other: vmov/vabs/vneg/vsqrt/vcmp/vcvt */
            int op2 = (rn << 1) | ((i >> 7) & 1) ;
            (void)op2;
            usev(vm, dbl);
            if (((i >> 16) & 0xf) == 1 && ((i >> 7) & 1)) lat = dbl ? 29 : 15; /* vsqrt */
            if (((i >> 16) & 0xe) == 4) return;    /* vcmp */
            defv(vd, dbl, lat);
            return;
        }
        if (opc == 8) lat = dbl ? 29 : 15;         /* vdiv */
        usev(vn, dbl); usev(vm, dbl);
        if (opc == 0 || opc == 1) { usev(vd, dbl); lat = 8; }   /* vmla/vmls */
        defv(vd, dbl, lat);
        return;
    }
    }
}

static uint32_t hazard_model(struct qemu_plugin_tb *tb, size_t n, detail_t *detail)
{
    memset(rdy, 0, sizeof(rdy));
    now_c = 0; stall_c = 0;
    for (size_t k = 0; k < n; k++) {
        struct qemu_plugin_insn *in = qemu_plugin_tb_get_insn(tb, k);
        uint32_t w = 0;
        if (qemu_plugin_insn_size(in) != 4) continue;
        qemu_plugin_insn_data(in, &w, 4);
        int old = stall_c;
        hazard_insn(w);
        if (detail) detail[k].hz = stall_c-old;
        now_c++;
    }
    return stall_c;
}

static void exec_cb(unsigned int cpu, void *udata)
{
    tb_t *t = udata;
    t->execs++;
    for (uint32_t i = 0; i < t->nlines; i++) {
        int miss=access_cache(&icache, t->lines[i], 1); t->imiss+=miss;
        if (t->detail && miss) {
            for (unsigned k=0;k<t->n;k++) if ((t->detail[k].pc & ~31u)==t->lines[i]) {
                t->detail[k].imiss++; break;
            }
        }
    }
}

static void mem_cb(unsigned int cpu, qemu_plugin_meminfo_t info, uint64_t vaddr, void *udata)
{
    detail_t *d = udata; tb_t *t=d->tb;
    if (qemu_plugin_mem_is_store(info)) { int m=access_cache(&dcache,(uint32_t)vaddr,0); t->wmiss+=m; d->wmiss+=m; }
    else { int m=access_cache(&dcache,(uint32_t)vaddr,1); t->rmiss+=m; d->rmiss+=m; }
}
static void mem_host(unsigned int cpu, qemu_plugin_meminfo_t info, uint64_t vaddr, void *udata)
{
    tb_t *t=udata;
    if (qemu_plugin_mem_is_store(info)) t->wmiss+=access_cache(&dcache,(uint32_t)vaddr,0);
    else t->rmiss+=access_cache(&dcache,(uint32_t)vaddr,1);
}

static void tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    uint64_t pc = qemu_plugin_tb_vaddr(tb);
    size_t n = qemu_plugin_tb_n_insns(tb);
    uint64_t key = pc ^ ((uint64_t)n << 48);
    g_mutex_lock(&lock);
    tb_t *t = g_hash_table_lookup(tbs, &key);
    if (!t) {
        t = g_new0(tb_t, 1);
        t->pc = pc;
        t->n = n;
        if (detail_lo <= pc && pc < detail_hi) {
            t->detail=g_new0(detail_t,n);
            for (unsigned k=0;k<n;k++) {
                struct qemu_plugin_insn *in=qemu_plugin_tb_get_insn(tb,k);
                t->detail[k].tb=t; t->detail[k].pc=qemu_plugin_insn_vaddr(in);
                qemu_plugin_insn_data(in,&t->detail[k].word,4);
            }
        }
        /* the cache lines this block's code covers */
        for (size_t i = 0; i < n; i++) {
            struct qemu_plugin_insn *in = qemu_plugin_tb_get_insn(tb, i);
            uint32_t l = (uint32_t)qemu_plugin_insn_vaddr(in) & ~(LINE - 1);
            if ((!t->nlines || t->lines[t->nlines - 1] != l) && t->nlines < 64)
                t->lines[t->nlines++] = l;
        }
        t->hz = hazard_model(tb, n, t->detail);
        uint64_t *k = g_new(uint64_t, 1);
        *k = key;
        g_hash_table_insert(tbs, k, t);
    }
    g_mutex_unlock(&lock);
    qemu_plugin_register_vcpu_tb_exec_cb(tb, exec_cb, QEMU_PLUGIN_CB_NO_REGS, t);
    for (size_t i = 0; i < n; i++) {
        struct qemu_plugin_insn *in = qemu_plugin_tb_get_insn(tb, i);
        if (t->detail) qemu_plugin_register_vcpu_mem_cb(in,mem_cb,QEMU_PLUGIN_CB_NO_REGS,QEMU_PLUGIN_MEM_RW,&t->detail[i]);
        else qemu_plugin_register_vcpu_mem_cb(in,mem_host,QEMU_PLUGIN_CB_NO_REGS,QEMU_PLUGIN_MEM_RW,t);
    }
}

static void at_exit(qemu_plugin_id_t id, void *p)
{
    const char *out = getenv("CACHESIM_OUT");
    FILE *f = fopen(out ? out : "cachesim.txt", "w");
    char name[1024]; snprintf(name,sizeof(name),"%s.detail",out?out:"cachesim.txt");
    FILE *df=fopen(name,"w");
    GHashTableIter it;
    gpointer k, v;
    g_hash_table_iter_init(&it, tbs);
    while (g_hash_table_iter_next(&it, &k, &v)) {
        tb_t *t = v;
        if (t->execs && t->detail) for (unsigned j=0;j<t->n;j++) {
            detail_t *d=&t->detail[j];
            fprintf(df,"%x %08x %llu %llu %llu %llu %llu\n",d->pc,d->word,
              (unsigned long long)t->execs,(unsigned long long)d->imiss,
              (unsigned long long)d->rmiss,(unsigned long long)d->wmiss,(unsigned long long)d->hz*t->execs);
        }
        if (t->execs)
            fprintf(f, "%llx %llu %llu %llu %llu %llu %llu\n", (unsigned long long)t->pc, (unsigned long long)t->n,
                (unsigned long long)t->execs, (unsigned long long)t->imiss, (unsigned long long)t->rmiss,
                (unsigned long long)t->wmiss, (unsigned long long)t->hz * t->execs);
    }
    fclose(f); fclose(df);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info, int argc, char **argv)
{
    detail_lo=getenv("DETAIL_LO")?strtoul(getenv("DETAIL_LO"),NULL,0):0;
    detail_hi=getenv("DETAIL_HI")?strtoul(getenv("DETAIL_HI"),NULL,0):0;
    tbs = g_hash_table_new(g_int64_hash, g_int64_equal);
    qemu_plugin_register_vcpu_tb_trans_cb(id, tb_trans);
    qemu_plugin_register_atexit_cb(id, at_exit, NULL);
    return 0;
}
