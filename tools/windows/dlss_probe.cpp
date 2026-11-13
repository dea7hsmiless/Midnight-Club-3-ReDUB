/* Standalone, headless NGX/Vulkan capability probe. No game files required.
 * Build with NVIDIA's separately downloaded SDK; do not vendor its binaries.
 * This verifies runtime support, not a completed game DLSS integration. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vulkan/vulkan.h>
#include <nvsdk_ngx_vk.h>
#include <nvsdk_ngx_helpers.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../../xboxrecomp/src/nv2a_vk/dlss_api.h"

static const char *project_id = "4a94aef4-470d-49bb-b426-b8d4d9150427";
static bool check(NVSDK_NGX_Result r, const char *step)
{
    std::printf("%s: NGX 0x%08x\n", step, unsigned(r));
    return NVSDK_NGX_SUCCEED(r);
}

#include "dlss_probe_eval.h"

int main(int argc, char **argv)
{
    bool bridge_test=false,run_evaluate=false;
    for(int i=1;i<argc;i++){bridge_test|=std::strcmp(argv[i],"--bridge")==0;run_evaluate|=std::strcmp(argv[i],"--evaluate")==0;}
    NVSDK_NGX_FeatureDiscoveryInfo info = {};
    info.SDKVersion = NVSDK_NGX_Version_API;
    info.FeatureID = NVSDK_NGX_Feature_SuperSampling;
    info.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
    info.Identifier.v.ProjectDesc = {project_id, NVSDK_NGX_ENGINE_TYPE_CUSTOM, "ReDUB-experimental-1"};
    info.ApplicationDataPath = L".";
    uint32_t count = 0;
    VkExtensionProperties *required = nullptr;
    if (!check(NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&info, &count, &required), "Instance requirements")) return 1;
    std::vector<const char *> extensions;
    for (uint32_t i = 0; i < count; ++i) extensions.push_back(required[i].extensionName);
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "ReDUB DLSS probe";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = uint32_t(extensions.size());
    ici.ppEnabledExtensionNames = extensions.data();
    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS) {std::puts("Vulkan instance failed"); return 2;}
    struct InstanceGuard {VkInstance v; ~InstanceGuard(){vkDestroyInstance(v,nullptr);}} ig{instance};
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    for (auto candidate : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(candidate, &props);
        std::printf("GPU: %s (vendor %04x)\n", props.deviceName, props.vendorID);
        if (props.vendorID == 0x10de && !gpu) gpu = candidate;
    }
    if (!gpu) {std::puts("No NVIDIA Vulkan device; no settings changed."); return 3;}
    NVSDK_NGX_FeatureRequirement requirement = {};
    if (!check(NVSDK_NGX_VULKAN_GetFeatureRequirements(instance, gpu, &info, &requirement), "Feature requirements")) return 4;
    std::printf("Feature support flags: 0x%x\n", unsigned(requirement.FeatureSupported));
    if (requirement.FeatureSupported) return 5;
    if (!check(NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(instance, gpu, &info, &count, &required), "Device requirements")) return 6;
    extensions.clear();
    for (uint32_t i = 0; i < count; ++i) {
        extensions.push_back(required[i].extensionName);
        std::printf("Required device extension: %s\n", required[i].extensionName);
    }
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, families.data());
    uint32_t family = 0;
    while (family < count && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) ++family;
    if (family == count) return 7;
    float priority = 1;
    VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = family; qci.queueCount = 1; qci.pQueuePriorities = &priority;
    VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    VkPhysicalDeviceVulkan12Features features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    features.bufferDeviceAddress = VK_TRUE;
    dci.pNext = &features;
    dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = uint32_t(extensions.size()); dci.ppEnabledExtensionNames = extensions.data();
    VkDevice device = VK_NULL_HANDLE;
    if (vkCreateDevice(gpu, &dci, nullptr, &device) != VK_SUCCESS) {std::puts("Vulkan device failed");return 8;}
    struct DeviceGuard {VkDevice v; ~DeviceGuard(){vkDestroyDevice(v,nullptr);}} dg{device};
    if(bridge_test) {
        wchar_t path[32768];GetModuleFileNameW(nullptr,path,32768);
        wchar_t *slash=std::wcsrchr(path,L'\\');if(!slash)return 14;
        std::wcscpy(slash+1,L"redub_dlss.dll");
        HMODULE module=LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!module){std::printf("Bridge load failed: %lu\n",GetLastError());return 14;}
        struct ModuleGuard {HMODULE v;~ModuleGuard(){FreeLibrary(v);}} mg{module};
        auto get=reinterpret_cast<RedubDlssGetApi>(GetProcAddress(module,"redub_dlss_api"));
        RedubDlssApi api={};
        if(!get||get(999,sizeof api,&api)||!get(REDUB_DLSS_ABI,sizeof api,&api))return 15;
        auto ctx=api.init(instance,gpu,device,L".");
        if(!ctx){std::puts(api.error());return 16;}
        struct ContextGuard {RedubDlssApi &api;RedubDlssContext *ctx;~ContextGuard(){api.destroy(ctx);}} cg{api,ctx};
        uint32_t w=0,h=0;
        if(!api.optimal(ctx,1920,1080,1,&w,&h)){std::puts(api.error());return 17;}
        std::printf("Bridge Quality: %ux%u -> 1920x1080\n",w,h);
        if(api.evaluate(ctx,VK_NULL_HANDLE,nullptr)){std::puts("Bridge accepted invalid input");return 18;}
        if(run_evaluate)for(int mode=1;mode<=3;mode++)if(!evaluate_test(gpu,device,family,nullptr,&api,ctx,mode)){std::puts(api.error());return 19;}
        std::puts("PASS: optional C ABI bridge initializes, rejects invalid calls, and executes its GPU test.");
        return 0;
    }
    if (!check(NVSDK_NGX_VULKAN_Init_with_ProjectID(project_id, NVSDK_NGX_ENGINE_TYPE_CUSTOM,
        "ReDUB-experimental-1", L".", instance, gpu, device,
        vkGetInstanceProcAddr, vkGetDeviceProcAddr, nullptr, NVSDK_NGX_Version_API), "Initialize NGX")) return 9;
    struct NgxGuard {VkDevice v; ~NgxGuard(){NVSDK_NGX_VULKAN_Shutdown1(v);}} ng{device};
    NVSDK_NGX_Parameter *params = nullptr;
    if (!check(NVSDK_NGX_VULKAN_GetCapabilityParameters(&params), "Capability parameters")) return 10;
    struct ParameterGuard {NVSDK_NGX_Parameter *v; ~ParameterGuard(){NVSDK_NGX_VULKAN_DestroyParameters(v);}} pg{params};
    int available = 0;
    NVSDK_NGX_Parameter_GetI(params, NVSDK_NGX_Parameter_SuperSampling_Available, &available);
    std::printf("DLSS Super Resolution available: %s\n", available ? "YES" : "NO");
    if (!available) return 11;
    for (auto mode : {NVSDK_NGX_PerfQuality_Value_MaxQuality, NVSDK_NGX_PerfQuality_Value_Balanced, NVSDK_NGX_PerfQuality_Value_MaxPerf}) {
        unsigned w=0,h=0,maxw=0,maxh=0,minw=0,minh=0;float sharp=0;
        if (!check(NGX_DLSS_GET_OPTIMAL_SETTINGS(params,1920,1080,mode,&w,&h,&maxw,&maxh,&minw,&minh,&sharp), "Optimal settings")) return 12;
        std::printf("Mode %d: render %ux%u -> 1920x1080\n", int(mode),w,h);
    }
    if(run_evaluate && !evaluate_test(gpu,device,family,params))return 13;
    std::puts("PASS: real NVIDIA runtime reports Super Resolution support. Game rendering is not connected yet.");
    return 0;
}
