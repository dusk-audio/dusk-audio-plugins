#!/usr/bin/env python3
"""Oracle Poly-Mod law gate: the routes follow the laws measured on the reference.

Voice.hpp (ORACLE POLY-MOD LAWS) sets the Poly-Mod routes from measurements of
the instrument Oracle is modeled on (#303, 2026-11-17), with the Poly-Mod amount
0..1 read as its 0..100 knob. The parts of those laws a render can show cleanly
are checked here, on one Oracle voice with the filter wide open and the filter
envelope held at full (attack 1 ms, sustain 1):

  FEnv -> freq A   amount 0.25 = 25 steps x 34.7 cents = 867.5 cents, the same
                   at A3 and A5 (exponential). Pitch from a parabolic FFT peak.
  FEnv -> PW A     amount 0.04 from a 10 % pulse narrows it to 0.8 %: the level
                   drops by more than 10 dB (it used to widen, and rise).
  osc B -> freq A  amount 1 with oscillator B a slow full-scale square: the
                   upward excursion is 7.5 semitones (zero-crossing periods while
                   oscillator B is high; the low half is too short to read).

    measured                     law      previous laws
    FEnv -> freq A A3 / A5       867.5    +2514 / +1035 cents (linear, 720 Hz)
    FEnv -> PW, level change     -15.1    +0.5 dB (widened)
    osc B -> freq A, up          +745.9   +6883 cents (linear, 2880 Hz)
"""
import sys

import numpy as np

from _harness import render

OPEN = dict(osc2Level=0, noiseLevel=0, filterCutoff=20000, filterRes=0, filterEnvAmt=0,
            analogAmt=0, unisonVoices=1, ampA=0.005, ampS=1, sr=48000)
HELD = dict(OPEN, filtA=0.001, filtD=0.01, filtS=1, filtR=0.5)


def peak_hz(x, fs):
    X = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    k = int(np.argmax(X[5:])) + 5
    a, b, c = np.log(X[k - 1:k + 2])
    return (k + 0.5 * (a - c) / (a - 2 * b + c)) * fs / len(x)


def tail(x, fs):
    return x[int(0.5 * fs):, 0]


def main():
    rows = []
    for note in (57, 81):
        fs, on = render(1, note, 1.5, 2, f"pm_fenv_{note}", osc1Wave=3, pmFenvOscA=0.25, **HELD)
        _, off = render(1, note, 1.5, 2, f"pm_fenv0_{note}", osc1Wave=3, **HELD)
        c = 1200.0 * np.log2(peak_hz(tail(on, fs), fs) / peak_hz(tail(off, fs), fs))
        rows.append((f"FEnv -> freq A, amount 0.25, note {note}", c, "cents", abs(c - 867.5) <= 10.0))
    fs, on = render(1, 69, 1.5, 2, "pm_pw_on", osc1Wave=4, osc1PW=0.10, pmFenvPWM=0.04, **HELD)
    _, off = render(1, 69, 1.5, 2, "pm_pw_off", osc1Wave=4, osc1PW=0.10, **HELD)
    rms = lambda y: 20.0 * np.log10(np.sqrt(np.mean(tail(y, fs) ** 2)) + 1e-20)
    d = rms(on) - rms(off)
    rows.append(("FEnv -> PW A, amount 0.04 from 10 %", d, "dB", d < -10.0))
    fs, x = render(1, 33, 2.0, 2, "pm_oscb", osc1Wave=3, osc2Wave=1, osc2Semi=-24,
                   osc2Detune=0, pmOscBOscA=1, **OPEN)
    y = x[int(0.3 * fs):, 0]
    zc = np.flatnonzero((y[:-1] < 0) & (y[1:] >= 0))
    t = zc + (-y[zc]) / (y[zc + 1] - y[zc])
    c = 1200.0 * np.log2(fs / np.diff(t) / 55.0)
    up = float(np.median(c[c > 0])) if np.any(c > 0) else float("nan")
    rows.append(("osc B -> freq A, amount 1, upward", up, "cents", abs(up - 750.0) <= 30.0))

    fails = 0
    for name, v, unit, ok in rows:
        fails += not ok
        print(f"  {name:<40} {v:+8.1f} {unit:<5} {'PASS' if ok else 'FAIL'}")
    print(f"polymod_law_gate: {'PASS' if not fails else f'FAIL ({fails} rows)'}")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
