#!/usr/bin/env python3
"""Prism op-4 feedback stability gate.

Sweep op-4 self-feedback from 0 to 1 (algo 1, op4 the top serial modulator).
Every render must be finite (no NaN/Inf) and bounded (peak <= a sane ceiling).
Feedback deepens the modulator's spectrum but, thanks to the 2-sample-average
damping, must never blow up.
"""
import sys
import numpy as np
from _fm import render, has_nan_inf

NOTE = 57
PEAK_CEIL = 4.0            # carrier is a bounded sine * level; well under this

PATCH = dict(
    op1Ratio=1, op1Level=1.0, op1A=0.002, op1D=1.0, op1S=1.0,
    op2Ratio=1, op2Level=0.0, op3Level=0, op4Level=0,
)


def main():
    ok = True
    print("feedback sweep (algo1, op4 self-feedback):")
    for fb in (0.0, 0.25, 0.5, 0.75, 1.0):
        # Route op4 as the serial top so its feedback reaches the carrier.
        sr, x = render(1, NOTE, 0.5, f"fb_{fb}",
                       op2Ratio=1, op2Level=0.8, op3Ratio=1, op3Level=0.8,
                       op4Ratio=1, op4Level=0.8, op4A=0.002, op4D=1.0, op4S=1.0,
                       op1Ratio=1, op1Level=1.0, op1A=0.002, op1D=1.0, op1S=1.0,
                       fb=fb)
        bad = has_nan_inf(x)
        peak = float(np.max(np.abs(x))) if len(x) else 0.0
        row_ok = (not bad) and peak <= PEAK_CEIL
        ok = ok and row_ok
        print(f"  fb={fb:4.2f}: peak {peak:6.3f}  nan/inf={bad}  -> {'ok' if row_ok else 'BAD'}")
    print(f"feedback_gate: {'PASS' if ok else 'FAIL'}")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
