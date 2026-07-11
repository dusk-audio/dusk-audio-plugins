#!/usr/bin/env python3
"""Sequencer timing gate.

120 BPM, 1/16 pattern: step onsets must land on the grid within +-1 ms. A second
render with swing 0.5 must delay the odd steps by half the swing amount
(effStep *= 1 + swing*0.5), i.e. odd-step intervals grow and even-step intervals
shrink accordingly.
"""
import sys
import numpy as np
from _acid import render

BPM = 120.0
STEP_S = 60.0 / BPM * 0.25   # 1/16 = 0.125 s
TOL_S = 0.001

PATCH = dict(bpm=BPM, rate=4, gate=0.5, wave=0, cutoff=4000, res=0.1,
             envMod=0.0, decay=0.05, sustain=0.0, root=48,
             on="1111111111111111", seconds=2.0)


def detect_onsets(sig, sr, thresh_frac=0.3, min_gap_s=0.03):
    a = np.abs(sig)
    # ~15 ms release smooths the low carrier ripple so each note gives ONE
    # rising edge (instant attack keeps onset timing sharp).
    rel = np.exp(-1.0 / (0.015 * sr))
    env = np.empty_like(a)
    e = 0.0
    for i in range(len(a)):
        e = a[i] if a[i] > e else e * rel
        env[i] = e
    thr = thresh_frac * np.max(env)
    min_gap = int(min_gap_s * sr)
    onsets, last = [], -min_gap
    for i in range(1, len(env)):
        if env[i] > thr and env[i - 1] <= thr and (i - last) >= min_gap:
            onsets.append(i)
            last = i
    return np.array(onsets) / sr


def main():
    ok = True

    # --- Straight grid ---
    sr, x = render("seq", "seq_grid", swing=0.0, **PATCH)
    on = detect_onsets(x, sr)
    if len(on) < 6:
        print(f"seq_gate: FAIL (only {len(on)} onsets)")
        sys.exit(1)
    intervals = np.diff(on[1:])           # ignore step-1 settling
    worst = float(np.max(np.abs(intervals - STEP_S)))
    grid_ok = worst <= TOL_S
    ok = ok and grid_ok
    print(f"[grid]  onsets {len(on)}  ideal {STEP_S*1000:.1f} ms  worst dev {worst*1000:.2f} ms  "
          f"{'PASS' if grid_ok else 'FAIL'} (tol +-{TOL_S*1000:.0f} ms)")

    # --- Swung grid ---
    # Swing (matching the Arpeggiator convention) lengthens the ODD step slots by
    # (1 + swing*0.5) = 1.25x, so onset interval[k] = duration of step k:
    #   even k -> STEP_S (125 ms),  odd k -> 1.25*STEP_S (156.25 ms).
    sr, xs = render("seq", "seq_swing", swing=0.5, **PATCH)
    ons = detect_onsets(xs, sr)
    if len(ons) < 6:
        print(f"seq_gate: FAIL (swing: only {len(ons)} onsets)")
        sys.exit(1)
    iv = np.diff(ons)
    even_iv = STEP_S             # step k even
    odd_iv = STEP_S * 1.25       # step k odd
    worst = 0.0
    for k, v in enumerate(iv):
        expected = odd_iv if (k % 2 == 1) else even_iv
        worst = max(worst, abs(v - expected))
    swing_ok = worst <= 2 * TOL_S
    ok = ok and swing_ok
    print(f"[swing] even {even_iv*1000:.1f} ms / odd {odd_iv*1000:.1f} ms  "
          f"worst dev {worst*1000:.2f} ms  {'PASS' if swing_ok else 'FAIL'} (tol +-{2*TOL_S*1000:.0f} ms)")

    print(f"seq_gate: {'PASS' if ok else 'FAIL'}")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
