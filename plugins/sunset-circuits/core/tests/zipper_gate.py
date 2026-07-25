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

PRESET LOADS
    Smoothing must not make program changes glide. The shell's loadProgram() only
    calls setParameter() repeatedly, so the engine classifies each snapshot: many
    smoothed controls jumping a large amount in one block is a preset load and
    snaps, one control moving is a knob and glides. Both branches are asserted at
    the end of this gate.
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

# label, extra patch, (param, lo, hi), limit dB, pre-smoothing dB (calibration)
CASES = [
    ("masterVol",    OPEN,                                          ("masterVol", -12, 0),      -65.0, -28.94),
    ("masterPan",    OPEN,                                          ("masterPan", -0.8, 0.8),   -65.0, -27.76),
    ("stereoWidth",  dict(OPEN, masterPan=0.4),                     ("stereoWidth", 0.0, 1.0),  -65.0, -30.41),
    ("filterCutoff", dict(filterCutoff=800, filterRes=0.3),         ("filterCutoff", 400, 1600), -90.0, -61.24),
    ("filterRes",    dict(filterCutoff=800, filterRes=0.3),         ("filterRes", 0.05, 0.9),   -70.0, -31.96),
    ("filterEnvAmt", dict(filterCutoff=800, filterRes=0.3, filterEnvAmt=0.3),
                                                                    ("filterEnvAmt", -0.8, 0.8), -85.0, -56.44),
    ("osc1Level",    OPEN,                                          ("osc1Level", 0.2, 1.0),    -70.0, -38.64),
    ("subLevel",     dict(OPEN, subLevel=0.5, subWave=1),           ("subLevel", 0.1, 1.0),     -70.0, -36.54),
    ("driveAmt",     dict(OPEN, driveOn=1, driveAmt=0.3, driveMix=1.0),
                                                                    ("driveAmt", 0.05, 0.9),    -65.0, -35.15),
    ("driveMix",     dict(OPEN, driveOn=1, driveAmt=0.6, driveMix=1.0),
                                                                    ("driveMix", 0.0, 1.0),     -65.0, -31.38),
    ("chorusMix",    dict(OPEN, chorusOn=1, chorusMix=0.5),         ("chorusMix", 0.0, 1.0),    -62.0, -26.56),
    ("chorusDepth",  dict(OPEN, chorusOn=1, chorusMix=1.0),         ("chorusDepth", 0.1, 1.0),  -60.0, -27.53),
    ("delayMix",     dict(OPEN, delayOn=1, delayMix=0.4),           ("delayMix", 0.0, 0.9),     -55.0, -23.29),
    ("delayFB",      dict(OPEN, delayOn=1, delayMix=0.5),           ("delayFB", 0.05, 0.9),     -60.0, -25.83),
    ("reverbMix",    dict(OPEN, reverbOn=1, reverbMix=0.4),         ("reverbMix", 0.0, 0.9),    -60.0, -25.13),
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


def step_case(label, patch, steps):
    param, lo, hi = steps
    _, ctl = render(2, 45, SECONDS, 2, f"zip_{label}_ctl", **dict(BASE, **patch))
    _, stp = render(2, 45, SECONDS, 2, f"zip_{label}_step",
                    setat=step_args(param, lo, hi), **dict(BASE, **patch))
    return out_of_band_db(ctl), out_of_band_db(stp)


# --- preset-load snap check ---------------------------------------------------
# masterVol drops 24 dB at CHANGE_T. Alone that is a knob move and must glide;
# accompanied by five more large jumps it is a preset load and must land at once.
# The five companions are FX controls whose effects are switched off, so they move
# the classifier without touching the audio being measured.
CHANGE_T = 1.5
SNAP_NOTE = 81                        # A5 -- ~1.8 cycles in the 2 ms probe window
SNAP_PATCH = dict(BASE, **dict(OPEN, ampR=1.0))
BULK_COMPANIONS = [("driveAmt", 0.95), ("driveMix", 0.0), ("chorusMix", 0.95),
                   ("delayMix", 0.95), ("reverbMix", 0.95)]


def snap_probe(label, companions):
    """dB by which the first 2 ms after the change still exceeds the settled level.

    0 dB means the new gain applied immediately (snapped); a glide is still near
    the OLD level 2 ms in, so it reads strongly positive.
    """
    setat = [f"{CHANGE_T}:masterVol:-24"] + \
            [f"{CHANGE_T}:{n}:{v}" for n, v in companions]
    _, x = render(2, SNAP_NOTE, 3.0, 2, f"zip_snap_{label}", setat=setat, **SNAP_PATCH)
    mono = x.mean(axis=1)
    # render_test fires a scheduled change on the first block starting at/after
    # its time, so the change lands exactly on this frame.
    frame = int(np.ceil(int(CHANGE_T * SR) / BLOCK) * BLOCK)
    win = mono[frame:frame + int(0.002 * SR)]
    settled = mono[frame + int(0.20 * SR):frame + int(0.30 * SR)]
    rms = lambda v: float(np.sqrt(np.mean(v ** 2)) + 1e-30)
    return 20.0 * np.log10(rms(win) / rms(settled))


def main():
    print(f"{'parameter':<14}{'floor':>9}{'stepped':>10}{'limit':>9}"
          f"{'pre-fix':>10}   result")
    failures = 0
    for label, patch, steps, limit, prefix_db in CASES:
        floor, stepped = step_case(label, patch, steps)
        ok = stepped <= limit
        failures += not ok
        print(f"{label:<14}{floor:>9.2f}{stepped:>10.2f}{limit:>9.1f}"
              f"{prefix_db:>10.2f}   {'PASS' if ok else 'FAIL'}")

    print("\n(floor = same patch held still; pre-fix = same probe before parameter "
          "smoothing existed)")

    print("\npreset-load handling (masterVol -24 dB at t=1.5 s):")
    bulk = snap_probe("bulk", BULK_COMPANIONS)     # 6 large jumps -> snap
    solo = snap_probe("solo", [])                  # 1 large jump  -> glide
    bulk_ok = abs(bulk) < 3.0
    solo_ok = solo > 10.0
    failures += (not bulk_ok) + (not solo_ok)
    print(f"  with 5 companion jumps  {bulk:+7.2f} dB above settled  "
          f"(want |x| < 3, i.e. snapped)   {'PASS' if bulk_ok else 'FAIL'}")
    print(f"  masterVol alone         {solo:+7.2f} dB above settled  "
          f"(want x > 10, i.e. glided)    {'PASS' if solo_ok else 'FAIL'}")

    total = len(CASES) + 2
    print(f"\nzipper_gate: {total - failures}/{total} "
          f"({'PASS' if failures == 0 else 'FAIL'})")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
