#!/usr/bin/env python3
"""Engine robustness gate — bad parameter writes, meter range, crossfade capture.

A. A BAD PARAMETER VALUE MUST NOT KILL THE ENGINE FOR GOOD
    One out-of-range write left the synth rendering silence PERMANENTLY, with
    the note still held, and writing a good value back did not bring it back.
    The value reaches a one-pole smoother whose state is recursive, so a
    non-finite target poisons it forever; every later sample comes out
    non-finite and is zeroed by softLimit, which is why the symptom is silence
    rather than NaN reaching the host.

        parameter     bad value   healthy peak   peak after restoring it
                                                 pre-fix      post-fix
        masterVol       nan         0.13370      0.00000      0.26356
        masterVol       inf         0.13370      0.00000      0.26356
        masterVol       1e30        0.13370      0.00000      0.26357
        filterCutoff    nan         0.13370      0.00000      0.13210
        stereoWidth     inf         0.13370      0.00000      0.13210

    1e30 is the case a non-finite guard CANNOT catch -- it is a perfectly finite
    float that pow() turns into +Inf downstream -- so it is swept alongside the
    two non-finite forms rather than instead of them. (masterVol recovers LOUDER
    than the healthy reading because the restore value is 0 dB and the patch
    runs at -6.)

    render_test rejects non-finite numbers everywhere else; setat= values are
    the one exception, precisely so this can be injected.

B. THE OUTPUT METERS MUST STAY INSIDE THEIR DECLARED RANGE
    The shell declares kParamOutLevelL/R with min = -60 dB, but the linear->dB
    conversion floored at 1e-6, so anything between 1e-6 and 1e-3 was published
    as -120..-60 dB. VST3 hosts normalise against the declared range and pin the
    meter, but LV2 hands the raw port value straight through, so the
    out-of-range values were visible there.

    Lowest value published over a note decaying into silence:
        pre-fix  -95.78 dB      post-fix  -60.00 dB

    Read through render_test's meters=<path> log, which records
    getOutputLevelL/R after every processBlock.

C. A NOTE PLAYED DURING A MODE CROSSFADE MUST SURVIVE THE COMMIT
    A mode switch fades the voice path out over 12 ms and commits when it
    reaches zero, and the commit resets the voice pool. A note-on arriving
    inside that window is routed to the outgoing mode -- correctly, that is what
    is still sounding -- and then wiped by the reset, so the key does nothing.

        key pressed        sustained level after the switch
                           pre-fix       post-fix
        +2 ms              -400.0 dBFS   -19.3 dBFS
        +6 ms              -400.0 dBFS   -19.3 dBFS
        +10 ms             -400.0 dBFS   -19.3 dBFS
        +20 ms (past it)    -19.3 dBFS   -19.3 dBFS   (control)

    block=64 on purpose. Scheduled events fire on the first block starting at or
    after their time, so at the default 512 frames all three offsets quantise to
    the SAME boundary and only one of them is really tested; at 64 frames they
    land 2, 6 and 10 ms into the 12 ms fade as intended.

    Two ways the replay could go wrong are checked with it: a key-up after a
    replayed note must still release it (pre-fix -311.5 dBFS held, i.e. there was
    no note to release), and a fade CANCELLED before it commits must replay
    nothing, so the note is not triggered twice. The cancelled-fade row passes on
    BOTH builds -- there was no replay at all pre-fix, so it cannot double-trigger
    there. It is a guard on the fix, not a detector of the bug, and it is the row
    that would catch the replay firing where it should not.

D. PORTAMENTO ACROSS AN OVERSAMPLING SWITCH
    setSampleRate re-derives the portamento coefficient for the new rate, but it
    did so at a hardcoded 0.1 s instead of at the time constant the live glide
    is using. It is called on every oversampling switch, so changing
    oversampling part-way through a 2 s glide silently rewrote it to a 0.1 s
    one. Mono mode, 2 s portamento from note 40 to 76, oversampling switched
    from 2x to 4x at 0.5 s:

        measured f0        0.6 s     1.0 s     1.5 s     2.0 s
        no switch (ref)    205.5 Hz  287.4 Hz  369.4 Hz  433.3 Hz
        pre-fix            585.2 Hz  657.8 Hz  658.7 Hz  658.7 Hz  (snapped)
        post-fix           205.9 Hz  288.6 Hz  371.1 Hz  435.2 Hz  (glides on)

    658.7 Hz is note 76: pre-fix the glide finished almost immediately after the
    switch. The gate compares the switched render against the unswitched one, so
    it needs no absolute pitch model -- only that the switch did not change the
    trajectory.
"""
import os
import sys
import numpy as np
from _harness import render, peak_hz, OUT

SR = 48000

CLEAN = dict(sr=SR, analogAmt=0, vintage=0, noiseLevel=0, cosmosChorus=0,
             driveOn=0, osc1Wave=3, osc2Level=0, osc3Level=0, subLevel=0,
             arpOn=0, reverbOn=0, delayOn=0, chorusOn=0,
             filterCutoff=8000, filterRes=0.1, filterEnvAmt=0,
             ampA=0.005, ampD=0.05, ampS=1.0, ampR=0.05, masterVol=-6)


def rms_db(x, sr, t0, t1):
    seg = x[int(t0 * sr):int(t1 * sr), 0]
    return 20.0 * np.log10(np.sqrt(np.mean(seg ** 2)) + 1e-20)


# --- A. bad parameter recovery ------------------------------------------------
BAD_T, RESTORE_T = 0.5, 1.0
HEALTHY_MIN_PEAK = 0.05      # the render has to be alive before the bad write
RECOVER_MIN_PEAK = 0.05      # ...and alive again after the good one
BAD_ROWS = (("masterVol", "nan", "0"), ("masterVol", "inf", "0"),
            ("masterVol", "1e30", "0"), ("filterCutoff", "nan", "8000"),
            ("stereoWidth", "inf", "0.5"))


def bad_parameter(fails):
    print("A. bad parameter value, then restore")
    print(f"   {'parameter':<14}{'bad':>6}{'healthy peak':>14}{'after restore':>15}"
          f"{'pre-fix':>9}   result")
    for name, bad, good in BAD_ROWS:
        _, x = render(0, 60, 2.0, 2, f"probust_{name}_{bad}",
                      setat=[f"{BAD_T}:{name}:{bad}", f"{RESTORE_T}:{name}:{good}"],
                      **CLEAN)
        healthy = float(np.max(np.abs(x[:int(0.4 * SR)])))
        recovered = float(np.max(np.abs(x[int(1.4 * SR):])))
        ok = healthy >= HEALTHY_MIN_PEAK and recovered >= RECOVER_MIN_PEAK
        if not ok:
            fails.append(f"A/{name}={bad}")
        print(f"   {name:<14}{bad:>6}{healthy:>14.5f}{recovered:>15.5f}"
              f"{0.0:>9.5f}   {'PASS' if ok else 'FAIL'}")


# --- B. meter floor -----------------------------------------------------------
METER_MIN_DB = -60.0
METER_EPS = 0.01             # the floor is exactly -60; allow float slop only


def meter_floor(fails):
    print("\nB. published output meters vs their declared -60 dB minimum")
    path = os.path.join(OUT, "probust_meters.txt")
    _, _ = render(0, 60, 3.0, 2, "probust_meters", release=0.3, meters=path,
                  **dict(CLEAN, ampR=0.3))
    m = np.loadtxt(path)
    lo_l, lo_r = float(m[:, 1].min()), float(m[:, 2].min())
    ok = lo_l >= METER_MIN_DB - METER_EPS and lo_r >= METER_MIN_DB - METER_EPS
    if not ok:
        fails.append("B")
    print(f"   {len(m)} blocks   lowest L {lo_l:.2f} dB   lowest R {lo_r:.2f} dB   "
          f"(min {METER_MIN_DB:.0f}, pre-fix -95.78)   {'PASS' if ok else 'FAIL'}")


# --- C. mode-crossfade note capture -------------------------------------------
SWITCH_T = 0.5
FADE_MS = 12.0
CROSS_BLOCK = 64             # see the module docstring: 512 collapses the offsets
CAPTURE_OFFSETS_MS = (2.0, 6.0, 10.0)
CONTROL_OFFSET_MS = 20.0
SOUNDING_DB = -40.0          # a captured note must reach at least this
SILENT_DB = -90.0


def crossfade_capture(fails):
    print(f"\nC. note-on inside the {FADE_MS:.0f} ms mode crossfade (block={CROSS_BLOCK})")
    print(f"   {'key at':>10}{'sustained dBFS':>17}{'pre-fix':>9}   result")
    for off_ms in CAPTURE_OFFSETS_MS + (CONTROL_OFFSET_MS,):
        t = SWITCH_T + off_ms / 1000.0
        _, x = render(0, 60, 2.0, 2, f"probust_cross_{off_ms}",
                      setat=f"{SWITCH_T}:mode:1", noteon=f"{t}:72",
                      block=CROSS_BLOCK, **CLEAN)
        lvl = rms_db(x, SR, 1.0, 1.9)
        ok = lvl > SOUNDING_DB
        if not ok:
            fails.append(f"C/+{off_ms:.0f}ms")
        tag = "-400.0" if off_ms in CAPTURE_OFFSETS_MS else " -19.3"
        print(f"   {f'+{off_ms:.0f} ms':>10}{lvl:>17.1f}{tag:>9}   "
              f"{'PASS' if ok else 'FAIL'}"
              f"{'' if off_ms in CAPTURE_OFFSETS_MS else '   (control, past the fade)'}")

    # A key-up after a replayed note must still release it.
    _, x = render(0, 60, 2.0, 2, "probust_cross_release",
                  setat=f"{SWITCH_T}:mode:1", noteon=f"{SWITCH_T + 0.006}:72",
                  noteoff="1.0:72", release=0.4, block=CROSS_BLOCK, **CLEAN)
    held = rms_db(x, SR, 0.7, 0.95)
    gone = rms_db(x, SR, 1.5, 1.9)
    rel_ok = held > SOUNDING_DB and gone < SILENT_DB
    if not rel_ok:
        fails.append("C/release")
    print(f"   key-up after the replay: held {held:.1f} dBFS -> {gone:.1f} dBFS   "
          f"{'PASS' if rel_ok else 'FAIL'}")

    # A fade CANCELLED before it commits must not replay the note a second time.
    # Switching back to the original mode inside the fade cancels it, so the
    # note-on is delivered normally and must sound exactly once -- at the same
    # level as the identical render with no mode switch at all.
    _, xc = render(0, 60, 2.0, 2, "probust_cross_cancel",
                   setat=[f"{SWITCH_T}:mode:1", f"{SWITCH_T + 0.004}:mode:0"],
                   noteon=f"{SWITCH_T + 0.006}:72", block=CROSS_BLOCK, **CLEAN)
    _, xn = render(0, 60, 2.0, 2, "probust_cross_noswitch",
                   noteon=f"{SWITCH_T + 0.006}:72", block=CROSS_BLOCK, **CLEAN)
    cancelled = rms_db(xc, SR, 1.0, 1.9)
    plain = rms_db(xn, SR, 1.0, 1.9)
    # A double trigger stacks a second voice: same pitch, so it shows up as
    # level, not as a new partial. 1.5 dB is well under the ~3 dB a duplicate
    # voice adds and well over the run-to-run spread (0.0 dB, deterministic).
    cancel_ok = abs(cancelled - plain) <= 1.5
    if not cancel_ok:
        fails.append("C/cancel")
    print(f"   cancelled fade: {cancelled:.1f} dBFS vs {plain:.1f} dBFS with no "
          f"switch (delta {cancelled - plain:+.2f} dB, max 1.5)   "
          f"{'PASS' if cancel_ok else 'FAIL'}")


# --- D. portamento across an oversampling switch ------------------------------
GLIDE_SECONDS = 2.5
GLIDE_FROM, GLIDE_TO = 40, 76
GLIDE_START_T = 0.2
OS_SWITCH_T = 0.5
GLIDE_PROBE_T = (0.6, 1.0, 1.5, 2.0)
GLIDE_TOL = 0.02             # 2% of the reference frequency at each probe point
PRE_FIX_GLIDE_HZ = (585.2, 657.8, 658.7, 658.7)

GLIDE = dict(CLEAN, portaTime=2.0, legato=0, glideMode=0, ampR=0.2,
             filterCutoff=12000)


def glide_track(name, setat):
    _, x = render(2, GLIDE_FROM, GLIDE_SECONDS, 2, name,
                  noteon=f"{GLIDE_START_T}:{GLIDE_TO}", setat=setat, **GLIDE)
    return [peak_hz(x[int(t * SR):int(t * SR) + 8192, 0], SR, 30.0)
            for t in GLIDE_PROBE_T]


def portamento_os(fails):
    print("\nD. portamento held across an oversampling switch (2 s glide, 2x -> 4x)")
    ref = glide_track("probust_glide_ref", [f"{OS_SWITCH_T}:masterTune:0"])
    got = glide_track("probust_glide_os",
                      [f"{OS_SWITCH_T}:masterTune:0", f"{OS_SWITCH_T}:oversampling:2"])
    print(f"   {'t':>6}{'no switch Hz':>14}{'switched Hz':>13}{'err':>8}"
          f"{'pre-fix Hz':>12}   result")
    ok_all = True
    for t, r, g, pre in zip(GLIDE_PROBE_T, ref, got, PRE_FIX_GLIDE_HZ):
        err = g / r - 1.0
        ok = abs(err) <= GLIDE_TOL
        ok_all = ok_all and ok
        print(f"   {t:>6.1f}{r:>14.1f}{g:>13.1f}{err * 100:>7.1f}%{pre:>12.1f}   "
              f"{'PASS' if ok else 'FAIL'}")
    if not ok_all:
        fails.append("D")


def main():
    fails = []
    bad_parameter(fails)
    meter_floor(fails)
    crossfade_capture(fails)
    portamento_os(fails)
    print(f"\nparam_robust_gate: {'PASS' if not fails else 'FAIL (' + ','.join(fails) + ')'}")
    sys.exit(0 if not fails else 1)


if __name__ == "__main__":
    main()
