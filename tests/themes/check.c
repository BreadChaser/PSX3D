// Compile the production scene body with a counting GPU substitute. This checks
// bounds/finite geometry and lifetime; it does not certify PICA output or speed.
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "theme.h"
static struct {int theme;} g_cfg;
static bool g_new3ds, playing, fail_tex, fail_text;
static unsigned textures, texts, draws;
static u64 now;
#define SYSCLOCK_ARM11 268123480ULL
#define CUR_PROCESS_HANDLE 0
#define GPU_A8 0
#define GPU_LINEAR 0
#define GPU_CLAMP_TO_EDGE 0
#define GPU_BLEND_ADD 0
#define GPU_SRC_ALPHA 0
#define GPU_ONE 0
#define GPU_ONE_MINUS_SRC_ALPHA 0
#define C2D_WithColor 0
typedef struct {void *data; unsigned size;} C3D_Tex;
typedef struct {unsigned short width,height;float left,top,right,bottom;} Tex3DS_SubTexture;
typedef struct {int value;} C2D_Text;
typedef void *C2D_TextBuf;
typedef struct {int value;} C2D_ImageTint;
typedef struct {C3D_Tex *tex;Tex3DS_SubTexture *sub;} C2D_Image;
static bool config_in_game(void){return playing;}
static bool dev_enabled(void){return false;}
static u64 svcGetSystemTick(void){return now;}
static void svcStoreProcessDataCache(int p,u32 a,unsigned n){(void)p;(void)a;(void)n;}
static float ui_depth(float x){return x*.75f;}
static void video_gpu_idle(void){}
#define log_printf(...) ((void)0)
#define C2D_Flush() ((void)0)
#define C3D_AlphaBlend(...) ((void)0)
#define C3D_TexSetFilter(...) ((void)0)
#define C3D_TexSetWrap(...) ((void)0)
static bool C3D_TexInit(C3D_Tex *t,unsigned w,unsigned h,int fmt){(void)fmt;if(fail_tex)return false;t->data=malloc(t->size=w*h);assert(t->data);textures++;return true;}
static void C3D_TexDelete(C3D_Tex *t){assert(t->data&&textures);free(t->data);t->data=NULL;textures--;}
static C2D_TextBuf C2D_TextBufNew(unsigned n){if(fail_text)return NULL;texts++;return malloc(n);}
static void C2D_TextBufDelete(C2D_TextBuf b){assert(b&&texts);free(b);texts--;}
#define C2D_TextParse(...) ((void)0)
#define C2D_TextOptimize(...) ((void)0)
#define C2D_PlainImageTint(...) ((void)0)
static void xy(float x,float y,float z){assert(isfinite(x)&&isfinite(y)&&isfinite(z));assert(fabsf(x)<100000&&fabsf(y)<100000);}
static void C2D_DrawTriangle(float x,float y,u32 a,float x1,float y1,u32 b,float x2,float y2,u32 c,float z){(void)a;(void)b;(void)c;xy(x,y,z);xy(x1,y1,z);xy(x2,y2,z);draws++;}
static void C2D_DrawRectSolid(float x,float y,float z,float w,float h,u32 c){(void)c;xy(x,y,z);assert(isfinite(w)&&isfinite(h));draws++;}
static void C2D_DrawRectangle(float x,float y,float z,float w,float h,u32 a,u32 b,u32 c,u32 d){(void)b;(void)c;(void)d;C2D_DrawRectSolid(x,y,z,w,h,a);}
static void C2D_DrawCircleSolid(float x,float y,float z,float r,u32 c){(void)c;xy(x,y,z);assert(isfinite(r)&&r>=0);draws++;}
static void C2D_DrawImageAt(C2D_Image im,float x,float y,float z,C2D_ImageTint *t,float sx,float sy){(void)t;assert(im.tex->data);xy(x,y,z);assert(isfinite(sx)&&isfinite(sy)&&sx>0&&sy>0);draws++;}
static void C2D_DrawText(C2D_Text *t,int f,float x,float y,float z,float sx,float sy,u32 c){(void)t;(void)f;(void)c;xy(x,y,z);assert(isfinite(sx)&&isfinite(sy));draws++;}
#include "theme-body.c"
#include "scene-body.c"
int main(void){
 // In-game paths cannot allocate any scene resources, even before first use.
 playing=true;scene_draw(false);scene_draw(true);assert(!ws&&!textures&&!texts);
 playing=false;fail_tex=true;scene_draw(false);assert(!ws&&!textures&&!texts);fail_tex=false;
 fail_text=true;scene_draw(true);assert(!ws&&!textures&&!texts);fail_text=false;
 unsigned peak=0;
 for(int model=0;model<2;model++)for(int th=0;th<theme_count();th++){
  g_new3ds=model;g_cfg.theme=th;
  for(int frame=0;frame<240;frame++){
   now=(u64)(frame*15.17)*SYSCLOCK_ARM11; // includes the monotonic-hour wrap
   draws=0;scene_draw(false);scene_draw(true);
   assert(ws&&textures==1&&texts==1);assert(draws<16384);if(draws>peak)peak=draws;
  }
  scene_release();scene_release();assert(!ws&&!textures&&!texts);
  playing=true;scene_draw(false);scene_draw(true);assert(!ws&&!textures&&!texts);playing=false;
 }
 printf("scene bounds/lifetime: 8 themes x 2 models x 240 times, peak %u draw objects for both screens; PASS\n",peak);
}
