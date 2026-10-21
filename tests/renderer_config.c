// Exercise the production config and option code through the host SDK shim.
#include "psx3d.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool g_new3ds, sw_o3ds, sw_nosputhread, sw_nogputhread, sw_noskip, sw_fs1, sw_fs3;
int g_syscore_pct;
char sw_cdra[8], sw_clock[8], sw_opt[8][48];
void log_printf(const char *fmt, ...) { (void)fmt; }
static int maps[INPUT_REMAP_N], hotkeys[INPUT_HOTKEY_N];

void input_map_reset(void) { memset(maps, 0, sizeof(maps)); }
int input_map_raw(int i) { return maps[i]; }
int input_map_factory(int i) { return 0; }
void input_map_set(int i,int v) { maps[i]=v; }
const char *input_map_key_name(int i) { static char key[16]; snprintf(key,sizeof(key),"test%d",i); return key; }
int input_hotkey_factory(int i) { (void)i; return 0; }
int input_hotkey_get(int i) { return hotkeys[i]; }
void input_hotkey_set(int i,int v) { hotkeys[i]=v; }
void input_hotkey_load(int i,const char *v) { hotkeys[i]=atoi(v); }
const char *input_hotkey_key_name(int i) { return i ? "hotkey_rewind" : "hotkey_ff"; }
const char *input_hotkey_combo_name(int i) { return "off"; }
void input_touch_begin(void) {}
void input_touch_reset(void) {}
void ws_policy_override(bool on) { (void)on; }
bool ws_available(void) { return false; }
int ws_console_policy(void) { return -1; }
bool cheats_widescreen(bool on) { (void)on; return false; }
void cheats_disc_changed(void) {}
bool renderer_is_gpu(void) { return true; }
bool settings_ui_debug_capture(void) { return false; }
const char *input_control_key(int row) { (void)row;return NULL; }

static void file(const char *path, const char *text)
{
	FILE *f = fopen(path, "w");
	assert(f);
	fputs(text, f);
	fclose(f);
}

int main(void)
{
 defaults_init(PSX3D_DEFAULTS);   // the real defaults.ini (docs/settings.md)
 for(int model=0;model<2;model++) {
  g_new3ds=model;
  file(PSX3D_CONFIG,"renderer=sw\nstereo=2\nhalf3d=1\ncore.pcsx_rearmed_gpu_unai_fast_lighting=enabled\n");
  config_load();
  assert(g_cfg.stereo==(model?2:0));
  struct retro_variable vars[]={
   {"psx3d_renderer","Renderer; Software|GPU"},
   {"pcsx_rearmed_gpu_unai_fast_lighting","Light; disabled|enabled"},
   {"example","Example; first|second"},{NULL,NULL}};
  options_register(vars);
  assert(options_count()==1 && !strcmp(options_key(0),"example"));
  assert(options_get("pcsx_rearmed_gpu_unai_fast_lighting")==NULL);
  // Real 3D asks the core for GTE depths on the New 3DS only.
  config_set_stereo_active(true);
  assert(!strcmp(options_get("pcsx_rearmed_psx3d_stereo"),model?"enabled":"disabled"));
  config_set_stereo_active(false);
  assert(!strcmp(options_get("pcsx_rearmed_psx3d_stereo"),"disabled"));
  file(PSX3D_DIR "/games/test.ini","renderer=sw\ncore.pcsx_rearmed_gpu_unai_lighting=disabled\n");
  config_game_begin("test");
  assert(!strcmp(options_get("example"),"first"));
  config_game_end(); config_save(); config_load();
  assert(g_cfg.stereo==(model?2:0));
  // Dithering is forced on both consoles (docs/settings.md): a saved choice is ignored.
  file(PSX3D_CONFIG,"renderer=gpu\n");
  config_load();
  struct retro_variable dither[]={{"pcsx_rearmed_dithering","Dithering; disabled|enabled|force"},{NULL,NULL}};
  options_register(dither);
  assert(!strcmp(options_get("pcsx_rearmed_dithering"),"enabled"));
  file(PSX3D_CONFIG,"renderer=gpu\ncore.pcsx_rearmed_dithering=disabled\n");
  config_load();
  assert(!strcmp(options_get("pcsx_rearmed_dithering"),"enabled"));
 }
 puts("PASS: legacy renderer/unai configs ignored; GPU-only options; 3D follows the console (no setting); dithering forced");
}
