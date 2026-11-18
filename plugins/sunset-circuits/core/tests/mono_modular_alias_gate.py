#!/usr/bin/env python3
"""Mono and Modular aliasing gate: the pre-filter and route band limit (#303).

Mono and Modular share Oracle's mechanism: the filter's input saturation, and an
audio-rate cutoff route (Modular's osc 3 -> filter), acting on the oscillators'
ultrasonic content folded harsh tones back into the audio band. They now get
Oracle's pre-filter and route low-pass (Voice.hpp, PRE-FILTER). Their factory
presets changed by at most 0.023 dB (spectral distance) and 0.01 dB in level.

Same metric and rows as oracle_alias_gate.py (one voice, default setting 2x,
48 kHz, C6 G6 C7, the mode's other sources off); the synced saw is osc 2 synced
to osc 1 a fifth above it. No reference instrument was measured for these two
modes, so every bar is a step bar: the kill-test value of the worst note plus
3 dB, pre-registered before the change was implemented.

    row                     before (C6/G6/C7)      kill test              bar
    Mono    saw open        -43.3 -41.5 -42.5      -68.4 -67.6 -68.4      -64
    Mono    saw filtered    -48.4 -43.7 -39.3      -66.0 -62.5 -57.6      -54
    Mono    synced saw      -42.6 -41.1 -42.1      -67.5 -66.5 -67.2      -63
    Modular saw open        -45.1 -43.8 -44.9      -69.2 -68.7 -69.4      -65
    Modular saw filtered    -54.8 -51.5 -48.4      -72.8 -72.1 -68.2      -65
    Modular synced saw      -44.6 -43.2 -44.4      -68.0 -67.2 -67.6      -64
    Modular osc3 -> filter  -54.1 -54.3 -47.9      -72.7 -72.8 -67.8      -64
"""
import sys
import warnings

from _harness import render
from oracle_alias_gate import CLEAN, NOTES, DEFAULT_OS, hz, ier

C = dict(CLEAN, osc1Wave=0, osc2Wave=0, osc1Level=1, osc2Level=0, osc2Semi=0, osc2Detune=0, noiseLevel=0,
         filterEnvAmt=0, ampA=0.005, ampD=0.01, ampS=1.0, hardSync=0, driveOn=0, ringMod=0, subLevel=0,
         osc3Level=0, modOsc3Filter=0, fmAmount=0)
ROWS = [
    (2, "Mono    saw open", dict(C, filterCutoff=20000, filterRes=0), -64.0),
    (2, "Mono    saw filtered", dict(C, filterCutoff=1830, filterRes=0.375), -54.0),
    (2, "Mono    synced saw", dict(C, filterCutoff=20000, filterRes=0, osc1Level=0, osc2Level=1, hardSync=1,
                               osc2Semi=7), -63.0),
    (3, "Modular saw open", dict(C, filterCutoff=20000, filterRes=0), -65.0),
    (3, "Modular saw filtered", dict(C, filterCutoff=1830, filterRes=0.375), -65.0),
    (3, "Modular synced saw", dict(C, filterCutoff=20000, filterRes=0, osc1Level=0, osc2Level=1, hardSync=1,
                                  osc2Semi=7), -64.0),
    (3, "Modular osc3 -> filter", dict(C, filterCutoff=1830, filterRes=0.375, osc3Wave=0, modOsc3Filter=0.3), -64.0),
]


def main():
    warnings.filterwarnings("ignore")
    fails = 0
    for mode, name, params, bar in ROWS:
        vals = []
        for note in NOTES:
            fs, x = render(mode, note, 6.0, DEFAULT_OS, f"mmag_{mode}_{note}", **params)
            vals.append(ier(x.mean(axis=1)[int(1.0 * fs):int(5.5 * fs)], fs, hz(note)))
        ok = max(vals) <= bar
        fails += not ok
        print(f"  {name:<24} IER " + " ".join(f"{v:6.1f}" for v in vals) + f" dB   bar {bar:5.1f}   {'PASS' if ok else 'FAIL'}")
    print(f"mono_modular_alias_gate: {'PASS' if not fails else f'FAIL ({fails} rows)'}")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
