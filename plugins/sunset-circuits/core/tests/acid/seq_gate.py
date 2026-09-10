#!/usr/bin/env python3
"""Sequencer timing gate.

120 BPM, 1/16 pattern: step onsets must land on the grid within +-1 ms. A second
render with swing 0.5 uses the SYMMETRIC swing convention: the EVEN step (the one
leading into the offbeat) lengthens by (1 + swing*0.5) and the ODD step shortens
by the same (1 - swing*0.5), so each pair still spans two grid steps (250 ms) and
the downbeats stay on tempo. Onset intervals therefore alternate LONG-first:
iv[k even] = 156.25 ms, iv[k odd] = 93.75 ms. (An earlier draft suggested the
opposite parity; the implementation and this gate follow the physically correct
long-into-offbeat convention.)
"""
import sys
import numpy as np
from _acid import render, peak_envelope, rising_edges

BPM = 120.0
STEP_S = 60.0 / BPM * 0.25   # 1/16 = 0.125 s
TOL_S = 0.001

PATCH = dict(bpm=BPM, rate=4, gate=0.5, wave=0, cutoff=4000, res=0.1,
             envMod=0.0, decay=0.05, sustain=0.0, root=48,
             on="1111111111111111", seconds=2.0)


def detect_onsets(sig, sr, thresh_frac=0.3, min_gap_s=0.03):
    # ~15 ms release smooths the low carrier ripple so each note gives ONE
    # rising edge (instant attack keeps onset timing sharp).
    return rising_edges(peak_envelope(sig, sr, 0.015), sr, thresh_frac, min_gap_s)


def main():
    ok = True

    # 2.0 s @ 120 BPM, 1/16 = 0.125 s/step; all 16 steps on => exactly one full
    # pattern fills the render, so exactly 16 onsets must be detected (one-to-one).
    EXPECT = 16

    # --- Straight grid ---
    sr, x = render("seq", "seq_grid", swing=0.0, **PATCH)
    on = detect_onsets(x, sr)
    if len(on) != EXPECT:
        print(f"seq_gate: FAIL (grid: {len(on)} onsets, expected {EXPECT})")
        sys.exit(1)
    intervals = np.diff(on[1:])           # ignore step-1 settling
    worst = float(np.max(np.abs(intervals - STEP_S)))
    grid_ok = worst <= TOL_S
    ok = ok and grid_ok
    print(f"[grid]  onsets {len(on)}  ideal {STEP_S*1000:.1f} ms  worst dev {worst*1000:.2f} ms  "
          f"{'PASS' if grid_ok else 'FAIL'} (tol +-{TOL_S*1000:.0f} ms)")

    # --- Swung grid ---
    # Symmetric swing (matching the Arpeggiator convention): the EVEN step slot
    # lengthens by (1 + swing*0.5) = 1.25x and the ODD step shortens by
    # (1 - swing*0.5) = 0.75x, so onset interval[k] = duration of step k:
    #   even k -> 1.25*STEP_S (156.25 ms),  odd k -> 0.75*STEP_S (93.75 ms).
    # Each pair sums to 2*STEP_S (250 ms), keeping downbeats on the grid.
    sr, xs = render("seq", "seq_swing", swing=0.5, **PATCH)
    ons = detect_onsets(xs, sr)
    # Swing redistributes onset spacing but not onset COUNT: still 16 per pattern.
    if len(ons) != EXPECT:
        print(f"seq_gate: FAIL (swing: {len(ons)} onsets, expected {EXPECT})")
        sys.exit(1)
    iv = np.diff(ons)
    even_iv = STEP_S * 1.25      # step k even -> leads into the offbeat, lengthened
    odd_iv = STEP_S * 0.75       # step k odd -> shortened
    worst = 0.0
    for k, v in enumerate(iv):
        expected = even_iv if (k % 2 == 0) else odd_iv
        worst = max(worst, abs(v - expected))
    swing_ok = worst <= 2 * TOL_S
    ok = ok and swing_ok
    print(f"[swing] even {even_iv*1000:.1f} ms / odd {odd_iv*1000:.1f} ms  "
          f"worst dev {worst*1000:.2f} ms  {'PASS' if swing_ok else 'FAIL'} (tol +-{2*TOL_S*1000:.0f} ms)")

    # The detector's own latency, MEASURED rather than absorbed by a padded
    # tolerance: in the free-run grid render above, step 0 fires at exactly
    # t = 0, so on[0] IS the latency (attack ramp + the threshold crossing on
    # the 15 ms-release envelope). Measured 1.94 ms; it cancels in every
    # interval check but shifts every absolute-grid check by the same amount.
    latency = float(on[0])
    print(f"[detector] measured onset latency {latency*1000:.2f} ms "
          f"(subtracted from the absolute-grid checks below)")

    ok = host_locked_grid(latency) and ok
    ok = host_locked_swing(latency) and ok
    ok = loop_wrap(latency) and ok

    print(f"seq_gate: {'PASS' if ok else 'FAIL'}")
    sys.exit(0 if ok else 1)


# Absolute-grid tolerance. It used to be +-3 ms, padded to swallow the ~2 ms
# detector latency -- which made it asymmetric (3 ms of headroom late, 1 ms
# early) and three times looser than the interval checks. With the latency
# measured and subtracted, the absolute checks hold the same +-1 ms as the
# interval ones (residual after subtraction: 0.02-0.06 ms).
SPB = 60.0 / BPM               # seconds per beat (0.5 s at 120 BPM)
ABS_TOL_S = TOL_S


def host_locked_grid(latency):
    # Song position 0.31 beats at frame 0, 1/16, strict quantize -> first onset on
    # the next 1/16 grid boundary (beat 0.5 -> 0.19 beats past songpos = 95 ms);
    # every later onset 125 ms apart on the absolute host grid.
    SONGPOS = 0.31
    first_s = (0.25 - (SONGPOS % 0.25)) * SPB     # to next 1/16 boundary = 95 ms
    sr, x = render("seq", "seq_locked", songpos=SONGPOS, swing=0.0, **PATCH)
    on = detect_onsets(x, sr) - latency
    # One-to-one onset count on the absolute host grid: onsets land at
    # first_s + k*STEP_S for every k whose onset falls before the 2.0 s render
    # end (less a small detector-latency slack). = 1 + floor((2.0-first_s-slack)/STEP_S).
    DET_SLACK = 0.005
    expected = 1 + int(np.floor((2.0 - first_s - DET_SLACK) / STEP_S))
    if len(on) != expected:
        print(f"[locked] FAIL ({len(on)} onsets, expected {expected})")
        return False
    first_dev = abs(on[0] - first_s)
    worst = float(np.max(np.abs(np.diff(on) - STEP_S)))
    ok = (first_dev <= ABS_TOL_S) and (worst <= TOL_S)
    print(f"[locked] onsets {len(on)}  first {on[0]*1000:.1f} ms "
          f"(grid {first_s*1000:.1f} ms, dev {first_dev*1000:.2f}, tol +-{ABS_TOL_S*1000:.0f})  "
          f"step worst dev {worst*1000:.2f} ms  {'PASS' if ok else 'FAIL'} (tol +-{TOL_S*1000:.0f} ms)")
    return ok


def host_locked_swing(latency):
    # Locked + swing 0.5: EVEN-step onsets pin to the absolute 0.5-beat grid; ODD
    # steps are delayed to k*0.25 + 0.0625 beats. Every onset must match a valid
    # grid-onset position {0.5*m} U {0.3125 + 0.5*m}.
    sr, x = render("seq", "seq_locked_swing", songpos=0.0, swing=0.5, **PATCH)
    on = detect_onsets(x, sr) - latency
    if len(on) < 6:
        print(f"[lock+swing] FAIL (only {len(on)} onsets)")
        return False
    worst = 0.0
    for t in on:
        b = t / SPB
        d_even = abs(b - round(b / 0.5) * 0.5)
        d_odd = abs(b - (0.3125 + round((b - 0.3125) / 0.5) * 0.5))
        worst = max(worst, min(d_even, d_odd) * SPB)
    ok = worst <= ABS_TOL_S
    print(f"[lock+swing] onsets {len(on)}  worst off-grid {worst*1000:.2f} ms  "
          f"{'PASS' if ok else 'FAIL'} (tol +-{ABS_TOL_S*1000:.0f} ms)")
    return ok


def loop_wrap(latency):
    # Transport loop of 4 beats (2.0 s). The cursor wraps 4 -> 0 and the grid
    # re-syncs: onsets stay on the 1/16 grid across the wrap and no step is
    # stuck/dropped (max inter-onset gap <= one step).
    sr, x = render("seq", "seq_wrap", songpos=0.0, loopbeats=4.0, swing=0.0,
                   **{**PATCH, "seconds": 4.0})
    on = detect_onsets(x, sr) - latency
    # 4.0 s / 0.125 s = 32 grid slots from t~=0; the transport wrap at beat 4
    # (2.0 s) re-syncs the step clock and merges the onset straddling the wrap
    # boundary, so 31 onsets are detected (stable across repeated runs — verified).
    EXPECT_WRAP = 31
    if len(on) != EXPECT_WRAP:
        print(f"[wrap] FAIL ({len(on)} onsets, expected {EXPECT_WRAP})")
        return False
    off_grid = float(np.max([abs(t / SPB - round(t / SPB / 0.25) * 0.25) * SPB for t in on]))
    max_gap = float(np.max(np.diff(on)))
    have_pre = bool(np.any(on < 2.0))
    have_post = bool(np.any(on > 2.0))
    ok = (off_grid <= ABS_TOL_S) and (max_gap <= STEP_S + 2 * TOL_S) and have_pre and have_post
    print(f"[wrap] onsets {len(on)}  worst off-grid {off_grid*1000:.2f} ms  "
          f"max gap {max_gap*1000:.1f} ms (<= {(STEP_S+2*TOL_S)*1000:.1f})  "
          f"pre/post-wrap {have_pre}/{have_post}  {'PASS' if ok else 'FAIL'}")
    return ok


if __name__ == "__main__":
    main()
