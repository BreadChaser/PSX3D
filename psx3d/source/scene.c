// Approved round-3 backgrounds, ported from the project's UI mock. Every
// frame is a pure function of time. Menu-only: no thread, timer, game hook or
// retained linear allocation. See docs/dev/themes.md and THEME-BACKGROUNDS.md.
#include "scene.h"
#include "devtools.h"
#include "psx3d.h"
#include "scene_data.h"
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
typedef struct {
    float epoch[COUNT(FL)];
    bool valid[COUNT(FL)];
    point_t p[2][COUNT(FL)][31];
} flow_cache_t;
typedef struct {
    flow_cache_t flow[2];
    float sine[1024], decay[256];
    C3D_Tex radial;
    Tex3DS_SubTexture radial_sub;
    C2D_Text glyph[12];
    C2D_TextBuf text;
    float grain[20][50];
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
static void ring(float x, float y, float r, float lw, u32 col, float a) {
    for (int i = 0; i < 64; i++) {
        float aa = i * TAU / 64, bb = (i + 1) * TAU / 64;
        line(x + cs(aa) * r, y + sn(aa) * r, x + cs(bb) * r, y + sn(bb) * r, col, a, a, lw);
    }
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
static void stars(const layer_t *L, float t) {
    int n = (int)((L->n ? L->n : 110) * clamp(strength) + .5f);
    if (n > COUNT(STARS))
        n = COUNT(STARS);
    for (int i = 0; i < n; i++) {
        const stars_seed_t *q = &STARS[i];
        float x = fract(q->x + t * L->drift / w) * w, y = q->y * 240;
        float a = q->a * strength * (.6f + .4f * sn(t * q->f + q->p));
        if (q->big)
            glow(x, y, 3, th->fx_colour, a * .5f, 1);
        blend(false);
        rect(x, y, 1, 1, alpha(mix(th->fx_colour, th->scene_white, .6f), a));
    }
}
static void starfield(const layer_t *L, float t) {
    float cx = (L->cx ? L->cx : .5f) * w, cy = (L->cy ? L->cy : .4f) * 240;
    int n = (int)((L->n ? L->n : 90) * fmaxf(.3f, strength) + .5f);
    if (n > COUNT(SF))
        n = COUNT(SF);
    blend(true);
    for (int i = 0; i < n; i++) {
        const sf_seed_t *q = &SF[i];
        float d = .18f + .82f * fract(q->z - t * .018f), d0 = fminf(1, d + .018f);
        float x = cx + q->x / d * 1.6f, y = cy + q->y / d * 1.1f;
        if (x < -10 || x > w + 10 || y < -10 || y > 250)
            continue;
        float a = q->b * strength * smooth(1, .7f, d) * smooth(.18f, .3f, d);
        line(cx + q->x / d0 * 1.6f, cy + q->y / d0 * 1.1f, x, y, th->fx_colour, 0, a * .45f,
             .8f + .18f / d);
        rect(x - .5f, y - .5f, d < .4f ? 1.6f : 1, d < .4f ? 1.6f : 1,
             alpha(mix(th->fx_colour, th->scene_white, .55f), a));
        if (d < .35f)
            glow(x, y, 3.5f, th->fx_colour, a * .35f, 1);
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
        float x =
            fract(q->x + t * (q->l ? 3.2f : 1.6f) / (w + 60)) * (w + 60) - 30 + 6 * sn(t * q->w + q->p);
        p[i].x = x;
        p[i].y = q->y * 240 + 5 * sn(t * q->w * .8f + q->p * 2);
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
                float x = fract((k * 97 + t * (6 + k)) / (w + 40)) * (w + 40) - 20;
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
        float pos = fract(q->ph + t * q->v) * (steps + 8) - 4;
        for (int k = 0; k < 4; k++) {
            float d0 = pos - (k + 1) * .9f, d1 = pos - k * .9f;
            if (d1 <= 0 || d0 >= steps)
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
    for (float x = 12; x < w; x += 24)
        grad(x, 0, 1, 240, alpha(th->fx_colour2, ga * .6f), alpha(th->fx_colour2, ga * .25f));
    for (float y = 12; y < 240; y += 24)
        rect(0, y, w, 1, alpha(th->fx_colour2, ga * (y > 165 && !bottom ? .35f : 1)));
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
        inv_radius[k] = 1 / br[k];
    }
    blend(false);
    // The smaller Old bottom screen keeps the moving four-light glyph field,
    // with 16x12 samples instead of 20x15. New and Old top stay unchanged.
    int dx_cell = g_new3ds ? 8 : (bottom ? 20 : 16);
    int dy_cell = g_new3ds ? 12 : (bottom ? 20 : 16);
    for (int j = 0; j < 240 / dy_cell; j++)
        for (int i = 0; i < (int)(w / dx_cell); i++) {
            float x = (i + .5f) * dx_cell, y = (j + .5f) * dy_cell, b = 0;
            for (int k = 0; k < 4; k++) {
                float dx = (x - bx[k]) * inv_radius[k], dy = (y - by[k]) * inv_radius[k], d = dx * dx + dy * dy;
                if (d < 4)
                    b += decay(d * 1.6f);
            }
            float gx = x / 8 - .5f, gy = y / 12 - .5f;
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
    for (int k = 0; k < 4; k++)
        glow(bx[k], by[k], br[k] * 1.3f, th->fx_colour, .1f * strength, .9f);
    blend(false);
    for (int y = 0; y < 240; y += 2)
        rect(0, y, w, 1, alpha(th->shadow, .16f));
}
static point_t ember_at(const em_seed_t *q, float t, float sp) {
    return (point_t){q->x * w + 14 * sn(t * q->w + q->p) + 5 * sn(t * q->w * 2.7f + q->p * 3),
                     255 - fract((q->y * 270 + t * sp) / 270) * 270};
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
        if (prev.y < p.y)
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
static void corona(const layer_t *L, float t) {
    float x = w * .5f, y = 96, R = 70;
    glow(x, y, R * 3.4f, th->fx_colour2, .16f * strength, .8f);
    glow(x, y, R * 2, th->fx_colour, .22f * strength, 1);
    blend(true);
    for (int i = 0; i < COUNT(CR); i++) {
        const cr_seed_t *q = &CR[i];
        float a = q->a + t * .004f, pol = cs(a + .35f);
        pol *= pol;
        float nz = .5f + .3f * sn(a * 3 + t * .05f + q->f1) + .2f * sn(a * 7 - t * .037f + q->f2),
              len = R * (.18f + .55f * nz + .9f * pol * nz), ca = cs(a), sa = sn(a), r0 = R * .98f,
              ww = q->w * (1.6f + 2.2f * pol), al = q->b * strength * (.09f + .07f * pol);
        u32 c = alpha(mix(th->fx_colour, th->scene_white, .5f), al);
        tri(x + ca * r0 - sa * ww, y + sa * r0 + ca * ww, c, x + ca * (r0 + len), y + sa * (r0 + len),
            alpha(th->fx_colour2, 0), x + ca * r0 + sa * ww, y + sa * r0 - ca * ww, c);
    }
    blend(false);
    C2D_DrawCircleSolid(x + depth, y, z, R, th->moon);
    blend(true);
    ring(x, y, R + .6f, 9, th->fx_colour, .08f * strength);
    ring(x, y, R + .6f, 3.5f, th->fx_colour, .28f * strength);
    ring(x, y, R + .6f, 1.2f, mix(th->fx_colour, th->scene_white, .7f), .9f * strength);
    float ba = -2.2f + t * .035f, bx = x + cs(ba) * (R + .5f), by = y + sn(ba) * (R + .5f),
          b = (.75f + .25f * sn(t * .6f)) * strength;
    glow(bx, by, 22, th->scene_white, .35f * b, 1);
    glow(bx, by, 6, th->scene_white, .8f * b, 1);
    for (int k = 0; k < 2; k++) {
        float dx = k ? 0 : 16, dy = k ? 16 : 0;
        line(bx - dx, by - dy, bx, by, th->scene_white, 0, .5f * b, .8f);
        line(bx, by, bx + dx, by + dy, th->scene_white, .5f * b, 0, .8f);
    }
}
static u32 sunset_sky(float y) {
    static const float at[] = {0, .38f, .7f, .88f, 1};
    float p = clamp(y / 141);
    for (int i = 1; i < 5; i++)
        if (p <= at[i])
            return mix(th->sun_sky[i - 1], th->sun_sky[i], (p - at[i - 1]) / (at[i] - at[i - 1]));
    return th->sun_sky[4];
}
static float ridge(int side, float x) {
    float d = side < 0 ? (150 - x) / 150 : (x - 250) / 150;
    return d <= 0 ? 0
                  : smooth(0, .55f, d) * (13 + 8 * sn(x * .04f + side) + 3.5f * sn(x * .11f + 2 * side));
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
    static const float at[] = {0, .38f, .7f, .88f, 1};
    for (int k = 1; k < 5; k++)
        grad(0, at[k - 1] * 141, w, (at[k] - at[k - 1]) * 141, th->sun_sky[k - 1], th->sun_sky[k]);
    for (int i = 0; i < COUNT(SUN_STARS); i++) {
        float y = SUN_STARS[i].y, a = SUN_STARS[i].a *
                                      (.55f + .45f * sn(t * SUN_STARS[i].f + SUN_STARS[i].p)) *
                                      (1 - y / 95);
        rect(SUN_STARS[i].x, y, 1, 1, alpha(th->scene_white, a));
    }
    glow(200, 120, 230, th->sun_glow, .3f, .6f);
    glow(200, 100, 120, th->sun_hot, .22f, 1);
    blend(false);
    // Horizontal chords keep the moving cuts inside the solar disc.
    for (int y = 58; y < 140; y++) {
        float dy = (y + .5f - 142) / 84.f, half = 84 * sqrtf(fmaxf(0, 1 - dy * dy));
        u32 c = mix(th->sun_disc[0], th->sun_disc[1], clamp((y - 58) / 37.8f));
        if (y > 96)
            c = mix(th->sun_disc[1], th->sun_disc[2], clamp((y - 96) / 25.2f));
        if (y > 121)
            c = mix(th->sun_disc[2], th->sun_disc[3], clamp((y - 121) / 21.f));
        for (int k = 0; k < 7; k++) {
            float p = fract((k + t * .18f) / 7), cut = 142 - 84 * .52f + p * 84 * .52f;
            if (y >= cut && y < cut + .4f + p * p * 7.5f)
                c = sunset_sky(y);
        }
        rect(200 - half, y, half * 2, 1, c);
    }
    for (int side = -1; side <= 1; side += 2) {
        float start = side < 0 ? 0 : 250, end = side < 0 ? 150 : 400;
        for (float x = start; x < end; x += 5) {
            float y0 = 140 - ridge(side, x), y1 = 140 - ridge(side, x + 5);
            u32 a = th->ridge_high, b = th->ridge_low;
            tri(x, y0, a, x + 5, y1, a, x + 5, 140, b);
            tri(x, y0, a, x + 5, 140, b, x, 140, b);
        }
        blend(true);
        for (float x = start; x < end; x += 5) {
            float a = side < 0 ? .85f - (x / 150) * .8f : .05f + (x - 250) / 150 * .8f;
            line(x, 140 - ridge(side, x), x + 5, 140 - ridge(side, x + 5), th->sun_rim, a * .18f,
                 a * .18f, 4);
            line(x, 140 - ridge(side, x), x + 5, 140 - ridge(side, x + 5), th->sun_rim, a, a, 1.1f);
        }
        blend(false);
    }
    grad(0, 140, w, 25, th->sun_floor[0], th->sun_floor[1]);
    grad(0, 165, w, 75, th->sun_floor[1], th->sun_floor[2]);
    glow(200, 144, 150, th->sun_glow, .35f, .22f);
    blend(true);
    for (int i = 0; i < 9; i++) {
        float d = i / 8.f, y = 143 + d * d * 34,
              ww = (110 - d * 60) * (.85f + .15f * sn(t * 1.1f + i * 1.9f));
        line(200 - ww, y, 200, y, th->sun_hot, 0, .32f * (1 - d), 1);
        line(200, y, 200 + ww, y, th->sun_hot, .32f * (1 - d), 0, 1);
    }
    for (int k = 0; k < 40; k++) {
        float zz = .6f + k - fract(t * 1.1f), y = 140 + 210 / zz;
        if (zz <= .2f || y > 241 || 210 / zz - 210 / (zz + 1) < 3)
            continue;
        float d = (y - 140) / 100, a = smooth(.02f, .14f, d) * (1 - .72f * smooth(.2f, .62f, d));
        line(0, y, 400, y, th->fx_colour, a * .16f, a * .16f, 3.2f);
        line(0, y, 400, y, th->fx_colour, a * .9f, a * .9f, 1);
    }
    for (int i = -16; i <= 16; i++) {
        float x0 = 200 + i * 4.2f, x1 = 200 + i * 200;
        for (int k = 0; k < 3; k++) {
            float a0 = k / 3.f, a1 = (k + 1) / 3.f;
            line(x0 + (x1 - x0) * a0, 140 + 160 * a0, x0 + (x1 - x0) * a1, 140 + 160 * a1,
                 th->fx_colour2, k == 0 ? 0 : .28f,
                 k == 0   ? .75f
                 : k == 1 ? .28f
                          : .08f,
                 1);
        }
    }
    grad(0, 126, w, 14, alpha(th->sun_glow, 0), alpha(th->sun_hot, .3f));
    grad(0, 140, w, 12, alpha(th->sun_hot, .3f), alpha(th->sun_glow, 0));
    line(0, 139.5f, 200, 139.5f, th->scene_white, .35f, .8f, 1);
    line(200, 139.5f, 400, 139.5f, th->scene_white, .8f, .35f, 1);
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
        case FX_STARFIELD:
            starfield(L, tt);
            break;
        case FX_CORONA:
            corona(L, tt);
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
