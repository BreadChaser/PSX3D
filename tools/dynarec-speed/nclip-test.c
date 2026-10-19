// ARMv6 arithmetic oracle: run under qemu-arm, not on a console.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "libpcsxcore/r3000a.h"

extern void gteNCLIP_nf_armv6(psxCP2Regs *, uint32_t);
extern void gteNCLIP_nf(psxCP2Regs *, uint32_t);
// Unused fallback references in the same assembly .text section.
void gteMVMVA_generic(void) { assert(0); }
void gteRTPS_sf1lm0(void) { assert(0); }
void gteRTPT_sf1lm0(void) { assert(0); }

static uint32_t rng = 0x735f019a;
static uint32_t next(void) { rng ^= rng << 13; rng ^= rng >> 17; return rng ^= rng << 5; }
static void check(psxCP2Regs a)
{
    psxCP2Regs b = a;
    gteNCLIP_nf(&a, 6);
    gteNCLIP_nf_armv6(&b, 6);
    assert(!memcmp(&a, &b, sizeof(a)));
}
int main(void)
{
    static const int16_t edge[] = {-32768, -32767, -1024, -1, 0, 1, 1023, 32766, 32767};
    psxCP2Regs a;
    unsigned n = 1;
    for (int i = 0; i < 6; i++) n *= sizeof(edge) / sizeof(*edge);
    for (unsigned i = 0; i < n; i++) {
        memset(&a, 0xa5, sizeof(a));
        unsigned k = i;
        for (int j = 0; j < 6; j++, k /= sizeof(edge) / sizeof(*edge))
            ((uint16_t *)&a.CP2D.r[12])[j] = (uint16_t)edge[k % (sizeof(edge) / sizeof(*edge))];
        check(a);
    }
    for (unsigned i = 0; i < 1000000; i++) {
        for (unsigned j = 0; j < sizeof(a) / 4; j++) ((uint32_t *)&a)[j] = next();
        check(a);
    }
    printf("NCLIP: %u edge combinations + 1000000 random full-register cases identical\n", n);
}
