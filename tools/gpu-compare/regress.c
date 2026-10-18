/* Host regressions for polygon coordinate overflow and held-frame lifetime. */
#include "plugins/gpulib/gpu.h"
#include "frontend/plugin_lib.h"
#include "plugins/gpu_pica/gpu_pica.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct psx_gpu gpu;
unsigned sim_c3d_draws;
static uint16_t vram[1024 * 512];
void pica_flip(void);
void pica_readback_rect(int x,int y,int w,int h);
void pica_drop_fills(void);

static u32 xy(int x, int y) { return (u16)x | ((u32)(u16)y << 16); }
static void commands(u32 *p, int n)
{
    u32 ex[8] = {0}; int sum=0, last=0, cmd=0;
    assert(renderer_do_cmd_list(p,n,ex,&sum,&last,&cmd)==n);
}
static void area(int x, int y, int right, int bottom, int ox, int oy)
{
    u32 p[]={0xe3000000u|x|(y<<10),0xe4000000u|(right-1)|((bottom-1)<<10),
        0xe5000000u|(ox&2047)|((oy&2047)<<11)};
    commands(p,3);
}
static void clear(void)
{
    u32 p[]={0x02000000,0,xy(1023,511),0x02000000,xy(0,511),xy(1023,1)};
    commands(p,6);
}
static void triangle(int x0,int y0,int x1,int y1,int x2,int y2)
{
    u32 p[]={0x200000ff,xy(x0,y0),xy(x1,y1),xy(x2,y2)};
    commands(p,4);
}
static void pixel(int x,int y,bool red)
{
    renderer_flush_queues();
    assert(((sim_target_pixel(x,y)>>11)!=0)==red);
}

int main(void)
{
    gpu.vram=vram;
    renderer_init();
    unsigned shader=0;
    assert(pica_init(&shader,4) && pica_game_begin(true));
    pica_frame_started();

    // Equivalent polygons cross both edges of both Crash framebuffers.
    // The right-buffer right edge used to wrap vertices separately and
    // then reject a legitimate 40px-wide triangle as >1023px wide.
    u16 edge[2][2][32*32];
    for(int bank=0;bank<2;bank++)for(int side=0;side<2;side++) {
        clear();
        area(bank*512,0,bank*512+512,240,bank*512+256,120);
        int x=side?236:-276;
        triangle(x,-20,x+40,-20,x,20);
        renderer_flush_queues();
        for(int y=0;y<32;y++)for(int k=0;k<32;k++)
            edge[bank][side][y*32+k]=sim_target_pixel(bank*512+(side?480:0)+k,96+y);
        pixel(bank*512+(side?495:2),105,true);
    }
    assert(!memcmp(edge[0],edge[1],sizeof(edge[0])));

    // An inside-area primitive ending exactly at the target edge uses
    // the fast path. It must use the same PICA boundary clamp as clipping.
    clear(); area(512,0,1024,240,768,120);
    triangle(236,-20,256,-20,236,20);
    pixel(1008,105,true);

    clear(); area(0,0,1024,512,-1024,0);
    triangle(-16,100,24,100,-16,140); // common wrap: -1040 -> 1008
    pixel(1010,105,true);
    clear(); area(0,0,1024,512,0,-1024);
    triangle(100,-524,140,-524,100,-484); // common Y wrap: -1548 -> 500
    pixel(105,505,true);

    clear(); area(0,0,1024,512,0,0);
    triangle(0,100,1023,100,0,140); // exactly 1023 wide is legal
    pixel(5,105,true);
    clear(); triangle(-1,100,1023,100,-1,140); // 1024 wide is illegal
    pixel(5,105,false);
    triangle(10,0,50,0,10,511); // exactly 511 tall is legal
    pixel(15,5,true);
    clear(); triangle(10,-1,50,-1,10,511); // 512 tall is illegal
    pixel(15,5,false);

    // A quad's size limit is per triangle: retain its legal second half.
    u32 quad[]={0x280000ff,xy(-1000,10),xy(10,10),xy(30,30),xy(50,10)};
    commands(quad,5); pixel(30,15,true);
    // The two halves must also get their own wrapped minimum.
    clear(); area(0,0,1024,512,-1024,0);
    u32 wrapped_quad[]={0x280000ff,xy(-16,100),xy(24,100),xy(-16,140),xy(24,140)};
    commands(wrapped_quad,5); pixel(1010,105,true);

    clear(); area(0,0,1024,512,0,0);
    triangle(100,100,140,100,100,140);
    // No texture feedback: fine bookkeeping stays disabled. An explicit
    // read of GPU drawing enables it safely, and a state reset drops it.
    char stats[2048];
    pica_stats(stats,sizeof(stats)); assert(strstr(stats,"rtt old"));
    pica_readback_rect(100,100,8,8);
    pica_stats(stats,sizeof(stats)); assert(strstr(stats,"rtt new"));
    pica_drop_fills();
    pica_stats(stats,sizeof(stats)); assert(strstr(stats,"rtt old"));
    triangle(100,100,140,100,100,140);
    unsigned draws=sim_c3d_draws;
    pica_prepare_present();
    assert(sim_c3d_draws>draws); // flush the final pending batch here
    C3D_Tex *tex; float l,t,r,b,w,h;
    gpu.screen.w=512; gpu.screen.h=240;
    draws=sim_c3d_draws;
    assert(pica_picture(&tex,&l,&t,&r,&b,&w,&h)==PICA_PIC_GPU);
    assert(pica_picture(&tex,&l,&t,&r,&b,&w,&h)==PICA_PIC_GPU);
    assert(sim_c3d_draws==draws); // stereo queries cannot rebind/draw

    // A real flip followed by a large skipped-frame fill must retain red
    // in the presented texture even though PS1 VRAM itself is now black.
    for(int y=0;y<=256;y+=256) {
        gpu.frameskip.set=1; gpu.screen.src_y=y;
        u32 fill[]={0x020000ff,xy(0,y),xy(1023,240)};
        commands(fill,3); renderer_flush_queues(); pica_flip();
        clear(); pica_prepare_present();
        assert(pica_picture(&tex,&l,&t,&r,&b,&w,&h)==PICA_PIC_GPU);
        assert((((u16*)tex->data)[sim_tiled(20,y+20,1024)]>>11)==31);
        pixel(20,y+20,false);
    }
    renderer_update_caches(0,0,1024,512,1); // loading a state invalidates hold
    pica_prepare_present();
    assert(pica_picture(&tex,&l,&t,&r,&b,&w,&h)==PICA_PIC_GPU);
    assert(((u16*)tex->data)[sim_tiled(20,276,1024)]==0);
    gpu.status=PSX_GPU_STATUS_BLANKING;
    assert(pica_picture(&tex,&l,&t,&r,&b,&w,&h)==PICA_PIC_NONE);
    gpu.status=PSX_GPU_STATUS_RGB24;
    assert(pica_picture(&tex,&l,&t,&r,&b,&w,&h)==PICA_PIC_CPU);
    pica_frame_ended();
    pica_game_end();
    assert(pica_game_begin(true)); // buffer cleanup/reallocation
    pica_exit();
    puts("polygon edges, size limits, presentation handoff and held frames: PASS");
    return 0;
}
