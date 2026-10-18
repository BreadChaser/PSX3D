// Host-side checks for the real stats formatter/cache, without the 3DS SDK.
#define PSX3D_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
typedef uint32_t u32;
typedef struct { char value[128]; } C2D_Text;
typedef void *C2D_TextBuf;
typedef struct { int px, py; } touchPosition;
#define KEY_TOUCH 1
#define C2D_WithColor 0
#define C2D_Color32(r,g,b,a) 0
static struct { int show_fps; } g_cfg;
static bool sw_stats, sw_prof, linked;
static int g_syscore_pct=30, parses, saves, sw_pmu0=-1;
static u32 keys;
static touchPosition touch;
char CdromId[10]="SCUS94900";
static bool log_linked(void) { return linked; }
static const char *options_get(const char *s) {
 if (strstr(s,"frameskip")) return "auto";
 if (strstr(s,"clock")) return "57";
 if (strstr(s,"readahead")) return "256";
 return "enabled";
}
static bool config_widescreen(void) { return false; }
static void config_save(void) { saves++; }
static unsigned audio_occupancy(void) { return 45; }
static size_t linearSpaceFree(void) { return 2048*1024; }
static C2D_TextBuf C2D_TextBufNew(int n) { return (void*)1; }
static void C2D_TextBufClear(C2D_TextBuf b) {}
static void C2D_TextParse(C2D_Text *t, C2D_TextBuf b, const char *s) { snprintf(t->value,sizeof(t->value),"%s",s); parses++; }
static void C2D_TextOptimize(C2D_Text *t) {}
static void C2D_DrawText(C2D_Text *t, int flags, float x, float y, float z, float sx, float sy, u32 color) {}
static void ui_rect(float x,float y,float w,float h,u32 c) {}
static u32 hidKeysDown(void) { return keys; }
static u32 hidKeysHeld(void) { return keys; }
static void hidTouchRead(touchPosition *t) { *t=touch; }
#include "../../psx3d/source/statsview.c"
static void frames(unsigned n) { while(n--) statsview_frame(); }
int main(void) {
 statsview_begin("Crash Bandicoot");
 assert(!statsview_collecting() && parses==0);
 frames(60); assert(emulated==0);
 statsview_cycle(); assert(g_cfg.show_fps==1 && saves==1);
 statsview_sample s={.fps=30,.target=60,.emu=20,.max=28,.present=2,.wait=1,.gpu=.2,.skipped=30,.late=9,.elapsed=1000,
  .audio="audio in=44100/60 calls queue=100..3000 (target 3072) underruns=7 drop=0",
  .renderer="pica: cmdlist 1ms; state 1ms; ends: atlas 1; evictions by size 0 0 0 0; 22 batches 88 prims 17 tile decodes (0 evictions 0 invalidations) 19 uploads 3 syncs 2 readbacks"};
 frames(60); statsview_update(&s); // discard the partial/hidden interval
 frames(60); statsview_update(&s);
 assert(strstr(brief,"100%"));
 assert(strstr(lines[2],"Draw 30  skip 30  late 9"));
 assert(strstr(lines[7],"underruns 7"));
 assert(strstr(lines[10],"19/17  sync/read 3/2"));
 assert(strstr(lines[11],"88/22"));
 assert(statsview_redraw(0)); assert(!statsview_redraw(0));
 int old=parses; frames(60); statsview_update(&s); assert(parses==old); assert(!statsview_redraw(0));
 // Extra emulated frames count toward speed even when not presented (FF).
 frames(120); statsview_update(&s); assert(strstr(brief,"200%"));
 s.target=50; frames(50); statsview_update(&s); assert(strstr(brief,"100%"));
 s.audio="audio off"; frames(50); statsview_update(&s); assert(!strcmp(lines[7],"Audio off"));
 s.target=60; s.audio="underruns=7";
 statsview_cycle(); assert(g_cfg.show_fps==2);
 keys=KEY_TOUCH; touch.py=190; assert(!statsview_touch()); // touch controls/menu retained
 touch.py=222; assert(statsview_touch() && g_cfg.show_fps==0);
 assert(statsview_redraw(0)); keys=0; statsview_redraw(0); assert(!statsview_redraw(0));
 statsview_begin("Crash Bandicoot"); assert(statsview_redraw(0)); assert(!statsview_redraw(0));
 sw_stats=true; assert(statsview_collecting()); sw_stats=false;
 sw_prof=true; assert(statsview_collecting()); sw_prof=false;
 linked=true; assert(statsview_collecting()); linked=false;
 // Write real formatted rows for the offline mockup.
 g_cfg.show_fps=2; frames(60); statsview_update(&s); frames(60); statsview_update(&s);
 for(int i=0;i<ROWS;i++) puts(lines[i]);
 puts("checks passed");
}
