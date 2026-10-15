// Settings file: sdmc:/3ds/PSX3D/config.ini
//   lines "name=value" set frontend settings,
//   lines "core.<option>=value" override PCSX-ReARMed core options.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psx3d.h"

#define MAX_OVERRIDES 128

psx3d_config g_cfg;

static struct { char *key, *value; } ovr[MAX_OVERRIDES];
static int novr;

// Defaults tuned for the 3DS that differ from the core's own defaults.
// (Options with only one sensible value on the 3DS are locked in options.c.)
static const char *const tuned_defaults[][2] = {
	{ NULL, NULL },
};

static void set_defaults(void)
{
	memset(&g_cfg, 0, sizeof(g_cfg));
	strcpy(g_cfg.last_dir, "sdmc:/");
	g_cfg.scale_mode = 0;
	g_cfg.smooth = 1;
	g_cfg.show_fps = 0;
	g_cfg.stereo = 1;
	for (size_t i = 0; i < sizeof(tuned_defaults) / sizeof(tuned_defaults[0]); i++)
		if (tuned_defaults[i][0])
			config_set_core_override(tuned_defaults[i][0], tuned_defaults[i][1]);
}

static char *trim(char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	char *e = s + strlen(s);
	while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t'))
		*--e = 0;
	return s;
}

void config_load(void)
{
	char line[PATH_LEN + 64];
	FILE *f;

	set_defaults();
	f = fopen(PSX3D_CONFIG, "r");
	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		char *l = trim(line), *eq = strchr(l, '=');
		if (*l == '#' || *l == ';' || !eq)
			continue;
		*eq = 0;
		char *k = trim(l), *v = trim(eq + 1);

		if (strncmp(k, "core.", 5) == 0)
			config_set_core_override(k + 5, v);
		else if (strcmp(k, "last_dir") == 0)
			snprintf(g_cfg.last_dir, sizeof(g_cfg.last_dir), "%s", v);
		else if (strcmp(k, "scale_mode") == 0)
			g_cfg.scale_mode = atoi(v) % 3;
		else if (strcmp(k, "smooth") == 0)
			g_cfg.smooth = atoi(v) != 0;
		else if (strcmp(k, "show_fps") == 0)
			g_cfg.show_fps = atoi(v) != 0;
		else if (strcmp(k, "stereo") == 0)
			g_cfg.stereo = atoi(v) != 0;
	}
	fclose(f);
}

void config_save(void)
{
	FILE *f = fopen(PSX3D_CONFIG, "w");
	if (!f)
		return;
	fprintf(f, "# PSX3D settings\n");
	fprintf(f, "last_dir=%s\n", g_cfg.last_dir);
	fprintf(f, "scale_mode=%d\n", g_cfg.scale_mode);
	fprintf(f, "smooth=%d\n", g_cfg.smooth);
	fprintf(f, "show_fps=%d\n", g_cfg.show_fps);
	fprintf(f, "stereo=%d\n", g_cfg.stereo);
	for (int i = 0; i < novr; i++) {
		// built-in defaults aren't saved, so improving them reaches old installs
		bool is_default = false;
		for (size_t j = 0; j < sizeof(tuned_defaults) / sizeof(tuned_defaults[0]); j++)
			if (tuned_defaults[j][0] && strcmp(ovr[i].key, tuned_defaults[j][0]) == 0 &&
			    strcmp(ovr[i].value, tuned_defaults[j][1]) == 0)
				is_default = true;
		if (!is_default)
			fprintf(f, "core.%s=%s\n", ovr[i].key, ovr[i].value);
	}
	fclose(f);
}

const char *config_core_override(const char *key)
{
	for (int i = 0; i < novr; i++)
		if (strcmp(ovr[i].key, key) == 0)
			return ovr[i].value;
	return NULL;
}

void config_set_core_override(const char *key, const char *value)
{
	for (int i = 0; i < novr; i++) {
		if (strcmp(ovr[i].key, key) == 0) {
			free(ovr[i].value);
			ovr[i].value = strdup(value);
			return;
		}
	}
	if (novr < MAX_OVERRIDES) {
		ovr[novr].key = strdup(key);
		ovr[novr].value = strdup(value);
		novr++;
	}
}

// Widescreen hack: the GTE squeezes 3D geometry horizontally by 4:3 / 5:3 =
// 0.8, so games draw a wider view that fills the 400x240 top screen.
#define WS_KEY "pcsx_rearmed_psx3d_widescreen"
#define WS_Q16 "52429"   // 0.8 in 16.16 fixed point

bool config_widescreen(void)
{
	const char *v = config_core_override(WS_KEY);
	return v && strcmp(v, "0") != 0;
}

void config_set_widescreen(bool on)
{
	config_set_core_override(WS_KEY, on ? WS_Q16 : "0");
	options_mark_updated();
}
