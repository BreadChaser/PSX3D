// Core options: the core registers its options with SET_VARIABLES
// ("Description; default|value2|value3"). Values set by the user are stored
// in config.c as overrides; everything else falls back to the core default.
#include <stdlib.h>
#include <string.h>
#include "psx3d.h"

#define MAX_OPTIONS 128
#define MAX_VALUES  64

typedef struct {
	char *key;
	char *desc;
	char *values[MAX_VALUES];
	int   nvalues;
} option_t;

static option_t opts[MAX_OPTIONS];
static int nopts;
static bool updated;

static void options_clear(void)
{
	for (int i = 0; i < nopts; i++) {
		free(opts[i].key);
		free(opts[i].desc);
		for (int j = 0; j < opts[i].nvalues; j++)
			free(opts[i].values[j]);
	}
	memset(opts, 0, sizeof(opts));
	nopts = 0;
}

void options_register(const struct retro_variable *vars)
{
	options_clear();
	for (; vars && vars->key && nopts < MAX_OPTIONS; vars++) {
		option_t *o = &opts[nopts];
		const char *semi = strstr(vars->value, "; ");
		if (!semi)
			continue;
		o->key = strdup(vars->key);
		o->desc = strndup(vars->value, semi - vars->value);

		const char *p = semi + 2;
		while (*p && o->nvalues < MAX_VALUES) {
			const char *bar = strchr(p, '|');
			size_t len = bar ? (size_t)(bar - p) : strlen(p);
			o->values[o->nvalues++] = strndup(p, len);
			if (!bar)
				break;
			p = bar + 1;
		}
		nopts++;
	}
	log_printf("registered %d core options\n", nopts);
}

static option_t *find(const char *key)
{
	for (int i = 0; i < nopts; i++)
		if (strcmp(opts[i].key, key) == 0)
			return &opts[i];
	return NULL;
}

const char *options_get(const char *key)
{
	const char *ov = config_core_override(key);
	option_t *o = find(key);

	if (o && ov) {
		// only hand the core values it actually knows about
		for (int j = 0; j < o->nvalues; j++)
			if (strcmp(o->values[j], ov) == 0)
				return o->values[j];
	}
	if (o && o->nvalues)
		return o->values[0];
	return ov;
}

bool options_take_updated(void)
{
	bool r = updated;
	updated = false;
	return r;
}

void options_mark_updated(void) { updated = true; }
int options_count(void) { return nopts; }
const char *options_key(int i) { return opts[i].key; }
const char *options_desc(int i) { return opts[i].desc; }

// Step option i to the next/previous value and store it as an override.
const char *options_cycle(int i, int dir)
{
	option_t *o = &opts[i];
	const char *cur = options_get(o->key);
	int idx = 0;

	for (int j = 0; j < o->nvalues; j++)
		if (cur && strcmp(o->values[j], cur) == 0)
			idx = j;
	idx = (idx + dir + o->nvalues) % o->nvalues;
	config_set_core_override(o->key, o->values[idx]);
	updated = true;
	return o->values[idx];
}
