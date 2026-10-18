// Measure citro3d's implicit whole-heap flush separately from GPU work.
#include "psx3d.h"
#ifdef PSX3D_PICA
#include "../../core/plugins/gpu_pica/gpu_pica.h"
#endif

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
#ifdef PSX3D_PICA
	pica_flush_vertices();
#endif
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
