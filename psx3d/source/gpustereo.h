// Real stereoscopic 3D for the GPU renderer (New 3DS only).
//
// The renderer's PS1 VRAM stays canonical: the left eye shows it unchanged.
// Every GPU write to it (draws, uploads, VRAM copies) is repeated into a
// separate right-eye target; polygon vertices whose depth is known move
// sideways there by their disparity. Nothing in the renderer changes: the
// repeats come from link-time wrappers, so with 3D off (and on the Old 3DS,
// which never turns it on) the renderer runs exactly as before.
//
// Depth comes from the GTE: it records the depth of every point it
// projects, keyed by screen position. When the main thread hands a polygon
// to the GPU thread, gs_capture() looks its corners up and stores their
// depth by VRAM position (the corners plus the vertices the renderer will
// create when it clips the polygon to the drawing area). The GPU thread
// then finds each vertex's depth by its VRAM position.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct { int16_t x, y, u, v; uint32_t rgba; } gs_vtx;   // gpu_pica's 12-byte vertex

// gpustereo_core.c: platform independent
bool gs_core_alloc(void);      // the depth table (main thread, renderer idle)
void gs_core_free(void);
void gs_core_clear(void);
// main thread: one polygon command (0x20-0x3f) with its corners' GTE depths
// (0 = unknown) and the drawing area / offset registers (E3, E4, E5) it uses
void gs_capture(const uint32_t *words, uint32_t cmd, uint32_t e3, uint32_t e4, uint32_t e5,
	const uint16_t z[4]);
// main thread, once per emulated frame: new generation, depth range update.
// strength 0..1 (the 3D slider), hres the picture's width in PS1 pixels;
// the GPU thread uses them from now on.
void gs_core_frame(float strength, int hres);
// GPU thread: n vertices (whole triangles) copied to dst, polygon vertices
// with a known depth shifted by their disparity. Returns how many moved.
int gs_shift(const gs_vtx *src, gs_vtx *dst, int n);
float gs_focus(void);          // depth on the screen plane, 0 until known
// 0..1: how much of the 3D effect is the whole picture behind the screen
// (frames without polygon depth) instead of real 3D; follows gs_core_frame
float gs_flat(void);
// GPU thread: the frontend showed this VRAM rectangle (draw-order layers);
// gs_core_pass_shown: the same for Real 3D's passes (gs_core_shown calls it)
void gs_core_shown(int x, int y, int w, int h);
void gs_core_pass_shown(int x, int y, int w, int h);
float gs_layer_blend(void);    // 0: draw-order layers .. 1: Real 3D
void gs_layer_status(char *out, int n);
bool gs_picture_flat(void);    // the picture shown has no layers (a still, an FMV): screen depth
// diagnostics, reset by each call: corners looked up, found; vertices
// shifted; corners rejected (another object's depth)
void gs_core_counts(unsigned *lookups, unsigned *found, unsigned *shifted, unsigned *rejected);

// gpustereo.c (3DS frontend)
void gpustereo_frame(void);           // main thread, after pica_thread_sync
void gpustereo_after_present(void);   // GPU thread, after C3D_FrameEnd
void gpustereo_game_end(void);        // GPU idle, before pica_exit
// presentation: the right eye's texture for the canonical/held picture, or NULL
struct C3D_Tex_t;
bool gpustereo_ready(void);
void *gpustereo_right_for(void *picture_tex);
void gpustereo_frame_end(void);       // before a command list is submitted
extern volatile bool gpustereo_copy_bypass;   // thumbnails: plain copies out of VRAM
int gpustereo_mode(void);             // 0 off, 1 screen depth, 2 real 3D (0 on Old 3DS)
float gpustereo_flat(void);           // real 3D: share of screen depth (1 until the right eye is ready)
void gpustereo_status(char *out, int n);
