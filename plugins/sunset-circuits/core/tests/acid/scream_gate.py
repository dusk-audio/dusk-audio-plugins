#!/usr/bin/env python3
"""Scream gate — resonance must scream near self-oscillation without blowing up.

Saw at 55 Hz through the filter at resonance 0.95 while the cutoff sweeps
200 -> 2000 Hz. Output must be finite (no NaN/Inf) and the peak bounded below
+6 dBFS (the per-stage tanh caps every state at +-1, so the loop cannot diverge).
"""
import sys
import numpy as np
from _acid import render, has_nan_inf

CEIL_DBFS = 6.0
# Calibrated lower bound so a SILENT render (peak ~ -inf dBFS) fails instead of
# passing the finite + ceiling checks. Measured peak on this patch is -11.11 dBFS;
# a floor ~19 dB below that (rounded) has ample margin above real output yet well
# above any silent/near-silent render.
MIN_DBFS = -30.0


def main():
    sr, x = render("filter", "scream", src=1, oscHz=55, res=0.95, drive=1.0,
                   sweep=1, cutlo=200, cuthi=2000, seconds=2.0)
    finite = not has_nan_inf(x)
    peak = float(np.max(np.abs(x)))
    peak_db = 20.0 * np.log10(peak + 1e-20)
    bounded = peak_db < CEIL_DBFS
    audible = peak_db > MIN_DBFS
    passed = finite and bounded and audible
    print(f"finite: {finite}   peak: {peak:.4f} ({peak_db:+.2f} dBFS)   "
          f"floor {MIN_DBFS:+.0f} / ceil {CEIL_DBFS:+.0f} dBFS")
    print(f"scream_gate: {'PASS' if passed else 'FAIL'}")
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()
