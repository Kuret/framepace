#!/usr/bin/env python3
"""Fake game for testing fps-cap deterministically, without a GPU.

Creates a framepace run file (layout v2) for itself and "presents" frames the way the layer
would: each frame takes max(cap interval, work time), the ring gets present_ns / frame_ns /
work_ns, and the cap is read back from the file every frame, so the daemon's changes act on it.

  fakegame.py --key steam-111 --phases 20:8,15:60,10:8 [--cap 0] [--mode adaptive|manual]
              [--trace FILE]

--phases is a comma-separated list of duration_seconds:work_ms (a 60 ms phase on a 120 fps cap is
a "loading screen": the game delivers fewer than half the cap). A phase may be spiky:
duration:work_ms/spike_ms@fraction, e.g. 8:5/30@0.2 is 8 s of 5 ms frames of which 20% take 30 ms. Run it with HOME pointing at a
scratch directory (and XDG_RUNTIME_DIR for the daemon's pid file). --trace appends
"seconds phase cap" lines four times a second.
"""
import argparse
import atexit
import os
import random
import struct
import time

MAGIC, VERSION, RING = 0x43415046, 2, 256
HEADER = struct.Struct("<IIiIQIIQ64s")
FRAME = struct.Struct("<QII")
SIZE = HEADER.size + RING * FRAME.size


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--key", default="fakegame")
    ap.add_argument("--phases", required=True)
    ap.add_argument("--cap", type=int, default=0)
    ap.add_argument("--mode", choices=("adaptive", "manual"), default="adaptive")
    ap.add_argument("--trace")
    args = ap.parse_args()
    phases = []
    for p in args.phases.split(","):
        dur, _, spec = p.partition(":")
        spec, _, spike = spec.partition("/")
        spike_ms, _, frac = spike.partition("@")
        phases.append((float(dur), float(spec), float(spike_ms or 0), float(frac or 0)))

    run_dir = os.path.expanduser("~/.local/state/framepace/run")
    os.makedirs(run_dir, exist_ok=True)
    path = os.path.join(run_dir, f"{os.getpid()}.state")
    fd = os.open(path, os.O_RDWR | os.O_CREAT | os.O_TRUNC, 0o644)
    os.ftruncate(fd, SIZE)
    import mmap
    m = mmap.mmap(fd, SIZE)
    os.close(fd)
    atexit.register(lambda: os.path.exists(path) and os.unlink(path))
    flags = 1 | (2 if args.mode == "adaptive" else 4)
    HEADER.pack_into(m, 0, MAGIC, VERSION, args.cap, flags, 0, 0, os.getpid(), 0,
                     args.key.encode())

    rnd = random.Random(1)
    seq = head = 0
    t0 = time.monotonic_ns()
    present = t0
    trace = open(args.trace, "w") if args.trace else None
    next_trace = 0.0
    for idx, (dur, work_ms, spike_ms, spike_frac) in enumerate(phases):
        end = time.monotonic_ns() + int(dur * 1e9)
        while time.monotonic_ns() < end:
            cap = struct.unpack_from("<i", m, 8)[0]
            ms = spike_ms if spike_frac and rnd.random() < spike_frac else work_ms
            work = ms * 1e6 * (1 + rnd.uniform(-0.03, 0.03))
            frame = max(1e9 / cap if cap > 0 else 0, work)
            present += int(frame)
            delay = (present - time.monotonic_ns()) / 1e9
            if delay > 0:
                time.sleep(delay)
            else:
                present = time.monotonic_ns()       # fell behind: resync like the layer
            head = (head + 1) % RING
            seq += 1
            FRAME.pack_into(m, HEADER.size + head * FRAME.size, present, int(frame), int(work))
            struct.pack_into("<I", m, 24, head)
            struct.pack_into("<Q", m, 16, seq)
            struct.pack_into("<Q", m, 32, present)
            if trace and (present - t0) / 1e9 >= next_trace:
                trace.write(f"{(present - t0) / 1e9:.2f} {idx} {cap}\n")
                trace.flush()
                next_trace += 0.25


if __name__ == "__main__":
    main()
