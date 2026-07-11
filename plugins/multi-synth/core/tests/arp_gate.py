#!/usr/bin/env python3
"""Arpeggiator timing gate.

120 BPM, 1/8 pattern (0.25 s/step), 3 held notes, Up. Detects note onsets and
checks the inter-onset intervals (after step 1) are on the grid within +-1 ms.
"""
import sys
import numpy as np
from _harness import render

BPM = 120.0
STEP_S = 60.0 / BPM * 0.5   # 1/8 note = 0.25 s
TOL_S = 0.001

PATCH = dict(osc1Wave=3, osc2Level=0, subLevel=0, noiseLevel=0, analogAmt=0,
             filterEnvAmt=0, filterCutoff=6000, filterRes=0, reverbOn=0, cosmosChorus=0,
             arpOn=1, arpMode=0, arpRate=3, arpGate=0.5, arpOctave=1,
             ampA=0.001, ampD=0.04, ampS=0.2, ampR=0.02)


def detect_onsets(sig, sr, thresh_frac=0.3, min_gap_s=0.1):
    # Peak envelope follower: instant attack, ~3 ms release. Detects rising
    # edges (note onsets) on the smoothed envelope, immune to the carrier's
    # per-cycle zero crossings.
    a = np.abs(sig)
    rel = np.exp(-1.0 / (0.003 * sr))
    env = np.empty_like(a)
    e = 0.0
    for i in range(len(a)):
        e = a[i] if a[i] > e else e * rel
        env[i] = e
    thr = thresh_frac * np.max(env)
    min_gap = int(min_gap_s * sr)
    onsets = []
    last = -min_gap
    for i in range(1, len(env)):
        if env[i] > thr and env[i - 1] <= thr and (i - last) >= min_gap:
            onsets.append(i)
            last = i
    return np.array(onsets) / sr


def free_run():
    sr, x = render(0, 60, 2.0, 2, "arp", tempo=BPM, playing=1, hold="64,67", **PATCH)
    onsets = detect_onsets(x[:, 0], sr)
    if len(onsets) < 4:
        print(f"[free]  FAIL (only {len(onsets)} onsets detected)")
        return False

    intervals = np.diff(onsets[1:])   # ignore the first (step 1) as spec allows
    worst = float(np.max(np.abs(intervals - STEP_S)))
    ok = worst <= TOL_S
    print(f"[free]  onsets {len(onsets)}  (first at {onsets[0]*1000:.1f} ms)  "
          f"ideal {STEP_S*1000:.1f} ms  worst dev {worst*1000:.2f} ms  "
          f"{'PASS' if ok else 'FAIL'} (tol +-{TOL_S*1000:.0f} ms)")
    return ok


def host_locked():
    # Host phase-lock: song position 0.31 beats at frame 0, note pressed at t=0,
    # 120 BPM 1/8. Strict quantize -> nothing sounds until the next 1/8 grid
    # boundary of the SONG position: beat 0.5 - 0.31 = 0.19 beats -> 95 ms. Every
    # later onset lands on the absolute host grid (250 ms apart).
    SONGPOS = 0.31
    FIRST_S = (0.5 - SONGPOS) * 60.0 / BPM   # 0.095 s
    FIRST_TOL_S = 0.002
    sr, x = render(0, 60, 2.0, 2, "arp_locked", tempo=BPM, playing=1,
                   songpos=SONGPOS, hold="64,67", **PATCH)
    onsets = detect_onsets(x[:, 0], sr)
    if len(onsets) < 4:
        print(f"[locked] FAIL (only {len(onsets)} onsets detected)")
        return False

    first_dev = abs(onsets[0] - FIRST_S)
    intervals = np.diff(onsets)
    worst = float(np.max(np.abs(intervals - STEP_S)))
    ok = (first_dev <= FIRST_TOL_S) and (worst <= TOL_S)
    print(f"[locked] onsets {len(onsets)}  first {onsets[0]*1000:.1f} ms "
          f"(grid {FIRST_S*1000:.1f} ms, dev {first_dev*1000:.2f} ms, tol +-{FIRST_TOL_S*1000:.0f})  "
          f"step worst dev {worst*1000:.2f} ms  {'PASS' if ok else 'FAIL'} (tol +-{TOL_S*1000:.0f} ms)")
    return ok


def main():
    ok = free_run()
    ok = host_locked() and ok
    print(f"arp_gate: {'PASS' if ok else 'FAIL'}")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
