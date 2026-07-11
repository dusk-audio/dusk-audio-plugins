#!/usr/bin/env python3
"""Aliasing sanity report (no hard gate).

Renders a high saw (MIDI 108, ~4186 Hz) and reports the worst non-harmonic
spectral image below 15 kHz, in dB relative to the fundamental, at 1x/2x/4x.
Documents the anti-alias benefit of the halfband-decimated oversampling path.
"""
import numpy as np
from _harness import render

# Cosmos (gentlest filter) with a stable cutoff so the report reflects
# oscillator aliasing, not filter nonlinearity. At 1x a bright cutoff makes the
# OTA one-pole coefficient exceed 1 (see pitch_gate note), so the 1x row is
# filter-limited and shown only for context; the 4x row is the meaningful one.
NOTE = 108
PATCH = dict(osc1Wave=0, osc2Level=0, subLevel=0, osc3Level=0, noiseLevel=0,
             analogAmt=0, filterEnvAmt=0, filterCutoff=16000, filterRes=0,
             cosmosChorus=0, ampA=0.005, ampD=0.01, ampS=1.0, reverbOn=0)


def worst_image_db(sig, sr, f0):
    w = np.hanning(len(sig))
    X = np.abs(np.fft.rfft(sig * w))
    f = np.fft.rfftfreq(len(sig), 1.0 / sr)
    fund = X[np.argmin(np.abs(f - f0))]
    tol = f0 * 0.03
    worst = -200.0
    worst_f = 0.0
    lo = np.searchsorted(f, 50.0)
    hi = np.searchsorted(f, 15000.0)
    for k in range(lo, hi):
        # skip near integer harmonics of f0
        if abs(f[k] / f0 - round(f[k] / f0)) * f0 < tol:
            continue
        db = 20.0 * np.log10(X[k] / (fund + 1e-20) + 1e-20)
        if db > worst:
            worst, worst_f = db, f[k]
    return worst, worst_f


# Prism (mode 4): a bright serial-FM patch (algo 1: 4->3->2->1, all ops hot) has
# rich sidebands that alias hard if the internal rate is too low. This re-measures
# FM aliasing at the INTEGRATED oversampling path (voices render at host*os, then
# halfband-decimate), the number that matters for the shipped plugin.
FM_NOTE = 96  # C7 ~2093 Hz; a high-ratio modulator sprays sidebands past Nyquist
# Clean 2-op FM (algo 1 serial with op3/op4 silent -> op2 modulates op1 only):
# carrier ratio 1, modulator ratio 4. Sidebands at f0 +- k*4*f0 fold at 1x and
# are tamed by the halfband-decimated OS path (2x/4x drop >30 dB below 1x).
FM_PATCH = dict(prismAlgo=0, op1Ratio=1, op1Level=1, op2Ratio=4, op2Level=0.5,
                op3Level=0, op4Level=0, prismFB=0,
                op1S=1, op2S=1,
                filterEnvAmt=0, filterCutoff=5000, filterRes=0, ampA=0.005,
                ampD=0.01, ampS=1.0, reverbOn=0)


def main():
    f0 = 440.0 * 2 ** ((NOTE - 69) / 12.0)
    print(f"note {NOTE} saw, f0 = {f0:.1f} Hz")
    for os_ in (1, 2, 4):
        sr, x = render(0, NOTE, 1.0, os_, f"alias_{os_}", **PATCH)
        seg = x[int(0.3 * sr):int(0.9 * sr), 0]
        db, fhz = worst_image_db(seg, sr, f0)
        print(f"  {os_}x: worst image below 15 kHz = {db:6.1f} dBc  @ {fhz:8.1f} Hz")

    fm0 = 440.0 * 2 ** ((FM_NOTE - 69) / 12.0)
    print(f"\nPrism serial-FM (algo 1, ratios 1/3/5/7, fb 0.3), f0 = {fm0:.1f} Hz")
    print("  internal rate = 48k*os; FM engine renders there before decimation")
    for os_ in (1, 2, 4):
        sr, x = render(4, FM_NOTE, 1.0, os_, f"alias_fm_{os_}", **FM_PATCH)
        seg = x[int(0.3 * sr):int(0.9 * sr), 0]
        db, fhz = worst_image_db(seg, sr, fm0)
        print(f"  {os_}x (internal {48000*os_//1000}k): worst image below 15 kHz = {db:6.1f} dBc  @ {fhz:8.1f} Hz")
    print("\nalias_gate: report only (no pass/fail)")


if __name__ == "__main__":
    main()
