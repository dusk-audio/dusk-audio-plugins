#!/usr/bin/env python3
"""Freeverb sanity gate.

Renders a percussive note through the reverb and estimates the RT of the tail.
Checks: (1) no NaN/inf, (2) DC offset < -60 dB, (3) tail decay time grows with
the decay parameter (RT at decay=8 clearly longer than at decay=1). Sanity, not
lab-tight.
"""
import sys
import numpy as np
from _harness import render, rms_envelope, has_nan_inf, dc_db

PATCH = dict(osc1Wave=3, osc2Level=0, subLevel=0, noiseLevel=0, analogAmt=0,
             filterEnvAmt=0, filterCutoff=6000, filterRes=0, cosmosChorus=0,
             ampA=0.001, ampD=0.03, ampS=0.0, ampR=0.03,
             reverbOn=1, reverbMix=0.7, reverbSize=0.7, reverbDamp=0.2)


def rt60(sig, sr):
    t, env = rms_envelope(sig, sr, win_ms=10.0)
    db = 20.0 * np.log10(env + 1e-9)
    peak = np.argmax(db)
    # fit the decaying region from just after the peak down ~35 dB
    start = peak + 3
    seg_t, seg_db = t[start:], db[start:]
    top = seg_db[0]
    mask = seg_db > (top - 35.0)
    if np.count_nonzero(mask) < 5:
        return float("nan")
    A = np.polyfit(seg_t[mask], seg_db[mask], 1)
    slope = A[0]  # dB/s (negative)
    return (-60.0 / slope) if slope < -1e-6 else float("inf")


def main():
    sr, x1 = render(0, 60, 5.0, 2, "rev_d1", reverbDecay=1, **PATCH)
    sr, x8 = render(0, 60, 5.0, 2, "rev_d8", reverbDecay=8, **PATCH)

    # Check BOTH stereo channels; a failure in either channel fails the gate.
    # Decay-growth is evaluated per channel (both channels must lengthen).
    nch = min(x1.shape[1], x8.shape[1])
    nan_ok, dc_ok, grows = True, True, True
    for ch in range(nch):
        s1, s8 = x1[:, ch], x8[:, ch]
        ch_nan = not (has_nan_inf(s1) or has_nan_inf(s8))
        dc1, dc8 = dc_db(s1), dc_db(s8)
        ch_dc = dc1 < -60.0 and dc8 < -60.0
        r1, r8 = rt60(s1, sr), rt60(s8, sr)
        ch_grows = np.isfinite(r1) and np.isfinite(r8) and r8 > r1 * 1.2
        nan_ok = nan_ok and ch_nan
        dc_ok = dc_ok and ch_dc
        grows = grows and ch_grows
        print(f"[ch{ch}] RT60 decay=1 -> {r1:.2f}s, decay=8 -> {r8:.2f}s (grows: {ch_grows})  "
              f"DC {dc1:.1f}/{dc8:.1f} dB (<-60: {ch_dc})  finite: {ch_nan}")

    passed = nan_ok and dc_ok and grows
    print(f"reverb_gate: {'PASS' if passed else 'FAIL'}")
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()
