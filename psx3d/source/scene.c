// Approved round-3 backgrounds, ported from the project's UI mock. Every
// frame is a pure function of time. Menu-only: no thread, timer, game hook or
// retained linear allocation. See docs/dev/themes.md and THEME-BACKGROUNDS.md.
#include "scene.h"
#include "devtools.h"
#include "psx3d.h"
#include "scene_data.h"
#include "scene_palms.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "scene_batch.h"

#define PI 3.14159265358979323846f
#define TAU (2 * PI)
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
typedef struct {
    float x, y;
} point_t;
typedef struct { float x, y, d; } ray_t;
typedef struct {
    float epoch[COUNT(FL)];
    bool valid[COUNT(FL)];
    point_t p[2][COUNT(FL)][31];
} flow_cache_t;
typedef struct {
    flow_cache_t flow[2];
    ray_t star_ray[2][COUNT(STARS)], chart_ray[2][COUNT(CN)];
    float sine[1024], decay[256];
    C3D_Tex radial;
    C3D_Tex palms;
    Tex3DS_SubTexture palms_sub;
    bool palms_ok;
    Tex3DS_SubTexture radial_sub;
    C2D_Text glyph[12];
    C2D_TextBuf text;
    float grain[20][50];
    float mesh_px[29][37], mesh_py[29][37], mesh_h[29][37], mesh_z[29];
    float mesh_rise[37], mesh_side[37];
    struct { float x, y, width; u32 colour; } sun[170];
    const theme_t *sun_theme;
    unsigned sun_count;
    u32 mesh_colour[29][37];
    // Exact world-depth keys: floating point cancellation can give adjacent
    // frames slightly different zw values even within one scrolling row.
    // Four entries retain those values without quantizing time or geometry.
    struct { float world[4], height[4][37]; unsigned valid, next; } mesh_wave[29];
#ifdef __3DS__
    scene_glyph cached_glyph[12];
#endif

} workspace_t;
static workspace_t *ws;
static float w, depth, z, strength;
static bool bottom, additive;
static const theme_t *th;
static u64 samples[2], ticks[2], peak[2], report_tick;
static const theme_t *sample_theme;

static float clamp(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static float smooth(float a, float b, float v) {
    float x = clamp((v - a) / (b - a));
    return x * x * (3 - 2 * x);
}
static float fract(float x) { return x - floorf(x); }
static float sn(float x) {
    float f = x * (1024 / TAU);
    int k = (int)f;
    k -= f < k;
    float a = f - k;
    return ws->sine[k & 1023] + (ws->sine[(k + 1) & 1023] - ws->sine[k & 1023]) * a;
}
static float cs(float x) { return sn(x + PI / 2); }
static float decay(float x) {
    int k = (int)(x * 32);
    return k < 0 ? 1 : k > 255 ? 0 : ws->decay[k];
}
static u32 alpha(u32 c, float a) { return (c & 0x00ffffff) | ((u32)(255 * clamp(a)) << 24); }
static u32 mix(u32 a, u32 b, float k) {
    u32 c = 0;
    k = clamp(k);
    for (int i = 0; i < 3; i++)
        c |= (u32)((float)((a >> (8 * i)) & 255) +
                   ((int)((b >> (8 * i)) & 255) - (int)((a >> (8 * i)) & 255)) * k)
             << (8 * i);
    return c | 0xff000000;
}
static void blend(bool add) {
    if (add == additive)
        return;
    C2D_Flush();
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, add ? GPU_ONE : GPU_ONE_MINUS_SRC_ALPHA,
                   GPU_SRC_ALPHA, add ? GPU_ONE : GPU_ONE_MINUS_SRC_ALPHA);
    additive = add;
}
static void tri(float ax, float ay, u32 ac, float bx, float by, u32 bc, float cx, float cy, u32 cc) {
    scene_triangle(ax + depth, ay, ac, bx + depth, by, bc, cx + depth, cy, cc, z);
}
static void rect(float x, float y, float ww, float hh, u32 c) {
    scene_rectangle(x + depth, y, z, ww, hh, c, c, c, c);
}
static void grad(float x, float y, float ww, float hh, u32 a, u32 b) {
    scene_rectangle(x + depth, y, z, ww, hh, a, a, b, b);
}
static void line(float x0, float y0, float x1, float y1, u32 col, float a0, float a1, float lw) {
    if (a0 < .003f && a1 < .003f)
        return;
    float dx = x1 - x0, dy = y1 - y0, dd = dx * dx + dy * dy;
    if (dd < .0001f)
        return;
    float k = lw * .5f / sqrtf(dd), nx = -dy * k, ny = dx * k;
    u32 a = alpha(col, a0), b = alpha(col, a1);
    scene_line_quad(x0 - nx + depth, y0 - ny, x1 - nx + depth, y1 - ny,
                    x0 + nx + depth, y0 + ny, x1 + nx + depth, y1 + ny, a, b, z);
}
// The mock's radial gradient, generated in code once per menu session. One
// textured quad avoids thousands of tiny fan triangles on the Old 3DS.
static void glow(float x, float y, float r, u32 col, float a, float sy) {
    if (a < .003f || r <= 0)
        return;
    blend(true);
#ifdef __3DS__
    scene_radial(&ws->radial, x-r+depth, y-r*sy, z, r*2, r*sy*2, alpha(col,a));
#else
    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint, alpha(col, a), 1);
    C2D_DrawImageAt((C2D_Image){&ws->radial, &ws->radial_sub}, x - r + depth, y - r * sy, z, &tint,
                    r / 64, r * sy / 64);
#endif
}
static void gradient(const layer_t *L) {
    blend(false);
    for (int k = 1; k < L->ns; k++)
        grad(0, L->stops[k - 1] * 240, w, (L->stops[k] - L->stops[k - 1]) * 240, L->colours[k - 1],
             L->colours[k]);
}
static void lights(const layer_t *L, float t) {
    for (int k = 0; k < L->nf; k++) {
        const float *f = L->fans[k];
        glow(f[0] * w, f[1] * 240, f[2] * w * L->z, L->colours[k] ? L->colours[k] : th->fx_colour,
             f[3] * strength * (1 + .06f * sn(t * .21f + f[0] * 7)), f[4]);
    }
}
// Advect existing points along rays from the vanishing point. Depth decreases
// linearly, so projected motion accelerates near the viewer. Each ray recycles
// independently at the edge; no scene-wide zoom/reset or extra primitives.
// The seed's original radius sets its phase, preserving the authored layout at t=0.
static ray_t forward_ray(point_t p, float cy) {
    float dx = p.x - w * .5f, dy = p.y - cy;
    float r = fmaxf(fabsf(dx) / (w * .5f + 30), fabsf(dy) / 270);
    float d = 1 / fmaxf(.001f, r);
    return (ray_t){dx * d, dy * d, d};
}
static point_t forward_at(ray_t ray, float t, float speed, float cy) {
    float zz = ray.d - t * speed;
    if (zz < 1) zz = 1 + fract((zz - 1) / 12) * 12;
    float k = 1 / zz;
    return (point_t){w * .5f + ray.x * k, cy + ray.y * k};
}
static point_t forward_point(point_t p, float t, float speed, float cy) {
    return forward_at(forward_ray(p, cy), t, speed, cy);
}
static void stars(const layer_t *L, float t) {
    int n = (int)((L->n ? L->n : 110) * clamp(strength) + .5f);
    if (n > COUNT(STARS))
        n = COUNT(STARS);
    u32 ink = mix(th->fx_colour, th->scene_white, .6f);
    for (int i = 0; i < n; i++) {
        const stars_seed_t *q = &STARS[i];
        point_t p = forward_at(ws->star_ray[bottom][i], t, .045f, bottom ? -24 : 96);
        float x = p.x, y = p.y;
        float a = q->a * strength * (.6f + .4f * sn(t * q->f + q->p));
        if (q->big)
            glow(x, y, 3, th->fx_colour, a * .5f, 1);
        blend(false);
        rect(x, y, 1, 1, alpha(ink, a));
    }
}
// Eclipse: independently authored depth flight. Small three-star groups share
// a depth plane, so their faint links expand with the stars instead of sliding
// across them. Work is O(n); no neighbour search or frame history is needed.
// Odysseus inspired only the quiet particles/glow/links, not this implementation.
static void eclipse(const layer_t *L, float t) {
    const u32 blue = TH_RGB(83, 132, 255), ice = TH_RGB(179, 205, 255);
    float cx = w * .5f, cy = bottom ? -36 : 100;
    glow(cx, cy, w * .72f, TH_RGB(24, 49, 156), .25f * strength, .64f);
    glow(w * .8f, bottom ? 180 : 42, w * .5f, TH_RGB(36, 41, 125), .10f * strength, .7f);
    blend(true);
    int groups = bottom ? 28 : 44;
    for (int i = 0; i < groups; i++) {
        const sf_seed_t *q = &SF[i];
        float phase = fract(q->z - t * .014f);
        float d = .22f + phase * .78f, k = 1 / d;
        float a = smooth(0, .14f, phase) * smooth(1, .83f, phase) * strength;
        float x = cx + q->x * 2.1f * k, y = cy + q->y * 1.8f * k;
        if (x < -40 || x > w + 40 || y < -40 || y > 280)
            continue;
        point_t p[3] = {{x, y},
                        {x + (6 + i % 7) * k, y + (i % 2 ? 7 : -9) * k},
                        {x - (5 + i % 5) * k, y + (9 + i % 6) * k}};
        if (i % 4 == 0) {
            line(p[0].x, p[0].y, p[1].x, p[1].y, blue, a * .11f, a * .07f, .65f);
            line(p[0].x, p[0].y, p[2].x, p[2].y, blue, a * .11f, a * .05f, .65f);
        }
        for (int j = 0; j < 3; j++) {
            float brightness = a * (j ? .23f : .38f + .30f * q->b);
            float size = .55f + .32f * k * (j ? .7f : 1);
            if (!j && i % 3 == 0)
                glow(p[j].x, p[j].y, 2.5f + k, blue, brightness * .32f, 1);
            rect(p[j].x - size * .5f, p[j].y - size * .5f, size, size, alpha(ice, brightness));
        }
    }
}
// Four additional colour families, built from bounded code geometry. Like
// Eclipse, these borrow only broad visual ideas (dust, facets, fireflies, flow).
static void solar(float t) {
    float cy = bottom ? -20 : 104;
    glow(w * .5f, cy, w * .65f, th->fx_colour, .22f * strength, .7f);
    glow(w * .2f, 180, w * .48f, th->fx_colour2, .12f * strength, .8f);
    blend(true);
    for (int i = 0; i < (bottom ? 54 : 86); i++) {
        const sf_seed_t *q = &SF[i];
        float phase = fract(q->z - t * .019f), d = .24f + .76f * phase, k = 1 / d;
        float a = strength * smooth(0, .14f, phase) * smooth(1, .8f, phase) * q->b;
        float x = w * .5f + q->x * 2 * k, y = cy + q->y * 1.7f * k;
        if (x < -8 || x > w + 8 || y < -8 || y > 248)
            continue;
        float r = .45f + .33f * k;
        if (i % 4 == 0)
            glow(x, y, 4 + k, th->fx_colour, a * .22f, 1);
        rect(x - r * .5f, y - r * .5f, r, r, alpha(th->fx_colour, a * .7f));
        if (i % 9 == 0) {
            line(x - 3 * k, y, x + 3 * k, y, th->fx_colour, 0, a * .2f, .6f);
            line(x, y - 3 * k, x, y + 3 * k, th->fx_colour, 0, a * .2f, .6f);
        }
    }
}
static void glacier(float t) {
    float cy = bottom ? -28 : 92;
    glow(w * .7f, cy, w * .8f, th->fx_colour, .16f * strength, .8f);
    blend(true);
    for (int i = 0; i < (bottom ? 24 : 40); i++) {
        const sf_seed_t *q = &SF[i];
        float phase = fract(q->z - t * .011f), d = .28f + .72f * phase, k = 1 / d;
        float a = strength * smooth(0, .16f, phase) * smooth(1, .82f, phase) * q->b;
        float x = w * .5f + q->x * 2.6f * k, y = cy + q->y * 2 * k;
        if (x < -24 || x > w + 24 || y < -24 || y > 264)
            continue;
        float rx = (2 + i % 4) * k, ry = (5 + i % 6) * k;
        tri(x, y - ry, alpha(th->fx_colour, a * .25f), x - rx, y, alpha(th->fx_colour, a * .06f), x,
            y + ry, alpha(th->fx_colour2, a * .09f));
        tri(x, y - ry, alpha(th->fx_colour, a * .25f), x + rx, y, alpha(th->fx_colour, a * .12f), x,
            y + ry, alpha(th->fx_colour2, a * .09f));
        line(x, y - ry, x - rx, y, th->fx_colour, a * .4f, a * .12f, .65f);
        line(x, y - ry, x + rx, y, th->fx_colour, a * .4f, a * .12f, .65f);
        if (i % 5 == 0)
            glow(x, y - ry, 3, th->fx_colour, a * .25f, 1);
    }
}
static void citron(float t) {
    float cy = bottom ? -40 : 110;
    glow(w * .3f, cy, w * .7f, th->fx_colour, .15f * strength, .65f);
    glow(w * .85f, 180, w * .45f, th->fx_colour2, .09f * strength, .8f);
    blend(true);
    struct { float x,y,r,a; } seeds[72];
    int count=0;
    for (int i = 0; i < (bottom ? 44 : 72); i++) {
        const sf_seed_t *q = &SF[i];
        float phase = fract(q->z - t * .023f), d = .22f + .78f * phase, k = 1 / d;
        float a = strength * smooth(0, .16f, phase) * smooth(1, .8f, phase) * q->b;
        float x = w * .5f + q->x * 2 * k, y = cy + q->y * 1.8f * k;
        if (x < -16 || x > w + 16 || y < -16 || y > 256)
            continue;
        float flutter = .75f + .25f * sn(t * .6f + q->z * TAU), r = (.6f + .45f * k) * flutter;
        glow(x, y, 3 + r * 2, th->fx_colour, a * .3f * flutter, 1);
        seeds[count].x=x;seeds[count].y=y;seeds[count].r=r;seeds[count++].a=a;
    }
    // All glows precede solid seeds: two texture batches, bounded stack only.
    for(int i=0;i<count;i++) {
        float x=seeds[i].x,y=seeds[i].y,r=seeds[i].r,a=seeds[i].a;
        // A tiny tapered seed and its warm core, no CRT/glyph layer.
        tri(x - r, y, alpha(th->fx_colour, a * .25f), x + r, y - r, alpha(th->fx_colour, a * .65f),
            x + r * .4f, y + r, alpha(th->fx_colour2, a * .15f));
        rect(x - .5f, y - .5f, 1, 1, alpha(th->scene_white, a * .6f));
    }
}
static void sienna(float t) {
    glow(w * .7f, bottom ? -20 : 90, w * .65f, th->fx_colour, .18f * strength, .7f);
    blend(true);
    // Expanding contour fibres: sparse, organic curves rather than a corridor.
    for (int band = 0; band < (bottom ? 6 : 10); band++) {
        float phase = fract(band / 10.f - t * .008f), d = .3f + .7f * phase, k = 1 / d;
        float a = strength * smooth(0, .18f, phase) * smooth(1, .8f, phase);
        point_t prev = {0};
        for (int j = 0; j <= 18; j++) {
            float u = j / 18.f, xx = (u - .5f) * w * 1.2f;
            float yy = 36 + band * 2 + 14 * sn(u * 6 + band * .6f) + 5 * sn(u * 13 + band);
            point_t p = {w * .5f + xx * k, (bottom ? -35 : 68) + yy * k};
            if (j)
                line(prev.x, prev.y, p.x, p.y, th->fx_colour, a * .20f, a * .20f, .7f);
            prev = p;
            if (j == 5 + band % 9)
                glow(p.x, p.y, 3, th->fx_colour, a * .45f, 1);
        }
    }
}
static point_t spine(float u, float y0, float y1) {
    return (point_t){-.1f * w + 1.2f * w * u, y0 + (y1 - y0) * u + 14 * sn(u * 5.5f + 1)};
}
static void galaxy(const layer_t *L, float t) {
    float y0 = (L->y0 ? L->y0 : .95f) * 240, y1 = (L->y1 ? L->y1 : .05f) * 240, width = 34 * L->z,
          slide = t * .004f;
    float nx = -(y1 - y0), ny = 1.2f * w, nl = sqrtf(nx * nx + ny * ny);
    nx /= nl;
    ny /= nl;
    for (int k = 0; k < 14; k++) {
        float u = fract(k / 14.f + slide);
        point_t p = spine(u, y0, y1);
        float a =
            (.75f + .25f * sn(k * 2.1f + t * .07f)) * smooth(0, .12f, u) * smooth(1, .88f, u) * strength;
        glow(p.x, p.y, width * (1.6f + .6f * sn(k * 1.7f)), k & 1 ? th->fx_colour : th->fx_colour2,
             .13f * a, .6f);
        if (k % 3 == 0)
            glow(p.x, p.y, width * .6f, mix(th->fx_colour, th->scene_white, .5f), .07f * a, .7f);
    }
    blend(false);
    for (int j = 0; j < 3; j++) {
        float off = j == 2 ? .22f : -.1f,
              lw = j == 0   ? 16
                   : j == 1 ? 8
                            : 7,
              a = j == 0   ? .1f
                  : j == 1 ? .12f
                           : .08f;
        point_t prev = {0};
        for (int k = 0; k <= 24; k++) {
            point_t p = spine(k / 24.f, y0, y1);
            float d = off * width * (1 + .5f * sn(k * .9f + t * .03f));
            p.x += nx * d;
            p.y += ny * d;
            if (k)
                line(prev.x, prev.y, p.x, p.y, th->shadow, a, a, lw);
            prev = p;
        }
    }
    int n = (int)(140 * fmaxf(.3f, strength));
    if (n > COUNT(GX))
        n = COUNT(GX);
    for (int i = 0; i < n; i++) {
        const gx_seed_t *q = &GX[i];
        point_t p = spine(fract(q->u + slide), y0, y1);
        float d = q->v * width * 1.3f;
        float a = q->a * strength * (.65f + .35f * sn(t * .6f + q->p)) * (1 - fabsf(q->v) * .6f);
        rect(p.x + nx * d, p.y + ny * d, 1, 1, alpha(mix(th->fx_colour, th->scene_white, .7f), a));
    }
}
static void constellations(const layer_t *L, float t) {
    struct {
        float x, y, a, l;
    } p[46];
    int n = (int)((L->n ? L->n : 34) * fmaxf(.4f, strength) + .5f);
    if (n > 46)
        n = 46;
    if (!g_new3ds)
        n = (n * 3 + 3) / 4;
    for (int i = 0; i < n; i++) {
        const cn_seed_t *q = &CN[i];
        point_t at = forward_at(ws->chart_ray[bottom][i], t, q->l ? .045f : .025f, bottom ? -24 : 96);
        float x = at.x;
        p[i].x = x;
        p[i].y = at.y;
        p[i].l = q->l;
        p[i].a = smooth(-30, 10, x) * smooth(w + 30, w - 10, x) * q->b * (q->l ? 1 : .55f);
    }
    blend(true);
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            if (p[i].l != p[j].l)
                continue;
            float dx = p[i].x - p[j].x, dy = p[i].y - p[j].y, D = p[i].l ? 66 : 49.5f,
                  dd = dx * dx + dy * dy;
            if (dd >= D * D)
                continue;
            float k = 1 - sqrtf(dd) / D, a = k * k * fminf(p[i].a, p[j].a) * .75f * strength;
            if (a <= .01f)
                continue;
            line(p[i].x, p[i].y, p[j].x, p[j].y, th->fx_colour, a * .25f, a * .25f, 2.4f);
            line(p[i].x, p[i].y, p[j].x, p[j].y, th->fx_colour, a, a, .8f);
        }
    if (!g_new3ds) {
        for (int i = 0; i < n; i++) {
            if (p[i].a < .02f) continue;
            float a = p[i].a * strength * (.8f + .2f * sn(t * .9f + p[i].x * .05f));
            glow(p[i].x, p[i].y, p[i].l ? 10 : 6, th->fx_colour, .5f * a, 1);
        }
        blend(false);
    }
    for (int i = 0; i < n; i++) {
        if (p[i].a < .02f)
            continue;
        float tw = .8f + .2f * sn(t * .9f + p[i].x * .05f), a = p[i].a * strength * tw,
              r = p[i].l ? 1.8f : 1.2f;
        if (g_new3ds)
            glow(p[i].x, p[i].y, p[i].l ? 10 : 6, th->fx_colour, .5f * a, 1);
        blend(false);
        rect(p[i].x - r / 2, p[i].y - r / 2, r, r, alpha(mix(th->fx_colour, th->scene_white, .75f), a));
    }
}
static void silk(const layer_t *L, float t) {
    static const float rb[3][10] = {{134, 15, .011f, .15f, 22, .010f, .2f, 1, .6f, 0},
                                    {150, 11, .015f, -.11f, 15, .014f, .16f, .75f, 2.6f, 1},
                                    {120, 9, .008f, .08f, 11, .018f, -.13f, .4f, 5, 2}};
    float cen[53], wid[53];
    blend(true);
    for (int r = 0; r < 3; r++) {
        const float *b = rb[r];
        // Old keeps all three ribbons and their bright edges; fewer interior
        // fibres and longer segments leave headroom for the rest of the menu.
        int step = g_new3ds ? 8 : 16, N = (int)(w / step) + 2;
        int S = g_new3ds ? (bottom ? 10 : 16) : (bottom ? 4 : 6);
        u32 col = r == 0 ? th->fx_colour : r == 1 ? th->fx_colour2 : th->silk_light;
        for (int j = 0; j < N; j++) {
            float x = -8 + j * step;
            cen[j] = b[0] + L->yShift + (x - w / 2) * .17f * (w / 400) +
                     b[1] * sn(x * b[2] + t * b[3] + b[8]) +
                     b[1] * .4f * sn(x * b[2] * 2.3f - t * b[3] * 1.7f + b[8] * 1.3f);
            wid[j] = b[4] * sn(x * b[5] + t * b[6] + b[8] * .7f);
        }
        for (int j = 1; j < N; j++) {
            float x0 = -8 + (j - 1) * step, x1 = x0 + step,
                  e0 = smooth(0, w * .18f, x0) * smooth(w, w * .82f, x0) * b[7] * strength,
                  e1 = smooth(0, w * .18f, x1) * smooth(w, w * .82f, x1) * b[7] * strength;
            u32 a = alpha(col, .055f * e0), bcol = alpha(col, .055f * e1);
            tri(x0, cen[j - 1] - wid[j - 1] / 2, a, x1, cen[j] - wid[j] / 2, bcol, x1,
                cen[j] + wid[j] / 2, bcol);
            tri(x0, cen[j - 1] - wid[j - 1] / 2, a, x1, cen[j] + wid[j] / 2, bcol, x0,
                cen[j - 1] + wid[j - 1] / 2, a);
            for (int k = 0; k < S; k++) {
                float u = k / (float)(S - 1) - .5f;
                bool edge = k == 0 || k == S - 1;
                float a = edge ? .55f : .13f;
                line(x0, cen[j - 1] + u * wid[j - 1], x1, cen[j] + u * wid[j], col, a * e0, a * e1,
                     edge ? 1.1f : .7f);
            }
            line(x0, cen[j - 1] - wid[j - 1] / 2, x1, cen[j] - wid[j] / 2, col, .07f * e0, .07f * e1, 6);
        }
        if (!r)
            for (int k = 0; k < 5; k++) {
                float travel = fract(k * .194f + t * .028f);
                float x = w * .5f + (k & 1 ? -1 : 1) *
                          travel / (1.6f - .6f * travel) * (w * .5f + 20);
                int j = (int)((x + 8) / step + .5f);
                j = j < 0 ? 0 : j >= N ? N - 1 : j;
                glow(x, cen[j] - wid[j] / 2, 5, th->silk_glint,
                     (.5f + .5f * sn(t * .8f + k * 1.9f)) * .45f * strength, 1);
            }
    }
}
static point_t fibre_at(const point_t *p, float d, int steps) {
    d = fmaxf(0, fminf(steps - .001f, d));
    int j = (int)d;
    float f = d - j;
    return (point_t){p[j].x + (p[j + 1].x - p[j].x) * f, p[j].y + (p[j + 1].y - p[j].y) * f};
}
// Flow changes slowly (the mock's field advances by .045 radians/second).
// Cache its integrated paths at 8 Hz and interpolate every menu frame. All
// fibres and 30 integration steps remain; this bounds temporal phase error
// without making the animation update at the cache rate.
static void flow_path(point_t p[31], int i, float t) {
    float h = (w + 60) / 30, tt = t * .045f;
    const fl_seed_t *q = &FL[i];
    p[0] = (point_t){-30, q->y * 240 + 18 * sn(t * .02f + q->bnd * 1.7f) + q->off};
    for (int k = 0; k < 30; k++) {
        float u = p[k].x / w, v = p[k].y / 240,
              a = .42f * sn(u * 2.6f + tt + v * 1.7f) + .22f * sn(v * 3.9f - u * 1.9f + tt * 1.4f) +
                  .12f * sn(u * 6.1f + v * 5.3f - tt * .8f);
        p[k + 1] = (point_t){p[k].x + cs(a) * h, p[k].y + sn(a) * h};
    }
}
static void flow(const layer_t *L, float t) {
    int steps = 30, keep = (int)(COUNT(FL) * fmaxf(.3f, fminf(1, strength * (L->n ? L->n : 1))) + .5f);
    flow_cache_t *cache = &ws->flow[bottom];
    struct { point_t p; u32 color; float a; } glows[COUNT(FL)];
    int glow_count = 0;
    blend(true);
    for (int i = 0; i < keep; i++) {
        // Preserve the four bright guide fibres; thin the dim weave on Old.
        if (!g_new3ds && (i & 1) && i != 15 && i != 27)
            continue;
        const fl_seed_t *q = &FL[i];
        point_t p[31];
        // Stagger the refresh across fibres so the integration work is spread
        // over frames instead of spiking eight times each second.
        float offset = i / (float)keep, clock = t * 8 + offset;
        float epoch = floorf(clock), phase = clock - epoch;
        if (!cache->valid[i] || epoch != cache->epoch[i]) {
            if (cache->valid[i] && epoch == cache->epoch[i] + 1)
                memcpy(cache->p[0][i], cache->p[1][i], sizeof(cache->p[0][i]));
            else
                flow_path(cache->p[0][i], i, (epoch - offset) / 8);
            flow_path(cache->p[1][i], i, (epoch + 1 - offset) / 8);
            cache->epoch[i] = epoch;
            cache->valid[i] = true;
        }

        for (int k = 0; k <= steps; k++) {
            point_t a = cache->p[0][i][k], b = cache->p[1][i][k];
            p[k] = (point_t){a.x + (b.x-a.x)*phase, a.y + (b.y-a.y)*phase};
        }
        u32 col = mix(th->fx_colour, th->fx_colour2, ((int)q->bnd & 1) * .6f);
        int stride = g_new3ds ? 1 : 2;
        for (int k = 0; k < steps; k += stride) {
            float a0 = smooth(0, w * .2f, p[k].x) * smooth(w, w * .8f, p[k].x),
                  a1 = smooth(0, w * .2f, p[k + stride].x) * smooth(w, w * .8f, p[k + stride].x),
                  opacity = (i == 0 || i == 12 || i == 26 || i == 36 ? .5f : .16f) * q->b * strength;
            line(p[k].x, p[k].y, p[k + stride].x, p[k + stride].y, col, opacity * a0, opacity * a1, .75f);
        }
        if (!q->bead)
            continue;
        float travel = fract(q->ph + t * .022f);
        float distance = travel / (1.7f - .7f * travel);
        float pos = steps * .5f + (i & 1 ? -1 : 1) * distance * (steps * .5f + 4);
        for (int k = 0; k < 4; k++) {
            float direction = i & 1 ? -1 : 1;
            float d0 = pos - direction * (k + 1) * .9f, d1 = pos - direction * k * .9f;
            if (fmaxf(d0, d1) <= 0 || fminf(d0, d1) >= steps)
                continue;
            point_t a = fibre_at(p, d0, steps), b = fibre_at(p, d1, steps);
            float e = smooth(0, 5, d1) * smooth(steps, steps - 5, d1) * q->b * strength * .55f;
            line(a.x, a.y, b.x, b.y, mix(col, th->scene_white, .5f), e * (1 - (k + 1) / 4.f),
                 e * (1 - k / 4.f), 1.2f);
        }
        if (pos > 0 && pos < steps) {
            point_t a = fibre_at(p, pos, steps);
            glows[glow_count].p = a;
            glows[glow_count].color = col;
            glows[glow_count++].a = .28f * q->b * strength * smooth(0, 5, pos) * smooth(steps, steps - 5, pos);
        }
    }
    // These contributions all add: defer texture mode until every solid fibre
    // and bead tail has been submitted.
    for (int i = 0; i < glow_count; i++)
        glow(glows[i].p.x, glows[i].p.y, 7, glows[i].color, glows[i].a, 1);
}
static point_t signal_at(int i, float d) {
    d = fmaxf(0, fminf(SIG[i].len - .001f, d));
    int k = (int)(d / 24);
    float f = d / 24 - k;
    return (point_t){12 + SIG[i].pts[k][0] + (SIG[i].pts[k + 1][0] - SIG[i].pts[k][0]) * f,
                     12 + SIG[i].pts[k][1] + (SIG[i].pts[k + 1][1] - SIG[i].pts[k][1]) * f};
}
static void signals(const layer_t *L, float t) {
    blend(true);
    float ga = (L->grid ? L->grid : .2f) * strength;
    for (float xx = 12; xx < w; xx += 24) {
        float x = forward_point((point_t){xx, 96}, t, .06f, 96).x;
        grad(x, 0, 1, 240, alpha(th->fx_colour2, ga * .6f), alpha(th->fx_colour2, ga * .25f));
    }
    for (float yy = 12; yy < 240; yy += 24) {
        float y = forward_point((point_t){w * .5f, yy}, t, .06f, 96).y;
        rect(0, y, w, 1, alpha(th->fx_colour2, ga * (y > 165 && !bottom ? .35f : 1)));
    }
    int n = (int)((L->n ? L->n : 14) * fmaxf(.4f, strength) + .5f);
    if (n > COUNT(SIG))
        n = COUNT(SIG);
    for (int i = 0; i < n; i++) {
        float pos = fract((t * SIG[i].v + SIG[i].ph * (SIG[i].len + 160)) / (SIG[i].len + 160)) *
                    (SIG[i].len + 160),
              life = smooth(0, 40, pos) * smooth(SIG[i].len, SIG[i].len - 60, pos);
        if (life <= .01f || pos > SIG[i].len)
            continue;
        float a = life * strength * 1.2f;
        point_t p = signal_at(i, pos);
        for (int k = 1; k <= 3; k++) {
            point_t q = signal_at(i, pos - 20 * k);
            line(q.x, q.y, p.x, p.y, th->fx_colour, a * .6f * (1 - k / 3.f),
                 a * .6f * (1 - (k - 1) / 3.f), 1.2f);
            line(q.x, q.y, p.x, p.y, th->fx_colour, a * .12f * (1 - k / 3.f),
                 a * .12f * (1 - (k - 1) / 3.f), 4);
            p = q;
        }
        p = signal_at(i, pos);
        glow(p.x, p.y, 10, th->fx_colour, a * .55f, 1);
        rect(p.x - 1, p.y - 1, 2, 2, alpha(mix(th->fx_colour, th->scene_white, .75f), a * .95f));
        p = signal_at(i, (int)(pos / 24) * 24);
        glow(p.x, p.y, 5, th->fx_colour, a * .35f * (1 - fract(pos / 24)), 1);
    }
}
static void glyphs(const layer_t *L, float t) {
    static const float gb[4][5] = {{.31f, .22f, .071f, .053f, .9f},
                                   {.71f, .31f, -.052f, .067f, 1},
                                   {.52f, .78f, .061f, -.043f, .8f},
                                   {.12f, .66f, -.047f, -.059f, .7f}};
    float bx[4], by[4], br[4], inv_radius[4];
    for (int k = 0; k < 4; k++) {
        bx[k] = (.5f + .42f * sn(t * gb[k][2] + gb[k][0] * 6.3f)) * w;
        by[k] = (.5f + .42f * sn(t * gb[k][3] + gb[k][1] * 6.3f + k)) * 240;
        br[k] = gb[k][4] * (bottom ? 44 : 56) * (.85f + .15f * sn(t * .2f + k));
        point_t p = forward_point((point_t){bx[k], by[k]}, t, .018f, bottom ? -24 : 96);
        bx[k] = p.x; by[k] = p.y;
        inv_radius[k] = 1 / br[k];
    }
    blend(false);
    // The smaller Old bottom screen keeps the moving four-light glyph field,
    // with 16x12 samples instead of 20x15. New and Old top stay unchanged.
    int dx_cell = g_new3ds ? 8 : (bottom ? 20 : 16);
    int dy_cell = g_new3ds ? 12 : (bottom ? 20 : 16);
    for (int j = 0; j < 240 / dy_cell; j++) {
        float cy = bottom ? -24 : 96, yy = (j + .5f) * dy_cell;
        float d = 270 / fmaxf(1, fabsf(yy - cy)), zz = d - t * .018f;
        if (zz < 1) zz = 1 + fract((zz - 1) / 12) * 12;
        float scale = d / zz;
        for (int i = 0; i < (int)(w / dx_cell); i++) {
            float xx = (i + .5f) * dx_cell;
            float x = fract(.5f + (xx - w * .5f) * scale / w) * w;
            float y = cy + (yy - cy) * scale, b = 0;
            for (int k = 0; k < 4; k++) {
                float dx = (x - bx[k]) * inv_radius[k], dy = (y - by[k]) * inv_radius[k], d = dx * dx + dy * dy;
                if (d < 4)
                    b += decay(d * 1.6f);
            }
            float gx = xx / 8 - .5f, gy = yy / 12 - .5f;
            float grain = ws->grain[(int)gy][(int)gx],
                  v = .5f + .5f * sn(gx * .23f - gy * .31f + t * .3f);
            b += (bottom ? .2f : .3f) * (.35f + .65f * v * v) * grain;
            b *= strength * .85f;
            if (b < .09f)
                continue;
            int ci = (int)(b * 12);
            if (ci > 11)
                ci = 11;
            u32 col = alpha(mix(th->fx_colour, th->scene_white, clamp(b - .6f)),
                            clamp(.12f + b * .6f) * (y > 162 && !bottom ? .35f : 1));
#ifdef __3DS__
            scene_draw_glyph(&ws->cached_glyph[ci], x - 3 + depth, y - 6, z, col);
#else
            C2D_DrawText(&ws->glyph[ci], C2D_WithColor, x - 3 + depth, y - 6, z, g_new3ds ? .25f : .32f, g_new3ds ? .25f : .32f, col);
#endif
        }
    }
    for (int k = 0; k < 4; k++)
        glow(bx[k], by[k], br[k] * 1.3f, th->fx_colour, .1f * strength, .9f);
    blend(false);
    for (int y = 0; y < 240; y += 2)
        rect(0, y, w, 1, alpha(th->shadow, .16f));
}
static point_t ember_at(const em_seed_t *q, float t, float sp) {
    return forward_point((point_t){q->x * w, 255 - q->y * 270}, t,
                         .038f * (sp / 7), bottom ? 270 : 210);
}
static void embers(const layer_t *L, float t) {
    for (int k = 0; k < 4; k++) {
        float ph = fract(k * .29f + t * .012f);
        glow(w * (.18f + .64f * fract(k * .618f)) + 20 * sn(t * .05f + k), 300 - ph * 360, 70,
             th->fx_colour2, .07f * strength * sn(ph * PI), 1.3f);
    }
    int n = (int)((L->n ? L->n : 80) * fmaxf(.3f, strength) + .5f);
    if (n > COUNT(EM))
        n = COUNT(EM);
    // All three passes use additive blending: grouping the same primitives
    // avoids switching image/circle shader state for every particle.
    struct { point_t p, prev; float a, size; int layer; } particles[COUNT(EM)];
    int count = 0;
    blend(true);
    for (int i = 0; i < n; i++) {
        const em_seed_t *q = &EM[i];
        int l = (int)q->l;
        float sp = (l == 0 ? 4 : l == 1 ? 7 : 11) * q->v;
        point_t p = ember_at(q, t, sp), prev = ember_at(q, t - .9f, sp);
        // A recycled ray must not leave a trail across the scene.
        if (fabsf(prev.x - p.x) + fabsf(prev.y - p.y) > 40)
            continue;
        float life = smooth(255, 215, p.y) * smooth(19.2f, 89.2f, p.y),
              heat = .55f + .45f * smooth(39.2f, 240, p.y),
              a = q->b * (.7f + .3f * sn(t * (.8f + q->w) + q->p)) * life * heat * strength * 1.45f;
        if (a < .02f)
            continue;
        particles[count].p = p;
        particles[count].prev = prev;
        particles[count].a = a;
        particles[count].size = l == 0 ? 1 : l == 1 ? 1.5f : 2.8f;
        particles[count++].layer = l;
    }
    for (int i = 0; i < count; i++) {
        float a = particles[i].a, size = particles[i].size;
        int l = particles[i].layer;
        line(particles[i].prev.x, particles[i].prev.y, particles[i].p.x, particles[i].p.y,
             th->fx_colour, 0, a * (l == 0 ? .45f : l == 1 ? .55f : .25f), size * .9f);
    }
    for (int i = 0; i < count; i++) {
        float a = particles[i].a, size = particles[i].size;
        int l = particles[i].layer;
        glow(particles[i].p.x, particles[i].p.y, size * (l == 2 ? 8 : 5.5f),
             th->fx_colour, a * (l == 2 ? .3f : .5f), 1);
    }
    for (int i = 0; i < count; i++) {
        float a = particles[i].a, size = particles[i].size;
        int l = particles[i].layer;
        C2D_DrawCircleSolid(particles[i].p.x + depth, particles[i].p.y, z, size * .6f,
            alpha(mix(th->fx_colour, th->scene_white, .6f * a), a * (l == 2 ? .55f : 1)));
    }
}
// Shipped rc9 rolling landscape, kept at full detail on both models.
// X-only powers are prepared once; all hill occlusion and clipping remain.
#define MESH_COLS 37            // x = -18 .. 18
#define MESH_ROWS 28
#define MESH_DZ   1.4f
#define HORIZON   140.0f

static const float *mesh_wave(int row, float world)
{
    for (unsigned k = 0; k < ws->mesh_wave[row].valid; k++)
        if (ws->mesh_wave[row].world[k] == world)
            return ws->mesh_wave[row].height[k];
    unsigned k = ws->mesh_wave[row].next;
    ws->mesh_wave[row].next = (k + 1) % 4;
    if (ws->mesh_wave[row].valid < 4) ws->mesh_wave[row].valid++;
    ws->mesh_wave[row].world[k] = world;
    for (int i = 0; i < MESH_COLS; i++) {
        float x = i - 18.0f;
        float n = 0.8f + 0.22f * sinf(x * 0.9f + world * 0.35f) +
                  0.12f * sinf(x * 1.7f - world * 0.5f);
        ws->mesh_wave[row].height[k][i] = ws->mesh_rise[i] * n;
    }
    return ws->mesh_wave[row].height[k];
}

// The theme's grid colours (near middle, sides, far), as numbers for mesh_colour.
typedef struct { int n[3], s[3], f[3]; } mesh_ink_t;

static void mesh_ink(mesh_ink_t *k, const theme_t *th)
{
	for (int i = 0; i < 3; i++) {
		k->n[i] = (th->original_grid[0] >> (8 * i)) & 255;
		k->s[i] = (th->original_grid[1] >> (8 * i)) & 255;
		k->f[i] = (th->original_grid[2] >> (8 * i)) & 255;
	}
}

static u32 mesh_colour(const mesh_ink_t *k, float x, float depth_power, u8 alpha)
{
	float side = fabsf(x) / 9.0f;
	if (side > 1) side = 1;
	float t = ws->mesh_side[(int)(x+18)];
	float r = k->n[0] + (k->s[0] - k->n[0]) * t, g = k->n[1] + (k->s[1] - k->n[1]) * t,
	      b = k->n[2] + (k->s[2] - k->n[2]) * t;
	float p = depth_power * (1 - side) * 0.85f;
	r += (k->f[0] - r) * p; g += (k->f[1] - g) * p; b += (k->f[2] - b) * p;
	return TH_RGBA((u8)r, (u8)g, (u8)b, alpha);
}

// Line clipped to just beyond the screen. Far off-screen vertices (the
// nearest grid rows land thousands of pixels out) can make the GPU drop the
// whole line, so lines vanished before reaching the screen edges.
static inline __attribute__((always_inline)) void clip_line(C2Di_Context *batch, float x0, float y0, u32 c0, float x1, float y1, u32 c1, float w)
{
	const float xmin = -8, xmax = 408, ymin = -8, ymax = 248;
    // Trivial reject before the four clip divisions. This changes no emitted
    // endpoint: the original clipper rejects these segments as well.
    if ((x0 < xmin && x1 < xmin) || (x0 > xmax && x1 > xmax) ||
        (y0 < ymin && y1 < ymin) || (y0 > ymax && y1 > ymax)) return;
	float t0 = 0, t1 = 1, dx = x1 - x0, dy = y1 - y0;
    if (!(x0 >= xmin && x0 <= xmax && y0 >= ymin && y0 <= ymax &&
          x1 >= xmin && x1 <= xmax && y1 >= ymin && y1 <= ymax)) {
    float p[4] = { -dx, dx, -dy, dy }, q[4] = { x0 - xmin, xmax - x0, y0 - ymin, ymax - y0 };
	for (int i = 0; i < 4; i++) {
		if (p[i] == 0) {
			if (q[i] < 0) return;
			continue;
		}
		float r = q[i] / p[i];
		if (p[i] < 0) { if (r > t1) return; if (r > t0) t0 = r; }
		else { if (r < t0) return; if (r < t1) t1 = r; }
	}
    }
	float ax=x0+dx*t0, ay=y0+dy*t0, bx=x0+dx*t1, by=y0+dy*t1;
    float len=sqrtf((bx-ax)*(bx-ax)+(by-ay)*(by-ay));
    if (len < .0001f) return;
    float nx=-(by-ay)*w*.5f/len, ny=(bx-ax)*w*.5f/len;
    scene_solid_line(batch, ax-nx,ay-ny,bx-nx,by-ny,ax+nx,ay+ny,bx+nx,by+ny,c0,c1,.16f);
}

static void draw_grid(float t)
{
	float (*px)[37] = ws->mesh_px, (*py)[37] = ws->mesh_py;
	float (*hgt)[37] = ws->mesh_h;
	float *rz = ws->mesh_z;
	const float cx = 200, f = 225;
	float zoff = t * 0.45f;                      // world units scrolled (slow drift)
	float phase = fmodf(zoff, MESH_DZ);

	for (int j = 0; j <= MESH_ROWS; j++) {
		// The nearest row must always land below the screen, or the floor
		// stops short of the bottom edge for part of each scroll step (at
		// z = 1.85 it projected to y = 194). It runs from 0.45 down past the
		// camera; clamped at 0.4 it still projects below the screen (y ~ 392),
		// so the lines toward it reach the bottom edge.
		float z = 0.45f - phase + j * MESH_DZ;
		if (z < 0.4f)
			z = 0.4f;
		rz[j] = z;
		float zw = z + zoff;
        const float *wave = mesh_wave(j, zw);
        float near = (z - 1.0f) / 6.0f;
        if (near > 1) near = 1;
        if (near < 0) near = 0;
        float scale = 0.35f + 0.65f * near;
		for (int i = 0; i < MESH_COLS; i++) {
			float x = i - 18.0f;
			float h = fabsf(x) <= 2.0f ? 0 : wave[i] * scale;
			hgt[j][i] = h;
			// far rows sit deeper behind the screen in 3D
			px[j][i] = cx + x / z * f + ui_depth(2.5f * (z - 1) / 40.0f - 0.5f);
			py[j][i] = HORIZON + (1.0f - h) / z * f * 0.45f;
		}
	}
	// hills hide the lines behind them in the floor's colour (the picture's, through the theme)
	const theme_t *th = theme();
	u32 fillc = th->original_floor;
	mesh_ink_t ink;
	mesh_ink(&ink, th);
    // A row has one depth power and each vertex has one RGB value. Keep
    // the original float operations; only reuse results formerly recomputed
    // for every endpoint. Alpha still belongs to the line's far/near edge.
    for (int j = 0; j <= MESH_ROWS; j++) {
        float d = (rz[j] - 1.0f) / 25.0f;
        if (d > 1) d = 1;
        if (d < 0) d = 0;
        float power = powf(d, 0.7f);
        // RGB depends on |x|; mirrored columns use exactly the same value.
        for (int i = 0; i <= MESH_COLS / 2; i++)
            ws->mesh_colour[j][MESH_COLS - 1 - i] =
                ws->mesh_colour[j][i] = mesh_colour(&ink, i - 18.0f, power, 255);
    }
    // Worst case: every cell emits two triangles, every edge a quad.
    // Buffer space is checked once; when it cannot fit, preserve the old
    // per-primitive checks and truncation behaviour via the NULL fallback.
    C2Di_Context *batch = scene_solid_begin(
        MESH_ROWS*(MESH_COLS-1)*6 +
        (MESH_ROWS*(2*MESH_COLS-1)+MESH_COLS-1)*4,
        MESH_ROWS*(MESH_COLS-1)*6 +
        (MESH_ROWS*(2*MESH_COLS-1)+MESH_COLS-1)*6);
    (void)batch;
	// back to front: hill cells hide the lines behind them
	for (int j = MESH_ROWS - 1; j >= 0; j--) {
		for (int i = 0; i < MESH_COLS - 1; i++) {
			if (hgt[j][i] + hgt[j][i + 1] + hgt[j + 1][i] + hgt[j + 1][i + 1] < 0.01f)
				continue;   // flat floor: nothing to hide
            // All coordinates are finite. Compare the same four corners
            // directly instead of six general-purpose libm min/max calls.
            if ((px[j][i] < 0 && px[j][i + 1] < 0 &&
                 px[j + 1][i] < 0 && px[j + 1][i + 1] < 0) ||
                (px[j][i] > 400 && px[j][i + 1] > 400 &&
                 px[j + 1][i] > 400 && px[j + 1][i + 1] > 400))
                continue;
            scene_solid_cell(batch, px[j][i], py[j][i], px[j][i + 1], py[j][i + 1],
                px[j + 1][i + 1], py[j + 1][i + 1], px[j + 1][i], py[j + 1][i], fillc, .15f);
		}
		// this strip's lines: the far edge across, and the lines toward the viewer
		float zf = rz[j + 1];
		u8 fade = zf > 32 ? (u8)(255 * (40 - zf) / 8) : 255;   // fade in at the horizon
		for (int i = 0; i < MESH_COLS - 1; i++)
			clip_line(batch, px[j + 1][i], py[j + 1][i], (ws->mesh_colour[j + 1][i] & 0x00ffffff) | ((u32)fade << 24),
				px[j + 1][i + 1], py[j + 1][i + 1], (ws->mesh_colour[j + 1][i + 1] & 0x00ffffff) | ((u32)fade << 24), 1.2f);
		for (int i = 0; i < MESH_COLS; i++)
			clip_line(batch, px[j + 1][i], py[j + 1][i], (ws->mesh_colour[j + 1][i] & 0x00ffffff) | ((u32)fade << 24),
				px[j][i], py[j][i], ws->mesh_colour[j][i], 1.2f);
	}
	for (int i = 0; i < MESH_COLS - 1; i++)
		clip_line(batch, px[0][i], py[0][i], ws->mesh_colour[0][i],
			px[0][i + 1], py[0][i + 1], ws->mesh_colour[0][i + 1], 1.4f);
}


// Cache detailed CC0 silhouettes from code-drawn spans once per menu visit.
// Colours remain theme roles; texture is just coverage and dies before a game.
static void original_palms(float t) {
    (void)t;
    if (!ws->palms_ok) {
        if (!C3D_TexInit(&ws->palms,1024,512,GPU_A8)) return;
        memset(ws->palms.data,0,ws->palms.size);
        for (int i=0;i<COUNT(original_palm_spans);i++) {
            unsigned x=original_palm_spans[i][0],y=original_palm_spans[i][1];
            for(unsigned end=x+original_palm_spans[i][2];x<end;x++) {
                unsigned off=(((y>>3)*128+(x>>3))<<6)|(x&1)|((y&1)<<1)|
                    ((x&2)<<1)|((y&2)<<2)|((x&4)<<2)|((y&4)<<3);
                ((u8*)ws->palms.data)[off]=255;
            }
        }
        C3D_TexSetFilter(&ws->palms,GPU_LINEAR,GPU_LINEAR);
        C3D_TexSetWrap(&ws->palms,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
        svcStoreProcessDataCache(CUR_PROCESS_HANDLE,(u32)ws->palms.data,ws->palms.size);
        ws->palms_sub=(Tex3DS_SubTexture){800,480,0,1,800/1024.f,1-480/512.f};
        ws->palms_ok=true;
    }
    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint,alpha(th->original_palm_rim,.65f),1);
    C2D_DrawImageAt((C2D_Image){&ws->palms,&ws->palms_sub},depth+.6f,0,z,&tint,.5f,.5f);
    C2D_PlainImageTint(&tint,th->original_palm,1);
    C2D_DrawImageAt((C2D_Image){&ws->palms,&ws->palms_sub},depth,0,z,&tint,.5f,.5f);
}
static void sunset(const layer_t *L, float t) {
    blend(false);
    if (bottom) {
        grad(0, 0, w, 84, th->floor_sky[0], th->floor_sky[1]);
        grad(0, 84, w, 156, th->floor_sky[1], th->floor_sky[2]);
        glow(160, -10, 220, th->sun_glow, .22f, .35f);
        blend(true);
        for (float y = -32 + fract(t * 9 / 32) * 32; y < 240; y += 32)
            rect(0, y, w, 1, alpha(th->fx_colour, .1f * (1 - y / 300)));
        for (float x = 16; x < w; x += 32)
            grad(x, 0, 1, 240, alpha(th->fx_colour2, .12f), alpha(th->fx_colour2, .02f));
        return;
    }
    // Original is the shipped rc9 scene, not the round-3 Outrun substitute.
    // Rich sunset palette, banded sun, palms and a rolling, self-occluding
    // 37x28 mesh. No Old detail switch: rc9's own menu cost is acceptable.
    static const float at[] = {0,.4f,.78f,1};
    for (int k=1;k<4;k++)
        grad(0,at[k-1]*140,w,(at[k]-at[k-1])*140,th->original_sky[k-1],th->original_sky[k]);
    grad(0,140,w,100,th->original_floor,th->sun_floor[2]);
    glow(200,130,112.5f,th->sun_glow,.38f,1);
    blend(false);
    // The band geometry and palette are static for a whole menu visit.
    // Cache the original half-pixel rectangles, without merging their edges.
    if (ws->sun_theme != th) {
    ws->sun_count = 0;
    for (float y=55;y<140;y+=.5f) {
        float dy=y+.25f-130, half=sqrtf(fmaxf(0,75*75-dy*dy));
        bool cut=false;
        for(float cy=75*.62f,k=0;cy<150;k++) {
            float gap=75*(.02f+.016f*k);
            if(y-55>=cy && y-55<=cy+gap) cut=true;
            cy+=gap+75*.08f;
        }
        if(cut)continue;
        float d=(y-55)/150.f;
        u32 c=d<.5f?mix(th->original_sun[0],th->original_sun[1],d*2):
              d<.85f?mix(th->original_sun[1],th->original_sun[2],(d-.5f)/.35f):
              mix(th->original_sun[2],th->original_sun[3],(d-.85f)/.15f);
        unsigned i = ws->sun_count++;
        ws->sun[i].x=200-half; ws->sun[i].y=y;
        ws->sun[i].width=half*2; ws->sun[i].colour=c;
    }
    ws->sun_theme = th;
    }
    for (unsigned i=0; i<ws->sun_count; i++)
        rect(ws->sun[i].x,ws->sun[i].y,ws->sun[i].width,.5f,ws->sun[i].colour);
    original_palms(t);
    blend(false);
    draw_grid(t);

}
static void vignette(float a) {
    blend(false);
    // A coarse radial mesh, matching the canvas' transparent inner circle.
    float cx = w / 2, cy = 108, r0 = 84, r1 = w * .72f;
    for (int k = 0; k < 32; k++) {
        float a0 = k * TAU / 32, a1 = (k + 1) * TAU / 32;
        float x0 = cs(a0), y0 = sn(a0), x1 = cs(a1), y1 = sn(a1);
        u32 clear = alpha(th->shadow, 0), dark = alpha(th->shadow, a);
        tri(cx + x0 * r0, cy + y0 * r0, clear, cx + x0 * r1, cy + y0 * r1, dark, cx + x1 * r1,
            cy + y1 * r1, dark);
        tri(cx + x0 * r0, cy + y0 * r0, clear, cx + x1 * r1, cy + y1 * r1, dark, cx + x1 * r0,
            cy + y1 * r0, clear);
    }
}
static bool prepare(void) {
    if (ws)
        return true;
    ws = calloc(1, sizeof(*ws));
    if (!ws)
        return false;
    // Projection rays are immutable: calculate divisions once per menu visit.
    for (int b = 0; b < 2; b++) {
        w = b ? 320 : 400;
        float cy = b ? -24 : 96;
        for (int i = 0; i < COUNT(STARS); i++)
            ws->star_ray[b][i] = forward_ray((point_t){STARS[i].x * w, STARS[i].y * 240}, cy);
        for (int i = 0; i < COUNT(CN); i++)
            ws->chart_ray[b][i] = forward_ray((point_t){CN[i].x * (w + 60) - 30, CN[i].y * 240}, cy);
    }
    for (int i=0;i<37;i++) {
        float x=fabsf(i-18.f);
        ws->mesh_rise[i]=x<=2?0:powf(x-2,1.15f)*.32f;
        ws->mesh_side[i]=powf(fminf(1,x/9),.8f);
    }
    for (int i = 0; i < 1024; i++)
        ws->sine[i] = sinf(i * TAU / 1024);
    for (int j = 0; j < 20; j++)
        for (int i = 0; i < 50; i++)
            ws->grain[j][i] = fract(sn(i * 12.99f + j * 78.23f) * 43758.5f);
    // exp(-x/32), computed once without linking a new libm function.
    ws->decay[0] = 1;
    for (int i = 1; i < 256; i++)
        ws->decay[i] = ws->decay[i - 1] * .9692332345f;
    if (!C3D_TexInit(&ws->radial, 128, 128, GPU_A8)) {
        free(ws);
        ws = NULL;
        return false;
    }
    C3D_TexSetFilter(&ws->radial, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&ws->radial, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    for (unsigned y = 0; y < 128; y++)
        for (unsigned x = 0; x < 128; x++) {
            float dx = (x + .5f - 64) / 64, dy = (y + .5f - 64) / 64;
            float d = sqrtf(dx * dx + dy * dy);
            float a = d < .45f ? 1 - d * (.65f / .45f) : .35f * (1 - d) / .55f;
            unsigned off = (((y >> 3) * 16 + (x >> 3)) << 6) | (x & 1) | ((y & 1) << 1) |
                           ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
            ((u8 *)ws->radial.data)[off] = (u8)(255 * clamp(a));
        }
    svcStoreProcessDataCache(CUR_PROCESS_HANDLE, (u32)ws->radial.data, ws->radial.size);
    ws->radial_sub = (Tex3DS_SubTexture){128, 128, 0, 1, 1, 0};
    ws->text = C2D_TextBufNew(64);
    if (!ws->text) {
        C3D_TexDelete(&ws->radial);
        free(ws);
        ws = NULL;
        return false;
    }
    const char *ramp = ".,:;-=+*x#%@";
    for (int i = 0; i < 12; i++) {
        char s[2] = {ramp[i], 0};
        C2D_TextParse(&ws->glyph[i], ws->text, s);
        C2D_TextOptimize(&ws->glyph[i]);
#ifdef __3DS__
        ws->cached_glyph[i] = scene_cache_glyph(&ws->glyph[i], g_new3ds ? .25f : .32f);
#endif
    }
    return true;
}
void scene_release(void) {
    if (!ws)
        return;
    video_gpu_idle();
    C3D_TexDelete(&ws->radial);
    if (ws->palms_ok) C3D_TexDelete(&ws->palms);
    C2D_TextBufDelete(ws->text);
    free(ws);
    ws = NULL;
    memset(samples, 0, sizeof(samples));
    memset(ticks, 0, sizeof(ticks));
    memset(peak, 0, sizeof(peak));
    report_tick = 0;
    sample_theme = NULL;
}
void scene_draw(bool is_bottom) {
    // Pause dialogs retain the game's picture and never load scene resources.
    if (config_in_game()) {
        C2D_DrawRectSolid(0, 0, .1f, is_bottom ? 320 : 400, 240, theme()->menu_bg);
        return;
    }
    if (!prepare()) {
        C2D_DrawRectSolid(0, 0, .1f, is_bottom ? 320 : 400, 240, theme()->menu_bg);
        return;
    }
    u64 start = svcGetSystemTick();
    th = theme();
    if (sample_theme != th || !report_tick) {
        memset(samples, 0, sizeof(samples));
        memset(ticks, 0, sizeof(ticks));
        memset(peak, 0, sizeof(peak));
        report_tick = start;
        sample_theme = th;
#ifdef __3DS__
        video_take_heap_flush_ticks(); // discard the preceding theme/game interval
#endif
    }
    bottom = is_bottom;
    w = bottom ? 320 : 400;
    depth = bottom ? 0 : ui_depth(2);
    z = .1f;
    additive = false;
    // Do not reset the clock on theme changes or game return; uptime is monotonic.
    double seconds = start / (double)SYSCLOCK_ARM11;
    float t = (float)(seconds - 3600.0 * (u32)(seconds / 3600.0));
    int index = theme_from_id(th->id);
    if (index < 0 || index >= COUNT(scenes))
        index = 0;
    const layer_t *layers = bottom ? scenes[index].bottom : scenes[index].top;
    for (int j = 0; j < 8; j++) {
        const layer_t *L = &layers[j];
        if (L->i == 0)
            break;
        strength = L->i;
        float tt = (t + L->t0) * L->v;
        switch (L->fx) {
        case FX_SKY:
            gradient(L);
            break;
        case FX_LIGHT:
            lights(L, tt);
            break;
        case FX_STARS:
            stars(L, tt);
            break;
        case FX_GALAXY:
            galaxy(L, tt);
            break;
        case FX_CONSTELLATIONS:
            constellations(L, tt);
            break;
        case FX_SILK:
            silk(L, tt);
            break;
        case FX_FLOW:
            flow(L, tt);
            break;
        case FX_SIGNALS:
            signals(L, tt);
            break;
        case FX_GLYPHS:
            glyphs(L, tt);
            break;
        case FX_SOLAR: solar(tt); break;
        case FX_GLACIER: glacier(tt); break;
        case FX_CITRON: citron(tt); break;
        case FX_SIENNA: sienna(tt); break;
        case FX_ECLIPSE:
            eclipse(L, tt);
            break;
        case FX_EMBERS:
            embers(L, tt);
            break;
        case FX_SUNSET:
        case FX_SUNSETFLOOR:
            sunset(L, tt);
            break;
        case FX_SHADE:
            blend(false);
            grad(0, L->y0, w, 240 - L->y0, alpha(th->shadow, 0), alpha(th->shadow, L->a));
            break;
        case FX_VIGNETTE:
            vignette(L->a);
            break;
        }
        z += .0001f;
    }
    blend(false);
    u64 elapsed = svcGetSystemTick() - start;
    int b = bottom ? 1 : 0;
    samples[b]++;
    ticks[b] += elapsed;
    if (elapsed > peak[b])
        peak[b] = elapsed;
    if (dev_enabled() && start - report_tick > 5ULL * SYSCLOCK_ARM11 && samples[0] && samples[1]) {
        log_printf("scene: %s top %.3f ms worst %.3f bottom %.3f ms worst %.3f (%lu/%lu draws; %.2f/%.2f draws/s)\n",
                   th->id, ticks[0] * 1000.0 / SYSCLOCK_ARM11 / samples[0],
                   peak[0] * 1000.0 / SYSCLOCK_ARM11, ticks[1] * 1000.0 / SYSCLOCK_ARM11 / samples[1],
                   peak[1] * 1000.0 / SYSCLOCK_ARM11, (unsigned long)samples[0],
                   (unsigned long)samples[1],
                   samples[0] * (double)SYSCLOCK_ARM11 / (start - report_tick),
                   samples[1] * (double)SYSCLOCK_ARM11 / (start - report_tick));
#ifdef __3DS__
        // Separate a cadence bottleneck outside scene generation from a heavy
        // effect. Menu-only diagnostics; no gameplay path or pacing change.
        log_printf("menu-cost: %s prior CPU %.3f GPU %.3f heap %.3f ms/frame\n",
                   th->id, C3D_GetProcessingTime(), C3D_GetDrawingTime(),
                   video_take_heap_flush_ticks() * 1000.0 / SYSCLOCK_ARM11 / samples[0]);
#endif
        memset(samples, 0, sizeof(samples));
        memset(ticks, 0, sizeof(ticks));
        memset(peak, 0, sizeof(peak));
        report_tick = start;
    }
}
