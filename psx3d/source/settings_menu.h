// Shared page order for library defaults and the paused-game settings.
#ifndef SETTINGS_MENU_H
#define SETTINGS_MENU_H
enum { SETTINGS_HOME, SETTINGS_DISPLAY, SETTINGS_PERFORMANCE, SETTINGS_CONTROLS,
       SETTINGS_SOUND, SETTINGS_GENERAL, SETTINGS_ADVANCED, SETTINGS_PAGES };
static const char *const settings_page_names[SETTINGS_PAGES] = {
	"Settings", "Display", "Performance", "Controls", "Sound", "General", "Advanced"
};
#endif
