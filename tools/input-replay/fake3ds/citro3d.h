#pragma once
#include "3ds.h"
typedef struct { void *data; u16 width, height; } C3D_Tex;
typedef struct { int dummy; } C3D_RenderTarget, C3D_AttrInfo, C3D_BufInfo, C3D_TexEnv, DVLB_s_inner;
typedef union { struct { float w, z, y, x; }; float c[4]; } C3D_FVec;
typedef struct { C3D_FVec r[4]; } C3D_Mtx;
typedef struct { unsigned numDVLE; int DVLE[2]; } DVLB_s;
typedef struct { void *vertexShader; } shaderProgram_s;
enum { GPU_RGBA8, GPU_TEXFACE_2D, GPU_FLOAT, GPU_UNSIGNED_BYTE, GPU_VERTEX_SHADER, GPU_CULL_NONE, GPU_WRITE_COLOR,
	GPU_SCISSOR_NORMAL, GPU_SCISSOR_DISABLE, GPU_BLEND_ADD, GPU_TRIANGLES, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR,
	GPU_MODULATE, GPU_REPLACE, GPU_NEAREST, GPU_LINEAR, GPU_REPEAT, GPU_CLAMP_TO_EDGE };
typedef enum { GPU_ZERO, GPU_ONE, GPU_SRC_COLOR, GPU_ONE_MINUS_SRC_COLOR, GPU_DST_COLOR, GPU_ONE_MINUS_DST_COLOR,
	GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_DST_ALPHA, GPU_ONE_MINUS_DST_ALPHA, GPU_SRC_ALPHA_SATURATE } GPU_BLENDFACTOR;
typedef enum { GPU_NEVER, GPU_ALWAYS, GPU_EQUAL, GPU_NOTEQUAL, GPU_LESS, GPU_LEQUAL, GPU_GREATER, GPU_GEQUAL } GPU_TESTFUNC;
extern unsigned sim_c3d_draws;
static inline bool C3D_TexInit(C3D_Tex *t, int w, int h, int f) { (void)f; t->data = calloc(w * h, 4); t->width = w; t->height = h; return t->data != NULL; }
static inline bool C3D_TexInitVRAM(C3D_Tex *t, int w, int h, int f) { return C3D_TexInit(t, w, h, f); }
static inline void C3D_TexDelete(C3D_Tex *t) { free(t->data); t->data = NULL; }
static inline void C3D_TexSetFilter(C3D_Tex *t, int a, int b) { (void)t; (void)a; (void)b; }
static inline void C3D_TexSetWrap(C3D_Tex *t, int a, int b) { (void)t; (void)a; (void)b; }
static inline void C3D_TexBind(int u, C3D_Tex *t) { (void)u; (void)t; }
static C3D_RenderTarget sim_rt; static C3D_AttrInfo sim_ai; static C3D_BufInfo sim_bi; static C3D_TexEnv sim_env;
static inline C3D_RenderTarget *C3D_RenderTargetCreateFromTex(C3D_Tex *t, int f, int l, int d) { (void)t; (void)f; (void)l; (void)d; return &sim_rt; }
static inline void C3D_RenderTargetDelete(C3D_RenderTarget *t) { (void)t; }
static inline bool C3D_FrameDrawOn(C3D_RenderTarget *t) { (void)t; return true; }
static inline void C3D_BindProgram(shaderProgram_s *p) { (void)p; }
static inline C3D_AttrInfo *C3D_GetAttrInfo(void) { return &sim_ai; }
static inline void AttrInfo_Init(C3D_AttrInfo *a) { (void)a; }
static inline int AttrInfo_AddLoader(C3D_AttrInfo *a, int r, int f, int n) { (void)a; (void)r; (void)f; (void)n; return 0; }
static inline C3D_BufInfo *C3D_GetBufInfo(void) { return &sim_bi; }
static inline void BufInfo_Init(C3D_BufInfo *b) { (void)b; }
static inline int BufInfo_Add(C3D_BufInfo *b, const void *d, int s, int n, u64 p) { (void)b; (void)d; (void)s; (void)n; (void)p; return 0; }
static inline void C3D_FVUnifMtx4x4(int t, int id, const C3D_Mtx *m) { (void)t; (void)id; (void)m; }
static inline void C3D_CullFace(int m) { (void)m; }
static inline void C3D_DepthTest(bool e, int f, int w) { (void)e; (void)f; (void)w; }
static inline void C3D_SetViewport(u32 x, u32 y, u32 w, u32 h) { (void)x; (void)y; (void)w; (void)h; }
static inline void C3D_SetScissor(int m, u32 l, u32 t, u32 r, u32 b) { (void)m; (void)l; (void)t; (void)r; (void)b; }
static inline void C3D_AlphaBlend(int a, int b, int c, int d, int e, int f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
static inline void C3D_AlphaTest(bool e, int f, int r) { (void)e; (void)f; (void)r; }
static inline C3D_TexEnv *C3D_GetTexEnv(int i) { (void)i; return &sim_env; }
static inline void C3D_TexEnvInit(C3D_TexEnv *e) { (void)e; }
static inline void C3D_TexEnvSrc(C3D_TexEnv *e, int m, int a, int b, int c) { (void)e; (void)m; (void)a; (void)b; (void)c; }
static inline void C3D_TexEnvFunc(C3D_TexEnv *e, int m, int f) { (void)e; (void)m; (void)f; }
static inline void C3D_DrawArrays(int p, int f, int n) { (void)p; (void)f; (void)n; sim_c3d_draws++; }
static inline float C3D_GetCmdBufUsage(void) { return 0; }
static inline bool C3D_FrameSplit(u8 f) { (void)f; return true; }
static inline float C3D_GetDrawingTime(void) { return 0; }
static DVLB_s sim_dvlb = { 2, {0, 1} };
static inline DVLB_s *DVLB_ParseFile(u32 *d, u32 n) { (void)d; (void)n; return &sim_dvlb; }
static inline void DVLB_Free(DVLB_s *d) { (void)d; }
static inline void shaderProgramInit(shaderProgram_s *p) { (void)p; }
static inline int shaderProgramSetVsh(shaderProgram_s *p, void *d) { (void)p; (void)d; return 0; }
static inline void shaderProgramFree(shaderProgram_s *p) { (void)p; }
static inline int shaderInstanceGetUniformLocation(void *s, const char *n) { (void)s; (void)n; return 0; }
static inline void C3D_FrameEnd(u8 f) { (void)f; }
static inline bool C3D_FrameBegin(u8 f) { (void)f; return true; }
enum { GPU_RGBA5551_ = 100, GPU_SHORT = 101, GPU_ONE_MINUS_CONSTANT_ALPHA = 102, GPU_CONSTANT_ALPHA = 103,
	GPU_BLEND_REVERSE_SUBTRACT = 104, C3D_RGB = 105, C3D_Alpha = 106, GPU_TEVSCALE_2 = 107, GPU_TEVOP_A_ONE_MINUS_SRC_ALPHA = 108 };
#define GPU_RGBA5551 GPU_RGBA5551_
static inline void C3D_BlendingColor(u32 c) { (void)c; }
static inline void C3D_TexEnvOpAlpha(C3D_TexEnv *e, int a, int b, int c) { (void)e; (void)a; (void)b; (void)c; }
static inline void C3D_TexEnvScale(C3D_TexEnv *e, int m, int s) { (void)e; (void)m; (void)s; }
static inline void C3D_FVUnifSet(int t, int id, float x, float y, float z, float w) { (void)t; (void)id; (void)x; (void)y; (void)z; (void)w; }
typedef u64 u64_alias;
#define GPUREG_TEXUNIT_CONFIG 0x80
#define GPUCMD_AddMaskedWrite(r, m, v) ((void)(r), (void)(m), (void)(v))

enum { GPU_PREVIOUS=270, GPU_TEXTURE1, GPU_ADD_SIGNED, GPU_CONSTANT, GPU_ADD, GPU_TEVSCALE_4, GPU_TEVOP_A_SRC_R, GPU_TEVOP_A_SRC_G, GPU_TEVOP_A_SRC_ALPHA };
static inline void C3D_TexEnvColor(C3D_TexEnv *e,u32 c) {(void)e;(void)c;}

enum { GPU_TEXTURE2=290, GPU_INTERPOLATE, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_ALPHA, GPU_TEVOP_A_SRC_B };
static inline void C3D_TexEnvOpRgb(C3D_TexEnv *e,int a,int b,int c) {(void)e;(void)a;(void)b;(void)c;}
