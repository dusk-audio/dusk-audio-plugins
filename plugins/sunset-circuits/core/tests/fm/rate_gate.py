#!/usr/bin/env python3
"""Prism operator envelope: Decay 2 and Rate Scale (issue #400).

Both follow the DX27 / DX100, measured on chipsynth OPMZ (tools repo,
opmz-parity/REVIEW.md, "Second decay and key rate scaling"):

1. HOLD. Decay 2 at its top (100 s) holds the sustain level, as Prism always
   did: the render is bit-identical to one that never sets Decay 2, and so is
   one a hair under the top (95 s, past the 92.9 s hold threshold).

2. DECAY 2 SLOPE, AND THE LONG GRID. From the sustain level the operator falls in a straight line
   in dB at 60 dB per Decay 2 time, the time snapped to the envelope's grid
   (48 steps from 1 ms to 10 s, continued to 86.3 s). On the DX the second
   decay runs on the first decay's rate table; Prism's Decay already reads as
   60 dB per its time, so the two agree by construction. Checked at 48 kHz and
   at 192 kHz (the 4x internal rate): the slope must not depend on the rate.
   Decay and Release run on the same long grid (the hardware's slowest first
   decay takes about 80 s for 60 dB, its slowest release about 40 s): a 40 s
   Decay and a 30 s Release fall at 60 dB per their grid time.

3. RATE SCALE AGAINST THE HARDWARE. The decay's speed on C1..C7 relative to C1,
   in the chip's rate steps (4 steps double the speed), against what OPMZ
   measured at Rate Scale 0, 1 and 2. Prism steps by 2^(1/4); the chip's own
   steps within a doubling run x1.25, x1.2, x1.17, x1.14, so single readings
   may sit up to 0.4 steps off; each doubling lands exactly.

4. MIDDLE C IS AS DISPLAYED. At C4 every Rate Scale renders bit-identically:
   the times on the panel hold there.

5. EVERY STAGE SCALES. The attack's 10-90 % rise and the release's fall run
   2^(steps/4) faster on C7 than on C4 at Rate Scale 2, as on the hardware,
   where rate scaling adds to every rate.

6. THE SLOWEST STAGES KEEP THEIR SPEED AT EVERY RATE. Rate Scale 3 on the
   lowest keys (C0, key code 0) runs every stage x0.074 of its displayed speed,
   so the grid's top, 86.3 s, takes 1162 s: per sample the level moves by 3e-8
   of itself at a 192 kHz internal rate, under float32's resolution near full
   level. Decay, Decay 2, Release (86.3 s) and Attack (10 s) must run at their
   grid time / 0.074 at 48, 96, 192 and 384 kHz (384 kHz is the internal rate
   of a 384 kHz session, which runs at 1x). A 20 s stretch of each gives the
   slope; the attack's is read through its curve, dB(pos) above.
"""
import math
import os
import sys

import numpy as np

from _fm import OUT, render

OK = True


def check(ok, msg):
    global OK
    OK = OK and ok
    print(f"  {'ok ' if ok else 'BAD'} {msg}")


def f0(note):
    return 440.0 * 2.0 ** ((note - 69) / 12.0)


def env_db(x, sr, note):
    per = max(1, int(round(sr / f0(note))))
    n = len(x) // per
    e = np.abs(x[:n * per]).reshape(n, per).max(axis=1)
    t = (np.arange(n) + 0.5) * per / sr
    return t, 20.0 * np.log10(np.maximum(e, 1e-12))


def slope(t, e, hi, lo):
    sel = np.flatnonzero((e <= hi) & (e >= lo))
    if len(sel) < 8:
        return float("nan")
    i0, i1 = sel[0], sel[-1]
    return float(np.polyfit(t[i0:i1 + 1], e[i0:i1 + 1], 1)[0])


def grid(t, top=58):
    lo, hi, steps = np.log2(0.001), np.log2(10.0), 47.0
    q = np.clip(np.round((np.log2(t) - lo) * steps / (hi - lo)), 0, top)
    return 2.0 ** (lo + q * (hi - lo) / steps)


def key_code(note):
    k = note - 13
    return 0 if k < 0 else min(31, 4 * (k // 12) + (k % 12) // 3)


# --- 1. Hold --------------------------------------------------------------------
print("Decay 2 at HOLD (bit-identical to no Decay 2):")
BASE = dict(op1A=0.001, op1D=0.05, op1S=0.8)
_, ref = render(7, 60, 0.8, "rate_hold_ref", **BASE)
for d2 in (100, 95):
    _, x = render(7, 60, 0.8, f"rate_hold_{d2}", op1D2=d2, **BASE)
    check(np.array_equal(ref, x), f"op1D2={d2}: identical")

# --- 2. Decay 2 slope -------------------------------------------------------------
print("Decay 2 slope, 60 dB per grid time, from the sustain level (op1S 0.8 = -9.03 dB):")
for sr_hz in (48000, 192000):
    for d2 in (0.5, 2.0, 8.0):
        secs = min(12.0, 0.2 + 0.75 * grid(d2))
        sr, x = render(7, 60, secs, f"rate_d2_{d2}_{sr_hz}", sr=sr_hz, op1D2=d2, **BASE)
        t, e = env_db(x, sr, 60)
        top = float(np.median(e[:3]))
        got = slope(t, e, top - 10.0, top - 40.0)
        want = -60.0 / grid(d2)
        err = abs(got / want - 1.0)
        check(err <= 0.02, f"{sr_hz:6d} Hz Decay 2 {d2:4.1f} s (grid {grid(d2):.3f} s): "
                           f"{got:8.2f} dB/s, want {want:8.2f} ({err * 100:.2f} %, tol 2 %)")

# --- 2b. Long Decay and Release -------------------------------------------------
# The hardware's slowest first decay falls 60 dB in about 80 s and its slowest
# release in about 40 s; Prism's Decay and Release run on the long grid to 86.3 s.
print("Long Decay and Release (the grid past 10 s):")
for stage, secs, params in (("Decay", 10.0, dict(op1A=0.001, op1D=40.0, op1S=0.0)),
                            ("Release", 10.0, dict(op1A=0.001, op1D=10.0, op1S=1.0, op1R=30.0, release=0.2))):
    t_set = params.get("op1R", params["op1D"])
    sr, x = render(7, 60, secs, f"rate_long_{stage}", **params)
    t, e = env_db(x, sr, 60)
    if stage == "Release":
        keep = t >= 0.21
        t, e = t[keep], e[keep]
    top = float(np.median(e[:3]))
    got = slope(t, e, top - 2.0, top - 12.0)
    want = -60.0 / grid(t_set)
    err = abs(got / want - 1.0)
    check(err <= 0.02, f"{stage} {t_set:.0f} s (grid {grid(t_set):.2f} s): {got:7.3f} dB/s, want {want:7.3f} "
                       f"({err * 100:.2f} %, tol 2 %)")

# --- 3. Rate Scale against OPMZ ------------------------------------------------
print("Rate Scale: decay speed relative to C1, in chip rate steps, Prism vs OPMZ:")
NOTES = (24, 36, 48, 60, 72, 84, 96)
OPMZ = {0: (0.0, 0.01, 1.29, 1.31, 2.36, 2.36, 3.26),
        1: (0.0, 1.29, 2.34, 3.25, 4.00, 5.31, 6.37),
        2: (0.0, 1.93, 3.98, 5.95, 8.01, 9.96, 11.99)}
for rs, dx in OPMZ.items():
    slopes = []
    for n in NOTES:
        sr, x = render(7, n, 6.0, f"rate_ks{rs}_{n}", op1A=0.001, op1D=0.5, op1S=0.0, op1RateScale=rs)
        t, e = env_db(x, sr, n)
        top = float(np.median(e[:3]))
        slopes.append(slope(t, e, top - 3.0, top - 40.0))
    steps = [4.0 * np.log2(s / slopes[0]) for s in slopes]
    worst = max(abs(a - b) for a, b in zip(steps, dx))
    check(worst <= 0.4, f"Rate Scale {rs}: Prism " + " ".join(f"{v:5.2f}" for v in steps)
          + f" | OPMZ " + " ".join(f"{v:5.2f}" for v in dx) + f" | worst {worst:.2f} steps (tol 0.4)")

# --- 4. Middle C ------------------------------------------------------------------
print("Middle C plays every stage as displayed, at every Rate Scale:")
_, c0 = render(7, 60, 0.8, "rate_mid_0", op1A=0.01, op1D=0.2, op1S=0.5, op1D2=1.0, release=0.6)
for rs in (1, 2, 3):
    _, c = render(7, 60, 0.8, f"rate_mid_{rs}", op1A=0.01, op1D=0.2, op1S=0.5, op1D2=1.0,
                  release=0.6, op1RateScale=rs)
    check(np.array_equal(c0, c), f"C4 at Rate Scale {rs} identical to Rate Scale 0")

# --- 5. Attack and release scale too -------------------------------------------------
print("Attack and release scale with the key (Rate Scale 2, C7 against C4):")
want = 2.0 ** (((key_code(96) >> 1) - (key_code(60) >> 1)) / 4.0)   # Rate Scale 2: key code >> 1


def rise(sr, x, note):
    t, e = env_db(x, sr, note)
    lin = 10.0 ** (e / 20.0)
    top = np.median(lin[len(lin) // 2:])
    a = t[np.argmax(lin >= 0.1 * top)]
    b = t[np.argmax(lin >= 0.9 * top)]
    return b - a


ATK = dict(op1A=0.2, op1D=10.0, op1S=1.0, op1RateScale=2)
r60 = rise(*render(7, 60, 0.6, "rate_atk60", **ATK), 60)
r96 = rise(*render(7, 96, 0.6, "rate_atk96", **ATK), 96)
check(abs((r60 / r96) / want - 1.0) <= 0.03, f"attack 10-90 %: C4 {r60 * 1000:.2f} ms, C7 {r96 * 1000:.2f} ms, "
                                             f"ratio {r60 / r96:.3f}, want {want:.3f} (tol 3 %)")
REL = dict(op1A=0.001, op1D=10.0, op1S=1.0, op1R=1.0, op1RateScale=2, release=0.2)
rel = []
for n in (60, 96):
    sr, x = render(7, n, 1.4, f"rate_rel{n}", **REL)
    t, e = env_db(x, sr, n)
    after = t >= 0.21
    rel.append(slope(t[after], e[after], e[after][0] - 3.0, e[after][0] - 40.0))
check(abs((rel[1] / rel[0]) / want - 1.0) <= 0.03, f"release slope: C4 {rel[0]:.1f} dB/s, C7 {rel[1]:.1f} dB/s, "
                                                   f"ratio {rel[1] / rel[0]:.3f}, want {want:.3f} (tol 3 %)")

# --- 6. The slowest stages, at every rate ---------------------------------------------
print("The slowest stages (Rate Scale 3 on C0, x0.074) at every internal rate:")
LOW = 12                                                    # C0, key code 0
mul = 2.0 ** ((key_code(LOW) - key_code(60)) / 4.0)          # Rate Scale 3: key code >> 0
K_END = math.exp(-4.3)                                       # the attack curve's end


def attack_pos(e):
    """Where on the attack curve (0 = -60 dB, 1 = full) the level e (dB) sits."""
    return -np.log(K_END + (-e / 60.0) * (1.0 - K_END)) / 4.3


def slow_check(ok, msg, name):
    """check(), then drop a passing render: these run to 30 MB (20 s at
    384 kHz), and only a failing one is worth keeping for a post-mortem."""
    check(ok, msg)
    if ok:
        os.remove(os.path.join(OUT, name + ".wav"))


SLOW = (("Decay", "d", dict(op1A=0.001, op1D=86.3, op1S=0.0)),
        ("Decay 2", "d2", dict(op1A=0.001, op1D=0.001, op1S=1.0, op1D2=86.3)),
        ("Release", "r", dict(op1A=0.001, op1D=10.0, op1S=1.0, op1R=86.3, release=0.2)))
for sr_hz in (48000, 96000, 192000, 384000):
    for stage, tag, params in SLOW:
        name = f"rate_slow_{tag}_{sr_hz}"
        sr, x = render(7, LOW, 20.0, name, sr=sr_hz, op1RateScale=3, **params)
        t, e = env_db(x, sr, LOW)
        keep = t >= 0.5
        got = float(np.polyfit(t[keep], e[keep], 1)[0])
        want = -60.0 / (grid(86.3) / mul)
        err = abs(got / want - 1.0)
        slow_check(err <= 0.02, f"{sr_hz:6d} Hz {stage:7s} 86.3 s (x{mul:.3f}: {grid(86.3) / mul:.0f} s): "
                                f"{got:8.4f} dB/s, want {want:8.4f} ({err * 100:.2f} %, tol 2 %)", name)
    name = f"rate_slow_a_{sr_hz}"
    sr, x = render(7, LOW, 14.0, name, sr=sr_hz, op1A=10.0, op1D=10.0, op1S=1.0, op1RateScale=3)
    t, e = env_db(x, sr, LOW)
    sel = (e >= -55.0) & (e <= -35.0)
    got = 1.0 / float(np.polyfit(t[sel], attack_pos(e[sel]), 1)[0]) if np.count_nonzero(sel) >= 8 else float("nan")
    want = grid(10.0) / mul
    err = abs(got / want - 1.0)
    slow_check(err <= 0.02, f"{sr_hz:6d} Hz Attack  10 s (x{mul:.3f}: {want:.0f} s): runs as {got:7.2f} s "
                            f"({err * 100:.2f} %, tol 2 %)", name)

print(f"rate_gate: {'PASS' if OK else 'FAIL'}")
sys.exit(0 if OK else 1)
