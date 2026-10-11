/* Pace decisions: grid hold, slew on a cap change, snap when already late. */
#include <stdio.h>
#include <string.h>

#include "../src/limiter.h"

static int failures;

static void check_u64(const char *what, unsigned long long got, unsigned long long want)
{
    if (got != want) {
        printf("FAIL %s: got %llu, want %llu\n", what, got, want);
        failures++;
    } else {
        printf("ok   %s\n", what);
    }
}

int main(void)
{
    const unsigned long long T = 10000000000ull;
    struct fp_pace p;

    /* First capped frame is not waited. */
    memset(&p, 0, sizeof p);
    check_u64("first frame", fp_limiter_next(&p, T, 100), T);
    check_u64("first interval", p.interval, 10000000ull);

    /* Steady cap: a late present does not shift the grid. */
    memset(&p, 0, sizeof p);
    unsigned long long d0 = fp_limiter_next(&p, T, 100);
    p.last_present = d0 + 50000ull;             /* 50 us of spin overshoot */
    unsigned long long d1 = fp_limiter_next(&p, p.last_present + 8000000ull, 100);
    check_u64("grid ignores present jitter", d1, T + 10000000ull);
    p.last_present = d1 + 200000ull;            /* presented 0.2 ms late */
    unsigned long long d2 = fp_limiter_next(&p, p.last_present + 10000000ull, 100);
    check_u64("grid holds after a late frame", d2, T + 20000000ull);

    /* 100 -> 60. First frame takes 0.4 ms, not the whole 6.67 ms. */
    memset(&p, 0, sizeof p);
    p.last_present = T;
    fp_limiter_next(&p, T, 100);                /* establish interval 10 ms; deadline T */
    p.last_present = T;
    unsigned long long sd = fp_limiter_next(&p, T + 8000000ull, 60);
    check_u64("slew step", p.interval, 10000000ull + FP_LIMITER_SLEW_NS);
    check_u64("slew deadline", sd, T + p.interval);
    check_u64("slew is not the full drop", p.interval < 16000000ull, 1);

    /* Early frames walk the interval onto 1e9/60. */
    memset(&p, 0, sizeof p);
    fp_limiter_next(&p, T, 100);
    p.last_present = T;
    unsigned long long now = T + 8000000ull;
    int grew = 1;
    for (int i = 0; i < 30 && p.interval != 1000000000ull / 60; i++) {
        unsigned long long before = p.interval;
        unsigned long long dl = fp_limiter_next(&p, now, 60);
        if (p.interval > before + FP_LIMITER_SLEW_NS || p.interval < before) {
            grew = 0;
            break;
        }
        if (dl != p.last_present + p.interval && dl != now) {
            grew = 0;
            break;
        }
        p.last_present = dl > now ? dl : now;
        now = p.last_present + 8000000ull;
    }
    check_u64("slew reaches 60 fps", p.interval, 1000000000ull / 60);
    check_u64("slew never jumps backward or past one step", (unsigned long long)grew, 1);

    /* Already missing, and the new cap is slower: present now, snap. */
    memset(&p, 0, sizeof p);
    fp_limiter_next(&p, T, 100);
    p.last_present = T;
    unsigned long long late_now = T + 10000000ull + FP_LIMITER_LATE_SLACK_NS + 1;
    unsigned long long ld = fp_limiter_next(&p, late_now, 60);
    check_u64("late drop snaps interval", p.interval, 1000000000ull / 60);
    check_u64("late drop presents now", ld, late_now);

    /* Overrun inside the slack keeps slewing. */
    memset(&p, 0, sizeof p);
    fp_limiter_next(&p, T, 100);
    p.last_present = T;
    fp_limiter_next(&p, T + 10000000ull + FP_LIMITER_LATE_SLACK_NS / 2, 60);
    check_u64("slack overrun still slews", p.interval, 10000000ull + FP_LIMITER_SLEW_NS);

    /* Raising the cap slews the interval down, including when this frame is late. */
    memset(&p, 0, sizeof p);
    fp_limiter_next(&p, T, 60);
    p.interval = 1000000000ull / 60;
    p.last_cap = 60;
    p.deadline = T;
    p.last_present = T;
    fp_limiter_next(&p, T + 8000000ull, 100);
    check_u64("raise slews down", p.interval, 1000000000ull / 60 - FP_LIMITER_SLEW_NS);

    memset(&p, 0, sizeof p);
    fp_limiter_next(&p, T, 60);
    p.interval = 1000000000ull / 60;
    p.last_cap = 60;
    p.deadline = T;
    p.last_present = T;
    unsigned long long raise_now = T + p.interval + FP_LIMITER_LATE_SLACK_NS + 5000000ull;
    fp_limiter_next(&p, raise_now, 100);
    check_u64("late raise still slews", p.interval, 1000000000ull / 60 - FP_LIMITER_SLEW_NS);

    /* More than one interval behind: resync to now. */
    memset(&p, 0, sizeof p);
    fp_limiter_next(&p, T, 100);
    p.last_present = T;
    unsigned long long behind = T + 30000000ull + 1;
    unsigned long long rd = fp_limiter_next(&p, behind, 100);
    check_u64("resync when a frame behind", rd, behind);

    /* Uncapped resets; the next capped frame does not slew from stale state. */
    memset(&p, 0, sizeof p);
    fp_limiter_next(&p, T, 100);
    p.interval = 10000000ull;
    p.last_cap = 100;
    p.deadline = T;
    p.last_present = T;
    fp_limiter_next(&p, T + 5000000ull, 0);
    unsigned long long again = fp_limiter_next(&p, T + 6000000ull, 60);
    check_u64("recap after off is a first frame", again, T + 6000000ull);
    check_u64("recap interval is the new cap", p.interval, 1000000000ull / 60);

    /* After the slew lands, the grid is used again: present jitter is ignored. */
    memset(&p, 0, sizeof p);
    fp_limiter_next(&p, T, 100);
    p.last_present = T;
    now = T;
    for (int i = 0; i < 40 && p.interval != 1000000000ull / 60; i++) {
        unsigned long long dl = fp_limiter_next(&p, now + 8000000ull, 60);
        p.last_present = dl > now + 8000000ull ? dl : now + 8000000ull;
        now = p.last_present;
    }
    unsigned long long grid = p.deadline;
    p.last_present = grid + 80000ull;
    unsigned long long held = fp_limiter_next(&p, p.last_present + 8000000ull, 60);
    check_u64("grid resumes after slew", held, grid + p.interval);

    if (failures) {
        printf("%d failed\n", failures);
        return 1;
    }
    printf("all passed\n");
    return 0;
}
