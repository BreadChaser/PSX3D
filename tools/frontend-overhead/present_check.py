#!/usr/bin/env python3
"""Exercise the real presentation/hold bodies with a call-counting SDK fixture."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2];source=(root/'psx3d/source/video.c').read_text()
present=source[source.index('void video_present_screens('):source.index('// Menus and legacy callers')]
hold=source[source.index('static void hold_on_gpu_thread('):source.index('// UI screens (browser, menus)')]
fixture=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef uint32_t u32; typedef uint64_t u64;
typedef struct {int id;} C3D_RenderTarget;
typedef struct {void *data;} C3D_Tex;
typedef struct {C3D_Tex *tex; void *subtex;} C2D_Image;
#define TEX_H 512
#define TEX_W 1024
#define TRANSFER_FLAGS 0
#define GPU_SCISSOR_DISABLE 0
#define GPU_ALWAYS 0
#define GPU_BLEND_ADD 0
#define GPU_SRC_ALPHA 0
#define GPU_ONE_MINUS_SRC_ALPHA 0
#define GFX_LEFT 0
#define GFX_RIGHT 1
#define MAX_DEPTH_PX 4
#define MAX_REAL_PX 4
#define GX_BUFFER_DIM(x,y) 0
#define C2D_Color32(r,g,b,a) 0
static struct {int stereo;} g_cfg;
static bool gpu=true, gpu_frame, sw_gpu_async, stereo_on, ui_wants_3d;
static bool plain_ok, plain_now, in_menu_call;
static unsigned frame_h=240; static float ui_parallax, slider;
static int ui_eye; static C3D_Tex tex; static u32 *pending;
static u64 frame_end_tick; static float gpu_ms;
static C3D_RenderTarget targets[3]={{0},{1},{2}},*top=&targets[0],*top_r=&targets[1],*bottom=&targets[2];
static unsigned begins,ends,top_clears,bottom_clears,top_scenes,bottom_scenes,transfers,draws,top_ui,bottom_ui;
static bool renderer_is_gpu(void){return gpu;}
static int (*pica_thread_call)(void(*fn)(void*),void*d);
struct present_request {bool show_game,repaint_top;void(*draw_top)(void*),(*draw_bottom)(void*);void*ud;};
static void present_on_gpu_thread(void*d){(void)d;assert(0);}
static void video_gpu_frame_begin(void){if(!gpu_frame){begins++;gpu_frame=true;}}
static void video_gpu_wait_previous(void){}
static float C3D_GetDrawingTime(void){return 0;}
static void pica_prepare_present(void){}
static void C2D_Prepare(void){}
static void C3D_SetScissor(int a,int b,int c,int d,int e){(void)a;(void)b;(void)c;(void)d;(void)e;}
static void C3D_AlphaTest(bool a,int b,int c){(void)a;(void)b;(void)c;}
static void C3D_AlphaBlend(int a,int b,int c,int d,int e,int f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;}
static void pica_state_lost(void){}
static void (*video_pre_draw)(void);
static void GX_DisplayTransfer(u32*a,int b,u32*c,int d,int e){(void)a;(void)b;(void)c;(void)d;(void)e;transfers++;}
static float slider_get(void){return slider;}
static void gfxSet3D(bool a){(void)a;}
static void top_set_plain(C3D_RenderTarget*t,int e,bool p){(void)t;(void)e;(void)p;}
static void C2D_TargetClear(C3D_RenderTarget*t,u32 c){(void)c;if(t==bottom)bottom_clears++;else top_clears++;}
static void top_begin(C3D_RenderTarget*t){assert(t!=bottom);top_scenes++;}
static void draw_game(float d){(void)d;draws++;}
static void C2D_SceneBegin(C3D_RenderTarget*t){assert(t==bottom);bottom_scenes++;}
static void C3D_FrameEnd(int a){(void)a;ends++;}
static u64 svcGetSystemTick(void){return 0;}
static void pica_frame_ended(void){}
static void video_hold(void);
static void draw_top(void*d){(void)d;top_ui++;}
static void draw_bottom(void*d){(void)d;bottom_ui++;}
'''
main=r'''
int main(void){
 video_hold(); assert(begins==0 && ends==0 && top_scenes==0 && transfers==0);
 // Dirty bottom alone must not touch the top, even if an upload is pending.
 // The product caller promotes pending uploads to a top repaint separately.
 pending=(u32*)1;
 video_present_screens(true,false,NULL,draw_bottom,NULL);
 assert(begins==1 && ends==1 && top_scenes==0 && top_clears==0);
 assert(bottom_scenes==1 && bottom_ui==1 && transfers==0 && pending);
 video_present_screens(true,true,NULL,NULL,NULL);
 assert(begins==2 && ends==2 && top_scenes==1 && top_clears==1 && transfers==1 && !pending);
 assert(bottom_scenes==1 && draws==1);
 gpu_frame=true; video_hold(); assert(begins==2 && ends==3 && top_scenes==1);
 // Slider/stereo repaint still updates both eyes and the overlay.
 g_cfg.stereo=1; slider=1;
 video_present_screens(true,true,draw_top,NULL,NULL);
 assert(top_scenes==3 && top_clears==3 && top_ui==2 && bottom_scenes==1);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(fixture+present+hold+main)
 subprocess.run(['cc','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('actual presentation: idle hold, bottom-only, pending upload, offscreen submit and stereo calls passed (ASan/UBSan)')
