#pragma once
#include "dispatch.h"

struct fp_overlay;

/* Create the per-swapchain overlay resources (render pass, pipeline, per-image
 * framebuffer/command buffer/fence/semaphore/vertex buffer). `queue_family`
 * is the family of the queue the swapchain is presented on. Returns NULL on
 * any failure (everything partially created is released). */
struct fp_overlay *fp_overlay_create(fp_device *dev, const fp_swapchain *sc,
                                     uint32_t queue_family);

/* Wait for the GPU to finish with the overlay and free everything. */
void fp_overlay_destroy(fp_device *dev, struct fp_overlay *ov);

/* Record + submit the overlay onto swapchain image `image_index`. The submit
 * waits on the app's present wait semaphores and signals *out_sem, which must
 * replace them in the present. Returns false (nothing consumed) when the
 * overlay was skipped for this frame; sets *broken when it must be disabled. */
bool fp_overlay_draw(fp_device *dev, struct fp_overlay *ov, VkQueue queue,
                     uint32_t image_index, uint32_t wait_count,
                     const VkSemaphore *waits, VkSemaphore *out_sem, bool *broken);
