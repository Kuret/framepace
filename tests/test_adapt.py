"""Adaptive cap policy: creep, deadband, climb quanta, settle notch, pace hold.

Loads tools/fps-cap and checks the pure decisions. No daemon, no run files.
"""
import importlib.machinery
import importlib.util
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# The tool has no .py suffix, so the default spec finder will not load it.
loader = importlib.machinery.SourceFileLoader("fps_cap", os.path.join(ROOT, "tools", "fps-cap"))
spec = importlib.util.spec_from_loader(loader.name, loader)
fc = importlib.util.module_from_spec(spec)
loader.exec_module(fc)

# A fixed ceiling so cap_from_work does not query the monitor.
fc.MAX_CAP = 240
fc.MIN_CAP = 50
fc.MARGIN = 1.05

failures = 0


def check(what, got, want):
    global failures
    if got != want:
        print(f"FAIL {what}: got {got!r}, want {want!r}")
        failures += 1
    else:
        print(f"ok   {what}")


def test_cap_from_work():
    # 8 ms * 1.05 = 8.4 ms -> 119.05 fps, floored.
    check("work to fps floors", fc.cap_from_work(8.0), 119)
    check("no sample", fc.cap_from_work(None), None)
    check("zero work", fc.cap_from_work(0), None)
    check("beyond reach", fc.cap_from_work(float("inf")), fc.MIN_CAP)


def test_creep_when_windows_disagree():
    long_w, short_w = 13.5, 8.62          # ~70 fps and ~110 fps
    long_cap, short_cap = fc.cap_from_work(long_w), fc.cap_from_work(short_w)
    check("long tail around 70", long_cap, 70)
    check("short tail around 110", short_cap, 110)

    target, far = fc.combine_targets(long_w, short_w, 70)
    check("creep ceiling", target, min(short_cap, long_cap + fc.CREEP_MAX))
    check("creep is not the fast climb", far, False)
    check("creep gap would have been far", target - 70 >= fc.CLIMB_FAR_GAP, True)
    # A full second of the fast step is 10 fps of backlog; the slow rate keeps 1.
    check("creep tick is one fps", fc.climb_steps(int(1e9), fc.RISE_STEP, target - 70, far), 1)

    # Already at the ceiling: hold, don't keep walking toward the short window.
    target, far = fc.combine_targets(long_w, short_w, long_cap + fc.CREEP_MAX)
    check("creep stops at the ceiling", target, long_cap + fc.CREEP_MAX)
    check("ceiling is a hold", fc.cap_move(long_cap + fc.CREEP_MAX, target), "hold")

    # The long window is genuinely beyond MIN_CAP: do not creep up out of it.
    target, far = fc.combine_targets(float("inf"), 8.0, 80)
    check("beyond reach stays at MIN_CAP", target, fc.MIN_CAP)
    check("beyond reach is not a climb", far, False)


def test_agree_uses_the_worse_window():
    target, far = fc.combine_targets(8.0, 8.05, 80)
    worse = min(fc.cap_from_work(8.0), fc.cap_from_work(8.05))
    check("agree takes the lower cap", target, worse)
    check("agree can fast-climb", far, True)
    check("fast tick caps the backlog at 2",
          fc.climb_steps(int(1e9), fc.RISE_STEP_FAR, target - 80, far), fc.CLIMB_MAX_STEP_FAR)

    # Close to the target: slow rate, and a 1 s stall still only adds 1 fps.
    target, far = fc.combine_targets(8.0, 8.05, target - 3)
    check("close climb is slow", far, False)
    check("slow tick caps the backlog at 1",
          fc.climb_steps(int(1e9), fc.RISE_STEP, 10, far), fc.CLIMB_MAX_STEP)
    check("half a slow step adds nothing", fc.climb_steps(int(0.4e9), fc.RISE_STEP, 10, False), 0)
    check("no room", fc.climb_steps(int(1e9), fc.RISE_STEP_FAR, 0, True), 0)


def test_deadband():
    check("two fps under drops", fc.cap_move(100, 98), "drop")
    check("one fps under holds", fc.cap_move(100, 99), "hold")
    check("equal holds", fc.cap_move(100, 100), "hold")
    check("above climbs", fc.cap_move(100, 101), "climb")


def test_settle_notch():
    # Achieved ~92: 96% is 88, but the notch is only 10% (100 -> 90).
    check("near miss drops one notch", fc.settle_notch(100, int(92 * 0.96)), 90)
    # WoW login: cap 77, achieved ~50. Leave it alone.
    check("login screen is not a notch", fc.settle_notch(77, int(50 * 0.96)), None)
    # Achieved just under 85% of the cap.
    check("under 85% is left alone", fc.settle_notch(100, int(85 * 0.96)), None)
    check("already at MIN_CAP", fc.settle_notch(fc.MIN_CAP, 40), None)
    check("backup above the cap", fc.settle_notch(100, 105), None)
    check("no backup", fc.settle_notch(100, None), None)


def test_pacing_itself():
    check("work filling the cap interval", fc.pacing_itself(9.5, 10.0, 100), True)
    check("real headroom is not the pace", fc.pacing_itself(8.0, 10.0, 100), False)
    check("frame time off the cap", fc.pacing_itself(11.5, 12.0, 100), False)
    check("half a millisecond of grid slack still counts", fc.pacing_itself(9.6, 10.4, 100), True)
    check("uncapped", fc.pacing_itself(9.5, 10.0, 0), False)


if __name__ == "__main__":
    test_cap_from_work()
    test_creep_when_windows_disagree()
    test_agree_uses_the_worse_window()
    test_deadband()
    test_settle_notch()
    test_pacing_itself()
    if failures:
        print(f"{failures} failed")
        sys.exit(1)
    print("all passed")
