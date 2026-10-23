// Animated menu backdrops. Call only from menu draw callbacks. All working
// storage is owned by the menu and released before loading any game.
#pragma once
#include <stdbool.h>
void scene_draw(bool bottom);
void scene_release(void);
