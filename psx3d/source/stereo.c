// Real stereo 3D by depth-image-based rendering.
//
// The core builds a coarse depth map of each PS1 frame (see
// core/libpcsxcore/psx3d_depth.h). Here the PS1 picture is drawn through a
// 40x30 mesh whose points are shifted left or right per eye by the depth
// under them, so the GPU does all the per-pixel work and the PS1 frame is
// only rendered once.
#include <stdio.h>
#include <string.h>
#include "psx3d.h"
#include "stereo_shbin.h"
#include "../../core/libpcsxcore/psx3d_depth.h"

#define GX 40
#define GY 30
#define NV ((GX + 1) * (GY + 1))
#define NI (GX * GY * 6)

typedef struct { float x, y, z, u, v; } vtx_t;

static DVLB_s *dvlb;
static shaderProgram_s prog;
static int uloc_proj;
static C3D_AttrInfo attr;
static C3D_Mtx proj;
static vtx_t *vbuf[2];          // one mesh per eye, linear memory
static u16 *ibuf;
static float disp[NV];          // per mesh point, in top-screen pixels
static float fc_smooth = -1.0f; // convergence (0 nearest .. 1 farthest), smoothed
static u32 last_seq;

bool stereo_init(void)
{
	dvlb = DVLB_ParseFile((u32 *)stereo_shbin, stereo_shbin_size);
	if (!dvlb)
		return false;
	shaderProgramInit(&prog);
	shaderProgramSetVsh(&prog, &dvlb->DVLE[0]);
	uloc_proj = shaderInstanceGetUniformLocation(prog.vertexShader, "projection");

	AttrInfo_Init(&attr);
	AttrInfo_AddLoader(&attr, 0, GPU_FLOAT, 3);   // position
	AttrInfo_AddLoader(&attr, 1, GPU_FLOAT, 2);   // texcoord

	Mtx_OrthoTilt(&proj, 0.0f, 400.0f, 240.0f, 0.0f, 0.0f, 1.0f, true);

	for (int e = 0; e < 2; e++)
		if (!(vbuf[e] = linearAlloc(NV * sizeof(vtx_t))))
			return false;
	if (!(ibuf = linearAlloc(NI * sizeof(u16))))
		return false;
	int n = 0;
	for (int y = 0; y < GY; y++)
		for (int x = 0; x < GX; x++) {
			u16 a = y * (GX + 1) + x, b = a + 1, c = a + GX + 1, d = c + 1;
			ibuf[n++] = a; ibuf[n++] = c; ibuf[n++] = b;
			ibuf[n++] = b; ibuf[n++] = c; ibuf[n++] = d;
		}
	GSPGPU_FlushDataCache(ibuf, NI * sizeof(u16));
	return true;
}

void stereo_exit(void)
{
	for (int e = 0; e < 2; e++)
		if (vbuf[e])
			linearFree(vbuf[e]);
	if (ibuf)
		linearFree(ibuf);
	if (dvlb) {
		shaderProgramFree(&prog);
		DVLB_Free(dvlb);
	}
	dvlb = NULL;
}

#define HUD_POP 0.4f      // overlays come this fraction of max depth out of the screen

// classify the 2x2 cells around a mesh point: HUD/text wins if it covers
// half of them (keeps overlays crisp), then the nearest real depth (so
// foreground edges don't get pulled back), else it's backdrop
static u16 sample(const psx3d_depthmap *m, float px, float py)
{
	int cx = (int)(px / PSX3D_DM_CELL - 0.5f), cy = (int)(py / PSX3D_DM_CELL - 0.5f);
	u16 best = 0;        // highest rank = nearest
	int overlay = 0;
	for (int dy = 0; dy < 2; dy++)
		for (int dx = 0; dx < 2; dx++) {
			int x = cx + dx, y = cy + dy;
			if (x < 0) x = 0;
			if (y < 0) y = 0;
			if (x >= PSX3D_DM_W) x = PSX3D_DM_W - 1;
			if (y >= PSX3D_DM_H) y = PSX3D_DM_H - 1;
			u16 z = m->z[y][x];
			if (z == PSX3D_Z_OVERLAY) overlay++;
			else if (z != PSX3D_Z_NONE && z != PSX3D_Z_FAR && z > best) best = z;
		}
	if (overlay >= 2)
		return PSX3D_Z_OVERLAY;
	return best ? best : PSX3D_Z_FAR;
}

// 0 = drawn last (nearest) .. 1 = drawn first (farthest)
static inline float farness(const psx3d_depthmap *m, u16 z)
{
	return 1.0f - (float)(z - 2) / (m->ranks ? m->ranks : 1);
}

// convergence: roughly the nearest 10% of the scene sits at the screen
// surface and everything else recedes behind it
static float convergence(const psx3d_depthmap *m, int cw, int ch)
{
	// returns the far-fraction below which the nearest 10% of the scene lies
	static u32 hist[256];
	u32 total = 0;
	memset(hist, 0, sizeof(hist));
	for (int y = 0; y < ch; y++)
		for (int x = 0; x < cw; x++) {
			u16 z = m->z[y][x];
			if (z != PSX3D_Z_NONE && z != PSX3D_Z_OVERLAY && z != PSX3D_Z_FAR) {
				int b = (int)(farness(m, z) * 255.0f);
				hist[b < 0 ? 0 : b > 255 ? 255 : b]++;
				total++;
			}
		}
	if (total < 8)
		return -1.0f;
	u32 want = total / 10, acc = 0;
	for (int i = 0; i < 256; i++) {
		acc += hist[i];
		if (acc > want)
			return i / 255.0f;
	}
	return 1.0f;
}

// Compute the per-point disparity for this frame. Returns false when the
// game hasn't produced any depth data (2D game, or not drawing 3D yet).
bool stereo_prepare(float max_px)
{
	const psx3d_depthmap *m = psx3d_depth_get();
	if (!m->seq || !m->w || !m->h)
		return false;
	int cw = (m->w + PSX3D_DM_CELL - 1) / PSX3D_DM_CELL;
	int ch = (m->h + PSX3D_DM_CELL - 1) / PSX3D_DM_CELL;
	if (cw > PSX3D_DM_W) cw = PSX3D_DM_W;
	if (ch > PSX3D_DM_H) ch = PSX3D_DM_H;

	if (m->seq != last_seq) {
		float fc = convergence(m, cw, ch);
		if (fc >= 0.0f)
			fc_smooth = fc_smooth >= 0.0f ? fc_smooth + (fc - fc_smooth) * 0.1f : fc;
		last_seq = m->seq;
	}
	if (fc_smooth < 0.0f)
		return false;

	for (int gy = 0; gy <= GY; gy++)
		for (int gx = 0; gx <= GX; gx++) {
			u16 z = sample(m, (float)gx * m->w / GX, (float)gy * m->h / GY);
			float d;
			if (z == PSX3D_Z_OVERLAY)
				d = -HUD_POP * max_px;           // HUD in front of the screen
			else if (z == PSX3D_Z_FAR)
				d = max_px;                      // backdrop at the back
			else {
				// nearest 10% at the screen surface, the rest recedes
				float f = farness(m, z);
				d = fc_smooth < 0.99f ? max_px * (f - fc_smooth) / (1.0f - fc_smooth) : 0.0f;
				if (d < -0.3f * max_px) d = -0.3f * max_px;
			}
			disp[gy * (GX + 1) + gx] = d;
		}
	return true;
}

// Draw the game texture through the mesh for one eye (-1 left, +1 right).
// Must be called inside a citro2d scene; restores citro2d state afterwards.
void stereo_draw(C3D_Tex *tex, float x0, float dw, unsigned frame_w, unsigned frame_h, int eye)
{
	vtx_t *v = vbuf[eye > 0];
	float tu = (float)frame_w / tex->width, tv = (float)frame_h / tex->height;

	for (int gy = 0; gy <= GY; gy++)
		for (int gx = 0; gx <= GX; gx++) {
			int i = gy * (GX + 1) + gx;
			float fx = (float)gx / GX, fy = (float)gy / GY;
			v[i].x = x0 + fx * dw + eye * 0.5f * disp[i];
			v[i].y = fy * 240.0f;
			v[i].z = 0.5f;
			v[i].u = fx * tu;
			v[i].v = 1.0f - fy * tv;
		}
	GSPGPU_FlushDataCache(v, NV * sizeof(vtx_t));

	C2D_Flush();
	C3D_BindProgram(&prog);
	C3D_SetAttrInfo(&attr);
	C3D_BufInfo *bi = C3D_GetBufInfo();
	BufInfo_Init(bi);
	BufInfo_Add(bi, v, sizeof(vtx_t), 2, 0x10);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uloc_proj, &proj);
	C3D_TexBind(0, tex);
	C3D_TexEnv *env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
	C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
	for (int i = 1; i < 6; i++)
		C3D_TexEnvInit(C3D_GetTexEnv(i));
	C3D_DepthTest(false, GPU_GEQUAL, GPU_WRITE_ALL);
	C3D_CullFace(GPU_CULL_NONE);
	C3D_DrawElements(GPU_TRIANGLES, NI, C3D_UNSIGNED_SHORT, ibuf);
	C2D_Prepare();
}

// Bottom-screen preview of the depth map (40x30 blocks): near is bright,
// far is dark, backdrop blue, HUD/overlays orange.
void stereo_draw_preview(float x0, float y0, float cell)
{
	const psx3d_depthmap *m = psx3d_depth_get();
	if (!m->seq || !m->w || !m->h || fc_smooth < 0.0f)
		return;
	for (int gy = 0; gy < GY; gy++)
		for (int gx = 0; gx < GX; gx++) {
			u16 z = sample(m, (gx + 0.5f) * m->w / GX, (gy + 0.5f) * m->h / GY);
			u32 c;
			if (z == PSX3D_Z_OVERLAY)
				c = C2D_Color32(255, 160, 40, 255);
			else if (z == PSX3D_Z_FAR)
				c = C2D_Color32(20, 30, 90, 255);
			else {
				int v = 30 + (int)((1.0f - farness(m, z)) * 225.0f);
				c = C2D_Color32(v, v, v, 255);
			}
			C2D_DrawRectSolid(x0 + gx * cell, y0 + gy * cell, 0.5f, cell, cell, c);
		}
}

// one log line: depth maps published so far and what the latest one contains
const char *stereo_stats(void)
{
	static char buf[128];
	const psx3d_depthmap *m = psx3d_depth_get();
	int depth = 0, overlay = 0, far = 0, none = 0;
	for (int y = 0; y < PSX3D_DM_H; y++)
		for (int x = 0; x < PSX3D_DM_W; x++) {
			u16 z = m->z[y][x];
			if (z == PSX3D_Z_NONE) none++;
			else if (z == PSX3D_Z_OVERLAY) overlay++;
			else if (z == PSX3D_Z_FAR) far++;
			else depth++;
		}
	snprintf(buf, sizeof(buf), "maps %lu (%ux%u) 3D polys %lu, cells: depth %d overlay %d far %d empty %d, convergence %.2f",
		m->seq, m->w, m->h, m->ranks, depth, overlay, far, none, fc_smooth);
	return buf;
}
