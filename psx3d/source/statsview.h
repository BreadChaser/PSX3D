#ifndef STATS_VIEW_H
#define STATS_VIEW_H
#include "psx3d.h"
/* All rates describe the last wall-clock second; no core profiling is enabled. */
typedef struct {
	float fps, target, emu, max, present, wait, gpu;
	unsigned skipped, late, elapsed;
	const char *audio, *renderer;
} statsview_sample;
bool statsview_collecting(void);
void statsview_begin(const char *title);
void statsview_frame(void);
void statsview_update(const statsview_sample *s);
void statsview_draw(void);
void statsview_cycle(void);
bool statsview_touch(void);
bool statsview_redraw(int osd_frames);
#endif
