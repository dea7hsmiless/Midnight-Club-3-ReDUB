/* Sample Xbox depth as its original D24S8 bytes, matching the GL path.
 * Used by the game's depth-dependent speed-blur mask. Included after the
 * shader compiler; everything executes on the Vulkan renderer thread. */
static VkPipeline s_depth_pack_pipe;
static VkPipelineLayout s_depth_pack_layout;
static VkDescriptorSetLayout s_depth_pack_set;
static int depth_pack_init(void)
{
    static const char shader[]=
        "#version 450\n"
        "layout(local_size_x=8,local_size_y=8)in;\n"
        "layout(set=0,binding=0)uniform sampler2D z;\n"
        "layout(rgba8,set=0,binding=1)writeonly uniform image2D packed_z;\n"
        "layout(push_constant)uniform P{int abgr;};\n"
        "void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy);\n"
        "if(any(greaterThanEqual(p,imageSize(packed_z))))return;\n"
        "float depth=texelFetch(z,p,0).r;\n"
        "uint v=min(uint(round(clamp(depth,0.0,1.0)*16777215.0)),16777215u);\n"
        "float hi=float(v>>16u)/255.0,mid=float((v>>8u)&255u)/255.0,lo=float(v&255u)/255.0;\n"
        "imageStore(packed_z,p,abgr!=0?vec4(0,lo,mid,hi):vec4(mid,lo,0,hi));}\n";
    VkShaderModule module;
    VkDescriptorSetLayoutBinding b[2];
    VkDescriptorSetLayoutCreateInfo sci={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    VkPushConstantRange push={VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(int)};
    VkPipelineLayoutCreateInfo lci={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    VkComputePipelineCreateInfo pci={VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    if(s_depth_pack_pipe)return 1;
    module=compile(GLSLANG_STAGE_COMPUTE,shader);
    if(!module)return 0;
    memset(b,0,sizeof b);
    for(int i=0;i<2;i++){b[i].binding=i;b[i].descriptorCount=1;b[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;}
    b[0].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b[1].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    sci.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    sci.bindingCount=2;sci.pBindings=b;
    if(!s_depth_pack_set&&vkCreateDescriptorSetLayout(s_dev,&sci,NULL,&s_depth_pack_set)!=VK_SUCCESS)goto fail;
    lci.setLayoutCount=1;lci.pSetLayouts=&s_depth_pack_set;lci.pushConstantRangeCount=1;lci.pPushConstantRanges=&push;
    if(!s_depth_pack_layout&&vkCreatePipelineLayout(s_dev,&lci,NULL,&s_depth_pack_layout)!=VK_SUCCESS)goto fail;
    pci.layout=s_depth_pack_layout;pci.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pci.stage.module=module;pci.stage.pName="main";
    if(vkCreateComputePipelines(s_dev,VK_NULL_HANDLE,1,&pci,NULL,&s_depth_pack_pipe)!=VK_SUCCESS)goto fail;
    vkDestroyShaderModule(s_dev,module,NULL);return 1;
fail:
    vkDestroyShaderModule(s_dev,module,NULL);
    LOGE("depth packing compute pipeline unavailable\n");return 0;
}
static VkImageView depth_as_color(uint32_t va,uint32_t color,VkDepthBuf **out)
{
    VkDepthBuf *d=NULL;
    uint32_t format;
    VkImageViewCreateInfo vi={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    VkDescriptorImageInfo images[2];VkWriteDescriptorSet writes[2];
    static int enabled=-1;static unsigned reported;
    if(enabled<0){const char *e=getenv("RECOMP_GL_DEPTH_TEX");enabled=!(e&&e[0]=='0');}
    if(!enabled)return VK_NULL_HANDLE;
    if(color==0x12||color==0x06)format=1;
    else if(color==0x3f||color==0x3a)format=2;
    else return VK_NULL_HANDLE;
    for(int i=0;i<VK_MAX_DEPTH;i++)
        if(s_depth[i].image&&s_depth[i].va==va&&(!d||s_depth[i].pw*s_depth[i].ph>d->pw*d->ph))d=&s_depth[i];
    if(!d)return VK_NULL_HANDLE;
    *out=d;
    if(d->packed&&d->converted_seq==d->seq&&d->converted_format==format)return d->packed_view;
    end_rendering();
    if(!depth_pack_init())return VK_NULL_HANDLE;
    if(!d->sample_view){
        vi.image=d->image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=s_depth_fmt;
        vi.subresourceRange.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;
        vi.subresourceRange.levelCount=vi.subresourceRange.layerCount=1;
        if(vkCreateImageView(s_dev,&vi,NULL,&d->sample_view)!=VK_SUCCESS)return VK_NULL_HANDLE;
    }
    if(!d->packed&&!make_image(d->pw,d->ph,VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,&d->packed,&d->packed_view,&d->packed_mem))return VK_NULL_HANDLE;
    barrier_all();
    memset(images,0,sizeof images);memset(writes,0,sizeof writes);
    images[0].imageView=d->sample_view;images[0].sampler=sampler_get(0x30303);
    images[0].imageLayout=images[1].imageLayout=VK_IMAGE_LAYOUT_GENERAL;
    images[1].imageView=d->packed_view;
    for(int i=0;i<2;i++){
        writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstBinding=i;writes[i].descriptorCount=1;
        writes[i].descriptorType=i?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo=&images[i];
    }
    vkCmdBindPipeline(s_cb,VK_PIPELINE_BIND_POINT_COMPUTE,s_depth_pack_pipe);
    p_push_desc(s_cb,VK_PIPELINE_BIND_POINT_COMPUTE,s_depth_pack_layout,0,2,writes);
    {int abgr=format==2;vkCmdPushConstants(s_cb,s_depth_pack_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof abgr,&abgr);}
    vkCmdDispatch(s_cb,(d->pw+7)/8,(d->ph+7)/8,1);
    barrier_all();
    d->converted_seq=d->seq;d->converted_format=format;
    if(reported++<4)LOGE("depth %08X %ux%u sampled as Xbox color %02X\n",va,d->pw,d->ph,color);
    return d->packed_view;
}
