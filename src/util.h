#pragma once
#include <stdint.h>
#include <stdbool.h>

uint64_t fp_now_ns(void);                 /* CLOCK_MONOTONIC */
bool fp_debug_enabled(void);              /* FRAMEPACE_DEBUG=1 */
void fp_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
