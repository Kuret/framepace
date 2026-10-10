#include "game.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "gate.h"
#include "util.h"

#define KEY_MAX 63

static void sanitise(char *s)
{
    size_t n = strlen(s);
    if (n > KEY_MAX)
        n = KEY_MAX;
    s[n] = '\0';
    for (size_t i = 0; i < n; i++)
        if (!isalnum((unsigned char)s[i]) && s[i] != '.' && s[i] != '_' && s[i] != '-')
            s[i] = '_';
    if (s[0] == '.')
        s[0] = '_';          /* never a hidden or relative file name */
    if (n == 0)
        strcpy(s, "unknown");
}

/* Purely numeric, nonzero id (a launcher's hash is not an id). */
static bool numeric_id(const char *v)
{
    if (!v || !v[0] || strlen(v) > 20)
        return false;
    bool nonzero = false;
    for (const char *p = v; *p; p++) {
        if (*p < '0' || *p > '9')
            return false;
        if (*p != '0')
            nonzero = true;
    }
    return nonzero;
}

static void base_name(char *dst, size_t dsz, const char *path)
{
    const char *b = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\')
            b = p + 1;
    size_t n = strlen(b);
    if (n >= dsz)
        n = dsz - 1;
    memcpy(dst, b, n);
    dst[n] = '\0';
}

void fp_game_key(char *out, size_t out_size)
{
    static const char *const ids[] = { "SteamAppId", "SteamGameId", "STEAM_COMPAT_APP_ID", NULL };
    char tmp[256] = "";
    for (int i = 0; ids[i]; i++) {
        const char *v = getenv(ids[i]);
        if (numeric_id(v)) {
            snprintf(tmp, sizeof tmp, "steam-%.20s", v);
            break;
        }
    }
    if (!tmp[0]) {
        if (fp_gate_exe_is_wine()) {
            char cmd[4096];
            size_t n = 0;
            FILE *f = fopen("/proc/self/cmdline", "r");
            if (f) {
                n = fread(cmd, 1, sizeof cmd - 1, f);
                fclose(f);
            }
            cmd[n] = '\0';            /* argv[0] ends at the first NUL */
            if (cmd[0])
                base_name(tmp, sizeof tmp, cmd);
        }
        if (!tmp[0])
            snprintf(tmp, sizeof tmp, "%s", fp_gate_exe_name());
    }
    sanitise(tmp);
    snprintf(out, out_size, "%s", tmp);
}

bool fp_conf_parse_line(char *line, char **key, char **value)
{
    char *hash = strchr(line, '#');
    if (hash)
        *hash = '\0';
    char *eq = strchr(line, '=');
    if (!eq)
        return false;
    *eq = '\0';
    char *k = line, *v = eq + 1;
    while (*k && isspace((unsigned char)*k))
        k++;
    size_t kn = strlen(k);
    while (kn && isspace((unsigned char)k[kn - 1]))
        k[--kn] = '\0';
    while (*v && isspace((unsigned char)*v))
        v++;
    size_t vn = strlen(v);
    while (vn && isspace((unsigned char)v[vn - 1]))
        v[--vn] = '\0';
    if (!kn)
        return false;
    for (char *p = k; *p; p++)
        *p = (char)tolower((unsigned char)*p);
    *key = k;
    *value = v;
    return true;
}

static bool truthy(const char *v)
{
    return !strcasecmp(v, "on") || !strcasecmp(v, "yes") || !strcasecmp(v, "true") ||
           !strcmp(v, "1");
}

static bool falsy(const char *v)
{
    return !strcasecmp(v, "off") || !strcasecmp(v, "no") || !strcasecmp(v, "false") ||
           !strcmp(v, "0");
}

static int parse_int(const char *v, int fallback)
{
    char *end;
    long n = strtol(v, &end, 10);
    if (end == v || *end || n < 0 || n > 100000)
        return fallback;
    return (int)n;
}

static void apply(struct fp_settings *s, const char *key, const char *val, bool global)
{
    if ((global && !strcmp(key, "default_mode")) || (!global && !strcmp(key, "mode"))) {
        if (!strcasecmp(val, "adaptive"))
            s->mode = FP_MODE_ADAPTIVE;
        else if (!strcasecmp(val, "manual"))
            s->mode = FP_MODE_MANUAL;
        else if (!strcasecmp(val, "off"))
            s->mode = FP_MODE_OFF;
    } else if ((global && !strcmp(key, "default_overlay")) || (!global && !strcmp(key, "overlay"))) {
        if (truthy(val))
            s->overlay = true;
        else if (falsy(val))
            s->overlay = false;
    } else if (global && !strcmp(key, "max_cap")) {
        s->max_cap = parse_int(val, s->max_cap);
    } else if (!global && !strcmp(key, "cap")) {
        s->cap = parse_int(val, s->cap);
    } else if (!global && !strcmp(key, "last_adaptive")) {
        s->last_adaptive = parse_int(val, s->last_adaptive);
    }
}

static void load_file(const char *path, struct fp_settings *s, bool global)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[512];
    int lines = 0;
    while (lines++ < 200 && fgets(line, sizeof line, f)) {
        char *k, *v;
        if (fp_conf_parse_line(line, &k, &v))
            apply(s, k, v, global);
    }
    fclose(f);
}

void fp_settings_load(const char *key, struct fp_settings *s)
{
    s->mode = FP_MODE_ADAPTIVE;
    s->cap = 0;
    s->overlay = true;
    s->last_adaptive = 0;
    s->max_cap = 0;
    const char *home = getenv("HOME");
    if (!home || !home[0])
        return;
    char path[4096 + 128];
    snprintf(path, sizeof path, "%s/.config/framepace/config", home);
    load_file(path, s, true);
    snprintf(path, sizeof path, "%s/.config/framepace/games/%s.conf", home, key);
    load_file(path, s, false);
}
