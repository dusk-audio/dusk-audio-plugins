#!/usr/bin/env python3
"""Pitch-correctness gate — THE regression test for the octave-drop bug.

Renders A440 (MIDI note 69) for each of modes 0-3 at oversampling 1x/2x/4x and
checks the FFT peak lands within +-5 cents of 440 Hz. Must pass 12/12.
"""
import sys
from _harness import render, peak_hz, cents

MODES = {0: "Cosmos", 1: "Oracle", 2: "Mono", 3: "Modular"}
TOL_CENTS = 5.0

# Clean single-saw patch so the fundamental is unambiguous. Cutoff kept moderate
# so the OTA filter's one-pole coefficient (g = tan(pi*fc/sr)) stays < 1 even at
# 1x (the filter models rely on oversampling headroom for very bright cutoffs).
CLEAN = dict(osc1Wave=0, osc2Level=0, subLevel=0, osc3Level=0, noiseLevel=0,
             analogAmt=0, filterEnvAmt=0, filterCutoff=5000, filterRes=0.0,
             ampA=0.005, ampD=0.01, ampS=1.0, reverbOn=0, cosmosChorus=0)


# note 69 = A440 (the spec test). note 96 = C7 (2093 Hz) is an extra guard: an
# A440-only test cannot distinguish "correct" from "every note plays 440".
NOTES = {69: 440.0, 96: 2093.005}


def main():
    print(f"{'mode':<9}{'note':>5}{'os':>4}{'peakHz':>12}{'cents':>9}   result")
    ok = 0
    total = 0
    for note, ref in NOTES.items():
        for mode, name in MODES.items():
            for os_ in (1, 2, 4):
                total += 1
                sr, x = render(mode, note, 1.0, os_, f"pitch_{mode}_{note}_{os_}", **CLEAN)
                seg = x[int(0.3 * sr):int(0.9 * sr), 0]
                pk = peak_hz(seg, sr)
                c = cents(pk, ref)
                passed = abs(c) <= TOL_CENTS
                ok += passed
                print(f"{name:<9}{note:>5}{os_:>3}x{pk:>12.3f}{c:>+9.2f}   {'PASS' if passed else 'FAIL'}")
    print(f"\npitch_gate: {ok}/{total} within +-{TOL_CENTS:.0f} cents "
          f"(spec requires 12/12 on note 69; note 96 is a regression guard)")
    sys.exit(0 if ok == total else 1)


if __name__ == "__main__":
    main()
