#pragma once
#include <pthread.h>
#include <stdbool.h>

#include "vk.h"

#define FP_MAX_FAMILIES 32
#define FP_MAX_QUEUES 64

#define FP_INSTANCE_FUNCS(X)                       \
    X(DestroyInstance)                             \
    X(GetPhysicalDeviceQueueFamilyProperties)      \
    X(GetPhysicalDeviceMemoryProperties)           \
    X(GetPhysicalDeviceSurfaceCapabilitiesKHR)

#define FP_DEVICE_FUNCS(X)                         \
    X(DestroyDevice)                               \
    X(GetDeviceQueue)                              \
    X(GetDeviceQueue2)                             \
    X(CreateSwapchainKHR)                          \
    X(DestroySwapchainKHR)                         \
    X(GetSwapchainImagesKHR)                       \
    X(QueuePresentKHR)                             \
    X(QueueSubmit)                                 \
    X(CreateImageView)                             \
    X(DestroyImageView)                            \
    X(CreateRenderPass)                            \
    X(DestroyRenderPass)                           \
    X(CreateFramebuffer)                           \
    X(DestroyFramebuffer)                          \
    X(CreateCommandPool)                           \
    X(DestroyCommandPool)                          \
    X(AllocateCommandBuffers)                      \
    X(BeginCommandBuffer)                          \
    X(EndCommandBuffer)                            \
    X(ResetCommandBuffer)                          \
    X(CreateFence)                                 \
    X(DestroyFence)                                \
    X(WaitForFences)                               \
    X(ResetFences)                                 \
    X(CreateSemaphore)                             \
    X(DestroySemaphore)                            \
    X(CreateBuffer)                                \
    X(DestroyBuffer)                               \
    X(GetBufferMemoryRequirements)                 \
    X(AllocateMemory)                              \
    X(FreeMemory)                                  \
    X(BindBufferMemory)                            \
    X(MapMemory)                                   \
    X(UnmapMemory)                                 \
    X(CreateShaderModule)                          \
    X(DestroyShaderModule)                         \
    X(CreatePipelineLayout)                        \
    X(DestroyPipelineLayout)                       \
    X(CreateGraphicsPipelines)                     \
    X(DestroyPipeline)                             \
    X(CmdBeginRenderPass)                          \
    X(CmdEndRenderPass)                            \
    X(CmdBindPipeline)                             \
    X(CmdBindVertexBuffers)                        \
    X(CmdSetViewport)                              \
    X(CmdSetScissor)                               \
    X(CmdDraw)

typedef struct fp_instance_table {
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr;
#define X(n) PFN_vk##n n;
    FP_INSTANCE_FUNCS(X)
#undef X
} fp_instance_table;

typedef struct fp_device_table {
    PFN_vkGetDeviceProcAddr GetDeviceProcAddr;
#define X(n) PFN_vk##n n;
    FP_DEVICE_FUNCS(X)
#undef X
} fp_device_table;

struct fp_overlay;
struct fp_relay;

typedef struct fp_swapchain {
    struct fp_swapchain *next;
    VkSwapchainKHR handle;
    VkFormat format;
    VkExtent2D extent;
    bool usage_ok;            /* COLOR_ATTACHMENT was added (or already present) */
    int ov_state;             /* 0 = not tried, 1 = ready, 2 = disabled */
    struct fp_overlay *ov;
    int relay_state;          /* same encoding */
    struct fp_relay *relay;
} fp_swapchain;

typedef struct fp_instance {
    struct fp_instance *next;
    void *key;
    VkInstance handle;
    fp_instance_table t;
} fp_instance;

typedef struct fp_device {
    struct fp_device *next;
    void *key;
    VkDevice handle;
    VkPhysicalDevice phys;
    fp_instance *inst;
    fp_device_table t;
    PFN_vkSetDeviceLoaderData set_loader_data;

    uint32_t nfamilies;
    VkQueueFlags family_flags[FP_MAX_FAMILIES];
    VkPhysicalDeviceMemoryProperties memprops;

    uint32_t nqueues;
    struct { VkQueue handle; uint32_t family; } queues[FP_MAX_QUEUES];

    pthread_mutex_t lock;     /* queues + swapchain list */
    fp_swapchain *swapchains;
} fp_device;
