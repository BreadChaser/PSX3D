// Direct PICA200 commands for the GPU renderer's draws
// (docs/dev/pica-direct.md).
//
// citro3d's C3D_DrawArrays sends every dirty state block in full, one
// GPUCMD call per register, then the eleven draw commands the same way.
// Here the same registers are written straight into citro3d's command
// buffer: every dirty block in full, in citro3d's order, and the draw
// commands from a template. citro3d's context stays the authority: its
// dirty flags are cleared exactly as C3Di_UpdateContext clears them, and
// anything else (citro2d, a new program, frame buffer or attribute layout,
// a restore after the HOME Menu) still goes through citro3d.
//
// Only writing the registers that changed is NOT the same on the real
// GPU: it changes pixels at polygon edges although the register state is
// identical. devtools/pica-direct proves the command stream with the real
// citro3d 1.7.1 code.
#include "../vendor/citro3d/internal.h"
#include "psx3d.h"
#include <string.h>

#define BLOCKS (C3DiF_Viewport | C3DiF_Scissor | C3DiF_Effect | C3DiF_TexAll | C3DiF_TexStatus | C3DiF_TexEnvAll)
#define ROOM 192   // words the largest direct draw can need

static const u16 tex_base[3] = { GPUREG_TEXUNIT0_BORDER_COLOR, GPUREG_TEXUNIT1_BORDER_COLOR, GPUREG_TEXUNIT2_BORDER_COLOR };
static const u16 tex_type[3] = { GPUREG_TEXUNIT0_TYPE, GPUREG_TEXUNIT1_TYPE, GPUREG_TEXUNIT2_TYPE };

static inline u32 *put(u32 *p, u32 reg, u32 mask, u32 value)
{
	p[0] = value;
	p[1] = GPUCMD_HEADER(0, mask, reg);
	return p + 2;
}

// consecutive registers
static __attribute__((noinline)) u32 *put_words(u32 *p, u32 reg, const u32 *v, int n)
{
	for (int i = 0; i < n; i++)
		p = put(p, reg + i, 0xF, v[i]);
	return p;
}

// the registers C3Di_EffectBind writes
static u32 *put_effect(u32 *p, const C3D_Effect *e)
{
	p = put(p, GPUREG_DEPTHMAP_ENABLE, 0xF, e->zBuffer ? 1 : 0);
	p = put(p, GPUREG_FACECULLING_CONFIG, 0xF, e->cullMode & 0x3);
	p = put(p, GPUREG_DEPTHMAP_SCALE, 0xF, e->zScale);
	p = put(p, GPUREG_DEPTHMAP_SCALE + 1, 0xF, e->zOffset);
	p = put(p, GPUREG_FRAGOP_ALPHA_TEST, 0xF, e->alphaTest);
	p = put(p, GPUREG_FRAGOP_ALPHA_TEST + 1, 0xF, e->stencilMode);
	p = put(p, GPUREG_FRAGOP_ALPHA_TEST + 2, 0xF, e->stencilOp);
	p = put(p, GPUREG_FRAGOP_ALPHA_TEST + 3, 0xF, e->depthTest);
	p = put(p, GPUREG_GAS_DELTAZ_DEPTH, 0x8, (u32)GPU_MAKEGASDEPTHFUNC((e->depthTest >> 4) & 7) << 24);
	p = put(p, GPUREG_BLEND_COLOR, 0xF, e->blendClr);
	p = put(p, GPUREG_BLEND_FUNC, 0xF, e->alphaBlend);
	p = put(p, GPUREG_LOGIC_OP, 0xF, e->clrLogicOp);
	p = put(p, GPUREG_COLOR_OPERATION, 0x7, e->fragOpMode);
	p = put(p, GPUREG_FRAGOP_SHADOW, 0xF, e->fragOpShadow);
	p = put(p, GPUREG_EARLYDEPTH_TEST1, 0x1, e->earlyDepth ? 1 : 0);
	p = put(p, GPUREG_EARLYDEPTH_TEST2, 0xF, e->earlyDepth ? 1 : 0);
	p = put(p, GPUREG_EARLYDEPTH_FUNC, 0x1, e->earlyDepthFunc);
	p = put(p, GPUREG_EARLYDEPTH_DATA, 0x7, e->earlyDepthRef);
	return p;
}

// C3D_UpdateUniforms would send nothing for either shader: no float,
// integer or boolean uniform is dirty. Loading a program's constants also
// marks its boolean uniforms dirty, so they need no check of their own.
static bool uniforms_clean(void)
{
	const u32 *f = (const u32 *)C3D_FVUnifDirty, *iv = (const u32 *)C3D_IVUnifDirty;
	if (((uintptr_t)f | (uintptr_t)iv) & 3)
		return false;
	u32 any = C3D_BoolUnifsDirty[0] | C3D_BoolUnifsDirty[1] | iv[0] | iv[1];
	for (int i = 0; i < 2 * C3D_FVUNIF_COUNT / 4; i += 4)
		any |= f[i] | f[i + 1] | f[i + 2] | f[i + 3];
	return !any;
}

// C3D_DrawArrays' commands after the state: primitive, count and first
// vertex are filled in
static const u32 draw_template[22] = {
	0, GPUCMD_HEADER(0, 0x2, GPUREG_PRIMITIVE_CONFIG),
	1, GPUCMD_HEADER(0, 0xF, GPUREG_RESTART_PRIMITIVE),
	0x80000000, GPUCMD_HEADER(0, 0xF, GPUREG_INDEXBUFFER_CONFIG),
	0, GPUCMD_HEADER(0, 0xF, GPUREG_NUMVERTICES),
	0, GPUCMD_HEADER(0, 0xF, GPUREG_VERTEX_OFFSET),
	1, GPUCMD_HEADER(0, 0x1, GPUREG_GEOSTAGE_CONFIG2),
	0, GPUCMD_HEADER(0, 0x1, GPUREG_START_DRAW_FUNC0),
	1, GPUCMD_HEADER(0, 0xF, GPUREG_DRAWARRAYS),
	1, GPUCMD_HEADER(0, 0x1, GPUREG_START_DRAW_FUNC0),
	0, GPUCMD_HEADER(0, 0x1, GPUREG_GEOSTAGE_CONFIG2),
	1, GPUCMD_HEADER(0, 0xF, GPUREG_VTX_FUNC),
};

void __real_C3D_DrawArrays(GPU_Primitive_t primitive, int first, int size);
// Called by the real 3D wrapper (gpustereo.c), which owns --wrap=C3D_DrawArrays.
void picadirect_draw_arrays(GPU_Primitive_t primitive, int first, int size)
{
	C3D_Context *ctx = C3Di_GetContext();
	u32 flags = ctx->flags;
	// Anything beyond the renderer's own state changes goes through citro3d.
	if ((flags & ~(BLOCKS | C3DiF_Active | C3DiF_DrawUsed)) || ctx->lightEnv || ctx->fixedAttribDirty ||
		(ctx->texEnvBuf & 7) == GPU_GAS || !gpuCmdBuf || gpuCmdBufSize - gpuCmdBufOffset < ROOM) {
		__real_C3D_DrawArrays(primitive, first, size);
		return;
	}
	if (flags & C3DiF_TexAll)
		for (int i = 0; i < 3; i++)
			if ((flags & C3DiF_Tex(i)) && ctx->tex[i] && !C3Di_TexIs2D(ctx->tex[i])) {
				__real_C3D_DrawArrays(primitive, first, size);
				return;
			}

	u32 *p = gpuCmdBuf + gpuCmdBufOffset;
	if (flags & C3DiF_Viewport) {
		p = put_words(p, GPUREG_VIEWPORT_WIDTH, ctx->viewport, 4);
		p = put_words(p, GPUREG_VIEWPORT_XY, ctx->viewport + 4, 1);
	}
	if (flags & C3DiF_Scissor)
		p = put_words(p, GPUREG_SCISSORTEST_MODE, ctx->scissor, 3);
	if (flags & C3DiF_Effect)
		p = put_effect(p, &ctx->effect);
	if (flags & C3DiF_TexAll) {
		u32 units = 0;
		for (int i = 0; i < 3; i++) {
			C3D_Tex *tex = ctx->tex[i];
			if (!tex)
				continue;
			units |= BIT(i);
			if (flags & C3DiF_Tex(i)) {   // the registers C3Di_SetTex writes for a 2D texture
				u32 r[5] = { tex->border, tex->dim, tex->param, tex->lodParam, osConvertVirtToPhys(tex->data) >> 3 };
				p = put_words(p, tex_base[i], r, 5);
				p = put(p, tex_type[i], 0xF, tex->fmt);
			}
		}
		ctx->texConfig = (ctx->texConfig & ~7) | units | BIT(16);
		flags |= C3DiF_TexStatus;
	}
	if (flags & C3DiF_TexStatus) {
		p = put(p, GPUREG_TEXUNIT_CONFIG, 0xB, ctx->texConfig);
		if (ctx->texConfig & BIT(16)) {   // texture cache clear
			ctx->texConfig &= ~BIT(16);
			p = put(p, GPUREG_TEXUNIT_CONFIG, 0x4, BIT(16));
		}
		p = put(p, GPUREG_TEXUNIT0_SHADOW, 0xF, ctx->texShadow);
	}
	if (flags & C3DiF_TexEnvAll)
		for (int i = 0; i < 6; i++)
			if (flags & C3DiF_TexEnv(i))
				p = put_words(p, GPUREG_TEXENV0_SOURCE + (i >= 4 ? i + 2 : i) * 8, (const u32 *)&ctx->texEnv[i],
					sizeof(C3D_TexEnv) / 4);
	ctx->flags = (flags & ~BLOCKS) | C3DiF_DrawUsed;
	gpuCmdBufOffset = p - gpuCmdBuf;

	// shader uniforms the renderer changed
	if (!uniforms_clean()) {
		C3D_UpdateUniforms(GPU_VERTEX_SHADER);
		C3D_UpdateUniforms(GPU_GEOMETRY_SHADER);
	}

	p = gpuCmdBuf + gpuCmdBufOffset;
	memcpy(p, draw_template, sizeof(draw_template));
	p[0] = primitive;
	p[6] = size;
	p[8] = first;
	gpuCmdBufOffset += sizeof(draw_template) / 4;
}
