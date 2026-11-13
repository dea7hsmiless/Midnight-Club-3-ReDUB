/* Experimental real-scene NGX evaluation. Native presentation is the default.
 * Explicit MC3_DLSS_PRESENT opts into the separate scene/HUD test path.
 * Included after temporal_history.h. All work stays on the renderer thread. */
#ifdef _WIN32
static const char temporal_inputs_shader[]=
    "#version 450\n"
    "layout(local_size_x=8,local_size_y=8)in;\n"
    "layout(set=0,binding=0)uniform sampler2D scene;\n"
    "layout(set=0,binding=1)uniform sampler2D raw_depth;\n"
    "layout(set=0,binding=2)uniform sampler2D history;\n"
    "layout(rgba16f,set=0,binding=3)writeonly uniform image2D color_out;\n"
    "layout(r32f,set=0,binding=4)writeonly uniform image2D depth_out;\n"
    "layout(rgba16f,set=0,binding=5)writeonly uniform image2D motion_out;\n"
    "layout(r32f,set=0,binding=6)writeonly uniform image2D reject_out;\n"
    "layout(push_constant)uniform Params{ivec2 validColor;}pc;\n"
    "void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy);\n"
    "if(any(greaterThanEqual(p,imageSize(depth_out))))return;\n"
    "vec4 mv=texelFetch(history,p,0);float z=texelFetch(raw_depth,p,0).r;\n"
    "float reject=clamp(mv.z,0.0,1.0);\n"
    "if(any(isnan(mv))||any(isinf(mv))||isnan(z)||isinf(z)){mv=vec4(0);z=1;reject=1;}\n"
    /* Bounds cover the full source image. Effects faults are corrected in
     * render-target allocation/sampling rather than masked in DLSS input. */
    "ivec2 color_p=clamp(p,ivec2(0),pc.validColor);\n"
    "imageStore(color_out,p,texelFetch(scene,color_p,0));\n"
    "imageStore(depth_out,p,vec4(clamp(z,0,1)));\n"
    "imageStore(motion_out,p,vec4(mv.xy,0,0));\n"
    "imageStore(reject_out,p,vec4(reject));}\n";
typedef struct {RedubDlssImage im;VkDeviceMemory memory;} TemporalInputImage;
static struct {
    VkPipeline pipeline,compose_pipeline;VkPipelineLayout layout,compose_layout;VkDescriptorSetLayout set,compose_set;
    TemporalInputImage color,depth,motion_packed,motion,reject,output,presentation;
    uint32_t width,height,frame,count;uint64_t last_ns;int failed,completed;
} s_temporal_inputs;
static void temporal_input_release(TemporalInputImage *im)
{
    if(im->im.image)garbage_add(im->im.image,im->im.view,im->memory);
    memset(im,0,sizeof *im);
}
static int temporal_input_image(TemporalInputImage *im,uint32_t w,uint32_t h,VkFormat format)
{
    temporal_input_release(im);
    im->im.width=w;im->im.height=h;im->im.format=format;
    return make_image(w,h,format,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,VK_IMAGE_ASPECT_COLOR_BIT,
        &im->im.image,&im->im.view,&im->memory);
}
static int temporal_inputs_init(VkSurf *s)
{
    if(!s_temporal_inputs.pipeline){
        VkShaderModule module=compile(GLSLANG_STAGE_COMPUTE,temporal_inputs_shader);
        VkDescriptorSetLayoutBinding b[7]={0};
        VkDescriptorSetLayoutCreateInfo sci={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        VkPipelineLayoutCreateInfo lci={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        VkComputePipelineCreateInfo pci={VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        VkPushConstantRange push={VK_SHADER_STAGE_COMPUTE_BIT,0,8};
        if(!module)return 0;
        for(int i=0;i<7;i++){
            b[i].binding=i;b[i].descriptorCount=1;b[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
            b[i].descriptorType=i<3?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        }
        sci.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;sci.bindingCount=7;sci.pBindings=b;
        VkResult result=vkCreateDescriptorSetLayout(s_dev,&sci,NULL,&s_temporal_inputs.set);
        lci.setLayoutCount=1;lci.pSetLayouts=&s_temporal_inputs.set;
        lci.pushConstantRangeCount=1;lci.pPushConstantRanges=&push;
        if(result==VK_SUCCESS)result=vkCreatePipelineLayout(s_dev,&lci,NULL,&s_temporal_inputs.layout);
        pci.layout=s_temporal_inputs.layout;pci.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pci.stage.module=module;pci.stage.pName="main";
        if(result==VK_SUCCESS)result=vkCreateComputePipelines(s_dev,VK_NULL_HANDLE,1,&pci,NULL,&s_temporal_inputs.pipeline);
        vkDestroyShaderModule(s_dev,module,NULL);
        if(result!=VK_SUCCESS)return 0;
    }
    if(s_temporal_inputs.width!=s->pw||s_temporal_inputs.height!=s->ph){
        uint32_t ow=s->dlss_ow?s->dlss_ow:(s->pw*3+1)/2,oh=s->dlss_oh?s->dlss_oh:(s->ph*3+1)/2,iw=0,ih=0;
        /* First real-scene diagnostic uses the SDK's Quality ratio, never a
         * guessed mode. Presentation-sized allocation is a later gate. */
        if(!s_dlss_api.optimal(s_dlss_context,ow,oh,s_dlss_mode,&iw,&ih)||iw!=s->pw||ih!=s->ph)return 0;
        flush_frame_wait();
        if(!temporal_input_image(&s_temporal_inputs.color,iw,ih,VK_FORMAT_R16G16B16A16_SFLOAT)||
           !temporal_input_image(&s_temporal_inputs.depth,iw,ih,VK_FORMAT_R32_SFLOAT)||
           !temporal_input_image(&s_temporal_inputs.motion_packed,iw,ih,VK_FORMAT_R16G16B16A16_SFLOAT)||
           !temporal_input_image(&s_temporal_inputs.motion,iw,ih,VK_FORMAT_R16G16_SFLOAT)||
           !temporal_input_image(&s_temporal_inputs.reject,iw,ih,VK_FORMAT_R32_SFLOAT)||
           !temporal_input_image(&s_temporal_inputs.output,ow,oh,VK_FORMAT_R16G16B16A16_SFLOAT)||
           !temporal_input_image(&s_temporal_inputs.presentation,ow,oh,VK_FORMAT_R16G16B16A16_SFLOAT))return 0;
        if(!s_dlss_api.create(s_dlss_context,s_cb,iw,ih,ow,oh,s_dlss_mode,0))return 0;
        s_temporal_inputs.width=iw;s_temporal_inputs.height=ih;s_temporal_inputs.frame=0;
        LOGE("DLSS scene test ready: %ux%u -> %ux%u; presentation %s\n",iw,ih,ow,oh,s_temporal_present?"experimental DLSS + native HUD":"native");
    }
    return 1;
}
/* DLSS owns RGB, not the Xbox framebuffer alpha used as a destination
 * blend/mask channel by the minimap. Reconstruct that channel separately. */
static const char temporal_compose_shader[]=
    "#version 450\n"
    "layout(local_size_x=8,local_size_y=8)in;\n"
    "layout(set=0,binding=0)uniform sampler2D reconstructed;\n"
    "layout(set=0,binding=1)uniform sampler2D native_color;\n"
    "layout(rgba16f,set=0,binding=2)writeonly uniform image2D composed;\n"
    "void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy),size=imageSize(composed);\n"
    "if(any(greaterThanEqual(p,size)))return;\n"
    "vec2 uv=(vec2(p)+.5)/vec2(size);\n"
    "vec3 rgb=texelFetch(reconstructed,p,0).rgb;\n"
    /* Destination alpha is game mask data, not display color. Preserve source
     * samples exactly; interpolation invents intermediate mask values. */
    "ivec2 nativeSize=textureSize(native_color,0);\n"
    "ivec2 nativePixel=clamp(ivec2(uv*vec2(nativeSize)),ivec2(0),nativeSize-1);\n"
    "float alpha=texelFetch(native_color,nativePixel,0).a;\n"
    "imageStore(composed,p,vec4(rgb,alpha));}\n";
static int temporal_compose_alpha(VkSurf *s)
{
    if(!s_temporal_inputs.compose_pipeline){
        VkShaderModule module=compile(GLSLANG_STAGE_COMPUTE,temporal_compose_shader);
        VkDescriptorSetLayoutBinding b[3]={0};
        VkDescriptorSetLayoutCreateInfo sci={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        VkPipelineLayoutCreateInfo lci={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        VkComputePipelineCreateInfo pci={VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        if(!module)return 0;
        for(int i=0;i<3;i++){
            b[i].binding=i;b[i].descriptorCount=1;b[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
            b[i].descriptorType=i<2?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        }
        sci.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;sci.bindingCount=3;sci.pBindings=b;
        VkResult result=vkCreateDescriptorSetLayout(s_dev,&sci,NULL,&s_temporal_inputs.compose_set);
        lci.setLayoutCount=1;lci.pSetLayouts=&s_temporal_inputs.compose_set;
        if(result==VK_SUCCESS)result=vkCreatePipelineLayout(s_dev,&lci,NULL,&s_temporal_inputs.compose_layout);
        pci.layout=s_temporal_inputs.compose_layout;pci.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pci.stage.module=module;pci.stage.pName="main";
        if(result==VK_SUCCESS)result=vkCreateComputePipelines(s_dev,VK_NULL_HANDLE,1,&pci,NULL,&s_temporal_inputs.compose_pipeline);
        vkDestroyShaderModule(s_dev,module,NULL);if(result!=VK_SUCCESS)return 0;
    }
    VkDescriptorImageInfo im[3]={0};VkWriteDescriptorSet wr[3]={0};
    VkImageView views[]={s_temporal_inputs.output.im.view,s->view,s_temporal_inputs.presentation.im.view};
    barrier_all();
    for(int i=0;i<3;i++){
        im[i].imageView=views[i];im[i].imageLayout=VK_IMAGE_LAYOUT_GENERAL;
        if(i<2)im[i].sampler=sampler_get(0x30303);
        wr[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;wr[i].dstBinding=i;wr[i].descriptorCount=1;
        wr[i].descriptorType=i<2?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        wr[i].pImageInfo=&im[i];
    }
    vkCmdBindPipeline(s_cb,VK_PIPELINE_BIND_POINT_COMPUTE,s_temporal_inputs.compose_pipeline);
    p_push_desc(s_cb,VK_PIPELINE_BIND_POINT_COMPUTE,s_temporal_inputs.compose_layout,0,3,wr);
    vkCmdDispatch(s_cb,(s_temporal_inputs.presentation.im.width+7)/8,(s_temporal_inputs.presentation.im.height+7)/8,1);
    barrier_all();return 1;
}
/* Promote only the completed scene target. Later native game draws (HUD,
 * minimap, radio text and final grading) use output-resolution attachments.
 * The original low-resolution targets are restored after frame submission. */
static int temporal_scene_promote(VkSurf *s,VkDepthBuf **depth)
{
    TemporalSurfacePlane *p=&s->alternate;
    VkDepthBuf *source=*depth;
    uint32_t ow=s_temporal_inputs.output.im.width,oh=s_temporal_inputs.output.im.height;
    VkFormatProperties properties;vkGetPhysicalDeviceFormatProperties(s_pd,s_depth_fmt,&properties);
    if((properties.optimalTilingFeatures&(VK_FORMAT_FEATURE_BLIT_SRC_BIT|VK_FORMAT_FEATURE_BLIT_DST_BIT))!=
        (VK_FORMAT_FEATURE_BLIT_SRC_BIT|VK_FORMAT_FEATURE_BLIT_DST_BIT))return 0;
    if(s->upscaled)return 1;
    if(p->pw!=ow||p->ph!=oh||!p->image||!p->motion){
        surface_discard_alternate(s);p->pw=ow;p->ph=oh;
        if(!make_image(ow,oh,VK_FORMAT_B8G8R8A8_UNORM,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|
            VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,&p->image,&p->view,&p->mem)||
           !make_image(ow,oh,VK_FORMAT_R16G16B16A16_SFLOAT,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|
            VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,&p->motion,&p->motion_view,&p->motion_mem))return 0;
    }
    if(!temporal_compose_alpha(s))return 0;
    VkDepthBuf *high=depth_get(source->va,ow,oh);
    if(!high)return 0;
    high->w=source->w;high->h=source->h;high->aa_sx=source->aa_sx;high->aa_sy=source->aa_sy;
    end_rendering();barrier_all();
    VkImageBlit bl={0};bl.srcSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    bl.srcSubresource.layerCount=1;bl.dstSubresource=bl.srcSubresource;
    bl.srcOffsets[1]=(VkOffset3D){ow,oh,1};bl.dstOffsets[1]=bl.srcOffsets[1];
    vkCmdBlitImage(s_cb,s_temporal_inputs.presentation.im.image,VK_IMAGE_LAYOUT_GENERAL,
        p->image,VK_IMAGE_LAYOUT_GENERAL,1,&bl,VK_FILTER_NEAREST);
    bl.srcOffsets[1]=(VkOffset3D){s->pw,s->ph,1};
    vkCmdBlitImage(s_cb,s->motion,VK_IMAGE_LAYOUT_GENERAL,p->motion,VK_IMAGE_LAYOUT_GENERAL,1,&bl,VK_FILTER_NEAREST);
    VkImageBlit depth_blits[2]={bl,bl};
    depth_blits[0].srcSubresource.aspectMask=depth_blits[0].dstSubresource.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;
    depth_blits[1].srcSubresource.aspectMask=depth_blits[1].dstSubresource.aspectMask=VK_IMAGE_ASPECT_STENCIL_BIT;
    vkCmdBlitImage(s_cb,source->image,VK_IMAGE_LAYOUT_GENERAL,high->image,VK_IMAGE_LAYOUT_GENERAL,2,depth_blits,VK_FILTER_NEAREST);
    high->seq++;p->motion_frame=s_frame;p->feedback_valid=0;
    barrier_all();surface_swap_resolution(s);s_temporal_presented_frame=s_frame;s->dirty=1;s->gen++;*depth=high;s_dyn_valid=0;
    hud_diagnostic(s,"scene");
    return 1;
}
static void temporal_scene_evaluate(const Nv2aRawBatch *b,VkSurf *s,VkDepthBuf **depth,int post)
{
    static int enabled=-1;
    VkDepthBuf *d=*depth;
    extern int windows_media_gameplay_get(void);
    if(enabled<0){const char *e=getenv("MC3_DLSS_EVALUATE");enabled=e&&*e=='1';}
    if(!enabled||!s_dlss_context||!s_temporal_on||s_temporal_inputs.failed||(s_temporal_inputs.completed&&!s_temporal_present)||!post||
       !d||!s->motion||s->w<512||s->h<400||b->regs[0x354/4]!=0x207||
       !windows_media_gameplay_get()||s_temporal_inputs.frame==s_frame||
       d->pw!=s->pw||d->ph!=s->ph||s_temporal_matches<100)return;
    end_rendering();
    if(!temporal_inputs_init(s))goto fail;
    hud_diagnostic(s,"input");
    if(!d->sample_view){
        VkImageViewCreateInfo vi={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image=d->image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=s_depth_fmt;
        vi.subresourceRange.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;
        vi.subresourceRange.levelCount=vi.subresourceRange.layerCount=1;
        if(vkCreateImageView(s_dev,&vi,NULL,&d->sample_view)!=VK_SUCCESS)goto fail;
    }
    VkDescriptorImageInfo im[7]={0};VkWriteDescriptorSet wr[7]={0};
    VkImageView views[]={s->view,d->sample_view,s->motion_view,s_temporal_inputs.color.im.view,
        s_temporal_inputs.depth.im.view,s_temporal_inputs.motion_packed.im.view,s_temporal_inputs.reject.im.view};
    barrier_all();
    for(int i=0;i<7;i++){
        im[i].imageView=views[i];im[i].imageLayout=VK_IMAGE_LAYOUT_GENERAL;
        if(i<3)im[i].sampler=sampler_get(0x30303);
        wr[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;wr[i].dstBinding=i;wr[i].descriptorCount=1;
        wr[i].descriptorType=i<3?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        wr[i].pImageInfo=&im[i];
    }
    vkCmdBindPipeline(s_cb,VK_PIPELINE_BIND_POINT_COMPUTE,s_temporal_inputs.pipeline);
    p_push_desc(s_cb,VK_PIPELINE_BIND_POINT_COMPUTE,s_temporal_inputs.layout,0,7,wr);
    int valid_x=(int)s->pw-1;
    int valid_y=(int)s->ph-1;
    if(valid_x<0)valid_x=0;if(valid_y<0)valid_y=0;
    int valid_color[2]={valid_x,valid_y};
    vkCmdPushConstants(s_cb,s_temporal_inputs.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof valid_color,valid_color);
    vkCmdDispatch(s_cb,(s->pw+7)/8,(s->ph+7)/8,1);barrier_all();
    VkImageBlit bl={0};bl.srcSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    bl.srcSubresource.layerCount=1;bl.dstSubresource=bl.srcSubresource;
    bl.srcOffsets[1]=(VkOffset3D){s->pw,s->ph,1};bl.dstOffsets[1]=bl.srcOffsets[1];
    vkCmdBlitImage(s_cb,s_temporal_inputs.motion_packed.im.image,VK_IMAGE_LAYOUT_GENERAL,
        s_temporal_inputs.motion.im.image,VK_IMAGE_LAYOUT_GENERAL,1,&bl,VK_FILTER_NEAREST);barrier_all();
    RedubDlssFrame frame={0};
    frame.color=s_temporal_inputs.color.im;frame.depth=s_temporal_inputs.depth.im;
    frame.motion=s_temporal_inputs.motion.im;frame.rejection=s_temporal_inputs.reject.im;
    frame.output=s_temporal_inputs.output.im;frame.frame_ms=1000.0f/30.0f;
    if(s_temporal_jitter)temporal_jitter_pixels(s_frame,&frame.jitter_x,&frame.jitter_y);
    uint64_t now=hz_now();
    float elapsed=s_temporal_inputs.last_ns?(float)((now-s_temporal_inputs.last_ns)/1e6):1000.0f/30.0f;
    frame.reset=s_temporal_inputs.frame+1!=s_frame||elapsed>250.0f;
    frame.frame_ms=elapsed>=1.0f&&elapsed<=250.0f?elapsed:1000.0f/30.0f;
    s_temporal_inputs.last_ns=now;
    if(s_temporal_inputs.count==59||s_temporal_inputs.count%300==0)
        LOGE("DLSS temporal inputs: frame=%u jitter=%.6f,%.6f reset=%d elapsed=%.3f history=%u/%u moving=%u\n",s_frame,frame.jitter_x,frame.jitter_y,frame.reset,elapsed,s_temporal_matches,s_temporal_draws,s_temporal_moving_matches);
    if(!s_dlss_api.evaluate(s_dlss_context,s_cb,&frame))goto fail;
    barrier_all();
    if(s_temporal_present&&!temporal_scene_promote(s,depth))goto fail;
    s_dyn_valid=0;s_temporal_inputs.frame=s_frame;s_temporal_inputs.count++;
    if(s_temporal_inputs.count==1||s_temporal_inputs.count%300==0)
        LOGE("DLSS real-scene evaluation %u recorded at frame %u; presentation %s, jitter %s\n",
            s_temporal_inputs.count,s_frame,s_temporal_present?"DLSS scene + native HUD":"native",s_temporal_jitter?"on":"off");
    return;
fail:
    s_temporal_inputs.failed=1;s_temporal_jitter=0;s_dyn_valid=0;
    LOGE("DLSS real-scene diagnostic stopped: %s; native presentation retained\n",s_dlss_api.error());
}
/* One bounded diagnostic readback also proves submission completed; a
 * successful NGX record call alone does not validate the resulting pixels. */
static void temporal_scene_readback(void)
{
    const char *prefix=getenv("MC3_DLSS_CAPTURE");
    if(s_temporal_inputs.completed||s_temporal_inputs.failed||s_temporal_inputs.count<60||!prefix||!*prefix)return;
    TemporalInputImage *images[]={&s_temporal_inputs.color,&s_temporal_inputs.depth,
        &s_temporal_inputs.motion,&s_temporal_inputs.reject,&s_temporal_inputs.output,&s_temporal_inputs.presentation};
    const char *names[]={"scene-color","scene-depth","scene-motion","scene-reject","dlss-output","scene-composed"};
    const uint32_t bpp[]={8,4,4,4,8,8};
    const int image_count=s_temporal_present?6:5;
    VkDeviceSize offsets[6],sizes[6],bytes=0;
    for(int i=0;i<image_count;i++){
        offsets[i]=bytes;sizes[i]=(VkDeviceSize)images[i]->im.width*images[i]->im.height*bpp[i];
        bytes=(bytes+sizes[i]+255)&~(VkDeviceSize)255;
    }
    VkBufferCreateInfo bi={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    VkBuffer buffer;VkDeviceMemory memory;VkMemoryRequirements rq;
    bi.size=bytes;bi.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if(vkCreateBuffer(s_dev,&bi,NULL,&buffer)!=VK_SUCCESS)return;
    vkGetBufferMemoryRequirements(s_dev,buffer,&rq);
    memory=alloc_mem(rq,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if(!memory||vkBindBufferMemory(s_dev,buffer,memory,0)!=VK_SUCCESS){
        if(memory)vkFreeMemory(s_dev,memory,NULL);vkDestroyBuffer(s_dev,buffer,NULL);return;
    }
    end_rendering();barrier_all();
    for(int i=0;i<image_count;i++){
        VkBufferImageCopy copy={0};copy.bufferOffset=offsets[i];
        copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;copy.imageSubresource.layerCount=1;
        copy.imageExtent=(VkExtent3D){images[i]->im.width,images[i]->im.height,1};
        vkCmdCopyImageToBuffer(s_cb,images[i]->im.image,VK_IMAGE_LAYOUT_GENERAL,buffer,1,&copy);
    }
    flush_frame_wait();void *data=NULL;int written=1;
    if(vkMapMemory(s_dev,memory,0,bytes,0,&data)==VK_SUCCESS){
        for(int i=0;i<image_count;i++){
            char path[2048];snprintf(path,sizeof path,"%s-%s.bin",prefix,names[i]);
            FILE *f=fopen(path,"wb");
            if(!f){written=0;continue;}
            uint32_t header[]={images[i]->im.width,images[i]->im.height,images[i]->im.format,s_temporal_inputs.frame};
            fwrite("RDIM0001",1,8,f);fwrite(header,4,4,f);
            if(fwrite((uint8_t *)data+offsets[i],1,(size_t)sizes[i],f)!=(size_t)sizes[i]||ferror(f))written=0;
            fclose(f);
        }
        vkUnmapMemory(s_dev,memory);
    }else written=0;
    vkDestroyBuffer(s_dev,buffer,NULL);vkFreeMemory(s_dev,memory,NULL);
    if(written){s_temporal_inputs.completed=1;if(!s_temporal_present)s_temporal_jitter=0;
        LOGE("DLSS real-scene GPU readback complete after %u evaluations: %s; presentation %s\n",s_temporal_inputs.count,prefix,s_temporal_present?"DLSS scene + native HUD":"native");}
}

#else
static void temporal_scene_evaluate(const Nv2aRawBatch *b,VkSurf *s,VkDepthBuf **depth,int post)
{(void)b;(void)s;(void)depth;(void)post;}
static void temporal_scene_readback(void){}
#endif
