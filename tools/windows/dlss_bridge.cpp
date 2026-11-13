/* Optional NVIDIA SDK module, isolated from MinGW/C++ runtime ABI differences.
 * ReDUB code is MIT; NVIDIA's separately downloaded SDK has its own license. */
#include "../../xboxrecomp/src/nv2a_vk/dlss_api.h"
#include <nvsdk_ngx_vk.h>
#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_vk.h>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <new>

struct RedubDlssContext {
    VkDevice device;
    NVSDK_NGX_Parameter *caps, *params;
    NVSDK_NGX_Handle *feature;
    uint32_t iw, ih, ow, oh;
};
static const char project_id[]="4a94aef4-470d-49bb-b426-b8d4d9150427";
static char last_error[256];
static bool active;
static int fail(const char *step,NVSDK_NGX_Result result) {
    std::snprintf(last_error,sizeof last_error,"%s: NGX 0x%08x",step,unsigned(result));return 0;
}
static int bad(const char *message) {
    std::snprintf(last_error,sizeof last_error,"%s",message);return 0;
}
static NVSDK_NGX_FeatureDiscoveryInfo discovery() {
    NVSDK_NGX_FeatureDiscoveryInfo info={};
    info.SDKVersion=NVSDK_NGX_Version_API;info.FeatureID=NVSDK_NGX_Feature_SuperSampling;
    info.Identifier.IdentifierType=NVSDK_NGX_Application_Identifier_Type_Project_Id;
    info.Identifier.v.ProjectDesc={project_id,NVSDK_NGX_ENGINE_TYPE_CUSTOM,"ReDUB-experimental-1"};
    info.ApplicationDataPath=L".";return info;
}
static int extensions(VkInstance instance,VkPhysicalDevice gpu,uint32_t *count,const char ***names) {
    static char storage[32][VK_MAX_EXTENSION_NAME_SIZE];static const char *pointers[32];
    if(!count||!names)return bad("Missing extension outputs");
    auto info=discovery();VkExtensionProperties *properties=nullptr;uint32_t n=0;
    auto result=gpu?NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(instance,gpu,&info,&n,&properties)
                   :NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&info,&n,&properties);
    if(NVSDK_NGX_FAILED(result))return fail("Extension discovery",result);
    if(n>32)return bad("Too many required NGX extensions");
    for(uint32_t i=0;i<n;i++){std::snprintf(storage[i],sizeof storage[i],"%s",properties[i].extensionName);pointers[i]=storage[i];}
    *count=n;*names=pointers;return 1;
}
static int instance_extensions(uint32_t *n,const char ***s){return extensions(VK_NULL_HANDLE,VK_NULL_HANDLE,n,s);}
static int device_extensions(VkInstance i,VkPhysicalDevice p,uint32_t *n,const char ***s){return extensions(i,p,n,s);}
static void destroy(RedubDlssContext *ctx) {
    if(!ctx)return;
    /* Reconfiguration/destruction is rare; never destroy resources used by
     * submitted work. No wait is inserted into the per-frame evaluate call. */
    vkDeviceWaitIdle(ctx->device);
    if(ctx->feature)NVSDK_NGX_VULKAN_ReleaseFeature(ctx->feature);
    if(ctx->params)NVSDK_NGX_VULKAN_DestroyParameters(ctx->params);
    if(ctx->caps)NVSDK_NGX_VULKAN_DestroyParameters(ctx->caps);
    NVSDK_NGX_VULKAN_Shutdown1(ctx->device);active=false;delete ctx;
}
static RedubDlssContext *init(VkInstance instance,VkPhysicalDevice gpu,VkDevice device,const wchar_t *cache) {
    if(active){bad("Only one NGX context may be active");return nullptr;}
    auto *ctx=new(std::nothrow) RedubDlssContext{};
    if(!ctx){bad("NGX context allocation failed");return nullptr;}
    ctx->device=device;
    auto result=NVSDK_NGX_VULKAN_Init_with_ProjectID(project_id,NVSDK_NGX_ENGINE_TYPE_CUSTOM,
        "ReDUB-experimental-1",cache?cache:L".",instance,gpu,device,vkGetInstanceProcAddr,vkGetDeviceProcAddr,nullptr,NVSDK_NGX_Version_API);
    if(NVSDK_NGX_FAILED(result)){fail("NGX initialization",result);delete ctx;return nullptr;}
    active=true;
    result=NVSDK_NGX_VULKAN_GetCapabilityParameters(&ctx->caps);
    if(NVSDK_NGX_FAILED(result)){fail("NGX capabilities",result);destroy(ctx);return nullptr;}
    int available=0;NVSDK_NGX_Parameter_GetI(ctx->caps,NVSDK_NGX_Parameter_SuperSampling_Available,&available);
    if(!available){bad("DLSS Super Resolution unavailable on this device/runtime");destroy(ctx);return nullptr;}
    result=NVSDK_NGX_VULKAN_AllocateParameters(&ctx->params);
    if(NVSDK_NGX_FAILED(result)){fail("NGX parameters",result);destroy(ctx);return nullptr;}
    last_error[0]=0;return ctx;
}
/* Public modes: 1 Quality, 2 Balanced, 3 Performance. Off never calls NGX. */
static NVSDK_NGX_PerfQuality_Value quality(int mode) {
    return mode==3?NVSDK_NGX_PerfQuality_Value_MaxPerf:mode==2?NVSDK_NGX_PerfQuality_Value_Balanced:NVSDK_NGX_PerfQuality_Value_MaxQuality;
}
static int optimal(RedubDlssContext *ctx,uint32_t ow,uint32_t oh,int mode,uint32_t *iw,uint32_t *ih) {
    if(!ctx||!ow||!oh||!iw||!ih||mode<1||mode>3)return bad("Invalid DLSS dimensions/mode");
    unsigned maxw=0,maxh=0,minw=0,minh=0;float sharp=0;
    auto result=NGX_DLSS_GET_OPTIMAL_SETTINGS(ctx->caps,ow,oh,quality(mode),iw,ih,&maxw,&maxh,&minw,&minh,&sharp);
    if(NVSDK_NGX_FAILED(result))return fail("DLSS optimal settings",result);
    return *iw&&*ih?1:bad("DLSS returned empty render dimensions");
}
static int create(RedubDlssContext *ctx,VkCommandBuffer cmd,uint32_t iw,uint32_t ih,uint32_t ow,uint32_t oh,int mode,int inverted) {
    if(!ctx||!cmd||!iw||!ih||!ow||!oh||mode<1||mode>3)return bad("Invalid DLSS feature parameters");
    if(ctx->feature){vkDeviceWaitIdle(ctx->device);NVSDK_NGX_VULKAN_ReleaseFeature(ctx->feature);ctx->feature=nullptr;}
    NVSDK_NGX_DLSS_Create_Params info={};
    info.Feature.InWidth=iw;info.Feature.InHeight=ih;info.Feature.InTargetWidth=ow;info.Feature.InTargetHeight=oh;
    info.Feature.InPerfQualityValue=quality(mode);
    info.InFeatureCreateFlags=NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
    if(inverted)info.InFeatureCreateFlags|=NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
    auto result=NGX_VULKAN_CREATE_DLSS_EXT1(ctx->device,cmd,1,1,&ctx->feature,ctx->params,&info);
    if(NVSDK_NGX_FAILED(result))return fail("Create DLSS feature",result);
    ctx->iw=iw;ctx->ih=ih;ctx->ow=ow;ctx->oh=oh;return 1;
}
static NVSDK_NGX_Resource_VK resource(const RedubDlssImage &im,bool write) {
    VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    if(im.format==VK_FORMAT_D32_SFLOAT||im.format==VK_FORMAT_D24_UNORM_S8_UINT||im.format==VK_FORMAT_D32_SFLOAT_S8_UINT)range.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;
    return NVSDK_NGX_Create_ImageView_Resource_VK(im.view,im.image,range,im.format,im.width,im.height,write);
}
static int evaluate(RedubDlssContext *ctx,VkCommandBuffer cmd,const RedubDlssFrame *frame) {
    if(!ctx||!ctx->feature||!cmd||!frame)return bad("Missing DLSS feature/frame");
    const auto &f=*frame;
    if(!f.color.image||!f.color.view||!f.depth.image||!f.depth.view||!f.motion.image||!f.motion.view||!f.output.image||!f.output.view)return bad("Incomplete DLSS frame resources");
    if(f.color.width!=ctx->iw||f.color.height!=ctx->ih||f.depth.width!=ctx->iw||f.depth.height!=ctx->ih||f.motion.width!=ctx->iw||f.motion.height!=ctx->ih||f.output.width!=ctx->ow||f.output.height!=ctx->oh)return bad("DLSS frame dimension mismatch");
    if(!std::isfinite(f.jitter_x)||!std::isfinite(f.jitter_y)||!std::isfinite(f.frame_ms)||f.frame_ms<=0)return bad("Invalid DLSS temporal constants");
    auto color=resource(f.color,false),depth=resource(f.depth,false),motion=resource(f.motion,false),output=resource(f.output,true);
    NVSDK_NGX_VK_DLSS_Eval_Params info={};
    info.Feature.pInColor=&color;info.Feature.pInOutput=&output;info.pInDepth=&depth;info.pInMotionVectors=&motion;
    info.InJitterOffsetX=f.jitter_x;info.InJitterOffsetY=f.jitter_y;info.InReset=f.reset;
    info.InMVScaleX=1;info.InMVScaleY=1;info.InRenderSubrectDimensions={ctx->iw,ctx->ih};
    info.InPreExposure=1;info.InExposureScale=1;info.InFrameTimeDeltaInMsec=f.frame_ms;
    NVSDK_NGX_Resource_VK rejection={};
    if(f.rejection.image&&f.rejection.view){
        if(f.rejection.width!=ctx->iw||f.rejection.height!=ctx->ih)return bad("DLSS rejection-mask dimension mismatch");
        rejection=resource(f.rejection,false);info.pInBiasCurrentColorMask=&rejection;
    }
    auto result=NGX_VULKAN_EVALUATE_DLSS_EXT(cmd,ctx->feature,ctx->params,&info);
    return NVSDK_NGX_FAILED(result)?fail("Evaluate DLSS",result):1;
}
static const char *error(){return last_error;}
extern "C" __declspec(dllexport) int redub_dlss_api(uint32_t abi,size_t size,RedubDlssApi *out) {
    if(abi!=REDUB_DLSS_ABI||size!=sizeof(RedubDlssApi)||!out)return 0;
    *out={REDUB_DLSS_ABI,instance_extensions,device_extensions,init,optimal,create,evaluate,destroy,error};return 1;
}
