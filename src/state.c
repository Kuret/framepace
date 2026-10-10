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

#include "util.h"

struct fp_state *g_fp_state;

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static bool g_pid_written;

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

static void do_open(void)
{
    const char *home = getenv("HOME");
    if (!home || !home[0]) {
        fp_log("state file disabled: HOME not set");
        return;
    }
    char dir[4096], path[4200];
    snprintf(dir, sizeof dir, "%s/.local/state/framepace", home);
    snprintf(path, sizeof path, "%s/state", dir);
    if (mkdir_p(dir) != 0) {
        fp_log("state file disabled: mkdir %s: %s", dir, strerror(errno));
        return;
    }
    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) {
        fp_log("state file disabled: open %s: %s", path, strerror(errno));
        return;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 ||
        ((size_t)st.st_size < sizeof(struct fp_state) &&
         ftruncate(fd, (off_t)sizeof(struct fp_state)) != 0)) {
        fp_log("state file disabled: size %s: %s", path, strerror(errno));
        close(fd);
        return;
    }
    void *m = mmap(NULL, sizeof(struct fp_state), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (m == MAP_FAILED) {
        fp_log("state file disabled: mmap %s: %s", path, strerror(errno));
        return;
    }
    struct fp_state *s = m;
    if (__atomic_load_n(&s->magic, __ATOMIC_ACQUIRE) != FP_STATE_MAGIC ||
        __atomic_load_n(&s->version, __ATOMIC_RELAXED) != FP_STATE_VERSION) {
        memset(s, 0, sizeof *s);
        s->version = FP_STATE_VERSION;
        s->cap = 0;
        s->flags = FP_FLAG_OVERLAY;
        __atomic_store_n(&s->magic, FP_STATE_MAGIC, __ATOMIC_RELEASE);
        fp_log("state file initialised");
    }
    g_fp_state = s;
    fp_log("state file %s", path);
}

bool fp_state_open(void)
{
    pthread_once(&g_once, do_open);
    return g_fp_state != NULL;
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
    if (!g_pid_written) {
        __atomic_store_n(&s->pid, (uint32_t)getpid(), __ATOMIC_RELEASE);
        g_pid_written = true;
    }
    uint32_t head = __atomic_load_n(&s->head, __ATOMIC_RELAXED);
    uint32_t idx = (head + 1) % FP_RING_SIZE;
    s->ring[idx].present_ns = present_ns;
    s->ring[idx].frame_ns = frame_ns;
    s->ring[idx].work_ns = work_ns;
    __atomic_store_n(&s->head, idx, __ATOMIC_RELEASE);
    __atomic_store_n(&s->seq, __atomic_load_n(&s->seq, __ATOMIC_RELAXED) + 1, __ATOMIC_RELEASE);
}
