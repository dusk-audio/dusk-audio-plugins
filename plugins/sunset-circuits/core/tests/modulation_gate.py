#!/usr/bin/env python3
"""Pitch-route rate invariance and synced sample-and-hold note lifecycle.

The Oracle envelope-to-pitch route holds a constant offset (the filter envelope
sits at full), making pitch a direct measurement of that route at every rate:
quality switches must preserve the sound. Its law is the measured Poly-Mod law
(Voice.hpp, ORACLE POLY-MOD LAWS): 33.908 semitones x amount, exponential. Until
2026-11-17 the route was linear FM (0.03 cycles per sample at 96 kHz x amount),
and this gate then covered Oscillator::applyFM's rate scaling as well.
Oscillator::applyFM is now covered through Cross Mod (Modular's two FM
routes use applyFM too): a 55 Hz sine
carrier, Cross Mod 0.01 from a full-scale square on osc 2 two octaves below,
must move the carrier up by the same number of hertz at every rate and factor:
within 0.3 Hz of the 48 kHz 2x reading, and within 1.5 Hz of the law
(2880 Hz x 0.01 = 28.8 Hz; measured 28.22 to 28.33 Hz). With applyFM's rate
scaling removed, 7 of the 9 rows fail (11.1 to 115.4 Hz).
S&H is tested before its first clock edge, on BOTH LFOs and on a reused mono
voice. A sine LFO is the positive control for the amplitude measurement.
"""
import sys
import numpy as np
from _harness import render, peak_hz


FENV_FULL_SEMITONES = 33.908138

BASE = dict(analogAmt=0, vintage=0, masterVol=-24, arpOn=0,
            driveOn=0, chorusOn=0, delayOn=0, reverbOn=0, cosmosChorus=0,
            osc1Wave=3, osc1Level=1, osc2Level=0, osc3Level=0,
            subLevel=0, noiseLevel=0, ampA=0.001, ampD=0.01, ampS=1,
            ampR=0.01, filtA=0.001, filtD=0.01, filtS=1,
            filterEnvAmt=0, filterCutoff=18000, filterRes=0)


def main():
    failures = []

    def check(name, ok, detail):
        print(f"{name}: {'PASS' if ok else 'FAIL'} ({detail})")
        if not ok:
            failures.append(name)

    for depth in (0.0, 0.1):
        for rate in (44100, 48000, 96000):
            for os in (1, 2, 4):
                sr, x = render(1, 69, 1.0, os, f"mod_pitch_{depth}_{rate}_{os}",
                               **dict(BASE, sr=rate, pmFenvOscA=depth))
                hz = peak_hz(x[sr // 2:, 0], sr)
                # One tenth of the fitted full range raises A440 by 3.39 semitones.
                expected = 440.0 * 2.0 ** (depth * FENV_FULL_SEMITONES / 12.0)
                check(f"pitch depth={depth} sr={rate} os={os}",
                      np.all(np.isfinite(x)) and np.max(np.abs(x)) < 0.1
                      and abs(hz - expected) < 0.3,
                      f"{hz:.3f} Hz, expected {expected:.3f}")

    # Cross Mod (applyFM): the same deviation in hertz at every rate and factor.
    xmod = {}
    for rate in (44100, 48000, 96000):
        for os in (1, 2, 4):
            sr, x = render(1, 33, 1.0, os, f"mod_xmod_{rate}_{os}",
                           **dict(BASE, sr=rate, osc2Wave=1, osc2Semi=-24, osc2Detune=0, crossMod=0.01))
            y = x[int(0.2 * sr):, 0]
            zc = np.flatnonzero((y[:-1] < 0) & (y[1:] >= 0))
            t = zc + (-y[zc]) / (y[zc + 1] - y[zc])
            hz = sr / np.diff(t)
            xmod[(rate, os)] = float(np.median(hz[hz > 56.0])) - 55.0 if np.any(hz > 56.0) else float("nan")
    ref = xmod[(48000, 2)]
    for (rate, os), up in xmod.items():
        check(f"Cross Mod sr={rate} os={os}", abs(up - ref) <= 0.3 and abs(up - 28.8) <= 1.5,
              f"+{up:.2f} Hz, 48 kHz 2x +{ref:.2f}, law +28.8")

    # A held note must retain its modulated pitch through a live quality change.
    sr, x = render(1, 69, 1.8, 2, "mod_pitch_live_os",
                   **dict(BASE, sr=48000, pmFenvOscA=0.1,
                          setat=["0.6:oversampling:2", "1.2:oversampling:0"]))
    peaks = [peak_hz(x[int(a * sr):int(b * sr), 0], sr)
             for a, b in ((0.2, 0.5), (0.8, 1.1), (1.4, 1.7))]
    held = 440.0 * 2.0 ** (0.1 * FENV_FULL_SEMITONES / 12.0)
    check("held pitch across 2x/4x/1x", all(abs(f - held) < 0.3 for f in peaks),
          ", ".join(f"{f:.3f} Hz" for f in peaks))

    for lfo in (1, 2):
        for os in (1, 2, 4):
            for shape in (0, 3):  # sine control and S&H
                patch = dict(BASE, sr=48000, playing=1, songpos=0, block=64,
                             modSrc0=lfo, modDst0=7,
                             noteoff="0.1:69", noteon="0.2:69")
                patch.update({f"lfo{lfo}Sync": 1, f"lfo{lfo}Rate": 0.25,
                              f"lfo{lfo}Shape": shape})
                tag = f"mod_sh_{lfo}_{os}_{shape}"
                sr, dry = render(2, 69, 0.4, os, tag + "_off",
                                 **dict(patch, modAmt0=0))
                _, wet = render(2, 69, 0.4, os, tag + "_on",
                                **dict(patch, modAmt0=0.7))
                gains = []
                for a, b in ((0.03, 0.08), (0.24, 0.30)):
                    d = dry[int(a * sr):int(b * sr), 0]
                    w = wet[int(a * sr):int(b * sr), 0]
                    gains.append(float(np.dot(d, w) / max(np.dot(d, d), 1e-20)))
                audible = [float(np.max(np.abs(wet[int(a * sr):int(b * sr)]
                                               - dry[int(a * sr):int(b * sr)])))
                           for a, b in ((0.03, 0.08), (0.24, 0.30))]
                check(f"LFO{lfo} shape={shape} os={os} startup/retrigger",
                      all(v > 1e-5 for v in audible)
                      and (shape != 3 or abs(gains[0] - gains[1]) > 0.01),
                      f"deltas={audible[0]:.6f}/{audible[1]:.6f}, "
                      f"gains={gains[0]:.4f}/{gains[1]:.4f}")

    print(f"modulation_gate: {'PASS' if not failures else 'FAIL'} "
          f"({len(failures)} failed checks)")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
