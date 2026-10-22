// Minimal host renderer: writes the production drawing calls to SVG.
#ifndef HOST_CITRO2D_H
#define HOST_CITRO2D_H
#include "3ds.h"
typedef void *C2D_Font;
typedef void *C2D_TextBuf;
typedef struct { char text[2048]; C2D_Font font; } C2D_Text;
enum { GPU_A4, GPU_A8 };
typedef struct { int cellHeight,cellWidth,baselinePos,nRows,nLines,sheetWidth,sheetSize,sheetFmt; u8 *sheetData; } TGLP_s;
typedef struct { TGLP_s *tglp; } FINF_s;
typedef struct { float left, glyphWidth, charWidth; } charWidthInfo_s;
typedef void C3D_Tex;
typedef struct { unsigned short width, height; float left, top, right, bottom; } Tex3DS_SubTexture;
typedef void C3D_RenderTarget;
enum { C2D_WithColor=1, C2D_AlignCenter=2, C2D_AlignRight=4, C2D_WordWrap=8, C2D_AtBaseline=16 };
static inline u32 C2D_Color32(u8 r, u8 g, u8 b, u8 a) { return r | g<<8 | b<<16 | (u32)a<<24; }
C2D_TextBuf C2D_TextBufNew(unsigned n);
void C2D_TextBufDelete(C2D_TextBuf b);
void C2D_TextBufClear(C2D_TextBuf b);
bool C2D_TextParse(C2D_Text *t, C2D_TextBuf b, const char *s);
bool C2D_TextFontParse(C2D_Text *t, C2D_Font f, C2D_TextBuf b, const char *s);
void C2D_TextOptimize(C2D_Text *t);
FINF_s *C2D_FontGetInfo(C2D_Font f);
int C2D_FontGlyphIndexFromCodePoint(C2D_Font f, u32 ch);
charWidthInfo_s *C2D_FontGetCharWidthInfo(C2D_Font f, int index);
void C2D_TextGetDimensions(C2D_Text *t, float sx, float sy, float *w, float *h);
void C2D_DrawText(C2D_Text *t, u32 flags, float x, float y, float z, float sx, float sy, ...);
void C2D_DrawRectSolid(float x, float y, float z, float w, float h, u32 c);
void C2D_DrawRectangle(float x, float y, float z, float w, float h, u32 a, u32 b, u32 c, u32 d);
void C2D_DrawCircleSolid(float x, float y, float z, float r, u32 c);
void C2D_DrawLine(float x, float y, u32 a, float x2, float y2, u32 b, float width, float z);
void C2D_DrawTriangle(float x,float y,u32 a,float x2,float y2,u32 b,float x3,float y3,u32 c,float z);
void C2D_DrawEllipse(float x,float y,float z,float w,float h,u32 a,u32 b,u32 c,u32 d);
#endif
