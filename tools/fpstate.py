#!/usr/bin/env python3
"""Low-level access to framepace run files (~/.local/state/framepace/run/<pid>.state, layout v2).

  fpstate.py [--pid N | --file PATH] get
  fpstate.py [...] set-cap N          (0 = unlimited)
  fpstate.py [...] overlay on|off
  fpstate.py [...] mode adaptive|manual|none   (display flags only)
  fpstate.py [...] frames [N]         (last N ring entries, default 20)
  fpstate.py [...] stress [CAPS] [TOGGLES]     (hammer cap/overlay while a game runs)
  fpstate.py list                     (run files)

Without --pid/--file the run file with the most recent present is used. This is a debugging
tool; use fps-cap for normal control (it also saves per-game settings).
"""
import glob
import mmap
import os
import random
import struct
import sys
import time

RUN_DIR = os.path.expanduser("~/.local/state/framepace/run")
MAGIC, VERSION = 0x43415046, 2
RING = 256
HDR = struct.Struct("<IIiIQIIQ64s")   # magic version cap flags seq head pid last_present_ns game
FRAME = struct.Struct("<QII")          # present_ns frame_ns work_ns
OFF_CAP, OFF_FLAGS, OFF_SEQ, OFF_HEAD = 8, 12, 16, 24
OFF_RING = HDR.size                    # 104
SIZE = OFF_RING + RING * FRAME.size
F_OVERLAY, F_ADAPTIVE, F_MANUAL = 1, 2, 4


def open_file(path):
    fd = os.open(path, os.O_RDWR)
    try:
        if os.fstat(fd).st_size < SIZE:
            return None
        m = mmap.mmap(fd, SIZE, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE)
    finally:
        os.close(fd)
    magic, version = struct.unpack_from("<II", m, 0)
    return m if (magic, version) == (MAGIC, VERSION) else None


def run_files():
    out = []
    for path in glob.glob(os.path.join(RUN_DIR, "*.state")):
        try:
            m = open_file(path)
        except OSError:
            continue
        if m:
            out.append((path, m))
    return out


def last_present(m):
    return struct.unpack_from("<Q", m, 32)[0]


def pick(pid=None, path=None):
    if path:
        m = open_file(path)
    else:
        runs = run_files()
        if pid:
            runs = [r for r in runs if os.path.basename(r[0]) == f"{pid}.state"]
        m = max(runs, key=lambda r: last_present(r[1]))[1] if runs else None
    if not m:
        sys.exit("no framepace run file found (is a game running with the layer active?)")
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
    magic, version, cap, flags, seq, head, pid, last_ns, game = HDR.unpack_from(m, 0)
    print(f"game={game.split(b'\0')[0].decode()} version={version} cap={cap} flags={flags:#x} "
          f"(overlay={bool(flags & F_OVERLAY)} adaptive={bool(flags & F_ADAPTIVE)} "
          f"manual={bool(flags & F_MANUAL)}) seq={seq} head={head} pid={pid} last_present_ns={last_ns}")


def cmd_frames(m, n):
    for present_ns, frame_ns, work_ns in entries(m, n):
        print(f"{present_ns:>16d} frame={frame_ns / 1e6:8.3f}ms work={work_ns / 1e6:8.3f}ms")


def cmd_stress(m, caps, toggles):
    seq0 = struct.unpack_from("<Q", m, OFF_SEQ)[0]
    for i in range(max(caps, toggles)):
        if i < caps:
            struct.pack_into("<i", m, OFF_CAP, random.randint(50, 144))
        if i < toggles:
            set_flag(m, F_OVERLAY, i % 2 == 0)
        time.sleep(0.005)
    set_flag(m, F_OVERLAY, True)
    time.sleep(0.2)
    seq1 = struct.unpack_from("<Q", m, OFF_SEQ)[0]
    print(f"stress done: seq {seq0} -> {seq1} ({seq1 - seq0} frames)")


def main(argv):
    args, pid, path = argv[1:], None, None
    while args and args[0] in ("--pid", "--file") and len(args) >= 2:
        if args[0] == "--pid":
            pid = int(args[1])
        else:
            path = args[1]
        args = args[2:]
    if not args:
        sys.exit(__doc__)
    c = args[0]
    if c == "list":
        for p, m in run_files():
            print(p, end="  ")
            cmd_get(m)
        return
    m = pick(pid, path)
    if c == "get":
        cmd_get(m)
    elif c == "set-cap" and len(args) == 2:
        struct.pack_into("<i", m, OFF_CAP, int(args[1]))
    elif c == "overlay" and len(args) == 2 and args[1] in ("on", "off"):
        set_flag(m, F_OVERLAY, args[1] == "on")
    elif c == "mode" and len(args) == 2 and args[1] in ("adaptive", "manual", "none"):
        set_flag(m, F_ADAPTIVE, args[1] == "adaptive")
        set_flag(m, F_MANUAL, args[1] == "manual")
    elif c == "frames":
        cmd_frames(m, int(args[1]) if len(args) > 1 else 20)
    elif c == "stress":
        cmd_stress(m, int(args[1]) if len(args) > 1 else 2000,
                   int(args[2]) if len(args) > 2 else 200)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
