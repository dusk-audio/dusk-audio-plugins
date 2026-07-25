#!/usr/bin/env python3
"""Zipper-noise gate — parameter smoothing regression test.

The engine snapshots its parameters once per block, so any continuous control
that is used directly from that snapshot STEPS once per block: ~94 discontinuities
a second at 512 frames / 48 kHz. Automate or drag such a control and the steps
splatter broadband energy across the spectrum -- zipper noise.

MEASUREMENT
    A pure sine voice (osc1 sine, flat amp envelope, no filter envelope, no analog
    drift, no vintage noise) is rendered while ONE parameter is stepped hard
    between two extremes on EVERY block boundary -- the worst case a host can
    produce. A clean voice has essentially nothing above a few kHz, so

        out-of-band ratio = energy above 4 kHz / total energy   (worst channel)

    is the stepping artifact and nothing else. The same patch rendered with the
    parameter held still gives the measurement floor. Per channel rather than the
    mono sum: masterPan is symmetric in (panL + panR), so a +/-p pan step cancels
    in L+R and only shows up channel by channel.

    Thresholds sit ~10 dB above the smoothed result and ~30 dB below the
    pre-smoothing result, so a regression fails loudly and normal noise cannot
    flake it. Both calibration numbers are in the table below and are printed on
    every run.

    Two parameters cannot be judged this way and get their own probes:
    noiseLevel (a noise source is broadband by nature, so an out-of-band metric
    is blind to it -- see ENVELOPE PROBE) and anything whose test signal is not a
    clean tone.

ENVELOPE PROBE
    For noiseLevel the artifact lives in the AMPLITUDE, not the spectrum, so the
    power envelope is folded at the 2-block modulation period. Content that is
    not locked to the block grid averages away; a stepped gain leaves a square
    edge in the folded envelope while a smoothed one leaves a rounded ramp, and
    the edge shows up as HF content in the fold.

FALSE POSITIVES
    De-zippering is only half the job: the preset-load classifier must NOT fire on
    ordinary automation, because snapping every block is exactly the zipper it was
    meant to remove. The ramp probe sweeps six witness parameters together in small
    per-block increments and asserts the glide path held. Its teeth are calibrated
    against the same render with a program change forced on every block.

PRESET LOADS
    Smoothing must not make program changes glide. notifyProgramChange() is the
    real signal the shell raises; the bulk-change heuristic is only a fallback for
    hosts that replay a stored patch as raw parameter writes. All three paths --
    glide, explicit snap, fallback snap -- are asserted at the end of this gate.
"""
import sys
import numpy as np
from _harness import render

SR = 48000
BLOCK = 512          # render_test's fixed block size
SECONDS = 4.0
ANALYSIS_START = 1.0
HF_HZ = 4000.0

# Pure sine voice: one oscillator, no envelope movement, no noise sources.
BASE = dict(sr=SR, analogAmt=0, unisonVoices=1, vintage=0,
            osc1Wave=3, osc1Level=1.0,
            osc2Level=0.0, osc3Level=0.0, subLevel=0.0, noiseLevel=0.0,
            ampA=0.001, ampD=0.01, ampS=1.0, ampR=0.3,
            filterEnvAmt=0.0, filtA=0.001, filtD=0.01, filtS=1.0,
            masterVol=0.0, masterPan=0.0, stereoWidth=0.5)

OPEN = dict(filterCutoff=12000, filterRes=0.1)   # filter out of the way

# label, mode, patch, (param, lo, hi), limit dB, pre-smoothing dB (calibration).
# Mode 2 (Mono) unless the parameter needs another engine: filterHP is only wired
# up in mode 0 (Cosmos) -- modes 1-3 ignore the HP argument entirely, so probing
# it in Mono measures nothing at all.
CASES = [
    ("masterVol",    2, OPEN,                                       ("masterVol", -12, 0),      -65.0, -28.94),
    ("masterPan",    2, OPEN,                                       ("masterPan", -0.8, 0.8),   -65.0, -27.76),
    ("stereoWidth",  2, dict(OPEN, masterPan=0.4),                  ("stereoWidth", 0.0, 1.0),  -65.0, -30.41),
    ("filterCutoff", 2, dict(filterCutoff=800, filterRes=0.3),      ("filterCutoff", 400, 1600), -90.0, -61.24),
    ("filterRes",    2, dict(filterCutoff=800, filterRes=0.3),      ("filterRes", 0.05, 0.9),   -70.0, -31.96),
    ("filterHP",     0, dict(OPEN, filterHP=200),                   ("filterHP", 60, 900),      -85.0, -57.66),
    ("filterEnvAmt", 2, dict(filterCutoff=800, filterRes=0.3, filterEnvAmt=0.3),
                                                                    ("filterEnvAmt", -0.8, 0.8), -85.0, -56.44),
    ("osc1Level",    2, OPEN,                                       ("osc1Level", 0.2, 1.0),    -70.0, -38.64),
    ("osc2Level",    2, dict(OPEN, osc2Level=0.8, osc2Wave=3, osc2Semi=0),
                                                                    ("osc2Level", 0.1, 1.0),    -70.0, -38.02),
    # osc3 is only mixed by Modular (mode 3), and it must be the ONLY voice: with
    # osc1 also up, activeGain normalisation (Voice.hpp: mix /= activeGain when the
    # sum exceeds 1) cancels an osc3Level change exactly and the probe reads a
    # perfect null on a broken build. Alone, activeGain <= 1 so no division occurs.
    ("osc3Level",    3, dict(OPEN, osc1Level=0.0, osc2Level=0.0, osc3Wave=3, osc3Level=0.5),
                                                                    ("osc3Level", 0.1, 1.0),    -65.0, -31.08),
    ("subLevel",     2, dict(OPEN, subLevel=0.5, subWave=1),        ("subLevel", 0.1, 1.0),     -70.0, -36.54),
    # Acid (mode 5) renders through its own mono voice, outside the poly path.
    ("acid cutoff",  5, dict(arpOn=0, filterCutoff=800, filterRes=0.3, ampD=5.0, ampS=1.0),
                                                                    ("filterCutoff", 400, 1600), -90.0, -58.49),
    ("driveAmt",     2, dict(OPEN, driveOn=1, driveAmt=0.3, driveMix=1.0),
                                                                    ("driveAmt", 0.05, 0.9),    -65.0, -35.15),
    ("driveMix",     2, dict(OPEN, driveOn=1, driveAmt=0.6, driveMix=1.0),
                                                                    ("driveMix", 0.0, 1.0),     -65.0, -31.38),
    ("chorusMix",    2, dict(OPEN, chorusOn=1, chorusMix=0.5),      ("chorusMix", 0.0, 1.0),    -62.0, -26.56),
    ("chorusDepth",  2, dict(OPEN, chorusOn=1, chorusMix=1.0),      ("chorusDepth", 0.1, 1.0),  -60.0, -27.53),
    ("delayMix",     2, dict(OPEN, delayOn=1, delayMix=0.4),        ("delayMix", 0.0, 0.9),     -55.0, -23.29),
    ("delayFB",      2, dict(OPEN, delayOn=1, delayMix=0.5),        ("delayFB", 0.05, 0.9),     -60.0, -25.83),
    ("reverbMix",    2, dict(OPEN, reverbOn=1, reverbMix=0.4),      ("reverbMix", 0.0, 0.9),    -60.0, -25.13),
]


def step_args(param, lo, hi, seconds=SECONDS):
    """One setat per block boundary, alternating lo/hi -- worst-case automation."""
    return [f"{b * BLOCK / SR:.9f}:{param}:{lo if b % 2 == 0 else hi}"
            for b in range(int(seconds * SR) // BLOCK)]


def out_of_band_db(x, hf_hz=HF_HZ):
    """Worst-channel energy above hf_hz relative to that channel's total, in dB."""
    seg = x[int(ANALYSIS_START * SR):]
    worst = -300.0
    for ch in range(seg.shape[1]):
        c = seg[:, ch]
        spec = np.abs(np.fft.rfft(c * np.hanning(len(c)))) ** 2
        f = np.fft.rfftfreq(len(c), 1.0 / SR)
        worst = max(worst, 10.0 * np.log10((spec[f >= hf_hz].sum() + 1e-30) /
                                           (spec.sum() + 1e-30)))
    return worst


def step_case(label, mode, patch, steps):
    param, lo, hi = steps
    _, ctl = render(mode, 45, SECONDS, 2, f"zip_{label}_ctl", **dict(BASE, **patch))
    _, stp = render(mode, 45, SECONDS, 2, f"zip_{label}_step",
                    setat=step_args(param, lo, hi), **dict(BASE, **patch))
    return out_of_band_db(ctl), out_of_band_db(stp)


# --- noiseLevel: envelope probe -----------------------------------------------
# A noise source is broadband, so the out-of-band metric cannot separate a gain
# step from the signal (measured: stepped -33.75 dB against a -34.59 dB floor,
# i.e. nothing). Fold the POWER envelope at the modulation period instead: a
# stepped gain leaves a square edge, a smoothed one a rounded ramp. 10 s rather
# than 4 s because the fold's floor falls as 1/sqrt(periods) and the separation
# is only ~5 dB at 4 s.
NOISE_SECONDS = 10.0
NOISE_LIMIT = -19.0
NOISE_PREFIX_DB = -14.82


def env_fold_db(x):
    """HF content of the block-synchronously folded power envelope, in dB."""
    seg = x[int(ANALYSIS_START * SR):, 0] ** 2
    period = 2 * BLOCK
    n = len(seg) // period
    folded = seg[:n * period].reshape(n, period).mean(axis=0)
    spec = np.fft.rfft(folded - folded.mean())
    f = np.fft.rfftfreq(period, 1.0 / SR)
    hf = np.sqrt((np.abs(spec[f >= 1000.0]) ** 2).sum())
    return 20.0 * np.log10(hf / (folded.mean() * period / 2 + 1e-30))


def noise_case():
    patch = dict(BASE, **dict(OPEN, osc1Level=0.0, noiseLevel=0.3))
    _, ctl = render(2, 45, NOISE_SECONDS, 2, "zip_noise_ctl", **patch)
    _, stp = render(2, 45, NOISE_SECONDS, 2, "zip_noise_step",
                    setat=step_args("noiseLevel", 0.05, 0.6, NOISE_SECONDS), **patch)
    return env_fold_db(ctl), env_fold_db(stp)


# --- false-positive probe -----------------------------------------------------
# Six witness parameters swept together, a full range every 0.5 s -- fast but
# entirely ordinary automation, ~0.021 of range per block against a 0.267
# threshold. The classifier must stay on the GLIDE path; if it decided this was a
# preset load it would snap every block and put the zipper straight back. The
# limit is calibrated against exactly that failure, reproduced by forcing a
# program change on every block (FP_SNAPPED_DB).
FP_RAMP = ["masterVol", "filterCutoff", "osc1Level", "driveMix", "chorusMix", "reverbMix"]
FP_LO = {"masterVol": -12.0, "filterCutoff": 2000.0, "osc1Level": 0.3,
         "driveMix": 0.0, "chorusMix": 0.0, "reverbMix": 0.0}
FP_HI = {"masterVol": 0.0, "filterCutoff": 9000.0, "osc1Level": 1.0,
         "driveMix": 1.0, "chorusMix": 1.0, "reverbMix": 0.9}
FP_LIMIT = -65.0
FP_SNAPPED_DB = -52.58      # same render with a program change forced every block


def fp_probe():
    setat = []
    for b in range(int(SECONDS * SR) // BLOCK):
        t = b * BLOCK / SR
        tri = 2.0 * abs((t / 0.5) % 1.0 - 0.5)      # 0..1 triangle
        for n in FP_RAMP:
            setat.append(f"{t:.9f}:{n}:{FP_LO[n] + (FP_HI[n] - FP_LO[n]) * tri:.6f}")
    patch = dict(BASE, **dict(OPEN, driveOn=1, driveAmt=0.4, chorusOn=1, reverbOn=1))
    _, x = render(2, 45, SECONDS, 2, "zip_fp_ramp", setat=setat, **patch)
    return out_of_band_db(x)


# --- preset-load snap check ---------------------------------------------------
# masterVol drops 24 dB at CHANGE_T and the probe asks how much of that drop has
# arrived a few ms later. Three cases pin down all three paths:
#
#   1. alone, no notify  -> a knob move. Must GLIDE (still near the old level).
#   2. alone + notify    -> the explicit program-change signal the shell raises
#                           from loadProgram() and the UI preset paths. Must SNAP.
#   3. six large jumps   -> the fallback heuristic for hosts that replay a stored
#                           patch as raw parameter writes. Must SNAP.
#
# Case 2 is the one that matters in a DAW: measured over all 2862 ordered factory
# preset pairs the heuristic alone fires on 74 (2.6%), because loadProgram()
# rewrites every parameter to default + kPresetBaseline before applying the
# preset's rows, so any two presets agree on nearly every witness.
CHANGE_T = 1.5
SNAP_NOTE = 93                        # A6 = 1760 Hz; 48000/1760 = 300/11 exactly,
SNAP_CYCLES = 11                      # so 11 cycles is EXACTLY 300 samples and the
SNAP_WIN = 300                        # window carries no partial-cycle RMS slop.
SNAP_PATCH = dict(BASE, **dict(OPEN, ampR=1.0))
BULK_COMPANIONS = [("driveAmt", 0.95), ("driveMix", 0.0), ("chorusMix", 0.95),
                   ("delayMix", 0.95), ("reverbMix", 0.95)]


def snap_probe(label, companions=(), notify=False):
    """dB by which the probe window still exceeds the settled level.

    0 dB means the new gain applied immediately (snapped); an 8 ms one-pole is
    still near the OLD level this early, so a glide reads strongly positive.
    """
    setat = [f"{CHANGE_T}:masterVol:-24"] + \
            [f"{CHANGE_T}:{n}:{v}" for n, v in companions]
    kw = dict(SNAP_PATCH)
    if notify:
        kw["notifyat"] = CHANGE_T
    _, x = render(2, SNAP_NOTE, 3.0, 2, f"zip_snap_{label}", setat=setat, **kw)
    mono = x.mean(axis=1)
    # render_test fires a scheduled change on the first block starting at/after
    # its time, so the change lands exactly on this frame.
    frame = int(np.ceil(int(CHANGE_T * SR) / BLOCK) * BLOCK)
    win = mono[frame:frame + SNAP_WIN]
    settled = mono[frame + int(0.20 * SR):frame + int(0.30 * SR)]
    rms = lambda v: float(np.sqrt(np.mean(v ** 2)) + 1e-30)
    return 20.0 * np.log10(rms(win) / rms(settled))


def main():
    print(f"{'parameter':<14}{'mode':>5}{'floor':>10}{'stepped':>10}{'limit':>9}"
          f"{'pre-fix':>10}   result")
    failures = 0
    for label, mode, patch, steps, limit, prefix_db in CASES:
        floor, stepped = step_case(label, mode, patch, steps)
        ok = stepped <= limit
        failures += not ok
        print(f"{label:<14}{mode:>5}{floor:>10.2f}{stepped:>10.2f}{limit:>9.1f}"
              f"{prefix_db:>10.2f}   {'PASS' if ok else 'FAIL'}")

    # noiseLevel needs the envelope probe: an out-of-band metric is blind to a
    # broadband source (see ENVELOPE PROBE in the module docstring).
    n_floor, n_stepped = noise_case()
    n_ok = n_stepped <= NOISE_LIMIT
    failures += not n_ok
    print(f"{'noiseLevel*':<14}{2:>5}{n_floor:>10.2f}{n_stepped:>10.2f}"
          f"{NOISE_LIMIT:>9.1f}{NOISE_PREFIX_DB:>10.2f}   {'PASS' if n_ok else 'FAIL'}")

    print("\n(floor = same patch held still; pre-fix = same probe before parameter "
          "smoothing existed)")
    print(f"(* noiseLevel is the folded power-envelope probe over {NOISE_SECONDS:.0f} s, "
          f"not the out-of-band ratio -- noise is broadband)")

    fp = fp_probe()
    fp_ok = fp <= FP_LIMIT
    failures += not fp_ok
    print(f"\nfalse positives (6 witness lanes swept together, full range every 0.5 s):")
    print(f"  glide path held         {fp:+7.2f} dB   (limit {FP_LIMIT:.1f}; forced snap "
          f"every block reads {FP_SNAPPED_DB:.2f}){'   PASS' if fp_ok else '   FAIL'}")

    print(f"\npreset-load handling (masterVol -24 dB at t=1.5 s, "
          f"{SNAP_CYCLES}-cycle / {SNAP_WIN}-sample window):")
    checks = [
        ("knob move (no notify)",   snap_probe("solo"),
         lambda v: v > 10.0,  "want x > 10, i.e. glided"),
        ("notifyProgramChange()",   snap_probe("notify", notify=True),
         lambda v: abs(v) < 2.0, "want |x| < 2, i.e. snapped"),
        ("fallback: 6 large jumps", snap_probe("bulk", BULK_COMPANIONS),
         lambda v: abs(v) < 2.0, "want |x| < 2, i.e. snapped"),
    ]
    for name, val, pred, want in checks:
        ok = pred(val)
        failures += not ok
        print(f"  {name:<24}{val:+7.2f} dB above settled  ({want})"
              f"{'   PASS' if ok else '   FAIL'}")

    total = len(CASES) + len(checks) + 2   # + noiseLevel + false-positive probe
    print(f"\nzipper_gate: {total - failures}/{total} "
          f"({'PASS' if failures == 0 else 'FAIL'})")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
