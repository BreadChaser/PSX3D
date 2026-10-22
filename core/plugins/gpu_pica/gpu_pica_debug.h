/* Developer diagnostics shared by the renderer and its host fixtures.
 * Values are stable: private worker scripts pass gldbg=N. See docs/dev/dev-switches.md.
 * This header introduces no storage or runtime code. */
#ifndef GPU_PICA_DEBUG_H
#define GPU_PICA_DEBUG_H
enum {
    PICA_DEBUG_VRAM = 32,
    PICA_DEBUG_NO_DRAW = 1024,
    PICA_DEBUG_SKIP_COMMANDS = 2048,
    PICA_DEBUG_NO_PRIMITIVES = 4096,
    PICA_DEBUG_NO_SPLIT = 8192,
    PICA_DEBUG_FLIP_READBACK = 16384,
    PICA_DEBUG_NO_MASK_PASS = 32768,
    PICA_DEBUG_NO_MERGE = 65536,
    PICA_DEBUG_REFERENCE_CACHE = 131072,
    PICA_DEBUG_RTT_OLD = 262144,
    PICA_DEBUG_NO_REGION_CACHE = 524288,
    PICA_DEBUG_RTT_NEW = 1048576,
    PICA_DEBUG_EAGER_FRAME = 4194304,
    PICA_DEBUG_PHASE_TIMING = 8388608,
    PICA_DEBUG_PACK_COPY = 16777216,
    PICA_DEBUG_COARSE_BOUNDS = 33554432
};
#endif
