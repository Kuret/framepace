#pragma once
/* Vulkan headers without prototypes: the layer never links libvulkan. */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#define FP_EXPORT __attribute__((visibility("default")))
