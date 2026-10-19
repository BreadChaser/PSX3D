// Compile the production C files against fake HID/graphics, without a console.
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include "../../source/psx3d.h"
#include "../../source/presets.c"
#include "../../source/input.c"
#include "../../source/config.c"
#include "../../source/gamecfg.c"
#include "../../source/options.c"
#include "../../source/ui.c"
#include "../../source/statsview.c"
bool sw_stats, sw_prof;
int sw_pmu0=-1;
char CdromId[10];
bool log_linked(void) { return false; }
bool dev_enabled(void) { return false; }
#ifndef SETTINGS_NAV_TEST
bool settings_ui_active(void) { return false; }
bool settings_ui_debug_capture(void) { return false; }
u32 dev_ui_keys_take(void) { return 0; }
#endif
bool dev_tick(bool playing) { (void)playing;return false; }
int dev_ui_pending(void) { return 0; }
unsigned audio_occupancy(void) { return 50; }
size_t linearSpaceFree(void) { return 1024*1024; }

bool g_new3ds, sw_o3ds, sw_nosputhread, sw_nogputhread, sw_noskip, sw_fs1, sw_fs3;
char sw_cdra[8], sw_clock[8], sw_opt[8][48];
bool renderer_is_gpu(void) { return true; }
int g_syscore_pct, ui_eye;
float ui_parallax;
static u32 fake_held, fake_down;
static touchPosition fake_touch;
static circlePosition fake_cpad, fake_cstick;
static FILE *svg;
static int drawn_glyphs;
static bool capture_bounds;
static float ink_left, ink_top, ink_right, ink_bottom;
static void ink_bounds(float x, float y, float x2, float y2)
{
 if(!capture_bounds)return;
 ink_left=fminf(ink_left,fminf(x,x2)); ink_right=fmaxf(ink_right,fmaxf(x,x2));
 ink_top=fminf(ink_top,fminf(y,y2)); ink_bottom=fmaxf(ink_bottom,fmaxf(y,y2));
}
static int confirm_step;
static bool confirm_accept;

void log_printf(const char *fmt, ...) { (void)fmt; }
C2D_Font libui_font(void) { return (C2D_Font)1; }
void hidScanInput(void) {}
u32 hidKeysHeld(void) { return fake_held; }
u32 hidKeysDown(void) { return fake_down; }
void hidCircleRead(circlePosition *p) { *p = fake_cpad; }
void irrstCstickRead(circlePosition *p) { *p = fake_cstick; }
void hidTouchRead(touchPosition *p) { *p = fake_touch; }
bool aptMainLoop(void)
{
 // Test a held A on entry, release, then a fresh A or B. No implicit accept.
 fake_down = 0;
 if (confirm_step == 0) fake_held = KEY_A;
 else if (confirm_step == 1) fake_held = 0;
 else fake_down = fake_held = confirm_accept ? KEY_A : KEY_B;
 return confirm_step++ < 4;
}
void video_present_vsync(bool game, void (*top)(void *), void (*bottom)(void *), void *ud)
{
 (void)game; (void)top;
 assert(confirm_step < 3); // held A must never confirm on the first iteration
 bottom(ud);
}
C2D_TextBuf C2D_TextBufNew(unsigned n) { (void)n; return NULL; }
void C2D_TextBufDelete(C2D_TextBuf b) { (void)b; }
void C2D_TextBufClear(C2D_TextBuf b) { (void)b; }
bool C2D_TextParse(C2D_Text *t, C2D_TextBuf b, const char *s)
{ (void)b; t->font=NULL; snprintf(t->text, sizeof(t->text), "%s", s); return true; }
bool C2D_TextFontParse(C2D_Text *t, C2D_Font f, C2D_TextBuf b, const char *s)
{ bool ok=C2D_TextParse(t,b,s); t->font=f; return ok; }
void C2D_TextOptimize(C2D_Text *t) { (void)t; }
static FT_Library ft;
static FT_Face title_face, body_face;
static FT_Face font_face(C2D_Font f) { return f ? title_face : body_face; }
FINF_s *C2D_FontGetInfo(C2D_Font f)
{
 (void)f; static u8 sheet[512*512/2];
 static TGLP_s tglp={.cellHeight=30,.cellWidth=30,.baselinePos=25,.nRows=16,.nLines=8,
  .sheetWidth=512,.sheetSize=sizeof(sheet),.sheetFmt=GPU_A4,.sheetData=sheet};
 static FINF_s info={&tglp}; static bool ready;
 if(!ready) {
  const char *chars="LR123";
  for(const char*p=chars;*p;p++) {
   assert(!FT_Load_Char(body_face,*p,FT_LOAD_RENDER));
   FT_GlyphSlot glyph=body_face->glyph;
   int ox=(*p%16)*31+1, oy=(*p/16)*31+1;
   for(unsigned row=0;row<glyph->bitmap.rows;row++)
    for(unsigned col=0;col<glyph->bitmap.width;col++) {
     unsigned pos=font_texel(ox+col,oy+25-glyph->bitmap_top+row,512);
     unsigned alpha=(glyph->bitmap.buffer[row*glyph->bitmap.pitch+col]+16)/17;
     sheet[pos/2]|=alpha<<(4*(pos&1));
    }
  }
  ready=true;
 }
 return &info;
}
int C2D_FontGlyphIndexFromCodePoint(C2D_Font f,u32 ch) { (void)f; return ch; }
charWidthInfo_s *C2D_FontGetCharWidthInfo(C2D_Font f,int index)
{
 FT_Face face=font_face(f); assert(!FT_Load_Char(face,index,FT_LOAD_NO_HINTING));
 static charWidthInfo_s cw;
 cw=(charWidthInfo_s){face->glyph->metrics.horiBearingX/64.0f,face->glyph->metrics.width/64.0f,face->glyph->advance.x/64.0f};
 return &cw;
}
void C2D_TextGetDimensions(C2D_Text *t,float sx,float sy,float *w,float *h)
{
 float width=0;
 for(const char *p=t->text;*p;p++) width+=C2D_FontGetCharWidthInfo(t->font,*p)->charWidth;
 if(w)*w=width*sx;
 if(h)*h=30*sy;
}
static unsigned host_rgb(u32 c) { return (c&255)<<16 | (c&0xff00) | (c>>16&255); }
void C2D_DrawRectSolid(float x,float y,float z,float w,float h,u32 c)
{ if(z==0.1f) assert(x==0 && y==0 && w==320 && h==240); ink_bounds(x,y,x+w,y+h); if(svg) fprintf(svg,"<rect x='%g' y='%g' width='%g' height='%g' fill='#%06x'/>\n",x,y,w,h,host_rgb(c)); }
static unsigned gradient_id;
static void svg_gradient(float x,float y,float x2,float y2,u32 a,u32 b)
{
 fprintf(svg,"<defs><linearGradient id='g%u' gradientUnits='userSpaceOnUse' x1='%g' y1='%g' x2='%g' y2='%g'><stop stop-color='#%06x'/><stop offset='1' stop-color='#%06x'/></linearGradient></defs>\n",++gradient_id,x,y,x2,y2,host_rgb(a),host_rgb(b));
}
void C2D_DrawRectangle(float x,float y,float z,float w,float h,u32 a,u32 b,u32 c,u32 d)
{
 (void)z;(void)b;(void)d; if(!svg)return;
 svg_gradient(x,y,x,y+h,a,c);
 fprintf(svg,"<rect x='%g' y='%g' width='%g' height='%g' fill='url(#g%u)'/>\n",x,y,w,h,gradient_id);
}
void C2D_DrawTriangle(float x,float y,u32 a,float x2,float y2,u32 b,float x3,float y3,u32 c,float z)
{
 (void)z; ink_bounds(x,y,x2,y2); ink_bounds(x3,y3,x3,y3); if(!svg)return;
 float top=fminf(y,fminf(y2,y3)), bottom=fmaxf(y,fmaxf(y2,y3));
 u32 ct= y==top?a:y2==top?b:c, cb= y==bottom?a:y2==bottom?b:c;
 svg_gradient(0,top,0,bottom,ct,cb);
 fprintf(svg,"<path d='M %g %g L %g %g L %g %g Z' shape-rendering='crispEdges' fill='url(#g%u)'/>\n",x,y,x2,y2,x3,y3,gradient_id);
}
void C2D_DrawEllipse(float x,float y,float z,float w,float h,u32 a,u32 b,u32 c,u32 d)
{
 (void)z;(void)b;(void)d; if(!svg)return;
 svg_gradient(x,y,x,y+h,a,c);
 fprintf(svg,"<ellipse cx='%g' cy='%g' rx='%g' ry='%g' fill='url(#g%u)'/>\n",x+w/2,y+h/2,w/2,h/2,gradient_id);
}
void C2D_DrawCircleSolid(float x,float y,float z,float r,u32 c)
{ (void)z; ink_bounds(x-r,y-r,x+r,y+r); if(svg) fprintf(svg,"<circle cx='%g' cy='%g' r='%g' fill='#%06x'/>\n",x,y,r,host_rgb(c)); }
void C2D_DrawLine(float x,float y,u32 a,float x2,float y2,u32 b,float width,float z)
{ (void)b;(void)z; ink_bounds(fminf(x,x2)-width/2,fminf(y,y2)-width/2,fmaxf(x,x2)+width/2,fmaxf(y,y2)+width/2); if(svg) fprintf(svg,"<path opacity='%g' d='M %g %g L %g %g' stroke='#%06x' stroke-width='%g'/>\n",(a>>24)/255.0,x,y,x2,y2,host_rgb(a),width); }
static void xml_text(const char *s)
{
 for (; *s; s++) switch(*s) {
 case '&': fputs("&amp;",svg); break;
 case '<': fputs("&lt;",svg); break;
 case '>': fputs("&gt;",svg); break;
 default: fputc(*s,svg);
 }
}
void C2D_DrawText(C2D_Text *t,u32 flags,float x,float y,float z,float sx,float sy,...)
{
 (void)z;
 drawn_glyphs+=strlen(t->text);
 if(capture_bounds) {
  float advance=0;
  for(const char *p=t->text;*p;p++) {
   FT_Face face=font_face(t->font); assert(!FT_Load_Char(face,*p,FT_LOAD_RENDER));
   FT_GlyphSlot glyph=face->glyph;
   ink_bounds(x+advance+glyph->bitmap_left*sx,y-glyph->bitmap_top*sy,
    x+advance+(glyph->bitmap_left+glyph->bitmap.width)*sx,y+(glyph->bitmap.rows-glyph->bitmap_top)*sy);
   advance+=glyph->advance.x/64.0f*sx;
  }
 }
 if (!svg) return;
 if(!t->font && (flags&C2D_AtBaseline) && y!=22)
  assert(sy*30 <= (g_new3ds?16:13)+0.01f); // labels never grow past the default size
 if(t->font && (flags&C2D_AtBaseline) && y==204) {
  charWidthInfo_s *cw=C2D_FontGetCharWidthInfo(t->font,t->text[0]);
  bool left=g_new3ds && g_cfg.touch_click_left;
  if(t->text[0]=='4') assert(fabsf(x+cw->left*sx-(left?246:12))<0.005f);
  if(t->text[0]=='S') assert(fabsf(x+(cw->left+cw->glyphWidth)*sx-(left?308:74))<0.005f);
 }
 if(t->font && (flags&C2D_AtBaseline) && y==214) {
  charWidthInfo_s *cw=C2D_FontGetCharWidthInfo(t->font,t->text[0]);
  bool left=g_new3ds && g_cfg.touch_click_left;
  if(t->text[0]=='P') assert(fabsf(x+cw->left*sx-(left?246:12))<0.005f);
  if(t->text[0]=='D') assert(fabsf(x+(cw->left+cw->glyphWidth)*sx-(left?308:74))<0.005f);
 }
 va_list ap; va_start(ap,sy);
 u32 c=va_arg(ap,u32);
 float width=flags&C2D_WordWrap ? va_arg(ap,double) : 10000;
 va_end(ap);
 fprintf(svg,"<text x='%g' y='%g' font-family='%s' font-size='%g' transform='translate(%g 0) scale(%g 1) translate(%g 0)' fill='#%06x' text-anchor='%s'>",x,flags&C2D_AtBaseline?y:y+sy*25,t->font?"Michroma":"DejaVu Sans",sy*30,x,sx/sy,-x,host_rgb(c),flags&C2D_AlignCenter?"middle":flags&C2D_AlignRight?"end":"start");
 int max=(int)(width/(sx*16)), col=0;
 char copy[2048]; strcpy(copy,t->text);
 for(char *word=strtok(copy," ");word;word=strtok(NULL," ")) {
  if(col && col+1+(int)strlen(word)>max) {
   fprintf(svg,"<tspan x='%g' dy='%g'>",x,sx*38); xml_text(word); fputs("</tspan>",svg); col=strlen(word);
  } else { if(col) fputc(' ',svg); xml_text(word); col+=(col?1:0)+strlen(word); }
 }
 fputs("</text>\n",svg);
}
static void shot(const char *name)
{
 char path[1024]; snprintf(path,sizeof(path),"%s/%s.svg",getenv("PSX3D_MOCKUPS"),name);
 svg=fopen(path,"w"); assert(svg);
 fputs("<svg xmlns='http://www.w3.org/2000/svg' width='640' height='480' viewBox='0 0 320 240'><rect width='320' height='240' fill='black'/>\n",svg);
}
static void shot_end(void) { fputs("</svg>\n",svg); fclose(svg); svg=NULL; }
static void poll(u32 keys,int x,int y)
{
 fake_down=keys&~fake_held; fake_held=keys;
 fake_touch=(touchPosition){x,y}; input_poll();
}
static int right_axis(unsigned id) { return input_state(0,RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_RIGHT,id); }
static int button(unsigned id) { return input_state(0,RETRO_DEVICE_JOYPAD,0,id); }
static void write_text(const char *path,const char *s)
{ FILE *f=fopen(path,"w"); assert(f); assert(fputs(s,f)>=0); assert(!fclose(f)); }
static bool contains(const char *path,const char *needle)
{ char buf[8192];FILE*f=fopen(path,"r");assert(f);size_t n=fread(buf,1,sizeof(buf)-1,f);buf[n]=0;fclose(f);return strstr(buf,needle)!=NULL; }

#define INHERIT_TEST_SLOTS { -2,-2,-2,-2,-2,-2 }
// Fixtures never enter the compiled retail table.
static const game_preset_t l2_fixture=GAME_PRESET_SAME("TEST00001",
 {0,0,0,1,0,0,INHERIT_TEST_SLOTS});
static const game_preset_t blank_fixture=GAME_PRESET_SAME("TEST00002",
 {0,0,0,0,0,0,INHERIT_TEST_SLOTS});
static const game_preset_t enabled_fixture=GAME_PRESET_SAME("TEST00003",
 {1,1,0,1,1,1,INHERIT_TEST_SLOTS});
static void paint(const char *name) { shot(name); touch_buttons_draw(); statsview_draw(); shot_end(); }
static void clean_input(bool model)
{
 input_gameplay(false); g_new3ds=model; game_presets_reset(); input_map_reset();
 g_cfg.touch_stick=-1; g_cfg.touch_click_left=0;
 fake_cpad=fake_cstick=(circlePosition){0}; input_touch_reset(); poll(0,0,0);
}
static void hide_touch(void)
{
 for(int i=10;i<INPUT_REMAP_N;i++) input_map_set(i,-1);
 input_stick_set(0); poll(0,0,0);
}
static void controls_shot(const char *name, int row)
{
 flist_t list={0}; flist_snap(&list,row,input_controls_count(),7);
 shot(name); ui_rect(0,0,320,240,C2D_Color32(6,9,24,255));
 ui_menu_header("CONTROLS","this game");
 flist_draw(&list,input_controls_count(),row,0,40,320,24,7,input_control_label,input_control_value,NULL,0xffe66e28);
 shot_end();
}
static void layout_checks(void)
{
 clean_input(false); current_preset=&l2_fixture;
 assert(input_map_raw(10)==-2 && input_map_get(10)==12 && input_map_get(12)==-1);
 assert(!touch_stick_active() && touch_button_visible(0) && !touch_button_visible(2));
 paint("bottom-l2r2-only"); controls_shot("controls-empty-slots",12);
 // Bind a previously empty slot through the actual Controls callback.
 assert(input_control_slot(12)==12 && !strcmp(input_control_value(12,NULL),"Hide"));
 input_control_cycle(12,-1); assert(input_map_get(12)==INPUT_REWIND_HOLD || input_map_get(12)<16);
 input_map_set(12,RETRO_DEVICE_ID_JOYPAD_X);
 assert(touch_button_visible(2)); paint("bottom-l2r2-triangle");
 controls_shot("controls-bound-slot",12);
 tbutton_t third=touch_button(2); assert(third.x==92 && third.y==123 && third.w==216 && third.h==105);
 poll(KEY_TOUCH,280,180); assert(button(RETRO_DEVICE_ID_JOYPAD_X)); poll(0,0,0);
 input_control_cycle(14,-1); // no hotkeys: row 14 is the stick
 assert(touch_stick_active()); paint("bottom-l2r2-triangle-stick");
 assert(!touch_button_contains(0,190,110));
 poll(0,0,0); poll(KEY_TOUCH,200,120); poll(KEY_TOUCH,231,120); assert(right_axis(0)==32767);
 poll(0,0,0); assert(!right_axis(0));
 // All 15 subsets tile once, with no overlapping targets or dead cut-out.
 current_preset=&blank_fixture; input_stick_set(0);
 for(unsigned mask=1;mask<16;mask++) {
  for(int i=0;i<4;i++) input_map_set(10+i,(mask&(1u<<i)) ? remap_default[10+i] : -1);
  int count=__builtin_popcount(mask), area=0;
  for(int i=0;i<4;i++) if(mask&(1u<<i)) {
   tbutton_t b=touch_button(i); area+=b.w*b.h;
   assert(b.x>=92 && b.y>=12 && b.x+b.w<=308 && b.y+b.h<=228);
   assert(b.w>=105 && b.h>=105);
   for(int x=b.x;x<b.x+b.w;x++) for(int y=b.y;y<b.y+b.h;y++) {
    assert(touch_button_contains(i,x,y));
    for(int j=0;j<4;j++) if(j!=i) assert(!touch_button_contains(j,x,y));
   }
   poll(0,0,0); poll(KEY_TOUCH,b.x+b.w/2,b.y+b.h/2); assert(button(remap_default[10+i]));
  }
  assert(area==(count==1 ? 216*216 : count==2 ? 2*105*216 : count==3 ? 2*105*105+216*105 : 4*105*105));
  poll(0,0,0);
  if(mask==1) paint("bottom-one-button");
  if(mask==3) paint("bottom-two-buttons");
  if(mask==7) paint("bottom-three-buttons");
  if(mask==15) paint("bottom-four-buttons");
 }
 hide_touch(); assert(!touch_buttons_active()); paint("bottom-blank");
 poll(KEY_TOUCH,180,100); assert(menu_touched() && !button(12)); poll(0,0,0);
 // Hide is absent, including when tapped; no disabled-panel geometry remains.
 input_map_set(10,12); input_map_set(10,-1); assert(!touch_button_visible(0));
 clean_input(false); paint("bottom-idle");
 input_touch_disc_log("CD-ROM ID: SCUS94423\n"); paint("bottom-curated-full");
 assert(!game_capabilities().fast_forward && !game_capabilities().rewind);
 input_gameplay(true); poll(KEY_SELECT|KEY_R,0,0);
 assert(!input_fast_forward() && button(2) && button(11)); poll(0,0,0);
 poll(KEY_SELECT|KEY_L,0,0); assert(!input_rewind() && button(2) && button(10));
 poll(0,0,0); input_gameplay(false);
 puts("PASS: empty slot edits, curved stick, all 15 stick-free tilings/targets, stick binding, Hide and blank MENU");
}
static void curved_fill_checks(void)
{
 clean_input(false); current_preset=&blank_fixture; input_stick_set(1);
 // Every binding mask uses disjoint rectangular ownership outside the ring.
 for(unsigned mask=1;mask<16;mask++) {
  for(int i=0;i<4;i++) input_map_set(10+i,(mask&(1u<<i)) ? remap_default[10+i] : -1);
  stick_cutout_draw(); // Exercise cached arc clipping for every binding mask.
  for(int x=92;x<308;x++) for(int y=12;y<228;y++) {
   int owners=0;
   for(int i=0;i<4;i++) {
    tbutton_t b=touch_button(i);
    bool expected=(mask&(1u<<i)) && x>=b.x && x<b.x+b.w && y>=b.y && y<b.y+b.h &&
     (x-200)*(x-200)+(y-120)*(y-120)>=3600;
    assert(touch_button_contains(i,x,y)==expected);
    owners+=touch_button_contains(i,x,y);
   }
   assert(owners<=1);
   if(__builtin_popcount(mask)==1) assert(owners==((x-200)*(x-200)+(y-120)*(y-120)>=3600));
  }
  if(mask==1) { assert(touch_button(0).w==216 && touch_button(0).h==216); paint("stick-one-button"); }
  if(mask==3) { assert(touch_button(0).h==216 && touch_button(1).h==216); paint("stick-two-columns"); }
  if(mask==5) { assert(touch_button(0).w==216 && touch_button(2).w==216); paint("stick-two-rows"); }
  if(mask==9) { assert(touch_button(0).w==216 && touch_button(3).w==216); paint("stick-two-diagonal"); }
 }
 input_map_set(10,12); input_map_set(11,13); input_map_set(12,9); input_map_set(13,-1);
 assert(touch_button(2).w==216 && touch_button(2).h==105);
 paint("bottom-l2r2-triangle-stick");
 poll(0,0,0); poll(KEY_TOUCH,280,180); assert(button(9)); poll(0,0,0);
 assert(!touch_button_contains(2,200,150));
 // Palette identity follows the action in every cluster/side slot and list row.
 for(int model=0;model<2;model++) {
  clean_input(model);
  for(int slot=10;slot<INPUT_REMAP_N;slot++) {
   input_map_set(slot,9); assert(input_map_get(slot)==9);
   for(int row=0;row<input_controls_count();row++) if(input_control_slot(row)==slot)
    assert(input_control_color(row)==ui_input_color(9));
  }
 }
 assert(ui_input_color(9)==C2D_Color32(47,163,107,255));
 const int faces[]={9,8,0,1};
 for(int id=2;id<=18;id++) {
  if(id==8 || id==9) continue;
  for(int face=0;face<4;face++) assert(ui_input_color(id)!=ui_input_color(faces[face]));
 }
 assert(ui_input_color(10)==ui_input_color(12) && ui_input_color(11)==ui_input_color(13));
 assert(ui_input_color(14)==ui_input_color(15) && ui_input_color(12)!=ui_input_color(14));
 assert(ui_input_color(10)==ui_input_color(11) && ui_input_color(16)==ui_input_color(18));
 puts("PASS: all 15 curved merges/targets, full-ring ownership, row/column fills and action palette");
}
static void side_and_new_checks(void)
{
 clean_input(true); hide_touch();
 input_map_set(14,INPUT_FF_TOGGLE); tbutton_t a=touch_button(4);
 assert(a.x==12 && a.y==64 && a.w==56 && a.h==112);
 paint("side-one-button");
 input_gameplay(true); poll(KEY_TOUCH,40,160); assert(input_fast_forward());
 poll(0,0,0); assert(touch_button_lit(4)); paint("side-one-button-lit");
 (void)input_touch_changed(); assert(!input_touch_changed()); input_gameplay(false);
 input_map_set(15,INPUT_REWIND_HOLD); a=touch_button(4); tbutton_t b=touch_button(5);
 assert(a.y==64 && a.h==48 && b.y==128 && b.h==48);
 paint("side-two-buttons");
 input_map_set(14,-1); b=touch_button(5); assert(b.y==64 && b.h==112);
 poll(0,0,0); poll(KEY_TOUCH,40,100); assert(touch_slot_held(5));
 input_map_set(15,-1); assert(!touch_buttons_active());
 for(int left=0;left<2;left++) {
  g_cfg.touch_click_left=left;
  int x0=left ? 12 : 208;
  for(unsigned mask=0;mask<4;mask++) {
   input_map_set(12,(mask&1)?14:-1); input_map_set(13,(mask&2)?15:-1);
   a=touch_button(2); b=touch_button(3);
   if(mask==3) {
    assert(a.x==x0 && a.y==12 && a.w==100 && a.h==102);
    assert(b.x==x0 && b.y==126 && b.w==100 && b.h==102);
    poll(0,0,0); paint(left ? "new3ds-left" : "new3ds-l3r3");
   } else if(mask) {
    tbutton_t only=touch_button(mask==1?2:3);
    assert(only.x==x0 && only.y==12 && only.w==100 && only.h==216);
    poll(0,0,0);
    paint(left ? (mask==1 ? "new3ds-left-remapped" : "new3ds-left-one-r3") :
     (mask==1 ? "new3ds-one-button" : "new3ds-one-r3"));
   }
   // Every column pixel, surrounding edge and the gap has exactly its drawn owner.
   for(int x=x0-1;x<=x0+100;x++) for(int y=11;y<=228;y++) {
    bool in=x>=x0 && x<x0+100;
    bool top=in && (mask&1) && y>=12 && y<(mask==3?114:228);
    bool bottom=in && (mask&2) && y>=(mask==3?126:12) && y<228;
    assert(touch_button_contains(2,x,y)==top);
    assert(touch_button_contains(3,x,y)==bottom);
   }
   if(mask&1) {
    poll(0,0,0); poll(KEY_TOUCH,x0+50,40); assert(button(14));
   }
   if(mask&2) {
    poll(0,0,0); poll(KEY_TOUCH,x0+50,200); assert(button(15));
   }
   poll(0,0,0); assert(touch_buttons_active()==(mask!=0));
  }
  input_map_set(14,16); assert(touch_button(4).x==(left ? 252 : 12));
  assert(touch_button_contains(TB_MENU,left ? 270 : 50,20));
  input_map_set(14,-1);
 }
 clean_input(false); input_stick_set(0); // Old side FF supported, rewind engine cannot run.
 input_map_set(14,-1); assert(!touch_button_visible(4)); input_map_set(14,16);
 assert(touch_button(4).h==112 && !touch_button_visible(5));
 paint("old-side-one-button");
 puts("PASS: one/two/zero side tiling, FF latch/cache, New mirrored edge-column targets and menu, Old rewind engine clamp");
}
static void action_checks(void)
{
 for(int model=0;model<2;model++) {
  clean_input(model); current_preset=&enabled_fixture;
  for(int slot=0;slot<INPUT_REMAP_N;slot++) {
   if((model && slot>=10 && slot<12) || (!model && slot==15)) continue;
   for(int action=16;action<=18;action++) {
    if(!model && action==18) continue;
    input_gameplay(false); input_map_reset(); g_cfg.touch_stick=-1;
    input_hotkey_set(0,0); input_hotkey_set(1,0); input_map_set(slot,action);
    input_touch_reset(); poll(0,0,0); input_gameplay(true);
    tbutton_t b= slot>=10 ? touch_button(slot-10) : (tbutton_t){0};
    // Curved-quarter center lies outside r60; use its visible area centroid.
    int x=slot<10?0:!model && slot<14 ? (slot&1?259:141) : b.x+b.w/2;
    int y=slot<10?0:!model && slot<14 ? (slot>=12?179:61) : b.y+b.h/2;
    u32 keys=slot<10 ? remap_keys[slot] : KEY_TOUCH;
    poll(keys,x,y); assert(input_fast_forward()==(action!=18)); assert(input_rewind()==(action==18));
    poll(keys,x,y); poll(0,0,0); assert(input_fast_forward()==(action==16)); assert(!input_rewind());
    if(action==16) { poll(keys,x,y); assert(!input_fast_forward()); poll(0,0,0); }
   }
  }
 }
 clean_input(true); input_gameplay(true);
 poll(KEY_SELECT|KEY_R,0,0); assert(input_fast_forward() && !joypad_mask());
 poll(0,0,0); poll(KEY_SELECT|KEY_L,0,0); assert(input_rewind() && !input_fast_forward());
 input_gameplay(false); poll(0,0,0); input_map_set(0,16); poll(KEY_A,0,0); input_gameplay(true);
 poll(KEY_A,0,0); assert(!input_fast_forward()); poll(0,0,0); poll(KEY_A,0,0); assert(input_fast_forward());
 current_preset=&blank_fixture; input_poll(); assert(!input_fast_forward());
 poll(0,0,0); input_gameplay(false);
 puts("PASS: every allowed action/slot on both consoles, hotkeys, toggle edges, menu blocking and capability revocation");
}
static void serial_config_checks(void)
{
 clean_input(false); config_reset_defaults(); config_load();
 assert(input_map_raw(10)==-2 && g_cfg.touch_stick==-1);
 assert(contains(PSX3D_CONFIG,"touch_slots=1\n") && contains(PSX3D_CONFIG,"map_TOUCH_L2=-2\n"));
 for(size_t i=0;i<sizeof(game_presets)/sizeof(game_presets[0]);i++) {
  char msg[64]; snprintf(msg,sizeof(msg),"CD-ROM ID: %s\n",game_presets[i].serial);
  input_touch_disc_log(msg);
  assert(current_preset == &game_presets[i] && input_map_raw(10)==-2);
  assert(game_capabilities().fast_forward == (game_presets[i].old3ds.fast_forward != 0));
 }
 input_touch_disc_log("CD-ROM ID: scus_944.23\n"); assert(current_preset);
 input_touch_disc_log("CD-ROM ID: SCUS944230\n"); assert(!current_preset);
 input_touch_disc_log("CD-ROM ID: SLUS99999\n"); assert(game_capabilities().fast_forward);
 // Explicit preset assignments, separate consoles and shorthand inheritance.
 static const game_preset_t assigned={"TEST00004",
  {0,0,0,0,0,0,{12,13,-1,-1,-1,-1}},
  {1,1,0,0,0,0,{-1,-1,9,-1,16,18}}};
 current_preset=&assigned; assert(input_map_get(10)==12 && input_map_get(12)==-1);
 input_map_set(12,9); config_save(); config_load(); assert(input_map_get(12)==9);
 config_game_begin("Dynamic slots"); current_preset=&l2_fixture;
 input_map_set(12,8); input_stick_set(1); input_hotkey_load(0,"off");
 config_controls_save(); config_save(); assert(contains(PSX3D_CONFIG,"map_TOUCH_L3=9\n"));
 config_game_end(); config_game_begin("Dynamic slots"); current_preset=&l2_fixture;
 assert(input_map_get(12)==8 && input_stick_binding()==1 && input_hotkey_get(0)==0);
 assert(config_game_reset()); assert(input_map_get(12)==9 && input_stick_binding()==0);
 assert(!gamecfg_get("map_TOUCH_L3") && !gamecfg_get("touch_stick"));
 config_game_end(); config_controls_defaults(); assert(input_map_raw(12)==-2);
 config_game_begin("Preset reset"); current_preset=&l2_fixture;
 input_map_set(12,9); input_stick_set(1); config_controls_save();
 config_controls_defaults(); assert(input_map_get(12)==-1 && !touch_stick_active());
 assert(current_preset==&l2_fixture); config_game_end();
 // Legacy default-valued maps become inherited, deliberate other remaps survive.
 write_text(PSX3D_CONFIG,"map_TOUCH_L2=12\nmap_TOUCH_L3=9\ntouch_stick=1\n");
 config_load(); assert(input_map_raw(10)==-2 && input_map_raw(12)==9 && g_cfg.touch_stick==-1);
 // The format marker follows the same whitespace rules as other INI keys.
 write_text(PSX3D_CONFIG,"map_TOUCH_L2=12\ntouch_stick=1\n touch_slots = 1 \n");
 config_load(); assert(input_map_raw(10)==12 && g_cfg.touch_stick==1);
 config_reset_defaults();
 write_text(PSX3D_DIR "/games/Legacy off.ini","touch_controls=2\n");
 config_game_begin("Legacy off"); assert(!touch_buttons_active());
 config_controls_save(); assert(!gamecfg_get("touch_controls")); config_game_end();
 config_game_begin("Legacy off"); assert(!touch_buttons_active());
 assert(config_game_reset()); assert(touch_buttons_active()); config_game_end();
 clean_input(true); current_preset=&assigned;
 assert(input_map_get(12)==9 && input_map_get(13)==-1);
 hide_touch(); input_map_set(12,9); input_map_set(13,0); paint("new3ds-symbols");
 input_map_set(12,16); input_map_set(13,18); paint("new3ds-actions"); controls_shot("controls-actions",12);
 config_reset_defaults();
 puts("PASS: per-console presets/serials, raw inheritance, global/game bindings+stick+hotkeys, resets and legacy migration");
}
static void symbol_checks(void)
{
 clean_input(false); input_stick_set(1);
 const int faces[]={9,8,0,1}; for(int i=0;i<4;i++) input_map_set(10+i,faces[i]);
 paint("bottom-symbols"); controls_shot("controls-symbols",12);
 input_map_set(10,4); input_map_set(11,7); input_map_set(12,3); input_map_set(13,2); paint("bottom-navigation");
 for(int model=0;model<2;model++) for(int id=0;id<=18;id++) {
  g_new3ds=model; if(!model && id==18) continue;
  capture_bounds=true; ink_left=ink_top=10000; ink_right=ink_bottom=-10000;
  touch_mark(id,142,61,model?16:13,model?3:2); capture_bounds=false;
  assert(fabsf((ink_left+ink_right)/2-142)<0.5f && fabsf((ink_top+ink_bottom)/2-61)<0.25f);
 }
 puts("PASS: all symbol/shoulder ink centers at both console label sizes");
}
static void stats_input_checks(void)
{
 for(int model=0;model<2;model++) for(int left=0;left<=model;left++) {
  clean_input(model); g_cfg.touch_click_left=left; input_gameplay(true); g_cfg.show_fps=2;
  poll(KEY_TOUCH,200,120); assert(!right_axis(0) && !right_axis(1));
  assert(!menu_touched() && !input_stats_touched());
  for(int i=0;i<INPUT_TOUCH_N;i++) assert(!touch_slot_held(i));
  poll(0,0,0); poll(KEY_TOUCH,150,195); assert(menu_touched());
  poll(0,0,0); poll(KEY_TOUCH,150,223); assert(input_stats_touched());
  assert(!input_stats_touched() && !menu_touched());
  poll(0,0,0); g_cfg.show_fps=1;
  if(model) {
   tbutton_t b=touch_button(3); assert(b.y+b.h==228);
   poll(KEY_TOUCH,b.x+50,227); assert(button(15) && !input_stats_touched());
   poll(0,0,0);
  }
  hide_touch();
  poll(KEY_TOUCH,left ? 270 : 40,210); assert(input_stats_touched() && !menu_touched());
  poll(0,0,0); poll(KEY_TOUCH,150,100); assert(menu_touched());
  poll(0,0,0); g_cfg.show_fps=0; input_gameplay(false);
 }
 puts("PASS: Detailed suppresses hidden controls, MENU remains reachable, stats taps never open blank-layout menus");
}
static void fps_mockups(void)
{
 clean_input(false); g_cfg.show_fps=1;
 strcpy(brief,"40.8 FPS"); strcpy(speed_label,"68% speed");
 paint("fps-old-full");
 clean_input(true); paint("fps-new-stacked");
 g_cfg.touch_click_left=1; paint("fps-new-left");
 clean_input(false); hide_touch(); paint("fps-blank");
 // No slots plus FPS draws MENU/readout, and Off draws nothing.
 assert(!touch_buttons_active());
 g_cfg.show_fps=0;
}
int main(void)
{
 assert(!mkdir("sdmc:",0700)); assert(!mkdir("sdmc:/3ds",0700)); assert(!mkdir(PSX3D_DIR,0700));
 assert(!FT_Init_FreeType(&ft));
 assert(!FT_New_Face(ft,getenv("PSX3D_TITLE_FONT"),0,&title_face));
 assert(!FT_New_Face(ft,getenv("PSX3D_BODY_FONT"),0,&body_face));
 assert(!FT_Set_Pixel_Sizes(title_face,0,30)); assert(!FT_Set_Pixel_Sizes(body_face,0,30));
 g_new3ds=false;
 assert(!strcmp(options_get("pcsx_rearmed_spu_interpolation"),"simple"));
 g_new3ds=true;
 assert(!strcmp(options_get("pcsx_rearmed_spu_interpolation"),"gaussian"));
 config_load(); layout_checks(); curved_fill_checks(); side_and_new_checks(); action_checks(); serial_config_checks(); symbol_checks(); stats_input_checks();
 fps_mockups();
 confirm_accept=false; confirm_step=0; assert(!ui_confirm("RESET?","Cancel test",false));
 confirm_accept=true; confirm_step=0; assert(ui_confirm("RESET?","Accept test",false));
 shot("reset-global"); confirm_t c={"RESET ALL SETTINGS?","Restore this console's defaults and all slot bindings? Game overrides are kept."}; confirm_draw(&c); shot_end();
 shot("reset-game"); c=(confirm_t){"RESET THIS GAME?","Remove this game's settings and control overrides?"}; confirm_draw(&c); shot_end();
 clean_input(false); controls_shot("controls-old-unlisted",14); clean_input(true); controls_shot("controls",14);
 FT_Done_Face(title_face); FT_Done_Face(body_face); FT_Done_FreeType(ft); options_clear(); clear_overrides();
 puts("PASS: confirmation fresh-A/cancel and production drawing mockups"); return 0;
}
