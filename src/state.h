#pragma once
/*
 * Per-process live state file: the runtime control channel between the layer
 * and external tools, one file per active game process at
 * $HOME/.local/state/framepace/run/<pid>.state. Little-endian, version 2.
 * See README.md.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define FP_STATE_MAGIC   0x43415046u   /* 'F','P','A','C' */
#define FP_STATE_VERSION 2u
#define FP_GAME_LEN      64
#define FP_RING_SIZE     256

#define FP_FLAG_OVERLAY  (1u << 0)
#define FP_FLAG_ADAPTIVE (1u << 1)
#define FP_FLAG_MANUAL   (1u << 2)

struct fp_frame {
    uint64_t present_ns;   /* CLOCK_MONOTONIC when handed to the driver */
    uint32_t frame_ns;     /* interval since previous present */
    uint32_t work_ns;      /* previous present returned -> this present called */
};

struct fp_state {
    uint32_t magic;
    uint32_t version;
    int32_t  cap;          /* fps cap, 0 = unlimited (external tools write) */
    uint32_t flags;        /* FP_FLAG_* (external tools write) */
    uint64_t seq;          /* total frames presented (layer writes, after ring) */
    uint32_t head;         /* index of the most recent ring entry (layer writes) */
    uint32_t pid;          /* pid of the process writing frames (layer writes) */
    uint64_t last_present_ns;       /* CLOCK_MONOTONIC of the latest present (layer writes) */
    char     game[FP_GAME_LEN];     /* game key, NUL-terminated (written once at creation) */
    struct fp_frame ring[FP_RING_SIZE];
};

_Static_assert(sizeof(struct fp_frame) == 16, "fp_frame layout");
_Static_assert(offsetof(struct fp_state, seq) == 16, "fp_state layout");
_Static_assert(offsetof(struct fp_state, last_present_ns) == 32, "fp_state layout");
_Static_assert(offsetof(struct fp_state, game) == 40, "fp_state layout");
_Static_assert(offsetof(struct fp_state, ring) == 104, "fp_state layout");
_Static_assert(sizeof(struct fp_state) == 104 + 16 * FP_RING_SIZE, "fp_state layout");

/* Mapped state of this process, or NULL when there is none (the layer is
 * then a pure passthrough). */
extern struct fp_state *g_fp_state;

/* Create this process's run file (first call) and map it. Reference counted:
 * call once per device created while the layer is active. */
bool fp_state_acquire(void);
/* Drop one reference; at zero the file is unmapped and deleted. */
void fp_state_release(void);

/* Relaxed reads of the control fields. */
int32_t  fp_state_cap(void);
uint32_t fp_state_flags(void);

/* Append one frame to the ring (release stores: entry, head, seq). */
void fp_state_record(uint64_t present_ns, uint32_t frame_ns, uint32_t work_ns);
