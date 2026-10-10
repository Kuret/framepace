#include "vk.h"

FP_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface *iface)
{
    (void)iface;
    return VK_ERROR_INITIALIZATION_FAILED;
}
