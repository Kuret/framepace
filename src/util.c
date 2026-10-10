#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

uint64_t fp_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

bool fp_debug_enabled(void)
{
    static int cached = -1;
    if (cached < 0) {
        const char *e = getenv("FRAMEPACE_DEBUG");
        cached = (e && e[0] && e[0] != '0') ? 1 : 0;
    }
    return cached == 1;
}

void fp_log(const char *fmt, ...)
{
    if (!fp_debug_enabled())
        return;
    char buf[512];
    int n = snprintf(buf, sizeof buf, "framepace: ");
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + n, sizeof buf - (size_t)n - 2, fmt, ap);
    va_end(ap);
    fprintf(stderr, "%s\n", buf);
}
