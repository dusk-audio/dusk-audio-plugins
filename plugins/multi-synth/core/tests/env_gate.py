#!/usr/bin/env python3
"""Amplitude-envelope timing gate.

Linear amp attack of 1.0 s -> measured 10-90% rise time must be within +-15%
of the ideal 0.8 s. Also reports the release 10-90% fall for a 0.5 s release.
Proves envelope time constants are correct at the internal (oversampled) rate.
"""
import sys
import numpy as np
from _harness import render, rms_envelope

ATTACK = 1.0
RELEASE = 0.5
IDEAL_RISE = 0.8            # 10..90% of a linear ramp over ATTACK
IDEAL_FALL = 0.4           # 10..90% of a linear ramp over RELEASE
TOL = 0.15

PATCH = dict(osc1Wave=3, osc2Level=0, subLevel=0, noiseLevel=0, analogAmt=0,
             filterEnvAmt=0, filterCutoff=6000, filterRes=0, reverbOn=0, cosmosChorus=0,
             ampCurve=0, ampA=ATTACK, ampD=0.01, ampS=1.0, ampR=RELEASE)


def crossing(t, env, level, rising, t0, t1):
    lo = np.searchsorted(t, t0)
    hi = np.searchsorted(t, t1)
    seg_t, seg_e = t[lo:hi], env[lo:hi]
    for i in range(1, len(seg_e)):
        if rising and seg_e[i - 1] < level <= seg_e[i]:
            return seg_t[i]
        if not rising and seg_e[i - 1] > level >= seg_e[i]:
            return seg_t[i]
    return None


def main():
    sr, x = render(0, 60, 3.0, 2, "env", release=2.0, **PATCH)
    sig = x[:, 0]
    t, env = rms_envelope(sig, sr, win_ms=4.0)

    plateau = np.median(env[(t > 1.1) & (t < 1.9)])  # sustain level
    t10 = crossing(t, env, 0.1 * plateau, True, 0.0, 1.5)
    t90 = crossing(t, env, 0.9 * plateau, True, 0.0, 1.5)
    rise = (t90 - t10) if (t10 and t90) else float("nan")

    f90 = crossing(t, env, 0.9 * plateau, False, 2.0, 3.0)
    f10 = crossing(t, env, 0.1 * plateau, False, 2.0, 3.0)
    fall = (f10 - f90) if (f10 and f90) else float("nan")

    err = abs(rise - IDEAL_RISE) / IDEAL_RISE
    passed = err <= TOL
    print(f"attack 10-90 rise: {rise:.3f}s  (ideal {IDEAL_RISE:.3f}s, err {err*100:+.1f}%, tol +-{TOL*100:.0f}%)")
    print(f"release 10-90 fall: {fall:.3f}s (ideal {IDEAL_FALL:.3f}s)")
    print(f"env_gate: {'PASS' if passed else 'FAIL'}")
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()
