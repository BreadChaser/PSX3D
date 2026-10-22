/* Real 3D in the native replay. GPL-2.0-or-later.
 * Environment: STEREO_STRENGTH (0..1, default 1; 0 = mirroring off, mono),
 * STEREO_AGE (oldest GTE depth accepted, frames, default 2, as the 3DS),
 * GPU_FRAMES as replay.c (also selects the right-eye PPMs, frame-NNNN-right.ppm),
 * GPU_HASH_VRAM also hashes the right eye's VRAM. */
#include "citro3d.h"
#include "stereo-sim.h"
#include "../../psx3d/source/gpustereo.h"
#include <assert.h>
#include <math.h>

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
static FILE *dump;      // STEREO_DUMP: frame, triangle vertices before/after the shift
static FILE *tris;      // STEREO_TRIS: every triangle drawn into PS1 VRAM (draw order)
static int dump_frame;

static void init(void)
{
    if (on >= 0) return;
    if (getenv("STEREO_STRENGTH")) strength = atof(getenv("STEREO_STRENGTH"));
    if (getenv("STEREO_AGE")) max_age = atoi(getenv("STEREO_AGE"));
    on = strength > 0;
    if (getenv("STEREO_DUMP")) dump = fopen(getenv("STEREO_DUMP"), "w");
    if (getenv("STEREO_TRIS")) tris = fopen(getenv("STEREO_TRIS"), "w");
    if (!on) return;
    assert(gs_core_alloc());
    assert(C3D_TexInitVRAM(&right, 1024, 512, GPU_RGBA5551));
    right_rt = C3D_RenderTargetCreateFromTex(&right, GPU_TEXFACE_2D, 0, -1);
    assert(right_rt);   // a render target needs a VRAM texture (as on the console)
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


// STEREO_HOLES: per frame, each eye's triangles (fills excepted) rasterized
// as the GPU does (pixel centres, top-left rule) into coverage maps of PS1
// VRAM; a hole is an uncovered pixel with covered pixels within 3 px on
// both sides (left/right or above/below): a crack line. Counted for
// polygons alone (a gap in a 3D mesh) and for everything drawn (a gap
// nothing at all fills). The left eye is the mono picture: its holes are
// the game's own; the right eye should have no more.
static uint8_t *hole_map[2];   // bit 0: polygon coverage, bit 1: anything
static int holes_on = -1;
static void holes_tri(uint8_t *m, const gs_vtx *v, uint8_t bits)
{
    double x[3], y[3];
    for (int k = 0; k < 3; k++) { x[k] = v[k].x / 16.0; y[k] = v[k].y / 16.0; }
    double area = (x[1] - x[0]) * (y[2] - y[0]) - (y[1] - y[0]) * (x[2] - x[0]);
    if (area == 0) return;
    if (area < 0) { double t = x[1]; x[1] = x[2]; x[2] = t; t = y[1]; y[1] = y[2]; y[2] = t; }
    int x0 = (int)floor(fmin(x[0], fmin(x[1], x[2]))), x1 = (int)ceil(fmax(x[0], fmax(x[1], x[2])));
    int y0 = (int)floor(fmin(y[0], fmin(y[1], y[2]))), y1 = (int)ceil(fmax(y[0], fmax(y[1], y[2])));
    x0 = x0 < 0 ? 0 : x0; y0 = y0 < 0 ? 0 : y0; x1 = x1 > 1024 ? 1024 : x1; y1 = y1 > 512 ? 512 : y1;
    for (int py = y0; py < y1; py++)
        for (int px = x0; px < x1; px++) {
            double cx = px + .5, cy = py + .5, b[3];
            int ok = 1;
            for (int e = 0; e < 3 && ok; e++) {
                int a = (e + 1) % 3, c = (e + 2) % 3;
                b[e] = (x[c] - x[a]) * (cy - y[a]) - (y[c] - y[a]) * (cx - x[a]);
                int tl = y[c] < y[a] || (y[c] == y[a] && x[c] > x[a]);
                ok = b[e] > 0 || (b[e] == 0 && tl);
            }
            if (ok) m[py * 1024 + px] |= bits;
        }
}
static void holes_draw(const gs_vtx *l, const gs_vtx *r, int n)
{
    if (holes_on < 0) holes_on = getenv("STEREO_HOLES") != NULL;
    if (!holes_on) return;
    if (!hole_map[0]) { hole_map[0] = calloc(1024 * 512, 1); hole_map[1] = calloc(1024 * 512, 1); }
    for (int i = 0; i + 3 <= n; i += 3) {
        int poly = 1, fill = (l[i].rgba >> 24) == 0 && l[i].u == l[i + 1].u && l[i + 1].u == l[i + 2].u &&
            l[i].v == l[i + 1].v && l[i + 1].v == l[i + 2].v;
        for (int k = 0; k < 3; k++) poly &= (l[i + k].x & 15) == 8 && (l[i + k].y & 15) == 7;
        if (fill) continue;
        uint8_t bits = 2 | poly;
        holes_tri(hole_map[0], &l[i], bits);
        holes_tri(hole_map[1], &r[i], bits);
    }
}
static unsigned holes_count(const uint8_t *m, uint8_t bit)
{
    unsigned n = 0;
    for (int y = 0; y < 512; y++)
        for (int x = 0; x < 1024; x++) {
            if (m[y * 1024 + x] & bit) continue;
            int l = 0, r = 0, u = 0, d = 0;
            for (int k = 1; k <= 3; k++) {
                l |= x - k >= 0 && (m[y * 1024 + x - k] & bit);
                r |= x + k < 1024 && (m[y * 1024 + x + k] & bit);
                u |= y - k >= 0 && (m[(y - k) * 1024 + x] & bit);
                d |= y + k < 512 && (m[(y + k) * 1024 + x] & bit);
            }
            n += (l && r) || (u && d);
        }
    return n;
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
    if (tris) {
        const gs_vtx *src = (const gs_vtx *)data + first;
        for (int i = 0; i + 3 <= n; i += 3) {
            fprintf(tris, "T %d %llu", dump_frame, draws);
            for (int k = 0; k < 3; k++)
                fprintf(tris, " %d %d %d %d %08x", src[i + k].x, src[i + k].y, src[i + k].u, src[i + k].v, src[i + k].rgba);
            fputc('\n', tris);
        }
    }
    gs_vtx *shifted = malloc(n * sizeof(gs_vtx)); assert(shifted);
    int moved = gs_shift((const gs_vtx *)data + first, shifted, n);
    C3D_RenderTarget back = { canon };
    C3D_FrameDrawOn(right_rt);
    if (moved) { sim_buf_set(shifted - first); moved_draws++; }
    // STEREO_DUMP_ALL: also sprites, rectangles, lines, uploads (kind S, last column);
    // STEREO_DUMP_FIRST / _LAST: only those frames
    if (dump && (!getenv("STEREO_DUMP_FIRST") || dump_frame >= atoi(getenv("STEREO_DUMP_FIRST"))) &&
        (!getenv("STEREO_DUMP_LAST") || dump_frame <= atoi(getenv("STEREO_DUMP_LAST")))) {
        const gs_vtx *src = (const gs_vtx *)data + first;
        int all = getenv("STEREO_DUMP_ALL") != NULL;
        for (int i = 0; i + 3 <= n; i += 3) {
            int poly = 1;
            for (int k = 0; k < 3; k++) poly &= (src[i + k].x & 15) == 8 && (src[i + k].y & 15) == 7;
            if (!poly && !all) continue;
            fprintf(dump, "%d %llu", dump_frame, draws);
            for (int k = 0; k < 3; k++)
                fprintf(dump, " %d %d %d", src[i + k].x, src[i + k].y, shifted[i + k].x - src[i + k].x);
            if (all) fprintf(dump, " %c %d %d %u", poly ? 'P' : 'S', src[i].u, src[i].v, (unsigned)(src[i].rgba >> 24));
            fputc('\n', dump);
        }
    }
    holes_draw((const gs_vtx *)data + first, shifted, n);
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
    dump_frame = frame + 1;
    if (dump) fprintf(dump, "F %d %d %d %d %d\n", frame, x, y, w, h);
    if (tris) fprintf(tris, "F %d %d %d %d %d %d\n", frame, x, y, w, h, info[1]);
    gs_core_frame(strength, w > 0 ? w : 320);
    if (!(info[1] & (1 << 21)))
        gs_core_shown(x, y, w, h);
    if (getenv("STEREO_LAYDBG")) {
        char st[1024]; gs_layer_status(st, sizeof(st));
        extern unsigned long long lay_cells, lay_prims, lay_tris;
        fprintf(stderr, "layers %d shown %d,%d flat %d work tris %llu prims %llu cells %llu %s\n", frame, x, y, gs_picture_flat(),
            lay_tris, lay_prims, lay_cells, st);
        lay_cells = lay_prims = lay_tris = 0;
    }
    unsigned lookups, found, shifted, rejected;
    gs_core_counts(&lookups, &found, &shifted, &rejected);
    extern unsigned gs_missed __attribute__((weak));   // (older gpustereo_core.c: none)
    fprintf(stderr, "stereo %d blend %.2f focus %.1f corners %u found %u shifted %u rejected %u missed %u draws %llu mirrored %llu moved %llu\n",
        frame, gs_layer_blend(), gs_focus(), lookups, found, shifted, rejected, &gs_missed ? gs_missed : 0, draws, mirrored, moved_draws);
    if (&gs_missed) gs_missed = 0;
    draws = mirrored = moved_draws = 0;
    if (holes_on > 0) {
        fprintf(stderr, "holes %d polygons left %u right %u all left %u right %u\n", frame,
            holes_count(hole_map[0], 1), holes_count(hole_map[1], 1), holes_count(hole_map[0], 2), holes_count(hole_map[1], 2));
        if (getenv("STEREO_HOLES_DUMP") && atoi(getenv("STEREO_HOLES_DUMP")) == frame) {
            // coverage maps: polygon coverage of left and right, one PGM each
            for (int e = 0; e < 2; e++) {
                char path[4096]; snprintf(path, sizeof(path), "%s/holes-%04d-%s.pgm", dir, frame, e ? "right" : "left");
                FILE *f = fopen(path, "wb"); assert(f);
                fprintf(f, "P5 1024 512 255\n");
                for (int i = 0; i < 1024 * 512; i++) fputc(hole_map[e][i] & 1 ? 255 : hole_map[e][i] & 2 ? 128 : 0, f);
                fclose(f);
            }
        }
        memset(hole_map[0], 0, 1024 * 512); memset(hole_map[1], 0, 1024 * 512);
    }
    if (getenv("GPU_HASH_VRAM")) {
        unsigned long long hash = 1469598103934665603ull;
        for (int yy = 0; yy < 512; yy++) for (int xx = 0; xx < 1024; xx++) {
            uint16_t p = ((uint16_t *)right.data)[sim_tiled(xx, yy, 1024)];
            hash = (hash ^ (p & 255)) * 1099511628211ull;
            hash = (hash ^ (p >> 8)) * 1099511628211ull;
        }
        fprintf(stderr, "frame %d right VRAM hash %016llx\n", frame, hash);
    }
    if (selected(frame) && w > 0 && h > 0 && !(info[1] & (1 << 21))) {
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
    // STEREO_POISON: at each flip the right eye's new back buffer (the
    // picture shown until now) becomes magenta; what the game doesn't draw
    // into it (a stale strip) is magenta when it is shown again
    static int px = -1, py = -1, pw, ph;
    if (getenv("STEREO_POISON") && (x != px || y != py)) {
        if (px >= 0)
            for (int r = 0; r < ph; r++) for (int c = 0; c < pw; c++)
                ((uint16_t *)right.data)[sim_tiled((px + c) & 1023, (py + r) & 511, 1024)] = 0xf83e;   // mask bit clear: mask-checking games draw over it
        px = x; py = y; pw = w; ph = h;
    }
}
