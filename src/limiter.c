/*
 * Frame limiter, equivalent to MangoHud's fps_limit_method=early: wait
 * before presenting, on a fixed deadline grid so that jitter in one frame
 * does not shift the following ones.
 */
#include "limiter.h"

#include <pthread.h>
#include <sched.h>
#include <time.h>
#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#define cpu_relax() _mm_pause()
#else
#define cpu_relax() sched_yield()
#endif

#include "util.h"

#define SPIN_MARGIN_NS 300000ull   /* sleep until deadline - 300us, then spin */

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_deadline;        /* slot of the previous frame (0 = none yet) */
static uint64_t g_last_present;     /* when the previous frame was handed down */
static int32_t g_last_cap;

static void sleep_until(uint64_t t)
{
    struct timespec ts = { .tv_sec = (time_t)(t / 1000000000ull),
                           .tv_nsec = (long)(t % 1000000000ull) };
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) != 0)
        ; /* EINTR: retry with the same absolute time */
}

uint64_t fp_limiter_wait(int32_t cap)
{
    uint64_t now = fp_now_ns();

    pthread_mutex_lock(&g_lock);
    if (cap <= 0) {
        g_deadline = now;
        g_last_cap = cap;
        g_last_present = now;
        pthread_mutex_unlock(&g_lock);
        return now;
    }

    uint64_t interval = 1000000000ull / (uint64_t)cap;
    uint64_t deadline;
    if (g_deadline == 0 || g_last_cap <= 0) {
        deadline = now;                         /* first capped frame: no wait */
    } else if (cap != g_last_cap) {
        /* Interval changed: restart the grid from the last present so the
         * schedule continues smoothly instead of jumping. */
        deadline = g_last_present + interval;
    } else {
        deadline = g_deadline + interval;
    }
    if (now > deadline + interval)
        deadline = now;                         /* fell behind by > 1 frame: resync */
    g_deadline = deadline;
    g_last_cap = cap;
    pthread_mutex_unlock(&g_lock);

    if (deadline > now) {
        if (deadline - now > SPIN_MARGIN_NS)
            sleep_until(deadline - SPIN_MARGIN_NS);
        unsigned n = 0;
        while (fp_now_ns() < deadline) {
            cpu_relax();
            if ((++n & 0x3ff) == 0)
                sched_yield();
        }
    }

    uint64_t out = fp_now_ns();
    pthread_mutex_lock(&g_lock);
    g_last_present = out;
    pthread_mutex_unlock(&g_lock);
    return out;
}
