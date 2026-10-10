#pragma once
#include <stdbool.h>

/*
 * Per-process activation. With FRAMEPACE=1 set globally the loader puts the
 * layer into every Vulkan app; this decides, once, whether this process is a
 * game. Inactive processes get no hooks on present/swapchain calls.
 */
bool fp_gate_active(void);
const char *fp_gate_reason(void);   /* why, for FRAMEPACE_DEBUG */

/* Process identity, valid after fp_gate_active(): the basename of
 * /proc/self/exe and whether it is a Wine loader. */
const char *fp_gate_exe_name(void);
bool fp_gate_exe_is_wine(void);

/* Name matching used for the deny lists (exposed for tests): the name matches
 * when it equals a comma-separated entry, or starts with it when the entry ends
 * in '*'. */
bool fp_gate_list_matches(const char *name, const char *list);
bool fp_gate_builtin_denied(const char *name);
