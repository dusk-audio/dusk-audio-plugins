#!/usr/bin/env python3
"""Hard-sync aliasing gate: a synced oscillator folds back no more than a free one.

Hard sync restarts the slave oscillator every time the master wraps. The
restart is a step in the slave's waveform, and it used to be a bare one, applied
up to a sample early on the internal sample grid: at the default 2x, a synced C7
folded back images only 25 to 32 dB below its strongest partial. Now the restart
lands where the master actually wrapped and the step (and, for the sine and
triangle, the corner) is band-limited (Oscillator::processSampleSynced).

A synced tone is periodic at the master's pitch, whatever the slave's, so
everything off that harmonic series is fold-back. Each row holds the master on
a high note (2093 Hz, MIDI 96) with the slave a fifth above it and synced, the
master and the other sources silent (the master still runs), the filter wide
open and nothing else in the path that could fold back: no poly-mod, no drift,
no effects, a low level into the filter. Mono and Modular sync osc 2 to osc 1;
Oracle syncs osc 1 to osc 2, so its rows swap the two (note 103 with osc 2 seven
semitones down puts its master on the same 2093 Hz). dBc = worst bin
off the harmonics, 50 Hz to 15 kHz, re the strongest harmonic (alias_gate's
measurement, re the strongest partial as fx_alias_gate does).

    worst image, dBc            2x old   2x new   4x old   4x new
    (old = the previous bare restart, same sync directions)
    Oracle  saw                  -30.4    -62.1    -35.7    -75.9
    Oracle  square               -32.2    -64.7    -38.4    -76.4
    Oracle  pulse 30 %           -24.8    -56.4    -31.6    -67.0
    Oracle  triangle             -25.3    -61.4    -31.2    -73.7
    Oracle  sine                 -29.3    -97.7    -34.8   -109.0
    Mono    saw                  -30.3    -59.5    -35.6    -69.5
    Modular saw                  -29.5    -60.0    -34.6    -70.9
"""
import sys

import numpy as np

from _harness import render

NOTE = 96
F0 = 440.0 * 2.0 ** ((NOTE - 69) / 12.0)
LIMIT_DBC = -50.0
BAND = (50.0, 15000.0)
COMMON = dict(hardSync=1, filterCutoff=20000, filterRes=0, filterEnvAmt=0, pmOscBFilt=0,
              pmFenvFilt=0, analogAmt=0, subLevel=0, osc3Level=0, noiseLevel=0,
              unisonVoices=1, ampA=0.005, ampD=0.01, ampS=1.0, sr=48000)


def sync_patch(mode, wave):
    """(played note, params): the slave audible at 0.3, a fifth above the master."""
    if mode == 1:   # Oracle: osc 2 is the master (its default +7 ct detune off), osc 1 the slave
        p = dict(osc1Wave=wave, osc1Level=0.3, osc2Level=0, osc2Semi=-7, osc2Detune=0)
        if wave == 4:
            p["osc1PW"] = 0.3
        return NOTE + 7, p
    p = dict(osc2Wave=wave, osc2Level=0.3, osc1Level=0, osc2Semi=7)
    if wave == 4:
        p["osc2PW"] = 0.3
    return NOTE, p


WAVES = {0: "saw", 1: "square", 2: "triangle", 3: "sine", 4: "pulse 30%"}
CASES = [(1, 0), (1, 1), (1, 4), (1, 2), (1, 3), (2, 0), (3, 0)]   # (mode, slave wave)
MODES = {1: "Oracle", 2: "Mono", 3: "Modular"}


def worst_image_dbc(sig, sr):
    X = np.abs(np.fft.rfft(sig * np.hanning(len(sig))))
    f = np.fft.rfftfreq(len(sig), 1.0 / sr)
    lo, hi = np.searchsorted(f, BAND[0]), np.searchsorted(f, BAND[1])
    hh = np.round(f[lo:hi] / F0)
    harm = (hh >= 1) & (np.abs(f[lo:hi] - hh * F0) < 0.03 * F0)
    ref = float(X[lo:hi][harm].max())
    k = int(np.argmax(np.where(harm, 0.0, X[lo:hi])))
    return 20.0 * np.log10(float(X[lo + k]) / ref + 1e-20), float(f[lo + k])


def main():
    fails = 0
    print(f"master {F0:.0f} Hz, slave a fifth up and synced; limit {LIMIT_DBC:.0f} dBc")
    for mode, wave in CASES:
        for osf in (2, 4):
            note, patch = sync_patch(mode, wave)
            sr, x = render(mode, note, 1.0, osf, f"syncalias_{mode}_{wave}_{osf}",
                           **COMMON, **patch)
            seg = x[int(0.3 * sr):int(0.9 * sr), 0]
            if not np.all(np.isfinite(seg)):
                db, hz = float("inf"), 0.0
            else:
                db, hz = worst_image_dbc(seg, sr)
            ok = db <= LIMIT_DBC
            fails += not ok
            print(f"  {MODES[mode]:<8} {WAVES[wave]:<10} {osf}x   worst image {db:7.1f} dBc"
                  f" @ {hz:7.0f} Hz   {'PASS' if ok else 'FAIL'}")
    print(f"sync_alias_gate: {'PASS' if not fails else f'FAIL ({fails} rows)'}")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
