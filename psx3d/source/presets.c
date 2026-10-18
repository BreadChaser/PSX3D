// Serial lookup happens on disc identification, never in the frame loop.
#include "psx3d.h"
#include <ctype.h>
#include <string.h>
#include "game_presets.h"
static const game_preset_t *current_preset;
void game_presets_reset(void)
{
	current_preset = NULL;
}
game_preset_options_t game_capabilities(void)
{
	static game_preset_options_t cached;
	static const game_preset_t *cached_preset;
	static int cached_model = -1;
	if (cached_model == g_new3ds && cached_preset == current_preset)
		return cached;
	// Uncurated games expose every available feature and touch control.
	game_preset_options_t result = {
		.fast_forward = 1,
		.rewind = g_new3ds,
		.touch_controls = TOUCH_CONTROLS_AUTO,
		.needs_l2r2 = !g_new3ds,
		.needs_l3r3 = 1,
		.needs_right_stick = !g_new3ds,
		.renderer = PRESET_RENDERER_DEFAULT,
		.touch_bindings = {-2, -2, -2, -2, -2, -2},
	};
	if (current_preset) {
		game_preset_options_t p = g_new3ds ? current_preset->new3ds : current_preset->old3ds;
#define RESOLVE(field)                                                                                       \
	if (p.field >= 0)                                                                                        \
	result.field = p.field
		RESOLVE(fast_forward);
		RESOLVE(rewind);
		RESOLVE(touch_controls);
		RESOLVE(renderer);
		RESOLVE(needs_l2r2);
		RESOLVE(needs_l3r3);
		RESOLVE(needs_right_stick);
		for (int i = 0; i < INPUT_TOUCH_N; i++)
			if (p.touch_bindings[i] != INPUT_BIND_PRESET)
				result.touch_bindings[i] = p.touch_bindings[i];
#undef RESOLVE
	}
	// The current rewind engine requires memory and a New 3DS worker core.
	// Presets cannot enable an engine that this console cannot run.
	if (!g_new3ds)
		result.rewind = 0;
	cached = result;
	cached_preset = current_preset;
	cached_model = g_new3ds;
	return result;
}

void game_presets_disc_log(const char *message)
{
	static const char prefix[] = "CD-ROM ID: ";
	if (strncmp(message, prefix, sizeof(prefix) - 1))
		return;
	const char *p = message + sizeof(prefix) - 1;
	char serial[10];
	int n = 0;
	bool valid = true;
	for (; *p && *p != '\n' && *p != '\r'; p++) {
		unsigned char c = (unsigned char)*p;
		if (c == '-' || c == '_' || c == '.')
			continue;
		if (n == 9 || (n < 4 ? !isalpha(c) : !isdigit(c))) {
			valid = false;
			break;
		}
		serial[n++] = toupper(c);
	}
	serial[n] = 0;
	current_preset = NULL;
	if (valid && n == 9)
		for (size_t i = 0; i < sizeof(game_presets) / sizeof(game_presets[0]); i++)
			if (!strcmp(serial, game_presets[i].serial)) {
				current_preset = &game_presets[i];
				break;
			}
}
