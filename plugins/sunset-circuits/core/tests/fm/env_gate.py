#!/usr/bin/env python3
"""Prism per-operator envelope gate.

On an e-piano-style patch (algo 5 dual stack) the tine modulator has a fast decay
while the carrier sustains. The audible result: the tone is bright at note onset
and mellows as the modulator's phase-mod depth collapses. We measure the spectral
centroid in an early window (~0.05 s) and a late window (~1.0 s); the centroid
must fall by more than 30%, proving per-op envelopes shape brightness over time.
"""
import sys
import numpy as np
from _fm import render, spectral_centroid

NOTE = 60
DROP_MIN = 0.30            # centroid must fall at least 30%

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


def main():
    sr, x = render(5, NOTE, 1.6, "env_epiano", **PATCH)
    early = window_centroid(x, sr, 0.03, 0.05)
    late = window_centroid(x, sr, 1.0, 0.10)
    drop = (early - late) / early if early > 0 else 0.0
    ok = drop >= DROP_MIN
    print(f"e-piano centroid: early {early:.0f} Hz -> late {late:.0f} Hz  (drop {drop*100:.1f}%, need >={DROP_MIN*100:.0f}%)")
    print(f"env_gate: {'PASS' if ok else 'FAIL'}")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
