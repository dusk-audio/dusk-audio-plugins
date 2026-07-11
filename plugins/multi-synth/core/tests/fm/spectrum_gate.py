#!/usr/bin/env python3
"""Prism spectrum gate.

Part A — purity: algo 8 (all-parallel), a single carrier op at ratio 1.0 with no
modulators must be a clean sine: THD < 1%.

Part B — sidebands: algo 1 (serial) with carrier ratio 1 and a modulator (op2)
at a NON-INTEGER ratio (2.37) must show the classic FM sidebands at f_c +/- n*f_m.
Because the ratio is non-integer the sidebands are not harmonics of F0, so plain
distortion cannot fake them. At least 3 of the expected non-harmonic sideband bins
must dominate the spectrum (tight peak-search tolerance).
"""
import sys
import numpy as np
from _fm import render, spectrum, thd_pct

NOTE = 69                    # A440
F0 = 440.0
THD_LIMIT = 1.0             # percent


def part_a():
    # Additive algo, index 7 (valid range 0..7); only op1 carries (level 1), others silent.
    sr, x = render(7, NOTE, 1.0, "spec_sine",
                   op1Ratio=1, op1Level=1, op1A=0.002, op1D=0.5, op1S=1.0,
                   op2Level=0, op3Level=0, op4Level=0)
    seg = x[int(0.2 * sr):int(0.9 * sr)]
    thd = thd_pct(seg, sr, F0)
    ok = thd < THD_LIMIT
    print(f"[A] pure sine (additive algo 7, 1 carrier): THD = {thd:.3f}%  (limit {THD_LIMIT}%)  -> {'PASS' if ok else 'FAIL'}")
    return ok


def part_b():
    # Algo 1 serial: op2 -> op1. carrier ratio 1, modulator ratio 2.37 (NON-integer)
    # so the sidebands are not harmonics of F0 and cannot be faked by distortion.
    fm_ratio = 2.37
    sr, x = render(1, NOTE, 1.0, "spec_sidebands",
                   op1Ratio=1, op1Level=1, op1A=0.002, op1D=1.0, op1S=1.0,
                   op2Ratio=fm_ratio, op2Level=0.9, op2A=0.002, op2D=1.0, op2S=1.0,
                   op3Level=0, op4Level=0)
    seg = x[int(0.2 * sr):int(0.9 * sr)]
    f, X = spectrum(seg, sr)
    fm = F0 * fm_ratio

    # Expected sidebands |f_c +/- k*f_m|, k = -3..3; keep only bins above 20 Hz that
    # are NOT near an integer harmonic of F0 (|f/F0 - round(f/F0)| > 0.1).
    expected = [abs(F0 + k * fm) for k in range(-3, 4)]
    expected = sorted({round(e) for e in expected
                       if e > 20.0 and abs(e / F0 - round(e / F0)) > 0.1})

    def peak_near(fc):
        tol = fm * 0.05
        m = (f > fc - tol) & (f < fc + tol)
        return np.max(X[m]) if np.any(m) else 0.0

    peak = float(np.max(X))
    if not (np.isfinite(peak) and peak > 1e-9):
        print(f"[B] serial algo1 sidebands: spectrum is silent (peak={peak:.2e})  -> FAIL")
        return False

    thresh = 0.05 * peak          # 5% of the global peak = "dominant"
    hits = [(fc, peak_near(fc)) for fc in expected]
    n_dom = sum(1 for _, a in hits if a >= thresh)
    ok = n_dom >= 3

    print(f"[B] serial algo1 sidebands (f_c={F0:.0f}, f_m={fm:.0f}):")
    for fc, amp in hits:
        mark = "*" if amp >= thresh else " "
        print(f"      {mark} {fc:6.0f} Hz  amp {amp/peak:5.2f} of peak")
    print(f"    dominant sideband bins: {n_dom} (need >=3)  -> {'PASS' if ok else 'FAIL'}")
    return ok


def main():
    a = part_a()
    b = part_b()
    passed = a and b
    print(f"spectrum_gate: {'PASS' if passed else 'FAIL'}")
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()
