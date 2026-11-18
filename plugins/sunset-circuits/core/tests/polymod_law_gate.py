#!/usr/bin/env python3
"""Oracle Poly-Mod law gate: the routes follow the laws measured on the reference.

Voice.hpp (ORACLE POLY-MOD LAWS) sets the Poly-Mod routes from measurements of
the instrument Oracle is modeled on (#303, 2026-11-17), with the Poly-Mod amount
0..1 read as its 0..100 knob. The parts of those laws a render can show cleanly
are checked here, on one Oracle voice with the filter wide open and the filter
envelope held at full (attack 1 ms, sustain 1):

  FEnv -> freq A   compare A3 and A5 with the four-note reference mean at each
                   amount. Tolerance is the population SD across the four measured
                   notes or 10 cents, whichever is larger. Pitch from an FFT peak.
  FEnv -> PW A     amount 0.04 from a 10 % pulse narrows it to 0.8 %: the level
                   drops by more than 10 dB (it used to widen, and rise).
  osc B -> freq A  oscillator B a sine two octaves below a sine osc A (A6, at a
                   low level so the filter stays linear): the downward excursion
                   of osc A's instantaneous frequency (analytic signal, low-passed
                   at 4.5 kHz, in cents, fitted with oscillator B's own sine) at
                   amounts 0.25 and 1, against the
                   reference's own downward excursions at 25 and 100 (-180.4 and
                   -713.7 cents, review re-measurement), +-10 cents.
  osc B -> PW A    a 50 % pulse at A3, oscillator B a square two octaves below:
                   the duty of each cycle (crossings of the midpoint), 1st and
                   99th percentiles, half their spread, at amounts 0.10, 0.25 and
                   0.50, against the reference measured the same way (0.060,
                   0.146 and 0.292, review re-measurement), +-0.010.
  Cross Mod        not on Oracle's panel, but Poly Mod Bells and Glass Bells use
                   it for the old linear osc B -> freq A FM (2880 Hz x amount):
                   amount 0.01 with the same slow square moves 55 Hz up by
                   28.8 Hz (+-1.5). If Cross Mod were gated off in Oracle, those
                   two presets would lose their clang without any other test
                   noticing.

    measured                     law      previous laws
    FEnv -> PW, level change     -15.1    +0.5 dB (widened)
    osc B -> freq A, 0.25 / 1    -178.4 / -713.3   (7.5 cents per step: -187.1 / -748.7)
    osc B -> PW A, 0.1/0.25/0.5  0.060 / 0.149 / 0.296   (0.30 x amount: 0.032 / 0.078 / 0.151)
    Cross Mod 0.01, Hz up        +28.25   (unchanged law)
"""
import sys

import numpy as np
from scipy import signal

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
    # Reference on A2, A3, A4 and A5. Each row is (amount, mean cents, SD cents).
    anchors = ((0.10, 344.075, 10.1028152), (0.25, 841.175, 30.7638891),
               (0.50, 1703.900, 44.1645786), (1.00, 3387.700, 47.6723715))
    for note in (57, 81):
        fs, off = render(1, note, 1.5, 2, f"pm_fenv0_{note}", osc1Wave=3, **HELD)
        base = peak_hz(tail(off, fs), fs)
        for amount, mean, sd in anchors:
            _, on = render(1, note, 1.5, 2, f"pm_fenv_{note}_{amount}", osc1Wave=3,
                           pmFenvOscA=amount, **HELD)
            cents = 1200.0 * np.log2(peak_hz(tail(on, fs), fs) / base)
            rows.append((f"FEnv -> freq A, amount {amount:.2f}, note {note}", cents, "cents",
                         abs(cents - mean) <= max(sd, 10.0)))
    fs, on = render(1, 69, 1.5, 2, "pm_pw_on", osc1Wave=4, osc1PW=0.10, pmFenvPWM=0.04, **HELD)
    _, off = render(1, 69, 1.5, 2, "pm_pw_off", osc1Wave=4, osc1PW=0.10, **HELD)
    rms = lambda y: 20.0 * np.log10(np.sqrt(np.mean(tail(y, fs) ** 2)) + 1e-20)
    d = rms(on) - rms(off)
    rows.append(("FEnv -> PW A, amount 0.04 from 10 %", d, "dB", d < -10.0))
    for amount, ref in ((0.25, -180.4), (1.0, -713.7)):
        fs, x = render(1, 93, 1.5, 2, f"pm_oscb_{amount}", osc1Wave=3, osc1Level=0.05, osc2Wave=3,
                       osc2Semi=-24, osc2Detune=0, pmOscBOscA=amount, **OPEN)
        y = x[int(0.3 * fs):, 0]
        y = signal.sosfiltfilt(signal.butter(6, 4500.0, "low", fs=fs, output="sos"), y)
        y = signal.sosfiltfilt(signal.butter(2, 50.0, "high", fs=fs, output="sos"), y)
        inst = (np.gradient(np.unwrap(np.angle(signal.hilbert(y)))) * fs / (2.0 * np.pi))[400:-400]
        # The pitch in cents follows oscillator B, a 440 Hz sine: fit that sine (and
        # its phase) by least squares; its amplitude is the excursion each way.
        cents = 1200.0 * np.log2(np.maximum(inst, 1.0) / 1760.0)
        tt = np.arange(len(cents)) / fs
        basis = np.column_stack([np.sin(2 * np.pi * 440.0 * tt), np.cos(2 * np.pi * 440.0 * tt), np.ones_like(tt)])
        coef = np.linalg.lstsq(basis, cents, rcond=None)[0]
        down = -float(np.hypot(coef[0], coef[1]))
        rows.append((f"osc B -> freq A, amount {amount}, down", down, "cents", abs(down - ref) <= 10.0))
    for amount, ref in ((0.10, 0.060), (0.25, 0.146), (0.50, 0.292)):
        fs, x = render(1, 57, 2.0, 2, f"pm_oscbpw_{amount}", osc1Wave=4, osc1PW=0.5, osc1Level=0.3,
                       osc2Wave=1, osc2Semi=-24, osc2Detune=0, pmOscBPWM=amount, **OPEN)
        y = x[int(0.3 * fs):, 0]
        th = 0.5 * (np.percentile(y, 5) + np.percentile(y, 95))
        up = np.flatnonzero((y[:-1] < th) & (y[1:] >= th))
        dn = np.flatnonzero((y[:-1] >= th) & (y[1:] < th))
        duty = []
        for a, b in zip(up[:-1], up[1:]):
            k = dn[(dn > a) & (dn < b)]
            if len(k):
                duty.append((k[0] - a) / (b - a))
        lo, hi = np.percentile(duty, [1, 99])
        half = 0.5 * float(hi - lo)
        rows.append((f"osc B -> PW A, amount {amount}, half range", half, "", abs(half - ref) <= 0.010))
    fs, x = render(1, 33, 2.0, 2, "pm_xmod", osc1Wave=3, osc2Wave=1, osc2Semi=-24,
                   osc2Detune=0, crossMod=0.01, **OPEN)
    y = x[int(0.3 * fs):, 0]
    zc = np.flatnonzero((y[:-1] < 0) & (y[1:] >= 0))
    t = zc + (-y[zc]) / (y[zc + 1] - y[zc])
    hz = fs / np.diff(t)
    dev = float(np.median(hz[hz > 56.0])) - 55.0 if np.any(hz > 56.0) else float("nan")
    rows.append(("Cross Mod 0.01 in Oracle, Hz up", dev, "Hz", abs(dev - 28.8) <= 1.5))

    fails = 0
    for name, v, unit, ok in rows:
        fails += not ok
        print(f"  {name:<40} {v:+8.3f} {unit:<5} {'PASS' if ok else 'FAIL'}" if unit == "" else
              f"  {name:<40} {v:+8.1f} {unit:<5} {'PASS' if ok else 'FAIL'}")
    print(f"polymod_law_gate: {'PASS' if not fails else f'FAIL ({fails} rows)'}")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
