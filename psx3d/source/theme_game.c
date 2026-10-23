// Cold preparation only: no allocation, timers or theme access in game draws.
#include "theme.h"
game_palette_t g_game_palette __attribute__((section(".theme_game.bss")));
void theme_game_begin(void)
{
	const theme_t *t = theme();
	theme_menu_refresh();
	g_game_palette = t->game;
	if (t == theme_get(0)) return; // preserve the Original controller exactly
	game_palette_t *g = &g_game_palette;
	g->pressed_add=TH_RGB((t->scene_white&255)*35/100, ((t->scene_white>>8)&255)*35/100, ((t->scene_white>>16)&255)*35/100);
	g->bg=t->menu_bg; g->title=t->header_title; g->ink=t->input_hide;
	g->mark=t->white; g->border=t->touch_border; g->face=t->touch_face;
	g->gradient[0]=t->hl_top | 0xff000000u; g->gradient[1]=t->hl_bottom | 0xff000000u;
	g->stick_rim=t->touch_border; g->stick_face=t->card_bottom;
	g->stick_grid=theme_alpha(t->value,179); g->stick_thumb=t->value;
	for(int i=0;i<4;i++) g->stripe[i]=t->accent[i];
	g->stats=t->help_text; g->stats_focus=t->value;
	g->budget_bg=t->section_rule; g->budget_over=t->error; g->budget_ok=t->help_bar;
	g->speed_ink=t->white; g->text=t->white; g->panel=t->touch_face; g->notice=t->message;
	g->warning=t->slow_text; g->alert_bg=t->slow_bg;
	g->alert_rule=t->help_impact; g->alert_text=t->message;
}
