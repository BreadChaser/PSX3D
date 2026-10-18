/* Link with --wrap for the gpulib renderer interface. Little-endian trace. */
#include "plugins/gpulib/gpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
uint16_t *psx3d_vram2;
unsigned sim_flips; /* older simulator gpu.c diagnostic */
void psx3d_set_half3d(int on) { (void)on; }
static FILE *out;
static int enabled;
static void record(unsigned tag, const void *data, unsigned bytes)
{
    if(!enabled)return;
    if(!out) {
        const char *path=getenv("GPU_TRACE");
        if(!path)return;
        out=fopen(path,"wb"); assert(out);
        fwrite("PSXGPU01",1,8,out);
    }
    unsigned header[2]={tag,bytes};
    assert(fwrite(header,4,2,out)==2);
    assert(fwrite(data,1,bytes,out)==bytes);
}
int __real_renderer_do_cmd_list(uint32_t*,int,uint32_t*,int*,int*,int*);
int __wrap_renderer_do_cmd_list(uint32_t *list,int n,uint32_t *ex,int *sum,int *last,int *cmd)
{
    uint32_t *copy=malloc(n*4); assert(copy); memcpy(copy,list,n*4);
    int used=__real_renderer_do_cmd_list(list,n,ex,sum,last,cmd);
    if(used)record(1,copy,used*4);
    free(copy); return used;
}
void __real_renderer_sync_ecmds(uint32_t*);
void __wrap_renderer_sync_ecmds(uint32_t *ex)
{ record(2,ex,32); __real_renderer_sync_ecmds(ex); }
void __real_renderer_update_caches(int,int,int,int,int);
void __wrap_renderer_update_caches(int x,int y,int w,int h,int changed)
{
    int rect[5]={x,y,w,h,changed};
    record(3,rect,sizeof(rect));
    uint16_t *pixels=malloc(w*h*2); assert(pixels);
    for(int r=0;r<h;r++)for(int c=0;c<w;c++)
        pixels[r*w+c]=gpu.vram[((y+r)&511)*1024+((x+c)&1023)];
    record(4,pixels,w*h*2); free(pixels);
    __real_renderer_update_caches(x,y,w,h,changed);
}
void trace_frame(int frame)
{
    int data[12]={frame,gpu.status,gpu.screen.src_x,gpu.screen.src_y,
        gpu.screen.w,gpu.screen.h,gpu.screen.hres,gpu.screen.vres,
        gpu.screen.x,gpu.screen.y,gpu.screen.x1,gpu.screen.y1};
    record(5,data,sizeof(data));
    int every=getenv("GPU_VERIFY_EVERY")?atoi(getenv("GPU_VERIFY_EVERY")):1;
    if(getenv("GPU_VERIFY") && every>0 && frame%every==0)
        record(6,gpu.vram,1024*512*2);
    if(out)fflush(out);
}

void trace_state(void)
{
    enabled=1;
    record(2,gpu.ex_regs,32);
    int rect[5]={0,0,1024,512,1}; record(3,rect,20);
    record(4,gpu.vram,1024*512*2);
}

// GP0 80h is executed by gpulib before renderer_update_caches. Record it
// explicitly so native replay can independently test the GPU copy path.
void trace_vram_copy(const uint32_t *params)
{ record(8,params,16); }
