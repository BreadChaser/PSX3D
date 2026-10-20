/* Real 3D in the native replay (replay-stereo). GPL-2.0-or-later.
 * The same depth capture and disparity code as the 3DS frontend
 * (psx3d/source/gpustereo_core.c), with the mirroring done against the
 * fake citro3d instead of the real one. */
#pragma once
#include <stdint.h>
void stereo_sim_list(const uint32_t *list, int n, const uint32_t *ex);
void stereo_sim_depths(const void *data, unsigned bytes);
void stereo_sim_frame(const char *dir, const int *info);

#ifdef STEREO_HOOKS   /* gpu_pica.c only: its draws and copies come here */
#include "citro3d.h"
void stereo_sim_draw(int primitive, int first, int n);
int stereo_sim_copy(u32 *src, u32 sd, u32 *dst, u32 dd, u32 n, u32 flags);
#define C3D_DrawArrays stereo_sim_draw
#define GX_TextureCopy stereo_sim_copy
#endif
