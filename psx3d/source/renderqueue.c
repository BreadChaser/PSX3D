// Adapted from citro3d 1.7.1 (zlib license in ../vendor/citro3d/LICENSE).
// Two command-list regions allow CPU recording while the previous frame runs.
#include "c3d_abi.h"
#include "psx3d.h"
#include <c3d/base.h>
#include <c3d/renderqueue.h>
#include <stdlib.h>

static C3D_RenderTarget *firstTarget, *lastTarget;
static C3D_RenderTarget *linkedTarget[3];

static TickCounter gpuTime, cpuTime;
static gxCmdQueue_s extraQueue;
static gxCmdQueue_s *buildQueue, *pendingQueue;
static u32 *commandBase;
static size_t commandWords;
static LightEvent queueDone;
static u64 pendingWaitTicks;

// The caller may record the next frame, but shared resources must not be
// overwritten until this returns (vertices use two separate regions).
void video_gpu_wait_previous(void);
bool video_gpu_has_previous(void) { return pendingQueue != NULL; }
u64 video_take_pending_wait_ticks(void)
{
	u64 ticks = pendingWaitTicks;
	pendingWaitTicks = 0;
	return ticks;
}

static void runQueue(void)
{
	LightEvent_Clear(&queueDone);
	pendingQueue = buildQueue;
	gxCmdQueueRun(buildQueue);
	if (!buildQueue->numEntries) LightEvent_Signal(&queueDone);
}

static bool inFrame, inSafeTransfer, measureGpuTime;
static bool needSwapTop, needSwapBot, isTopStereo;
static float framerate = 60.0f;
static float framerateCounter[2] = { 60.0f, 60.0f };
static u32 frameCounter[2];
static void (* frameEndCb)(void*);
static void* frameEndCbData;

static void C3Di_RenderTargetDestroy(C3D_RenderTarget* target);

static bool framerateLimit(int id)
{
	framerateCounter[id] -= framerate;
	if (framerateCounter[id] <= 0.0f)
	{
		framerateCounter[id] += 60.0f;
		return true;
	}
	return false;
}

static void onVBlank0(C3D_UNUSED void* unused)
{
	if (framerateLimit(0))
		frameCounter[0]++;
}

static void onVBlank1(C3D_UNUSED void* unused)
{
	if (framerateLimit(1))
		frameCounter[1]++;
}

static void onQueueFinish(gxCmdQueue_s* queue)
{
	if (measureGpuTime)
	{
		osTickCounterUpdate(&gpuTime);
		measureGpuTime = false;
	}
	if (inSafeTransfer)
	{
		inSafeTransfer = false;
		if (inFrame)
		{
			gxCmdQueueStop(queue);
			gxCmdQueueClear(queue);
			pendingQueue = NULL;
		}
	}
	else
	{
		if (needSwapTop)
		{
			gfxScreenSwapBuffers(GFX_TOP, isTopStereo);
			needSwapTop = false;
		}
		if (needSwapBot)
		{
			gfxScreenSwapBuffers(GFX_BOTTOM, false);
			needSwapBot = false;
		}
	}
	LightEvent_Signal(&queueDone);
}

void C3D_FrameSync(void)
{
	u32 cur[2];
	u32 start[2] = { frameCounter[0], frameCounter[1] };
	do
	{
		gspWaitForAnyEvent();
		cur[0] = frameCounter[0];
		cur[1] = frameCounter[1];
	} while (cur[0]==start[0] || cur[1]==start[1]);
}

u32 C3D_FrameCounter(int id)
{
	return frameCounter[id];
}

// Only zero (poll) and -1 (wait) are used by the frame/transfer helpers.
static bool C3Di_WaitAndClearQueue(s64 timeout)
{
	if (!pendingQueue) return true;
	u64 start = svcGetSystemTick();
	if (timeout == 0) {
		if (!LightEvent_TryWait(&queueDone)) return false;
	} else {
		LightEvent_Wait(&queueDone);
	}
	gxCmdQueueStop(pendingQueue);
	gxCmdQueueClear(pendingQueue);
	pendingQueue = NULL;
	pendingWaitTicks += svcGetSystemTick() - start;
	return true;
}

void video_gpu_wait_previous(void)
{
	C3Di_WaitAndClearQueue(-1);
}

void C3Di_RenderQueueEnableVBlank(void)
{
	gspSetEventCallback(GSPGPU_EVENT_VBlank0, onVBlank0, NULL, false);
	gspSetEventCallback(GSPGPU_EVENT_VBlank1, onVBlank1, NULL, false);
}

void C3Di_RenderQueueDisableVBlank(void)
{
	gspSetEventCallback(GSPGPU_EVENT_VBlank0, NULL, NULL, false);
	gspSetEventCallback(GSPGPU_EVENT_VBlank1, NULL, NULL, false);
}

bool c3d_abi_failed;

void C3Di_RenderQueueInit(void)
{
	C3D_Context* ctx = C3Di_GetContext();

	const char *bad = c3d_abi_mismatch(ctx, VIDEO_CMDBUF_BYTES);
	if (bad) {
		// video_init() sees the flag and stops before anything is drawn.
		log_printf("citro3d private ABI mismatch (%s); build with the pinned SDK\n", bad);
		c3d_abi_failed = true;
		return;
	}

	C3Di_RenderQueueEnableVBlank();

	commandBase = ctx->cmdBuf;
	commandWords = ctx->cmdBufSize;
	buildQueue = &ctx->gxQueue;
	extraQueue.maxEntries = ctx->gxQueue.maxEntries;
	extraQueue.entries = malloc(extraQueue.maxEntries * sizeof(gxCmdEntry_s));
	LightEvent_Init(&queueDone, RESET_STICKY);
	gxCmdQueueSetCallback(&ctx->gxQueue, onQueueFinish, NULL);
	gxCmdQueueSetCallback(&extraQueue, onQueueFinish, NULL);
	GX_BindQueue(buildQueue);
}

void C3Di_RenderQueueExit(void)
{
	int i;
	C3D_RenderTarget *a, *next;

	C3Di_WaitAndClearQueue(-1);
	gxCmdQueueSetCallback(&C3Di_GetContext()->gxQueue, NULL, NULL);
	gxCmdQueueSetCallback(&extraQueue, NULL, NULL);
	GX_BindQueue(NULL);
	free(extraQueue.entries);
	extraQueue.entries = NULL;
	C3Di_GetContext()->cmdBuf = commandBase;
	C3Di_GetContext()->cmdBufSize = commandWords;

	C3Di_RenderQueueDisableVBlank();

	for (i = 0; i < 3; i ++)
		linkedTarget[i] = NULL;

	for (a = firstTarget; a; a = next)
	{
		next = a->next;
		C3Di_RenderTargetDestroy(a);
	}
}

void C3Di_RenderQueueWaitDone(void)
{
	C3Di_WaitAndClearQueue(-1);
}

float C3D_FrameRate(float fps)
{
	float old = framerate;
	if (fps > 0.0f && fps <= 60.0f)
	{
		framerate = fps;
		framerateCounter[0] = fps;
		framerateCounter[1] = fps;
	}
	return old;
}

bool C3D_FrameBegin(u8 flags)
{
	C3D_Context* ctx = C3Di_GetContext();
	if (inFrame) return false;

	if (flags & C3D_FRAME_SYNCDRAW)
		C3D_FrameSync();
	bool overlap = (flags & VIDEO_FRAME_OVERLAP) && extraQueue.entries;
	// Reuse the normal recording region when the previous frame has already
	// finished. Only switch banks when there is GPU work left to overlap.
	if (overlap && pendingQueue && LightEvent_TryWait(&queueDone))
		C3Di_WaitAndClearQueue(0);
	if (!overlap && !C3Di_WaitAndClearQueue((flags & C3D_FRAME_NONBLOCK) ? 0 : -1))
		return false;

	buildQueue = overlap && pendingQueue == &ctx->gxQueue ? &extraQueue : &ctx->gxQueue;
	gxCmdQueueClear(buildQueue);
	GX_BindQueue(buildQueue);
	ctx->cmdBufSize = extraQueue.entries ? commandWords / 2 : commandWords;
	ctx->cmdBuf = commandBase + (buildQueue == &extraQueue ? ctx->cmdBufSize : 0);
	inFrame = true;
	osTickCounterStart(&cpuTime);
	GPUCMD_SetBuffer(ctx->cmdBuf, ctx->cmdBufSize, 0);
	return true;
}

bool C3D_FrameDrawOn(C3D_RenderTarget* target)
{
	if (!inFrame) return false;

	target->used = true;
	C3D_SetFrameBuf(&target->frameBuf);
	C3D_SetViewport(0, 0, target->frameBuf.width, target->frameBuf.height);
	return true;
}

// citro3d updates its usage only at a split; the renderer checks it after
// every batch, so report the words recorded so far in this bank instead.
float __real_C3D_GetCmdBufUsage(void);
float __wrap_C3D_GetCmdBufUsage(void)
{
	C3D_Context* ctx = C3Di_GetContext();
	if (!inFrame) return __real_C3D_GetCmdBufUsage();
	return 1.0f - (float)(gpuCmdBufSize - gpuCmdBufOffset) / ctx->cmdBufSize;
}

static void splitFrame(u8 flags)
{
	u32 *cmdBuf, cmdBufSize;
	if (C3Di_SplitFrame(&cmdBuf, &cmdBufSize))
		GX_ProcessCommandList(cmdBuf, cmdBufSize*4, video_cache_command_list(cmdBuf, cmdBufSize*4, flags));
}

// The queue starts only at FrameEnd, so a split frees neither command words
// nor queue entries, and libctru panics when its 32 entries are full. A frame
// that outgrows either (thousands of batches, or a texture decode per split)
// is submitted and waited for first, like the renderer's readback syncs; its
// vertices stay valid. Leave room for a split plus a copy, then FrameEnd and
// presentation: a 24-bit movie transfer, the picture and three screens.
#define QUEUE_ROOM 10
static unsigned earlySubmits;

void C3D_FrameSplit(u8 flags)
{
	if (!inFrame) return;
	if (buildQueue->maxEntries - buildQueue->numEntries < QUEUE_ROOM || __wrap_C3D_GetCmdBufUsage() > 0.8f)
	{
		C3D_FrameEnd(0);
		C3D_FrameBegin(0);
		earlySubmits++;
		if (!(earlySubmits & (earlySubmits - 1)))   // 1, 2, 4, ...
			log_printf("GPU: frame too large for one submission, sent early (%u)\n", earlySubmits);
	}
	splitFrame(flags);
}

void C3D_FrameEnd(u8 flags)
{
	if (!inFrame) return;

	video_gpu_wait_previous();
	flags = video_cache_frame_end(flags);
	if (frameEndCb)
		frameEndCb(frameEndCbData);

	splitFrame(flags);
	GPUCMD_SetBuffer(NULL, 0, 0);
	osTickCounterUpdate(&cpuTime);
	inFrame = false;

	// Flush the entire linear memory if the user did not explicitly mandate to flush the command list
	if (!(flags & GX_CMDLIST_FLUSH))
	{
		extern u32 __ctru_linear_heap;
		extern u32 __ctru_linear_heap_size;
		GSPGPU_FlushDataCache((void*)__ctru_linear_heap, __ctru_linear_heap_size);
	}

	int i;
	C3D_RenderTarget* target;
	isTopStereo = false;
	for (i = 2; i >= 0; i --)
	{
		target = linkedTarget[i];
		if (!target || !target->used)
			continue;
		target->used = false;
		C3D_FrameBufTransfer(&target->frameBuf, target->screen, target->side, target->transferFlags);
		if (target->screen == GFX_TOP)
		{
			needSwapTop = true;
			if (target->side == GFX_RIGHT)
				isTopStereo = true;
		}
		else if (target->screen == GFX_BOTTOM)
			needSwapBot = true;
	}

	measureGpuTime = true;
	osTickCounterStart(&gpuTime);
	runQueue();
}

void C3D_FrameEndHook(void (* hook)(void*), void* param)
{
	frameEndCb = hook;
	frameEndCbData = param;
}

float C3D_GetDrawingTime(void)
{
	return osTickCounterRead(&gpuTime);
}

float C3D_GetProcessingTime(void)
{
	return osTickCounterRead(&cpuTime);
}

static C3D_RenderTarget* C3Di_RenderTargetNew(void)
{
	C3D_RenderTarget* target = (C3D_RenderTarget*)malloc(sizeof(C3D_RenderTarget));
	if (!target) return NULL;
	memset(target, 0, sizeof(C3D_RenderTarget));
	return target;
}

static void C3Di_RenderTargetFinishInit(C3D_RenderTarget* target)
{
	target->prev = lastTarget;
	target->next = NULL;
	if (lastTarget)
		lastTarget->next = target;
	if (!firstTarget)
		firstTarget = target;
	lastTarget = target;
}

C3D_RenderTarget* C3D_RenderTargetCreate(int width, int height, GPU_COLORBUF colorFmt, C3D_DEPTHTYPE depthFmt)
{
	GPU_DEPTHBUF depthFmtReal = GPU_RB_DEPTH16;
	void* depthBuf = NULL;
	void* colorBuf = vramAlloc(C3D_CalcColorBufSize(width,height,colorFmt));
	if (!colorBuf) goto _fail0;
	if (C3D_DEPTHTYPE_OK(depthFmt))
	{
		depthFmtReal = C3D_DEPTHTYPE_VAL(depthFmt);
		size_t depthSize = C3D_CalcDepthBufSize(width,height,depthFmtReal);
		vramAllocPos vramBank = addrGetVRAMBank(colorBuf);
		depthBuf = vramAllocAt(depthSize, vramBank ^ VRAM_ALLOC_ANY); // Attempt opposite bank first...
		if (!depthBuf) depthBuf = vramAllocAt(depthSize, vramBank); // ... if that fails, attempt same bank
		if (!depthBuf) goto _fail1;
	}

	C3D_RenderTarget* target = C3Di_RenderTargetNew();
	if (!target) goto _fail2;

	C3D_FrameBuf* fb = &target->frameBuf;
	C3D_FrameBufAttrib(fb, width, height, false);
	C3D_FrameBufColor(fb, colorBuf, colorFmt);
	target->ownsColor = true;
	if (depthBuf)
	{
		C3D_FrameBufDepth(fb, depthBuf, depthFmtReal);
		target->ownsDepth = true;
	}
	C3Di_RenderTargetFinishInit(target);
	return target;

_fail2:
	if (depthBuf) vramFree(depthBuf);
_fail1:
	vramFree(colorBuf);
_fail0:
	return NULL;
}

C3D_RenderTarget* C3D_RenderTargetCreateFromTex(C3D_Tex* tex, GPU_TEXFACE face, int level, C3D_DEPTHTYPE depthFmt)
{
	if (!addrIsVRAM(tex->data)) return NULL; // Render targets must be in VRAM
	C3D_RenderTarget* target = C3Di_RenderTargetNew();
	if (!target) return NULL;

	C3D_FrameBuf* fb = &target->frameBuf;
	C3D_FrameBufTex(fb, tex, face, level);

	if (C3D_DEPTHTYPE_OK(depthFmt))
	{
		GPU_DEPTHBUF depthFmtReal = C3D_DEPTHTYPE_VAL(depthFmt);
		size_t depthSize = C3D_CalcDepthBufSize(fb->width,fb->height,depthFmtReal);
		vramAllocPos vramBank = addrGetVRAMBank(tex->data);
		void* depthBuf = vramAllocAt(depthSize, vramBank ^ VRAM_ALLOC_ANY); // Attempt opposite bank first...
		if (!depthBuf) depthBuf = vramAllocAt(depthSize, vramBank); // ... if that fails, attempt same bank
		if (!depthBuf)
		{
			free(target);
			return NULL;
		}

		C3D_FrameBufDepth(fb, depthBuf, depthFmtReal);
		target->ownsDepth = true;
	}

	C3Di_RenderTargetFinishInit(target);
	return target;
}

void C3Di_RenderTargetDestroy(C3D_RenderTarget* target)
{
	if (target->ownsColor)
		vramFree(target->frameBuf.colorBuf);
	if (target->ownsDepth)
		vramFree(target->frameBuf.depthBuf);

	C3D_RenderTarget** prevNext = target->prev ? &target->prev->next : &firstTarget;
	C3D_RenderTarget** nextPrev = target->next ? &target->next->prev : &lastTarget;
	*prevNext = target->next;
	*nextPrev = target->prev;
	free(target);
}

void C3D_RenderTargetDelete(C3D_RenderTarget* target)
{
	if (inFrame)
		svcBreak(USERBREAK_PANIC); // Shouldn't happen.
	if (target->linked)
		C3D_RenderTargetDetachOutput(target);
	else
		C3Di_WaitAndClearQueue(-1);
	C3Di_RenderTargetDestroy(target);
}

void C3D_RenderTargetSetOutput(C3D_RenderTarget* target, gfxScreen_t screen, gfx3dSide_t side, u32 transferFlags)
{
	int id = 0;
	if (screen==GFX_BOTTOM) id = 2;
	else if (side==GFX_RIGHT) id = 1;
	if (linkedTarget[id])
	{
		linkedTarget[id]->linked = false;
		if (!inFrame)
			C3Di_WaitAndClearQueue(-1);
	}
	linkedTarget[id] = target;
	if (target)
	{
		target->linked = true;
		target->transferFlags = transferFlags;
		target->screen = screen;
		target->side = side;
	}
}

static void C3Di_SafeDisplayTransfer(u32* inadr, u32 indim, u32* outadr, u32 outdim, u32 flags)
{
	C3Di_WaitAndClearQueue(-1);
	inSafeTransfer = true;
	GX_DisplayTransfer(inadr, indim, outadr, outdim, flags);
	runQueue();
}

static void C3Di_SafeTextureCopy(u32* inadr, u32 indim, u32* outadr, u32 outdim, u32 size, u32 flags)
{
	C3Di_WaitAndClearQueue(-1);
	inSafeTransfer = true;
	GX_TextureCopy(inadr, indim, outadr, outdim, size, flags);
	runQueue();
}

static void C3Di_SafeMemoryFill(u32* buf0a, u32 buf0v, u32* buf0e, u16 control0, u32* buf1a, u32 buf1v, u32* buf1e, u16 control1)
{
	C3Di_WaitAndClearQueue(-1);
	inSafeTransfer = true;
	GX_MemoryFill(buf0a, buf0v, buf0e, control0, buf1a, buf1v, buf1e, control1);
	runQueue();
}

void C3D_SyncDisplayTransfer(u32* inadr, u32 indim, u32* outadr, u32 outdim, u32 flags)
{
	if (inFrame)
	{
		C3D_FrameSplit(0);
		GX_DisplayTransfer(inadr, indim, outadr, outdim, flags);
	} else
	{
		C3Di_SafeDisplayTransfer(inadr, indim, outadr, outdim, flags);
		gspWaitForPPF();
	}
}

void C3D_SyncTextureCopy(u32* inadr, u32 indim, u32* outadr, u32 outdim, u32 size, u32 flags)
{
	if (inFrame)
	{
		C3D_FrameSplit(0);
		GX_TextureCopy(inadr, indim, outadr, outdim, size, flags);
	} else
	{
		C3Di_SafeTextureCopy(inadr, indim, outadr, outdim, size, flags);
		gspWaitForPPF();
	}
}

void C3D_SyncMemoryFill(u32* buf0a, u32 buf0v, u32* buf0e, u16 control0, u32* buf1a, u32 buf1v, u32* buf1e, u16 control1)
{
	if (inFrame)
	{
		C3D_FrameSplit(0);
		GX_MemoryFill(buf0a, buf0v, buf0e, control0, buf1a, buf1v, buf1e, control1);
	} else
	{
		C3Di_SafeMemoryFill(buf0a, buf0v, buf0e, control0, buf1a, buf1v, buf1e, control1);
		gspWaitForPSC0();
	}
}
