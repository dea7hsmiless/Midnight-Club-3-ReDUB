/* Vulkan ClearAttachments ignores color write masks. The Xbox does not:
 * alpha-only clears establish destination-alpha masks used by the radar. */
static void masked_color_clear(VkSurf *surface,VkDepthBuf *depth,uint32_t flags,uint32_t argb,const VkClearRect *rect)
{
    static VkPipeline pipelines[32];static VkPipelineLayout layout;static VkShaderModule vs,fs;
    uint32_t mask=((flags&0x10)?VK_COLOR_COMPONENT_R_BIT:0)|((flags&0x20)?VK_COLOR_COMPONENT_G_BIT:0)|
                  ((flags&0x40)?VK_COLOR_COMPONENT_B_BIT:0)|((flags&0x80)?VK_COLOR_COMPONENT_A_BIT:0);
    if(!mask)return;
    if(!layout){
        const char *vertex="#version 450\nvoid main(){vec2 p=vec2(float((gl_VertexIndex<<1)&2),float(gl_VertexIndex&2));gl_Position=vec4(p*2.-1.,0,1);}";
        const char *fragment="#version 450\nlayout(push_constant)uniform Color{vec4 color;}pc;layout(location=0)out vec4 o;void main(){o=pc.color;}";
        vs=compile(GLSLANG_STAGE_VERTEX,vertex);fs=compile(GLSLANG_STAGE_FRAGMENT,fragment);if(!vs||!fs)return;
        VkPushConstantRange push={VK_SHADER_STAGE_FRAGMENT_BIT,0,16};VkPipelineLayoutCreateInfo ci={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};ci.pushConstantRangeCount=1;ci.pPushConstantRanges=&push;
        if(vkCreatePipelineLayout(s_dev,&ci,NULL,&layout)!=VK_SUCCESS)return;
    }
    VkPipeline *pipeline=&pipelines[mask+(depth?16:0)];
    if(!*pipeline){
        VkPipelineShaderStageCreateInfo stages[2]={{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
        stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vs;stages[0].pName="main";
        stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=fs;stages[1].pName="main";
        VkPipelineVertexInputStateCreateInfo vertex={VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo assembly={VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport={VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};viewport.viewportCount=viewport.scissorCount=1;
        VkPipelineRasterizationStateCreateInfo raster={VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.polygonMode=VK_POLYGON_MODE_FILL;raster.lineWidth=1;raster.cullMode=VK_CULL_MODE_NONE;
        VkPipelineMultisampleStateCreateInfo samples={VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};samples.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds={VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        VkPipelineColorBlendAttachmentState attachments[2]={{0},{0}};attachments[0].colorWriteMask=mask;
        VkPipelineColorBlendStateCreateInfo blend={VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blend.attachmentCount=s_temporal_on?2:1;blend.pAttachments=attachments;
        VkDynamicState states[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};VkPipelineDynamicStateCreateInfo dynamic={VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};dynamic.dynamicStateCount=2;dynamic.pDynamicStates=states;
        VkFormat formats[]={VK_FORMAT_B8G8R8A8_UNORM,VK_FORMAT_R16G16B16A16_SFLOAT};VkPipelineRenderingCreateInfo rendering={VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};rendering.colorAttachmentCount=blend.attachmentCount;rendering.pColorAttachmentFormats=formats;
        if(depth)rendering.depthAttachmentFormat=rendering.stencilAttachmentFormat=s_depth_fmt;
        VkGraphicsPipelineCreateInfo create={VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};create.pNext=&rendering;create.stageCount=2;create.pStages=stages;create.pVertexInputState=&vertex;create.pInputAssemblyState=&assembly;create.pViewportState=&viewport;create.pRasterizationState=&raster;create.pMultisampleState=&samples;create.pDepthStencilState=&ds;create.pColorBlendState=&blend;create.pDynamicState=&dynamic;create.layout=layout;
        if(vkCreateGraphicsPipelines(s_dev,s_pcache,1,&create,NULL,pipeline)!=VK_SUCCESS){LOGE("masked color clear pipeline failed\n");return;}
    }
    begin_rendering(surface,depth);set_viewport(surface->pw,surface->ph);
    vkCmdSetScissor(s_cb,0,1,&rect->rect);s_sc_cur=rect->rect;
    float color[4];argb_vec4(argb,color);vkCmdBindPipeline(s_cb,VK_PIPELINE_BIND_POINT_GRAPHICS,*pipeline);
    vkCmdPushConstants(s_cb,layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof color,color);vkCmdDraw(s_cb,3,1,0,0);
    surface->dirty=1;surface->gen++;s_dyn_valid=0;s_cb_gen++;
}
