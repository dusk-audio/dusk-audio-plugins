#!/usr/bin/env python3
"""Pitch-correctness gate — THE regression test for the octave-drop bug.

Renders A440 (MIDI note 69) for each of modes 0-5 at oversampling 1x/2x/4x and
checks the FFT peak lands within +-5 cents of 440 Hz. Modes 0-3 use a clean
single-saw patch; mode 4 (Prism) uses algo 8 (additive) with a single unity-ratio
carrier so the FM output is a pure sine at f0; mode 5 (Acid) plays a live saw
through the mono acid path (sequencer off). Spec requires every note-69 row to
pass; note 96 is an extra "not every note plays 440" guard.
"""
import sys
from _harness import render, peak_hz, cents

TOL_CENTS = 5.0

# Modes 0-3: clean single saw so the fundamental is unambiguous. Moderate cutoff
# keeps the OTA one-pole coefficient (g = tan(pi*fc/sr)) well-behaved even at 1x.
CLEAN = dict(osc1Wave=0, osc2Level=0, subLevel=0, osc3Level=0, noiseLevel=0,
             analogAmt=0, filterEnvAmt=0, filterCutoff=5000, filterRes=0.0,
             ampA=0.005, ampD=0.01, ampS=1.0, reverbOn=0, cosmosChorus=0)

# Mode 4 (Prism): algo 8 (index 7, additive) with only op1 as a unity-ratio
# carrier -> a pure sine at the played frequency. Filter open, sustained.
# (cutoff kept moderate for the same 1x one-pole reason as CLEAN; a 440/2093 Hz
#  carrier passes a 5 kHz LPF cleanly.)
PRISM = dict(prismAlgo=7, op1Ratio=1, op1Fine=0, op1Level=1, op1S=1.0,
             op2Level=0, op3Level=0, op4Level=0, prismFB=0,
             filterEnvAmt=0, filterCutoff=5000, filterRes=0.0,
             analogAmt=0, ampA=0.005, ampD=0.01, ampS=1.0, reverbOn=0)

# Mode 5 (Acid): live-played saw through the mono acid path (sequencer off),
# static filter so the fundamental is clean and sustained.
ACID = dict(arpOn=0, osc1Wave=0, filterEnvAmt=0, filterCutoff=5000,
            filterRes=0.0, ampD=5.0, ampS=1.0, analogAmt=0, reverbOn=0)

MODES = {0: ("Cosmos", CLEAN), 1: ("Oracle", CLEAN), 2: ("Mono", CLEAN),
         3: ("Modular", CLEAN), 4: ("Prism", PRISM), 5: ("Acid", ACID)}

NOTES = {69: 440.0, 96: 2093.005}


def main():
    print(f"{'mode':<9}{'note':>5}{'os':>4}{'peakHz':>12}{'cents':>9}   result")
    ok = 0
    total = 0
    for note, ref in NOTES.items():
        for mode, (name, patch) in MODES.items():
            for os_ in (1, 2, 4):
                total += 1
                sr, x = render(mode, note, 1.0, os_, f"pitch_{mode}_{note}_{os_}", **patch)
                seg = x[int(0.3 * sr):int(0.9 * sr), 0]
                pk = peak_hz(seg, sr)
                c = cents(pk, ref)
                passed = abs(c) <= TOL_CENTS
                ok += passed
                print(f"{name:<9}{note:>5}{os_:>3}x{pk:>12.3f}{c:>+9.2f}   {'PASS' if passed else 'FAIL'}")
    print(f"\npitch_gate: {ok}/{total} within +-{TOL_CENTS:.0f} cents "
          f"(spec requires every note-69 row; note 96 is a regression guard)")
    sys.exit(0 if ok == total else 1)


if __name__ == "__main__":
    main()
