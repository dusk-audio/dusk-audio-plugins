#!/usr/bin/env python3
"""Oracle aliasing gate: per source, at the default Oversampling setting, 48 kHz (#303).

Owner rule (2026-11-17): at its default setting the Oracle may alias no worse
than the instrument it is modeled on, measured the same way on the same rows.
The reference rows below were measured on that instrument at its default
quality with this file's metric; each row's bar is that measurement, per note.

Metric: order-tracked inharmonic energy ratio (IER). The fundamental is
band-passed and its instantaneous frequency tracked (300 Hz low-pass), the
signal is resampled at 64 points per cycle of that track so every harmonic
sits on an integer order whatever the pitch does, and a power-averaged
Blackman-Harris order spectrum (256-cycle segments) splits the energy into
harmonic (within 0.04 order of an integer >= 1) and inharmonic (every other
order >= 0.5) up to 20 kHz. IER = inharmonic re harmonic, dB. Every row holds
one note, so everything off the harmonic series is fold-back (or noise). The
method floor is about -75 dB at C6; C5 is not used (its harmonics to 20 kHz
pass order 32, past what 64 points per cycle can hold).

Rows (one Oracle voice, A = osc 1, B = osc 2, drift and unison off, no time
effects, 6 s, analysed 1.0 to 5.5 s, notes C6 G6 C7):
  saw open     osc A saw alone, filter wide open, no resonance
  synced saw   osc A synced to osc B (B a fifth below, silent), filter open
  saw filtered osc A saw alone through a static 1.83 kHz filter, resonance 0.375
  B -> filter  the same, with osc B (silent, same pitch) moving the cutoff at
               audio rate, +-1.875 octaves
  trumpet      Oracle Trumpet's voice (both saws, sync, filter and poly-mod
               envelopes, drive), mod wheel down

Bars are of two kinds. "reference" bars are the reference measurements, gated
on every row since step F3 (the oscillators' minimum-phase BLEP) brought the
last of them there. "step" bars are what each step of the #303 plan was
measured to deliver in its kill test before it was implemented, with at least
3 dB to spare; they stay in force:

  step F1 (pre-filter before the Oracle filter, 4-pole 18 kHz):
      saw open and synced saw <= -58 dB at every note
      (kill test: -62.6/-60.9/-61.7 and -67.5/-66.5/-66.3; before: about -47)
  step F3 (minimum-phase BLEP; review round 1 item 5): a pulse the filter
      envelope narrows to 0.5 % of the cycle (FEnv -> PW, from 10 %), at the
      default 2x (minimum-phase BLEP) and at 4x (polyBLEP, where a pulse
      narrower than two samples is drawn two samples wide at the same area)
      <= -60 dB at every note (kill test: -71.9 / -76.6 / -77.4 and -62.6 /
      -64.3 / -65.5; before: -33.8 / -35.3 / -34.8 and -56.1 / -47.6 / -46.2)
  step F2 (osc B -> filter modulation low-passed at 10 kHz):
      B -> filter and trumpet <= -62 dB at every note
      (kill test: -71.8/-72.6/-67.4 and -71.7/-72.6/-68.0; after F1: C7 -49.0 and -50.6)

    row            reference C6/G6/C7     before #303 F1..F5    after F3 (2x)
    saw open       -70.8 -72.3 -72.3      -48.6 -46.7 -47.9     -74.9 -80.1 -80.9
    synced saw     -70.6 -72.1 -72.1      -47.7 -46.3 -47.4     -74.9 -80.1 -81.0
    saw filtered   -71.0 -78.4 -71.0      -59.1 -54.7 -52.1     -75.8 -87.4 -87.1
    B -> filter    -58.1 -66.3 -68.1      -51.8 -50.8 -48.5     -75.9 -87.6 -89.4
    trumpet        -60.9 -70.8 -75.9      -51.6 -50.4 -49.0     -75.7 -83.9 -82.5

C6 sits on the method floor (about -75 dB) in every row now.
"""
import sys
import warnings

import numpy as np
from scipy import signal

from _harness import render

NOTES = (84, 91, 96)
SR = 48000
DEFAULT_OS = 2        # the Oversampling control's default at 48 kHz
N_PER_CYCLE = 64
TRACK_LP_HZ = 300.0
SEG_CYCLES = 256

CLEAN = dict(analogAmt=0, unisonVoices=1, vintage=0, chorusOn=0, delayOn=0, reverbOn=0, arpOn=0,
             masterVol=-12, sr=SR)
BASE = dict(CLEAN, osc1Wave=0, osc2Wave=0, osc1Level=1, osc2Level=0, osc2Semi=0, osc2Detune=0,
            noiseLevel=0, filterEnvAmt=0, ampA=0.005, ampD=0.01, ampS=1.0, hardSync=0, driveOn=0,
            pmOscBFilt=0, pmFenvFilt=0, pmFenvOscA=0, pmOscBOscA=0, crossMod=0, ringMod=0)
TRUMPET = dict(CLEAN, osc1Wave=0, osc2Wave=0, osc1Detune=0, osc2Detune=0, osc1Level=1, osc2Level=1,
               noiseLevel=0, filterCutoff=355.03714, filterRes=0.3750001, filterEnvAmt=1.0,
               ampA=0.0479766, ampD=0.062679, ampS=0.5550002, ampR=0.1213579,
               filtA=0.0480906, filtD=0.1982077, filtS=0.4049999, filtR=0.1000001, filtCurve=2,
               crossMod=0, ringMod=0, pmFenvOscA=0, pmFenvFilt=0.2797203, pmOscBOscA=0, pmOscBPWM=0,
               pmOscBFilt=0.6048388, driveOn=1, driveAmt=0.3450003, hardSync=1, portaTime=0,
               lfo1Rate=4.8056626, lfo1Shape=1, lfo1Fade=0.675, lfo2Rate=2.0504818, lfo2Shape=1,
               lfo2Fade=1.3249999, modSrc0=4, modSrc1=1, modDst0=13, modDst1=2, modAmt0=1, modAmt1=1)

NARROW = dict(BASE, osc1Wave=4, osc1PW=0.10, filterCutoff=20000, filterRes=0, filtA=0.001, filtD=0.01,
              filtS=1, filtR=0.5, pmFenvPWM=(0.10 - 0.005) / 2.3)

# name: (params, played-note offset, {note: reference bar}, {note: step bar} or None, step that gates the
#        reference bars or None if gated now)
ROWS = {
    "saw open":     (dict(BASE, filterCutoff=20000, filterRes=0), 0,
                     {84: -70.8, 91: -72.3, 96: -72.3}, {84: -58.0, 91: -58.0, 96: -58.0}, None),
    "synced saw":   (dict(BASE, filterCutoff=20000, filterRes=0, hardSync=1, osc2Semi=-7), 7,
                     {84: -70.6, 91: -72.1, 96: -72.1}, {84: -58.0, 91: -58.0, 96: -58.0}, None),
    "saw filtered": (dict(BASE, filterCutoff=1830, filterRes=0.375), 0,
                     {84: -71.0, 91: -78.4, 96: -71.0}, None, None),
    "B -> filter":  (dict(BASE, filterCutoff=1830, filterRes=0.375, pmOscBFilt=0.6048388), 0,
                     {84: -58.1, 91: -66.3, 96: -68.1}, {84: -62.0, 91: -62.0, 96: -62.0}, None),
    "trumpet":      (dict(TRUMPET), 0, {84: -60.9, 91: -70.8, 96: -75.9}, {84: -62.0, 91: -62.0, 96: -62.0}, None),
}


def hz(note):
    return 440.0 * 2.0 ** ((note - 69) / 12.0)


def order_spectrum(m, sr, f_nom):
    lo, hi = f_nom * 2 ** -0.4, f_nom * 2 ** 0.4
    b = signal.sosfiltfilt(signal.butter(4, [lo, hi], "bandpass", fs=sr, output="sos"), m)
    ph = np.unwrap(np.angle(signal.hilbert(b)))
    f = np.gradient(ph) * sr / (2 * np.pi)
    f = signal.sosfiltfilt(signal.butter(2, TRACK_LP_HZ, "low", fs=sr, output="sos"), f)
    cyc = np.cumsum(f) / sr
    up = 16
    mu = signal.resample_poly(m, up, 1)
    tu = np.arange(len(mu)) / (sr * up)
    t = np.arange(len(m)) / sr
    trim = int(0.1 * sr)
    k0 = int(np.ceil(cyc[trim] * N_PER_CYCLE))
    k1 = int(np.floor(cyc[-trim] * N_PER_CYCLE))
    tk = np.interp(np.arange(k0, k1) / N_PER_CYCLE, cyc, t)
    y = np.interp(tk, tu, mu)
    L = N_PER_CYCLE * SEG_CYCLES
    w = signal.windows.blackmanharris(L)
    acc, n = None, 0
    for a in range(0, len(y) - L + 1, L // 2):
        P = np.abs(np.fft.rfft(y[a:a + L] * w)) ** 2
        acc = P if acc is None else acc + P
        n += 1
    order = np.fft.rfftfreq(L, 1.0 / N_PER_CYCLE)
    return order, acc / max(n, 1), float(np.median(f[trim:-trim]))


def ier(m, sr, f_nom):
    order, P, fmed = order_spectrum(m, sr, f_nom)
    dist = np.abs(order - np.round(order))
    top = 20000.0 / fmed
    harm = (dist <= 0.04) & (order >= 0.96) & (order <= top)
    inh = (~harm) & (order >= 0.5) & (order <= top)
    return 10 * np.log10(P[inh].sum() / P[harm].sum() + 1e-30)


def main():
    warnings.filterwarnings("ignore")
    fails = 0
    for osf in (DEFAULT_OS, 4):
        for note in NOTES:
            fs, x = render(1, note, 6.0, osf, f"oag_narrow_{osf}_{note}", **NARROW)
            v = ier(x.mean(axis=1)[int(1.0 * fs):int(5.5 * fs)], fs, hz(note))
            ok = v <= -60.0
            fails += not ok
            print(f"  narrow pulse {osf}x note {note}  IER {v:6.1f} dB   step bar  -60.0 {'PASS' if ok else 'FAIL'}")
    for name, (params, offset, ref, step, gated_by) in ROWS.items():
        for note in NOTES:
            fs, x = render(1, note + offset, 6.0, DEFAULT_OS, f"oag_{name.replace(' ', '_')}_{note}", **params)
            m = x.mean(axis=1)[int(1.0 * fs):int(5.5 * fs)]
            v = ier(m, fs, hz(note))
            cells = []
            if step is not None:
                ok = v <= step[note]
                fails += not ok
                cells.append(f"step bar {step[note]:6.1f} {'PASS' if ok else 'FAIL'}")
            if gated_by is None:
                ok = v <= ref[note]
                fails += not ok
                cells.append(f"reference {ref[note]:6.1f} {'PASS' if ok else 'FAIL'}")
            else:
                cells.append(f"reference {ref[note]:6.1f} pending {gated_by} "
                             f"({'meets it' if v <= ref[note] else f'{v - ref[note]:+.1f} dB'})")
            print(f"  {name:<13} note {note}  IER {v:6.1f} dB   " + "   ".join(cells))
    print(f"oracle_alias_gate: {'PASS' if not fails else f'FAIL ({fails} cells)'}")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
