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
    # Symmetric swing (matching the Arpeggiator convention): the EVEN step slot
    # lengthens by (1 + swing*0.5) = 1.25x and the ODD step shortens by
    # (1 - swing*0.5) = 0.75x, so onset interval[k] = duration of step k:
    #   even k -> 1.25*STEP_S (156.25 ms),  odd k -> 0.75*STEP_S (93.75 ms).
    # Each pair sums to 2*STEP_S (250 ms), keeping downbeats on the grid.
    sr, xs = render("seq", "seq_swing", swing=0.5, **PATCH)
    ons = detect_onsets(xs, sr)
    if len(ons) < 6:
        print(f"seq_gate: FAIL (swing: only {len(ons)} onsets)")
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

    print(f"seq_gate: {'PASS' if ok else 'FAIL'}")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
