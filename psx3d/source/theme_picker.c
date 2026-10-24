// Shared production drawing for Settings > Display > Theme and SDK diagnostics.
#include "psx3d.h"
#include "scene.h"
static const char *name(int i, void *ud) {
    const flist_t *list=ud;
    float y=40+(i-list->scroll)*23;
    return y<40 || y+23>178 ? "" : theme_get(i)->name;
}
void theme_picker_draw(const flist_t *list, int selected, int committed) {
    const theme_t *th = theme();
    scene_draw(true);
    flist_draw(list, theme_count(), selected, 0, 40, 320, 23, 6, name, NULL, (void *)list, th->accent[3]);
    for (int i = 0; i < theme_count(); i++) {
        float y = 40 + (i - list->scroll) * 23;
        if (y < 40 || y + 23 > 178)
            continue;
        C2D_DrawRectSolid(286, y + 7, .35f, 10, 10,
                          i == 6 ? TH_RGB(83, 132, 255) : theme_get(i)->fx_colour);
    }
    C2D_DrawRectSolid(317, 40 + 138 * list->scroll / theme_count(), .3f, 2,
                      138.f * 6 / theme_count(), th->muted);
    // Hide the list's fading overflow before drawing the header and hint.
    C2D_DrawRectSolid(0,0,.4f,320,39,th->menu_bg);
    C2D_DrawRectSolid(0,178,.4f,320,30,th->menu_bg);
    ui_menu_header("Theme", "Preview");
    for(int i=0;i<4;i++) C2D_DrawRectSolid(10+i*18,26,.5f,16,2,th->accent[i]);
    ui_text_ex(NULL, 12, 186, .39f, th->muted, 0,
               selected == committed ? "Current theme" : "A selects · B restores current theme");
    ui_button_t bar[] = {{KEY_B, "Cancel", 0, false}, {KEY_A, "Select", UI_ICON_CHECK, false}};
    ui_bar_draw(bar, 2);
}
