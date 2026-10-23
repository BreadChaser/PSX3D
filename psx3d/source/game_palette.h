#pragma once
#include <stdint.h>

// Gameplay colours are copied once at launch; draw paths read only this cache.
// A separate linker section keeps this storage after the established BSS pins.
typedef struct {
	uint32_t bg, title, ink, mark, border, face, gradient[2], pressed_add;
	uint32_t stick_rim, stick_face, stick_grid, stick_thumb;
	uint32_t stripe[4], stats, stats_focus, budget_bg, budget_over, budget_ok;
	uint32_t text, panel, notice, warning, alert_bg, alert_rule, alert_text;
	uint32_t black, osd_bg, speed_ink, speed_shadow, speed_bg;
} game_palette_t;
extern game_palette_t g_game_palette;
extern uint32_t g_input_palette[20]; // hide at 0, PS1/actions at id+1
void theme_game_begin(void);
void theme_menu_refresh(void);

