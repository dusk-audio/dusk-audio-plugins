#!/usr/bin/env python3
"""FX-stage aliasing report (no hard gate).

alias_gate.py measures the VOICE path, which renders at host*os and halfband
decimates. The EffectsChain (Effects.hpp) runs AFTER that decimation, at plain
host rate, and contains memoryless nonlinearities with no oversampling of their
own:

    DriveEffect::saturate   tanh / hard clip / asymmetric exp  (Effects.hpp ~59)
    DelayEffect             tanh(fb * 1.1) tape character      (Effects.hpp ~357)
                            softClamp() on the write path      (Effects.hpp ~362)

Anything those generate above 24 kHz folds straight back into the audible band
and the voice oversampling factor cannot help. This report quantifies that.

Two independent methods, deliberately. They agree to 0.4 dB where they overlap
(the cross-check block at the end of section 1), which is what makes either one
trustworthy.

  IN-ENGINE (sections 1 and 2). Play a bright saw at a high note and measure the
  worst non-harmonic bin between 50 Hz and 15 kHz, in dB relative to the
  STRONGEST harmonic -- alias_gate's convention, so the numbers sit next to its
  published -47 dBc voice-path reference. The reference is the strongest
  harmonic rather than the fundamental because drive routinely pushes h2/h3
  past h1, and referencing a suppressed fundamental would inflate every image.
  The controlling experiment for "would oversampling the FX stage fix it" is the
  192 kHz column: rendering at sr=192000 with os=1 puts the voices at the same
  192 kHz internal rate as the shipped 48 kHz/4x path but ALSO runs the effects
  at 192 kHz, so decimating that offline to 48 kHz changes exactly one variable
  -- the rate the nonlinearity is evaluated at.

  OFFLINE RESIDUAL (section 3). The in-engine metric needs a single harmonic
  series to measure against, and real presets have detuned oscillators, unison,
  a sub, noise and FM, all legitimately inharmonic. So instead: capture the
  exact signal the drive stage receives, run DriveEffect's own nonlinearity over
  it at 48 kHz and at 8x, and difference the two. Same input sample for sample,
  so the difference is fold-back and nothing else, whatever the material.
  Section 0 proves the Python model IS the shipped nonlinearity before any of
  this is believed.

Report only. Run standalone or via run_all.sh. Runtime is a few minutes; the
preset sweep is most of it.

Usage:
    fx_alias_gate.py                # everything
    fx_alias_gate.py --no-presets   # sections 0-2 only (fast)
    fx_alias_gate.py --json         # machine-readable
"""
import contextlib
import json
import os
import re
import subprocess
import sys

import numpy as np
import soundfile as sf

try:
    from scipy.signal import resample_poly
except ImportError:      # scipy is not a dependency of the other gates
    resample_poly = None

from _harness import render

HERE = os.path.dirname(os.path.abspath(__file__))
PRESET_BIN = os.path.join(HERE, "build", "preset_render")
PARAMS_HPP = os.path.join(HERE, "..", "..", "dpf-plugin", "MultiSynthParams.hpp")
OUT = "/tmp/msynth_fxalias"
os.makedirs(OUT, exist_ok=True)

# alias_gate's published voice-path reference at 2x oversampling, quoted by the
# QA checklist as the engineering guide for the ear pass.
#
# CONVENTION WARNING. alias_gate measures images re the FUNDAMENTAL; this file
# measures them re the STRONGEST HARMONIC (sections 1-2) and re the strongest
# bin of the alias-free reference (section 3). The three coincide only while the
# fundamental is the strongest partial, which is true of the undriven voice path
# and false once drive is engaged. So this constant is a documented FALLBACK
# only: section 1 re-measures the same voice path under this file's own
# convention every run ("drive OFF" row) and every later comparison uses that
# measured number, not this one. They have agreed to 0.1 dB so far, which is
# why the constant is still worth carrying as a drift tripwire.
VOICE_REF_2X_DBC = -47.0

HOST_SR = 48000
CTRL_SR = 192000          # 4x host: the "effects also oversampled" control
BAND_LO, BAND_HI = 50.0, 15000.0
# Fold-back that lands at 14 kHz is far less audible than fold-back at 2 kHz, so
# every row also carries the worst image below 8 kHz -- the band where an
# inharmonic tone has no masker anywhere near it and reads as "grit".
CRIT_HI = 8000.0
# The default resample_poly window (kaiser beta 5) has ~0.02 dB of passband
# ripple, which shows up as a -53 dBc floor in the residual -- the same order as
# the thing being measured. beta 14 puts the round-trip floor below -100 dBc.
# Verified per run by self_check(); see the "method floor" line in the summary.
RESAMPLE_WIN = ("kaiser", 14.0)

DRIVE_NAMES = {0: "SoftClip", 1: "HardClip", 2: "Tube"}

kOutputMakeup = 2.51188643   # +8 dB output makeup, MultiSynthDSP.cpp:672
# Master volume used for every render in this file. Low enough that the output
# stage's softLimit() is provably identity (see PATCH), and exactly unwound
# again by capture_scale() when a render is used as a drive-stage capture.
CAPTURE_VOL_DB = -30.0

# Same note and patch as alias_gate so the voice-path row is directly
# comparable: MIDI 108 saw, filter wide open, no chorus/reverb/analog drift.
#
# masterVol is pinned at CAPTURE_VOL_DB for one reason: softLimit() in the
# output stage (MultiSynthDSP.cpp:874) is a THIRD host-rate memoryless
# nonlinearity, and any fold-back it contributes would be silently booked
# against the drive. At the shipped 0 dB plus the +8 dB makeup, the driven
# renders peak around 0.67 against softLimit's 0.9 knee -- close enough that a
# hotter patch or a future default would cross it without anyone noticing.
# Pinning the level puts ~25 dB between the two. The drive stage runs BEFORE
# the master gain, so this is a pure output scaling and every dBc in this file
# is invariant to it (asserted per row by assert_linear_output()).
NOTE = 108
PATCH = dict(osc1Wave=0, osc2Level=0, subLevel=0, osc3Level=0, noiseLevel=0,
             analogAmt=0, filterEnvAmt=0, filterCutoff=16000, filterRes=0,
             cosmosChorus=0, ampA=0.005, ampD=0.01, ampS=1.0, reverbOn=0,
             chorusOn=0, delayOn=0, vintage=0, masterVol=CAPTURE_VOL_DB)

# softLimit() is identity below this; anything at or above it means the output
# stage is shaping the signal and the row's fold-back is not the drive's alone.
SOFTLIMIT_KNEE = 0.9


# ---------------------------------------------------------------------------
# measurement
# ---------------------------------------------------------------------------
def worst_image_db(sig, sr, f0, lo_hz=BAND_LO, hi_hz=BAND_HI):
    """Worst non-harmonic bin in [lo,hi], dB re the strongest harmonic."""
    w = np.hanning(len(sig))
    X = np.abs(np.fft.rfft(sig * w))
    f = np.fft.rfftfreq(len(sig), 1.0 / sr)
    tol = f0 * 0.03

    # Reference = strongest harmonic in band. Drive routinely pushes h2/h3 past
    # the fundamental, and referencing a suppressed fundamental would inflate
    # every image by the amount of that suppression.
    ref = 0.0
    h = 1
    while h * f0 < min(hi_hz, sr * 0.5):
        k = int(np.argmin(np.abs(f - h * f0)))
        ref = max(ref, float(X[k]))
        h += 1
    if ref <= 0.0:
        return -200.0, 0.0

    lo = np.searchsorted(f, lo_hz)
    hi = np.searchsorted(f, hi_hz)
    # h == 0 is NOT a harmonic: masking it would hide every image below tol Hz.
    hh = np.round(f[lo:hi] / f0)
    mask = (hh >= 1) & (np.abs(f[lo:hi] - hh * f0) < tol)
    band = np.where(mask, 0.0, X[lo:hi])
    k = int(np.argmax(band))
    db = 20.0 * np.log10(band[k] / ref + 1e-20)
    return float(db), float(f[lo + k])


def analysis_segment(x, sr, frac_lo=0.35, frac_hi=0.95):
    """Steady-state LEFT-channel slice (drops the attack and the release tail).

    Left channel, not a mono sum, for the same reason capture_drive_input() uses
    it: every nonlinearity in the chain is per-channel, so a summed signal is
    f((L+R)/2), which is not what the engine evaluates.
    """
    n = len(x)
    seg = x[int(frac_lo * n):int(frac_hi * n)]
    return seg[:, 0] if seg.ndim > 1 else seg


def to_host_rate(x, sr):
    """Decimate a control render to the host rate with a proper anti-alias FIR."""
    if sr == HOST_SR:
        return x, HOST_SR
    q = sr // HOST_SR
    return resample_poly(x, 1, q, axis=0, window=RESAMPLE_WIN), HOST_SR


def note_hz(n):
    return 440.0 * 2.0 ** ((n - 69) / 12.0)


def assert_linear_output(x, name):
    """Fail loudly if a render reached the output stage's softLimit() knee.

    Every dBc in sections 1-2 is attributed to the drive or the delay. That
    attribution is only valid while softLimit() (MultiSynthDSP.cpp:874) is
    identity, so this is checked rather than assumed -- on the FULL render, not
    the analysis window, since the peak may sit in the attack.
    """
    peak = float(np.max(np.abs(x)))
    if peak >= SOFTLIMIT_KNEE:
        raise RuntimeError(
            f"{name}: peak {peak:.3f} reached softLimit()'s {SOFTLIMIT_KNEE} knee -- "
            "the output stage is shaping this render, so its fold-back is not the "
            "drive stage's alone. Lower CAPTURE_VOL_DB.")
    return peak


def analyse(seg, sr, f0, peak):
    """(worst dBc, its Hz, worst dBc below 8 kHz, RMS dBFS, peak dBFS)."""
    rms = 20.0 * np.log10(float(np.sqrt(np.mean(seg ** 2))) + 1e-20)
    db, hz = worst_image_db(seg, sr, f0)
    crit, _ = worst_image_db(seg, sr, f0, hi_hz=CRIT_HI)
    return db, hz, crit, rms, 20.0 * np.log10(peak + 1e-20)


def measure(mode, note, seconds, osf, name, control=False, **params):
    """Render and return (worst_dbc, hz, worst_dbc_below_8k, rms_dbfs, peak_dbfs)."""
    p = dict(params)
    if control:
        p["sr"] = CTRL_SR
        sr, x = render(mode, note, seconds, 1, name, **p)
        peak = assert_linear_output(x, name)
        x, sr = to_host_rate(x, sr)
    else:
        sr, x = render(mode, note, seconds, osf, name, **p)
        peak = assert_linear_output(x, name)
    return analyse(analysis_segment(x, sr), sr, note_hz(note), peak)


# ---------------------------------------------------------------------------
# section 1 -- drive stage
# ---------------------------------------------------------------------------
DRIVE_AMTS = (0.25, 0.5, 0.75, 1.0)


def section_drive(results):
    f0 = note_hz(NOTE)
    print(f"== 1. Drive stage ==  saw note {NOTE} (f0 {f0:.0f} Hz), driveMix=1")
    print("   dBc = worst non-harmonic bin 50 Hz..15 kHz, re strongest harmonic.")
    print("   '2x'/'4x' are the shipped voice-oversampling factors; the FX chain")
    print("   runs at 48 kHz in both. '192k' is the control that also runs the FX")
    print("   at 192 kHz, i.e. what a fully oversampled drive stage would deliver.")
    print("   The '<8k' columns repeat the measurement below 8 kHz, where an")
    print("   inharmonic tone has no nearby masker and reads as grit.")
    print()
    print("   'fold4x' = 4x minus 192k, and it is the ONLY single-variable")
    print("   isolation here: both arms render the voices at 192 kHz internally,")
    print("   so the one thing that differs is whether the FX chain also runs")
    print("   there. Do NOT read '2x minus 192k' the same way -- it moves the")
    print("   voice rate AND the FX rate at once, and carries the voice path's")
    print("   own 2x aliasing (see the drive-OFF row) inside it. The 2x column is")
    print("   still what a user hears at the shipped default, so it is reported")
    print("   as an absolute level, not as a difference.\n")

    hdr = (f"   {'config':<26}{'2x':>8}{'4x':>8}{'192k':>8}"
           f"{'2x<8k':>8}{'4x<8k':>8}{'192k<8k':>9}{'fold4x':>8}"
           f"{'pk2x':>7}{'rms2x':>7}   worst-image freq")
    base = {}
    print(hdr)
    for osf in (2, 4):
        base[osf] = measure(0, NOTE, 1.0, osf, f"fx_base_{osf}", driveOn=0, **PATCH)
    base["ctrl"] = measure(0, NOTE, 1.0, 1, "fx_base_ctrl", control=True,
                           driveOn=0, **PATCH)
    print(f"   {'drive OFF (voice path)':<26}{base[2][0]:>8.1f}{base[4][0]:>8.1f}"
          f"{base['ctrl'][0]:>8.1f}{base[2][2]:>8.1f}{base[4][2]:>8.1f}"
          f"{base['ctrl'][2]:>9.1f}{base[4][0] - base['ctrl'][0]:>8.1f}"
          f"{base[2][4]:>7.1f}{base[2][3]:>7.1f}   {base[2][1]:>8.0f} Hz (2x)")

    for dtype in (0, 1, 2):
        for amt in DRIVE_AMTS:
            r2 = measure(0, NOTE, 1.0, 2, f"fx_d{dtype}_{amt}_2",
                         driveOn=1, driveType=dtype, driveAmt=amt,
                         driveMix=1.0, **PATCH)
            r4 = measure(0, NOTE, 1.0, 4, f"fx_d{dtype}_{amt}_4",
                         driveOn=1, driveType=dtype, driveAmt=amt,
                         driveMix=1.0, **PATCH)
            rc = measure(0, NOTE, 1.0, 1, f"fx_d{dtype}_{amt}_ctrl", control=True,
                         driveOn=1, driveType=dtype, driveAmt=amt,
                         driveMix=1.0, **PATCH)
            label = f"{DRIVE_NAMES[dtype]} amt {amt:.2f}"
            print(f"   {label:<26}{r2[0]:>8.1f}{r4[0]:>8.1f}{rc[0]:>8.1f}"
                  f"{r2[2]:>8.1f}{r4[2]:>8.1f}{rc[2]:>9.1f}"
                  f"{r4[0] - rc[0]:>8.1f}{r2[4]:>7.1f}{r2[3]:>7.1f}"
                  f"   {r2[1]:>8.0f} Hz (2x)")
            results["drive"].append(dict(
                type=DRIVE_NAMES[dtype], amt=amt,
                dbc_2x=r2[0], hz_2x=r2[1], crit_2x=r2[2],
                rms_2x=r2[3], peak_2x=r2[4],
                dbc_4x=r4[0], crit_4x=r4[2],
                dbc_ctrl=rc[0], crit_ctrl=rc[2],
                delta_2x=r2[0] - base[2][0], delta_4x=r4[0] - base[4][0],
                # fold_4x is the clean isolation; fold_2x is kept only because
                # it is the shipped-default headroom, and is base-corrected by
                # subtracting the drive-OFF row's own 2x-vs-192k gap.
                fold_4x=r4[0] - rc[0], fold_crit_4x=r4[2] - rc[2],
                fold_2x_raw=r2[0] - rc[0],
                fold_2x_basecorrected=(r2[0] - rc[0])
                - (base[2][0] - base["ctrl"][0])))
    results["drive_base"] = {str(k): dict(dbc=v[0], hz=v[1], crit=v[2],
                                          rms=v[3], peak=v[4])
                             for k, v in base.items()}
    # The reference every later flag is compared against, measured under THIS
    # file's convention rather than borrowed from alias_gate's (see
    # VOICE_REF_2X_DBC). Section 3 reads it from here.
    results["voice_ref_measured"] = base[2][0]
    drift = base[2][0] - VOICE_REF_2X_DBC
    print(f"\n   voice-path reference re-measured under this file's convention: "
          f"{base[2][0]:.1f} dBc at 2x")
    print(f"   (alias_gate's published {VOICE_REF_2X_DBC:.0f} dBc uses the "
          f"FUNDAMENTAL as reference; drift {drift:+.1f} dB)")

    # Cross-check: the same configurations measured by the completely different
    # offline-residual method of section 3. Two methods, one signal -- if they
    # disagree, one of them is wrong and neither verdict is safe.
    p = dict(PATCH)
    p.update(masterVol=CAPTURE_VOL_DB, masterPan=0, stereoWidth=0.5,
             unisonVoices=1, driveOn=0)
    x = render(0, NOTE, 1.0, 2, "fx_xchk_cap", **p)[1][:, 0] / capture_scale()
    n = len(x)
    x = x[int(0.35 * n):int(0.95 * n)]
    print("   cross-check (offline residual, same signal and settings):")
    print(f"   {'config':<26}{'in-engine':>11}{'offline':>9}")
    for dtype, amt in ((0, 1.0), (1, 1.0), (2, 1.0), (0, 0.5)):
        r, ideal = residual_at(x, HOST_SR, dtype, amt, 1.0, 1)
        off = band_stats(r, ideal, HOST_SR)[0]
        eng = next(q["dbc_2x"] for q in results["drive"]
                   if q["type"] == DRIVE_NAMES[dtype] and abs(q["amt"] - amt) < 1e-6)
        print(f"   {DRIVE_NAMES[dtype] + f' amt {amt:.2f}':<26}{eng:>11.1f}{off:>9.1f}")
        results.setdefault("cross_check", []).append(
            dict(type=DRIVE_NAMES[dtype], amt=amt, in_engine=eng, offline=off))
    print()


# ---------------------------------------------------------------------------
# section 2 -- delay tape character
# ---------------------------------------------------------------------------
DELAY_FBS = (0.4, 0.7, 0.9, 0.95)


def section_delay(results):
    print(f"== 2. Delay tape character ==  saw note {NOTE}, 120 ms free-run delay")
    print("   tanh(fb*1.1) sits INSIDE the feedback loop, so images accumulate")
    print("   per pass; measured over the middle 60% (0.35..0.95) of a 3 s")
    print("   render, which is long enough past a 120 ms delay for the")
    print("   recirculation to have settled.\n")
    common = dict(PATCH)
    common.update(delayOn=1, delaySync=0, delayTime=120.0, delayMix=0.7,
                  delayPP=0, driveOn=0)

    print("   The comb response of a long feedback delay moves harmonics and")
    print("   images by tens of dB on its own, so the absolute columns are NOT")
    print("   comparable to section 1. The tape-ON minus tape-off delta is --")
    print("   it holds the comb fixed and changes only the nonlinearity.\n")
    print(f"   {'config':<26}{'2x':>8}{'4x':>8}{'192k':>8}{'2x<8k':>8}{'192k<8k':>9}")
    for fb in DELAY_FBS:
        row = {}
        for tape in (0, 1):
            row[(tape, 2)] = measure(0, NOTE, 3.0, 2, f"fx_dly_{fb}_{tape}_2",
                                     delayFB=fb, delayTape=tape, **common)
            row[(tape, 4)] = measure(0, NOTE, 3.0, 4, f"fx_dly_{fb}_{tape}_4",
                                     delayFB=fb, delayTape=tape, **common)
            row[(tape, "c")] = measure(0, NOTE, 3.0, 1, f"fx_dly_{fb}_{tape}_ctrl",
                                       control=True, delayFB=fb, delayTape=tape,
                                       **common)
            label = f"fb {fb:.2f} tape {'ON ' if tape else 'off'}"
            print(f"   {label:<26}{row[(tape, 2)][0]:>8.1f}{row[(tape, 4)][0]:>8.1f}"
                  f"{row[(tape, 'c')][0]:>8.1f}{row[(tape, 2)][2]:>8.1f}"
                  f"{row[(tape, 'c')][2]:>9.1f}")
        d2 = row[(1, 2)][0] - row[(0, 2)][0]
        dc = row[(1, "c")][0] - row[(0, "c")][0]
        print(f"   {'  -> tape delta':<26}{d2:>8.1f}{row[(1, 4)][0] - row[(0, 4)][0]:>8.1f}"
              f"{dc:>8.1f}{row[(1, 2)][2] - row[(0, 2)][2]:>8.1f}"
              f"{row[(1, 'c')][2] - row[(0, 'c')][2]:>9.1f}"
              f"   alias-attributable {d2 - dc:+.1f} dB")
        results["delay"].append(dict(
            fb=fb,
            off_2x=row[(0, 2)][0], off_4x=row[(0, 4)][0], off_ctrl=row[(0, "c")][0],
            on_2x=row[(1, 2)][0], on_4x=row[(1, 4)][0], on_ctrl=row[(1, "c")][0],
            tape_delta_2x=d2, tape_delta_ctrl=dc, tape_alias_2x=d2 - dc))
    print()


# ---------------------------------------------------------------------------
# section 3 -- factory preset sweep (exact offline residual)
# ---------------------------------------------------------------------------
# The "worst inharmonic bin" metric of sections 1-2 needs a single harmonic
# series to measure against. Real presets have detuned oscillators, unison, a
# sub, noise and FM, all of which are legitimately inharmonic -- that metric
# cannot tell their content apart from fold-back, so it is the wrong tool here.
#
# Instead: capture the exact signal the drive stage receives (the preset
# rendered with drive and all post-drive effects off, with the master gain and
# pan law unwound), then run DriveEffect's own nonlinearity over it twice --
# once at 48 kHz as shipped, once at 8x -- and difference the results. The two
# share an input sample for sample, so the difference is fold-back and nothing
# else. Running the same difference against a 2x and a 4x version sizes the
# candidate fix directly.
OS_IDEAL = 8                    # "alias-free" reference rate for the residual
STRESS = dict(type=0, amt=0.75, mix=1.0)   # what a user cranking Drive gets
EXPECTED_PRESETS = 54           # tripwire on the MultiSynthParams.hpp parser


def parse_presets():
    """(name, {param: value}) per factory preset, straight from the header."""
    src = open(PARAMS_HPP, encoding="utf-8").read()
    rows = {}
    for m in re.finditer(r"static constexpr PresetRow (kP\d+)\[\]\s*=\s*\{(.*?)\};",
                         src, re.S):
        vals = {}
        for k, v in re.findall(r"\{\s*kParam(\w+)\s*,\s*(-?[\d.]+(?:[eE][-+]?\d+)?)f?\s*\}",
                               m.group(2)):
            vals[k[0].lower() + k[1:]] = float(v)
        rows[m.group(1)] = vals
    table = re.search(r"kFactoryPresets\[\]\s*=\s*\{(.*?)\n\};", src, re.S).group(1)
    out = [(name, rows.get(sym, {}))
           for name, sym in re.findall(r'\{\s*"([^"]*)"\s*,\s*(kP\d+)\s*,', table)]
    # This is a regex over C++, so it fails silently and partially if the header
    # is ever reformatted -- a missed row would quietly drop a preset from the
    # sweep, or (worse) give it an empty param dict and report it as "no drive".
    # Both are caught here rather than in the table.
    n_decl = int(re.search(r"kNumFactoryPresets\s*=\s*\(int\)\(sizeof\(kFactoryPresets\)",
                           src) is not None)
    if n_decl and len(out) != EXPECTED_PRESETS:
        raise RuntimeError(
            f"parsed {len(out)} factory presets, expected {EXPECTED_PRESETS} -- "
            f"{PARAMS_HPP} layout changed, fix the parser (or bump "
            f"EXPECTED_PRESETS if presets were genuinely added)")
    empty = [nm for nm, r in out if not r]
    if empty:
        raise RuntimeError(
            f"factory presets parsed with no rows at all: {empty} -- the "
            f"PresetRow regex is not matching, every drive setting would read "
            f"as off")
    return out


def preset_render(index, path, **kw):
    args = [PRESET_BIN, str(index), path] + [f"{k}={v}" for k, v in kw.items()]
    r = subprocess.run(args, capture_output=True, text=True, timeout=300)
    if r.returncode != 0:
        raise RuntimeError(f"preset_render {index} failed: {r.stderr.strip()}")
    x, sr = sf.read(path, always_2d=True)
    return sr, x


def capture_scale():
    """Engine gain between the drive stage and the rendered file, exactly.

    masterGain = kOutputMakeup * 10^(vol/20) (MultiSynthDSP.cpp:673); the pan
    angle for masterPan=0 is pi/4 (cpp:774) so the left channel takes cos(pi/4);
    stereoWidth=0.5 maps to a side factor of 1.0 (cpp:775), making the mid/side
    stage an identity. softLimit() never engages at CAPTURE_VOL_DB.
    """
    return kOutputMakeup * 10.0 ** (CAPTURE_VOL_DB / 20.0) * np.cos(np.pi / 4.0)


HIGH_NOTE = 84   # C6 held: the exposed-lead case, where fold-back is worst


def capture_drive_input(index, high=False):
    """Capture what DriveEffect::process() actually receives (LEFT channel).

    Left only, not a mono sum: the drive is a per-channel nonlinearity, so
    f((L+R)/2) is not (f(L)+f(R))/2 and a summed capture models the wrong thing.

    high=False plays the preset's own mode-appropriate performance (what the
    demo pack and the preset audit use). high=True holds C6 instead: aliasing
    scales with how much energy sits near Nyquist, and a bass patch played at
    C2 simply has none, so the natural performance alone would under-report the
    exposed-lead case by ~30 dB. arpOn is deliberately left alone -- forcing it
    off silences every arp and acid preset (the note routes into the sequencer,
    MultiSynthDSP.cpp:335, and the poly voices are muted in acid mode).
    """
    tag = f"cap_{index:02d}{'_hi' if high else ''}.wav"
    # driveOn=0 removes the stage under test; chorus/delay/reverb sit AFTER it
    # and would pollute the capture; cosmosChorus sits BEFORE it and is kept.
    # (Modular presets also run SpringReverbFX, which has no off switch -- see
    # MultiSynthDSP.cpp:571 -- so those captures carry a spring tail. Flagged
    # in the table; it changes the material, not the method.)
    kw = dict(driveOn=0, chorusOn=0, delayOn=0, reverbOn=0, vintage=0,
              masterVol=CAPTURE_VOL_DB, masterPan=0, stereoWidth=0.5, tail=0.3)
    if high:
        kw.update(perf="hold", hold=HIGH_NOTE, bars=2)
    sr, x = preset_render(index, os.path.join(OUT, tag), **kw)
    left = x[:, 0] / capture_scale()
    n = len(left)
    return sr, left[int(0.15 * n):int(0.98 * n)]


def drive_np(x, dtype, amt, mix):
    """DriveEffect::process() with its smoothers settled (Effects.hpp:65-94)."""
    if amt < 0.001:
        return x.copy()
    g = 1.0 + amt * 10.0
    u = x * g
    if dtype == 0:
        y = np.tanh(u)
    elif dtype == 1:
        y = np.clip(u, -1.0, 1.0)
    else:
        y = np.where(u >= 0.0, 1.0 - np.exp(-u), -(1.0 - np.exp(u)) * 0.8)
    y = y / (1.0 + amt * 2.0)
    return x * (1.0 - mix) + y * mix


def band_stats(residual, reference, sr, hi_hz=BAND_HI):
    """(peak-bin dBc re the reference's strongest bin, energy ratio dB)."""
    n = min(len(residual), len(reference))
    w = np.hanning(n)
    R = np.abs(np.fft.rfft(residual[:n] * w))
    S = np.abs(np.fft.rfft(reference[:n] * w))
    f = np.fft.rfftfreq(n, 1.0 / sr)
    lo, hi = np.searchsorted(f, BAND_LO), np.searchsorted(f, hi_hz)
    ref = float(np.max(S[lo:hi]))
    if ref <= 0.0:
        return -200.0, -200.0
    peak = 20.0 * np.log10(float(np.max(R[lo:hi])) / ref + 1e-20)
    energy = 10.0 * np.log10(float(np.sum(R[lo:hi] ** 2))
                             / (float(np.sum(S[lo:hi] ** 2)) + 1e-30) + 1e-30)
    return peak, energy


def residuals(x, dtype, amt, mix, factors=(1, 2, 4)):
    """Fold-back left after running the drive stage at each oversampling factor.

    Returns {factor: residual} plus the alias-free reference. Every arm shares
    one input sample for sample, so the difference is fold-back and nothing
    else -- no windowing, alignment or spectral-estimation step in between.
    """
    up = resample_poly(x, OS_IDEAL, 1, window=RESAMPLE_WIN)
    ideal = resample_poly(drive_np(up, dtype, amt, mix), 1, OS_IDEAL,
                          window=RESAMPLE_WIN)
    out = {}
    for k in factors:
        if k == 1:
            got = drive_np(x, dtype, amt, mix)
        else:
            got = resample_poly(
                drive_np(resample_poly(x, k, 1, window=RESAMPLE_WIN),
                         dtype, amt, mix), 1, k, window=RESAMPLE_WIN)
        n = min(len(got), len(ideal))
        out[k] = got[:n] - ideal[:n]
    n = min(len(v) for v in out.values())
    return {k: v[:n] for k, v in out.items()}, ideal[:n]


def residual_at(x, sr, dtype, amt, mix, os_factor):
    r, ideal = residuals(x, dtype, amt, mix, factors=(os_factor,))
    return r[os_factor], ideal


def self_check(x, sr):
    """Resampler round-trip floor: identity through the same up/down path."""
    up = resample_poly(x, OS_IDEAL, 1, window=RESAMPLE_WIN)
    back = resample_poly(up, 1, OS_IDEAL, window=RESAMPLE_WIN)
    n = min(len(x), len(back))
    return band_stats(x[:n] - back[:n], x[:n], sr)


def verify_model(results):
    """Prove drive_np() is the engine's drive stage before trusting section 3.

    Renders one patch with the drive bypassed and again with it engaged, and
    checks the model against the engine harmonic by harmonic. A model that is
    only approximately the shipped nonlinearity would make every number below
    meaningless, so this runs first and prints its own numbers.
    """
    p = dict(PATCH)
    p.update(masterVol=CAPTURE_VOL_DB, masterPan=0, stereoWidth=0.5,
             unisonVoices=1)
    scale = capture_scale()
    note = 84
    f0 = note_hz(note)
    x = render(0, note, 1.0, 2, "fx_ver_off", driveOn=0, **p)[1][:, 0] / scale
    print("== 0. Model verification ==  drive_np() vs the engine's DriveEffect")
    print("   per-harmonic magnitude ratio, engine / model (1.000 = exact)\n")
    print(f"   {'config':<20}{'h1':>9}{'h2':>9}{'h3':>9}{'h4':>9}{'h6':>9}"
          f"{'h8':>9}{'resid':>9}")
    rows = []
    for dtype, amt in ((0, 0.4), (1, 0.8), (2, 0.6)):
        ye = render(0, note, 1.0, 2, f"fx_ver_{dtype}", driveOn=1,
                    driveType=dtype, driveAmt=amt, driveMix=1.0,
                    **p)[1][:, 0] / scale
        yp = drive_np(x, dtype, amt, 1.0)
        n = len(x)
        s = slice(int(0.4 * n), int(0.95 * n))
        a, b = ye[s], yp[s]
        w = np.hanning(len(a))
        A, B = np.fft.rfft(a * w), np.fft.rfft(b * w)
        f = np.fft.rfftfreq(len(a), 1.0 / HOST_SR)
        ratios = []
        for h in (1, 2, 3, 4, 6, 8):
            k = int(np.argmin(np.abs(f - h * f0)))
            sl = slice(k - 2, k + 3)
            ratios.append(float(np.abs(A[sl]).sum() / (np.abs(B[sl]).sum() + 1e-30)))
        resid = 20.0 * np.log10(np.sqrt(np.mean((a - b) ** 2))
                                / (np.sqrt(np.mean(a ** 2)) + 1e-20))
        print(f"   {DRIVE_NAMES[dtype] + ' amt ' + f'{amt:.2f}':<20}"
              + "".join(f"{r:>9.4f}" for r in ratios) + f"{resid:>9.1f}")
        rows.append(dict(type=DRIVE_NAMES[dtype], amt=amt,
                         ratios=ratios, resid_db=resid))
    results["verify"] = rows
    print("   The residual column is NOT model error in the nonlinearity: the")
    print("   engine's output DC blocker runs AFTER the drive and the capture's")
    print("   runs BEFORE it, and the two do not commute across a nonlinearity.")
    print("   It is a linear, phase-only difference (sub-degree per harmonic) and")
    print("   it is identical in the 1x and 8x arms of the residual, so it")
    print("   cancels. The magnitude ratios are the fidelity that matters.\n")


def section_presets(results):
    presets = parse_presets()
    print(f"== 3. Drive-stage alias residual, all {len(presets)} factory presets ==")
    print("   Method: capture the exact signal DriveEffect receives, apply its")
    print("   nonlinearity at 48 kHz and at 8x, difference. The difference IS the")
    print("   fold-back -- no inharmonicity assumption, so detuned/unison/noise/FM")
    print("   presets measure correctly. dBc = worst residual bin re the strongest")
    print("   bin of the alias-free result, 50 Hz..15 kHz.")
    print("   Two captures per preset: 'auto' = the preset's own mode-appropriate")
    print(f"   performance, 'C6' = a held MIDI {HIGH_NOTE}. The worse of the two is")
    print("   reported, with 'src' naming which one it came from.")
    print("   'own'    = the preset's shipped drive setting")
    print(f"   'stress' = {DRIVE_NAMES[STRESS['type']]} amt {STRESS['amt']} mix "
          f"{STRESS['mix']} forced on, i.e. a user reaching for the Drive knob")
    print("   '2x'/'4x' = what would be left if the drive stage were oversampled")
    ref = results.get("voice_ref_measured", VOICE_REF_2X_DBC)
    print(f"\n   FLAG THRESHOLD {ref:.1f} dBc = the voice path measured in section 1.")
    print("   Caveat, because it is comparing across two conventions: section 1")
    print("   reads the worst image in an OUTPUT spectrum re its strongest")
    print("   harmonic, this section reads the worst bin of a RESIDUAL re the")
    print("   strongest bin of the alias-free reference. Both are 'worst spurious")
    print("   bin re the strongest tone present', which is what makes the")
    print("   comparison meaningful, but they are not the same measurement and a")
    print("   row sitting a decibel either side of the line means little.\n")
    print(f"   {'#':>3} {'name':<20}{'drv':>6}{'amt':>6}"
          f"{'own1x':>8}{'own2x':>7}{'own4x':>7}"
          f"{'str1x':>8}{'str2x':>7}{'str4x':>7}{'src':>6}  flag")

    flagged, checked = [], False
    nan = float("nan")
    for i, (name, rows) in enumerate(presets):
        drive_on = rows.get("driveOn", 0.0) > 0.5
        dtype = int(rows.get("driveType", 0.0))
        amt = rows.get("driveAmt", 0.3)
        mix = rows.get("driveMix", 1.0)

        own, stress, src, best = {}, {}, "-", -400.0
        silent = True
        for label, high in (("auto", False), ("C6", True)):
            sr, x = capture_drive_input(i, high=high)
            if 20.0 * np.log10(float(np.sqrt(np.mean(x ** 2))) + 1e-20) < -80.0:
                continue
            silent = False
            # Publish the method floor from the first NON-SILENT capture: run on
            # a silent one it reports the round-trip of digital zero (-200 dB)
            # and every residual below would look trustworthy.
            if not checked:
                pk, en = self_check(x, sr)
                results["self_check"] = dict(peak_dbc=pk, energy_db=en,
                                             from_preset=i, capture=label)
                checked = True
            res, ideal = residuals(x, STRESS["type"], STRESS["amt"], STRESS["mix"])
            st = {k: band_stats(v, ideal, sr)[0] for k, v in res.items()}
            ow = {}
            if drive_on:
                res, ideal = residuals(x, dtype, amt, mix)
                ow = {k: band_stats(v, ideal, sr)[0] for k, v in res.items()}
            # rank on the shipped setting where there is one, else on the stress
            score = ow[1] if drive_on else st[1]
            if score > best:
                best, own, stress, src = score, ow, st, label

        if silent:
            print(f"   {i:>3} {name:<20}{'-':>6}{0:>6.2f}"
                  f"{nan:>8.1f}{nan:>7.1f}{nan:>7.1f}"
                  f"{nan:>8.1f}{nan:>7.1f}{nan:>7.1f}{'-':>6}  silent-capture")
            results["presets"].append(dict(index=i, name=name, silent=True))
            continue

        flag = ""
        if drive_on and own[1] > ref:
            flag = "OVER-REF"
            flagged.append((i, name, dtype, amt, own[1], own[2], own[4]))
        elif stress[1] > ref:
            flag = "stress-only"
        if rows.get("mode", 0.0) == 3.0:
            flag = (flag + " spring-in-capture").strip()
        dn = DRIVE_NAMES[dtype][:5] if drive_on else "-"
        print(f"   {i:>3} {name:<20}{dn:>6}{(amt if drive_on else 0):>6.2f}"
              f"{own.get(1, nan):>8.1f}{own.get(2, nan):>7.1f}{own.get(4, nan):>7.1f}"
              f"{stress[1]:>8.1f}{stress[2]:>7.1f}{stress[4]:>7.1f}{src:>6}"
              f"  {flag}")
        results["presets"].append(dict(
            index=i, name=name, silent=False, drive=drive_on,
            type=DRIVE_NAMES[dtype] if drive_on else None,
            amt=amt if drive_on else 0.0, source=src,
            own_1x=own.get(1), own_2x=own.get(2), own_4x=own.get(4),
            stress_1x=stress[1], stress_2x=stress[2], stress_4x=stress[4],
            flag=flag))
    print()
    return flagged


# ---------------------------------------------------------------------------
def preflight(do_presets):
    """Everything this run needs, checked before minutes of renders are spent.

    Report-only gates must not turn a missing dependency into a silent absence:
    run_all.sh swallows a nonzero exit as "report-only, not fatal", so an
    unavailable scipy or an unbuilt preset_render would just make the report
    quietly disappear from the suite output. Say so instead, and exit 0.
    """
    if resample_poly is None:
        print("fx_alias_gate SKIPPED: needs scipy (resample_poly) and it is not "
              "installed.")
        print("   Every measurement here is a rate-conversion difference, so "
              "there is no degraded mode to fall back to.")
        print("   Install with: python3 -m pip install scipy")
        return False
    if do_presets and not os.path.exists(PRESET_BIN):
        print(f"fx_alias_gate SKIPPED: {PRESET_BIN} not built.")
        print("   Build it with: cmake --build build   (run_all.sh does this "
              "first), or pass --no-presets for sections 0-2 only.")
        return False
    if not os.path.exists(PARAMS_HPP):
        print(f"fx_alias_gate SKIPPED: {PARAMS_HPP} not found.")
        return False
    return True


def main():
    do_presets = "--no-presets" not in sys.argv
    as_json = "--json" in sys.argv
    results = dict(drive=[], delay=[], presets=[])

    if not preflight(do_presets):
        return 0

    # --json emits only JSON on stdout: the tables would otherwise make the
    # stream unparseable for anything consuming it.
    sink = open(os.devnull, "w") if as_json else None
    with contextlib.redirect_stdout(sink) if as_json else contextlib.nullcontext():
        if not as_json:
            print("fx_alias_gate: effects-chain aliasing report (report only)\n")
        verify_model(results)
        section_drive(results)
        section_delay(results)
        flagged = section_presets(results) if do_presets else []
    if sink is not None:
        sink.close()

    if as_json:
        print(json.dumps(results, indent=2,
                         default=lambda o: o.item() if hasattr(o, "item") else str(o)))
        return 0

    ref = results.get("voice_ref_measured", VOICE_REF_2X_DBC)
    print("== Summary ==")
    print(f"   voice-path reference (measured, this file's convention): "
          f"{ref:.1f} dBc at 2x")
    print("   fold-back below is 4x minus 192k -- the single-variable isolation.")
    for name in ("SoftClip", "HardClip", "Tube"):
        rows = [r for r in results["drive"] if r["type"] == name]
        w = max(rows, key=lambda r: r["fold_4x"])
        print(f"   {name:<9}: worst fold-back {w['fold_4x']:+5.1f} dB at amt "
              f"{w['amt']:.2f} ({w['dbc_4x']:.1f} dBc at 4x vs {w['dbc_ctrl']:.1f} "
              f"dBc oversampled; below 8 kHz {w['fold_crit_4x']:+.1f} dB). "
              f"Shipped-default 2x level {w['dbc_2x']:.1f} dBc")
    dworst = max(results["delay"], key=lambda r: r["tape_alias_2x"])
    print(f"   delay tape: worst alias-attributable delta "
          f"{dworst['tape_alias_2x']:+.1f} dB at fb {dworst['fb']:.2f} "
          f"(total tape delta {dworst['tape_delta_2x']:+.1f} dB, of which "
          f"{dworst['tape_delta_ctrl']:+.1f} dB is real harmonics)")
    if do_presets:
        rows = [r for r in results["presets"] if not r.get("silent")]
        shipped = [r for r in rows if r["drive"]]
        sc = results.get("self_check", {})
        print(f"   method floor: resampler round-trip residual "
              f"{sc.get('peak_dbc', float('nan')):.1f} dBc "
              f"(everything above this is real fold-back)")
        print(f"   presets   : {len(flagged)} of {len(results['presets'])} flagged "
              f"OVER-REF ({len(shipped)} ship with drive on, "
              f"{len(results['presets']) - len(rows)} captured silent)")
        for r in sorted(shipped, key=lambda r: -(r["own_1x"] or -400))[:12]:
            print(f"      #{r['index']:<3} {r['name']:<20} {r['type']:<9} "
                  f"amt {r['amt']:.2f}  shipped {r['own_1x']:6.1f} dBc  "
                  f"(2x {r['own_2x']:6.1f}, 4x {r['own_4x']:6.1f}, "
                  f"worst capture {r['source']})")
        sw = max(rows, key=lambda r: r["stress_1x"])
        print(f"   worst under the stress config: #{sw['index']} {sw['name']} "
              f"{sw['stress_1x']:.1f} dBc (2x {sw['stress_2x']:.1f}, "
              f"4x {sw['stress_4x']:.1f})")
        n_stress = sum(1 for r in rows if r["stress_1x"] > ref)
        print(f"   {n_stress} of {len(rows)} exceed {ref:.1f} dBc once "
              f"Drive is turned up to {STRESS['amt']:.2f}")
    else:
        print("   presets   : SKIPPED (--no-presets). Section 3 sweeps all "
              f"{EXPECTED_PRESETS} factory presets and is the slow part of this "
              "gate; run_all.sh runs it only when FX_ALIAS_FULL=1.")
    print("\nfx_alias_gate: report only (no pass/fail)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
