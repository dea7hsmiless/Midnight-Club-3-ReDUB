/* Controlled GPU test: upscale a static, constant-color scene with exact
 * zero motion and a constant depth plane. This is not game motion data. */
#include <nvsdk_ngx_helpers_vk.h>
#include <cmath>
#include <cstdint>

static bool evaluate_test(VkPhysicalDevice gpu, VkDevice device, uint32_t family,
                          NVSDK_NGX_Parameter *params,RedubDlssApi *bridge=nullptr,RedubDlssContext *context=nullptr,int mode=1)
{
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkFormat format;
        uint32_t width, height;
    } images[4];
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkBuffer readback = VK_NULL_HANDLE;
    VkDeviceMemory readmem = VK_NULL_HANDLE;
    NVSDK_NGX_Handle *feature = nullptr;
    struct Cleanup {
        VkDevice device; Image *images; VkCommandPool &pool;
        VkBuffer &buffer; VkDeviceMemory &memory; NVSDK_NGX_Handle *&feature;
        ~Cleanup() {
            vkDeviceWaitIdle(device);
            if (feature) NVSDK_NGX_VULKAN_ReleaseFeature(feature);
            if (buffer) vkDestroyBuffer(device,buffer,nullptr);
            if (memory) vkFreeMemory(device,memory,nullptr);
            for(int i=0;i<4;i++) {
                if(images[i].view)vkDestroyImageView(device,images[i].view,nullptr);
                if(images[i].image)vkDestroyImage(device,images[i].image,nullptr);
                if(images[i].memory)vkFreeMemory(device,images[i].memory,nullptr);
            }
            if(pool)vkDestroyCommandPool(device,pool,nullptr);
        }
    } cleanup{device,images,pool,readback,readmem,feature};
    VkPhysicalDeviceMemoryProperties memory;
    vkGetPhysicalDeviceMemoryProperties(gpu,&memory);
    auto allocate = [&](const VkMemoryRequirements &rq,VkMemoryPropertyFlags flags,VkDeviceMemory &out) {
        for(uint32_t i=0;i<memory.memoryTypeCount;i++) {
            if(!(rq.memoryTypeBits&(1u<<i)) || (memory.memoryTypes[i].propertyFlags&flags)!=flags)continue;
            VkMemoryAllocateInfo ai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            ai.allocationSize=rq.size;ai.memoryTypeIndex=i;
            return vkAllocateMemory(device,&ai,nullptr,&out)==VK_SUCCESS;
        }
        return false;
    };
    uint32_t iw=1280,ih=720;const uint32_t ow=1920,oh=1080;
    if(bridge&&!bridge->optimal(context,ow,oh,mode,&iw,&ih))return false;
    const VkFormat formats[]={VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_R32_SFLOAT,
                              VK_FORMAT_R16G16_SFLOAT,VK_FORMAT_R16G16B16A16_SFLOAT};
    VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    for(int i=0;i<4;i++) {
        Image &im=images[i];im.width=i==3?ow:iw;im.height=i==3?oh:ih;im.format=formats[i];
        VkImageCreateInfo ci={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType=VK_IMAGE_TYPE_2D;ci.format=im.format;ci.extent={im.width,im.height,1};
        ci.mipLevels=1;ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;
        ci.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if(vkCreateImage(device,&ci,nullptr,&im.image)!=VK_SUCCESS)return false;
        VkMemoryRequirements rq;vkGetImageMemoryRequirements(device,im.image,&rq);
        if(!allocate(rq,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,im.memory))return false;
        if(vkBindImageMemory(device,im.image,im.memory,0)!=VK_SUCCESS)return false;
        VkImageViewCreateInfo vi={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image=im.image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=im.format;vi.subresourceRange=range;
        if(vkCreateImageView(device,&vi,nullptr,&im.view)!=VK_SUCCESS)return false;
    }
    VkCommandPoolCreateInfo pci={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.queueFamilyIndex=family;pci.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if(vkCreateCommandPool(device,&pci,nullptr,&pool)!=VK_SUCCESS)return false;
    VkCommandBufferAllocateInfo cai={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool=pool;cai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;cai.commandBufferCount=1;
    if(vkAllocateCommandBuffers(device,&cai,&cmd)!=VK_SUCCESS)return false;
    VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
    auto begin = [&]() {
        if(vkResetCommandBuffer(cmd,0)!=VK_SUCCESS)return false;
        VkCommandBufferBeginInfo bi={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        return vkBeginCommandBuffer(cmd,&bi)==VK_SUCCESS;
    };
    auto submit = [&]() {
        if(vkEndCommandBuffer(cmd)!=VK_SUCCESS)return false;
        VkSubmitInfo si={VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&cmd;
        return vkQueueSubmit(queue,1,&si,VK_NULL_HANDLE)==VK_SUCCESS&&vkQueueWaitIdle(queue)==VK_SUCCESS;
    };
    if(!begin())return false;
    NVSDK_NGX_DLSS_Create_Params create={};
    create.Feature.InWidth=iw;create.Feature.InHeight=ih;
    create.Feature.InTargetWidth=ow;create.Feature.InTargetHeight=oh;
    create.Feature.InPerfQualityValue=NVSDK_NGX_PerfQuality_Value_MaxQuality;
    create.InFeatureCreateFlags=NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
    if(bridge){if(!bridge->create(context,cmd,iw,ih,ow,oh,mode,0))return false;}
    else if(!check(NGX_VULKAN_CREATE_DLSS_EXT1(device,cmd,1,1,&feature,params,&create),"Create actual DLSS feature"))return false;
    for(int i=0;i<4;i++) {
        VkImageMemoryBarrier b={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;b.newLayout=VK_IMAGE_LAYOUT_GENERAL;
        b.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.image=images[i].image;b.subresourceRange=range;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
        VkClearColorValue color={};
        if(i==0){color.float32[0]=0.25f;color.float32[1]=0.5f;color.float32[2]=0.75f;color.float32[3]=1;}
        if(i==1)color.float32[0]=0.5f;
        vkCmdClearColorImage(cmd,images[i].image,VK_IMAGE_LAYOUT_GENERAL,&color,1,&range);
    }
    if(!submit())return false;
    NVSDK_NGX_Resource_VK resources[4];
    for(int i=0;i<4;i++)resources[i]=NVSDK_NGX_Create_ImageView_Resource_VK(images[i].view,images[i].image,range,images[i].format,images[i].width,images[i].height,i==3);
    for(int frame=0;frame<16;frame++) {
        if(!begin())return false;
        VkMemoryBarrier mb={VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        mb.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;mb.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&mb,0,nullptr,0,nullptr);
        NVSDK_NGX_VK_DLSS_Eval_Params eval={};
        eval.Feature.pInColor=&resources[0];eval.pInDepth=&resources[1];eval.pInMotionVectors=&resources[2];
        eval.Feature.pInOutput=&resources[3];eval.InRenderSubrectDimensions={iw,ih};
        eval.InMVScaleX=1;eval.InMVScaleY=1;eval.InReset=frame==0;
        eval.InPreExposure=1;eval.InExposureScale=1;eval.InFrameTimeDeltaInMsec=1000.0f/30;
        if(bridge){
            RedubDlssFrame frame_info={};
            RedubDlssImage *dest[]={&frame_info.color,&frame_info.depth,&frame_info.motion,&frame_info.output};
            for(int i=0;i<4;i++)*dest[i]={images[i].image,images[i].view,images[i].format,images[i].width,images[i].height};
            frame_info.frame_ms=1000.0f/30;frame_info.reset=frame==0;
            if(!bridge->evaluate(context,cmd,&frame_info))return false;
        }else{
            NVSDK_NGX_Result result=NGX_VULKAN_EVALUATE_DLSS_EXT(cmd,feature,params,&eval);
            if(NVSDK_NGX_FAILED(result)){check(result,"Evaluate DLSS");return false;}
        }
        if(!submit())return false;
    }
    const VkDeviceSize bytes=VkDeviceSize(ow)*oh*8;
    VkBufferCreateInfo bci={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bci.size=bytes;bci.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if(vkCreateBuffer(device,&bci,nullptr,&readback)!=VK_SUCCESS)return false;
    VkMemoryRequirements rq;vkGetBufferMemoryRequirements(device,readback,&rq);
    if(!allocate(rq,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,readmem))return false;
    if(vkBindBufferMemory(device,readback,readmem,0)!=VK_SUCCESS||!begin())return false;
    VkMemoryBarrier mb={VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;mb.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&mb,0,nullptr,0,nullptr);
    VkBufferImageCopy copy={};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={ow,oh,1};
    vkCmdCopyImageToBuffer(cmd,images[3].image,VK_IMAGE_LAYOUT_GENERAL,readback,1,&copy);
    mb.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;mb.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&mb,0,nullptr,0,nullptr);
    if(!submit())return false;
    void *mapped=nullptr;if(vkMapMemory(device,readmem,0,bytes,0,&mapped)!=VK_SUCCESS)return false;
    const auto pixels=static_cast<const uint16_t *>(mapped);
    /* Expected half-floats for 0.25, 0.5 and 0.75. Check central samples
     * within a small encoding tolerance; an untouched zero output fails. */
    bool good=true;
    for(uint32_t y=oh/4;y<oh*3/4;y+=oh/8)for(uint32_t x=ow/4;x<ow*3/4;x+=ow/8) {
        const auto p=pixels+(size_t(y)*ow+x)*4;
        good=good&&std::abs(int(p[0])-0x3400)<64&&std::abs(int(p[1])-0x3800)<64&&std::abs(int(p[2])-0x3a00)<64;
    }
    vkUnmapMemory(device,readmem);
    std::printf("%s: DLSS mode %d, 16 GPU evaluations, %ux%u -> %ux%u; controlled-scene readback.\n",good?"PASS":"FAIL",mode,iw,ih,ow,oh);
    return good;
}
