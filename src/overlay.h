#pragma once
#include "dispatch.h"

/*
 * Relay: a per-swapchain set of per-image fences and semaphores used to
 * interpose our own queue submission between the app's rendering and the
 * present. The submit waits on the app's present semaphores, optionally runs
 * the overlay command buffer, and signals our semaphore + fence. Waiting on
 * the fence on the CPU is the "frame rendered" point used by the limiter.
 */
struct fp_relay;
struct fp_overlay;

struct fp_relay *fp_relay_create(fp_device *dev, const fp_swapchain *sc);
/* Block (up to 1 s per image) until every relay submission has finished. */
void fp_relay_idle(fp_device *dev, struct fp_relay *r);
void fp_relay_destroy(fp_device *dev, struct fp_relay *r);

/* Wait until the previous use of image `idx` has finished, so its semaphore,
 * fence, command buffer and vertex buffer can be reused. False on timeout. */
bool fp_relay_acquire(fp_device *dev, struct fp_relay *r, uint32_t idx, uint64_t timeout_ns);

/* Submit on `queue`: wait on `waits` (app's present semaphores), run `cb`
 * (may be VK_NULL_HANDLE), signal the image's semaphore (*out_sem) and fence.
 * Returns false when nothing was submitted; *broken is set if the relay must
 * no longer be used. */
bool fp_relay_submit(fp_device *dev, struct fp_relay *r, VkQueue queue, uint32_t idx,
                     uint32_t wait_count, const VkSemaphore *waits, VkCommandBuffer cb,
                     VkSemaphore *out_sem, bool *broken);

/* CPU-wait for the submit made for image `idx`. True if it signalled in time. */
bool fp_relay_wait(fp_device *dev, struct fp_relay *r, uint32_t idx, uint64_t timeout_ns);

/* Create the per-swapchain overlay resources (render pass, pipeline, per-image
 * framebuffer/command buffer/vertex buffer). `queue_family` is the family of
 * the queue the swapchain is presented on. NULL on any failure. */
struct fp_overlay *fp_overlay_create(fp_device *dev, const fp_swapchain *sc,
                                     uint32_t queue_family);

/* Free everything. The caller must have made the GPU idle (fp_relay_idle). */
void fp_overlay_destroy(fp_device *dev, struct fp_overlay *ov);

/* Record the overlay for image `idx` into its command buffer (the caller must
 * have fp_relay_acquire'd that image). False = skipped this frame; *broken
 * is set when the overlay must be disabled. */
bool fp_overlay_record(fp_device *dev, struct fp_overlay *ov, uint32_t idx,
                       VkCommandBuffer *out_cb, bool *broken);
