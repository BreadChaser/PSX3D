/* Minimal synchronous citro3d implementation for gpu_pica visual inspection.
 * GPL-2.0-or-later. Models this renderer's shader and TEV/blend configuration,
 * not PICA precision, its texture cache, command processor, or hardware bugs. */
#include "citro3d.h"
#include <math.h>
#include <string.h>
#include <assert.h>

unsigned sim_dither_setups;
static C3D_RenderTarget rt, targets[8];
static int ntargets;
static C3D_BufInfo buf;
static C3D_Tex textures[2];
static C3D_TexEnv envs[6];
static float proj[4][4], texscale[2];
static int viewport[4], scissor[4], scissor_on;
static int blend[6], alpha_on, alpha_func, alpha_ref;
static float constant_alpha;

// Optional delayed-reader guard: synchronous rasterization alone cannot
// detect a CPU upload overwritten before hardware consumes its texture.
static C3D_Tex pending_stage;
static u16 stage_expected[512*256];
static u8 stage_used[512*256];
static int check_stage = -1;
static void check_stage_pending(void)
{
    if (check_stage < 0) check_stage = getenv("SIM_CHECK_STAGE_LIFETIME") != NULL;
    if (!check_stage || !pending_stage.data) return;
    const u16 *pixels=pending_stage.data;
    for (int i=0;i<512*256;i++)
        assert(!stage_used[i] || pixels[i]==stage_expected[i]);
}
// Atlas readers are also delayed on hardware. Record only sampled words,
// then reject a CPU rewrite while a submitted/queued draw can still read it.
static C3D_Tex pending_atlas;
static u32 atlas_expected[1024*1024], atlas_indices[1024*1024];
static u8 atlas_used[1024*1024];
static unsigned atlas_count;
static int check_atlas = -1;
static void check_atlas_pending(void)
{
    if (check_atlas < 0) check_atlas = getenv("SIM_CHECK_ATLAS_LIFETIME") != NULL;
    if (!check_atlas || !pending_atlas.data) return;
    const u32 *pixels=pending_atlas.data;
    for (unsigned j=0;j<atlas_count;j++) {
        unsigned i=atlas_indices[j];
        assert(pixels[i]==atlas_expected[i]);
    }
}
void C3D_FrameEnd(u8 flags)
{
    (void)flags;
    check_stage_pending();
    check_atlas_pending();
    for (unsigned j=0;j<atlas_count;j++) atlas_used[atlas_indices[j]]=0;
    atlas_count=0;
    pending_atlas.data=NULL;
    memset(stage_used,0,sizeof(stage_used));
    pending_stage.data=NULL;
}

unsigned sim_tiled(int x, int y, int w)
{
    unsigned m = (x&1) | ((y&1)<<1) | ((x&2)<<1) | ((y&2)<<2) | ((x&4)<<2) | ((y&4)<<3);
    return ((y>>3)*(w>>3)+(x>>3))*64+m;
}

void C3D_TexBind(int u, C3D_Tex *t) { assert(u < 2); if(t) textures[u] = *t; else memset(&textures[u],0,sizeof(textures[u])); }
C3D_RenderTarget *C3D_RenderTargetCreateFromTex(C3D_Tex *t, int f, int l, int d)
{ (void)f; (void)l; (void)d; assert(ntargets<8); C3D_RenderTarget *p=&targets[ntargets++]; p->tex=t; return p; }
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
C3D_TexEnv *C3D_GetTexEnv(int i) { assert(i<6); if(i==1)sim_dither_setups++; return &envs[i]; }
void C3D_TexEnvInit(C3D_TexEnv *e)
{ memset(e,0,sizeof(*e)); e->scale[0]=e->scale[1]=1; e->src[0][0]=e->src[1][0]=GPU_PREVIOUS; e->func[0]=e->func[1]=GPU_REPLACE; }
static int channel(int m) { return m==C3D_Alpha; }
void C3D_TexEnvSrc(C3D_TexEnv *e,int m,int a,int b,int c)
{
    for(int k=0;k<2;k++) if(m==C3D_Both||k==channel(m))
    { e->src[k][0]=a; e->src[k][1]=b; e->src[k][2]=c; }
}
void C3D_TexEnvFunc(C3D_TexEnv *e,int m,int f)
{ for(int k=0;k<2;k++) if(m==C3D_Both||k==channel(m)) e->func[k]=f; }
void C3D_TexEnvScale(C3D_TexEnv *e,int m,int s)
{ assert(s==GPU_TEVSCALE_2||s==GPU_TEVSCALE_4); for(int k=0;k<2;k++) if(m==C3D_Both||k==channel(m)) e->scale[k]=s==GPU_TEVSCALE_2?2:4; }
void C3D_TexEnvOpAlpha(C3D_TexEnv *e,int a,int b,int c)
{ e->alpha_op[0]=a; e->alpha_op[1]=b; e->alpha_op[2]=c; }
void C3D_TexEnvColor(C3D_TexEnv *e,u32 color) { e->color=color; }
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
{ v[0]=(((p>>11)<<3)|((p>>11)>>2))/255.f; v[1]=((((p>>6)&31)<<3)|(((p>>6)&31)>>2))/255.f; v[2]=((((p>>1)&31)<<3)|(((p>>1)&31)>>2))/255.f; v[3]=p&1; }
u16 sim_target_pixel(int x,int y)
{ return ((u16*)rt.tex->data)[sim_tiled(x,y,rt.tex->width)]; }
static void sample(int unit,double u,double v,float *c)
{
    C3D_Tex texture=textures[unit];
    if(!texture.data) { memset(c,0,4*sizeof(float)); return; }
    int x=(int)floor(u*(unit ? 1.f : texscale[0])*texture.width);
    int y=(int)floor(v*(unit ? 1.f : texscale[1])*texture.height);
    if(unit) { x=(x%texture.width+texture.width)%texture.width; y=(y%texture.height+texture.height)%texture.height; }
    x=x<0?0:x>=texture.width?texture.width-1:x;
    y=y<0?0:y>=texture.height?texture.height-1:y;
    unsigned i=sim_tiled(x,texture.height-1-y,texture.width);
    if(texture.format==GPU_RGBA5551) unpack(((u16*)texture.data)[i],c);
    else {
        u32 p=((u32*)texture.data)[i];
        if (check_atlas && unit==0 && texture.width==1024 && texture.height==1024) {
            assert(!pending_atlas.data || pending_atlas.data==texture.data);
            pending_atlas=texture;
            if (!atlas_used[i]) {
                atlas_used[i]=1;
                atlas_indices[atlas_count++]=i;
                atlas_expected[i]=p;
            }
        }
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
typedef struct { double x,y,u,v,dx,dy; float c[4]; } Varying;
static double edge(const Varying *a,const Varying *b,double x,double y)
{ return (b->x-a->x)*(y-a->y)-(b->y-a->y)*(x-a->x); }
static int top_left(const Varying *a,const Varying *b)
{ return b->y<a->y || (b->y==a->y && b->x>a->x); }
void C3D_DrawArrays(int primitive,int first,int n)
{
    assert(primitive==GPU_TRIANGLES && n%3==0 && rt.tex);
    sim_c3d_draws++;
    check_stage_pending();
    check_atlas_pending();
    if (check_stage && textures[0].format==GPU_RGBA5551 &&
        textures[0].width==512 && textures[0].height==256) {
        pending_stage=textures[0];
        int u0=512,u1=0,v0=256,v1=0;
        for (int i=first;i<first+n;i++) {
            const Vertex *v=(const void*)((const u8*)buf.data+i*buf.stride);
            if (v->u<u0) u0=v->u;
            if (v->u>u1) u1=v->u;
            if (v->v<v0) v0=v->v;
            if (v->v>v1) v1=v->v;
        }
        assert(u0>=0 && u1<=512 && v0>=0 && v1<=256);
        for (int y=v0;y<v1;y++) for (int x=u0;x<u1;x++) {
            unsigned k=sim_tiled(x,255-y,512);
            stage_used[k]=1;
            stage_expected[k]=((u16*)pending_stage.data)[k];
        }
    }
    for(int i=first;i<first+n;i+=3) {
        Varying v[3];
        for(int j=0;j<3;j++) {
            const Vertex *a=(const void*)((const u8*)buf.data+(i+j)*buf.stride);
            float x=proj[0][0]*a->x+proj[0][1]*a->y+proj[0][3];
            float y=proj[1][0]*a->x+proj[1][1]*a->y+proj[1][3];
            v[j].x=viewport[0]+(x+1)*viewport[2]*.5f;
            v[j].y=rt.tex->height-viewport[1]-(y+1)*viewport[3]*.5f;
            v[j].u=a->u; v[j].v=a->v;
            v[j].dx=a->x/32.f; v[j].dy=a->y/32.f;
            for(int k=0;k<4;k++)v[j].c[k]=((a->rgba>>(8*k))&255)/255.f;
        }
        double area=edge(&v[0],&v[1],v[2].x,v[2].y);
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
            double b[3]={edge(&v[1],&v[2],x+.5f,y+.5f),edge(&v[2],&v[0],x+.5f,y+.5f),edge(&v[0],&v[1],x+.5f,y+.5f)};
            if(b[0]<0||b[1]<0||b[2]<0)continue;
            if((b[0]==0&&!top_left(&v[1],&v[2]))||(b[1]==0&&!top_left(&v[2],&v[0]))||(b[2]==0&&!top_left(&v[0],&v[1])))continue;
            // Renderer positions are quarter pixels and UVs are integers. Keep
            // their edge equations exact until nearest-neighbour selection.
            // Float interpolation rounded integer boundaries differently after
            // translating the same texture to another atlas cell.
            long long den=llround(area*16);
            long long un=llround((b[1]*(v[1].u-v[0].u)+b[2]*(v[2].u-v[0].u))*16);
            long long vn=llround((b[1]*(v[1].v-v[0].v)+b[2]*(v[2].v-v[0].v))*16);
            long long ui=(long long)v[0].u+(un>=0 ? un/den : -((-un+den-1)/den));
            long long vi=(long long)v[0].v+(vn>=0 ? vn/den : -((-vn+den-1)/den));
            for(int k=0;k<3;k++)b[k]/=area;
            float tc[4],dt[4],pc[4],s[4]={0},d[4];
            sample(0,ui,vi,tc);
            float du=v[0].dx+b[1]*(v[1].dx-v[0].dx)+b[2]*(v[2].dx-v[0].dx);
            float dv=v[0].dy+b[1]*(v[1].dy-v[0].dy)+b[2]*(v[2].dy-v[0].dy);
            sample(1,du,dv,dt);
            for(int k=0;k<4;k++)
                pc[k]=v[0].c[k]+b[1]*(v[1].c[k]-v[0].c[k])+b[2]*(v[2].c[k]-v[0].c[k]);
            for(int stage=0;stage<6;stage++) {
                float previous[4]; memcpy(previous,s,sizeof(s));
                for(int k=0;k<4;k++) {
                    C3D_TexEnv *e=&envs[stage]; int ch=k==3;
                    float args[2];
                    for(int j=0;j<2;j++) {
                        int component=k;
                        if(ch) switch(e->alpha_op[j]) {
                        case GPU_TEVOP_A_SRC_R:component=0;break;
                        case GPU_TEVOP_A_SRC_G:component=1;break;
                        case GPU_TEVOP_A_SRC_B:component=2;break;
                        }
                        switch(e->src[ch][j]) {
                        case GPU_TEXTURE0: args[j]=tc[component]; break;
                        case GPU_TEXTURE1: args[j]=dt[component]; break;
                        case GPU_PREVIOUS: args[j]=previous[component]; break;
                        case GPU_PRIMARY_COLOR: args[j]=pc[component]; break;
                        case GPU_CONSTANT: args[j]=((e->color>>(8*component))&255)/255.f; break;
                        default: args[j]=0; break;
                        }
                    }
                    if(ch) for(int j=0;j<2;j++) if(e->alpha_op[j]==GPU_TEVOP_A_ONE_MINUS_SRC_ALPHA) args[j]=1-args[j];
                    float value;
                    switch(e->func[ch]) {
                    case GPU_ADD: value=args[0]+args[1]; break;
                    case GPU_MODULATE: value=args[0]*args[1]; break;
                    case GPU_ADD_SIGNED: value=args[0]+args[1]-128/255.f; break;
                    case GPU_REPLACE: value=args[0]; break;
                    default: assert(0); value=0;
                    }
                    // Model the byte output of each TEV stage. This is not
                    // an emulation of PICA's full interpolation precision.
                    s[k]=floorf(clamp(value*e->scale[ch])*255+.5f)/255.f;
                }
            }
            if(!test(s[3]))continue;
            unsigned idx=sim_tiled(x,y,rt.tex->width);
            void *pixel=(char*)rt.tex->data+idx*(rt.tex->format==GPU_RGBA8?4:2);
            if(rt.tex->format==GPU_RGBA8) { u32 p=*(u32*)pixel; for(int k=0;k<4;k++) d[k]=((p>>(24-8*k))&255)/255.f; }
            else unpack(*(u16*)pixel,d);
            for(int k=0;k<4;k++) {
                float a=s[k]*factor(blend[k==3?4:2],k,s,d);
                float z=d[k]*factor(blend[k==3?5:3],k,s,d);
                pc[k]=clamp(blend[k==3?1:0]==GPU_BLEND_REVERSE_SUBTRACT?z-a:a+z);
            }
            // PICA RGB5A1 drops the low three bits of the blended 8-bit
            // component (also modeled by Azahar Common::Color::EncodeRGB5A1).
            // The original harness rounded normalized floats to 31 instead.
            if(rt.tex->format==GPU_RGBA8) {
                u32 p=0; for(int k=0;k<4;k++) p|=(u32)(pc[k]*255+.5f)<<(24-8*k); *(u32*)pixel=p;
            } else *(u16*)pixel=(((int)(pc[0]*255+1e-4f)>>3)<<11)|(((int)(pc[1]*255+1e-4f)>>3)<<6)|(((int)(pc[2]*255+1e-4f)>>3)<<1)|(pc[3]>=.5f);
        }
    }
    // GPU writes to the atlas are ordered after older GPU readers. They
    // retire those readers without requiring a CPU-side synchronization.
    if (check_atlas && pending_atlas.data==rt.tex->data) {
        const u32 *pixels=pending_atlas.data;
        for (unsigned j=0;j<atlas_count;j++) {
            unsigned i=atlas_indices[j];
            atlas_expected[i]=pixels[i];
        }
    }
}
