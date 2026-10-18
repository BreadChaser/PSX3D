/* Minimal synchronous citro3d implementation for gpu_pica visual inspection.
 * GPL-2.0-or-later. Models this renderer's shader and TEV/blend configuration,
 * not PICA precision, its texture cache, command processor, or hardware bugs. */
#include "citro3d.h"
#include <math.h>
#include <string.h>
#include <assert.h>

static C3D_RenderTarget rt;
static C3D_BufInfo buf;
static C3D_Tex texture;
static C3D_TexEnv envs[6];
static float proj[4][4], texscale[2];
static int viewport[4], scissor[4], scissor_on;
static int blend[6], alpha_on, alpha_func, alpha_ref;
static float constant_alpha;

unsigned sim_tiled(int x, int y, int w)
{
    unsigned m = (x&1) | ((y&1)<<1) | ((x&2)<<1) | ((y&2)<<2) | ((x&4)<<2) | ((y&4)<<3);
    return ((y>>3)*(w>>3)+(x>>3))*64+m;
}

void C3D_TexBind(int u, C3D_Tex *t) { assert(u == 0); texture = *t; }
C3D_RenderTarget *C3D_RenderTargetCreateFromTex(C3D_Tex *t, int f, int l, int d)
{ (void)f; (void)l; (void)d; rt.tex = t; return &rt; }
bool C3D_FrameDrawOn(C3D_RenderTarget *t) { rt = *t; return true; }
C3D_BufInfo *C3D_GetBufInfo(void) { return &buf; }
int BufInfo_Add(C3D_BufInfo *b, const void *d, int s, int n, u64 p)
{ assert(n == 3 && p == 0x210); b->data=d; b->stride=s; return 0; }
void C3D_SetViewport(u32 x,u32 y,u32 w,u32 h)
{ viewport[0]=x; viewport[1]=y; viewport[2]=w; viewport[3]=h; }
void C3D_SetScissor(int m,u32 l,u32 t,u32 r,u32 b)
{ scissor_on=m!=GPU_SCISSOR_DISABLE; scissor[0]=l; scissor[1]=t; scissor[2]=r; scissor[3]=b; }
void C3D_AlphaBlend(int a,int b,int c,int d,int e,int f)
{ blend[0]=a; blend[1]=b; blend[2]=c; blend[3]=d; blend[4]=e; blend[5]=f; }
void C3D_BlendingColor(u32 c) { constant_alpha=(c>>24)/255.f; }
void C3D_AlphaTest(bool e,int f,int r) { alpha_on=e; alpha_func=f; alpha_ref=r; }
C3D_TexEnv *C3D_GetTexEnv(int i) { assert(i<6); return &envs[i]; }
void C3D_TexEnvInit(C3D_TexEnv *e)
{ memset(e,0,sizeof(*e)); e->scale[0]=e->scale[1]=1; }
static int channel(int m) { return m==C3D_Alpha; }
void C3D_TexEnvSrc(C3D_TexEnv *e,int m,int a,int b,int c)
{
    for(int k=0;k<2;k++) if(m==C3D_Both||k==channel(m))
    { e->src[k][0]=a; e->src[k][1]=b; e->src[k][2]=c; }
}
void C3D_TexEnvFunc(C3D_TexEnv *e,int m,int f)
{ for(int k=0;k<2;k++) if(m==C3D_Both||k==channel(m)) e->func[k]=f; }
void C3D_TexEnvScale(C3D_TexEnv *e,int m,int s)
{ assert(s==GPU_TEVSCALE_2); for(int k=0;k<2;k++) if(m==C3D_Both||k==channel(m)) e->scale[k]=2; }
void C3D_TexEnvOpAlpha(C3D_TexEnv *e,int a,int b,int c)
{ (void)b; (void)c; assert(a==GPU_TEVOP_A_ONE_MINUS_SRC_ALPHA); e->inv_alpha=1; }
int shaderInstanceGetUniformLocation(void *s,const char *n)
{ (void)s; return !strcmp(n,"proj")?0:4; }
void C3D_FVUnifSet(int t,int id,float x,float y,float z,float w)
{
    assert(t==GPU_VERTEX_SHADER);
    if(id==4) { texscale[0]=x; texscale[1]=y; }
    else { assert(id>=0&&id<4); proj[id][0]=x; proj[id][1]=y; proj[id][2]=z; proj[id][3]=w; }
}
static float clamp(float x) { return fminf(1.f,fmaxf(0.f,x)); }
static void unpack(u16 p,float *v)
{ v[0]=(p>>11)/31.f; v[1]=((p>>6)&31)/31.f; v[2]=((p>>1)&31)/31.f; v[3]=p&1; }
u16 sim_target_pixel(int x,int y)
{ return ((u16*)rt.tex->data)[sim_tiled(x,y,rt.tex->width)]; }
static void sample(float u,float v,float *c)
{
    int x=(int)floorf(u*texscale[0]*texture.width);
    int y=(int)floorf(v*texscale[1]*texture.height);
    x=x<0?0:x>=texture.width?texture.width-1:x;
    y=y<0?0:y>=texture.height?texture.height-1:y;
    unsigned i=sim_tiled(x,texture.height-1-y,texture.width);
    if(texture.format==GPU_RGBA5551) unpack(((u16*)texture.data)[i],c);
    else {
        u32 p=((u32*)texture.data)[i];
        for(int k=0;k<4;k++) c[k]=((p>>(24-k*8))&255)/255.f;
    }
}
static float factor(int f,int k,const float *s,const float *d)
{
    switch(f) {
    case GPU_ZERO:return 0; case GPU_ONE:return 1;
    case GPU_SRC_ALPHA:return s[3]; case GPU_ONE_MINUS_SRC_ALPHA:return 1-s[3];
    case GPU_DST_ALPHA:return d[3]; case GPU_ONE_MINUS_DST_ALPHA:return 1-d[3];
    case GPU_CONSTANT_ALPHA:return constant_alpha; case GPU_ONE_MINUS_CONSTANT_ALPHA:return 1-constant_alpha;
    case GPU_SRC_COLOR:return s[k]; case GPU_ONE_MINUS_SRC_COLOR:return 1-s[k];
    case GPU_DST_COLOR:return d[k]; case GPU_ONE_MINUS_DST_COLOR:return 1-d[k];
    default:assert(0); return 0;
    }
}
static int test(float a)
{
    int x=(int)lrintf(a*255);
    if(!alpha_on)return 1;
    switch(alpha_func) {
    case GPU_ALWAYS:return 1; case GPU_NEVER:return 0;
    case GPU_EQUAL:return x==alpha_ref; case GPU_NOTEQUAL:return x!=alpha_ref;
    case GPU_GREATER:return x>alpha_ref; case GPU_GEQUAL:return x>=alpha_ref;
    case GPU_LESS:return x<alpha_ref; case GPU_LEQUAL:return x<=alpha_ref;
    default:assert(0);return 0;
    }
}
typedef struct { s16 x,y,u,v; u32 rgba; } Vertex;
typedef struct { float x,y,u,v,c[4]; } Varying;
static float edge(const Varying *a,const Varying *b,float x,float y)
{ return (b->x-a->x)*(y-a->y)-(b->y-a->y)*(x-a->x); }
static int top_left(const Varying *a,const Varying *b)
{ return b->y<a->y || (b->y==a->y && b->x>a->x); }
void C3D_DrawArrays(int primitive,int first,int n)
{
    assert(primitive==GPU_TRIANGLES && n%3==0 && rt.tex);
    sim_c3d_draws++;
    for(int i=first;i<first+n;i+=3) {
        Varying v[3];
        for(int j=0;j<3;j++) {
            const Vertex *a=(const void*)((const u8*)buf.data+(i+j)*buf.stride);
            float x=proj[0][0]*a->x+proj[0][1]*a->y+proj[0][3];
            float y=proj[1][0]*a->x+proj[1][1]*a->y+proj[1][3];
            v[j].x=viewport[0]+(x+1)*viewport[2]*.5f;
            v[j].y=rt.tex->height-viewport[1]-(y+1)*viewport[3]*.5f;
            v[j].u=a->u; v[j].v=a->v;
            for(int k=0;k<4;k++)v[j].c[k]=((a->rgba>>(8*k))&255)/255.f;
        }
        float area=edge(&v[0],&v[1],v[2].x,v[2].y);
        if(area==0)continue;
        if(area<0) { Varying tmp=v[1]; v[1]=v[2]; v[2]=tmp; area=-area; }
        int x0=fmaxf(0,floorf(fminf(v[0].x,fminf(v[1].x,v[2].x))));
        int y0=fmaxf(0,floorf(fminf(v[0].y,fminf(v[1].y,v[2].y))));
        int x1=fminf(rt.tex->width,ceilf(fmaxf(v[0].x,fmaxf(v[1].x,v[2].x))));
        int y1=fminf(rt.tex->height,ceilf(fmaxf(v[0].y,fmaxf(v[1].y,v[2].y))));
        for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++) {
            if(x<viewport[0]||x>=viewport[0]+viewport[2]||y<rt.tex->height-viewport[1]-viewport[3]||y>=rt.tex->height-viewport[1])continue;
            int bottom=rt.tex->height-1-y;
            if(scissor_on&&(x<scissor[0]||x>=scissor[2]||bottom<scissor[1]||bottom>=scissor[3]))continue;
            float b[3]={edge(&v[1],&v[2],x+.5f,y+.5f),edge(&v[2],&v[0],x+.5f,y+.5f),edge(&v[0],&v[1],x+.5f,y+.5f)};
            if(b[0]<0||b[1]<0||b[2]<0)continue;
            if((b[0]==0&&!top_left(&v[1],&v[2]))||(b[1]==0&&!top_left(&v[2],&v[0]))||(b[2]==0&&!top_left(&v[0],&v[1])))continue;
            for(int k=0;k<3;k++)b[k]/=area;
            float tc[4],pc[4],s[4],d[4];
            float u=v[0].u+b[1]*(v[1].u-v[0].u)+b[2]*(v[2].u-v[0].u);
            float t=v[0].v+b[1]*(v[1].v-v[0].v)+b[2]*(v[2].v-v[0].v);
            sample(u,t,tc);
            for(int k=0;k<4;k++) {
                pc[k]=v[0].c[k]+b[1]*(v[1].c[k]-v[0].c[k])+b[2]*(v[2].c[k]-v[0].c[k]);
                C3D_TexEnv *e=&envs[0]; int ch=k==3;
                float a=e->src[ch][0]==GPU_TEXTURE0?tc[k]:pc[k];
                float z=e->src[ch][1]==GPU_TEXTURE0?tc[k]:pc[k];
                if(ch&&e->inv_alpha)a=1-a;
                s[k]=clamp((e->func[ch]==GPU_MODULATE?a*z:a)*e->scale[ch]);
                // TEV produces an 8-bit component before the alpha test/blender.
                s[k]=floorf(s[k]*255+.5f)/255.f;
            }
            if(!test(s[3]))continue;
            unsigned idx=sim_tiled(x,y,rt.tex->width);
            u16 *pixel=(u16*)rt.tex->data+idx;
            unpack(*pixel,d);
            for(int k=0;k<4;k++) {
                float a=s[k]*factor(blend[k==3?4:2],k,s,d);
                float z=d[k]*factor(blend[k==3?5:3],k,s,d);
                pc[k]=clamp(blend[k==3?1:0]==GPU_BLEND_REVERSE_SUBTRACT?z-a:a+z);
            }
            *pixel=((int)(pc[0]*31+.5f)<<11)|((int)(pc[1]*31+.5f)<<6)|((int)(pc[2]*31+.5f)<<1)|(pc[3]>=.5f);
        }
    }
}
