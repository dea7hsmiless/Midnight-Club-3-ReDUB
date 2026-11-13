/* Conservative per-draw history. Geometry is content-addressed, not matched
 * by draw order or transient guest pointers. Reused/ambiguous geometry,
 * missing frames and changed vertex data reject temporal reuse. */
typedef struct {
    float m[16],vpoff[4],aa[4],temporal[4],render_size[4];
} TemporalBlock;
typedef struct {
    uint64_t key;
    uint32_t occurrences;
    TemporalBlock block;
    float constants[192][4];
} TemporalHistory;
#define TEMPORAL_SLOTS 4096
static TemporalHistory s_history[2][TEMPORAL_SLOTS];
static uint32_t s_history_frame=0xFFFFFFFFu,s_history_side;
static uint32_t s_temporal_draws,s_temporal_matches;
static uint32_t s_temporal_moving_matches;
/* Only opaque world/depth-writing draws own the scene motion attachment.
 * Read-only depth effects and HUD must not overwrite background motion with
 * an invalid zero vector. Their reconstruction mask is handled separately. */
static int temporal_world_draw(const Nv2aRawBatch *b,const VkSurf *s)
{
    return b->xform&&b->zeta_va&&b->regs[0x30C/4]&&(b->regs[0x35C/4]||!b->regs[0x304/4])&&
        s->w>=512&&s->h>=400&&b->surface.width>=s->w*3/4&&b->surface.height>=s->h*3/4;
}
static int temporal_postprocess(const Nv2aRawBatch *b,const VkSurf *s,int scene_texture)
{
    if(b->vertex_count<3)return 0;
    if(b->regs[0x30C/4]&&b->regs[0x35C/4]&&b->regs[0x354/4]!=0x207)return 0;
    if(b->xform!=0&&scene_texture&&b->vertex_count<=8)return 1;
    /* Remix also emits screen-space quads through its vertex-program path.
     * Only accept a flat, unit-W pixel rectangle under read-only
     * depth; ordinary transformed geometry must retain rejection writes. */
    int screen_program=b->xform==2&&b->vertex_count==4&&
        !b->regs[0x35C/4];
    if(b->xform!=0&&!screen_program)return 0;
    float lo[2]={1e20f,1e20f},hi[2]={-1e20f,-1e20f};
    for(uint32_t v=0;v<b->vertex_count;v++){
        float p[4];
        if(b->attr_direct&1)attr_to_float4(b,0,v,p);
        else memcpy(p,b->attrs+(size_t)v*NV2A_RAW_ATTRS*4,16);
        if(screen_program&&(p[2]!=0.0f||p[3]!=1.0f))return 0;
        for(int axis=0;axis<2;axis++){if(p[axis]<lo[axis])lo[axis]=p[axis];if(p[axis]>hi[axis])hi[axis]=p[axis];}
    }
    return hi[0]-lo[0]>=s->w*.8f&&hi[1]-lo[1]>=s->h*.75f;
}

static void temporal_trace(const Nv2aRawBatch *b,const VkSurf *surface,const TemporalBlock *block,int writes,int post)
{
#ifdef _WIN32
    static FILE *file;static uint32_t begin,frame;static int done;
    const char *prefix=getenv("MC3_DLSS_CAPTURE");
    extern int windows_media_gameplay_get(void);
    if(file&&frame!=s_frame){fclose(file);file=NULL;done=1;}
    if(done||!prefix||!*prefix)return;
    if(!windows_media_gameplay_get()){begin=0;return;}
    if(!begin)begin=s_frame;
    if(s_frame-begin<120||s_temporal_moving_matches<100)return;
    if(!file){char path[2048];snprintf(path,sizeof path,"%s-passes.log",prefix);file=fopen(path,"w");frame=s_frame;}
    if(!file||surface->w<512||surface->h<400)return;
    float first[4]={0};
    if(b->vertex_count){if(b->attr_direct&1)attr_to_float4(b,0,0,first);else memcpy(first,b->attrs,16);}
    fprintf(file,"target=%08X %ux%u clip=%ux%u xform=%u vertices=%u indices=%u depth=%u/%u func=%X blend=%u/%X/%X stencil=%u mask=%X motion=%d post=%d valid=%.0f tex=%08X,%08X,%08X,%08X v0=%.3f,%.3f,%.3f,%.3f\n",
        surface->va,surface->w,surface->h,b->surface.width,b->surface.height,b->xform,b->vertex_count,b->index_count,
        b->regs[0x30C/4],b->regs[0x35C/4],b->regs[0x354/4],b->regs[0x304/4],b->regs[0x344/4],b->regs[0x348/4],b->regs[0x32C/4],b->regs[0x358/4],writes,post,block->temporal[2],
        b->tex_va[0],b->tex_va[1],b->tex_va[2],b->tex_va[3],first[0],first[1],first[2],first[3]);
#else
    (void)b;(void)surface;(void)block;(void)writes;(void)post;
#endif
}
static void temporal_begin_frame(uint32_t frame)
{
    if(s_history_frame==frame)return;
    s_history_side^=1;
    for(unsigned i=0;i<TEMPORAL_SLOTS;i++){
        s_history[s_history_side][i].key=0;
        if(s_history_frame+1!=frame)s_history[s_history_side^1][i].key=0;
    }
    s_history_frame=frame;
}
static uint64_t temporal_hash(uint64_t h,const void *bytes,size_t count)
{
    const uint8_t *p=bytes;for(size_t i=0;i<count;i++)h=(h^p[i])*1099511628211ull;return h;
}
static TemporalHistory *temporal_find(unsigned side,uint64_t key,int insert)
{
    for(unsigned i=0;i<TEMPORAL_SLOTS;i++){
        TemporalHistory *entry=&s_history[side][(key+i)&(TEMPORAL_SLOTS-1)];
        if(entry->key==key)return entry;
        if(!entry->key){if(!insert)return NULL;entry->key=key;entry->occurrences=0;return entry;}
    }
    return NULL;
}
static void temporal_uniforms(const Nv2aRawBatch *b,const VkSurf *surface,const VsBlock *current,
                              uint64_t program,TemporalBlock *out,float previous_constants[192][4])
{
    uint64_t key=1469598103934665603ull;
    memset(out,0,sizeof *out);memcpy(out->m,current->m,sizeof out->m);
    memcpy(out->vpoff,current->vpoff,sizeof out->vpoff);memcpy(out->aa,current->aa,sizeof current->aa);
    out->render_size[0]=(float)surface->pw;out->render_size[1]=(float)surface->ph;
    memcpy(previous_constants,b->vp_consts,192*16);
    temporal_begin_frame(s_frame);
#ifdef _WIN32
    extern int windows_media_gameplay_get(void);
    if(!windows_media_gameplay_get())return;
#endif
    /* All scene geometry must share the sample grid, including read-only
     * sky and transparent geometry. Screen-space postprocessing and native
     * HUD remain unjittered; they consume the already jittered scene. */
    if(s_temporal_jitter&&s_temporal_presented_frame!=s_frame&&b->xform&&b->zeta_va&&
       surface->w>=512&&surface->h>=400&&b->surface.width>=surface->w*3/4&&
       b->surface.height>=surface->h*3/4&&!temporal_postprocess(b,surface,0)){
        float x,y;temporal_jitter_pixels(s_frame,&x,&y);
        out->temporal[0]=2*x/surface->pw;out->temporal[1]=2*y/surface->ph;
        out->temporal[3]=1;
    }
    if(!temporal_world_draw(b,surface))return;
    /* The minimap renders 3D geometry into a small viewport of the main
     * allocation; it is HUD, not a valid world-motion source. */
    if(b->surface.width<surface->w*3/4||b->surface.height<surface->h*3/4)return;

    s_temporal_draws++;
    key=temporal_hash(key,&program,sizeof program);
    key=temporal_hash(key,&b->xform,sizeof b->xform);
    key=temporal_hash(key,&b->vertex_count,sizeof b->vertex_count);
    key=temporal_hash(key,&b->index_count,sizeof b->index_count);
    key=temporal_hash(key,&surface->pw,sizeof surface->pw);
    key=temporal_hash(key,&surface->ph,sizeof surface->ph);
    key=temporal_hash(key,&surface->w,sizeof surface->w);
    key=temporal_hash(key,&surface->h,sizeof surface->h);
    key=temporal_hash(key,current->surf,sizeof current->surf);
    key=temporal_hash(key,b->indices,(size_t)b->index_count*4);
    /* Fixed-function position uses only v0 and the composite transform.
     * Retain the primary material as an instance discriminator, but changing
     * secondary light maps or vertex colors must not discard static motion. */
    key=temporal_hash(key,b->tex_va,b->xform==1?sizeof b->tex_va[0]:sizeof b->tex_va);
    /* Arbitrary vertex programs retain all inputs. Fixed-function position
     * uses v0 only. Hash normalized position inputs in full, not sparse samples. */
    for(uint32_t a=0;a<(b->xform==1?1u:NV2A_RAW_ATTRS);a++){
        if(!(b->attr_present&(1u<<a))){key=temporal_hash(key,b->attr_const[a],16);continue;}
        for(uint32_t v=0;v<b->vertex_count;v++){
            float value[4];
            if(b->attr_direct&(1u<<a))attr_to_float4(b,a,v,value);
            else memcpy(value,b->attrs+((size_t)v*NV2A_RAW_ATTRS+a)*4,16);
            key=temporal_hash(key,value,16);
        }
    }
    if(!key)key=1;
    TemporalHistory *previous=temporal_find(s_history_side^1,key,0),*next=temporal_find(s_history_side,key,1);
    if(!next)return;
    next->occurrences++;
    if(previous&&previous->occurrences==1&&next->occurrences==1){
        memcpy(out->m,previous->block.m,sizeof out->m);
        memcpy(out->vpoff,previous->block.vpoff,sizeof out->vpoff);
        memcpy(out->aa,previous->block.aa,sizeof out->aa);
        memcpy(previous_constants,previous->constants,192*16);
        out->temporal[2]=1;s_temporal_matches++;
        if(memcmp(previous->block.m,current->m,sizeof current->m)||
           (b->xform==2&&memcmp(previous->constants,b->vp_consts,192*16)))s_temporal_moving_matches++;
    }
    memcpy(next->block.m,current->m,sizeof current->m);
    memcpy(next->block.vpoff,current->vpoff,sizeof current->vpoff);
    memcpy(next->block.aa,current->aa,sizeof current->aa);
    memcpy(next->constants,b->vp_consts,192*16);

}
