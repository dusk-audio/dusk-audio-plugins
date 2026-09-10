#!/usr/bin/env python3
"""Filter stability/self-oscillation gate.

The TPT models are expected to decay at ordinary resonance and may sustain a
bounded musical oscillation at maximum resonance.  What must never return is
the former forward-Euler failure: a near-full-scale limit cycle at Nyquist.

WHAT THIS RENDERS
    The voice must stay ALIVE with a silent input, which is the one state that
    lets the filter run free. Releasing the note does not do it -- the voice
    deactivates and its filter state is reset, so a released render measures
    -600 dBFS on a broken build too (verified). Instead the note is held for the
    whole render at ampS = 1.0 and every source feeding the filter (osc 1/2/3,
    sub, noise, and the four Prism operators) is set to zero at 0.3 s. From
    there the filter has no input and the voice is still rendering it.

    Measured over the free tail (1.0-2.0 s), at max cutoff and OS=Off:

        mode          pre-fix                     post-fix
        Cosmos     -1.95 dBFS @ 24000 Hz        -258 dBFS, no peak
        Oracle     -2.90 dBFS @ 24000 Hz        -271 dBFS, no peak
        Mono       -4.63 dBFS @ 24000 Hz        -264 dBFS, no peak
        Prism      -3.20 dBFS @ 24000 Hz        -266 dBFS, no peak

    24000 Hz is 0.500*sr: the limit cycle, not a resonance. Resonance does not
    change the pre-fix reading at all (the onset is already far below the
    ceiling at res 0), which is why all three resonance values are swept: the
    onset frequency depends on resonance even though the tail level does not.

    Modular (mode 3) is excluded on purpose. It runs LadderFilter, which clamps
    its own g to 0.95 and is stable, so it keeps its full cutoff range.

LIMITS
    resonance 0/0.5: tail RMS < -80 dBFS.
    resonance 1.0: bounded below -6 dBFS; Oracle must sustain above -80 dBFS.
    every audible tail: no spectral peak at or above 0.45*sr.
"""
import sys
import numpy as np
from _harness import render

SR = 48000
SECONDS = 2.0
SILENCE_T = 0.3          # every source into the filter goes to zero here
TAIL_T = 1.0             # free-tail analysis window starts here
TAIL_MAX_DB = -80.0
OSC_MAX_DB = -6.0
NYQUIST_FRAC = 0.45      # no spectral peak at or above this fraction of sr
PEAK_FLOOR_DB = -140.0   # below this there is no peak, only denormals

MODES = ((0, "Cosmos"), (1, "Oracle"), (2, "Mono"), (4, "Prism"))
RESONANCES = (0.0, 0.5, 1.0)

# Pre-fix tail RMS at res 0 (identical at 0.5 and 1.0), for the printed column.
PRE_FIX_DB = {0: -1.95, 1: -2.90, 2: -4.63, 4: -3.20}

# Max cutoff, no envelope on it, and nothing downstream that could ring on its
# own: an effect tail would be indistinguishable from a filter tail.
PATCH = dict(sr=SR, analogAmt=0, vintage=0, noiseLevel=0, cosmosChorus=0,
             reverbOn=0, delayOn=0, chorusOn=0, driveOn=0, arpOn=0,
             filterCutoff=20000, filterEnvAmt=0,
             osc1Level=1.0, osc2Level=0, osc3Level=0, subLevel=0,
             ampA=0.005, ampD=0.05, ampS=1.0, ampR=0.05,
             filtA=0.005, filtD=0.05, filtS=1.0)

# Everything that can feed the filter, silenced together. The Prism operator
# levels are in here so mode 4 goes quiet too.
SILENCE = [f"{SILENCE_T}:{p}:0" for p in
           ("osc1Level", "osc2Level", "osc3Level", "subLevel", "noiseLevel",
            "op1Level", "op2Level", "op3Level", "op4Level")]


def tail_stats(x, sr):
    tail = x[int(TAIL_T * sr):, 0]
    rms = 20.0 * np.log10(np.sqrt(np.mean(tail ** 2)) + 1e-30)
    win = np.hanning(len(tail))
    spec = np.abs(np.fft.rfft(tail * win))
    freq = np.fft.rfftfreq(len(tail), 1.0 / sr)
    return rms, float(freq[int(np.argmax(spec))])


def main():
    fails = []
    print(f"{'mode':<9}{'res':>5}{'tail dBFS':>11}{'peak Hz':>10}{'peak/sr':>9}"
          f"{'pre-fix':>9}   result")
    for mode, name in MODES:
        for res in RESONANCES:
            _, x = render(mode, 60, SECONDS, 1, f"filtstab_{mode}_{res}",
                          filterRes=res, setat=SILENCE, **PATCH)
            rms, peak = tail_stats(x, SR)
            if res < 1.0:
                level_ok = rms < TAIL_MAX_DB
            else:
                # Oracle's defining self-oscillation must be present.  The other
                # modes may ring or decay, but none may approach full scale.
                level_ok = rms < OSC_MAX_DB and (mode != 1 or rms >= TAIL_MAX_DB)
            # Only judge the peak when the tail is loud enough to have one.
            peak_ok = (rms < PEAK_FLOOR_DB) or (peak < NYQUIST_FRAC * SR)
            ok = level_ok and peak_ok
            if not ok:
                fails.append(f"{name}/res{res}")
            print(f"{name:<9}{res:>5.1f}{rms:>11.2f}{peak:>10.1f}"
                  f"{peak / SR:>9.3f}{PRE_FIX_DB[mode]:>9.2f}   "
                  f"{'PASS' if ok else 'FAIL'}")

    print(f"\n(tail = RMS of {TAIL_T}-{SECONDS} s with every filter input at zero since "
          f"{SILENCE_T} s; res<1 must decay below {TAIL_MAX_DB:.0f} dBFS; "
          f"max-res tails stay below {OSC_MAX_DB:.0f} dBFS; no peak at/above "
          f"{NYQUIST_FRAC:.2f}*sr)")
    print(f"filter_stability_gate: {'PASS' if not fails else 'FAIL (' + ','.join(fails) + ')'}")
    sys.exit(0 if not fails else 1)


if __name__ == "__main__":
    main()
