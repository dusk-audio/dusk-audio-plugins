#!/usr/bin/env python3
"""Prism per-operator envelope gate.

1. BRIGHTNESS CONTOUR. On an e-piano-style patch (algo 5 dual stack) the tine
   modulator has a fast decay while the carrier sustains. The audible result: the
   tone is bright at note onset and mellows as the modulator's phase-mod depth
   collapses. We measure the spectral centroid in an early window (~0.05 s) and a
   late window (~1.0 s); the centroid must fall by at least 30%, proving per-op
   envelopes shape brightness over time. Checked at 48 kHz AND at 192 kHz -- see
   below for why the second rate is not redundant.

2. RATE INDEPENDENCE. The operator ADSRs do NOT tick once per sample. They tick
   at a decimated control rate (FMEngine::kEnvControlRate) with the per-sample
   value linearly interpolated between control points, and the divisor is derived
   from the engine's rate so the control rate stays near 48 kHz: /1 at 48 kHz,
   /2 at 96 kHz, /4 at 192 kHz -- which is what the shipped 4x oversampled voice
   path runs at.

   The consequence for this suite: at its default 48 kHz the divisor is 1 and the
   decimation is INVISIBLE. Run only there, every gate in this directory would
   pass no matter what the decimation did at 4x. So the envelope's wall-clock
   time constants are measured at all three rates and required to agree. That is
   the direct guard on the one mistake the decimation makes easy -- ticking N
   times less often without also preparing the envelope at 1/N of the rate, which
   would stretch every attack, decay and release by a factor of N.
"""
import sys
import numpy as np
from _fm import render, spectral_centroid

NOTE = 60
DROP_MIN = 0.30            # centroid must fall at least 30%

# --- rate-independence check ---------------------------------------------------
RATES = (48000, 96000, 192000)     # -> envelope divisor 1, 2, 4
RATE_ATTACK = 0.02                 # long enough to time to better than 0.2%
# 10-90% of the Exponential (p^2) curve is (sqrt(0.9) - sqrt(0.1)) * attack.
RATE_IDEAL_RISE = (np.sqrt(0.9) - np.sqrt(0.1)) * RATE_ATTACK
RATE_TOL = 0.02                    # measured spread across the three rates: 0.00%
RATE_IDEAL_TOL = 0.01              # measured error vs the ideal curve: -0.18%

# Algo 5 = (op2->op1) + (op4->op3). Tine stack: op2 high ratio, FAST decay to 0.
# Body stack: op4->op3 gentle. Carriers op1 (body) and op3 sustain.
PATCH = dict(
    op1Ratio=1,  op1Level=0.7, op1A=0.001, op1D=1.2, op1S=0.8, op1R=0.4,   # carrier body
    op2Ratio=14, op2Level=1.0, op2A=0.001, op2D=0.10, op2S=0.0, op2R=0.2,  # tine modulator, fast decay
    op3Ratio=1,  op3Level=0.4, op3A=0.001, op3D=1.2, op3S=0.7, op3R=0.4,   # carrier
    op4Ratio=1,  op4Level=0.4, op4A=0.001, op4D=0.6, op4S=0.2, op4R=0.3,   # body modulator
)


def window_centroid(sig, sr, t0, dur=0.05):
    a = int(t0 * sr)
    b = int((t0 + dur) * sr)
    return spectral_centroid(sig[a:b], sr)


def peak_envelope(sig, sr, f0):
    """Peak magnitude per carrier period — an amplitude envelope with no filter
    ringing of its own, so it cannot smear the rise time it is used to measure."""
    per = max(1, int(sr / f0))
    n = len(sig) // per
    env = np.maximum.reduceat(np.abs(sig[:n * per]), np.arange(0, n * per, per))
    t = (np.arange(n) + 1.0) * per / sr
    return t, env


def rise_10_90(t, env):
    """10-90% rise of a monotonic attack, interpolated inside the envelope step."""
    top = float(np.median(env[int(0.25 * len(env)):int(0.9 * len(env))]))
    if not np.isfinite(top) or top <= 0.0:
        return float("nan")

    def cross(level):
        for i in range(1, len(env)):
            if env[i - 1] < level <= env[i]:
                f = (level - env[i - 1]) / (env[i] - env[i - 1] + 1e-30)
                return t[i - 1] + f * (t[i] - t[i - 1])
        return None

    a10, a90 = cross(0.1 * top), cross(0.9 * top)
    return (a90 - a10) if (a10 and a90) else float("nan")


def rate_independence():
    """Operator-envelope time constants must not depend on the engine's rate."""
    f0 = 440.0 * 2 ** ((69 - 69) / 12.0)
    rises = []
    for sr_hz in RATES:
        # Algo 7 (additive), one carrier, sustain 1: the amplitude envelope IS
        # the operator envelope, with nothing else in the measurement.
        sr, x = render(7, 69, 0.3, f"env_rate_{sr_hz}", sr=sr_hz,
                       op1Ratio=1, op1Level=1, op1A=RATE_ATTACK, op1D=2.0, op1S=1.0,
                       op2Level=0, op3Level=0, op4Level=0)
        t, env = peak_envelope(x, sr, f0)
        rises.append(rise_10_90(t, env))

    ref = rises[0]
    ok = np.isfinite(ref) and ref > 0.0
    print(f"operator-envelope rate independence "
          f"(attack {RATE_ATTACK*1000:.0f} ms, ideal 10-90 {RATE_IDEAL_RISE*1000:.3f} ms):")
    for sr_hz, r in zip(RATES, rises):
        div = max(1, int(sr_hz / 48000.0 + 0.5))
        if not np.isfinite(r) or r <= 0.0:
            print(f"   {sr_hz:7d} Hz (env /{div}): rise could not be measured -> FAIL")
            ok = False
            continue
        spread = abs(r - ref) / ref
        ideal_err = abs(r - RATE_IDEAL_RISE) / RATE_IDEAL_RISE
        row_ok = spread <= RATE_TOL and ideal_err <= RATE_IDEAL_TOL
        ok = ok and row_ok
        print(f"   {sr_hz:7d} Hz (env /{div}): rise {r*1000:.4f} ms  "
              f"vs 48 kHz {spread*100:+.2f}% (tol {RATE_TOL*100:.0f}%)  "
              f"vs ideal {ideal_err*100:+.2f}% (tol {RATE_IDEAL_TOL*100:.0f}%)  "
              f"-> {'ok' if row_ok else 'BAD'}")
    return ok


def brightness(sr_hz):
    sr, x = render(5, NOTE, 1.6, f"env_epiano_{sr_hz}", sr=sr_hz, **PATCH)
    early = window_centroid(x, sr, 0.03, 0.05)
    late = window_centroid(x, sr, 1.0, 0.10)
    drop = (early - late) / early if early > 0 else 0.0
    ok = drop >= DROP_MIN
    div = max(1, int(sr_hz / 48000.0 + 0.5))
    print(f"e-piano centroid @ {sr_hz} Hz (env /{div}): early {early:.0f} Hz -> "
          f"late {late:.0f} Hz  (drop {drop*100:.1f}%, need >={DROP_MIN*100:.0f}%)")
    return ok


def main():
    # 48 kHz is the historical case (divisor 1); 192 kHz is what the shipped 4x
    # voice path runs at, and the only one of the two that exercises decimation.
    ok = brightness(48000)
    ok = brightness(192000) and ok
    ok = rate_independence() and ok
    print(f"env_gate: {'PASS' if ok else 'FAIL'}")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
