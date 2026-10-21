// Guard for the citro3d internals PSX3D depends on.
//
// renderqueue.c replaces citro3d's render queue and writes into its private
// C3D_Context (vendor/citro3d/internal.h, a copy from citro3d 1.7.1-2);
// gpu_cache.c wraps private C3Di_* functions and relies on how often
// C3D_UpdateUniforms scans its dirty flags. A different citro3d can compile and
// link against both and still corrupt the context, so the build is pinned:
//   - the assertions below fail the compile when the headers or the copy change;
//   - tools/build/toolchain.py (run by `make`) compares libcitro3d.a, the
//     headers and the size of its __C3D_Context with tools/build/toolchain.lock
//     and C3D_ABI_CONTEXT_SIZE;
//   - c3d_abi_mismatch() checks the live context at startup.
// Values are from the pinned SDK (citro3d 1.7.1-2, libctru 2.7.0-1, armv6k).
#pragma once
#include "../vendor/citro3d/internal.h"
#include <c3d/uniforms.h>
#include <stddef.h>
#include <stdint.h>

// Also read by tools/build/toolchain.py: libcitro3d.a's __C3D_Context must be this big.
#define C3D_ABI_CONTEXT_SIZE 720

#define C3D_ABI_CHECK(cond) _Static_assert(cond, "citro3d ABI changed: " #cond \
	" (see tools/build/toolchain.lock and docs/building.md, Upgrading the SDK)")

C3D_ABI_CHECK(sizeof(C3D_Context) == C3D_ABI_CONTEXT_SIZE);
// What renderqueue.c reads and writes, and the fields that fix the later layout.
C3D_ABI_CHECK(offsetof(C3D_Context, gxQueue) == 0);
C3D_ABI_CHECK(offsetof(C3D_Context, cmdBuf) == 20);
C3D_ABI_CHECK(offsetof(C3D_Context, cmdBufSize) == 24);
C3D_ABI_CHECK(offsetof(C3D_Context, cmdBufUsage) == 28);
C3D_ABI_CHECK(offsetof(C3D_Context, flags) == 32);
C3D_ABI_CHECK(offsetof(C3D_Context, fb) == 472);
C3D_ABI_CHECK(offsetof(C3D_Context, fixedAttribs) == 524);
// Public types embedded in the context (and the queue renderqueue.c allocates).
C3D_ABI_CHECK(sizeof(gxCmdQueue_s) == 20);
C3D_ABI_CHECK(sizeof(gxCmdEntry_s) == 32);
C3D_ABI_CHECK(sizeof(C3D_Effect) == 52);
C3D_ABI_CHECK(sizeof(C3D_AttrInfo) == 24);
C3D_ABI_CHECK(sizeof(C3D_BufInfo) == 152);
C3D_ABI_CHECK(sizeof(C3D_TexEnv) == 20);
C3D_ABI_CHECK(sizeof(C3D_FrameBuf) == 16);
C3D_ABI_CHECK(sizeof(C3D_Tex) == 24);
C3D_ABI_CHECK(C3DiF_Active == 1);
// gpu_cache.c skips C3D_UpdateUniforms by checking these dirty flags itself.
C3D_ABI_CHECK(C3D_FVUNIF_COUNT == 96);
C3D_ABI_CHECK(C3D_IVUNIF_COUNT == 4);

// Called from C3Di_RenderQueueInit, the last step of C3D_Init, before anything
// uses the context. Returns what looks wrong, or NULL. Each check is something
// C3D_Init 1.7.1-2 sets (confirmed in the library's disassembly): the context
// is active, the command list is a 16-byte aligned buffer of `cmdBufBytes`
// (cmdBufSize counts words), and the GX queue holds 32 entries.
static inline const char *c3d_abi_mismatch(const C3D_Context *ctx, size_t cmdBufBytes)
{
	if (!(ctx->flags & C3DiF_Active))
		return "context flags";
	if (!ctx->cmdBuf || ((uintptr_t)ctx->cmdBuf & 15))
		return "command buffer";
	if (ctx->cmdBufSize != cmdBufBytes / 4)
		return "command buffer size";
	if (!ctx->gxQueue.entries || ctx->gxQueue.maxEntries != 32)
		return "GX queue";
	return NULL;
}
