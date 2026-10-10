#pragma once
#include <stdint.h>

/* Block until the next frame slot for `cap` fps (early limiter: sleeps in
 * vkQueuePresentKHR before the frame is handed to the driver). cap <= 0
 * means unlimited. Returns the CLOCK_MONOTONIC time at which the caller
 * should hand the frame down. Safe to call from any thread. */
uint64_t fp_limiter_wait(int32_t cap);
