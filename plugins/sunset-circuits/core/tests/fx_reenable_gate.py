#!/usr/bin/env python3
"""Effects-chain state gate — stale buffers on re-enable, and the sync'd delay.

A. RE-ENABLING AN EFFECT INTO SILENCE MUST BE SILENT
    Every effect here returns early from process() while disabled, which stops
    the WRITES into its delay lines and tanks but leaves their contents intact.
    Turning the effect back on resumes reading, so the audio that was in there
    when it was bypassed is played back at the moment of the enable -- however
    long ago that was, and at whatever level it had.

    The probe holds a note, disables the effect, kills every note (panic), waits
    for the output to go silent, then re-enables into that silence. Anything
    audible after the enable came out of a stale buffer.

        row               quiet window   re-enable   pre-fix   limit
        chorus              -400.0 dBFS    -400.0      -29.2    -90
        reverbOn            -400.0 dBFS    -400.0      -14.2    -90
        reverb pre-delay     -87.0 dBFS    -111.0      -28.3    -95
        spring (via mode)   -400.0 dBFS    -400.0      -44.4    -90

    The reverbOn row was a FULL-SCALE, clipping burst before the fix.

    The pre-delay row is the one with a live floor: reverbOn stays 1 for it (the
    pre-delay line is what is being bypassed, not the reverb), so the Freeverb
    tank is still decaying through the quiet window at -87 dB. Its limit is
    therefore set below the measured post-fix reading rather than at the -90
    the silent rows use, and the row additionally requires the re-enable window
    to be QUIETER than the preceding quiet window -- a replay is a burst, and a
    burst goes up.

    The spring tank has no parameter of its own: SpringReverbFX is enabled only
    in Modular, so its enable edge IS the mode switch, and the row drives it
    that way.

B. TEMPO-SYNCED DELAY TIME
    The sync path can ask for far more delay than the free-running knob's 2 s
    ceiling -- the slowest division is 1/1 = 4 beats, which is 2 s only at
    exactly 120 BPM and grows without bound as the tempo drops. The line was
    sized for 2 s, so everything longer was silently clamped and only 120 BPM
    was right, by coincidence:

        tempo      expected    pre-fix    post-fix
         20 BPM    12.000 s     2.000 s   12.000 s
         60 BPM     4.000 s     2.000 s    4.000 s
         90 BPM     2.667 s     2.000 s    2.667 s
        120 BPM     2.000 s     2.000 s    2.000 s
         60 BPM 1/8  0.500 s    0.500 s    0.500 s   (control: short division)

    Measured as the first echo of a single 50 ms note at delayMix 1.0 with no
    feedback. Every reading carries a constant +0.3 ms, which is the note's own
    attack reaching the detection threshold, not a delay error.
"""
import sys
import numpy as np
from _harness import render

SR = 48000

CLEAN = dict(sr=SR, analogAmt=0, vintage=0, noiseLevel=0, cosmosChorus=0,
             driveOn=0, osc1Wave=3, osc2Level=0, osc3Level=0, subLevel=0,
             arpOn=0, filterCutoff=8000, filterRes=0.1, filterEnvAmt=0,
             ampA=0.005, ampD=0.05, ampS=1.0, ampR=0.05, masterVol=-6)

# --- A. re-enable into silence ------------------------------------------------
DISABLE_T = 0.3
PANIC_T = 0.5
ENABLE_T = 2.0
QUIET_WIN = (1.5, 1.99)      # settled silence, just before the enable
BURST_WIN = (2.0, 2.1)       # the enable edge and 100 ms after it

SPRING_ENABLE_T = 2.5        # the spring row needs a longer settle
SPRING_QUIET = (2.0, 2.49)
SPRING_BURST = (2.5, 2.9)


def rms_db(x, sr, win):
    seg = x[int(win[0] * sr):int(win[1] * sr), 0]
    return 20.0 * np.log10(np.sqrt(np.mean(seg ** 2)) + 1e-20)


def reenable_rows():
    """(label, limit_db, pre_fix_db, quiet_win, burst_win, render kwargs)."""
    off = {k: v for k, v in CLEAN.items()}
    return [
        ("chorus", -90.0, -29.2, QUIET_WIN, BURST_WIN,
         dict(off, reverbOn=0, delayOn=0, chorusOn=1, chorusMix=1.0,
              chorusDepth=1.0,
              setat=[f"{DISABLE_T}:chorusOn:0", f"{ENABLE_T}:chorusOn:1"],
              panicat=PANIC_T)),
        ("reverbOn", -90.0, -14.2, QUIET_WIN, BURST_WIN,
         dict(off, reverbOn=1, delayOn=0, chorusOn=0, reverbMix=1.0, reverbPD=50,
              setat=[f"{DISABLE_T}:reverbOn:0", f"{ENABLE_T}:reverbOn:1"],
              panicat=PANIC_T)),
        ("reverb pre-delay", -95.0, -28.3, QUIET_WIN, BURST_WIN,
         dict(off, reverbOn=1, delayOn=0, chorusOn=0, reverbMix=1.0, reverbPD=200,
              setat=[f"{DISABLE_T}:reverbPD:0", f"{ENABLE_T}:reverbPD:200"],
              panicat=PANIC_T)),
        ("spring (mode 3)", -90.0, -44.4, SPRING_QUIET, SPRING_BURST,
         dict(off, reverbOn=0, delayOn=0, chorusOn=0,
              setat=[f"{DISABLE_T}:mode:0", f"{SPRING_ENABLE_T}:mode:3"],
              panicat=PANIC_T)),
    ]


def reenable(fails):
    print("A. re-enabling an effect into silence")
    print(f"   {'effect':<18}{'quiet dBFS':>12}{'re-enable':>11}{'limit':>8}"
          f"{'pre-fix':>9}   result")
    for label, limit, prefix, qwin, bwin, kw in reenable_rows():
        mode = 3 if label.startswith("spring") else 0
        secs = 4.0 if label.startswith("spring") else 3.0
        _, x = render(mode, 60, secs, 2,
                      f"fxre_{label.split()[0]}", **kw)
        quiet = rms_db(x, SR, qwin)
        burst = rms_db(x, SR, bwin)
        # Below the row's own limit, and never LOUDER than the settled silence
        # it was re-enabled into. The second half is what makes the pre-delay
        # row (live reverb tail, -87 dB floor) judgeable at all.
        ok = burst < limit and burst <= quiet + 1.0
        if not ok:
            fails.append(label.split()[0])
        print(f"   {label:<18}{quiet:>12.1f}{burst:>11.1f}{limit:>8.0f}"
              f"{prefix:>9.1f}   {'PASS' if ok else 'FAIL'}")


# --- B. tempo-synced delay ----------------------------------------------------
DIV_WHOLE = 0                # ArpRateDivision::Whole = 4 beats
DIV_EIGHTH = 3               # 0.5 beats
DIV_BEATS = {DIV_WHOLE: 4.0, DIV_EIGHTH: 0.5}
DIV_NAME = {DIV_WHOLE: "1/1", DIV_EIGHTH: "1/8"}
ECHO_TOL_S = 0.003           # absolute floor of the tolerance
ECHO_TOL_REL = 0.005         # ...or 0.5% of the expected time, whichever is larger
PRE_FIX_ECHO_S = {(20, DIV_WHOLE): 2.000, (60, DIV_WHOLE): 2.000,
                  (90, DIV_WHOLE): 2.000, (120, DIV_WHOLE): 2.000,
                  (60, DIV_EIGHTH): 0.500}

DELAY = dict(CLEAN, reverbOn=0, chorusOn=0, ampS=0.0, ampD=0.02, ampR=0.02,
             delayOn=1, delaySync=1, delayFB=0.0, delayMix=1.0,
             delayPP=0, delayTape=0)


def first_echo_s(x, sr, after_s=0.2):
    """Time of the first sample past `after_s` that rises above 5% of peak."""
    a = np.abs(x[:, 0])
    i0 = int(after_s * sr)
    idx = np.flatnonzero(a[i0:] > 0.05 * float(np.max(a)))
    return (i0 + idx[0]) / sr if len(idx) else float("nan")


def delay_sync(fails):
    print("\nB. tempo-synced delay time (delayMix 1.0, no feedback)")
    print(f"   {'tempo':>6} {'div':<5}{'expected s':>12}{'measured s':>12}"
          f"{'err ms':>9}{'tol ms':>8}{'pre-fix s':>11}   result")
    for bpm, div in ((20, DIV_WHOLE), (60, DIV_WHOLE), (90, DIV_WHOLE),
                     (120, DIV_WHOLE), (60, DIV_EIGHTH)):
        exp = DIV_BEATS[div] * 60.0 / bpm
        tol = max(ECHO_TOL_S, ECHO_TOL_REL * exp)
        _, x = render(0, 60, max(2.0, exp + 1.0), 2, f"fxre_delay_{bpm}_{div}",
                      tempo=bpm, delayDiv=div, release=0.05, **DELAY)
        t = first_echo_s(x, SR)
        err = t - exp
        ok = abs(err) <= tol
        if not ok:
            fails.append(f"delay/{bpm}/{DIV_NAME[div]}")
        print(f"   {bpm:>6.0f} {DIV_NAME[div]:<5}{exp:>12.3f}{t:>12.3f}"
              f"{err * 1000:>9.1f}{tol * 1000:>8.1f}"
              f"{PRE_FIX_ECHO_S[(bpm, div)]:>11.3f}   {'PASS' if ok else 'FAIL'}")


def main():
    fails = []
    reenable(fails)
    delay_sync(fails)
    print(f"\nfx_reenable_gate: {'PASS' if not fails else 'FAIL (' + ','.join(fails) + ')'}")
    sys.exit(0 if not fails else 1)


if __name__ == "__main__":
    main()
