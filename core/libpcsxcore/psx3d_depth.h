/*
 * PSX3D stereo 3D support: per-vertex depth tracking.
 *
 * The GTE records the depth (SZ) of every point it projects, keyed by the
 * screen position it produced. Games copy those screen positions straight
 * into GPU polygon packets, so the GPU side can look each corner's depth
 * back up and build a coarse depth map of the frame. The frontend uses that
 * map to shift each part of the picture per eye.
 */
#ifndef PSX3D_DEPTH_H
#define PSX3D_DEPTH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PSX3D_ZT_BITS  16     // 64K entries: fewer collisions, fewer corners without a depth
#define PSX3D_DM_W     80     // depth map cells, each 8x8 PS1 pixels,
#define PSX3D_DM_H     60     // covering up to 640x480
#define PSX3D_DM_CELL  8

typedef struct {
	uint32_t key;   // packed screen x/y as the GTE wrote it (SXY)
	uint16_t z;     // GTE SZ (0..65535)
	uint16_t frame; // psx3d_zframe when written: stale entries don't match
} psx3d_zent;

typedef struct {
	uint32_t seq;            // bumped every time a new frame's map is published
	uint16_t w, h;           // PS1 display resolution the map was built for
	uint32_t ranks;          // 3D polygons drawn this frame (max depth value - 2)
	uint16_t z[PSX3D_DM_H][PSX3D_DM_W];  // see PSX3D_Z_* below
} psx3d_depthmap;

// Depth values 2..65534 are the draw-order rank of 3D polygons in the
// frame: PS1 games have no depth buffer and must draw back to front, so a
// higher rank is nearer. (Raw GTE depth can't be trusted for this: games
// draw skyboxes and backdrops as small geometry close to the camera.)
// Other values:
#define PSX3D_Z_NONE     0       // nothing drawn here
#define PSX3D_Z_OVERLAY  1       // 2D drawn after 3D this frame: HUD, text
#define PSX3D_Z_FAR      0xffff  // 2D drawn before any 3D: clears, backdrops

extern psx3d_zent psx3d_ztable[1 << PSX3D_ZT_BITS];
extern int psx3d_stereo_enabled;
extern uint16_t psx3d_zframe;

static inline uint32_t psx3d_zkey(int x, int y)
{
	return ((uint32_t)(y & 0xffff) << 16) | (uint32_t)(x & 0xffff);
}

static inline uint32_t psx3d_zhash(uint32_t key)
{
	return (key * 2654435761u) >> (32 - PSX3D_ZT_BITS);
}

// turn tracking on/off safely (main thread)
void psx3d_stereo_set(int on);

// GTE side (main thread)
static inline void psx3d_zput(int sx, int sy, int sz)
{
	uint32_t key = psx3d_zkey(sx, sy);
	psx3d_zent *e = &psx3d_ztable[psx3d_zhash(key)];
	e->z = (uint16_t)sz;
	e->frame = psx3d_zframe;
	e->key = key;
}

// GPU command side (main thread at submit time)
void psx3d_depth_cmd(const uint32_t *list, uint32_t cmd);
int psx3d_depth_poly(const uint32_t *words, uint32_t cmd);
void psx3d_depth_rect(int x, int y, int w, int h);
void psx3d_depth_publish(int disp_w, int disp_h);

// HUD layer: 2D primitives drawn after the 3D scene go into a separate
// VRAM-sized buffer instead of VRAM, so the frontend can warp the scene and
// lay the HUD on top. Pixels nothing was drawn to hold PSX3D_OV_KEY.
#define PSX3D_OV_KEY 0x7c1f
// latest complete HUD layer, where in it the displayed frame is, and a
// 512-bit map of the VRAM rows the HUD drew to
const uint16_t *psx3d_overlay_get(int *x, int *y, int *w, int *h, const uint32_t **rows);
// bumped by the GPU thread around each layer swap (odd while swapping);
// a reader that sees it change must discard what it read
extern volatile uint32_t psx3d_ov_seq;

// frontend: call once per emulated frame, then read the latest map
void psx3d_depth_frame(void);
const psx3d_depthmap *psx3d_depth_get(void);

#ifdef __cplusplus
}
#endif

#endif
