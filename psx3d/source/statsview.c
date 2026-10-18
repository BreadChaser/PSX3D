// Bottom-screen diagnostics. Formatting/font parsing happens at 1 Hz only.
// The caller leaves the bottom render target untouched between changes.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "statsview.h"

#define ROWS 14
static char title[80], lines[ROWS][128], brief[128];
static C2D_Text text[ROWS], short_text, footer;
static C2D_TextBuf buf;
static unsigned emulated;
extern char CdromId[10];
static bool dirty = true;
static bool reset_interval = true;
static float budget_fraction;

bool statsview_collecting(void)
{
	return g_cfg.show_fps || sw_stats || sw_prof || sw_pmu0 >= 0 || log_linked();
}

static const char *opt(const char *name)
{
	const char *v = options_get(name);
	return v ? v : "n/a";
}

static void parse(void)
{
	if (!buf) buf = C2D_TextBufNew(2048);
	if (!buf) return;
	C2D_TextBufClear(buf);
	C2D_TextParse(&short_text, buf, brief);
	C2D_TextOptimize(&short_text);
	C2D_TextParse(&footer, buf, "Tap here: Off / FPS / Detailed");
	C2D_TextOptimize(&footer);
	for (int i = 0; i < ROWS; i++) {
		C2D_TextParse(&text[i], buf, lines[i]);
		C2D_TextOptimize(&text[i]);
	}
	dirty = true;
}

void statsview_begin(const char *name)
{
	snprintf(title, sizeof(title), "%.48s", name);
	memset(lines, 0, sizeof(lines));
	strcpy(brief, "Waiting for stats...");
	strcpy(lines[0], "PERFORMANCE / waiting for stats");
	emulated = 0;
	reset_interval = true;
	budget_fraction = 0;
	dirty = true;   // also clear the menu when resuming with stats off
	if (g_cfg.show_fps) parse();
}

void statsview_frame(void)
{
	if (statsview_collecting()) emulated++;
}

void statsview_update(const statsview_sample *s)
{
	unsigned frames = emulated;
	emulated = 0;
	if (!g_cfg.show_fps) return;
	if (reset_interval) { reset_interval = false; return; } // drain counters accumulated while hidden
	char previous[sizeof(lines)], previous_brief[sizeof(brief)];
	memcpy(previous, lines, sizeof(lines));
	memcpy(previous_brief, brief, sizeof(brief));
	float old_budget = budget_fraction;
	float speed = s->elapsed && s->target ? frames * 100000.0f / s->elapsed / s->target : 0;
	snprintf(brief, sizeof(brief), "%.1f/%.1f fps  %.0f%%  %.1fms", s->fps, s->target, speed, s->emu);
	snprintf(lines[0], sizeof(lines[0]), "%.27s  %.9s", title, CdromId);
	snprintf(lines[1], sizeof(lines[1]), "HOST %.1f / %.1f fps    SPEED %.0f%%", s->fps, s->target, speed);
	snprintf(lines[2], sizeof(lines[2]), "Draw %u  skip %u  late %u / second", frames > s->skipped ? frames - s->skipped : 0, s->skipped, s->late);
	snprintf(lines[3], sizeof(lines[3]), "Frameskip: %.24s", opt("pcsx_rearmed_frameskip_type"));
	snprintf(lines[4], sizeof(lines[4]), "Emu %.2f ms    max %.2f", s->emu, s->max);
	snprintf(lines[5], sizeof(lines[5]), "Present %.2f   wait %.2f   GPU %.2f ms", s->present, s->wait, s->gpu);
	snprintf(lines[6], sizeof(lines[6]), "Budget %.1f / %.1f ms", s->emu + s->present, 1000.0f / s->target);
	budget_fraction = (s->emu + s->present) * s->target / 1000;
	const char *u = s->audio ? strstr(s->audio, "underruns=") : NULL;
	snprintf(lines[7], sizeof(lines[7]), "Audio %u%% (50=target)  underruns %lu", audio_occupancy(), u ? strtoul(u + 10, NULL, 10) : 0);
	if (s->audio && !strcmp(s->audio, "audio off")) strcpy(lines[7], "Audio off");
	snprintf(lines[8], sizeof(lines[8]), "CPU0 n/a  syscore allowance %d%%", g_syscore_pct);
	snprintf(lines[9], sizeof(lines[9]), "SPU cfg %.8s CD %.6s time n/a", opt("pcsx_rearmed_spu_thread"), opt("pcsx_rearmed_cd_readahead"));
	unsigned batches=0, prims=0, decodes=0, evict=0, inval=0, uploads=0, syncs=0, reads=0;
	if (s->renderer) {
		const char *p = strstr(s->renderer, "; evictions by size ");
		if (p) p = strchr(p + 1, ';');
		if (p) sscanf(p + 1, "%u batches %u prims %u tile decodes (%u evictions %u invalidations) %u uploads %u syncs %u readbacks", &batches,&prims,&decodes,&evict,&inval,&uploads,&syncs,&reads);
	}
	snprintf(lines[10], sizeof(lines[10]), "Tex up/dec %u/%u  sync/read %u/%u", uploads, decodes, syncs, reads);
	snprintf(lines[11], sizeof(lines[11]), "Draw/batch %u/%u  linear %luK  DRC n/a", prims, batches, (unsigned long)(linearSpaceFree()/1024));
	if (!s->renderer || !*s->renderer) {
		strcpy(lines[10], "Renderer counters n/a (software GPU)");
		snprintf(lines[11], sizeof(lines[11]), "Linear %luK  DRC n/a", (unsigned long)(linearSpaceFree()/1024));
	}
	snprintf(lines[12], sizeof(lines[12]), "PS1 clock %.8s  GTE regs %.8s", opt("pcsx_rearmed_psxclock"), opt("pcsx_rearmed_gteregsunneeded"));
	snprintf(lines[13], sizeof(lines[13]), "GTE flags %.8s  light %.8s  WS %s", opt("pcsx_rearmed_nogteflags"), opt("pcsx_rearmed_gpu_unai_fast_lighting"), config_widescreen() ? "on" : "off");
	if (memcmp(previous, lines, sizeof(lines)) || strcmp(previous_brief, brief) ||
	    (int)(old_budget*308) != (int)(budget_fraction*308)) parse();
}

void statsview_draw(void)
{
	if (!g_cfg.show_fps || !buf) return;
	u32 color = C2D_Color32(175, 183, 198, 255);
	if (g_cfg.show_fps == 1) {
		C2D_DrawText(&short_text, C2D_WithColor, 4, 222, .6f, .42f, .42f, color);
		return;
	}
	for (int i=0; i<ROWS; i++)
		C2D_DrawText(&text[i], C2D_WithColor, 6, 3+i*12, .6f, .38f, .38f,
			i == 1 ? C2D_Color32(125,215,185,255) : color);
	float f = budget_fraction > 1 ? 1 : budget_fraction;
	ui_rect(6, 176, 308, 3, C2D_Color32(30,35,42,255));
	ui_rect(6, 176, 308*f, 3, budget_fraction > 1 ? C2D_Color32(220,130,90,255) : C2D_Color32(85,170,140,255));
	C2D_DrawText(&footer, C2D_WithColor, 6, 222, .6f, .38f, .38f, color);
	// Options are sampled with the panel, never looked up in the draw path.
}

void statsview_cycle(void)
{
	if (!g_cfg.show_fps) {
		reset_interval = true;
		memset(lines, 0, sizeof(lines));
		strcpy(lines[0], "PERFORMANCE / waiting for stats");
		strcpy(brief, "Waiting for stats...");
		parse();
	}
	g_cfg.show_fps = (g_cfg.show_fps + 1) % 3;
	if (g_cfg.show_fps && !buf) parse();
	dirty = true;
	config_save();
}

bool statsview_touch(void)
{
	if (!g_cfg.show_fps || !(hidKeysDown() & KEY_TOUCH)) return false;
	touchPosition t; hidTouchRead(&t);
	if (t.py < 218) return false;
	statsview_cycle();
	return true;
}

bool statsview_redraw(int osd)
{
	static int mode = -1, previous_osd;
	static u32 held;
	static touchPosition previous_touch;
	touchPosition touch; hidTouchRead(&touch);
	u32 keys = hidKeysHeld() & KEY_TOUCH;
	bool changed = dirty || mode != g_cfg.show_fps || osd || previous_osd || keys != held || (keys && (touch.px != previous_touch.px || touch.py != previous_touch.py));
	previous_touch = touch;
	dirty = false; mode = g_cfg.show_fps; previous_osd = osd; held = keys;
	return changed;
}
