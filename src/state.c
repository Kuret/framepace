#include "state.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "game.h"
#include "util.h"

struct fp_state *g_fp_state;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_refs;
static bool g_atexit_registered;
static pid_t g_owner;           /* pid that created the file (a fork must not delete it) */
static char g_path[4200];

static int mkdir_p(char *path)
{
    for (char *p = path + 1; *p; p++) {
        if (*p != '/')
            continue;
        *p = 0;
        if (mkdir(path, 0755) != 0 && errno != EEXIST) {
            *p = '/';
            return -1;
        }
        *p = '/';
    }
    return (mkdir(path, 0755) != 0 && errno != EEXIST) ? -1 : 0;
}

static void remove_file(void)
{
    if (g_path[0] && getpid() == g_owner)
        unlink(g_path);
}

static void on_exit_cleanup(void)
{
    /* Games often exit without destroying their device. */
    remove_file();
}

/* Game key and saved settings (falling back to the global defaults) decide the
 * starting cap, mode and overlay, so a game starts as the user left it. */
static void init_from_settings(struct fp_state *s)
{
    char key[FP_GAME_LEN];
    struct fp_settings set;
    fp_game_key(key, sizeof key);
    fp_settings_load(key, &set);
    memcpy(s->game, key, sizeof s->game);
    s->flags = set.overlay ? FP_FLAG_OVERLAY : 0;
    switch (set.mode) {
    case FP_MODE_OFF:
        s->cap = 0;
        s->flags |= FP_FLAG_MANUAL;
        break;
    case FP_MODE_MANUAL:
        s->cap = set.cap;
        s->flags |= FP_FLAG_MANUAL;
        break;
    default:
        /* Without a remembered value and a fixed MAX_CAP the layer cannot know
         * the monitor's refresh rate: start uncapped and let the daemon set it. */
        s->cap = set.last_adaptive > 0 ? set.last_adaptive : set.max_cap;
        s->flags |= FP_FLAG_ADAPTIVE;
        break;
    }
    fp_log("game %s: mode %d, cap %d, overlay %d", key, set.mode, s->cap, set.overlay);
}

static void create_run_file(void)
{
    const char *home = getenv("HOME");
    if (!home || !home[0]) {
        fp_log("state disabled: HOME not set");
        return;
    }
    char dir[4096];
    snprintf(dir, sizeof dir, "%s/.local/state/framepace/run", home);
    snprintf(g_path, sizeof g_path, "%s/%d.state", dir, (int)getpid());
    if (mkdir_p(dir) != 0) {
        fp_log("state disabled: mkdir %s: %s", dir, strerror(errno));
        g_path[0] = 0;
        return;
    }
    int fd = open(g_path, O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        fp_log("state disabled: open %s: %s", g_path, strerror(errno));
        g_path[0] = 0;
        return;
    }
    if (ftruncate(fd, (off_t)sizeof(struct fp_state)) != 0) {
        fp_log("state disabled: size %s: %s", g_path, strerror(errno));
        close(fd);
        unlink(g_path);
        g_path[0] = 0;
        return;
    }
    void *m = mmap(NULL, sizeof(struct fp_state), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (m == MAP_FAILED) {
        fp_log("state disabled: mmap %s: %s", g_path, strerror(errno));
        unlink(g_path);
        g_path[0] = 0;
        return;
    }
    struct fp_state *s = m;
    memset(s, 0, sizeof *s);
    s->version = FP_STATE_VERSION;
    s->pid = (uint32_t)getpid();
    init_from_settings(s);
    g_owner = getpid();
    __atomic_store_n(&s->magic, FP_STATE_MAGIC, __ATOMIC_RELEASE);
    g_fp_state = s;
    if (!g_atexit_registered) {
        atexit(on_exit_cleanup);
        g_atexit_registered = true;
    }
    fp_log("state file %s", g_path);
}

bool fp_state_acquire(void)
{
    pthread_mutex_lock(&g_lock);
    if (g_refs == 0)
        create_run_file();
    bool ok = g_fp_state != NULL;
    if (ok)
        g_refs++;
    pthread_mutex_unlock(&g_lock);
    return ok;
}

void fp_state_release(void)
{
    pthread_mutex_lock(&g_lock);
    if (g_refs > 0 && --g_refs == 0 && g_fp_state) {
        struct fp_state *s = g_fp_state;
        g_fp_state = NULL;
        munmap(s, sizeof *s);
        remove_file();
        g_path[0] = 0;
    }
    pthread_mutex_unlock(&g_lock);
}

int32_t fp_state_cap(void)
{
    return g_fp_state ? __atomic_load_n(&g_fp_state->cap, __ATOMIC_RELAXED) : 0;
}

uint32_t fp_state_flags(void)
{
    return g_fp_state ? __atomic_load_n(&g_fp_state->flags, __ATOMIC_RELAXED) : 0;
}

void fp_state_record(uint64_t present_ns, uint32_t frame_ns, uint32_t work_ns)
{
    struct fp_state *s = g_fp_state;
    if (!s)
        return;
    uint32_t head = __atomic_load_n(&s->head, __ATOMIC_RELAXED);
    uint32_t idx = (head + 1) % FP_RING_SIZE;
    s->ring[idx].present_ns = present_ns;
    s->ring[idx].frame_ns = frame_ns;
    s->ring[idx].work_ns = work_ns;
    __atomic_store_n(&s->head, idx, __ATOMIC_RELEASE);
    __atomic_store_n(&s->seq, __atomic_load_n(&s->seq, __ATOMIC_RELAXED) + 1, __ATOMIC_RELEASE);
    __atomic_store_n(&s->last_present_ns, present_ns, __ATOMIC_RELEASE);
}
