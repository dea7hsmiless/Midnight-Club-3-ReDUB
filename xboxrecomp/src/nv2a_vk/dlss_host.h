/* Optional DLSS module loading and Vulkan device negotiation. No SDK linkage
 * in the C renderer. Native presentation remains the per-frame fallback until
 * a complete, validated temporal frame is supplied to the runtime module. */
#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#include "dlss_api.h"
static HMODULE s_dlss_module;
static RedubDlssApi s_dlss_api;
static RedubDlssContext *s_dlss_context;
static int s_dlss_mode;

static void dlss_host_close(void)
{
    if(s_dlss_context)s_dlss_api.destroy(s_dlss_context);
    s_dlss_context=NULL;
    if(s_dlss_module)FreeLibrary(s_dlss_module);
    s_dlss_module=NULL;s_dlss_mode=0;memset(&s_dlss_api,0,sizeof s_dlss_api);
}
static int dlss_host_load(void)
{
    wchar_t path[32768],*slash;
    const char *mode=getenv("MC3_DLSS");
    RedubDlssGetApi get;
    s_dlss_mode=mode?atoi(mode):0;
    if(s_dlss_mode<1||s_dlss_mode>3){s_dlss_mode=0;return 0;}
    DWORD length=GetModuleFileNameW(NULL,path,32768);
    if(!length||length>=32768)goto fail;
    slash=wcsrchr(path,L'\\');if(!slash||slash-path>32730)goto fail;
    wcscpy(slash+1,L"redub_dlss.dll");
    s_dlss_module=LoadLibraryExW(path,NULL,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!s_dlss_module)goto fail;
    get=(RedubDlssGetApi)(void *)GetProcAddress(s_dlss_module,"redub_dlss_api");
    if(!get||!get(REDUB_DLSS_ABI,sizeof s_dlss_api,&s_dlss_api))goto fail;
    LOGE("optional DLSS module loaded (mode %d); native frame fallback until temporal inputs are ready\n",s_dlss_mode);
    return 1;
fail:
    LOGE("DLSS module unavailable/incompatible (Windows error %lu); native Vulkan retained\n",GetLastError());
    dlss_host_close();return 0;
}
static int dlss_add_extensions(const char **dest,uint32_t *used,uint32_t capacity,
                               uint32_t count,const char **names)
{
    for(uint32_t i=0;i<count;i++){
        uint32_t j;for(j=0;j<*used;j++)if(!strcmp(dest[j],names[i]))break;
        if(j<*used)continue;
        if(*used>=capacity)return 0;
        dest[(*used)++]=names[i];
    }
    return 1;
}
static void dlss_instance_extensions(const char **dest,uint32_t *used,uint32_t capacity)
{
    uint32_t count=0;const char **names=NULL;
    if(!s_dlss_mode)return;
    uint32_t before=*used;
    if(!s_dlss_api.instance_extensions(&count,&names)||!dlss_add_extensions(dest,used,capacity,count,names)){
        LOGE("DLSS instance negotiation failed: %s; native Vulkan retained\n",s_dlss_api.error());
        *used=before;dlss_host_close();
    }
}
static int dlss_device_extensions(const char **dest,uint32_t *used,uint32_t capacity)
{
    uint32_t count=0,available=0;const char **names=NULL;VkExtensionProperties *properties=NULL;
    uint32_t before=*used;int valid=0;
    if(!s_dlss_mode)return 0;
    if(!s_dlss_api.device_extensions(s_inst,s_pd,&count,&names))goto fail;
    if(vkEnumerateDeviceExtensionProperties(s_pd,NULL,&available,NULL)!=VK_SUCCESS)goto fail;
    properties=calloc(available,sizeof *properties);if(!properties)goto fail;
    if(vkEnumerateDeviceExtensionProperties(s_pd,NULL,&available,properties)!=VK_SUCCESS)goto fail;
    for(uint32_t i=0;i<count;i++){
        uint32_t j;for(j=0;j<available;j++)if(!strcmp(names[i],properties[j].extensionName))break;
        if(j==available)goto fail;
    }
    {
        VkPhysicalDeviceVulkan12Features have={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        VkPhysicalDeviceFeatures2 f={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        f.pNext=&have;vkGetPhysicalDeviceFeatures2(s_pd,&f);
        if(!have.bufferDeviceAddress)goto fail;
    }
    if(!dlss_add_extensions(dest,used,capacity,count,names))goto fail;
    valid=1;
fail:
    free(properties);
    if(!valid){*used=before;LOGE("DLSS device requirements unavailable; native Vulkan retained\n");dlss_host_close();}
    return valid;
}
static void dlss_host_init(void)
{
    uint32_t iw=0,ih=0,ow=s_headless?1920:s_sc_extent.width,oh=s_headless?1080:s_sc_extent.height;
    if(!s_dlss_mode)return;
    s_dlss_context=s_dlss_api.init(s_inst,s_pd,s_dev,L".");
    if(!s_dlss_context){LOGE("DLSS initialization failed: %s; native Vulkan retained\n",s_dlss_api.error());dlss_host_close();return;}
    if(!s_dlss_api.optimal(s_dlss_context,ow,oh,s_dlss_mode,&iw,&ih)){
        LOGE("DLSS dimension query failed: %s; native Vulkan retained\n",s_dlss_api.error());dlss_host_close();return;
    }
    LOGE("DLSS runtime ready: mode %d recommends %ux%u -> %ux%u; rendering remains native pending valid scene/motion/HUD inputs\n",s_dlss_mode,iw,ih,ow,oh);
    s_temporal_on=s_temporal_supported;
    const char *evaluate=getenv("MC3_DLSS_EVALUATE"),*jitter=getenv("MC3_DLSS_JITTER");
    const char *present=getenv("MC3_DLSS_PRESENT");
    s_temporal_present=s_temporal_on&&evaluate&&*evaluate=='1'&&present&&*present=='1';
    s_temporal_jitter=s_temporal_on&&evaluate&&*evaluate=='1'&&jitter&&*jitter=='1';
}
#endif
