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


def main():
    sr, x = render(0, 60, 2.0, 2, "arp", tempo=BPM, playing=1, hold="64,67", **PATCH)
    onsets = detect_onsets(x[:, 0], sr)
    if len(onsets) < 4:
        print(f"arp_gate: FAIL (only {len(onsets)} onsets detected)")
        sys.exit(1)

    intervals = np.diff(onsets[1:])   # ignore the first (step 1) as spec allows
    worst = float(np.max(np.abs(intervals - STEP_S)))
    print(f"onsets detected: {len(onsets)}  (first at {onsets[0]*1000:.1f} ms)")
    print(f"step interval: ideal {STEP_S*1000:.1f} ms, "
          f"measured mean {np.mean(intervals)*1000:.2f} ms, worst dev {worst*1000:.2f} ms")
    passed = worst <= TOL_S
    print(f"arp_gate: {'PASS' if passed else 'FAIL'} (tol +-{TOL_S*1000:.0f} ms)")
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()
