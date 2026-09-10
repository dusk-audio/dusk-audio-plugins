#!/usr/bin/env python3
"""Slope gate — verifies the AcidFilter is an ~18 dB/oct (3-pole) lowpass.

White noise through the filter at a fixed 1 kHz cutoff, resonance 0; the fitted
magnitude rolloff between 2 kHz and 8 kHz must land in 15..21 dB/oct.
"""
import sys
from _acid import render, rolloff_slope_db_oct, has_nan_inf

# The fitted slope is SIGNED (negative for a lowpass rolloff). Require it to sit
# in the -21..-15 dB/oct band so a positive/flat slope (a broken filter) fails.
LO, HI = -21.0, -15.0


def main():
    # Long render averages the noise spectrum for a stable slope fit.
    sr, x = render("filter", "slope", src=0, cutoff=1000, res=0, drive=1.0, seconds=6.0)
    if has_nan_inf(x):
        print("slope_gate: FAIL (NaN/Inf)")
        sys.exit(1)
    slope = rolloff_slope_db_oct(x, sr, 2000.0, 8000.0)
    passed = LO <= slope <= HI
    print(f"fitted rolloff 2-8 kHz: {slope:+.2f} dB/oct  (target {LO:.0f}..{HI:.0f})")
    print(f"slope_gate: {'PASS' if passed else 'FAIL'}")
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()
