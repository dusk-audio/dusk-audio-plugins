#!/usr/bin/env python3
"""Accent gate — accented steps must be louder AND brighter.

Two renders of the SAME 1/16 pattern, one with every step accented, one with
none. Per accented step the level must rise >= 4 dB and the spectral centroid
must rise (brighter) versus the identical un-accented step.
"""
import sys
import numpy as np
from _acid import render, spectral_centroid

BPM = 120.0
STEP_S = 60.0 / BPM * 0.25   # 1/16 = 0.125 s
MIN_DB = 4.0

# Plucky patch with real filter env-mod so accent brightening is visible.
PATCH = dict(bpm=BPM, rate=4, gate=0.9, wave=0, cutoff=500, res=0.5,
             envMod=0.6, decay=0.12, sustain=0.0, accentAmt=1.0,
             root=36, on="1111111111111111", seconds=2.0)


def step_windows(sig, sr, n=8):
    """Return the first n per-step slices (from step 1 onward)."""
    out = []
    for k in range(n):
        a = int((k + 1) * STEP_S * sr)          # skip step 0 (settling)
        b = int((k + 2) * STEP_S * sr)
        if b <= len(sig):
            out.append(sig[a:b])
    return out


def main():
    sr, acc = render("seq", "accent_on", accents="1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1", **PATCH)
    _, pla = render("seq", "accent_off", accents="0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0", **PATCH)

    wa = step_windows(acc, sr)
    wp = step_windows(pla, sr)
    n = min(len(wa), len(wp))

    louder = []
    brighter = []
    for i in range(n):
        pa = np.max(np.abs(wa[i])) + 1e-20
        pp = np.max(np.abs(wp[i])) + 1e-20
        louder.append(20.0 * np.log10(pa / pp))
        ca = spectral_centroid(wa[i], sr)
        cp = spectral_centroid(wp[i], sr)
        brighter.append(ca - cp)

    louder = np.array(louder)
    brighter = np.array(brighter)
    lvl_ok = bool(np.all(louder >= MIN_DB))
    bright_ok = bool(np.all(brighter > 0.0))
    print(f"per-step level delta (dB): min {louder.min():+.2f} mean {louder.mean():+.2f}  (need >= {MIN_DB})")
    print(f"per-step centroid delta (Hz): min {brighter.min():+.1f} mean {brighter.mean():+.1f}  (need > 0)")
    passed = lvl_ok and bright_ok
    print(f"accent_gate: {'PASS' if passed else 'FAIL'}")
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()
