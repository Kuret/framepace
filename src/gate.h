#pragma once
#include <stdbool.h>

/*
 * Per-process activation. With FRAMEPACE=1 set globally the loader puts the
 * layer into every Vulkan app; this decides, once, whether this process is a
 * game. Inactive processes get no hooks on present/swapchain calls.
 */
bool fp_gate_active(void);
const char *fp_gate_reason(void);   /* why, for FRAMEPACE_DEBUG */
