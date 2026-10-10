#!/usr/bin/env python3
"""Read/write the framepace shared state file (~/.local/state/framepace/state).

  fpstate.py get
  fpstate.py set-cap N          (0 = unlimited)
  fpstate.py overlay on|off
  fpstate.py mode adaptive|manual|none   (display flags only)
  fpstate.py frames [N]         (last N ring entries, default 20)
  fpstate.py stress [CAPS] [TOGGLES]     (hammer cap/overlay while a game runs)
"""
import mmap
import os
import random
import struct
import sys
import time

PATH = os.path.expanduser("~/.local/state/framepace/state")
MAGIC = 0x43415046
RING = 256
HDR = struct.Struct("<IIiIQII")   # magic version cap flags seq head pid
FRAME = struct.Struct("<QII")      # present_ns frame_ns work_ns
OFF_CAP, OFF_FLAGS, OFF_SEQ, OFF_HEAD = 8, 12, 16, 24
OFF_RING = 32
SIZE = OFF_RING + RING * FRAME.size
F_OVERLAY, F_ADAPTIVE, F_MANUAL = 1, 2, 4


def open_state():
    if not os.path.exists(PATH):
        sys.exit(f"{PATH} does not exist (run a game with the layer first)")
    fd = os.open(PATH, os.O_RDWR)
    if os.fstat(fd).st_size < SIZE:
        sys.exit("state file too small")
    m = mmap.mmap(fd, SIZE, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE)
    os.close(fd)
    magic, version = struct.unpack_from("<II", m, 0)
    if magic != MAGIC or version != 1:
        sys.exit("bad magic/version (not initialised by the layer yet)")
    return m


def get_u32(m, off):
    return struct.unpack_from("<I", m, off)[0]


def put_u32(m, off, v):
    struct.pack_into("<I", m, off, v)


def set_flag(m, bit, on):
    f = get_u32(m, OFF_FLAGS)
    put_u32(m, OFF_FLAGS, (f | bit) if on else (f & ~bit))


def entries(m, n):
    head = get_u32(m, OFF_HEAD)
    seq = struct.unpack_from("<Q", m, OFF_SEQ)[0]
    n = min(n, RING, seq)
    out = []
    for i in range(n - 1, -1, -1):
        out.append(FRAME.unpack_from(m, OFF_RING + ((head - i) % RING) * FRAME.size))
    return out


def cmd_get(m):
    magic, version, cap, flags, seq, head, pid = HDR.unpack_from(m, 0)
    print(f"version={version} cap={cap} flags={flags:#x} "
          f"(overlay={bool(flags & F_OVERLAY)} adaptive={bool(flags & F_ADAPTIVE)} "
          f"manual={bool(flags & F_MANUAL)}) seq={seq} head={head} pid={pid}")


def cmd_frames(m, n):
    for present_ns, frame_ns, work_ns in entries(m, n):
        print(f"{present_ns:>16d} frame={frame_ns / 1e6:8.3f}ms work={work_ns / 1e6:8.3f}ms")


def cmd_stress(m, caps, toggles):
    seq0 = struct.unpack_from("<Q", m, OFF_SEQ)[0]
    for i in range(caps):
        struct.pack_into("<i", m, OFF_CAP, random.randint(50, 144))
        if i < toggles:
            set_flag(m, F_OVERLAY, i % 2 == 0)
        time.sleep(0.005)
    for i in range(caps, toggles):
        set_flag(m, F_OVERLAY, i % 2 == 0)
        time.sleep(0.005)
    set_flag(m, F_OVERLAY, True)
    time.sleep(0.2)
    seq1 = struct.unpack_from("<Q", m, OFF_SEQ)[0]
    print(f"stress done: seq {seq0} -> {seq1} ({seq1 - seq0} frames)")


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    m = open_state()
    c = argv[1]
    if c == "get":
        cmd_get(m)
    elif c == "set-cap" and len(argv) == 3:
        struct.pack_into("<i", m, OFF_CAP, int(argv[2]))
    elif c == "overlay" and len(argv) == 3 and argv[2] in ("on", "off"):
        set_flag(m, F_OVERLAY, argv[2] == "on")
    elif c == "mode" and len(argv) == 3 and argv[2] in ("adaptive", "manual", "none"):
        set_flag(m, F_ADAPTIVE, argv[2] == "adaptive")
        set_flag(m, F_MANUAL, argv[2] == "manual")
    elif c == "frames":
        cmd_frames(m, int(argv[2]) if len(argv) > 2 else 20)
    elif c == "stress":
        cmd_stress(m, int(argv[2]) if len(argv) > 2 else 2000,
                   int(argv[3]) if len(argv) > 3 else 200)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
