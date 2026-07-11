#!/usr/bin/env python3
"""Prism aliasing report (no hard gate).

The engine renders at whatever rate the voice hands it; standalone it has no
oversampling, so a bright high-note patch at 44.1 kHz is a worst case. We render
a bright bell patch at MIDI 96 and report the loudest non-harmonic image below
Nyquist, in dBc relative to the fundamental. Phase 3's oversampled voice rate is
what actually tames this; this report documents the naked-engine baseline so a
later regression is visible.
"""
import numpy as np
from _fm import render, spectrum

NOTE = 96
SR = 44100

# Bright serial patch (algo 1) — lots of high sidebands = plenty to alias.
PATCH = dict(
    op1Ratio=1, op1Level=1.0, op1A=0.002, op1D=1.0, op1S=1.0,
    op2Ratio=3, op2Level=1.0, op2A=0.002, op2D=1.0, op2S=1.0,
    op3Ratio=7, op3Level=0.9, op3A=0.002, op3D=1.0, op3S=1.0,
    op4Ratio=1, op4Level=0.0,
)


def worst_image_dbc(sig, sr, f0):
    f, X = spectrum(sig, sr)
    lo = np.searchsorted(f, 30.0)
    hi = np.searchsorted(f, sr * 0.49)
    fund_bin = np.argmin(np.abs(f - f0))
    fund = X[fund_bin]
    tol = f0 * 0.03
    worst, worst_f = -200.0, 0.0
    for k in range(lo, hi):
        # skip integer harmonics of f0 (legitimate partials); h>=1 guard keeps
        # sub-f0 bins (h==0) from being masked as "harmonics"
        h = round(f[k] / f0)
        if h >= 1 and abs(f[k] - h * f0) < tol:
            continue
        db = 20.0 * np.log10(X[k] / (fund + 1e-20) + 1e-20)
        if db > worst:
            worst, worst_f = db, f[k]
    return worst, worst_f


def main():
    f0 = 440.0 * 2 ** ((NOTE - 69) / 12.0)
    print(f"note {NOTE} bright bell, f0 = {f0:.1f} Hz @ {SR} Hz (no oversampling)")
    sr, x = render(1, NOTE, 1.0, "alias_hi", sr=SR, **PATCH)
    seg = x[int(0.2 * sr):int(0.9 * sr)]
    db, fhz = worst_image_dbc(seg, sr, f0)
    print(f"  worst non-harmonic image below Nyquist = {db:6.1f} dBc  @ {fhz:8.1f} Hz")
    print("alias_gate: report only (no pass/fail)")


if __name__ == "__main__":
    main()
