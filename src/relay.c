/*
 * Relay submissions: see overlay.h. One fence + semaphore per swapchain image.
 * Fences start signalled and are reset only immediately before a submit, so a
 * fence is unsignalled exactly while a submission using the paired semaphore
 * is pending.
 */
#include <stdlib.h>

#include "overlay.h"

struct fp_relay {
    uint32_t nimages;
    VkFence *fence;
    VkSemaphore *sem;
    bool broken;
};

void fp_relay_destroy(fp_device *dev, struct fp_relay *r)
{
    if (!r)
        return;
    VkDevice d = dev->handle;
    for (uint32_t i = 0; i < r->nimages; i++) {
        if (r->sem && r->sem[i])
            dev->t.DestroySemaphore(d, r->sem[i], NULL);
        if (r->fence && r->fence[i])
            dev->t.DestroyFence(d, r->fence[i], NULL);
    }
    free(r->sem);
    free(r->fence);
    free(r);
}

struct fp_relay *fp_relay_create(fp_device *dev, const fp_swapchain *sc)
{
    VkDevice d = dev->handle;
    uint32_t n = 0;
    if (dev->t.GetSwapchainImagesKHR(d, sc->handle, &n, NULL) != VK_SUCCESS || n == 0)
        return NULL;
    struct fp_relay *r = calloc(1, sizeof *r);
    if (!r)
        return NULL;
    r->fence = calloc(n, sizeof *r->fence);
    r->sem = calloc(n, sizeof *r->sem);
    if (!r->fence || !r->sem) {
        fp_relay_destroy(dev, r);
        return NULL;
    }
    r->nimages = n;
    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                              .flags = VK_FENCE_CREATE_SIGNALED_BIT };
    VkSemaphoreCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    for (uint32_t i = 0; i < n; i++) {
        if (dev->t.CreateFence(d, &fci, NULL, &r->fence[i]) != VK_SUCCESS ||
            dev->t.CreateSemaphore(d, &sci, NULL, &r->sem[i]) != VK_SUCCESS) {
            r->broken = true;   /* nothing submitted yet: safe to destroy */
            fp_relay_destroy(dev, r);
            return NULL;
        }
    }
    return r;
}

void fp_relay_idle(fp_device *dev, struct fp_relay *r)
{
    if (!r || r->broken)
        return;
    for (uint32_t i = 0; i < r->nimages; i++)
        dev->t.WaitForFences(dev->handle, 1, &r->fence[i], VK_TRUE, 1000000000ull);
}

bool fp_relay_acquire(fp_device *dev, struct fp_relay *r, uint32_t idx, uint64_t timeout_ns)
{
    if (r->broken || idx >= r->nimages)
        return false;
    return dev->t.WaitForFences(dev->handle, 1, &r->fence[idx], VK_TRUE, timeout_ns) ==
           VK_SUCCESS;
}

bool fp_relay_submit(fp_device *dev, struct fp_relay *r, VkQueue queue, uint32_t idx,
                     uint32_t wait_count, const VkSemaphore *waits, VkCommandBuffer cb,
                     VkSemaphore *out_sem, bool *broken)
{
    *broken = false;
    if (r->broken || idx >= r->nimages)
        return false;

    /* The overlay draws at COLOR_ATTACHMENT_OUTPUT; a bare relay has no work
     * of its own, so it just waits for everything. */
    VkPipelineStageFlags stage_buf[16];
    VkPipelineStageFlags *stages = stage_buf;
    VkPipelineStageFlags stage = cb ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                    : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    if (wait_count > 16) {
        stages = malloc(wait_count * sizeof *stages);
        if (!stages)
            return false;
    }
    for (uint32_t i = 0; i < wait_count; i++)
        stages[i] = stage;

    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                        .waitSemaphoreCount = wait_count, .pWaitSemaphores = waits,
                        .pWaitDstStageMask = stages,
                        .commandBufferCount = cb ? 1 : 0, .pCommandBuffers = &cb,
                        .signalSemaphoreCount = 1, .pSignalSemaphores = &r->sem[idx] };
    bool ok = false;
    if (dev->t.ResetFences(dev->handle, 1, &r->fence[idx]) != VK_SUCCESS) {
        r->broken = *broken = true;
    } else if (dev->t.QueueSubmit(queue, 1, &si, r->fence[idx]) != VK_SUCCESS) {
        r->broken = *broken = true;   /* fence is left unsignalled: never reuse */
    } else {
        *out_sem = r->sem[idx];
        ok = true;
    }
    if (stages != stage_buf)
        free(stages);
    return ok;
}

bool fp_relay_wait(fp_device *dev, struct fp_relay *r, uint32_t idx, uint64_t timeout_ns)
{
    if (r->broken || idx >= r->nimages)
        return false;
    return dev->t.WaitForFences(dev->handle, 1, &r->fence[idx], VK_TRUE, timeout_ns) ==
           VK_SUCCESS;
}
