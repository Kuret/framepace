#pragma once
#include <stdint.h>

/* Block until the next frame slot for `cap` fps (early limiter: sleeps in
 * vkQueuePresentKHR before the frame is handed to the driver). cap <= 0
 * means unlimited. Returns the CLOCK_MONOTONIC time at which the caller
 * should hand the frame down. Safe to call from any thread.
 *
 * A cap change does not take the new interval in one frame. The paced
 * interval moves by at most FP_LIMITER_SLEW_NS per frame, so a large drop or
 * climb is a ramp. A frame that is already late while the new cap is slower
 * is presented immediately and the interval snaps: a drop must not add wait
 * to a frame that has already missed. */
uint64_t fp_limiter_wait(int32_t cap);

#define FP_LIMITER_SLEW_NS 400000ull   /* 0.4 ms of interval per frame */
#define FP_LIMITER_LATE_SLACK_NS 1000000ull   /* overrun this small still slews; it is jitter */

/* Pace decision, split out so tests can step it without sleeping.
 * `last_present` is when the previous frame was handed down; fp_limiter_wait
 * updates it after the wait. Zeroed state is a first frame. */
struct fp_pace {
    uint64_t deadline;      /* previous slot (0 = none yet) */
    uint64_t interval;      /* paced interval, slewing toward 1e9/cap */
    uint64_t last_present;
    int32_t last_cap;
};

uint64_t fp_limiter_next(struct fp_pace *p, uint64_t now, int32_t cap);
