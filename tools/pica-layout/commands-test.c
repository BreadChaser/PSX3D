/* Compare default and split builds: GP0 line consumption, timing, and pixels. */
#define main polygon_regress_main
#include "../gpu-compare/regress.c"
#undef main
extern unsigned long long pica_sim_hash;
int main(void)
{
    gpu.vram = vram;
    renderer_init();
    unsigned shader = 0;
    assert(pica_init(&shader, 4) && pica_game_begin(true));
    pica_frame_started();
    area(0, 0, 320, 240, 0, 0);
    for (unsigned cmd = 0x40; cmd <= 0x5f; cmd++) {
        u32 words[24] = {0x200000ff, xy(2,2), xy(4,2), xy(2,4)};
        int n = 4;
        words[n++] = (cmd << 24) | 0x80ff;
        words[n++] = xy(20,20);
        if (cmd & 0x10) words[n++] = 0xff0080;
        words[n++] = xy(40,40);
        if (cmd & 8) {
            if (cmd & 0x10) words[n++] = 0xff8000;
            words[n++] = xy(60,20);
            words[n++] = 0x50005000;
        }
        words[n++] = 0xe6000000;
        for (int count = 0; count <= n; count++) {
            u32 ex[8] = {0}; int sum = 31, last = 17, final = 0;
            int used = renderer_do_cmd_list(words, count, ex, &sum, &last, &final);
            renderer_flush_queues();
            printf("%02x %d: %d %d %d %08x %016llx\n", cmd, count,
                   used, sum, last, final, pica_sim_hash);
        }
    }
    unsigned long long pixels = 1469598103934665603ull;
    for (int y = 0; y < 240; y++) for (int x = 0; x < 320; x++)
        pixels = (pixels ^ sim_target_pixel(x,y)) * 1099511628211ull;
    printf("pixels %016llx\n", pixels);
    pica_exit();
    return 0;
}
