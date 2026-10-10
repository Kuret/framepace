/*
 * framepace - Vulkan implicit layer: runtime-adjustable frame limiter and
 * a minimal overlay. This file holds the loader plumbing (negotiation,
 * proc-addr dispatch, instance/device bookkeeping) and the hooked entry points.
 */
#include <stdlib.h>
#include <string.h>

#include "dispatch.h"
#include "limiter.h"
#include "state.h"
#include "util.h"

/* ---- instance / device registries (dispatch key -> object) -------------- */

static pthread_mutex_t g_reg_lock = PTHREAD_MUTEX_INITIALIZER;
static fp_instance *g_instances;
static fp_device *g_devices;

static inline void *get_key(const void *dispatchable)
{
    return *(void *const *)dispatchable;
}

static fp_instance *instance_from_key(void *key)
{
    pthread_mutex_lock(&g_reg_lock);
    fp_instance *i = g_instances;
    while (i && i->key != key)
        i = i->next;
    pthread_mutex_unlock(&g_reg_lock);
    return i;
}

static fp_device *device_from_key(void *key)
{
    pthread_mutex_lock(&g_reg_lock);
    fp_device *d = g_devices;
    while (d && d->key != key)
        d = d->next;
    pthread_mutex_unlock(&g_reg_lock);
    return d;
}

/* ---- queue tracking ------------------------------------------------------ */

static void track_queue(fp_device *dev, VkQueue q, uint32_t family)
{
    if (!q)
        return;
    pthread_mutex_lock(&dev->lock);
    bool known = false;
    for (uint32_t i = 0; i < dev->nqueues; i++)
        if (dev->queues[i].handle == q)
            known = true;
    if (!known && dev->nqueues < FP_MAX_QUEUES) {
        dev->queues[dev->nqueues].handle = q;
        dev->queues[dev->nqueues].family = family;
        dev->nqueues++;
    }
    pthread_mutex_unlock(&dev->lock);
}

/* Find the device owning a queue by handle (the loader may not have stamped
 * the dispatch pointer yet when we first saw it, so handles are the primary
 * key) and fall back to the dispatch key. */
static fp_device *device_from_queue(VkQueue q, uint32_t *family_out)
{
    fp_device *found = NULL;
    uint32_t fam = UINT32_MAX;
    pthread_mutex_lock(&g_reg_lock);
    for (fp_device *d = g_devices; d && !found; d = d->next) {
        pthread_mutex_lock(&d->lock);
        for (uint32_t i = 0; i < d->nqueues; i++) {
            if (d->queues[i].handle == q) {
                found = d;
                fam = d->queues[i].family;
                break;
            }
        }
        pthread_mutex_unlock(&d->lock);
    }
    pthread_mutex_unlock(&g_reg_lock);
    if (!found) {
        found = device_from_key(get_key(q));
    }
    if (family_out)
        *family_out = fam;
    return found;
}

static VKAPI_ATTR void VKAPI_CALL fp_GetDeviceQueue(VkDevice device, uint32_t family,
                                                    uint32_t index, VkQueue *pQueue)
{
    fp_device *dev = device_from_key(get_key(device));
    dev->t.GetDeviceQueue(device, family, index, pQueue);
    if (pQueue)
        track_queue(dev, *pQueue, family);
}

static VKAPI_ATTR void VKAPI_CALL fp_GetDeviceQueue2(VkDevice device,
                                                     const VkDeviceQueueInfo2 *pInfo,
                                                     VkQueue *pQueue)
{
    fp_device *dev = device_from_key(get_key(device));
    dev->t.GetDeviceQueue2(device, pInfo, pQueue);
    if (pQueue && pInfo)
        track_queue(dev, *pQueue, pInfo->queueFamilyIndex);
}

/* ---- instance ------------------------------------------------------------ */

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fp_GetInstanceProcAddr(VkInstance, const char *);
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fp_GetDeviceProcAddr(VkDevice, const char *);

static VKAPI_ATTR VkResult VKAPI_CALL
fp_CreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator, VkInstance *pInstance)
{
    VkLayerInstanceCreateInfo *link = (VkLayerInstanceCreateInfo *)pCreateInfo->pNext;
    while (link && !(link->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO &&
                     link->function == VK_LAYER_LINK_INFO))
        link = (VkLayerInstanceCreateInfo *)link->pNext;
    if (!link || !link->u.pLayerInfo)
        return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr next_gipa = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkCreateInstance next_create =
        (PFN_vkCreateInstance)next_gipa(VK_NULL_HANDLE, "vkCreateInstance");
    if (!next_create)
        return VK_ERROR_INITIALIZATION_FAILED;

    link->u.pLayerInfo = link->u.pLayerInfo->pNext;   /* advance the chain */
    VkResult res = next_create(pCreateInfo, pAllocator, pInstance);
    if (res != VK_SUCCESS)
        return res;

    fp_instance *inst = calloc(1, sizeof *inst);
    if (!inst)
        return VK_SUCCESS;   /* un-hooked instance still works (never found => passthrough) */
    inst->handle = *pInstance;
    inst->key = get_key(*pInstance);
    inst->t.GetInstanceProcAddr = next_gipa;
#define X(n) inst->t.n = (PFN_vk##n)next_gipa(*pInstance, "vk" #n);
    FP_INSTANCE_FUNCS(X)
#undef X

    pthread_mutex_lock(&g_reg_lock);
    inst->next = g_instances;
    g_instances = inst;
    pthread_mutex_unlock(&g_reg_lock);

    fp_state_open();
    fp_log("layer loaded (instance %p)", (void *)*pInstance);
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL fp_DestroyInstance(VkInstance instance,
                                                     const VkAllocationCallbacks *pAllocator)
{
    void *key = get_key(instance);
    fp_instance *inst = NULL;
    pthread_mutex_lock(&g_reg_lock);
    for (fp_instance **p = &g_instances; *p; p = &(*p)->next) {
        if ((*p)->key == key) {
            inst = *p;
            *p = inst->next;
            break;
        }
    }
    pthread_mutex_unlock(&g_reg_lock);
    if (!inst)
        return;
    PFN_vkDestroyInstance next = inst->t.DestroyInstance;
    free(inst);
    next(instance, pAllocator);
}

/* ---- device -------------------------------------------------------------- */

static VKAPI_ATTR VkResult VKAPI_CALL
fp_CreateDevice(VkPhysicalDevice phys, const VkDeviceCreateInfo *pCreateInfo,
                const VkAllocationCallbacks *pAllocator, VkDevice *pDevice)
{
    fp_instance *inst = instance_from_key(get_key(phys));

    VkLayerDeviceCreateInfo *link = (VkLayerDeviceCreateInfo *)pCreateInfo->pNext;
    while (link && !(link->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO &&
                     link->function == VK_LAYER_LINK_INFO))
        link = (VkLayerDeviceCreateInfo *)link->pNext;
    if (!link || !link->u.pLayerInfo)
        return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr next_gipa = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr next_gdpa = link->u.pLayerInfo->pfnNextGetDeviceProcAddr;

    PFN_vkSetDeviceLoaderData set_data = NULL;
    for (VkLayerDeviceCreateInfo *c = (VkLayerDeviceCreateInfo *)pCreateInfo->pNext; c;
         c = (VkLayerDeviceCreateInfo *)c->pNext) {
        if (c->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO &&
            c->function == VK_LOADER_DATA_CALLBACK) {
            set_data = c->u.pfnSetDeviceLoaderData;
            break;
        }
    }

    VkInstance inst_handle = inst ? inst->handle : VK_NULL_HANDLE;
    PFN_vkCreateDevice next_create =
        (PFN_vkCreateDevice)next_gipa(inst_handle, "vkCreateDevice");
    if (!next_create)
        return VK_ERROR_INITIALIZATION_FAILED;

    link->u.pLayerInfo = link->u.pLayerInfo->pNext;
    VkResult res = next_create(phys, pCreateInfo, pAllocator, pDevice);
    if (res != VK_SUCCESS)
        return res;
    if (!inst)
        return VK_SUCCESS;

    fp_device *dev = calloc(1, sizeof *dev);
    if (!dev)
        return VK_SUCCESS;
    dev->handle = *pDevice;
    dev->key = get_key(*pDevice);
    dev->phys = phys;
    dev->inst = inst;
    dev->set_loader_data = set_data;
    pthread_mutex_init(&dev->lock, NULL);
    dev->t.GetDeviceProcAddr = next_gdpa;
#define X(n) dev->t.n = (PFN_vk##n)next_gdpa(*pDevice, "vk" #n);
    FP_DEVICE_FUNCS(X)
#undef X

    if (inst->t.GetPhysicalDeviceQueueFamilyProperties) {
        VkQueueFamilyProperties props[FP_MAX_FAMILIES];
        uint32_t n = FP_MAX_FAMILIES;
        inst->t.GetPhysicalDeviceQueueFamilyProperties(phys, &n, props);
        if (n > FP_MAX_FAMILIES)
            n = FP_MAX_FAMILIES;
        dev->nfamilies = n;
        for (uint32_t i = 0; i < n; i++)
            dev->family_flags[i] = props[i].queueFlags;
    }
    if (inst->t.GetPhysicalDeviceMemoryProperties)
        inst->t.GetPhysicalDeviceMemoryProperties(phys, &dev->memprops);

    pthread_mutex_lock(&g_reg_lock);
    dev->next = g_devices;
    g_devices = dev;
    pthread_mutex_unlock(&g_reg_lock);

    fp_log("device created (%p), %u queue families", (void *)*pDevice, dev->nfamilies);
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL fp_DestroyDevice(VkDevice device,
                                                   const VkAllocationCallbacks *pAllocator)
{
    void *key = get_key(device);
    fp_device *dev = NULL;
    pthread_mutex_lock(&g_reg_lock);
    for (fp_device **p = &g_devices; *p; p = &(*p)->next) {
        if ((*p)->key == key) {
            dev = *p;
            *p = dev->next;
            break;
        }
    }
    pthread_mutex_unlock(&g_reg_lock);
    if (!dev)
        return;
    PFN_vkDestroyDevice next = dev->t.DestroyDevice;
    pthread_mutex_destroy(&dev->lock);
    free(dev);
    next(device, pAllocator);
}

/* ---- present ------------------------------------------------------------- */

static pthread_mutex_t g_stat_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_prev_present_ns;   /* previous frame's hand-down time */
static uint64_t g_prev_return_ns;    /* when the previous present returned */

static VKAPI_ATTR VkResult VKAPI_CALL fp_QueuePresentKHR(VkQueue queue,
                                                         const VkPresentInfoKHR *pPresentInfo)
{
    uint64_t entry_ns = fp_now_ns();
    uint32_t family;
    fp_device *dev = device_from_queue(queue, &family);
    if (!dev)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (!g_fp_state)
        return dev->t.QueuePresentKHR(queue, pPresentInfo);   /* pure passthrough */

    VkPresentInfoKHR present = *pPresentInfo;

    /* Limiter: after the overlay has been submitted, before handing down. */
    uint64_t present_ns = fp_limiter_wait(fp_state_cap());

    VkResult res = dev->t.QueuePresentKHR(queue, &present);
    uint64_t return_ns = fp_now_ns();

    pthread_mutex_lock(&g_stat_lock);
    uint32_t frame_ns = g_prev_present_ns ? (uint32_t)(present_ns - g_prev_present_ns) : 0;
    uint32_t work_ns = g_prev_return_ns && entry_ns > g_prev_return_ns
                           ? (uint32_t)(entry_ns - g_prev_return_ns) : 0;
    g_prev_present_ns = present_ns;
    g_prev_return_ns = return_ns;
    fp_state_record(present_ns, frame_ns, work_ns);
    pthread_mutex_unlock(&g_stat_lock);
    return res;
}

/* ---- proc-addr dispatch -------------------------------------------------- */

#define HOOK_INSTANCE(n) if (!strcmp(name, "vk" #n)) return (PFN_vkVoidFunction)fp_##n
#define HOOK_DEVICE(n)                                                       \
    if (!strcmp(name, "vk" #n))                                              \
        return (PFN_vkVoidFunction)fp_##n

static PFN_vkVoidFunction device_hook(const char *name)
{
    HOOK_DEVICE(GetDeviceProcAddr);
    HOOK_DEVICE(DestroyDevice);
    HOOK_DEVICE(GetDeviceQueue);
    HOOK_DEVICE(GetDeviceQueue2);
    HOOK_DEVICE(QueuePresentKHR);
    return NULL;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fp_GetDeviceProcAddr(VkDevice device,
                                                                      const char *name)
{
    if (!device || !name)
        return NULL;
    fp_device *dev = device_from_key(get_key(device));
    if (!dev)
        return NULL;
    PFN_vkVoidFunction hook = device_hook(name);
    if (hook) {
        /* Only hand out a hook when the next layer/driver implements it. */
        PFN_vkVoidFunction next = dev->t.GetDeviceProcAddr(device, name);
        return next ? hook : NULL;
    }
    return dev->t.GetDeviceProcAddr(device, name);
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fp_GetInstanceProcAddr(VkInstance instance,
                                                                        const char *name)
{
    if (!name)
        return NULL;
    HOOK_INSTANCE(GetInstanceProcAddr);
    HOOK_INSTANCE(CreateInstance);
    if (!instance)
        return NULL;
    fp_instance *inst = instance_from_key(get_key(instance));
    if (!inst)
        return NULL;
    HOOK_INSTANCE(DestroyInstance);
    HOOK_INSTANCE(CreateDevice);
    HOOK_INSTANCE(GetDeviceProcAddr);
    PFN_vkVoidFunction hook = device_hook(name);
    if (hook) {
        PFN_vkVoidFunction next = inst->t.GetInstanceProcAddr(instance, name);
        return next ? hook : NULL;
    }
    return inst->t.GetInstanceProcAddr(instance, name);
}

/* ---- loader entry points ------------------------------------------------- */

FP_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface *iface)
{
    if (!iface || iface->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (iface->loaderLayerInterfaceVersion > 2)
        iface->loaderLayerInterfaceVersion = 2;
    if (iface->loaderLayerInterfaceVersion >= 2) {
        iface->pfnGetInstanceProcAddr = fp_GetInstanceProcAddr;
        iface->pfnGetDeviceProcAddr = fp_GetDeviceProcAddr;
        iface->pfnGetPhysicalDeviceProcAddr = NULL;
    }
    return VK_SUCCESS;
}

FP_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char *name)
{
    return fp_GetInstanceProcAddr(instance, name);
}

FP_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetDeviceProcAddr(VkDevice device, const char *name)
{
    return fp_GetDeviceProcAddr(device, name);
}
