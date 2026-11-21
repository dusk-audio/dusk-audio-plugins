"""Every note in an Oracle chord that fits the voice budget must sound.

Start with three older notes. Two incoming notes take free slots, then a third
forces a steal. Check each new fundamental against the same chord played alone.
This catches a fresh, still-silent attack being mistaken for the quietest victim.
"""
import numpy as np

from _harness import render


NOTES = (67, 71, 78)
PATCH = dict(osc1Wave=3, osc1Level=1, osc2Level=0, osc3Level=0, subLevel=0,
             noiseLevel=0, ampA=.015, ampD=.01, ampS=1, ampR=3, velSens=0,
             analogAmt=0, vintage=0, filterCutoff=12000, filterEnvAmt=0,
             filterRes=0, masterVol=-18, reverbOn=0, delayOn=0, chorusOn=0,
             driveOn=0)


def levels(audio, rate):
    segment = audio[round(.75 * rate):round(.95 * rate)]
    window = np.hanning(len(segment))
    t = np.arange(len(segment)) / rate
    result = []
    for note in NOTES:
        hz = 440 * 2 ** ((note - 69) / 12)
        amplitude = 2 * abs((window * np.exp(-2j * np.pi * hz * t)) @ segment) / window.sum()
        result.append(20 * np.log10(max(np.max(amplitude), 1e-12)))
    return np.array(result)


if __name__ == "__main__":
    sr, clean = render(1, NOTES[0], 1, 2, "oracle_clean_chord",
                       hold="71,78", **PATCH)
    reference = levels(clean, sr)
    passed = True
    for name, options in (("released", {"release": .3}), ("held", {})):
        sr, audio = render(1, 48, 1, 2, "oracle_chord_" + name, hold="51,56",
                           noteon=[f"0.6:{note}" for note in NOTES], **options, **PATCH)
        ratios = levels(audio, sr) - reference
        ok = np.all(reference > -55) and np.all(np.abs(ratios) < 3)
        values = ", ".join(f"{v:+.1f}" for v in ratios)
        print(f"{'PASS' if ok else 'FAIL'} Oracle chord over {name} voices, "
              f"MIDI {NOTES} relative to isolated chord: {values} dB (within 3 dB)")
        passed = passed and ok
    raise SystemExit(0 if passed else 1)
