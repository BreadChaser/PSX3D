// GPU command buffer room.
//
// libctru panics (svcBreak in GPUCMD_AddInternal) when a command finds the
// buffer full. The renderer checks the buffer only after a batch; CPU uploads
// (push_to_target), texture decodes and Real 3D's second eye draw without that
// check, so a frame of many small uploads could fill it: Crash Bandicoot's
// cold boot with Real 3D and the draw-order layers did (New 3DS, 2026-10-22;
// devtools/draworder-crash). No draw now starts with less than VIDEO_CMD_ROOM
// words left: the frame is submitted first, through renderqueue.c's
// C3D_FrameSplit guard (it submits once the buffer is 80% used, far below
// this). VIDEO_CMD_ROOM covers one draw with every citro3d state block dirty
// plus the split after it; the most the simulator saw for a Real 3D pair is
// about 300 words.
//
// PSX3D_COLD: linked after all other code, so nothing moves (picadirect.c's
// direct path keeps its code); this runs only when citro3d itself draws.
#include <3ds.h>
#include <citro3d.h>
#include "psx3d.h"

void __real_C3D_DrawArrays(GPU_Primitive_t primitive, int first, int size);
void picadirect_draw_arrays(GPU_Primitive_t primitive, int first, int size);

static inline bool short_of(u32 words)
{
	return gpuCmdBuf && gpuCmdBufSize - gpuCmdBufOffset < words;
}

// picadirect.c: a draw through citro3d itself
PSX3D_COLD void cmdroom_draw_arrays(GPU_Primitive_t primitive, int first, int size)
{
	if (short_of(VIDEO_CMD_ROOM))
		C3D_FrameSplit(0);
	__real_C3D_DrawArrays(primitive, first, size);
}

// gpustereo.c: the left eye's draw of a Real 3D pair. The right eye's follows
// with the frame buffer switched and its vertices in a ring that is reused two
// frames later, so both must reach the GPU in one submission: the room for
// both is made before the first.
PSX3D_COLD void cmdroom_draw_pair(GPU_Primitive_t primitive, int first, int size)
{
	if (short_of(2 * VIDEO_CMD_ROOM))
		C3D_FrameSplit(0);
	picadirect_draw_arrays(primitive, first, size);
}
