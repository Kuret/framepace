#pragma once
#include <stdbool.h>
#include <stddef.h>

#define FP_MODE_ADAPTIVE 0
#define FP_MODE_MANUAL   1
#define FP_MODE_OFF      2

struct fp_settings {
    int mode;            /* FP_MODE_* */
    int cap;             /* manual cap (0 = none) */
    bool overlay;
    int last_adaptive;   /* last cap the adaptive controller settled on, 0 = unknown */
    int max_cap;         /* MAX_CAP from the global config, 0 = auto */
};

/* Game key: "steam-<id>" for a numeric, nonzero Steam id, else the executable
 * name (Wine: basename of argv[0], native: basename of /proc/self/exe),
 * sanitised to [A-Za-z0-9._-], at most 63 characters. */
void fp_game_key(char *out, size_t out_size);

/* Defaults, then ~/.config/framepace/config (DEFAULT_MODE, DEFAULT_OVERLAY,
 * MAX_CAP), then ~/.config/framepace/games/<key>.conf (mode, cap, overlay,
 * last_adaptive). Unknown keys and bad lines are ignored; never fails. */
void fp_settings_load(const char *key, struct fp_settings *s);

/* Parse one "key = value" line (used by tests). Returns false for blank,
 * comment and malformed lines. key is lower-cased. */
bool fp_conf_parse_line(char *line, char **key, char **value);
