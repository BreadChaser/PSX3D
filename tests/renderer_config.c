// Exercise the production config and option code through the host SDK shim.
#include "psx3d.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool g_new3ds, sw_o3ds, sw_nosputhread, sw_nogputhread, sw_noskip, sw_fs1;
int g_syscore_pct;
char sw_cdra[8], sw_clock[8], sw_opt[8][48];
void log_printf(const char *fmt, ...) { (void)fmt; }
void input_map_reset(void) {}
bool renderer_is_gpu(void) { return false; }

static void file(const char *path, const char *text)
{
	FILE *f = fopen(path, "w");
	assert(f);
	fputs(text, f);
	fclose(f);
}

static int renderer_option(void)
{
	for (int i = 0; i < options_count(); i++)
		if (!strcmp(options_key(i), "psx3d_renderer"))
			return i;
	return -1;
}

int main(void)
{
	config_load();
	assert(g_cfg.renderer == 1);
	assert(g_cfg.half3d == 1);   // Old 3DS software default survives
	struct retro_variable vars[] = {
		{ "example", "Example; first|second" }, { NULL, NULL }
	};
	options_register(vars);
	assert(renderer_option() >= 0);

	file(PSX3D_CONFIG, "debug=1\nrenderer=sw\n");
	config_load();
	options_register(vars);
	int idx = renderer_option();
	assert(idx >= 0);
	assert(!strcmp(options_get(options_key(idx)), "Software"));
	config_save();
	config_load();
	assert(g_cfg.renderer == 1);

	file(PSX3D_DIR "/games/test.ini", "screen=1\nrenderer=gpu\n");
	config_game_begin("test");
	assert(g_cfg.renderer == 0);
	options_cycle(idx, 1);
	assert(g_cfg.renderer == 1);
	assert(!strcmp(gamecfg_get("renderer"), "sw"));
	config_set_renderer(0);
	config_save();
	config_game_end();
	assert(g_cfg.renderer == 1);
	config_load();
	assert(g_cfg.renderer == 1);
	config_game_begin("test");
	assert(g_cfg.renderer == 0);
	config_game_end();
	config_game_begin("other");
	assert(g_cfg.renderer == 1);
	config_game_end();

	g_cfg.debug = 0;
	options_register(vars);
	assert(renderer_option() >= 0);
	struct retro_core_option_definition defs[] = {
		{ .key = "example", .desc = "Example",
		  .values = {{ "first", NULL }, { "second", NULL }}, .default_value = "first" },
		{ 0 }
	};
	g_cfg.debug = 1;
	options_register_v1(defs);
	assert(renderer_option() >= 0);
	assert(options_count() == 2);
	options_register_v1(defs);
	assert(options_count() == 2);
	// New 3DS worker defaults on; switch/option controls it, while Old 3DS
	// remains single-threaded even with an explicit enable override.
	g_new3ds = true;
	remove(PSX3D_CONFIG);
	config_load();
	assert(g_cfg.renderer == 1 && g_cfg.half3d == 0);
	assert(!strcmp(options_get("pcsx_rearmed_gpu_thread_rendering"), "enabled"));
	sw_nogputhread = true;
	assert(!strcmp(options_get("pcsx_rearmed_gpu_thread_rendering"), "disabled"));
	sw_nogputhread = false;
	strcpy(sw_opt[0], "gpu_thread_rendering=disabled");
	assert(!strcmp(options_get("pcsx_rearmed_gpu_thread_rendering"), "disabled"));
	strcpy(sw_opt[0], "gpu_thread_rendering=enabled");
	g_new3ds = false;
	assert(!strcmp(options_get("pcsx_rearmed_gpu_thread_rendering"), "disabled"));
	sw_opt[0][0] = 0;
	puts("renderer config: software defaults on both models, normal visibility, cycling, persistence, per-game override and global restoration passed");
}
