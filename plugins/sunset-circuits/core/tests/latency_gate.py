#!/usr/bin/env python3
"""Latency gate: the voice path is delayed by the same amount at every oversampling factor.

The plugin reports 13 samples of latency at 1x, 2x and 4x, and pads the voice path
so that is true: 13 samples at 1x, where nothing else delays it, 2 at 2x and none
at 4x, where the decimator supplies the rest. Hosts compensate by the REPORTED
figure, so a pad that went wrong would put the voices out of time with every
other track while the report, and every check on it, stayed at 13.

This measures the delay itself. The same note (Mono, sharp attack, dry) is
rendered at each factor, and the 2x and 4x renders are cross-correlated with
the 1x one over the first 50 ms, with parabolic interpolation for the fraction
of a sample. The decimators' own delays are fractional (11.3 samples at 2x,
12.9 at 4x), so the factors agree to within 0.6 of a sample, not exactly; a pad
one sample off moves a lag by a whole sample.

    measured lag vs 1x     2x       4x
    padded (now)           +0.33    -0.04
    unpadded (1.0.8)      +11.32   +12.93
"""
import sys

import numpy as np

from _harness import render

MAX_LAG = 0.6
DRY = dict(reverbOn=0, delayOn=0, chorusOn=0, cosmosChorus=0, driveOn=0, vintage=0,
           analogAmt=0, ampA=0.001, ampD=0.3, ampS=0.6)


def xcorr_lag(a, b, n=2400):
    """Lag (samples) of b relative to a over the first n samples, sub-sample."""
    a, b = a[:n], b[:n]
    c = np.correlate(b, a, mode="full")
    k = int(np.argmax(c))
    if 0 < k < len(c) - 1:
        y0, y1, y2 = c[k - 1], c[k], c[k + 1]
        k = k + 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2)
    return k - (n - 1)


def main():
    x = {}
    for factor in (1, 2, 4):
        _, r = render(2, 57, 0.5, factor, f"latency_{factor}x", **DRY)
        x[factor] = r[:, 0]
    fails = []
    for factor in (2, 4):
        lag = xcorr_lag(x[1], x[factor])
        ok = abs(lag) <= MAX_LAG
        print(f"{factor}x vs 1x: {lag:+6.2f} samples (within +-{MAX_LAG})  "
              f"{'PASS' if ok else 'FAIL'}")
        if not ok:
            fails.append(f"{factor}x")
    print(f"latency_gate: {'PASS' if not fails else 'FAIL (' + ','.join(fails) + ')'}")
    sys.exit(0 if not fails else 1)


if __name__ == "__main__":
    main()
