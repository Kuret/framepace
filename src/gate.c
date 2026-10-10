#include "gate.h"

#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static bool g_active;
static char g_reason[192];
static char g_exe[256];
static bool g_exe_wine;

static bool env_true(const char *name)
{
    const char *v = getenv(name);
    return v && v[0] && strcmp(v, "0") != 0;
}

/* Match a process name against pattern[0..len): exact, or a prefix match when
 * the pattern ends in '*'. /proc/self/comm is truncated to 15 characters, so a
 * pattern longer than that also matches a 15-char comm that is its prefix. */
static bool name_matches(const char *name, const char *pattern, size_t len)
{
    size_t n = strlen(name);
    if (len == 0)
        return false;
    if (pattern[len - 1] == '*') {
        size_t p = len - 1;                 /* literal prefix, may be empty ("*") */
        if (n >= p && memcmp(name, pattern, p) == 0)
            return true;
        return n == 15 && p > 15 && memcmp(name, pattern, 15) == 0;
    }
    if (n == len && memcmp(name, pattern, len) == 0)
        return true;
    return n == 15 && len > 15 && memcmp(name, pattern, 15) == 0;
}

/* Comma-separated list of names/patterns. */
static bool in_list(const char *name, const char *list)
{
    while (list && *list) {
        const char *end = strchr(list, ',');
        size_t len = end ? (size_t)(end - list) : strlen(list);
        const char *start = list;
        while (len && start[0] == ' ') { start++; len--; }
        while (len && start[len - 1] == ' ')
            len--;
        if (name_matches(name, start, len))
            return true;
        if (!end)
            break;
        list = end + 1;
    }
    return false;
}

/* Processes that are never limited even with a game's environment: gamescope
 * (it limits itself), the Steam client, and launcher helpers that carry the
 * game's Steam/umu environment and may create a Vulkan device (umu's python,
 * Steam Runtime and pressure-vessel tools). A trailing '*' is a prefix match. */
static const char *const builtin_deny =
    "gamescope,gamescopereaper,steamwebhelper,steam,"
    "python*,srt-*,steam-runtime-*,pressure-vessel*,pv-*,vulkaninfo";

bool fp_gate_list_matches(const char *name, const char *list)
{
    return in_list(name, list);
}

bool fp_gate_builtin_denied(const char *name)
{
    return in_list(name, builtin_deny);
}

static void read_names(char *comm, size_t csz, char *exe, size_t esz)
{
    comm[0] = exe[0] = '\0';
    FILE *f = fopen("/proc/self/comm", "r");
    if (f) {
        if (fgets(comm, (int)csz, f))
            comm[strcspn(comm, "\n")] = '\0';
        fclose(f);
    }
    char path[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", path, sizeof path - 1);
    if (n > 0) {
        path[n] = '\0';
        const char *base = strrchr(path, '/');
        base = base ? base + 1 : path;
        size_t l = strlen(base);
        if (l >= esz)
            l = esz - 1;
        memcpy(exe, base, l);
        exe[l] = '\0';
    }
}

static void decide(void)
{
    static const char *const game_env[] = { "SteamGameId", "SteamAppId", "STEAM_COMPAT_APP_ID",
                                            "STEAM_COMPAT_DATA_PATH", "WINEPREFIX",
                                            "WINELOADERNOEXEC", "WINEDEBUG", "UMU_ID", NULL };
    static const char *const wine_names[] = { "wine64-preloader", "wine-preloader", "wine64",
                                              "wine", NULL };
    char comm[64], exe[256];
    read_names(comm, sizeof comm, exe, sizeof exe);
    memcpy(g_exe, exe, sizeof g_exe);
    for (int i = 0; wine_names[i]; i++)
        if (strcmp(exe, wine_names[i]) == 0)
            g_exe_wine = true;

    const char *fp = getenv("FRAMEPACE");
    if (fp && strcmp(fp, "0") == 0) {
        snprintf(g_reason, sizeof g_reason, "inactive: FRAMEPACE=0");
        return;
    }
    if (getenv("DISABLE_FRAMEPACE")) {
        snprintf(g_reason, sizeof g_reason, "inactive: DISABLE_FRAMEPACE is set");
        return;
    }
    if (fp_gate_builtin_denied(comm) || fp_gate_builtin_denied(exe)) {
        snprintf(g_reason, sizeof g_reason, "inactive: process '%.60s' is never limited (launcher or runtime helper)",
                 fp_gate_builtin_denied(comm) ? comm : exe);
        return;
    }
    const char *user = getenv("FRAMEPACE_DENY");
    if (user && (in_list(comm, user) || in_list(exe, user))) {
        snprintf(g_reason, sizeof g_reason, "inactive: process '%.60s' is in FRAMEPACE_DENY",
                 in_list(comm, user) ? comm : exe);
        return;
    }
    if (env_true("FRAMEPACE_FORCE")) {
        g_active = true;
        snprintf(g_reason, sizeof g_reason, "active: FRAMEPACE_FORCE=1");
        return;
    }
    for (int i = 0; game_env[i]; i++) {
        const char *v = getenv(game_env[i]);
        if (!v || !v[0])
            continue;
        /* a Steam id of 0 means "no game" (the Steam client itself) */
        if (strcmp(v, "0") == 0 && (!strcmp(game_env[i], "SteamAppId") ||
                                    !strcmp(game_env[i], "SteamGameId") ||
                                    !strcmp(game_env[i], "STEAM_COMPAT_APP_ID")))
            continue;
        g_active = true;
        snprintf(g_reason, sizeof g_reason, "active: %s is set", game_env[i]);
        return;
    }
    for (int i = 0; wine_names[i]; i++) {
        if (strcmp(exe, wine_names[i]) == 0 || strcmp(comm, wine_names[i]) == 0) {
            g_active = true;
            snprintf(g_reason, sizeof g_reason, "active: Wine process '%s'", wine_names[i]);
            return;
        }
    }
    snprintf(g_reason, sizeof g_reason,
             "inactive: '%.60s' does not look like a game (no Steam/Wine/Proton environment; "
             "set FRAMEPACE_FORCE=1 to force)", comm[0] ? comm : exe);
}

bool fp_gate_active(void)
{
    pthread_once(&g_once, decide);
    return g_active;
}

const char *fp_gate_reason(void)
{
    pthread_once(&g_once, decide);
    return g_reason;
}

const char *fp_gate_exe_name(void)
{
    pthread_once(&g_once, decide);
    return g_exe;
}

bool fp_gate_exe_is_wine(void)
{
    pthread_once(&g_once, decide);
    return g_exe_wine;
}
