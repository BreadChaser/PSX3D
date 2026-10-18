/***************************************************************************
*   Copyright (C) 2010 PCSX4ALL Team                                      *
*   Copyright (C) 2010 Unai                                               *
*   Copyright (C) 2011 notaz                                              *
*   Copyright (C) 2016 Senquack (dansilsby <AT> gmail <DOT> com)          *
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
*   This program is distributed in the hope that it will be useful,       *
*   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
*   GNU General Public License for more details.                          *
*                                                                         *
*   You should have received a copy of the GNU General Public License     *
*   along with this program; if not, write to the                         *
*   Free Software Foundation, Inc.,                                       *
*   51 Franklin Street, Fifth Floor, Boston, MA 02111-1307 USA.           *
***************************************************************************/

#include <stddef.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "arm_features.h"
#include "../gpulib/gpu.h"
#include "../gpulib/gpu_async.h"
#include "../../libpcsxcore/psx3d_depth.h"
#include "old/if.h"

//#include "port.h"
#include "gpu_unai.h"

// GPU fixed point math
#include "gpu_fixedpoint.h"

// Inner loop driver instantiation file
#include "gpu_inner.h"

// GPU internal image drawing functions
#include "gpu_raster_image.h"

// GPU internal line drawing functions
#include "gpu_raster_line.h"

// GPU internal polygon drawing functions
#include "gpu_raster_polygon.h"

// GPU internal sprite drawing functions
#include "gpu_raster_sprite.h"

// GPU command buffer execution/store
#include "gpu_command.h"

/////////////////////////////////////////////////////////////////////////////

#ifndef GPU_UNAI_NO_OLD
#define IS_OLD_RENDERER() gpu_unai.config.old_renderer
#else
#define IS_OLD_RENDERER() false
#endif

int renderer_init(void)
{
  memset((void*)&gpu_unai, 0, sizeof(gpu_unai));
  gpu_unai.vram = (le16_t *)gpu.vram;

  // Original standalone gpu_unai initialized TextureWindow[]. I added the
  //  same behavior here, since it seems unsafe to leave [2],[3] unset when
  //  using HLE and Rearmed gpu_neon sets this similarly on init. -senquack
  gpu_unai.TextureWindow[0] = 0;
  gpu_unai.TextureWindow[1] = 0;
  gpu_unai.TextureWindow[2] = 255;
  gpu_unai.TextureWindow[3] = 255;
  //senquack - new vars must be updated whenever texture window is changed:
  //           (used for polygon-drawing in gpu_inner.h, gpu_raster_polygon.h)
  gpu_unai.inn.mask_v00u =
    ((u32)gpu_unai.TextureWindow[3] << 24) | gpu_unai.TextureWindow[2];

  // Configuration options
  gpu_unai.config = gpu_unai_config_ext;
  //senquack - disabled, not sure this is needed and would require modifying
  // sprite-span functions, perhaps unnecessarily. No Abe Oddysey hack was
  // present in latest PCSX4ALL sources we were using.
  //gpu_unai.config.enableAbbeyHack = gpu_unai_config_ext.abe_hack;
  gpu_unai.inn.ilace_mask = gpu_unai.config.ilace_force;

#if defined(GPU_UNAI_USE_INT_DIV_MULTINV) || (!defined(GPU_UNAI_NO_OLD) && !defined(GPU_UNAI_USE_FLOATMATH))
  // s_invTable
  for(int i=1;i<=(1<<TABLE_BITS);++i)
  {
    double v = 1.0 / double(i);
#ifdef GPU_TABLE_10_BITS
    v *= double(0xffffffff>>1);
#else
    v *= double(0x80000000);
#endif
    s_invTable[i-1]=s32(v);
  }
#endif

  SetupLightLUT();
  SetupDitheringConstants();

  return 0;
}

static void psx3d_release_buffers(void);

void renderer_finish(void)
{
  psx3d_stereo_set(0);
  psx3d_release_buffers();
}

void renderer_notify_screen_change(const struct psx_gpu_screen *screen)
{

  gpu_unai.inn.ilace_mask = gpu_unai.config.ilace_force;

  if (gpu.state.downscale_enable)
  {
    gpu_unai.inn.ilace_mask |= !!(gpu.status & PSX_GPU_STATUS_INTERLACE);
  }

  /*
  printf("res change hres: %d   vres: %d   depth: %d   ilace_mask: %d\n",
      gpu.screen.hres, gpu.screen.vres, (gpu.status & PSX_GPU_STATUS_RGB24) ? 24 : 15,
      gpu_unai.ilace_mask);
  */
}

#ifdef USE_GPULIB
// Handles GP0 draw settings commands 0xE1...0xE6
static void gpuGP0Cmd_0xEx(gpu_unai_t &gpu_unai, u32 cmd_word)
{
  // Assume incoming GP0 command is 0xE1..0xE6, convert to 1..6
  u8 num = (cmd_word >> 24) & 7;
  switch (num) {
    case 1: {
      // GP0(E1h) - Draw Mode setting (aka "Texpage")
      u32 cur_texpage = gpu_unai.GPU_GP1 & 0x7FF;
      u32 new_texpage = cmd_word & 0x7FF;
      if (cur_texpage != new_texpage) {
        gpu_unai.GPU_GP1 = (gpu_unai.GPU_GP1 & ~0x7FF) | new_texpage;
        gpuSetTexture(gpu_unai.GPU_GP1);
      }
    } break;

    case 2: {
      // GP0(E2h) - Texture Window setting
      if (cmd_word != gpu_unai.TextureWindowCur) {
        static const u8 TextureMask[32] = {
          255, 7, 15, 7, 31, 7, 15, 7, 63, 7, 15, 7, 31, 7, 15, 7,
          127, 7, 15, 7, 31, 7, 15, 7, 63, 7, 15, 7, 31, 7, 15, 7
        };
        gpu_unai.TextureWindowCur = cmd_word;
        gpu_unai.TextureWindow[0] = ((cmd_word >> 10) & 0x1F) << 3;
        gpu_unai.TextureWindow[1] = ((cmd_word >> 15) & 0x1F) << 3;
        gpu_unai.TextureWindow[2] = TextureMask[(cmd_word >> 0) & 0x1F];
        gpu_unai.TextureWindow[3] = TextureMask[(cmd_word >> 5) & 0x1F];
        gpu_unai.TextureWindow[0] &= ~gpu_unai.TextureWindow[2];
        gpu_unai.TextureWindow[1] &= ~gpu_unai.TextureWindow[3];

        // Inner loop vars must be updated whenever texture window is changed:
        gpu_unai.inn.mask_v00u =
          ((u32)gpu_unai.TextureWindow[3] << 24) | gpu_unai.TextureWindow[2];

        gpuSetTexture(gpu_unai.GPU_GP1);
      }
    } break;

    case 3: {
      // GP0(E3h) - Set Drawing Area top left (X1,Y1)
      gpu_unai.DrawingArea[0] = cmd_word         & 0x3FF;
      gpu_unai.DrawingArea[1] = (cmd_word >> 10) & 0x3FF;
    } break;

    case 4: {
      // GP0(E4h) - Set Drawing Area bottom right (X2,Y2)
      gpu_unai.DrawingArea[2] = (cmd_word         & 0x3FF) + 1;
      gpu_unai.DrawingArea[3] = ((cmd_word >> 10) & 0x3FF) + 1;
    } break;

    case 5: {
      // GP0(E5h) - Set Drawing Offset (X,Y)
      s32 ox = GPU_EXPANDSIGN(cmd_word), oy = GPU_EXPANDSIGN(cmd_word >> 11);
      gpu_unai.DrawingOffset[0] = ox;
      gpu_unai.DrawingOffset[1] = oy;
    } break;

    case 6: {
      // GP0(E6h) - Mask Bit Setting
      gpu_unai.Masking  = (cmd_word & 0x2) <<  1;
      gpu_unai.PixelMSB = (cmd_word & 0x1) <<  8;
    } break;
  }
}
#endif

#include "../gpulib/gpu_timing.h"

// Strip lower 3 bits of each color and determine if lighting should be used:
static inline bool need_lighting(u32 rgb_raw)
{
  return (rgb_raw & HTOLE32(0xF8F8F8)) != HTOLE32(0x808080);
}

static inline void textured_sprite(int &cpu_cycles_sum, int &cpu_cycles)
{
  u32 PRIM = le32_to_u32(gpu_unai.PacketBuffer.U4[0]) >> 24;
  gpuSetCLUT(le32_to_u32(gpu_unai.PacketBuffer.U4[2]) >> 16);
  u32 driver_idx = Blending_Mode | gpu_unai.TEXT_MODE | gpu_unai.Masking | Blending | (gpu_unai.PixelMSB>>1);
  s32 w = 0, h = 0;

  //senquack - Only color 808080h-878787h allows skipping lighting calculation:
  // This fixes Silent Hill running animation on loading screens:
  // (On PSX, color values 0x00-0x7F darken the source texture's color,
  //  0x81-FF lighten textures (ultimately clamped to 0x1F),
  //  0x80 leaves source texture color unchanged, HOWEVER,
  //   gpu_unai uses a simple lighting LUT whereby only the upper
  //   5 bits of an 8-bit color are used, so 0x80-0x87 all behave as
  //   0x80.
  //
  // NOTE: I've changed all textured sprite draw commands here and
  //  elsewhere to use proper behavior, but left poly commands
  //  alone, I don't want to slow rendering down too much. (TODO)
  if (need_lighting(le32_raw(gpu_unai.PacketBuffer.U4[0])))
    driver_idx |= Lighting;
  PS driver = gpuSpriteDrivers[driver_idx];
  PtrUnion packet = { .ptr = (void*)&gpu_unai.PacketBuffer };
  gpuDrawS(packet, driver, &w, &h);
  gput_sum(cpu_cycles_sum, cpu_cycles, gput_sprite(w, h));
}


/////////////////////////////////////////////////////////////////////////////
// PSX3D stereo: build a coarse depth map of the frame being drawn.
// Polygon corners are matched to the depths the GTE recorded for the same
// screen positions; PS1 games draw back to front, so later polygons simply
// overwrite earlier ones. Sprites and anything without depth data count as
// the screen plane (0), which keeps HUDs and text on the screen surface.

// ---- dual rendering: each eye drawn into its own VRAM, every vertex moved
// sideways by its depth. The main thread works out the shifts when a
// primitive is submitted (the GTE depth table matches the frame then) and
// queues them; the GPU thread pops one pack per polygon or sprite.
extern "C" { uint16_t *psx3d_vram2; }
static u16 *psx3d_vram2_mem;
static bool psx3d_seen_3d;   // any 3D geometry drawn yet in this frame
static float psx3d_strength;          // max far disparity, PS1 pixels (0 = flat)
static float psx3d_zc = 0.0f;         // convergence depth: zero disparity here
static double psx3d_logz_sum;
static u32 psx3d_logz_n;

// log2 from the float's bits (+-0.09): plenty for an average depth
static inline float psx3d_fastlog2(float x)
{
  union { float f; u32 i; } u = { x };
  return (float)(int)(u.i - 0x3f800000) * (1.0f / (1 << 23));
}

static inline int psx3d_disp(float z)
{
  float S = psx3d_strength;
  if (S <= 0.0f || z <= 0.0f || psx3d_zc <= 0.0f)
    return 0;
  float d = S * (1.0f - psx3d_zc / z);
  if (d > S) d = S;
  if (d < -0.4f * S) d = -0.4f * S;   // keep pop-out gentle
  return (int)(d < 0 ? d - 0.5f : d + 0.5f);
}

// per-frame stats for tuning (frontend logs them)
static int psx3d_st_dmin, psx3d_st_dmax, psx3d_st_n3d, psx3d_st_n2d, psx3d_st_zmin, psx3d_st_zmax;
static int psx3d_pub_st[6];
// the last complete second
extern "C" void psx3d_stats(float *zc, int *n3d, int *n2d, int *dmin, int *dmax, int *zmin, int *zmax)
{
  *zc = psx3d_zc; *n3d = psx3d_pub_st[0]; *n2d = psx3d_pub_st[1];
  *dmin = psx3d_pub_st[2]; *dmax = psx3d_pub_st[3]; *zmin = psx3d_pub_st[4];
  *zmax = psx3d_pub_st[5];
}

// 2D drawing: only big pieces drawn before any 3D are backdrops (pushed
// far). Everything else, text and HUD above all, stays on the screen plane:
// letters are separate small sprites, and splitting a word across two
// depths makes it unreadable.
static inline int psx3d_flat_disp(int w, int h)
{
  if (psx3d_seen_3d || w < 64 || h < 64)
    return 0;
  return psx3d_disp(65535.0f) * 7 / 10;
}

static inline u32 psx3d_pack(const int *d, int n)
{
  u32 p = 0;
  for (int i = 0; i < n; i++)
    p |= (u32)(u8)(s8)d[i] << (8 * i);
  return p;
}

static u16 psx3d_cur[PSX3D_DM_H][PSX3D_DM_W];
static psx3d_depthmap psx3d_pub;
static u32 psx3d_prims;      // primitives recorded since the last publish
static u32 psx3d_rank;       // 3D polygons drawn so far this frame

// 2D drawn after the 3D scene is an overlay (HUD); before it, a backdrop
static inline u16 psx3d_flat_z(void)
{
  return psx3d_seen_3d ? PSX3D_Z_OVERLAY : PSX3D_Z_FAR;
}

// HUD layer. The main thread classifies primitives at submit time (when
// the GTE depth table matches the frame) and queues one flag per primitive;
// the GPU thread draws the same primitives in the same order and pops them.
static inline int psx3d_sext11(u32 v) { return ((s32)(v << 21)) >> 21; }

#define OV_RING 65536
#define OV_FLAG_FRAME 2          // frame boundary marker
static volatile u32 *psx3d_ov_ring; // allocated only while software stereo is active
static volatile u32 psx3d_ov_head, psx3d_ov_tail;
static u16 *psx3d_ov_buf[2];     // [front] = last complete frame's HUD
static volatile int psx3d_ov_front;
volatile uint32_t psx3d_ov_seq;
static u32 psx3d_ov_rows[2][16]; // VRAM rows each layer has drawn to (512 bits)
static int psx3d_ov_x, psx3d_ov_y, psx3d_ov_w, psx3d_ov_h;

static void psx3d_ov_push(u32 f)
{
  psx3d_ov_ring[psx3d_ov_head % OV_RING] = f;
  __sync_synchronize();
  psx3d_ov_head++;
}

static void psx3d_ov_clear_rows(u16 *b, const u32 *rows)
{
  u32 v = PSX3D_OV_KEY | (PSX3D_OV_KEY << 16);
  for (int y = 0; y < 512; y++) {
    if (!(rows[y >> 5] & (1u << (y & 31))))
      continue;
    u32 *p = (u32 *)(b + y * 1024);
    for (int i = 0; i < 512; i++)
      p[i] = v;
  }
}

static void psx3d_ov_clear(u16 *b)
{
  u32 all[16];
  memset(all, 0xff, sizeof(all));
  psx3d_ov_clear_rows(b, all);
}

// GPU thread: remember which rows a HUD primitive touched
static void psx3d_ov_mark(int back, const le32_t *list, u32 cmd)
{
  int ymin = 1 << 20, ymax = -(1 << 20);
  if (cmd < 0x40) {
    int n = (cmd & 8) ? 4 : 3, tex = (cmd & 4) != 0, gouraud = (cmd & 0x10) != 0, idx = 1;
    for (int v = 0; v < n; v++) {
      if (v > 0 && gouraud) idx++;
      int y = psx3d_sext11(le32_to_u32(list[idx++]) >> 16);
      if (tex) idx++;
      if (y < ymin) ymin = y;
      if (y > ymax) ymax = y;
    }
  } else {
    int y = psx3d_sext11(le32_to_u32(list[1]) >> 16), h;
    switch (cmd & 0x18) {
    case 0x00: h = (le32_to_u32(list[(cmd & 4) ? 3 : 2]) >> 16) & 0x1ff; break;
    case 0x08: h = 1; break;
    case 0x10: h = 8; break;
    default:   h = 16; break;
    }
    ymin = y; ymax = y + h - 1;
  }
  ymin += gpu_unai.DrawingOffset[1];
  ymax += gpu_unai.DrawingOffset[1];
  if (ymin < 0) ymin = 0;
  if (ymax > 511) ymax = 511;
  for (int y = ymin; y <= ymax; y++)
    psx3d_ov_rows[back][y >> 5] |= 1u << (y & 31);
}

static bool psx3d_ov_alloc(void)
{
  for (int i = 0; i < 2; i++) {
    if (!psx3d_ov_buf[i] && !(psx3d_ov_buf[i] = (u16 *)malloc(1024 * 512 * 2)))
      return false;
    psx3d_ov_clear(psx3d_ov_buf[i]);
  }
  return true;
}

// Turn stereo depth/HUD tracking on or off (main thread). The GPU thread
// may still have queued primitives that never got a flag; let it finish
// them first, then start both sides of the flag queue from zero, so flags
// and primitives can't end up offset from each other.
extern "C" void psx3d_stereo_set(int on)
{
  on = on && !psx3d_renderer_is_pica() && gpu.vram != NULL;
  if (on == psx3d_stereo_enabled)
    return;
  gpu_async_sync(&gpu);
  psx3d_stereo_enabled = 0;
  psx3d_vram2 = NULL;
  __sync_synchronize();
  psx3d_ov_head = psx3d_ov_tail = 0;
  psx3d_prims = 0;
  psx3d_seen_3d = false;
  if (on) {
    if (!psx3d_ov_ring)
      psx3d_ov_ring = (u32 *)malloc(OV_RING * sizeof(u32));
    if (!psx3d_ov_ring)
      return;
    // same layout as gpulib's VRAM (2MB, drawing may run past the end)
    if (!psx3d_vram2_mem)
      psx3d_vram2_mem = (u16 *)malloc(1024 * 512 * 2 * 2 + 8192);
    if (!psx3d_vram2_mem) {
      free((void *)psx3d_ov_ring);
      psx3d_ov_ring = NULL;
      return;
    }
    u16 *v2 = (u16 *)(((uintptr_t)psx3d_vram2_mem + 4095) & ~(uintptr_t)4095);
    memcpy(v2, gpu.vram, 1024 * 512 * 2);
    psx3d_vram2 = v2;
  } else {
    free(psx3d_vram2_mem);
    psx3d_vram2_mem = NULL;
    free((void *)psx3d_ov_ring);
    psx3d_ov_ring = NULL;
  }
  __sync_synchronize();
  psx3d_stereo_enabled = on;
}

// GPUshutdown has already stopped the worker. No software allocations survive
// into the library or a subsequent PICA game.
static void psx3d_release_buffers(void)
{
  psx3d_vram2 = NULL;
  free(psx3d_vram2_mem);
  psx3d_vram2_mem = NULL;
  free((void *)psx3d_ov_ring);
  psx3d_ov_ring = NULL;
  for (int i = 0; i < 2; i++) {
    free(psx3d_ov_buf[i]);
    psx3d_ov_buf[i] = NULL;
  }
  psx3d_ov_head = psx3d_ov_tail = 0;
  psx3d_strength = psx3d_zc = 0;
  psx3d_logz_sum = psx3d_logz_n = 0;
  memset(psx3d_cur, 0, sizeof(psx3d_cur));
  memset(&psx3d_pub, 0, sizeof(psx3d_pub));
  memset(psx3d_pub_st, 0, sizeof(psx3d_pub_st));
}

// frontend: 3D strength (max far disparity in PS1 pixels, from the slider)
// frontend: draw 3D polygons at half vertical resolution (Old 3DS speed)
static bool psx3d_half3d;
extern "C" void psx3d_set_half3d(int on)
{
  psx3d_half3d = on != 0;
}

extern "C" void psx3d_set_strength(float px)
{
  psx3d_strength = px;
}

// frontend: the right eye's picture: VRAM pointer and the display area
extern "C" const uint16_t *psx3d_right_get(int *x, int *y, int *w, int *h, int *rgb24)
{
  if (!psx3d_vram2)
    return NULL;
  *x = gpu.screen.src_x; *y = gpu.screen.src_y;
  *w = gpu.screen.hres;  *h = gpu.screen.vres;
  *rgb24 = (gpu.status & PSX_GPU_STATUS_RGB24) != 0;
  return psx3d_vram2;
}

// GPU thread: shifts for the next polygon or sprite
static u32 psx3d_shift_pop(void)
{
  if (psx3d_ov_tail == psx3d_ov_head)
    return 0;
  u32 f = psx3d_ov_ring[psx3d_ov_tail % OV_RING];
  psx3d_ov_tail++;
  return f;
}

extern "C" const uint16_t *psx3d_overlay_get(int *x, int *y, int *w, int *h, const uint32_t **rows)
{
  if (!psx3d_ov_buf[0])
    return NULL;
  *x = psx3d_ov_x; *y = psx3d_ov_y; *w = psx3d_ov_w; *h = psx3d_ov_h;
  *rows = psx3d_ov_rows[psx3d_ov_front];
  return psx3d_ov_buf[psx3d_ov_front];
}

// called by the frontend once per emulated frame
// called by the frontend once per emulated frame: move the convergence
// toward this frame's typical depth (smoothly, so the picture doesn't pump)
extern "C" void psx3d_depth_frame(void)
{
  if (!psx3d_stereo_enabled)
    return;
  if (psx3d_logz_n > 32) {
    // the focus only follows real scene changes: a creeping focus makes
    // every whole-pixel shift flip back and forth (shimmering)
    float target = exp2f((float)(psx3d_logz_sum / psx3d_logz_n)) * 0.75f;
    static float aim;
    if (psx3d_zc <= 0.0f)
      psx3d_zc = aim = target;
    else if (target > aim * 1.15f || target < aim * 0.87f)
      aim = target;
    psx3d_zc += (aim - psx3d_zc) * 0.01f;
  }
  psx3d_logz_sum = 0;
  psx3d_logz_n = 0;
  static int frames;
  if (++frames >= 60) {   // stats cover a second, then start over
    frames = 0;
    psx3d_pub_st[0] = psx3d_st_n3d; psx3d_pub_st[1] = psx3d_st_n2d;
    psx3d_pub_st[2] = psx3d_st_dmin; psx3d_pub_st[3] = psx3d_st_dmax;
    psx3d_pub_st[4] = psx3d_st_zmin; psx3d_pub_st[5] = psx3d_st_zmax;
    psx3d_st_dmin = 127; psx3d_st_dmax = -127; psx3d_st_n3d = psx3d_st_n2d = 0;
    psx3d_st_zmin = 1 << 30; psx3d_st_zmax = 0;
  }
  psx3d_seen_3d = false;
  psx3d_prims = 0;
  psx3d_zframe++;
}

extern "C" const psx3d_depthmap *psx3d_depth_get(void)
{
  return &psx3d_pub;
}

extern "C" void psx3d_depth_publish(int disp_w, int disp_h)
{
  // both a buffer flip and a draw-offset change can end a frame; only the
  // first one has anything to publish
  if (!psx3d_prims)
    return;
  psx3d_prims = 0;
  memcpy(psx3d_pub.z, psx3d_cur, sizeof(psx3d_cur));
  psx3d_pub.w = disp_w;
  psx3d_pub.h = disp_h;
  psx3d_pub.ranks = psx3d_rank;
  psx3d_pub.seq++;
  memset(psx3d_cur, 0, sizeof(psx3d_cur));
  psx3d_seen_3d = false;
  psx3d_rank = 0;
}


extern "C" void psx3d_depth_rect(int x, int y, int w, int h)
{
  if (psx3d_seen_3d)
    return;   // HUD: goes to its own layer, keep the scene's depth underneath
  int x0 = x / PSX3D_DM_CELL, y0 = y / PSX3D_DM_CELL;
  int x1 = (x + w - 1) / PSX3D_DM_CELL, y1 = (y + h - 1) / PSX3D_DM_CELL;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 >= PSX3D_DM_W) x1 = PSX3D_DM_W - 1;
  if (y1 >= PSX3D_DM_H) y1 = PSX3D_DM_H - 1;
  u16 z = PSX3D_Z_FAR;
  for (int cy = y0; cy <= y1; cy++)
    for (int cx = x0; cx <= x1; cx++)
      psx3d_cur[cy][cx] = z;
}

static inline u16 psx3d_clampz(float zz)
{
  return zz < 2.0f ? 2 : (zz > 65534.0f ? 65534 : (u16)zz);
}

// z: per-corner depths, or NULL for a flat 2D polygon
static void psx3d_tri(const int *x, const int *y, const float *z)
{
  int minx = x[0], maxx = x[0], miny = y[0], maxy = y[0];
  for (int i = 1; i < 3; i++) {
    if (x[i] < minx) minx = x[i];
    if (x[i] > maxx) maxx = x[i];
    if (y[i] < miny) miny = y[i];
    if (y[i] > maxy) maxy = y[i];
  }
  float area = (float)(x[1] - x[0]) * (y[2] - y[0]) - (float)(x[2] - x[0]) * (y[1] - y[0]);
  int cx0 = minx / PSX3D_DM_CELL, cx1 = maxx / PSX3D_DM_CELL;
  int cy0 = miny / PSX3D_DM_CELL, cy1 = maxy / PSX3D_DM_CELL;
  if (cx0 < 0) cx0 = 0;
  if (cy0 < 0) cy0 = 0;
  if (cx1 >= PSX3D_DM_W) cx1 = PSX3D_DM_W - 1;
  if (cy1 >= PSX3D_DM_H) cy1 = PSX3D_DM_H - 1;
  if (cx0 > cx1 || cy0 > cy1)
    return;

  bool hit = false;
  if (area != 0.0f) {
    float inv = 1.0f / area;
    for (int cy = cy0; cy <= cy1; cy++) {
      float py = cy * PSX3D_DM_CELL + PSX3D_DM_CELL * 0.5f;
      for (int cx = cx0; cx <= cx1; cx++) {
        float px = cx * PSX3D_DM_CELL + PSX3D_DM_CELL * 0.5f;
        float w0 = ((x[1] - px) * (y[2] - py) - (x[2] - px) * (y[1] - py)) * inv;
        float w1 = ((x[2] - px) * (y[0] - py) - (x[0] - px) * (y[2] - py)) * inv;
        float w2 = 1.0f - w0 - w1;
        if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
          continue;
        psx3d_cur[cy][cx] = psx3d_clampz(w0 * z[0] + w1 * z[1] + w2 * z[2]);
        hit = true;
      }
    }
  }
  if (!hit) {
    // smaller than a cell: still mark the cell it sits in, so distant
    // geometry made of tiny triangles doesn't fall to the screen plane
    int cx = (x[0] + x[1] + x[2]) / 3 / PSX3D_DM_CELL;
    int cy = (y[0] + y[1] + y[2]) / 3 / PSX3D_DM_CELL;
    if (cx >= 0 && cx < PSX3D_DM_W && cy >= 0 && cy < PSX3D_DM_H) {
      psx3d_cur[cy][cx] = psx3d_clampz((z[0] + z[1] + z[2]) * (1.0f / 3.0f));
    }
  }
}

// per-corner disparities of a polygon, packed one signed byte per corner
static u32 psx3d_poly_shifts(const u32 *w, u32 cmd)
{
  int n = (cmd & 8) ? 4 : 3, tex = (cmd & 4) != 0, gouraud = (cmd & 0x10) != 0;
  float z[4];
  int found = 0, idx = 1, d[4];
  int minx = 1 << 20, maxx = -(1 << 20), miny = 1 << 20, maxy = -(1 << 20);
  float zsum = 0.0f;
  for (int v = 0; v < n; v++) {
    if (v > 0 && gouraud)
      idx++;
    u32 word = le32_to_u32(((const le32_t *)w)[idx++]);
    if (tex)
      idx++;
    int x = psx3d_sext11(word), y = psx3d_sext11(word >> 16);
    if (x < minx) minx = x;
    if (x > maxx) maxx = x;
    if (y < miny) miny = y;
    if (y > maxy) maxy = y;
    u32 key = psx3d_zkey(x, y);
    const psx3d_zent *e = &psx3d_ztable[psx3d_zhash(key)];
    // this frame's or the previous frame's projections only (games often
    // transform during one frame and submit early in the next)
    if (e->key == key && e->z && (u16)(psx3d_zframe - e->frame) <= 1) {
      z[v] = (float)e->z;
      zsum += z[v];
      found++;
    } else {
      z[v] = -1.0f;
    }
  }
  if (!found) {
    psx3d_st_n2d++;
    int dd = psx3d_flat_disp(maxx - minx, maxy - miny);
    for (int v = 0; v < n; v++) d[v] = dd;
    return psx3d_pack(d, n);
  }
  psx3d_seen_3d = true;
  psx3d_st_n3d++;
  float zavg = zsum / found;
  for (int v = 0; v < n; v++) {
    if (z[v] < 0.0f)
      z[v] = zavg;
    d[v] = psx3d_disp(z[v]);
    if (d[v] < psx3d_st_dmin) psx3d_st_dmin = d[v];
    if (d[v] > psx3d_st_dmax) psx3d_st_dmax = d[v];
    if ((int)z[v] < psx3d_st_zmin) psx3d_st_zmin = (int)z[v];
    if ((int)z[v] > psx3d_st_zmax) psx3d_st_zmax = (int)z[v];
    psx3d_logz_sum += psx3d_fastlog2(z[v]);
    psx3d_logz_n++;
  }
  return psx3d_pack(d, n);
}

extern "C" void psx3d_depth_cmd_(const u32 *list_, u32 cmd);
extern "C" void psx3d_depth_cmd(const u32 *list_, u32 cmd)
{
  if (cmd < 0x20 || cmd >= 0x80 || (cmd >= 0x40 && cmd < 0x60))
    return;   // polygons and sprites only (lines can be re-submitted when split)
  psx3d_depth_cmd_(list_, cmd);
}

extern "C" void psx3d_depth_cmd_(const u32 *list_, u32 cmd)
{
  psx3d_prims++;
  if (cmd < 0x40) {
    psx3d_ov_push(psx3d_poly_shifts(list_, cmd));
  } else {
    const le32_t *list = (const le32_t *)list_;
    int w, h;
    switch (cmd & 0x18) {
    case 0x00: {   // variable size
      u32 wh = le32_to_u32(list[(cmd & 4) ? 3 : 2]);
      w = wh & 0x3ff; h = (wh >> 16) & 0x1ff;
    } break;
    case 0x08: w = h = 1; break;
    case 0x10: w = h = 8; break;
    default:   w = h = 16; break;
    }
    psx3d_st_n2d++;
    psx3d_ov_push((u32)(u8)(s8)psx3d_flat_disp(w, h));
  }
}

// move each corner of the packet in the GPU's buffer for one eye
static void psx3d_apply_shift(u32 cmd, u32 pack, int eye)
{
  if (!pack)
    return;
  if (cmd < 0x40) {
    int n = (cmd & 8) ? 4 : 3, tex = (cmd & 4) != 0, gouraud = (cmd & 0x10) != 0, idx = 1;
    for (int v = 0; v < n; v++) {
      if (v > 0 && gouraud)
        idx++;
      int d = (s8)(pack >> (8 * v));
      int sh = eye ? d - d / 2 : -(d / 2);
      u32 word = le32_to_u32(gpu_unai.PacketBuffer.U4[idx]);
      int x = psx3d_sext11(word) + sh;
      gpu_unai.PacketBuffer.U4[idx] = u32_to_le32((word & 0xffff0000u) | (x & 0xffff));
      idx++;
      if (tex)
        idx++;
    }
  } else {
    int d = (s8)pack;
    int sh = eye ? d - d / 2 : -(d / 2);
    u32 word = le32_to_u32(gpu_unai.PacketBuffer.U4[1]);
    int x = psx3d_sext11(word) + sh;
    gpu_unai.PacketBuffer.U4[1] = u32_to_le32((word & 0xffff0000u) | (x & 0xffff));
  }
}

extern const unsigned char cmd_lengths[256];

int renderer_do_cmd_list(u32 *list_, int list_len, uint32_t *ex_regs,
 int *cycles_sum_out, int *cycles_last, int *last_cmd)
{
  int cpu_cycles_sum = 0, cpu_cycles = *cycles_last;
  u32 cmd = 0, len, i;
  le32_t *list = (le32_t *)list_;
  le32_t *list_start = list;
  le32_t *list_end = list + list_len;
  le16_t *vram_saved = NULL;

  if (IS_OLD_RENDERER()) {
    return oldunai_do_cmd_list(list_, list_len, ex_regs,
             cycles_sum_out, cycles_last, last_cmd);
  }

  for (; list < list_end; list += 1 + len)
  {
    cmd = le32_to_u32(*list) >> 24;
    len = cmd_lengths[cmd];
    if (list + 1 + len > list_end) {
      cmd = -1;
      break;
    }

    // with the async GPU, gpu_async.c records depth on the main thread
    if (psx3d_stereo_enabled && !gpu_async_enabled(&gpu))
      psx3d_depth_cmd((const u32 *)list, cmd);
    // stereo: draw commands run once per eye, the second into the right
    // eye's VRAM, polygons and sprites moved by their depth
    bool psx3d_prim = cmd >= 0x20 && cmd < 0x80 && (cmd < 0x40 || cmd >= 0x60);
    u32 psx3d_sh = (psx3d_stereo_enabled && psx3d_prim) ? psx3d_shift_pop() : 0;
    int eyes = (psx3d_vram2 && cmd >= 0x02 && cmd < 0x80) ? 2 : 1;
    u32 len0 = len;
    int sum_eye0 = 0, last_eye0 = 0;
    vram_saved = NULL;
    for (int eye = 0; eye < eyes; eye++) {
    if (eye) {
      len = len0;
      sum_eye0 = cpu_cycles_sum; last_eye0 = cpu_cycles;
      vram_saved = gpu_unai.vram;
      gpu_unai.vram = (le16_t *)psx3d_vram2;
    }

    #define PRIM cmd
    gpu_unai.PacketBuffer.U4[0] = list[0];
    for (i = 1; i <= len; i++)
      gpu_unai.PacketBuffer.U4[i] = list[i];
    if (psx3d_sh && eyes == 2)
      psx3d_apply_shift(cmd, psx3d_sh, eye);

    PtrUnion packet = { .ptr = (void*)&gpu_unai.PacketBuffer };
    // half-res applies to shaded/textured polygons; quads that are really
    // sprites (2D) were simplified away above or below and stay full res
    gpu_unai.half3d = psx3d_half3d && cmd >= 0x20 && cmd < 0x40;

    switch (cmd)
    {
      case 0x02:
        gpuClearImage(packet);
        gput_sum(cpu_cycles_sum, cpu_cycles,
           gput_fill(le16_to_s16(packet.U2[4]) & 0x3ff, le16_to_s16(packet.U2[5]) & 0x1ff));
        break;

      case 0x20:
      case 0x21:
      case 0x22:
      case 0x23: {          // Monochrome 3-pt poly
        PP driver = gpuPolySpanDrivers[
          Blending_Mode |
          gpu_unai.Masking | Blending | gpu_unai.PixelMSB
        ];
        gpuDrawPolyF(packet, driver, false);
        gput_sum(cpu_cycles_sum, cpu_cycles, gput_poly_base());
      } break;

      case 0x24:
      case 0x25:
      case 0x26:
      case 0x27: {          // Textured 3-pt poly
        gpuSetCLUT   (le32_to_u32(gpu_unai.PacketBuffer.U4[2]) >> 16);
        gpuSetTexture(le32_to_u32(gpu_unai.PacketBuffer.U4[4]) >> 16);

        u32 driver_idx =
          Dithering |
          Blending_Mode | gpu_unai.TEXT_MODE |
          gpu_unai.Masking | Blending | gpu_unai.PixelMSB;

        if (!FastLightingEnabled()) {
          driver_idx |= Lighting;
        } else {
          if (!((gpu_unai.PacketBuffer.U1[0]>0x5F) && (gpu_unai.PacketBuffer.U1[1]>0x5F) && (gpu_unai.PacketBuffer.U1[2]>0x5F)))
            driver_idx |= Lighting;
        }

        PP driver = gpuPolySpanDrivers[driver_idx];
        gpuDrawPolyFT(packet, driver, false);
        gput_sum(cpu_cycles_sum, cpu_cycles, gput_poly_base_t());
      } break;

      case 0x28:
      case 0x29:
      case 0x2A:
      case 0x2B: {          // Monochrome 4-pt poly
        PP driver = gpuPolySpanDrivers[
          Blending_Mode |
          gpu_unai.Masking | Blending | gpu_unai.PixelMSB
        ];
        gpuDrawPolyF(packet, driver, true); // is_quad = true
        gput_sum(cpu_cycles_sum, cpu_cycles, gput_quad_base());
      } break;

      case 0x2C:
      case 0x2D:
      case 0x2E:
      case 0x2F: {          // Textured 4-pt poly
        u32 dithering = Dithering;
        u32 simplified_count;
        gpuSetTexture(le32_to_u32(gpu_unai.PacketBuffer.U4[4]) >> 16);
        if (!dithering &&
            (simplified_count = prim_try_simplify_quad_t(gpu_unai.PacketBuffer.U4,
              gpu_unai.PacketBuffer.U4)))
        {
          for (i = 0;; ) {
            textured_sprite(cpu_cycles_sum, cpu_cycles);
            if (++i >= simplified_count)
              break;
            memcpy(&gpu_unai.PacketBuffer.U4[0], &gpu_unai.PacketBuffer.U4[i * 4], 16);
          }
          break;
        }
        gpuSetCLUT(le32_to_u32(gpu_unai.PacketBuffer.U4[2]) >> 16);

        u32 driver_idx =
          dithering |
          Blending_Mode | gpu_unai.TEXT_MODE |
          gpu_unai.Masking | Blending | gpu_unai.PixelMSB;

        if (!FastLightingEnabled()) {
          driver_idx |= Lighting;
        } else {
          if (!((gpu_unai.PacketBuffer.U1[0]>0x5F) && (gpu_unai.PacketBuffer.U1[1]>0x5F) && (gpu_unai.PacketBuffer.U1[2]>0x5F)))
            driver_idx |= Lighting;
        }

        PP driver = gpuPolySpanDrivers[driver_idx];
        gpuDrawPolyFT(packet, driver, true); // is_quad = true
        gput_sum(cpu_cycles_sum, cpu_cycles, gput_quad_base_t());
      } break;

      case 0x30:
      case 0x31:
      case 0x32:
      case 0x33: {          // Gouraud-shaded 3-pt poly
        u32 dithering = Dithering;
        //NOTE: The '129' here is CF_GOURAUD | CF_LIGHT, however
        // this is an untextured poly, so CF_LIGHT (texture blend)
        // shouldn't apply. Until the original array of template
        // instantiation ptrs is fixed, we're stuck with this. (TODO)
        u8 gouraud = 129;
        if (!dithering) {
          u32 xor_ = 0, rgb0 = le32_raw(gpu_unai.PacketBuffer.U4[0]);
          for (i = 1; i < 3; i++)
            xor_ |= rgb0 ^ le32_raw(gpu_unai.PacketBuffer.U4[i * 2]);
          if ((xor_ & HTOLE32(0xf8f8f8)) == 0)
            gouraud = 0;
        }
        PP driver = gpuPolySpanDrivers[
          dithering |
          Blending_Mode |
          gpu_unai.Masking | Blending | gouraud | gpu_unai.PixelMSB
        ];
        if (gouraud)
          gpuDrawPolyG(packet, driver, false);
        else
          gpuDrawPolyF(packet, driver, false, POLYTYPE_G);
        gput_sum(cpu_cycles_sum, cpu_cycles, gput_poly_base_g());
      } break;

      case 0x34:
      case 0x35:
      case 0x36:
      case 0x37: {          // Gouraud-shaded, textured 3-pt poly
        gpuSetCLUT    (le32_to_u32(gpu_unai.PacketBuffer.U4[2]) >> 16);
        gpuSetTexture (le32_to_u32(gpu_unai.PacketBuffer.U4[5]) >> 16);
        u32 dithering = Dithering;
        u8 lighting = Lighting;
        u8 gouraud = lighting ? (1<<7) : 0;
        if (lighting) {
          u32 xor_ = 0, rgb0 = le32_raw(gpu_unai.PacketBuffer.U4[0]);
          for (i = 1; i < 3; i++)
            xor_ |= rgb0 ^ le32_raw(gpu_unai.PacketBuffer.U4[i * 3]);
          if ((xor_ & HTOLE32(0xf8f8f8)) == 0) {
            gouraud = 0;
            if (!dithering && !need_lighting(rgb0))
              lighting = 0;
          }
        }
        PP driver = gpuPolySpanDrivers[
          dithering |
          Blending_Mode | gpu_unai.TEXT_MODE |
          gpu_unai.Masking | Blending | gouraud | lighting | gpu_unai.PixelMSB
        ];
        if (gouraud)
          gpuDrawPolyGT(packet, driver, false); // is_quad = true
        else
          gpuDrawPolyFT(packet, driver, false, POLYTYPE_GT);
        gput_sum(cpu_cycles_sum, cpu_cycles, gput_poly_base_gt());
      } break;

      case 0x38:
      case 0x39:
      case 0x3A:
      case 0x3B: {          // Gouraud-shaded 4-pt poly
        u32 dithering = Dithering;
        // See notes regarding '129' for 0x30..0x33 further above -senquack
        u8 gouraud = 129;
        if (!dithering) {
          u32 xor_ = 0, rgb0 = le32_raw(gpu_unai.PacketBuffer.U4[0]);
          for (i = 1; i < 4; i++)
            xor_ |= rgb0 ^ le32_raw(gpu_unai.PacketBuffer.U4[i * 2]);
          if ((xor_ & HTOLE32(0xf8f8f8)) == 0)
            gouraud = 0;
        }
        PP driver = gpuPolySpanDrivers[
          dithering |
          Blending_Mode |
          gpu_unai.Masking | Blending | gouraud | gpu_unai.PixelMSB
        ];
        if (gouraud)
          gpuDrawPolyG(packet, driver, true); // is_quad = true
        else
          gpuDrawPolyF(packet, driver, true, POLYTYPE_G);
        gput_sum(cpu_cycles_sum, cpu_cycles, gput_quad_base_g());
      } break;

      case 0x3C:
      case 0x3D:
      case 0x3E:
      case 0x3F: {          // Gouraud-shaded, textured 4-pt poly
        u32 dithering = Dithering;
        u32 simplified_count;
        gpuSetTexture(le32_to_u32(gpu_unai.PacketBuffer.U4[5]) >> 16);
        if (!dithering &&
             (simplified_count = prim_try_simplify_quad_gt(gpu_unai.PacketBuffer.U4,
              gpu_unai.PacketBuffer.U4)))
        {
          for (i = 0;; ) {
            textured_sprite(cpu_cycles_sum, cpu_cycles);
            if (++i >= simplified_count)
              break;
            memcpy(&gpu_unai.PacketBuffer.U4[0], &gpu_unai.PacketBuffer.U4[i * 4], 16);
          }
          break;
        }
        gpuSetCLUT(le32_to_u32(gpu_unai.PacketBuffer.U4[2]) >> 16);
        u8 lighting = Lighting;
        u8 gouraud = lighting ? (1<<7) : 0;
        if (lighting) {
          u32 xor_ = 0, rgb0 = le32_raw(gpu_unai.PacketBuffer.U4[0]);
          for (i = 1; i < 4; i++)
            xor_ |= rgb0 ^ le32_raw(gpu_unai.PacketBuffer.U4[i * 3]);
          if ((xor_ & HTOLE32(0xf8f8f8)) == 0) {
            gouraud = 0;
            if (!dithering && !need_lighting(rgb0))
              lighting = 0;
          }
        }
        PP driver = gpuPolySpanDrivers[
          dithering |
          Blending_Mode | gpu_unai.TEXT_MODE |
          gpu_unai.Masking | Blending | gouraud | lighting | gpu_unai.PixelMSB
        ];
        if (gouraud)
          gpuDrawPolyGT(packet, driver, true); // is_quad = true
        else
          gpuDrawPolyFT(packet, driver, true, POLYTYPE_GT);
        gput_sum(cpu_cycles_sum, cpu_cycles, gput_quad_base_gt());
      } break;

      case 0x40:
      case 0x41:
      case 0x42:
      case 0x43: {          // Monochrome line
        // Shift index right by one, as untextured prims don't use lighting
        u32 driver_idx = (Blending_Mode | gpu_unai.Masking | Blending | (gpu_unai.PixelMSB>>3)) >> 1;
        PSD driver = gpuPixelSpanDrivers[driver_idx];
        gpuDrawLineF(packet, driver);
        gput_sum(cpu_cycles_sum, cpu_cycles, gput_line(0));
      } break;

      case 0x48 ... 0x4F: { // Monochrome line strip
        u32 num_vertexes = 1;
        le32_t *list_position = &list[2];

        // Shift index right by one, as untextured prims don't use lighting
        u32 driver_idx = (Blending_Mode | gpu_unai.Masking | Blending | (gpu_unai.PixelMSB>>3)) >> 1;
        PSD driver = gpuPixelSpanDrivers[driver_idx];
        gpuDrawLineF(packet, driver);

        while(1)
        {
          gpu_unai.PacketBuffer.U4[1] = gpu_unai.PacketBuffer.U4[2];
          gpu_unai.PacketBuffer.U4[2] = *list_position++;
          gpuDrawLineF(packet, driver);
          gput_sum(cpu_cycles_sum, cpu_cycles, gput_line(0));

          num_vertexes++;
          if(list_position >= list_end) {
            cmd = -1;
            goto breakloop;
          }
          if((le32_raw(*list_position) & HTOLE32(0xf000f000)) == HTOLE32(0x50005000))
            break;
        }

        len += (num_vertexes - 2);
      } break;

      case 0x50:
      case 0x51:
      case 0x52:
      case 0x53: {          // Gouraud-shaded line
        // Shift index right by one, as untextured prims don't use lighting
        u32 driver_idx = (Blending_Mode | gpu_unai.Masking | Blending | (gpu_unai.PixelMSB>>3)) >> 1;
        // Index MSB selects Gouraud-shaded PixelSpanDriver:
        driver_idx |= (1 << 5);
        PSD driver = gpuPixelSpanDrivers[driver_idx];
        gpuDrawLineG(packet, driver);
        gput_sum(cpu_cycles_sum, cpu_cycles, gput_line(0));
      } break;

      case 0x58 ... 0x5F: { // Gouraud-shaded line strip
        u32 num_vertexes = 1;
        le32_t *list_position = &list[2];

        // Shift index right by one, as untextured prims don't use lighting
        u32 driver_idx = (Blending_Mode | gpu_unai.Masking | Blending | (gpu_unai.PixelMSB>>3)) >> 1;
        // Index MSB selects Gouraud-shaded PixelSpanDriver:
        driver_idx |= (1 << 5);
        PSD driver = gpuPixelSpanDrivers[driver_idx];
        gpuDrawLineG(packet, driver);

        while(1)
        {
          gpu_unai.PacketBuffer.U4[0] = gpu_unai.PacketBuffer.U4[2];
          gpu_unai.PacketBuffer.U4[1] = gpu_unai.PacketBuffer.U4[3];
          gpu_unai.PacketBuffer.U4[2] = *list_position++;
          gpu_unai.PacketBuffer.U4[3] = *list_position++;
          gpuDrawLineG(packet, driver);
          gput_sum(cpu_cycles_sum, cpu_cycles, gput_line(0));

          num_vertexes++;
          if(list_position >= list_end) {
            cmd = -1;
            goto breakloop;
          }
          if((le32_raw(*list_position) & HTOLE32(0xf000f000)) == HTOLE32(0x50005000))
            break;
        }

        len += (num_vertexes - 2) * 2;
      } break;

      case 0x60:
      case 0x61:
      case 0x62:
      case 0x63: {          // Monochrome rectangle (variable size)
        PT driver = gpuTileDrivers[(Blending_Mode | gpu_unai.Masking | Blending | (gpu_unai.PixelMSB>>3)) >> 1];
        s32 w = 0, h = 0;
        gpuDrawT(packet, driver, &w, &h);
        gput_sum(cpu_cycles_sum, cpu_cycles, gput_sprite(w, h));
      } break;

      case 0x64:
      case 0x65:
      case 0x66:
      case 0x67:            // Textured rectangle (variable size)
        textured_sprite(cpu_cycles_sum, cpu_cycles);
        break;

      case 0x68:
      case 0x69:
      case 0x6A:
      case 0x6B: {          // Monochrome rectangle (1x1 dot)
        gpu_unai.PacketBuffer.U4[2] = u32_to_le32(0x00010001);
        PT driver = gpuTileDrivers[(Blending_Mode | gpu_unai.Masking | Blending | (gpu_unai.PixelMSB>>3)) >> 1];
        s32 w = 0, h = 0;
        gpuDrawT(packet, driver, &w, &h);
        gput_sum(cpu_cycles_sum, cpu_cycles, gput_sprite(1, 1));
      } break;

      case 0x70:
      case 0x71:
      case 0x72:
      case 0x73: {          // Monochrome rectangle (8x8)
        gpu_unai.PacketBuffer.U4[2] = u32_to_le32(0x00080008);
        PT driver = gpuTileDrivers[(Blending_Mode | gpu_unai.Masking | Blending | (gpu_unai.PixelMSB>>3)) >> 1];
        s32 w = 0, h = 0;
        gpuDrawT(packet, driver, &w, &h);
        gput_sum(cpu_cycles_sum, cpu_cycles, gput_sprite(w, h));
      } break;

      case 0x74:
      case 0x75:
      case 0x76:
      case 0x77: {          // Textured rectangle (8x8)
        gpu_unai.PacketBuffer.U4[3] = u32_to_le32(0x00080008);
        textured_sprite(cpu_cycles_sum, cpu_cycles);
      } break;

      case 0x78:
      case 0x79:
      case 0x7A:
      case 0x7B: {          // Monochrome rectangle (16x16)
        gpu_unai.PacketBuffer.U4[2] = u32_to_le32(0x00100010);
        PT driver = gpuTileDrivers[(Blending_Mode | gpu_unai.Masking | Blending | (gpu_unai.PixelMSB>>3)) >> 1];
        s32 w = 0, h = 0;
        gpuDrawT(packet, driver, &w, &h);
        gput_sum(cpu_cycles_sum, cpu_cycles, gput_sprite(w, h));
      } break;

      case 0x7C:
      case 0x7D:
      case 0x7E:
      case 0x7F: {          // Textured rectangle (16x16)
        gpu_unai.PacketBuffer.U4[3] = u32_to_le32(0x00100010);
        textured_sprite(cpu_cycles_sum, cpu_cycles);
      } break;

#ifdef TEST
      case 0x80:          //  vid -> vid
        gpuMoveImage(packet);
        break;

      case 0xA0:          //  sys -> vid
      {
        u32 load_width = list[2] & 0xffff;
        u32 load_height = list[2] >> 16;
        u32 load_size = load_width * load_height;

        len += load_size / 2;
      } break;

      case 0xC0:
        break;
#else
      case 0x1F:                   //  irq?
      case 0x80 ... 0x9F:          //  vid -> vid
      case 0xA0 ... 0xBF:          //  sys -> vid
      case 0xC0 ... 0xDF:          //  vid -> sys
        // Handled by gpulib
        goto breakloop;
#endif
      case 0xE1 ... 0xE6: { // Draw settings
        u32 cmd_word = le32_to_u32(gpu_unai.PacketBuffer.U4[0]);
        ex_regs[(cmd_word >> 24) & 7] = cmd_word;
        gpuGP0Cmd_0xEx(gpu_unai, cmd_word);
      } break;
    }
    if (eye) {   // the second eye's timing doesn't count: the real GPU draws once
      cpu_cycles_sum = sum_eye0; cpu_cycles = last_eye0;
    }
    } // eye
    if (vram_saved)
      gpu_unai.vram = vram_saved;
    vram_saved = NULL;
  }

breakloop:
  if (vram_saved)
    gpu_unai.vram = vram_saved;
  ex_regs[1] &= ~0x1ff;
  ex_regs[1] |= gpu_unai.GPU_GP1 & 0x1ff;

  *cycles_sum_out += cpu_cycles_sum;
  *cycles_last = cpu_cycles;
  *last_cmd = cmd;
  return list - list_start;
}

void renderer_sync_ecmds(u32 *ecmds_)
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  // the funcs below expect LE
  uint32_t i, ecmds[8];
  for (i = 1; i <= 6; i++)
    ecmds[i] = HTOLE32(ecmds_[i]);
#else
  uint32_t *ecmds = ecmds_;
#endif
  if (!IS_OLD_RENDERER()) {
    int dummy;
    renderer_do_cmd_list(&ecmds[1], 6, ecmds, &dummy, &dummy, &dummy);
  }
  else
    oldunai_renderer_sync_ecmds(ecmds);
}

void renderer_update_caches(int x, int y, int w, int h, int state_changed)
{
}

void renderer_flush_queues(void)
{
}

void renderer_set_interlace(int enable, int is_odd)
{
  renderer_notify_screen_change(&gpu.screen);
}

#include "../../frontend/plugin_lib.h"
// Handle any gpulib settings applicable to gpu_unai:
void renderer_set_config(const struct rearmed_cbs *cbs)
{
  gpu_unai.vram = (le16_t *)gpu.vram;
  gpu_unai.config.old_renderer  = cbs->gpu_unai.old_renderer;
  gpu_unai.config.ilace_force   = cbs->gpu_unai.ilace_force;
  gpu_unai.config.lighting      = cbs->gpu_unai.lighting;
  gpu_unai.config.fast_lighting = cbs->gpu_unai.fast_lighting;
  gpu_unai.config.blending      = cbs->gpu_unai.blending;
  gpu_unai.config.dithering     = cbs->dithering != 0;
  gpu_unai.config.force_dithering = cbs->dithering >> 1;

  renderer_notify_screen_change(&gpu.screen);
  oldunai_renderer_set_config(cbs);
}

// vim:shiftwidth=2:expandtab
