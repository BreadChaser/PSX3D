// Reuse the existing production-drawing host renderer and config/input stubs.
#define main bottom_screen_checks
#include "../../psx3d/tests/host/check.c"
#undef main
#include <time.h>
#include "settings_menu.h"
static bool have_disk;
static unsigned menu_disc;
static struct retro_disk_control_callback disk;
static const char *game_title = "Example game";
static void draw_top(void *ud) { (void)ud; }
bool rewind_active(void) { return false; }
int cheats_count(void) { return 0; }
const char *cheats_desc(int i) { (void)i; return ""; }
bool cheats_on(int i) { (void)i; return false; }
bool cheats_forced(int i) { (void)i; return false; }
bool cheats_widescreen(bool on) { return on; }
// These functions and tables are extracted verbatim from production sources.
#include "settings_extract.h"
#define HAVE_NO_LANGEXTRA
#define GPU_UNAI
#define GPU_PICA
#define _3DS
#define USE_ASYNC_CDROM
#include "../../core/frontend/libretro_core_options.h"
static void register_core_options(void)
{
 struct retro_core_option_definition defs[MAX_OPTIONS] = {0};
 int i;
 for(i=0; option_defs_us[i].key && i<MAX_OPTIONS-1; i++) {
  defs[i].key=option_defs_us[i].key; defs[i].desc=option_defs_us[i].desc;
  defs[i].info=option_defs_us[i].info; defs[i].default_value=option_defs_us[i].default_value;
  memcpy(defs[i].values,option_defs_us[i].values,sizeof(defs[i].values));
 }
 options_register_v1(defs);
}

static bool has_item(const int *items, int n, int item)
{
 for (int i=0;i<n;i++) if(items[i]==item) return true;
 return false;
}
static void check_spacing(int n, int sel, const char *(*label)(int,void*),
 const char *(*value)(int,void*), void *ud)
{
 for(int i=0;i<n;i++) {
  C2D_Text t; float lw,vw;
  C2D_TextFontParse(&t,NULL,NULL,label(i,ud)); C2D_TextGetDimensions(&t,.5,.5,&lw,NULL);
  const char *v=value(i,ud);
  C2D_TextFontParse(&t,NULL,NULL,v?v:""); C2D_TextGetDimensions(&t,.45,.45,&vw,NULL);
  // 20px selected left inset, 8px right inset, 8px minimum gap.
  if(lw+vw>284) fprintf(stderr,"overlapping row: %s / %s (%g)\n",label(i,ud),v,lw+vw);
  assert(lw+vw <= 284);
 }
 (void)sel;
}
static int option_index(const char *key)
{
 for(int i=0;i<options_count();i++) if(!strcmp(options_key(i),key)) return i;
 return -1;
}
static unsigned disc_count(void) { return 2; }
static void visibility(void)
{
 int items[M_COUNT];
 for(int model=0;model<2;model++) for(int software=0;software<2;software++) {
  g_new3ds=model; host_gpu=!software; config_reset_defaults(); g_cfg.renderer=software;
  for(int page=0;page<SETTINGS_PAGES;page++) {
   int n=settings_items(items,page);
   assert(n>0 && n<=7);
   if(page==SETTINGS_DISPLAY) assert(has_item(items,n,S_3D)==model);
   if(page==SETTINGS_CONTROLS) assert(has_item(items,n,S_CLICK_SIDE)==model);
   if(page==SETTINGS_PERFORMANCE) assert(has_item(items,n,S_RES3D)==software);
   n=menu_items(items,page);
   assert(n>0 && n<=7);
   if(page==SETTINGS_DISPLAY) assert(has_item(items,n,M_3D)==model);
   if(page==SETTINGS_CONTROLS) assert(has_item(items,n,M_CLICK_SIDE)==model);
   if(page==SETTINGS_PERFORMANCE) {
    assert(has_item(items,n,M_RES3D)==software);
    assert(has_item(items,n,M_REWIND)==model);
   }
  }
 }
 // No side choice when the preset/mappings leave a blank touch screen;
 // FPS brings its mirrored corner back. Detailed uses a fixed full screen.
 g_new3ds=true; config_reset_defaults();
 for(int i=INPUT_PHYSICAL_N;i<INPUT_REMAP_N;i++) input_map_set(i,INPUT_BIND_HIDE);
 input_touch_reset();
 int n=menu_items(items,SETTINGS_CONTROLS); assert(!has_item(items,n,M_CLICK_SIDE));
 g_cfg.show_fps=1; n=menu_items(items,SETTINGS_CONTROLS); assert(has_item(items,n,M_CLICK_SIDE));
 g_cfg.show_fps=2; n=menu_items(items,SETTINGS_CONTROLS); assert(!has_item(items,n,M_CLICK_SIDE));
 config_reset_defaults();
 n=menu_items(items,-1); assert(n==7 && !has_item(items,n,M_DISC));
 have_disk=true; disk.get_num_images=disc_count;
 n=menu_items(items,-1); assert(n==8 && has_item(items,n,M_DISC)); have_disk=false;
 g_cfg.debug=0; n=menu_items(items,SETTINGS_ADVANCED); assert(n==1);
 g_cfg.debug=1; n=menu_items(items,SETTINGS_ADVANCED); assert(n==3);
 // A curated Old profile cannot re-enable rewind, nor its control actions.
 g_new3ds=false; game_presets_disc_log("CD-ROM ID: SCUS94423\n");
 n=menu_items(items,SETTINGS_PERFORMANCE); assert(!has_item(items,n,M_REWIND));
 assert(!game_capabilities().fast_forward && !game_capabilities().rewind);
 game_presets_reset();
 // Hide software-only settings according to the running renderer, even when
 // the pending renderer has changed; library settings use the chosen default.
 g_new3ds=true; config_reset_defaults(); host_gpu=true;
 config_game_begin("settings-check"); config_set_renderer(1);
 n=menu_items(items,SETTINGS_PERFORMANCE); assert(!has_item(items,n,M_RES3D));
 config_set_renderer(0); host_gpu=false;
 n=menu_items(items,SETTINGS_PERFORMANCE); assert(has_item(items,n,M_RES3D));
 config_game_end();
 struct retro_variable vars[] = {
  {"pcsx_rearmed_frameskip_type","Skip; disabled|auto|auto_threshold|fixed_interval"},
  {"pcsx_rearmed_frameskip_threshold","Limit; 33|50"},
  {"pcsx_rearmed_frameskip_interval","Frames; 1|2"},
  {"pcsx_rearmed_gpu_unai_fast_lighting","Light; disabled|enabled"},
  {"pcsx_rearmed_screen_centering","Position; auto|manual"},
  {"pcsx_rearmed_screen_centering_x","X; 0|2"},
  {"pcsx_rearmed_psx3d_stereo","Stereo; disabled|enabled"},
  {"pcsx_rearmed_psx3d_widescreen","Wide; 0|52429"},
  {"psx3d_renderer","Renderer; Software|GPU (experimental)"},
  {NULL,NULL}
 };
 options_register(vars); g_cfg.renderer=0;
 assert(option_index("pcsx_rearmed_gpu_unai_fast_lighting")==-1);
 assert(option_index("pcsx_rearmed_screen_centering")==-1);
 assert(option_index("pcsx_rearmed_psx3d_stereo")==-1);
 assert(option_index("pcsx_rearmed_psx3d_widescreen")==-1);
 assert(option_index("psx3d_renderer")==-1);
 g_cfg.renderer=1;
 assert(option_index("pcsx_rearmed_gpu_unai_fast_lighting")>=0);
 assert(option_index("pcsx_rearmed_screen_centering")>=0);
 assert(option_index("pcsx_rearmed_screen_centering_x")==-1);
 assert(option_index("pcsx_rearmed_frameskip_threshold")==-1);
 assert(option_index("pcsx_rearmed_frameskip_interval")==-1);
 config_set_core_override("pcsx_rearmed_frameskip_type","auto_threshold");
 assert(option_index("pcsx_rearmed_frameskip_threshold")>=0);
 assert(option_index("pcsx_rearmed_frameskip_interval")>=0);
 config_set_core_override("pcsx_rearmed_screen_centering","manual");
 assert(option_index("pcsx_rearmed_screen_centering_x")>=0);
}
int main(void)
{
 assert(!mkdir("sdmc:",0700)); assert(!mkdir("sdmc:/3ds",0700)); assert(!mkdir(PSX3D_DIR,0700));
 assert(!FT_Init_FreeType(&ft));
 assert(!FT_New_Face(ft,getenv("PSX3D_TITLE_FONT"),0,&title_face));
 assert(!FT_New_Face(ft,getenv("PSX3D_BODY_FONT"),0,&body_face));
 assert(!FT_Set_Pixel_Sizes(title_face,0,30)); assert(!FT_Set_Pixel_Sizes(body_face,0,30));
 config_load(); visibility();
 menu_t navigation={.sel=5}; menu_open_page(&navigation,true);
 assert(navigation.main_sel==5 && navigation.sel==0 && navigation.nitems==6);
 menu_open_page(&navigation,false); assert(navigation.sel==5 && !navigation.settings);
 register_core_options();
 // Record the exact normal Core options inventory for each console/renderer.
 char inventory_path[1024]; snprintf(inventory_path,sizeof(inventory_path),"%s/core-inventory.tsv",getenv("PSX3D_MOCKUPS"));
 FILE *inventory=fopen(inventory_path,"w"); assert(inventory);
 for(int model=0;model<2;model++) for(int software=0;software<2;software++) {
  g_new3ds=model; config_reset_defaults(); g_cfg.renderer=software;
  int n=options_count();
  for(int i=0;i<n;i++) {
   assert(strcmp(options_desc(i),opts[visible[i]].desc)); // every visible core option has a plain name
   option_t *o=&opts[visible[i]];
   C2D_Text t; float lw,vw;
   C2D_TextFontParse(&t,NULL,NULL,options_desc(i)); C2D_TextGetDimensions(&t,.5,.5,&lw,NULL);
   for(int j=0;j<o->nvalues;j++) {
    const char *saved=config_core_override(o->key);
    char *saved_copy=saved?strdup(saved):NULL;
    config_set_core_override(o->key,o->values[j]);
    C2D_TextFontParse(&t,NULL,NULL,options_value(i)); C2D_TextGetDimensions(&t,.45,.45,&vw,NULL);
    assert(lw+vw<=284);
    if(saved_copy) { config_set_core_override(o->key,saved_copy); free(saved_copy); }
    else { // remove only this temporary global override, retaining defaults
     for(int k=0;k<novr;k++) if(!strcmp(ovr[k].key,o->key)) {
      free(ovr[k].key); free(ovr[k].value); memmove(&ovr[k],&ovr[k+1],(novr-k-1)*sizeof(ovr[0])); novr--; break;
     }
    }
   }
   fprintf(inventory,"%s\t%s\t%s\t%s\t%s\n",model?"New":"Old",software?"Software":"GPU",options_key(i),options_desc(i),options_value(i));
  }
 }
 fclose(inventory);

 for(int model=0;model<2;model++) for(int game=0;game<2;game++) {
  g_new3ds=model; config_reset_defaults(); host_gpu=!g_cfg.renderer;
  if(game) config_game_begin("settings-mockup");
  for(int page=0;page<SETTINGS_PAGES;page++) {
   settings_t st={.page=page}; st.nitems=settings_items(st.items,page);
   menu_t m={.settings=true,.group=page}; m.nitems=menu_items(m.items,page);
   char name[100]; snprintf(name,sizeof(name),"%s-%s-%s",model?"new":"old",game?"game":"global",settings_page_names[page]);
   shot(name);
   if(game) { flist_snap(&m.fl,0,m.nitems,7); menu_draw_bottom(&m); check_spacing(m.nitems,0,menu_label,menu_value,&m); }
   else { flist_snap(&st.fl,0,st.nitems,7); settings_bottom(&st); check_spacing(st.nitems,0,s_label,s_value,&st); }
   shot_end();
  }
  // A Core options sample uses the actual compiled core's option definitions.
  settings_t core_st={.page=SETTINGS_ADVANCED,.core=true};
  menu_t core_m={.settings=true,.group=SETTINGS_ADVANCED,.core_page=true};
  int count=options_count();
  flist_snap(&core_st.core_fl,0,count,7); flist_snap(&core_m.core_fl,0,count,7);
  char core_name[100]; snprintf(core_name,sizeof(core_name),"%s-%s-Core",model?"new":"old",game?"game":"global");
  shot(core_name); if(game) menu_draw_bottom(&core_m); else settings_bottom(&core_st); shot_end();
  // Show half-height on Old 3DS too, with Software explicitly selected.
  if(!model) {
   host_gpu=false; g_cfg.renderer=1;
   settings_t st={.page=SETTINGS_PERFORMANCE}; st.nitems=settings_items(st.items,st.page);
   menu_t m={.settings=true,.group=SETTINGS_PERFORMANCE}; m.nitems=menu_items(m.items,m.group);
   shot(game?"old-game-Software":"old-global-Software");
   if(game) { flist_snap(&m.fl,0,m.nitems,7); menu_draw_bottom(&m); check_spacing(m.nitems,0,menu_label,menu_value,&m); }
   else { flist_snap(&st.fl,0,st.nitems,7); settings_bottom(&st); check_spacing(st.nitems,0,s_label,s_value,&st); }
   shot_end();
  }
  if(game) config_game_end();
 }
 options_clear(); clear_overrides();
 FT_Done_Face(title_face); FT_Done_Face(body_face); FT_Done_FreeType(ft);
 puts("PASS: settings model/renderer/preset visibility, conditional core options and production menu drawing/spacing");
 return 0;
}
