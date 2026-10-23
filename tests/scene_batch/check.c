// Exercise the production fast path against the pinned SDK's actual primitives.
#include <citro2d.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "scene_batch.h"
extern C2Di_Context __C2Di_Context;
static C2Di_Vertex vertices[64];
static u16 indices[96];
// GPU state writes are inert; the SDK's state machine and append code run intact.
C3D_FVec C3D_FVUnif[2][C3D_FVUNIF_COUNT];
bool C3D_FVUnifDirty[2][C3D_FVUNIF_COUNT];
static C3D_TexEnv env[6];
CFNT_s *g_sharedFont;
Result fontEnsureMapped(void) { assert(g_sharedFont); return 0; }
C3D_TexEnv *C3D_GetTexEnv(int id) { return &env[id]; }
void C3D_DrawElements(GPU_Primitive_t primitive, int count, int type, const void *indices) {
    (void)primitive; (void)count; (void)type; (void)indices;
}
void C3D_TexBind(int unit, C3D_Tex *tex) { (void)unit; (void)tex; }
void C3D_ProcTexBind(int unit, C3D_ProcTex *tex) { (void)unit; (void)tex; }
void C3D_ProcTexLutBind(GPU_PROCTEX_LUTID id, C3D_ProcTexLut *lut) { (void)id; (void)lut; }
static void reset(void) {
    memset(&__C2Di_Context, 0, sizeof(__C2Di_Context));
    memset(vertices, 0xa5, sizeof(vertices));
    memset(indices, 0xa5, sizeof(indices));
    __C2Di_Context.vtxBuf = vertices;
    __C2Di_Context.idxBuf = indices;
    __C2Di_Context.vtxBufSize = 64;
    __C2Di_Context.idxBufSize = 96;
    __C2Di_Context.flags = C2DiF_Active | C2DiF_DirtyProj;

}
static C2Di_Vertex expected_v[64];
static u16 expected_i[96];
static void save(void) {
    memcpy(expected_v,vertices,sizeof(vertices));
    memcpy(expected_i,indices,sizeof(indices));
}
static void same(unsigned v, unsigned i) {
    assert(__C2Di_Context.vtxBufPos == v && __C2Di_Context.idxBufPos == i);
    assert(!memcmp(expected_v,vertices,sizeof(vertices)));
    assert(!memcmp(expected_i,indices,sizeof(indices)));
}
int main(void) {
    reset(); C2D_DrawTriangle(1,2,3,4,5,6,7,8,9,.1f); save();
    reset(); scene_triangle(1,2,3,4,5,6,7,8,9,.1f); same(3,3);
    reset(); C2D_DrawRectangle(1,2,.1f,4,5,6,7,8,9); save();
    reset(); scene_rectangle(1,2,.1f,4,5,6,7,8,9); same(4,6);
    // Horizontal SDK line is exactly the scene parallelogram representation.
    reset(); C2D_DrawLine(1,2,3,5,2,6,2,.1f); save();
    reset(); scene_line_quad(1,1,5,1,1,3,5,3,3,6,.1f); same(4,6);
    C3D_Tex texture = {0};
    Tex3DS_SubTexture sub = {128,128,0,1,1,0};
    C2D_ImageTint tint; C2D_PlainImageTint(&tint,0xabcdef12,1);
    reset(); C2D_DrawImageAt((C2D_Image){&texture,&sub},3,4,.1f,&tint,2,3); save();
    reset(); scene_radial(&texture,3,4,.1f,256,384,0xabcdef12); same(4,6);
    assert(__C2Di_Context.curTex == &texture);
    assert(!(__C2Di_Context.flags & C2DiF_DirtyAny));
    // Compare cached single-glyph output to the actual DrawText implementation.
    static CFNT_s font;
    static TGLP_s glyph_info;
    font.finf.tglp = &glyph_info;
    font.finf.lineFeed = 32;
    glyph_info.cellHeight = 30;
    g_sharedFont = &font;
    struct { struct C2D_TextBuf_s head; scene_sdk_glyph glyph; } buf = {0};
    buf.glyph = (scene_sdk_glyph){0,&texture,2,11,{.1f,.8f,.3f,.4f},0};
    struct C2D_Font_s custom = {&font, &texture, 1};
    C2D_Text text = {.buf=&buf.head,.begin=0,.end=1,.width=11,.lines=1,.words=1,.font=&custom};
    reset(); C2D_DrawText(&text,C2D_WithColor,2,3,.1f,.25f,.25f,0xabcdef12); save();
    scene_glyph cached = scene_cache_glyph(&text,.25f);
    reset(); scene_draw_glyph(&cached,2,3,.1f,0xabcdef12); same(4,6);
    reset(); scene_radial(&texture,3,4,.1f,256,384,0xabcdef12);
    // Same state stays clean; a mode change goes through the SDK state machine.
    scene_radial(&texture,0,0,0,1,1,0); assert(!(__C2Di_Context.flags & C2DiF_DirtyAny));
    scene_triangle(0,0,0,1,1,1,2,2,2,0); assert(!(__C2Di_Context.flags & C2DiF_Mode_Mask));
    reset(); __C2Di_Context.vtxBufSize=4; __C2Di_Context.idxBufSize=6;
    scene_rectangle(0,0,0,1,1,0,0,0,0); assert(__C2Di_Context.vtxBufPos==4);
    save(); scene_rectangle(0,0,0,1,1,0,0,0,0); same(4,6);
    reset(); __C2Di_Context.flags=0; save();
    scene_rectangle(0,0,0,1,1,0,0,0,0); same(0,0);
    puts("scene batch: actual SDK triangle/rectangle/line/radial/glyph output, state and capacity PASS");
}
