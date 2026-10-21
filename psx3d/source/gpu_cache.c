// Measure citro3d's implicit whole-heap flush separately from GPU work.
#include "psx3d.h"
#include "c3d_abi.h"
#include "../../core/plugins/gpu_pica/gpu_pica.h"

static u64 heap_ticks;

Result __real_GSPGPU_FlushDataCache(const void *p, u32 size);

Result __wrap_GSPGPU_FlushDataCache(const void *p, u32 size)
{
	extern u32 __ctru_linear_heap, __ctru_linear_heap_size;
	bool heap = (u32)p == __ctru_linear_heap && size == __ctru_linear_heap_size;
	u64 start = heap ? svcGetSystemTick() : 0;
	Result rc = __real_GSPGPU_FlushDataCache(p, size);
	if (heap)
		heap_ticks += svcGetSystemTick() - start;
	return rc;
}

u64 video_take_heap_flush_ticks(void)
{
	u64 ticks = heap_ticks;
	heap_ticks = 0;
	return ticks;
}

// Menus retain citro3d's conservative heap flush. Texture decoding/uploads already flush their writes.
static bool targeted;
static C3D_BufInfo *vertex_info;
static struct { uintptr_t begin, end; } ranges[16];
static unsigned nranges;

void video_cache_targeted(bool enabled)
{
	targeted = enabled;
	if (enabled && !vertex_info) vertex_info = C3D_GetBufInfo();
}

static void add_range(const void *p, size_t size)
{
	if (!size) return;
	uintptr_t begin = (uintptr_t)p, end = begin + size;
	for (unsigned i = 0; i < nranges; i++) {
		if (begin <= ranges[i].end && end >= ranges[i].begin) {
			if (begin < ranges[i].begin) ranges[i].begin = begin;
			if (end > ranges[i].end) ranges[i].end = end;
			return;
		}
	}
	if (nranges == sizeof(ranges)/sizeof(ranges[0])) {
		for (unsigned i = 0; i < nranges; i++)
			svcStoreProcessDataCache(CUR_PROCESS_HANDLE, (u32)ranges[i].begin,
				ranges[i].end - ranges[i].begin);
		nranges = 0;
	}
	ranges[nranges].begin = begin;
	ranges[nranges++].end = end;
}

static void vertex_range(unsigned first, unsigned count)
{
	C3D_BufInfo *info = vertex_info;
	for (int i = 0; i < info->bufCount; i++) {
		C3D_BufCfg *buf = &info->buffers[i];
		unsigned stride = (buf->flags[1] >> 16) & 0xff;
		extern u32 __ctru_linear_heap;
		const u8 *heap = (const u8 *)__ctru_linear_heap;
		const u8 *base = heap + (info->base_paddr + buf->offset - osConvertVirtToPhys(heap));
		add_range(base + first * stride, count * stride);
	}
}

void __real_C3D_DrawElements(GPU_Primitive_t primitive, int count, int type, const void *indices);
void __wrap_C3D_DrawElements(GPU_Primitive_t primitive, int count, int type, const void *indices)
{
	if (targeted && count > 0) {
		unsigned lo = ~0u, hi = 0;
		for (int i = 0; i < count; i++) {
			unsigned v = type == C3D_UNSIGNED_SHORT ? ((const u16 *)indices)[i] : ((const u8 *)indices)[i];
			if (v < lo) lo = v;
			if (v > hi) hi = v;
		}
		vertex_range(lo, hi - lo + 1);
		add_range(indices, count * (type == C3D_UNSIGNED_SHORT ? 2 : 1));
	}
	__real_C3D_DrawElements(primitive, count, type, indices);
}

static void flush_ranges(void)
{
	if (renderer_is_gpu())
		pica_flush_vertices();
	for (unsigned i = 0; i < nranges; i++)
		svcStoreProcessDataCache(CUR_PROCESS_HANDLE, (u32)ranges[i].begin,
			ranges[i].end - ranges[i].begin);
	nranges = 0;
}

u8 video_cache_command_list(const void *commands, u32 bytes, u8 flags)
{
	if (targeted || (flags & GX_CMDLIST_FLUSH)) {
		// The command list is CPU-written. Clean it here instead of asking
		// the GSP thread to clean every split list before starting the GPU.
		svcStoreProcessDataCache(CUR_PROCESS_HANDLE, (u32)commands, bytes);
		flags &= ~GX_CMDLIST_FLUSH;
	}
	return flags;
}

u8 video_cache_frame_end(u8 flags)
{
	if (targeted) {
		C2D_Flush(); // emit the final UI batch before the citro2d end hook resets it
		flush_ranges();
		flags |= GX_CMDLIST_FLUSH;
	}
	return flags;
}

// citro3d updates the shader uniforms on every draw, twice (vertex and
// geometry stage), scanning 96 + 4 dirty flags a byte at a time; nearly
// always nothing is dirty. Skip the call then: the commands are the same. The shader constants' dirty flag is private to citro3d,
// so the two functions that set it are wrapped to note it.
static bool shader_consts_dirty[2] = { true, true };
void __real_C3D_UpdateUniforms(GPU_SHADER_TYPE type);
void __real_C3Di_LoadShaderUniforms(shaderInstance_s *si);
void __real_C3Di_DirtyUniforms(GPU_SHADER_TYPE type);

void __wrap_C3Di_LoadShaderUniforms(shaderInstance_s *si)
{
	shader_consts_dirty[si->dvle->type] = true;
	__real_C3Di_LoadShaderUniforms(si);
}

void __wrap_C3Di_DirtyUniforms(GPU_SHADER_TYPE type)
{
	shader_consts_dirty[type] = true;
	__real_C3Di_DirtyUniforms(type);
}

static bool any_set(const bool *flags, unsigned n)
{
	if (!((uintptr_t)flags & 3) && !(n & 3)) {
		const u32 *w = (const u32 *)flags;
		u32 any = 0;
		for (unsigned i = 0; i < n / 4; i++)
			any |= w[i];
		return any;
	}
	for (unsigned i = 0; i < n; i++)
		if (flags[i])
			return true;
	return false;
}

void __wrap_C3D_UpdateUniforms(GPU_SHADER_TYPE type)
{
	if (!shader_consts_dirty[type] && !C3D_BoolUnifsDirty[type] &&
		!any_set(C3D_FVUnifDirty[type], C3D_FVUNIF_COUNT) && !any_set(C3D_IVUnifDirty[type], C3D_IVUNIF_COUNT))
		return;
	shader_consts_dirty[type] = false;
	__real_C3D_UpdateUniforms(type);
}
