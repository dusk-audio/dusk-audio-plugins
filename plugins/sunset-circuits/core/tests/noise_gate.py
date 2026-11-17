#!/usr/bin/env python3
"""Mixer noise gate: Oracle white at the measured level; no mode ducks its oscillators.

Oracle's mixer noise follows measurements of the instrument it is modeled on
(#303, 2026-11-17; Voice.hpp, ORACLE MIXER NOISE): white rather than pink, at a
level set against the same synth's own oscillator 1 saw (A3, filter open), the
same at every oversampling factor, and with no ducking of the oscillators when
it is turned up (the noise used to count in the mixer's level sum).

One Oracle voice, A3, filter wide open, no effects, master low enough that the
output limiter stays out of it, 48 kHz:
  white      octave bands 125 Hz .. 2 kHz rise 3 dB per octave (+-1)
  level      the 1 kHz octave band, re the saw's total energy: -22.3 dB (+-1.5)
  rate       the noise's own 1 kHz band at 1x, 2x and 4x within 1 dB
  no duck    noise at full lowers a single saw by no more than 0.5 dB

    measured                 now          before (pink, in the level sum)
    slope, dB per octave     +3.0         -0.0
    1 kHz band re saw, 2x    -22.2        -17.7
    1 kHz band spread        0.5          0.5
    ducking, one saw         -0.19..-0.20 -3.4..-3.9

The rate row passed before too (pink noise is not rate-scaled); it guards the
white source's density scaling, which a fixed per-sample level would break.

Mono and Modular keep their pink noise but, like Oracle, no longer count it in
the mixer's level sum (#303, 2026-11-17). Same voice and settings, the mode's
other sources (sub, osc 3) at 0:
  pink       octave bands 125 Hz .. 2 kHz flat (0 dB per octave, +-1)
  no duck    noise at full lowers a single saw by no more than 0.5 dB

    measured                 now            before (noise in the level sum)
    Mono slope, dB/oct       -0.0           -0.0
    Mono ducking             -0.34..-0.38   -2.55..-2.96
    Modular ducking          -0.34..-0.38   -2.78..-3.24

What is left of the ducking is the filter's input saturation reacting to the
noise's own power, not the mixer.
"""
import sys

import numpy as np

from _harness import render

F0 = 220.0
BASE = dict(sr=48000, osc1Wave=0, osc2Wave=0, osc2Detune=0, filterCutoff=20000, filterRes=0,
            filterEnvAmt=0, analogAmt=0, unisonVoices=1, vintage=0, masterVol=-24,
            ampA=0.005, ampD=0.01, ampS=1.0)
CENTRES = [125, 250, 500, 1000, 2000]


def welch(x, fs, n=16384):
    w = np.hanning(n)
    P = np.zeros(n // 2 + 1)
    k = 0
    for s in range(0, len(x) - n, n // 2):
        P += np.abs(np.fft.rfft(x[s:s + n] * w)) ** 2
        k += 1
    return np.fft.rfftfreq(n, 1 / fs), P / k


def render_tail(name, osf, **p):
    fs, x = render(1, 57, 3.0, osf, name, **dict(BASE, **p))
    return x[int(0.5 * fs):, 0], fs


def octave(f, P, c):
    return P[(f >= c / np.sqrt(2)) & (f < c * np.sqrt(2))].sum()


def main():
    rows, one_k = [], {}
    for osf in (1, 2, 4):
        noi, fs = render_tail(f"noise_n_{osf}", osf, osc1Level=0, osc2Level=0, noiseLevel=1)
        f, Pn = welch(noi, fs)
        one_k[osf] = 10 * np.log10(octave(f, Pn, 1000))
        saw0, _ = render_tail(f"noise_s0_{osf}", osf, osc1Level=1, osc2Level=0, noiseLevel=0)
        saw1, _ = render_tail(f"noise_s1_{osf}", osf, osc1Level=1, osc2Level=0, noiseLevel=1)
        _, P0 = welch(saw0, fs)
        _, P1 = welch(saw1, fs)
        hh = np.round(f / F0)
        harm = (hh >= 1) & (np.abs(f - hh * F0) < 3 * fs / 16384) & (f < 20000)
        duck = 10 * np.log10(P1[harm].sum() / P0[harm].sum())
        rows.append((f"no duck, one saw, {osf}x", duck, "dB", duck >= -0.5))
        if osf == 2:
            lv = [10 * np.log10(octave(f, Pn, c)) for c in CENTRES]
            slope = float(np.polyfit(np.log2(CENTRES), lv, 1)[0])
            rows.append(("white: slope 125 Hz .. 2 kHz", slope, "dB/oct", abs(slope - 3.0) <= 1.0))
            tot = P0[(f >= 20) & (f <= 20000)].sum()
            rel = 10 * np.log10(octave(f, Pn, 1000) / tot)
            rows.append(("level: 1 kHz band re own saw, 2x", rel, "dB", abs(rel + 22.3) <= 1.5))
    spread = max(one_k.values()) - min(one_k.values())
    rows.append(("rate: 1 kHz band spread over 1x/2x/4x", spread, "dB", spread <= 1.0))

    for mode, label, others in ((2, "Mono", dict(subLevel=0)), (3, "Modular", dict(osc3Level=0))):
        for osf in (1, 2, 4):
            fs, s0 = render(mode, 57, 3.0, osf, f"noise_m{mode}s0_{osf}",
                            **dict(BASE, osc1Level=1, osc2Level=0, noiseLevel=0, **others))
            _, s1 = render(mode, 57, 3.0, osf, f"noise_m{mode}s1_{osf}",
                           **dict(BASE, osc1Level=1, osc2Level=0, noiseLevel=1, **others))
            f, P0 = welch(s0[int(0.5 * fs):, 0], fs)
            _, P1 = welch(s1[int(0.5 * fs):, 0], fs)
            hh = np.round(f / F0)
            harm = (hh >= 1) & (np.abs(f - hh * F0) < 3 * fs / 16384) & (f < 20000)
            duck = 10 * np.log10(P1[harm].sum() / P0[harm].sum())
            rows.append((f"{label}: no duck, one saw, {osf}x", duck, "dB", duck >= -0.5))
        fs, n = render(mode, 57, 3.0, 2, f"noise_m{mode}n", **dict(BASE, osc1Level=0, osc2Level=0, noiseLevel=1, **others))
        f, Pn = welch(n[int(0.5 * fs):, 0], fs)
        lv = [10 * np.log10(octave(f, Pn, c)) for c in CENTRES]
        slope = float(np.polyfit(np.log2(CENTRES), lv, 1)[0])
        rows.append((f"{label}: pink, slope 125 Hz .. 2 kHz", slope, "dB/oct", abs(slope) <= 1.0))

    fails = 0
    for name, v, unit, ok in rows:
        fails += not ok
        print(f"  {name:<40} {v:+7.2f} {unit:<6} {'PASS' if ok else 'FAIL'}")
    print(f"noise_gate: {'PASS' if not fails else f'FAIL ({fails} rows)'}")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
