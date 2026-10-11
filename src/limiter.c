/*
 * Frame limiter, equivalent to MangoHud's fps_limit_method=early: wait
 * before presenting, on a fixed deadline grid so that jitter in one frame
 * does not shift the following ones.
 *
 * While the cap's interval and the paced interval differ, the interval moves
 * by at most FP_LIMITER_SLEW_NS per frame and each deadline is last_present
 * plus that interval. Once they match, the fixed grid resumes.
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
static struct fp_pace g_pace;

static void sleep_until(uint64_t t)
{
    struct timespec ts = { .tv_sec = (time_t)(t / 1000000000ull),
                           .tv_nsec = (long)(t % 1000000000ull) };
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) != 0)
        ; /* EINTR: retry with the same absolute time */
}

uint64_t fp_limiter_next(struct fp_pace *p, uint64_t now, int32_t cap)
{
    if (cap <= 0) {
        p->deadline = now;
        p->interval = 0;
        p->last_cap = cap;
        p->last_present = now;
        return now;
    }

    uint64_t want = 1000000000ull / (uint64_t)cap;
    uint64_t interval = p->interval ? p->interval : want;
    uint64_t deadline;

    if (p->deadline == 0 || p->last_cap <= 0) {
        deadline = now;                         /* first capped frame: no wait */
        interval = want;
    } else {
        int dropping = want > interval;
        /* Already past the pace we are on, and the new cap is slower.
         * Present now and take the new interval next: stretching this frame
         * would add delay to one that has already missed. A sub-millisecond
         * overrun is the limiter's own jitter and keeps slewing. */
        int late = p->last_present &&
                   now > p->last_present + interval + FP_LIMITER_LATE_SLACK_NS;
        if (late && dropping) {
            interval = want;
            deadline = now;
        } else if (interval != want) {
            if (want > interval) {
                uint64_t room = want - interval;
                interval += room < FP_LIMITER_SLEW_NS ? room : FP_LIMITER_SLEW_NS;
            } else {
                uint64_t room = interval - want;
                interval -= room < FP_LIMITER_SLEW_NS ? room : FP_LIMITER_SLEW_NS;
            }
            deadline = p->last_present + interval;
        } else {
            deadline = p->deadline + interval;  /* steady: stay on the grid */
        }
        if (deadline != now && now > deadline + interval)
            deadline = now;                     /* fell behind by > 1 frame: resync */
    }

    p->deadline = deadline;
    p->interval = interval;
    p->last_cap = cap;
    return deadline;
}

uint64_t fp_limiter_wait(int32_t cap)
{
    uint64_t now = fp_now_ns();

    pthread_mutex_lock(&g_lock);
    uint64_t deadline = fp_limiter_next(&g_pace, now, cap);
    pthread_mutex_unlock(&g_lock);

    if (cap <= 0)
        return deadline;

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
    g_pace.last_present = out;
    pthread_mutex_unlock(&g_lock);
    return out;
}
