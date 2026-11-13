#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../xboxrecomp/src/kernel/nv2a_backend.h"
typedef struct {uint32_t w,h,pw,ph,va;} VkSurf;
typedef struct {float surf[4],m[16],vpoff[4],aa[2];} VsBlock;
static uint32_t s_frame;
static uint32_t s_temporal_presented_frame=UINT32_MAX;
static int s_temporal_jitter;
#include "../../xboxrecomp/src/nv2a_vk/temporal_jitter.h"
static void attr_to_float4(const Nv2aRawBatch *b,uint32_t a,uint32_t v,float *out){memcpy(out,b->attrs+((size_t)v*16+a)*4,16);}
int windows_media_gameplay_get(void){return 1;}
#include "../../xboxrecomp/src/nv2a_vk/temporal_history.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL at line %d\n",__LINE__);return 1;}}while(0)
int main(void)
{
    float attrs[3*16*4]={0},constants[192][4]={0},past[192][4];
    uint32_t regs[0x2000/4]={0},indices[]={0,1,2};
    Nv2aRawBatch b={0};VsBlock current={0};TemporalBlock out;VkSurf surface={.w=768,.h=480,.pw=1152,.ph=720};
    b.xform=1;b.zeta_va=1;b.regs=regs;b.vertex_count=3;b.index_count=3;
    b.indices=indices;b.attrs=attrs;b.attr_present=1;b.vp_consts=constants;
    b.surface.width=768;b.surface.height=480;
    regs[0x30c/4]=regs[0x35c/4]=1;
    current.m[0]=current.m[5]=current.m[10]=current.m[15]=1;
    current.aa[0]=current.aa[1]=1;current.surf[0]=2.0f/768;current.surf[1]=2.0f/480;
    for(int i=0;i<3;i++){attrs[i*64]=i;attrs[i*64+3]=1;}
    s_frame=10;temporal_uniforms(&b,&surface,&current,0,&out,past);CHECK(out.temporal[2]==0);
    s_frame=11;current.m[3]=2;temporal_uniforms(&b,&surface,&current,0,&out,past);
    CHECK(out.temporal[2]==1&&out.m[3]==0);
    s_frame=12;attrs[0]=7;temporal_uniforms(&b,&surface,&current,0,&out,past);CHECK(out.temporal[2]==0);
    s_frame=13;temporal_uniforms(&b,&surface,&current,0,&out,past);CHECK(out.temporal[2]==1);
    temporal_uniforms(&b,&surface,&current,0,&out,past);CHECK(out.temporal[2]==0);
    s_frame=14;temporal_uniforms(&b,&surface,&current,0,&out,past);CHECK(out.temporal[2]==0);
    s_frame=100;temporal_uniforms(&b,&surface,&current,0,&out,past);CHECK(out.temporal[2]==0);
    s_frame=101;surface.w=770;temporal_uniforms(&b,&surface,&current,0,&out,past);CHECK(out.temporal[2]==0);
    s_frame=102;temporal_uniforms(&b,&surface,&current,9,&out,past);CHECK(out.temporal[2]==0);
    s_frame=103;constants[0][0]=111;temporal_uniforms(&b,&surface,&current,9,&out,past);CHECK(out.temporal[2]==1&&past[0][0]==0);
    s_frame=104;constants[0][0]=99;temporal_uniforms(&b,&surface,&current,9,&out,past);CHECK(out.temporal[2]==1&&past[0][0]==111);
    CHECK(out.temporal[0]==0&&out.temporal[1]==0);
    CHECK(temporal_world_draw(&b,&surface));
    s_temporal_jitter=1;s_frame=105;
    temporal_uniforms(&b,&surface,&current,9,&out,past);
    float jx,jy;temporal_jitter_pixels(s_frame,&jx,&jy);
    CHECK(out.temporal[0]==2*jx/surface.pw&&out.temporal[1]==2*jy/surface.ph);
    for(unsigned i=0;i<36;i++){temporal_jitter_pixels(i,&jx,&jy);CHECK(jx>=-.5f&&jx<.5f&&jy>=-.5f&&jy<.5f);}
    s_temporal_jitter=0;
    regs[0x35c/4]=0;regs[0x304/4]=1;CHECK(!temporal_world_draw(&b,&surface));
    s_temporal_jitter=1;s_frame=106;temporal_uniforms(&b,&surface,&current,9,&out,past);
    CHECK(out.temporal[0]!=0||out.temporal[1]!=0);
    regs[0x304/4]=0;CHECK(temporal_world_draw(&b,&surface));
    s_temporal_presented_frame=s_frame;temporal_uniforms(&b,&surface,&current,9,&out,past);
    CHECK(out.temporal[0]==0&&out.temporal[1]==0);
    s_temporal_presented_frame=UINT32_MAX;s_temporal_jitter=0;regs[0x35c/4]=1;
    b.surface.width=128;CHECK(!temporal_world_draw(&b,&surface));b.surface.width=768;
    b.xform=1;s_frame=107;temporal_uniforms(&b,&surface,&current,9,&out,past);
    s_frame=108;b.attr_const[3][0]=.25f;
    temporal_uniforms(&b,&surface,&current,9,&out,past);CHECK(out.temporal[2]==1);
    s_frame=109;b.tex_va[1]=1234;temporal_uniforms(&b,&surface,&current,9,&out,past);CHECK(out.temporal[2]==1);
    s_frame=110;attrs[0]+=1;temporal_uniforms(&b,&surface,&current,9,&out,past);CHECK(out.temporal[2]==0);

    b.xform=2;b.vertex_count=4;b.index_count=4;
    float quad[4*16*4]={0};b.attrs=quad;
    for(int i=0;i<4;i++){quad[i*64]=(i&1)?719.5f:-.5f;quad[i*64+1]=(i&2)?479.5f:-.5f;quad[i*64+3]=1;}
    regs[0x35c/4]=0;regs[0x354/4]=0x207;
    CHECK(temporal_postprocess(&b,&surface,0));
    regs[0x30c/4]=0;regs[0x354/4]=0x203;CHECK(temporal_postprocess(&b,&surface,0));
    regs[0x30c/4]=1;CHECK(temporal_postprocess(&b,&surface,0));
    quad[2]=.5f;CHECK(!temporal_postprocess(&b,&surface,0));quad[2]=0;
    regs[0x35c/4]=1;regs[0x354/4]=0x203;CHECK(!temporal_postprocess(&b,&surface,0));
    puts("PASS: actual temporal history retains previous transforms/constants and rejects changed geometry, ambiguous reuse, skipped frames, resized surfaces and different shaders; jitter disabled by default and explicit diagnostic jitter uses matching bounded render-pixel offsets.");
    return 0;
}
