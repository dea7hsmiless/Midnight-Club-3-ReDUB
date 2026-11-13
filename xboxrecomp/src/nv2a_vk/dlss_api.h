/* ReDUB's C ABI between the C renderer and optional MSVC/NVIDIA SDK module.
 * No NVIDIA headers or binaries are required by the ordinary game build.
 * All calls belong to the renderer thread. Inputs must be in GENERAL layout;
 * the caller owns barriers, command submission, GPU completion and images. */
#ifndef REDUB_DLSS_API_H
#define REDUB_DLSS_API_H
#include <stdint.h>
#include <stddef.h>
#include <vulkan/vulkan.h>
#define REDUB_DLSS_ABI 1u
typedef struct RedubDlssContext RedubDlssContext;
typedef struct {
    VkImage image;
    VkImageView view;
    VkFormat format;
    uint32_t width, height;
} RedubDlssImage;
typedef struct {
    RedubDlssImage color, depth, motion, output, rejection;
    float jitter_x, jitter_y, frame_ms;
    int reset;
} RedubDlssFrame;
typedef struct {
    uint32_t abi;
    int (*instance_extensions)(uint32_t *count, const char ***names);
    int (*device_extensions)(VkInstance, VkPhysicalDevice, uint32_t *count, const char ***names);
    RedubDlssContext *(*init)(VkInstance, VkPhysicalDevice, VkDevice, const wchar_t *cache);
    int (*optimal)(RedubDlssContext *, uint32_t ow, uint32_t oh, int mode, uint32_t *iw, uint32_t *ih);
    int (*create)(RedubDlssContext *, VkCommandBuffer, uint32_t iw, uint32_t ih, uint32_t ow, uint32_t oh, int mode, int inverted_depth);
    int (*evaluate)(RedubDlssContext *, VkCommandBuffer, const RedubDlssFrame *);
    void (*destroy)(RedubDlssContext *);
    const char *(*error)(void);
} RedubDlssApi;
typedef int (*RedubDlssGetApi)(uint32_t abi, size_t size, RedubDlssApi *);
#endif
