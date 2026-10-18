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
  int n=settings_items(items);
  assert(n>7 && n<=S_COUNT);
  assert(has_item(items,n,S_3D)==model);
  assert(has_item(items,n,S_CLICK_SIDE)==model);
  assert(has_item(items,n,S_RES3D)==software);
  n=menu_items(items,true);
  assert(n>7 && n<=M_COUNT);
  assert(has_item(items,n,M_3D)==model);
  assert(has_item(items,n,M_CLICK_SIDE)==model);
  assert(has_item(items,n,M_RES3D)==software);
  assert(has_item(items,n,M_REWIND)==model);
  int general=settings_find(items,n,M_AUTOSAVE,M_DISPLAY,M_ADVANCED);
  assert(items[general+1]==(model?M_REWIND:M_DEFAULTS));
  if(model) assert(items[general+2]==M_DEFAULTS);
  // Every header has at least one leaf; none is reachable with Up/Down.
  for(int context=0;context<2;context++) {
   int first=context?M_DISPLAY:S_DISPLAY, last=context?M_ADVANCED:S_ADVANCED;
   n=context?menu_items(items,true):settings_items(items);
   int leaves=0;
   for(int i=0;i<n;i++) {
    if(items[i]>=first && items[i]<=last) {
     assert(i+1<n && items[i+1]>last);
     assert(context?menu_sections[items[i+1]]==items[i]-first+1:s_sections[items[i+1]]==items[i]-first+1);
    } else leaves++;
   }
   for(int dir=-1;dir<=1;dir+=2) {
    int start=settings_find(items,n,-1,first,last), sel=start;
    for(int i=0;i<leaves;i++) {
     sel=settings_step(items,n,sel,dir,first,last);
     assert(items[sel]<first || items[sel]>last);
     assert((sel==start)==(i==leaves-1));
    }
   }
  }
 }
 // Rebuilding a list preserves the setting, not its old numeric row index.
 g_new3ds=true; config_reset_defaults(); g_cfg.renderer=1;
 int n=settings_items(items), sel=settings_find(items,n,S_DEFAULTS,S_DISPLAY,S_ADVANCED);
 assert(items[sel]==S_DEFAULTS);
 g_cfg.renderer=0; n=settings_items(items);
 sel=settings_find(items,n,S_DEFAULTS,S_DISPLAY,S_ADVANCED); assert(items[sel]==S_DEFAULTS);
 g_cfg.show_fps=2; n=menu_items(items,true);
 sel=settings_find(items,n,M_FPS,M_DISPLAY,M_ADVANCED); assert(items[sel]==M_FPS);
 sel=settings_find(items,n,M_CLICK_SIDE,M_DISPLAY,M_ADVANCED); assert(items[sel]==M_SCREEN);
 // No side choice when the preset/mappings leave a blank touch screen;
 // FPS brings its mirrored corner back. Detailed uses a fixed full screen.
 g_new3ds=true; config_reset_defaults();
 for(int i=INPUT_PHYSICAL_N;i<INPUT_REMAP_N;i++) input_map_set(i,INPUT_BIND_HIDE);
 input_touch_reset();
 n=menu_items(items,true); assert(!has_item(items,n,M_CLICK_SIDE));
 g_cfg.show_fps=1; n=menu_items(items,true); assert(has_item(items,n,M_CLICK_SIDE));
 g_cfg.show_fps=2; n=menu_items(items,true); assert(!has_item(items,n,M_CLICK_SIDE));
 config_reset_defaults();
 n=menu_items(items,false); assert(n==7 && !has_item(items,n,M_DISC));
 have_disk=true; disk.get_num_images=disc_count;
 n=menu_items(items,false); assert(n==8 && has_item(items,n,M_DISC)); have_disk=false;
 g_cfg.debug=0; n=menu_items(items,true); assert(!has_item(items,n,M_RECORD) && !has_item(items,n,M_TONE));
 g_cfg.debug=1; n=menu_items(items,true); assert(has_item(items,n,M_RECORD) && has_item(items,n,M_TONE));
 // A curated Old profile cannot re-enable rewind, nor its control actions.
 g_new3ds=false; game_presets_disc_log("CD-ROM ID: SCUS94423\n");
 n=menu_items(items,true); assert(!has_item(items,n,M_REWIND));
 assert(!game_capabilities().fast_forward && !game_capabilities().rewind);
 game_presets_reset();
 // Hide software-only settings according to the running renderer, even when
 // the pending renderer has changed; library settings use the chosen default.
 g_new3ds=true; config_reset_defaults(); host_gpu=true;
 config_game_begin("settings-check"); config_set_renderer(1);
 n=menu_items(items,true); assert(!has_item(items,n,M_RES3D));
 config_set_renderer(0); host_gpu=false;
 n=menu_items(items,true); assert(has_item(items,n,M_RES3D));
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
static bool rewind_text(const char *text)
{
 return strstr(text,"rewind") || strstr(text,"Rewind");
}
// A New 3DS SD card may retain rewind preferences and bindings. Keep those
// bytes portable, but never expose or execute them on Old 3DS.
static void old_rewind_checks(void)
{
 clean_input(false); config_reset_defaults();
 write_text(PSX3D_CONFIG,"touch_slots=1\nmap_A=18\nmap_TOUCH_L3=18\nmap_TOUCH_X2=18\nhotkey_rewind=SELECT+L\nrewind=1\n");
 config_load();
 assert(input_map_get(0)==INPUT_REWIND_HOLD && input_map_get(12)==INPUT_REWIND_HOLD);
 mkdir(PSX3D_DIR "/games",0700);
 write_text(PSX3D_DIR "/games/New SD.ini","rewind=1\nmap_B=18\nmap_TOUCH_R3=18\nhotkey_rewind=SELECT+L\n");
 config_game_begin("New SD");
 current_preset=&enabled_fixture; // Even a preset explicitly requesting rewind.
 assert(gamecfg_int("rewind",0)==1 && !game_capabilities().rewind);
 assert(input_map_get(1)==INPUT_REWIND_HOLD && input_map_get(13)==INPUT_REWIND_HOLD);
 assert(!touch_map_enabled(INPUT_REWIND_HOLD));
 assert(!touch_button_visible(2) && !touch_button_visible(3) && !touch_button_visible(5));
 for(int row=0;row<input_controls_count();row++) {
  assert(control_item(row)!=INPUT_REMAP_N+2 && control_item(row)!=INPUT_PHYSICAL_N+5);
  assert(!rewind_text(input_control_label(row,NULL)));
  assert(!rewind_text(input_control_value(row,NULL)));
 }
 assert(!rewind_text(input_controls_help()));
 int items[M_COUNT], n=menu_items(items,true); assert(!has_item(items,n,M_REWIND));
 input_gameplay(true);
 poll(KEY_SELECT|KEY_L,0,0); assert(!input_rewind() && button(2) && button(10));
 poll(0,0,0); poll(KEY_A|KEY_B,0,0); assert(!input_rewind());
 poll(0,0,0); input_gameplay(false);
 // Every slot, both cycling directions, including a forbidden loaded value.
 for(int slot=0;slot<INPUT_REMAP_N;slot++) for(int dir=-1;dir<=1;dir+=2) {
  input_map_set(slot,INPUT_REWIND_HOLD);
  for(int step=0;step<2*N_PS1;step++) {
   input_map_cycle(slot,dir); assert(input_map_get(slot)!=INPUT_REWIND_HOLD);
  }
 }
 config_game_end(); config_reset_defaults(); clean_input(false);
 puts("PASS: Old 3DS copied global/game rewind=1, preset, bindings, hotkey, touch, help, and remap cycles");
}
int main(void)
{
 assert(!mkdir("sdmc:",0700)); assert(!mkdir("sdmc:/3ds",0700)); assert(!mkdir(PSX3D_DIR,0700));
 assert(!FT_Init_FreeType(&ft));
 assert(!FT_New_Face(ft,getenv("PSX3D_TITLE_FONT"),0,&title_face));
 assert(!FT_New_Face(ft,getenv("PSX3D_BODY_FONT"),0,&body_face));
 assert(!FT_Set_Pixel_Sizes(title_face,0,30)); assert(!FT_Set_Pixel_Sizes(body_face,0,30));
 config_load(); visibility(); old_rewind_checks();
 menu_t navigation={.sel=5}; menu_open_page(&navigation,true);
 assert(navigation.main_sel==5 && navigation.sel==1 && navigation.items[0]==M_DISPLAY && navigation.nitems>7);
 // Headers and the footer cannot be selected by touch, at every scroll position.
 for(int top=0;top<navigation.nitems;top++) {
  navigation.fl.scroll=top;
  for(int row=0;row<7;row++) {
   int hit=menu_row_at(&navigation,40+row*24+12), index=top+row;
   if(index>=navigation.nitems || menu_header(index,&navigation)) assert(hit==-1);
   else assert(hit==index);
  }
  assert(menu_row_at(&navigation,39)==-1 && menu_row_at(&navigation,222)==-1);
 }
 int only_headers[]={S_DISPLAY,S_GENERAL};
 assert(settings_step(only_headers,2,0,1,S_DISPLAY,S_ADVANCED)==-1);
 assert(settings_step(only_headers,0,0,1,S_DISPLAY,S_ADVANCED)==-1);
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
  // These are scroll positions within ONE list, not category child pages.
  static const char *const names[]={"Settings","Display","Performance","Controls","Sound","General","Advanced"};
  for(int section=0;section<SETTINGS_SECTIONS;section++) {
   settings_t st={0}; st.nitems=settings_items(st.items);
   menu_t m={.settings=true}; m.nitems=menu_items(m.items,true);
   int global_item=S_SCREEN, game_item=M_SCREEN;
   for(int i=S_SCREEN;i<S_COUNT;i++) if(s_sections[i]==section) { global_item=i; break; }
   for(int i=M_SCREEN;i<M_COUNT;i++) if(menu_sections[i]==section) { game_item=i; break; }
   st.sel=settings_find(st.items,st.nitems,global_item,S_DISPLAY,S_ADVANCED);
   m.sel=settings_find(m.items,m.nitems,game_item,M_DISPLAY,M_ADVANCED);
   char name[100]; snprintf(name,sizeof(name),"%s-%s-%s",model?"new":"old",game?"game":"global",names[section]);
   shot(name);
   if(game) { flist_snap(&m.fl,m.sel,m.nitems,7); menu_draw_bottom(&m); check_spacing(m.nitems,m.sel,menu_label,menu_value,&m); }
   else { flist_snap(&st.fl,st.sel,st.nitems,7); settings_bottom(&st); check_spacing(st.nitems,st.sel,s_label,s_value,&st); }
   shot_end();
  }
  settings_t core_st={.core=true};
  menu_t core_m={.settings=true,.core_page=true};
  int count=options_count();
  flist_snap(&core_st.core_fl,0,count,7); flist_snap(&core_m.core_fl,0,count,7);
  char core_name[100]; snprintf(core_name,sizeof(core_name),"%s-%s-Core",model?"new":"old",game?"game":"global");
  shot(core_name); if(game) menu_draw_bottom(&core_m); else settings_bottom(&core_st); shot_end();
  // Show half-height on Old 3DS too, with Software explicitly selected.
  if(!model) {
   host_gpu=false; g_cfg.renderer=1;
   settings_t st={0}; st.nitems=settings_items(st.items);
   menu_t m={.settings=true}; m.nitems=menu_items(m.items,true);
   st.sel=settings_find(st.items,st.nitems,S_RES3D,S_DISPLAY,S_ADVANCED);
   m.sel=settings_find(m.items,m.nitems,M_RES3D,M_DISPLAY,M_ADVANCED);
   shot(game?"old-game-Software":"old-global-Software");
   if(game) { flist_snap(&m.fl,m.sel,m.nitems,7); menu_draw_bottom(&m); check_spacing(m.nitems,m.sel,menu_label,menu_value,&m); }
   else { flist_snap(&st.fl,st.sel,st.nitems,7); settings_bottom(&st); check_spacing(st.nitems,st.sel,s_label,s_value,&st); }
   shot_end();
  }
  if(game) config_game_end();
 }
 options_clear(); clear_overrides();
 FT_Done_Face(title_face); FT_Done_Face(body_face); FT_Done_FreeType(ft);
 puts("PASS: settings model/renderer/preset visibility, conditional core options and production menu drawing/spacing");
 return 0;
}
