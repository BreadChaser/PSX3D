// Section order and cursor helpers shared by both settings lists.
#ifndef SETTINGS_MENU_H
#define SETTINGS_MENU_H
enum { SETTINGS_DISPLAY = 1, SETTINGS_PERFORMANCE, SETTINGS_CONTROLS,
       SETTINGS_SOUND, SETTINGS_GENERAL, SETTINGS_ADVANCED, SETTINGS_SECTIONS };
static inline int settings_step(const int *items, int n, int sel, int dir,
	int first_header, int last_header)
{
	if (!n) return -1;
	for (int i = 0; i < n; i++) {
		sel = (sel + dir + n) % n;
		if (items[sel] < first_header || items[sel] > last_header) return sel;
	}
	return -1;
}
static inline int settings_find(const int *items, int n, int item,
	int first_header, int last_header)
{
	for (int i = 0; i < n; i++)
		if (items[i] == item && (item < first_header || item > last_header)) return i;
	return settings_step(items, n, n - 1, 1, first_header, last_header);
}
#endif
