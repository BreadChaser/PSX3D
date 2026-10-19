#include "plugins/gpulib/gpu.h"
#include "frontend/plugin_lib.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "commands.h"
#ifdef PICA
#include "citro3d.h"
#include "plugins/gpu_pica/gpu_pica.h"
#include "plugins/gpulib/psx3d_frameskip.h"
psx3d_skip_state psx3d_skip;
extern int pica_debug;
#endif
#ifdef GPU_COMPARE_ALT_REFERENCE
// The portable gpu_neon backend uses this only for allocation diagnostics.
void SysPrintf(const char *fmt, ...) { (void)fmt; }
#endif
int psx3d_skipgate = 1;
struct psx_gpu gpu;
unsigned sim_c3d_draws;
static uint16_t vram[1024*1024]; /* peops uses extra guard space */
static void ppm(const char *dir,int *info)
{
    int frame=info[0],x=info[2],y=info[3],w=info[4],h=info[5];
    int every=getenv("FRAME_EVERY")?atoi(getenv("FRAME_EVERY")):1;
    if(every>1 && frame%every)return;
    if(getenv("FRAME_FIRST") && frame<atoi(getenv("FRAME_FIRST")))return;
    if(getenv("FRAME_LAST") && frame>atoi(getenv("FRAME_LAST")))return;

    const char *selected=getenv("GPU_FRAMES");
    if(selected) {
        int keep=0;
        while(*selected) {
            char *end; long n=strtol(selected,&end,10);
            if(end==selected)break;
            if(n==frame)keep=1;
            selected=*end==',' ? end+1 : end;
        }
        if(!keep)return;
    }
    if(w<=0||h<=0)return;
#ifdef PICA
    if(info[1]&PSX_GPU_STATUS_RGB24)
        pica_prepare_cpu_scanout(y*2048+x*2,w,h);
#endif
    char path[4096]; snprintf(path,sizeof(path),"%s/frame-%04d.ppm",dir,frame);
    FILE *f=fopen(path,"wb"); assert(f); fprintf(f,"P6\n%d %d\n255\n",w,h);
    for(int r=0;r<h;r++)for(int c=0;c<w;c++) {
        unsigned char rgb[3];
        if(info[1]&PSX_GPU_STATUS_RGB24) {
            unsigned char *p=(void*)(gpu.vram+((y+r)&511)*1024);
            for(int k=0;k<3;k++)rgb[k]=p[(x*2+c*3+k)&2047];
        } else {
#ifdef PICA
            u16 p=sim_target_pixel((x+c)&1023,(y+r)&511);
            rgb[0]=(p>>11)*255/31; rgb[1]=((p>>6)&31)*255/31; rgb[2]=((p>>1)&31)*255/31;
#else
            uint16_t p=gpu.vram[((y+r)&511)*1024+((x+c)&1023)];
            rgb[0]=(p&31)*255/31; rgb[1]=((p>>5)&31)*255/31; rgb[2]=((p>>10)&31)*255/31;
#endif
        }
        assert(fwrite(rgb,1,3,f)==3);
    }
    assert(fclose(f)==0);
}
int main(int argc,char **argv)
{
    if(argc!=3) { fprintf(stderr,"usage: replay trace output-directory\n"); return 1; }
    FILE *f=fopen(argv[1],"rb"); assert(f);
    char magic[8]; assert(fread(magic,1,8,f)==8&&!memcmp(magic,"PSXGPU01",8));
    gpu.vram=vram;
    renderer_init();
    struct rearmed_cbs cbs={0}; cbs.dithering=getenv("GPU_DITHER") ? atoi(getenv("GPU_DITHER")) : 1; renderer_set_config(&cbs);
#ifdef PICA
    unsigned shader=0; assert(pica_init(&shader,4)); assert(pica_game_begin(true));
    if(getenv("PICA_DEBUG"))pica_debug=atoi(getenv("PICA_DEBUG"));
    if(getenv("PICA_SUBMIT"))pica_submit=strtoul(getenv("PICA_SUBMIT"),NULL,0);
    if(getenv("PICA_PRIMS"))pica_prims=strtoul(getenv("PICA_PRIMS"),NULL,0);
    extern bool pica_texmask, pica_texmemo, pica_texevict;
    if(getenv("TEXEVICT"))pica_texevict=atoi(getenv("TEXEVICT"));
    if(getenv("TEXMASK"))pica_texmask=atoi(getenv("TEXMASK"));
    if(getenv("TEXMEMO"))pica_texmemo=atoi(getenv("TEXMEMO"));
    pica_frame_started();
#endif
    uint32_t ex[8]={0}; int rect[5]={0}; unsigned header[2];
    int copied=0;
    while(fread(header,4,2,f)==2) {
        unsigned tag=header[0],bytes=header[1];
        assert(bytes<=16*1024*1024);
        void *data=malloc(bytes+4); assert(data); assert(fread(data,1,bytes,f)==bytes);
        switch(tag) {
        case 1: {
            int sum=0,last=0,cmd=0;
            int used=renderer_do_cmd_list(data,bytes/4,ex,&sum,&last,&cmd);
            if(used!=(int)bytes/4) { fprintf(stderr,"short replay %d/%u command %x\n",used,bytes/4,cmd); return 2; }
            break;
        }
        case 2: assert(bytes==32); memcpy(ex,data,32); renderer_sync_ecmds(ex); break;
        case 3: assert(bytes==20); memcpy(rect,data,20); break;
        case 4: {
#ifdef GPU_COMPARE_ALT_REFERENCE
            // gpu_neon queues blocks which still read VRAM/CLUTs. Match
            // gpulib's synchronization before the CPU overwrites them.
            renderer_flush_queues();
#endif
#ifdef PICA
            if(copied) { copied=0; break; } // the explicit GPU copy already ran
#endif
            int x=rect[0],y=rect[1],w=rect[2],h=rect[3];
            assert(bytes==(unsigned)(w*h*2));
            for(int r=0;r<h;r++)for(int c=0;c<w;c++)
                gpu.vram[((y+r)&511)*1024+((x+c)&1023)]=((uint16_t*)data)[r*w+c];
            renderer_update_caches(x,y,w,h,rect[4]); break;
        }
        case 5:
            renderer_flush_queues(); ppm(argv[2],data);
#ifdef PICA
            { static unsigned previous;
              static unsigned previous_setups;
              fprintf(stderr,"frame %d draw calls %u dither setups %u\n",*(int*)data,sim_c3d_draws-previous,sim_dither_setups-previous_setups);
              previous_setups=sim_dither_setups;
              previous=sim_c3d_draws; }
            if(getenv("GPU_DUMP_VRAM")) {
                FILE *dump=fopen(getenv("GPU_DUMP_VRAM"),"wb"); assert(dump);
                for(int y=0;y<512;y++)for(int x=0;x<1024;x++) { u16 pixel=sim_target_pixel(x,y); fwrite(&pixel,2,1,dump); }
                assert(!fclose(dump));
            }
            if(getenv("GPU_HASH_VRAM")) {
                // Include offscreen pixels and mask bits: a scanout RGB PNG
                // alone cannot detect reordering that corrupts later masks.
                unsigned long long hash=1469598103934665603ull;
                for(int y=0;y<512;y++)for(int x=0;x<1024;x++) {
                    u16 pixel=sim_target_pixel(x,y);
                    hash=(hash^(pixel&255))*1099511628211ull;
                    hash=(hash^(pixel>>8))*1099511628211ull;
                }
                fprintf(stderr,"frame %d VRAM hash %016llx\n",*(int*)data,hash);
            }
            { int *info=data;
              gpu.screen.src_x=info[2]; gpu.screen.src_y=info[3];
              gpu.screen.w=info[4]; gpu.screen.h=info[5]; }
            if(getenv("PICA_STATS")) { char stats[2048]; pica_stats(stats,sizeof(stats)); fprintf(stderr,"stats %d %s\n",*(int*)data,stats); }
            C3D_FrameEnd(0); pica_frame_ended(); pica_frame_started();
#endif
            break;
        case 8: {
            assert(bytes==16); unsigned *p=data;
            int sx=p[1]&1023,sy=(p[1]>>16)&511,dx=p[2]&1023,dy=(p[2]>>16)&511;
            int w=((p[3]-1)&1023)+1,h=(((p[3]>>16)-1)&511)+1;
            fprintf(stderr,"VRAM copy %d %d %d %d %d %d\n",sx,sy,dx,dy,w,h);
#ifdef PICA
            copied=pica_vram_copy(sx,sy,dx,dy,w,h,ex[6]&3);
#else
            (void)sx;(void)sy;(void)dx;(void)dy;(void)w;(void)h;
#endif
            break;
        }
        case 9: // Synthetic state-replacement barrier, before the VRAM upload.
            assert(bytes==0);
#ifdef PICA
            { void pica_drop_fills(void); pica_drop_fills(); }
#endif
            break;
        case 7: // Synthetic option-change fixtures; captures do not emit this.
            assert(bytes==4); cbs.dithering=*(uint32_t*)data;
            renderer_set_config(&cbs); break;
        case 6:
#if !defined(PICA) && !defined(GPU_COMPARE_ALT_REFERENCE)
            assert(bytes==1024*512*2);
            if(cbs.dithering==1 && memcmp(data,vram,bytes)) {
                unsigned different=0;
                for(unsigned k=0;k<bytes/2;k++)different+=vram[k]!=((uint16_t*)data)[k];
                fprintf(stderr,"reference replay mismatch: %u VRAM pixels\n",different); return 3;
            }
#endif
            break;
        default: fprintf(stderr,"unknown record %u\n",tag); return 2;
        }
        free(data);
    }
    fclose(f);
#ifdef PICA
    C3D_FrameEnd(0); pica_frame_ended(); pica_exit();
#endif
    #ifdef PICA_SIM_HASH
    extern unsigned long long pica_sim_hash;
    fprintf(stderr,"draw hash %016llx\n",pica_sim_hash);
#endif
    renderer_finish();
    return 0;
}
