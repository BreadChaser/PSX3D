/* Real 3D in the native replay. GPL-2.0-or-later.
 * Environment: STEREO_STRENGTH (0..1, default 1; 0 = mirroring off, mono),
 * STEREO_AGE (oldest GTE depth accepted, frames, default 2, as the 3DS),
 * GPU_FRAMES as replay.c (also selects the right-eye PPMs, frame-NNNN-right.ppm),
 * GPU_HASH_VRAM also hashes the right eye's VRAM. */
#include "citro3d.h"
#include "stereo-sim.h"
#include "../../psx3d/source/gpustereo.h"
#include <assert.h>

extern const unsigned char cmd_lengths[256];
C3D_Tex *sim_rt_tex(void);
const void *sim_buf_data(void);
void sim_buf_set(const void *d);

static C3D_Tex right;
static C3D_RenderTarget *right_rt;
static C3D_Tex *canon;
static int on = -1, max_age = 2;
static float strength = 1;
static int seeded;
static uint16_t *zs;
static unsigned zn, zpos;
static unsigned long long draws, mirrored, moved_draws;

static void init(void)
{
    if (on >= 0) return;
    if (getenv("STEREO_STRENGTH")) strength = atof(getenv("STEREO_STRENGTH"));
    if (getenv("STEREO_AGE")) max_age = atoi(getenv("STEREO_AGE"));
    on = strength > 0;
    if (!on) return;
    assert(gs_core_alloc());
    assert(C3D_TexInit(&right, 1024, 512, GPU_RGBA5551));
    right_rt = C3D_RenderTargetCreateFromTex(&right, GPU_TEXFACE_2D, 0, -1);
}

void stereo_sim_depths(const void *data, unsigned bytes)
{
    init();
    free(zs);
    zs = malloc(bytes + 16); assert(zs);
    memcpy(zs, data, bytes);
    zn = bytes / 16; zpos = 0;
}

static inline int sext11(uint32_t v) { return ((int32_t)(v << 21)) >> 21; }

// The 3DS main thread's capture (gpustereo.c __wrap_psx3d_depth_cmd), from
// the depths the trace recorded at the same moment.
void stereo_sim_list(const uint32_t *list, int n, const uint32_t *ex)
{
    init();
    if (!on || !zs) return;
    uint32_t e3 = ex[3], e4 = ex[4], e5 = ex[5];
    for (int pos = 0; pos < n;) {
        uint32_t cmd = list[pos] >> 24;
        int len = 1 + cmd_lengths[cmd];
        if (cmd >= 0x48 && cmd < 0x50) {
            int k = 2; while (pos + k + 1 < n && (list[pos + k + 1] & 0xf000f000) != 0x50005000) k++;
            len += k - 2;
        } else if (cmd >= 0x58 && cmd < 0x60) {
            int k = 2; while (pos + k * 2 < n && (list[pos + k * 2] & 0xf000f000) != 0x50005000) k++;
            len += (k - 2) * 2;
        }
        if (cmd == 0xe3) e3 = list[pos];
        if (cmd == 0xe4) e4 = list[pos];
        if (cmd == 0xe5) e5 = list[pos];
        if (cmd >= 0x20 && cmd < 0x40) {
            assert(zpos < zn);
            const uint16_t *e = zs + zpos++ * 8;
            uint16_t z[4];
            for (int k = 0; k < 4; k++)
                z[k] = e[k * 2 + 1] <= max_age ? e[k * 2] : 0;
            gs_capture(list + pos, cmd, e3, e4, e5, z);
        }
        pos += len;
    }
    assert(zpos == zn);
    free(zs); zs = NULL;
}

static int is_canonical(const C3D_Tex *t)
{
    return t && t != &right && t->format == GPU_RGBA5551 && t->width == 1024 && t->height == 512;
}

void stereo_sim_draw(int primitive, int first, int n)
{
    init();
    C3D_Tex *t = sim_rt_tex();
    draws++;
    if (!on || !is_canonical(t)) { C3D_DrawArrays(primitive, first, n); return; }
    canon = t;
    if (!seeded) { memcpy(right.data, canon->data, 1024 * 512 * 2); seeded = 1; }
    C3D_DrawArrays(primitive, first, n);
    const void *data = sim_buf_data();
    gs_vtx *shifted = malloc(n * sizeof(gs_vtx)); assert(shifted);
    int moved = gs_shift((const gs_vtx *)data + first, shifted, n);
    C3D_RenderTarget back = { canon };
    C3D_FrameDrawOn(right_rt);
    if (moved) { sim_buf_set(shifted - first); moved_draws++; }
    C3D_DrawArrays(primitive, first, n);
    sim_buf_set(data);
    C3D_FrameDrawOn(&back);
    free(shifted);
    mirrored++;
}

static int in_canon(const void *p)
{
    return canon && (const uint8_t *)p >= (const uint8_t *)canon->data &&
        (const uint8_t *)p < (const uint8_t *)canon->data + 1024 * 512 * 2;
}

int stereo_sim_copy(u32 *src, u32 sd, u32 *dst, u32 dd, u32 n, u32 flags)
{
    int rc = GX_TextureCopy(src, sd, dst, dd, n, flags);
    if (on > 0 && seeded && in_canon(dst)) {
        uint8_t *r = right.data, *c = canon->data;
        GX_TextureCopy(in_canon(src) ? (u32 *)(r + ((uint8_t *)src - c)) : src, sd,
            (u32 *)(r + ((uint8_t *)dst - c)), dd, n, flags);
    }
    return rc;
}

static int selected(int frame)
{
    const char *s = getenv("GPU_FRAMES");
    if (!s) return 1;
    while (*s) {
        char *end; long v = strtol(s, &end, 10);
        if (end == s) break;
        if (v == frame) return 1;
        s = *end == ',' ? end + 1 : end;
    }
    return 0;
}

void stereo_sim_frame(const char *dir, const int *info)
{
    init();
    if (!on) return;
    int frame = info[0], x = info[2], y = info[3], w = info[4], h = info[5];
    gs_core_frame(strength);
    unsigned lookups, found, shifted;
    gs_core_counts(&lookups, &found, &shifted);
    fprintf(stderr, "stereo %d focus %.1f corners %u found %u shifted %u draws %llu mirrored %llu moved %llu\n",
        frame, gs_focus(), lookups, found, shifted, draws, mirrored, moved_draws);
    draws = mirrored = moved_draws = 0;
    if (getenv("GPU_HASH_VRAM")) {
        unsigned long long hash = 1469598103934665603ull;
        for (int yy = 0; yy < 512; yy++) for (int xx = 0; xx < 1024; xx++) {
            uint16_t p = ((uint16_t *)right.data)[sim_tiled(xx, yy, 1024)];
            hash = (hash ^ (p & 255)) * 1099511628211ull;
            hash = (hash ^ (p >> 8)) * 1099511628211ull;
        }
        fprintf(stderr, "frame %d right VRAM hash %016llx\n", frame, hash);
    }
    if (!selected(frame) || w <= 0 || h <= 0 || (info[1] & (1 << 21))) return;
    char path[4096]; snprintf(path, sizeof(path), "%s/frame-%04d-right.ppm", dir, frame);
    FILE *f = fopen(path, "wb"); assert(f);
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int r = 0; r < h; r++) for (int c = 0; c < w; c++) {
        uint16_t p = ((uint16_t *)right.data)[sim_tiled((x + c) & 1023, (y + r) & 511, 1024)];
        unsigned char rgb[3] = { (p >> 11) * 255 / 31, ((p >> 6) & 31) * 255 / 31, ((p >> 1) & 31) * 255 / 31 };
        assert(fwrite(rgb, 1, 3, f) == 3);
    }
    assert(fclose(f) == 0);
}
