#!/usr/bin/env python3
"""Prism output frequency response against the four-operator hardware (issue #400).

The DX27 / DX100 Prism is modeled on has no filter and is flat across the band
(measured on chipsynth OPMZ, one carrier at full level, steady RMS against
middle C: -0.36 / -0.11 / -0.02 dB at C1 / C2 / C3, within 0.13 dB from C4 to
8.4 kHz). Prism carries a filter and output high-passes the hardware does not:

1. FLAT WITH THE FILTER FULLY OPEN. One sine carrier, filter at its top
   (20 kHz), no envelope. Each note's level against middle C must sit within
   0.25 dB of the hardware's. Before: -0.76 dB at C2 (two 20 Hz high-passes) and
   -2.13 dB at 8.4 kHz (four poles at 20 kHz).

2. NO DIP ON THE WAY OUT. From a 16 kHz cutoff to 20 kHz the filter eases out
   of the signal (TPTFourPole::processOpen). Sweeping it there, the 8.4 kHz
   level must only rise: a dry/wet crossfade over the whole cascade would dip
   about 4 dB mid-sweep, a stage-by-stage blend does not.

3. RESONANCE STAYS SANE IN THE OPEN ZONE. Full resonance with the cutoff at
   17, 18, 19 and 20 kHz: finite output, no louder than 1 dB over the same note
   without resonance (the feedback fades with the openness).
"""
import sys

import numpy as np

from _harness import render

PRISM = 4
OK = True
BASE = dict(prismAlgo=7, prismFB=0, analogAmt=0, vintage=0, noiseLevel=0, filterRes=0,
            filterEnvAmt=0, ampA=0.001, ampD=10, ampS=1, ampR=0.001, velSens=0, unisonVoices=1,
            chorusOn=0, delayOn=0, reverbOn=0, driveOn=0, masterVol=-18,
            op1Ratio=1, op1Level=1, op1Vel=0, op1KeyScale=0, op1A=0.001, op1D=10, op1S=1, op1R=0.001,
            op2Level=0, op3Level=0, op4Level=0)
DX = {24: -0.36, 36: -0.11, 48: -0.02, 60: 0.0, 96: -0.03, 108: -0.13, 120: -0.13}


def check(ok, msg):
    global OK
    OK = OK and ok
    print(f"  {'ok ' if ok else 'BAD'} {msg}")


def level(note, name, **kw):
    sr, x = render(PRISM, note, 1.0, 2, name, release=0.95, **dict(BASE, **kw))
    seg = x[int(0.25 * sr):int(0.85 * sr)].mean(axis=1)
    return 20.0 * np.log10(np.sqrt(np.mean(seg ** 2)) + 1e-30), float(np.max(np.abs(x))), bool(np.isfinite(x).all())


print("1. Flat with the filter fully open (against the hardware):")
ref, _, _ = level(60, "resp_60", filterCutoff=20000)
for n, dx in DX.items():
    v, _, _ = level(n, f"resp_{n}", filterCutoff=20000)
    d = v - ref
    check(abs(d - dx) <= 0.25, f"note {n:3d} ({440 * 2 ** ((n - 69) / 12):7.1f} Hz): {d:+6.2f} dB, hardware {dx:+6.2f}")

print("2. Sweeping out through the top of the range, 8.4 kHz only rises:")
row = [(fc, level(120, f"sweep_{fc}", filterCutoff=fc)[0]) for fc in (15000, 16000, 17000, 18000, 19000, 19500, 20000)]
steps = [b[1] - a[1] for a, b in zip(row, row[1:])]
check(all(s >= -0.01 for s in steps), "  ".join(f"{fc // 1000}.{fc % 1000 // 100}k {v - row[-1][1]:+.2f}" for fc, v in row))

print("3. Full resonance in the open zone stays sane:")
for fc in (17000, 18000, 19000, 20000):
    dry, _, _ = level(84, f"res0_{fc}", filterCutoff=fc)
    wet, peak, finite = level(84, f"res1_{fc}", filterCutoff=fc, filterRes=1.0)
    check(finite and wet - dry <= 1.0, f"cutoff {fc} Hz: resonance 1 {wet - dry:+.2f} dB over resonance 0, peak {peak:.3f}")

print(f"prism_response_gate: {'PASS' if OK else 'FAIL'}")
sys.exit(0 if OK else 1)
