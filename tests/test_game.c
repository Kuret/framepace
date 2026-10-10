/* Tests for the game key and the saved-settings parser. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/game.h"
#include "../src/gate.h"

static int failures;

static void check_str(const char *what, const char *got, const char *want)
{
    if (strcmp(got, want)) {
        printf("FAIL %s: got '%s', want '%s'\n", what, got, want);
        failures++;
    } else {
        printf("ok   %s = '%s'\n", what, got);
    }
}

static void check_int(const char *what, long got, long want)
{
    if (got != want) {
        printf("FAIL %s: got %ld, want %ld\n", what, got, want);
        failures++;
    } else {
        printf("ok   %s = %ld\n", what, got);
    }
}

static void key_with(const char *what, const char *app, const char *game, const char *compat,
                     const char *want)
{
    char key[64];
    unsetenv("SteamAppId"); unsetenv("SteamGameId"); unsetenv("STEAM_COMPAT_APP_ID");
    if (app) setenv("SteamAppId", app, 1);
    if (game) setenv("SteamGameId", game, 1);
    if (compat) setenv("STEAM_COMPAT_APP_ID", compat, 1);
    fp_game_key(key, sizeof key);
    check_str(what, key, want);
}

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (f) { fputs(text, f); fclose(f); }
}

int main(void)
{
    key_with("SteamAppId", "123", NULL, NULL, "steam-123");
    key_with("SteamGameId", NULL, "456", NULL, "steam-456");
    key_with("STEAM_COMPAT_APP_ID", NULL, NULL, "789", "steam-789");
    key_with("AppId wins", "1", "2", "3", "steam-1");
    key_with("zero ignored", "0", NULL, NULL, "test_game");
    key_with("umu hash ignored", NULL, NULL, "umu-abc123", "test_game");
    key_with("zero then game id", "0", "55", NULL, "steam-55");
    key_with("not purely numeric", "12a", NULL, NULL, "test_game");

    /* deny matching: exact entries, trailing-* prefixes, FRAMEPACE_DENY lists */
    check_int("deny gamescope", fp_gate_builtin_denied("gamescope"), 1);
    check_int("deny gamescopereaper", fp_gate_builtin_denied("gamescopereaper"), 1);
    check_int("deny steam", fp_gate_builtin_denied("steam"), 1);
    check_int("steamx not denied (exact entry)", fp_gate_builtin_denied("steamx"), 0);
    check_int("deny python3.14", fp_gate_builtin_denied("python3.14"), 1);
    check_int("deny python", fp_gate_builtin_denied("python"), 1);
    check_int("deny srt-launcher", fp_gate_builtin_denied("srt-logger"), 1);
    check_int("deny steam-runtime-launch-client (comm-truncated)",
              fp_gate_builtin_denied("steam-runtime-l"), 1);
    check_int("deny pressure-vessel-wrap", fp_gate_builtin_denied("pressure-vessel-wrap"), 1);
    check_int("deny pv-adverb", fp_gate_builtin_denied("pv-adverb"), 1);
    check_int("deny vulkaninfo", fp_gate_builtin_denied("vulkaninfo"), 1);
    check_int("vkcube allowed", fp_gate_builtin_denied("vkcube"), 0);
    check_int("Wow.exe allowed", fp_gate_builtin_denied("Wow.exe"), 0);
    check_int("pythonic game is denied by the prefix", fp_gate_builtin_denied("pythonista"), 1);
    check_int("py not denied", fp_gate_builtin_denied("py"), 0);
    check_int("pv alone not denied", fp_gate_builtin_denied("pv"), 0);
    check_int("user list exact", fp_gate_list_matches("foo", "bar, foo ,baz"), 1);
    check_int("user list miss", fp_gate_list_matches("foobar", "bar,foo"), 0);
    check_int("user list prefix", fp_gate_list_matches("foobar", "bar,foo*"), 1);
    check_int("user list empty", fp_gate_list_matches("foo", ""), 0);
    check_int("user list lone star", fp_gate_list_matches("anything", "*"), 1);
    check_int("long entry vs 15-char comm", fp_gate_list_matches("verylongprocess1", "verylongprocess1234"), 0);
    check_int("long entry matches truncated comm", fp_gate_list_matches("verylongprocess", "verylongprocess1234"), 1);

    char line[128], *k, *v;
    strcpy(line, "  Mode =  adaptive  # comment");
    check_int("parse ok", fp_conf_parse_line(line, &k, &v), 1);
    check_str("parse key lower-cased", k, "mode");
    check_str("parse value", v, "adaptive");
    strcpy(line, "# only a comment");
    check_int("comment line", fp_conf_parse_line(line, &k, &v), 0);
    strcpy(line, "no equals sign");
    check_int("bad line", fp_conf_parse_line(line, &k, &v), 0);
    strcpy(line, " = value without key");
    check_int("empty key", fp_conf_parse_line(line, &k, &v), 0);

    char dir[] = "/tmp/fp_test_game_XXXXXX";
    if (!mkdtemp(dir)) return 1;
    char path[256];
    setenv("HOME", dir, 1);
    snprintf(path, sizeof path, "%s/.config", dir); mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/.config/framepace", dir); mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/.config/framepace/games", dir); mkdir(path, 0755);

    struct fp_settings s;
    fp_settings_load("nothing", &s);
    check_int("default mode adaptive", s.mode, FP_MODE_ADAPTIVE);
    check_int("default overlay on", s.overlay, 1);
    check_int("default cap", s.cap, 0);

    snprintf(path, sizeof path, "%s/.config/framepace/config", dir);
    write_file(path, "MIN_CAP = 50\nDEFAULT_MODE = manual\nDEFAULT_OVERLAY = off\nMAX_CAP = 120\n");
    fp_settings_load("nothing", &s);
    check_int("global default mode", s.mode, FP_MODE_MANUAL);
    check_int("global default overlay", s.overlay, 0);
    check_int("global max cap", s.max_cap, 120);

    snprintf(path, sizeof path, "%s/.config/framepace/games/g.conf", dir);
    write_file(path, "mode = off\ncap = 72\noverlay = on\nlast_adaptive = 91\ngarbage\ncap = x\nunknown = 1\n");
    fp_settings_load("g", &s);
    check_int("game mode off", s.mode, FP_MODE_OFF);
    check_int("bad cap value ignored", s.cap, 72);
    check_int("game overlay on", s.overlay, 1);
    check_int("last_adaptive", s.last_adaptive, 91);

    snprintf(path, sizeof path, "%s/.config/framepace/games/g.conf", dir);
    unlink(path);
    snprintf(path, sizeof path, "%s/.config/framepace/games", dir); rmdir(path);
    snprintf(path, sizeof path, "%s/.config/framepace/config", dir); unlink(path);
    snprintf(path, sizeof path, "%s/.config/framepace", dir); rmdir(path);
    snprintf(path, sizeof path, "%s/.config", dir); rmdir(path);
    rmdir(dir);

    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures != 0;
}
