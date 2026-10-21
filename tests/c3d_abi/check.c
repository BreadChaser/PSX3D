// Runs c3d_abi_mismatch() from psx3d/source/c3d_abi.h on the real C3D_Context layout (the
// static assertions in that header are checked while compiling this file) with a valid
// context and with each field it checks corrupted. Built for ARM Linux; see run.sh.
#include <stdio.h>
#include <string.h>
#include "c3d_abi.h"

static C3D_Context good(void *buf, void *queue)
{
	C3D_Context c;
	memset(&c, 0, sizeof c);
	c.flags = 0xff804211;          /* what C3D_Init 1.7.1-2 stores before C3Di_RenderQueueInit */
	c.cmdBuf = buf;
	c.cmdBufSize = 0x100000 / 4;
	c.gxQueue.entries = queue;
	c.gxQueue.maxEntries = 32;
	return c;
}

int main(void)
{
	static u32 buf[16] __attribute__((aligned(16)));
	static gxCmdEntry_s queue[32];
	int bad = 0;
	C3D_Context c = good(buf, queue);
	const char *r = c3d_abi_mismatch(&c, 0x100000);
	printf("valid context      -> %s\n", r ? r : "ok");
	bad += r != NULL;
	c = good(buf, queue); c.flags = 0;                  r = c3d_abi_mismatch(&c, 0x100000); printf("inactive           -> %s\n", r); bad += !r;
	c = good(buf, queue); c.cmdBuf = NULL;              r = c3d_abi_mismatch(&c, 0x100000); printf("no command buffer  -> %s\n", r); bad += !r;
	c = good(buf, queue); c.cmdBuf = (u32 *)((char *)buf + 4); r = c3d_abi_mismatch(&c, 0x100000); printf("misaligned buffer  -> %s\n", r); bad += !r;
	c = good(buf, queue); c.cmdBufSize = 0x100000;      r = c3d_abi_mismatch(&c, 0x100000); printf("size in bytes      -> %s\n", r); bad += !r;
	c = good(buf, queue); c.gxQueue.entries = NULL;     r = c3d_abi_mismatch(&c, 0x100000); printf("no queue entries   -> %s\n", r); bad += !r;
	c = good(buf, queue); c.gxQueue.maxEntries = 64;    r = c3d_abi_mismatch(&c, 0x100000); printf("64-entry queue     -> %s\n", r); bad += !r;
	printf("sizeof(C3D_Context) = %zu (static asserts passed at compile time)\n", sizeof c);
	return bad;
}
