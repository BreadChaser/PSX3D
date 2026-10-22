/* Synthetic HLE execution + state round trip: no game/BIOS or private fixture. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro.h"
#include "libpcsxcore/r3000a.h"
#include "libpcsxcore/new_dynarec/new_dynarec.h"
bool pica_init(const void *, unsigned);
bool pica_game_begin(bool);
void pica_frame_started(void);
void pica_frame_ended(void);
void renderer_flush_queues(void);
static uint64_t digest(const void *ptr, size_t n) {
    const unsigned char *p=ptr;
    uint64_t h=1469598103934665603ull;
    while(n--) h=(h^*p++)*1099511628211ull;
    return h;
}
void dynarec_microtest(void) {
    static const unsigned shader=0;
    assert(pica_init(&shader,sizeof(shader)));
    assert(pica_game_begin(true));
    size_t size=retro_serialize_size();
    void *saved=malloc(size);
    assert(saved && retro_serialize(saved,size));
    uint64_t hashes[60][3]; unsigned cycles[60];
    for(unsigned pass=0;pass<2;pass++) {
        assert(retro_unserialize(saved,size));
        for(unsigned frame=0;frame<60;frame++) {
            pica_frame_started(); retro_run(); renderer_flush_queues(); pica_frame_ended();
            uint64_t h[]={digest(psxRegs.ptrs.psxM,0x200000),digest(&psxRegs.GPR,sizeof(psxRegs.GPR)),digest(&psxRegs.CP2,sizeof(psxRegs.CP2))};
            if(!pass) {memcpy(hashes[frame],h,sizeof(h)); cycles[frame]=psxRegs.cycle;}
            else {assert(!memcmp(hashes[frame],h,sizeof(h))); assert(cycles[frame]==psxRegs.cycle);}
        }
    }
    assert(*(uint32_t *)(psxRegs.ptrs.psxM+0x100)); // guest actually executed
    free(saved);
    puts("PASS: synthetic HLE core 60-frame state round trip: every RAM/GPR/CP2 hash and cycle identical");
}
