// Menu-only Citro2D 1.7.0 buffer submission. Same shader, transforms, state
// transitions, indices and vertices as the SDK primitives; append complete
// vertices together instead of calling AppendVtx once per vertex. No new GPU
// allocation or rendering hook. Keep the vendored ABI in sync with the SDK.
#pragma once
#ifdef __3DS__
#include <citro2d.h>
#include "../vendor/citro2d/internal.h"
#include <stddef.h>
#include <string.h>
_Static_assert(sizeof(C2Di_Vertex) == 32, "Citro2D vertex ABI");
_Static_assert(offsetof(C2Di_Vertex, color) == 28, "Citro2D colour ABI");
// Values checked against libcitro2d.a 1.7.0 (also hashed by toolchain.lock).
_Static_assert(sizeof(C2Di_Context) == 1448, "Citro2D context ABI");
_Static_assert(offsetof(C2Di_Context, vtxBuf) == 1280, "Citro2D vertex buffer ABI");
_Static_assert(offsetof(C2Di_Context, idxBuf) == 1284, "Citro2D index buffer ABI");
_Static_assert(offsetof(C2Di_Context, vtxBufSize) == 1288, "Citro2D vertex capacity ABI");
_Static_assert(offsetof(C2Di_Context, vtxBufPos) == 1292, "Citro2D vertex position ABI");
_Static_assert(offsetof(C2Di_Context, idxBufSize) == 1296, "Citro2D index capacity ABI");
_Static_assert(offsetof(C2Di_Context, idxBufPos) == 1300, "Citro2D index position ABI");
_Static_assert(offsetof(C2Di_Context, flags) == 1308, "Citro2D flags ABI");
_Static_assert(offsetof(C2Di_Context, curTex) == 1440, "Citro2D texture ABI");

// Single-character system-font metadata, read only while preparing the menu.
// Layout from citro2d v1.7.0 source/text.c, under vendor/citro2d/LICENSE.
typedef struct {
    u32 lineNo;
    C3D_Tex *sheet;
    float xPos, width;
    struct { float left, top, right, bottom; } texcoord;
    u32 wordNo;
} scene_sdk_glyph;
struct C2D_TextBuf_s {
    u32 reserved[2];
    size_t glyphCount, glyphBufSize;
    scene_sdk_glyph glyphs[];
};
_Static_assert(sizeof(scene_sdk_glyph) == 36, "Citro2D glyph ABI");
_Static_assert(offsetof(struct C2D_TextBuf_s, glyphs) == 16, "Citro2D text buffer ABI");
typedef struct {
    C3D_Tex *sheet;
    float x, width, height, left, top, right, bottom;
} scene_glyph;
static inline scene_glyph scene_cache_glyph(const C2D_Text *text, float scale) {
    const scene_sdk_glyph *g = &text->buf->glyphs[text->begin];
    float s = scale * (30.0f / fontGetGlyphInfo(fontGetSystemFont())->cellHeight);
    return (scene_glyph){g->sheet, g->xPos*s, g->width*s, scale*30.0f,
        g->texcoord.left, g->texcoord.top, g->texcoord.right, g->texcoord.bottom};
}

static inline C2Di_Context *scene_reserve(unsigned vertices, unsigned indices, u32 mode, C3D_Tex *texture) {
    C2Di_Context *ctx = C2Di_GetContext();
    if (!(ctx->flags & C2DiF_Active) || ctx->vtxBufPos > ctx->vtxBufSize ||
        ctx->idxBufPos > ctx->idxBufSize || vertices > ctx->vtxBufSize - ctx->vtxBufPos ||
        indices > ctx->idxBufSize - ctx->idxBufPos)
        return NULL;
    C2Di_SetMode(mode);
    if (texture)
        C2Di_SetTex(texture);
    if (ctx->flags & C2DiF_DirtyAny)
        C2Di_Update();
    return ctx;
}
static inline void scene_vertex(C2Di_Vertex *v, float x, float y, float z,
                                 float u, float t, float p, float q, u32 color) {
    v->pos[0]=x; v->pos[1]=y; v->pos[2]=z;
    v->texcoord[0]=u; v->texcoord[1]=t;
    v->ptcoord[0]=p; v->ptcoord[1]=q; v->color=color;
}
static inline C2Di_Vertex *scene_quad(C2Di_Context *ctx) {
    unsigned base = ctx->vtxBufPos;
    u16 *ix = ctx->idxBuf + ctx->idxBufPos;
    ix[0] = base; ix[1] = base+2; ix[2] = base+1;
    ix[3] = base+1; ix[4] = base+2; ix[5] = base+3;
    ctx->idxBufPos += 6;
    ctx->vtxBufPos += 4;
    return ctx->vtxBuf + base;
}
static inline void scene_solid_triangle(C2Di_Context *ctx, float ax, float ay, u32 ac, float bx, float by, u32 bc,
                                  float cx, float cy, u32 cc, float z) {
    if (!ctx) ctx = scene_reserve(3, 3, C2DiF_Mode_Solid, NULL);
    if (!ctx) return;
    unsigned base = ctx->vtxBufPos;
    u16 *ix = ctx->idxBuf + ctx->idxBufPos;
    // SDK AppendTri order.
    ix[0] = base; ix[1] = base+1; ix[2] = base+2;
    ctx->idxBufPos += 3;
    C2Di_Vertex *v = ctx->vtxBuf + base;
    scene_vertex(&v[0],ax,ay,z,0,0,0,0,ac); scene_vertex(&v[1],bx,by,z,0,0,0,0,bc); scene_vertex(&v[2],cx,cy,z,0,0,0,0,cc);
    ctx->vtxBufPos += 3;
}
// Two original A/B/C, A/C/D triangles, sharing their identical corners.
static inline void scene_solid_cell(C2Di_Context *ctx, float ax,float ay,float bx,float by,
                                    float cx,float cy,float dx,float dy,u32 color,float z) {
    if (!ctx) {
        scene_solid_triangle(NULL,ax,ay,color,bx,by,color,cx,cy,color,z);
        scene_solid_triangle(NULL,ax,ay,color,cx,cy,color,dx,dy,color,z);
        return;
    }
    unsigned base=ctx->vtxBufPos;
    u16 *ix=ctx->idxBuf+ctx->idxBufPos;
    ix[0]=base; ix[1]=base+1; ix[2]=base+2;
    ix[3]=base; ix[4]=base+2; ix[5]=base+3;
    ctx->idxBufPos+=6; ctx->vtxBufPos+=4;
    C2Di_Vertex *v=ctx->vtxBuf+base;
    scene_vertex(v,ax,ay,z,0,0,0,0,color);
    scene_vertex(v+1,bx,by,z,0,0,0,0,color);
    scene_vertex(v+2,cx,cy,z,0,0,0,0,color);
    scene_vertex(v+3,dx,dy,z,0,0,0,0,color);
}
static inline void scene_triangle(float ax, float ay, u32 ac, float bx, float by, u32 bc,
                                  float cx, float cy, u32 cc, float z) {
    scene_solid_triangle(NULL,ax,ay,ac,bx,by,bc,cx,cy,cc,z);
}
static inline void scene_solid_line(C2Di_Context *ctx, float ax,float ay,float bx,float by,float cx,float cy,float dx,float dy,u32 a,u32 b,float z) {
    if (!ctx) ctx = scene_reserve(4,6,C2DiF_Mode_Solid,NULL);
    if (!ctx) return;
    // Left/right endpoints in the same order as SDK DrawLine.
    C2Di_Vertex *v = scene_quad(ctx);
    scene_vertex(&v[0],ax,ay,z,0,0,0,0,a); scene_vertex(&v[1],bx,by,z,0,0,0,0,b);
    scene_vertex(&v[2],cx,cy,z,0,0,0,0,a); scene_vertex(&v[3],dx,dy,z,0,0,0,0,b);
}
static inline void scene_line_quad(float ax,float ay,float bx,float by,float cx,float cy,float dx,float dy,u32 a,u32 b,float z) {
    scene_solid_line(NULL,ax,ay,bx,by,cx,cy,dx,dy,a,b,z);
}
// Bounded solid-only runs may reserve once. No flush, texture or mode change
// may occur before the run ends. NULL retains per-primitive capacity checks.
static inline C2Di_Context *scene_solid_begin(unsigned vertices, unsigned indices) {
    return scene_reserve(vertices,indices,C2DiF_Mode_Solid,NULL);
}
static inline void scene_rectangle(float x,float y,float z,float w,float h,u32 a,u32 b,u32 c,u32 d) {
    C2Di_Context *ctx = scene_reserve(4,6,C2DiF_Mode_Solid,NULL);
    if (!ctx) return;
    C2Di_Vertex *v = scene_quad(ctx);
    scene_vertex(&v[0],x,y,z,0,0,0,0,a); scene_vertex(&v[1],x+w,y,z,0,0,0,0,b);
    scene_vertex(&v[2],x,y+h,z,0,0,0,0,c); scene_vertex(&v[3],x+w,y+h,z,0,0,0,0,d);
}
static inline void scene_radial(C3D_Tex *texture,float x,float y,float z,float w,float h,u32 color) {
    C2Di_Context *ctx = C2Di_GetContext();
    u32 mode = (ctx->flags & C2DiF_TintMode_Mask) >> (C2DiF_TintMode_Shift-C2DiF_Mode_Shift);
    ctx = scene_reserve(4,6,mode,texture);
    if (!ctx) return;
    C2Di_Vertex *v = scene_quad(ctx);
    scene_vertex(&v[0],x,y,z,0,1,0,1,color);
    scene_vertex(&v[1],x+w,y,z,1,1,0,1,color);
    scene_vertex(&v[2],x,y+h,z,0,0,0,1,color);
    scene_vertex(&v[3],x+w,y+h,z,1,0,0,1,color);
}
static inline void scene_draw_glyph(const scene_glyph *g, float x, float y, float z, u32 color) {
    C2Di_Context *ctx = scene_reserve(4,6,C2DiF_Mode_Text,g->sheet);
    if (!ctx) return;
    x += g->x;
    C2Di_Vertex *v = scene_quad(ctx);
    scene_vertex(&v[0],x,y,z,g->left,g->top,0,1,color);
    scene_vertex(&v[1],x+g->width,y,z,g->right,g->top,0,1,color);
    scene_vertex(&v[2],x,y+g->height,z,g->left,g->bottom,0,1,color);
    scene_vertex(&v[3],x+g->width,y+g->height,z,g->right,g->bottom,0,1,color);
}
#else
typedef void C2Di_Context;
#define scene_solid_begin(vertices, indices) NULL
#define scene_solid_triangle(ctx, ...) C2D_DrawTriangle(__VA_ARGS__)
static inline void scene_solid_cell(C2Di_Context *ctx, float ax,float ay,float bx,float by,
                                    float cx,float cy,float dx,float dy,u32 color,float z) {
    (void)ctx;
    C2D_DrawTriangle(ax,ay,color,bx,by,color,cx,cy,color,z);
    C2D_DrawTriangle(ax,ay,color,cx,cy,color,dx,dy,color,z);
}
#define scene_solid_line(ctx, ...) scene_line_quad(__VA_ARGS__)
#define scene_triangle C2D_DrawTriangle
#define scene_rectangle C2D_DrawRectangle
static inline void scene_line_quad(float ax,float ay,float bx,float by,float cx,float cy,float dx,float dy,u32 a,u32 b,float z) {
    C2D_DrawTriangle(ax,ay,a,bx,by,b,cx,cy,a,z);
    C2D_DrawTriangle(bx,by,b,dx,dy,b,cx,cy,a,z);
}
#endif
