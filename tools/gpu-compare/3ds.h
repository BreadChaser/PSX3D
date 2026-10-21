/* fake libctru for running glc3d.c in the ARM simulator: no GPU, CPU work only */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64; typedef int32_t s32; typedef int16_t s16;
static inline void *linearAlloc(size_t n) { return calloc(1, n); }
static inline void linearFree(void *p) { free(p); }
static inline u32 linearSpaceFree(void) { return 32u << 20; }
static inline int GSPGPU_FlushDataCache(const void *p, u32 n) { (void)p; (void)n; return 0; }
static inline u64 svcGetSystemTick(void) { return 0; }
static inline int GSPGPU_InvalidateDataCache(const void *p, u32 n) { (void)p; (void)n; return 0; }
int GX_TextureCopy(u32 *src, u32 sd, u32 *dst, u32 dd, u32 n, u32 flags);
#ifndef GX_BUFFER_DIM
#define GX_BUFFER_DIM(w,h) (((h)<<16)|((w)&65535))
#endif
