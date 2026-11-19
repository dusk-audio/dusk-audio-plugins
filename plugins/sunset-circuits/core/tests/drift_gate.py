#!/usr/bin/env python3
"""Analog drift gate: the pitch wander is the same at every rate.

Analog (analogAmt) adds a slow random pitch wander to every poly voice
(Voice.hpp, ANALOG DRIFT). It used to be counted in internal samples, so it
kept its depth but moved twice as fast at 4x as at 2x and half as fast at 1x:
the oversampling switch changed the sound of every preset with Analog up. It
is now timed in seconds against the 48 kHz / 2x default it was voiced at.

One sine note (Oracle, osc 1 alone, filter wide open, no effects, one voice,
Analog 1) is rendered at 1x, 2x and 4x at 44.1 and 48 kHz. The note's
instantaneous pitch comes from complex demodulation at the nominal frequency;
the wander is summarised by its RMS in cents (how deep) and the spectral
centroid of the cents trace, 0.2 to 200 Hz (how fast). Every setting must sit
within SPEED_TOL of the 48 kHz / 2x reference in speed and within
DEPTH_TOL_DB in depth.

    measured (centroid Hz, 44.1k 1x/2x/4x | 48k 1x/2x/4x)
    rate-counted drift (1.1.0)   9.3 / 18.9 / 35.0 | 10.3 / 19.8 / 37.5   FAIL
    time-based drift             20.6 / 20.7 / 20.1 | 19.9 / 19.8 / 20.7   PASS
"""
import sys

import numpy as np

from _harness import render

NOTE, F0 = 69, 440.0
SECONDS = 20.0
SPEED_TOL = (0.8, 1.25)      # centroid ratio vs the 48 kHz / 2x reference
DEPTH_TOL_DB = 1.5           # RMS-cents ratio vs the reference, in dB
PATCH = dict(osc1Wave=3, osc1Level=1, osc2Level=0, noiseLevel=0, filterCutoff=20000,
             filterRes=0, filterEnvAmt=0, ampA=0.005, ampD=0.01, ampS=1.0, analogAmt=1,
             unisonVoices=1, vintage=0, reverbOn=0, delayOn=0, chorusOn=0, driveOn=0)


def wander(x, fs):
    """(RMS cents, centroid Hz) of the note's pitch wander."""
    n = np.arange(len(x))
    z = x * np.exp(-2j * np.pi * F0 * n / fs)
    Z = np.fft.fft(z)
    Z[np.abs(np.fft.fftfreq(len(z), 1.0 / fs)) > 200.0] = 0.0
    ph = np.unwrap(np.angle(np.fft.ifft(Z)))
    cents = 1200.0 * np.log2((F0 + np.gradient(ph) * fs / (2.0 * np.pi)) / F0)
    c = cents[::int(fs // 1000)]          # 1 kHz is plenty for a 200 Hz band
    c = c[1000:-1000]                     # drop the attack and the FFT edges
    c = c - c.mean()
    seg = 4096
    P = np.zeros(seg // 2 + 1)
    for s in range(0, len(c) - seg, seg // 2):
        P += np.abs(np.fft.rfft(c[s:s + seg] * np.hanning(seg))) ** 2
    fr = np.fft.rfftfreq(seg, 1e-3)
    band = (fr >= 0.2) & (fr <= 200.0)
    return float(np.sqrt(np.mean(c ** 2))), float(np.sum(fr[band] * P[band]) / np.sum(P[band]))


def main():
    res = {}
    for sr in (44100, 48000):
        for osf in (1, 2, 4):
            fs, x = render(1, NOTE, SECONDS, osf, f"drift_{sr}_{osf}", sr=sr, **PATCH)
            res[(sr, osf)] = wander(x[:, 0], fs)
    ref_rms, ref_cen = res[(48000, 2)]
    fails = 0
    print(f"{'rate':>6} {'OS':>3}  {'rms cents':>9}  {'centroid Hz':>11}  {'speed':>6}  {'depth dB':>8}")
    for (sr, osf), (rms, cen) in sorted(res.items()):
        speed = cen / ref_cen
        depth = 20.0 * np.log10(rms / ref_rms)
        ok = SPEED_TOL[0] <= speed <= SPEED_TOL[1] and abs(depth) <= DEPTH_TOL_DB
        fails += not ok
        print(f"{sr:>6} {osf:>2}x  {rms:9.3f}  {cen:11.2f}  {speed:6.2f}  {depth:+8.2f}  "
              f"{'PASS' if ok else 'FAIL'}")
    print(f"drift_gate: {'PASS' if not fails else f'FAIL ({fails} settings)'} "
          f"(speed within {SPEED_TOL[0]}..{SPEED_TOL[1]}x and depth within "
          f"+-{DEPTH_TOL_DB} dB of 48 kHz / 2x)")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
