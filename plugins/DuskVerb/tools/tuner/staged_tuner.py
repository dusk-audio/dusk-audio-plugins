#!/usr/bin/env python3
"""
PRODUCTION TOOL — frozen architecture, autonomous configuration.

Single command runs the full pipeline:
    python3 staged_tuner.py "Vocal Hall" \
        --anchor-rendered /tmp/anchor_vh/VocalHall_noiseburst.wav \
        --category Halls

The script:
  1. Reads CATEGORY_RULES[category] for stage1_ranges + stage1_x0 + has_dpv.
  2. Slope-fits tail_t60 on the sustained-pink anchor; converts to a
     Decay-knob seed + RMSE-driven sigma scale for Stage 2.
  3. Auto-strips DPV params from Stage 2/3 if has_dpv is False.
  4. Runs the 3-stage CMA-ES sweep + final full_check with the matching
     --category ENGINE_CEILINGS bypass.

No per-preset code edits. To add a new category, add an entry to
CATEGORY_RULES + an ENGINE_CEILINGS entry in full_check.py.

3-Stage Sequential Tuner (CMA-ES) — replaces the single-pass weighted-sum
Optuna sweep with a human-mix-engineer workflow:

  Stage 1: SPATIAL + ENVELOPE  ("Feel")     ~300 trials, CMA-ES
    Active params: Size, Diffusion, Width, Mod Depth, Mod Rate
    Locked:        EQs flat, Multipliers 1.0, Decay 1.0
    Loss:          envelope_shape_L1 + |stereo_corr Δ|

  Stage 2: TEMPORAL + DECAY    ("Tail")     ~500 trials, CMA-ES
    Active:        Decay, Bass/Mid/Treble Multiply, Low/High Crossover
    Locked:        Stage 1 best + EQs flat
    Loss:          per-band EDT (t10) + t30 + t60

  Stage 3: SPECTRAL + EQ       ("Polish")   ~500 trials, CMA-ES
    Active:        Bass Shelf, HF Shelf, Box Cut, Struct HF Damp,
                   Lo Cut, Hi Cut, Saturation, Gain Trim
    Locked:        Stage 1 + Stage 2 best
    Loss:          spec_L1 + sustained-pink ss-band-energies + cent_50 + RMS

Why staged: in a single-pass weighted sum, Optuna can solve a TEMPORAL gap
with a SPECTRAL knob (e.g. blast Bass Shelf +14 dB to fake an EDT extension).
Each stage here locks the "fake-it" parameters away from the objective they
shouldn't influence. Result is closer to how a human mix engineer tunes:
shape first, decay next, polish EQ last.

Why CMA-ES: highly correlated DSP parameters (Decay ↔ HF Damping, Mults ↔
Crossovers) defeat TPE's independent-axis assumption. CMA-ES models the
covariance matrix natively and converges faster on coupled landscapes.

Inputs: same as tune_preset.py (anchor noiseburst path + DV preset name).
Outputs: best.json from each stage + a combined final.json + full_check report.

Usage:
    python3 staged_tuner.py "Vintage Vocal Plate" \\
        --anchor-rendered /tmp/anchor_v2/LexAnchor_noiseburst.wav \\
        --workers 6
"""
from __future__ import annotations
import argparse, json, shutil, subprocess, sys, tempfile, time
from pathlib import Path

import numpy as np
import optuna
import soundfile as sf
from scipy.signal import butter, sosfiltfilt, hilbert

REPO = Path(__file__).resolve().parents[4]
RENDER_BIN = REPO / "build" / "tests" / "duskverb_render" / "duskverb_render"
DEFAULT_VST3 = Path.home() / ".vst3" / "DuskVerb.vst3"

sys.path.insert(0, str(Path(__file__).resolve().parent))
from metrics_external import envelope_shape_l1, compute_metrics
from full_check import osc_envelope_p2p


# ─── Param domains (full range — each stage picks a subset) ─────────────────

ALL_PARAMS = {
    # Stage 1 — spatial + envelope
    "Size":             (0.10,   1.00),
    "Diffusion":        (0.25,   1.00),    # Hardened floor — prevents sampler
                                            # from dissolving tank density into
                                            # discrete delays to cheat Stage 3.
    "Width":            (0.50,   1.05),    # mono-compat clamp
    "Mod Depth":        (0.00,   0.60),
    "Mod Rate":         (0.10,   3.00),
    # Stage 1 expanded — early structure (added 2026-05-27 to give the
    # optimizer pre-tail + early-reflection control on top of plain spatial
    # params; closes spec_L1 max peaks in 200-500 Hz region that often come
    # from default ER level/size on smaller spaces).
    "Pre-Delay":        (0.0,    50.0),    # ms — beyond 50 reads as slap
    "Early Ref Level":  (0.0,     1.0),
    "Early Ref Size":   (0.0,     1.0),
    # Stage 2 — temporal + decay
    "Decay Time":       (0.20,  12.00),
    "Treble Multiply":  (0.50,   1.50),
    "Bass Multiply":    (0.50,   1.50),
    "Mid Multiply":     (0.50,   1.50),
    "Low Crossover":    (80.0,  900.0),
    "High Crossover":   (3000.0, 10000.0),
    # Stage 3 — spectral + EQ
    "Lo Cut":           (20.0,  500.0),
    "Hi Cut":           (10000.0, 20000.0),
    # Post-tank high-shelf attenuation depth (Phase 1 engine surgery, now
    # APVTS-exposed). 0 dB = shelf flat (Hi Cut knob has no effect),
    # -24 dB = aggressive HF tuck. Engine clamps to [-24, 0].
    "Hi Cut Shelf":     (-24.0, 0.0),
    "Saturation":       (0.00,   0.40),
    "Gain Trim":        (-12.0,  24.0),
    "DPV HF Shelf Gain":   (-12.0,   24.0),
    "DPV HF Shelf Freq":   (2000.0, 20000.0),
    "DPV Struct HF Damp":  (2000.0, 18000.0),
    "DPV Box Cut Gain":    (-12.0,    6.0),
    "DPV Box Cut Freq":    (100.0,  800.0),
    "DPV Bass Shelf Gain": (-6.0,   18.0),
    "DPV Bass Shelf Freq": (60.0,   500.0),
    # Phase α (2026-05-29): PostTankEQ 4-band GAIN exposed to optimizer via
    # APVTS. Operates linearly at the engine exit gate AFTER the FDN
    # feedback loop — immune to gBase compression that buffers high
    # frequencies during the recursive damping pass. Lets the Stage 3
    # Polish phase notch out HF overshoot (8 k / 16 k bands on Bright Hall
    # currently +18 / +39 % hot under the new per-line decay tilt) without
    # disturbing internal loop dynamics. Tight ±6 dB correction window —
    # wider ranges risk the optimizer using EQ as a primary tone-shaper
    # instead of the surgical exit-stage scalpel it's designed to be.
    "PostTankEQ Band 0 Gain":  (-12.0, 12.0),
    "PostTankEQ Band 1 Gain":  (-12.0, 12.0),
    "PostTankEQ Band 2 Gain":  (-12.0, 12.0),
    "PostTankEQ Band 3 Gain":  (-12.0, 12.0),
    # Phase γ (2026-05-29): PostTankBandTrim 4-region linear gain trim.
    # Independent of FDN damping — direct exit-stage scalpel for EDT band-
    # shape, late bass boom, and per-band steady-state RMS drift. Tight
    # ±8 dB corrective window per user mandate.
    "Post Band Sub Gain":      (-8.0,  8.0),
    "Post Band Low-Mid Gain":  (-8.0,  8.0),
    "Post Band Mid-High Gain": (-8.0,  8.0),
    "Post Band Air Gain":      (-8.0,  8.0),
}

# Defaults applied when a param is "locked flat" for a stage (so its
# contribution to the rendered output is neutral / minimal).
STAGE1_FLAT_DEFAULTS = {
    # All Multipliers neutral (unity feedback per band)
    "Treble Multiply": 1.0, "Bass Multiply": 1.0, "Mid Multiply": 1.0,
    "Low Crossover": 300.0, "High Crossover": 5000.0,  # neutral crossover points
    # Decay locked to a 1.0 s baseline so spatial-only sweep doesn't drift
    "Decay Time": 1.0,
    # Early structure defaults (when locked out of a stage's active set)
    "Pre-Delay": 15.0, "Early Ref Level": 0.45, "Early Ref Size": 0.55,
    # EQs flat
    "Lo Cut": 20.0, "Hi Cut": 20000.0, "Saturation": 0.0,
    "Gain Trim": 0.0,
    # Phase α PostTankEQ — 0 dB = unity bypass (designUnity → bit-identical
    # passthrough). Locking these flat in stages that don't own them keeps
    # presets without an EQ override sounding identical to pre-Phase-α.
    "PostTankEQ Band 0 Gain": 0.0,
    "PostTankEQ Band 1 Gain": 0.0,
    "PostTankEQ Band 2 Gain": 0.0,
    "PostTankEQ Band 3 Gain": 0.0,
    # Phase γ PostTankBandTrim — 0 dB on every region = unity-coeff
    # high-shelves + 1.0 makeup gain → bit-identical bypass.
    "Post Band Sub Gain":      0.0,
    "Post Band Low-Mid Gain":  0.0,
    "Post Band Mid-High Gain": 0.0,
    "Post Band Air Gain":      0.0,
    "DPV HF Shelf Gain": 0.0,   "DPV HF Shelf Freq": 8000.0,
    "DPV Struct HF Damp": 18000.0,  # near-off
    "DPV Box Cut Gain": 0.0,    "DPV Box Cut Freq": 400.0,
    "DPV Bass Shelf Gain": 0.0, "DPV Bass Shelf Freq": 200.0,
}

# Always-locked harness overrides (forced on every trial)
HARNESS_OVERRIDES = {
    "Dry/Wet": 1.0,
    "Bus Mode": 1,
    "Freeze": 0,
}


# ─── Category Rule Profiles ─────────────────────────────────────────────────
#
# Each preset category maps to a self-contained profile of architectural
# guardrails the tuner enforces autonomously. The user doesn't edit ranges,
# loss weights, or feature flags between presets — they pass --category and
# the tuner picks the right profile.
#
# Schema per profile:
#   stage1_ranges     : dict[param_name -> (lo, hi)] — overrides ALL_PARAMS
#                       for THIS stage only. Halls clamp Mod Depth + Size
#                       to keep the FDN out of metallic-chorus territory.
#   stage1_x0         : dict[param_name -> float] — CMA-ES seed in native
#                       units. Override the structural defaults (Mod Depth
#                       mild, Size large for halls, etc.).
#   has_dpv           : bool — when True, Stage 2 includes DPV Struct HF
#                       Damp + Stage 3 includes the 6 DPV shelf params.
#                       When False, those 7 dead axes are stripped so
#                       CMA-ES focuses on knobs actually wired to the engine.
#
# Add a new category by adding a new entry — no code changes required.
CATEGORY_RULES = {
    "Halls": {
        "stage1_ranges": {
            "Mod Depth": (0.0, 0.20),   # loosened 0.15→0.20 for more mod headroom
            "Size":      (0.55, 1.0),   # loosened 0.65→0.55 for size headroom
            "Pre-Delay": (0.0, 50.0),   # hall pre-delay range
        },
        "stage1_x0": {
            "Mod Depth": 0.10,
            "Size":      0.80,
            "Pre-Delay": 20.0,
            "Early Ref Level": 0.45,
            "Early Ref Size":  0.55,
        },
        # Stage 3 range overrides: widen Hi Cut floor to 4000 Hz (default
        # ALL_PARAMS floor is 10000). FDN doesn't have VVV's Color Mode
        # structural HF damping, so Hi Cut is the only post-tank HF-tilt
        # control. v11 Vocal Hall settled at 10005 Hz (pinned at the floor)
        # and STILL measured ss hi +4.10 dB / ss air +3.92 dB hotter than
        # Lex — audible "trashy" character per listening test. The widened
        # floor gives Stage 3 the headroom to actually clamp HF.
        "stage3_ranges": {
            "Hi Cut": (4000.0, 20000.0),
        },
        "has_dpv": False,
    },
    "Plates": {
        "stage1_ranges": {
            "Size":      (0.10, 0.80),
            "Diffusion": (0.30, 1.0),
            "Mod Depth": (0.0, 0.30),    # plates can carry more mod than halls
            "Pre-Delay": (0.0, 30.0),    # plate pre-delay tighter than halls
        },
        "stage1_x0": {
            "Size":      0.45,
            "Diffusion": 0.55,
            "Mod Depth": 0.18,
            "Pre-Delay": 5.0,
            "Early Ref Level": 0.0,      # plates have minimal ER content
            "Early Ref Size":  0.30,
        },
        # has_dpv defaults to False — only Vintage Vocal Plate (algo 1) uses
        # DattorroPlateVintage. Other Plates (Vocal Plate, Snare Plate XL,
        # etc.) are FDN and the DPV params are no-op. Per-preset override
        # below lifts has_dpv=True for DPV-routed presets only.
        "has_dpv": False,
    },
    "Chambers": {
        "stage1_ranges": {
            "Mod Depth": (0.0, 0.25),
            "Size":      (0.40, 0.95),    # loosened ceiling 0.85→0.95
            "Pre-Delay": (0.0, 25.0),
        },
        "stage1_x0": {
            "Mod Depth": 0.10,
            "Size":      0.65,
            "Pre-Delay": 10.0,
            "Early Ref Level": 0.30,      # chambers have prominent ERs
            "Early Ref Size":  0.50,
        },
        "has_dpv": False,
    },
    "Rooms": {
        "stage1_ranges": {
            "Mod Depth": (0.0, 0.30),     # loosened 0.20→0.30 (rooms get more mod)
            "Size":      (0.10, 0.80),    # widened 0.15-0.75 → 0.10-0.80
            "Pre-Delay": (0.0, 20.0),
        },
        "stage1_x0": {
            "Mod Depth": 0.10,
            "Size":      0.40,
            "Pre-Delay": 3.0,
            "Early Ref Level": 0.60,      # rooms dominated by early reflections
            "Early Ref Size":  0.40,
        },
        "has_dpv": False,
    },
    "Ambient": {
        # Long, evolving pads. Heavier mod is part of the character; allow
        # bigger Mod Depth ceiling than other categories.
        "stage1_ranges": {
            "Mod Depth": (0.0, 0.35),
            "Size":      (0.60, 1.0),
        },
        "stage1_x0": {
            "Mod Depth": 0.20,
            "Size":      0.80,
        },
        "has_dpv": False,
    },
    "Springs": {
        # Spring engine — mod is the character (drip / wobble).
        "stage1_ranges": {
            "Mod Depth": (0.10, 0.50),
            "Size":      (0.30, 0.90),
        },
        "stage1_x0": {
            "Mod Depth": 0.30,
            "Size":      0.65,
        },
        "has_dpv": False,
    },
    "": {   # Default profile — no category specified
        "stage1_ranges": {},
        "stage1_x0": {},
        "has_dpv": False,
    },
}


# Per-preset overrides on top of category profile. Use when a single preset
# uses a different engine than the rest of its category (e.g. Vintage Vocal
# Plate is the only algo-1 DPV preset; everything else in Plates is FDN).
PRESET_OVERRIDES = {
    "Vintage Vocal Plate": {"has_dpv": True},
}


# ─── Render harness wrapper ─────────────────────────────────────────────────

def render(preset, overrides, vst3, out_dir, prerun=5.0, sustained=4.0):
    out_dir.mkdir(parents=True, exist_ok=True)
    cmd = [str(RENDER_BIN), "--vst3", str(vst3),
           "--output-dir", str(out_dir),
           "--prerun-seconds", str(prerun),
           "--sustained-pink-seconds", str(sustained)]
    for k, v in HARNESS_OVERRIDES.items():
        cmd += ["--param", f"{k}={v}"]
    for k, v in overrides.items():
        if k in HARNESS_OVERRIDES:
            continue
        cmd += ["--param", f"{k}={v}"]
    cmd.append(preset)
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
    if r.returncode != 0:
        return None
    preset_token = "".join(c for c in preset if c.isalnum() or c in "+-_'")
    noise = out_dir / f"{preset_token}_noiseburst.wav"
    sustained_p = out_dir / f"{preset_token}_sustained.wav"
    snare = out_dir / f"{preset_token}_snare.wav"
    if not (noise.exists() and sustained_p.exists() and snare.exists()):
        return None
    return {"noiseburst": str(noise), "sustained": str(sustained_p),
            "snare": str(snare)}


# ─── Loss helpers — each stage uses a subset ────────────────────────────────

def _band_decay_time(p, lo, hi, target_db, input_off_s=4.0):
    """Per-band time-to-(-target_db) post input-off (sustained-pink stimulus)."""
    x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim>1 else x
    off = int(input_off_s * sr)
    if off >= len(m): return None
    tail = m[off:]
    hi_c = min(hi, sr * 0.49)
    if lo <= 0:
        sos = butter(4, hi_c, 'low', fs=sr, output='sos')
    else:
        sos = butter(4, [lo, hi_c], 'band', fs=sr, output='sos')
    y = sosfiltfilt(sos, tail)
    pwr = y ** 2
    win = max(int(0.01 * sr), 1)
    sm = np.convolve(pwr, np.ones(win) / win, mode='same')
    peak = float(np.max(sm))
    if peak < 1e-12: return None
    pidx = int(np.argmax(sm))
    floor = float(np.median(sm[-min(int(0.5 * sr), len(sm)):]))
    thr = max(peak * 10 ** (-target_db / 10), floor * 4.0)
    below = np.where((np.arange(len(sm)) > pidx) & (sm < thr))[0]
    return (int(below[0]) - pidx) / sr if len(below) else None


def _ss_band_rms_db(p, lo, hi, t0=2.5, t1=4.0):
    x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim>1 else x
    a, b = int(t0 * sr), min(int(t1 * sr), len(m))
    hi_c = min(hi, sr * 0.49)
    if lo <= 0:
        sos = butter(4, hi_c, 'low', fs=sr, output='sos')
    else:
        sos = butter(4, [lo, hi_c], 'band', fs=sr, output='sos')
    y = sosfiltfilt(sos, m)
    return float(20 * np.log10(np.sqrt(np.mean(y[a:b] ** 2)) + 1e-30))


def _late_low_rms_db(p, t0, t1, lo, hi):
    """Peak-aligned late-window low-band integrated RMS (dB). Catches the
    "boomy" tail signature spec_L1 averaging misses: bass that lingers
    after the anchor's structural damping has attenuated it."""
    x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim > 1 else x
    peak = int(np.argmax(np.abs(m)))
    i0 = max(0, peak + int(t0 * sr))
    i1 = min(len(m), peak + int(t1 * sr))
    if i1 - i0 < 200: return None
    seg = m[i0:i1]
    hi_c = min(hi, sr * 0.49)
    sos = butter(4, [max(lo, 10.0), hi_c], 'band', fs=sr, output='sos')
    y = sosfiltfilt(sos, seg)
    rms = float(np.sqrt(np.mean(y ** 2)))
    return 20.0 * np.log10(max(rms, 1e-12))


def _post_peak_band_rms_db(p, t0_ms, t1_ms, lo, hi):
    """Peak-aligned band-passed integrated RMS in a [t0, t1] ms window.
    Used for temporal-spectral terms: HF bloom hot (50-300 ms × 4-8k) +
    body sustain cold (300-800 ms × 100-2k). Captures listener-perceived
    shape that broadband third-octave averaging misses."""
    x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim > 1 else x
    peak = int(np.argmax(np.abs(m)))
    i0 = max(0, peak + int(t0_ms * sr / 1000))
    i1 = min(len(m), peak + int(t1_ms * sr / 1000))
    if i1 - i0 < 200: return None
    seg = m[i0:i1]
    hi_c = min(hi, sr * 0.49)
    sos = butter(4, [max(lo, 10.0), hi_c], 'band', fs=sr, output='sos')
    y = sosfiltfilt(sos, seg)
    rms = float(np.sqrt(np.mean(y ** 2)))
    return 20.0 * np.log10(max(rms, 1e-12))


def _t60_band_schroeder (p, lo, hi):
    """Per-band T60 via Schroeder backward integration on the post-peak
    noiseburst tail. Bandpass → reverse-cumulative energy → log-scale slope
    fit between -5 and -25 dB → T60 = -60/slope. Returns seconds or None."""
    x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim > 1 else x
    nyq = sr * 0.49
    sos = butter(4, [max(lo, 10.0), min(hi, nyq)], 'band', fs=sr, output='sos')
    y = sosfiltfilt(sos, m)
    peak = int(np.argmax(np.abs(y)))
    tail = y[peak : peak + int(sr * 4.0)]
    if len(tail) < int(sr * 0.5): return None
    sq  = tail ** 2
    edc = np.cumsum(sq[::-1])[::-1]
    if edc[0] <= 1.0e-30: return None
    edc_db = 10.0 * np.log10 (np.maximum (edc / edc[0], 1.0e-12))
    t = np.arange(len(edc_db)) / sr
    try:
        i5  = int (np.where (edc_db <= -5.0) [0][0])
        i25 = int (np.where (edc_db <= -25.0)[0][0])
    except IndexError:
        return None
    if i25 <= i5: return None
    slope = np.polyfit (t[i5:i25], edc_db[i5:i25], 1)[0]
    if slope >= -1.0e-3: return None
    return -60.0 / slope


def _tail_mod_peak_freq (p, t0, t1, lo, hi, env_smooth_ms=30,
                          f_lo=0.3, f_hi=8.0):
    """Per-band dominant envelope-mod peak frequency in [f_lo, f_hi] Hz.
    Bandpass → Hilbert env → linear detrend → Hanning window → rFFT →
    arg max within target range. Returns Hz or None."""
    x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim > 1 else x
    peak = int(np.argmax(np.abs(m)))
    i0 = max(0, peak + int(t0 * sr))
    i1 = min(len(m), peak + int(t1 * sr))
    if i1 - i0 < int(sr * 0.5): return None
    seg = m[i0:i1]
    hi_c = min(hi, sr * 0.49)
    sos = butter(4, [max(lo, 10.0), hi_c], 'band', fs=sr, output='sos')
    y = sosfiltfilt(sos, seg)
    env = np.abs(hilbert(y))
    win = max(1, int(sr * env_smooth_ms / 1000.0))
    env_s = np.convolve(env, np.ones(win) / win, mode='same')
    env_db = 20.0 * np.log10(np.maximum(env_s, 1.0e-12))
    t = np.arange(len(env_db)) / sr
    slope, intercept = np.polyfit(t, env_db, 1)
    env_ac = env_db - (slope * t + intercept)
    if len(env_ac) < 64: return None
    n = len(env_ac)
    nfft = 1 << int(np.ceil(np.log2(n * 4)))
    spec = np.abs(np.fft.rfft(env_ac * np.hanning(n), n=nfft))
    f = np.fft.rfftfreq(nfft, d=1 / sr)
    mask = (f >= f_lo) & (f <= f_hi)
    if not np.any(mask): return None
    idx = int(np.argmax(spec[mask]))
    return float(f[mask][idx])


def _tail_envelope_ripple_db(p, t0, t1, lo, hi, env_smooth_ms=30):
    """Detrended dB-envelope std on the tail (post-peak) in a band. Lex
    natural decay gives ~1 dB ripple (smooth slope); engine modulation
    artefacts inflate it. Used as the asymmetric ripple-hot penalty
    that catches per-band mod wobble the broadband osc_p2p metric misses
    (VH v14 audition: mid 1-4 kHz ripple std 4.15 dB vs Lex 0.95 dB)."""
    x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim > 1 else x
    peak = int(np.argmax(np.abs(m)))
    i0 = max(0, peak + int(t0 * sr))
    i1 = min(len(m), peak + int(t1 * sr))
    if i1 - i0 < int(sr * 0.5): return None
    seg = m[i0:i1]
    hi_c = min(hi, sr * 0.49)
    sos = butter(4, [max(lo, 10.0), hi_c], 'band', fs=sr, output='sos')
    y = sosfiltfilt(sos, seg)
    env = np.abs(hilbert(y))
    win = max(1, int(sr * env_smooth_ms / 1000.0))
    env_s = np.convolve(env, np.ones(win) / win, mode='same')
    env_db = 20.0 * np.log10(np.maximum(env_s, 1e-12))
    # Detrend: remove the linear decay slope so only AC ripple remains.
    t = np.arange(len(env_db)) / sr
    slope, intercept = np.polyfit(t, env_db, 1)
    env_db_ac = env_db - (slope * t + intercept)
    return float(np.std(env_db_ac))


def _anchor_decay_estimate(sustained_wav: Path) -> tuple:
    """Extrapolated tail_t60 from the sustained-pink anchor, mapped to a
    DV Decay Time knob seed. Returns (seed_knob, slope_fit_rmse_db).

    Prior version measured peak-to-(-30 dB) on the SNARE stimulus and
    multiplied by 1.3 — calibrated for short plates (~0.4 s). On long FDN
    halls (VVV Vocal Hall t60 ≈ 3.9 s) the snare transient decay isn't
    representative of steady-state tail length, so the estimator under-shot
    by ~35 % and CMA-ES (sigma0=0.2 normalized → ~±2 s window for Decay's
    [0.2, 12] range) couldn't escape the wrong seed.

    Fix: measure tail_t60 by extrapolation from sustained-pink input-off,
    then divide by 1.8 to convert to DV's Decay knob units (empirically
    DV's measured t60 ≈ Decay_knob × 1.8 across plates, halls, ambient).
    """
    x, sr = sf.read(str(sustained_wav))
    m = x.mean(axis=1) if x.ndim > 1 else x
    off = int(4.0 * sr)
    if off >= len(m):
        return 1.0, 999.0
    tail = m[off:]
    win = max(int(0.01 * sr), 1)
    pw = tail ** 2
    sm = np.convolve(pw, np.ones(win) / win, mode='same')
    peak = float(np.max(sm))
    if peak < 1e-12:
        return 1.0, 999.0
    pidx = int(np.argmax(sm))
    start_idx = pidx + int(0.05 * sr)
    if start_idx >= len(sm):
        return 1.0, 999.0
    floor = float(np.median(sm[-min(int(0.5 * sr), len(sm)):]))
    thr40 = max(peak * 10 ** (-40 / 10), floor * 4.0)
    below40 = np.where((np.arange(len(sm)) > start_idx) & (sm < thr40))[0]
    end_idx = int(below40[0]) if len(below40) else len(sm) - 1
    if end_idx - start_idx < int(0.05 * sr):
        return 1.0, 999.0
    seg = sm[start_idx:end_idx]
    db = 10 * np.log10(seg + 1e-30)
    t = np.arange(len(db)) / sr
    A = np.vstack([t, np.ones_like(t)]).T
    slope_db_per_s, intercept = np.linalg.lstsq(A, db, rcond=None)[0]
    if slope_db_per_s >= -0.5:
        return 1.0, 999.0
    # Slope-fit residual RMSE in dB — high RMSE = noisy / non-linear decay
    # (modulator wobble, mode beating). Used to widen sigma0 dynamically so
    # CMA-ES explores wider around uncertain seeds.
    rmse_db = float(np.sqrt(np.mean((db - (slope_db_per_s * t + intercept)) ** 2)))
    t60 = -60.0 / slope_db_per_s
    knob = max(0.2, min(12.0, t60 / 1.8))
    return knob, rmse_db


def stage1_loss(dv_files, lex_files):
    """Envelope-shape L1 (snare) + stereo correlation gap (noiseburst) +
    osc P2P gap (envelope ripple from modulator). Stage 1 owns mod_depth +
    mod_rate, so it must see what the modulator does to the envelope.
    Without osc_p2p, Stage 1's loss is blind to the gate that catches Lex's
    heavy pumping vs DV's mild random-walk LFO."""
    x_dv, sr = sf.read(dv_files["snare"])
    x_lx, _ = sf.read(lex_files["snare"])
    m_dv = x_dv.mean(axis=1) if x_dv.ndim>1 else x_dv
    m_lx = x_lx.mean(axis=1) if x_lx.ndim>1 else x_lx
    env_l1 = envelope_shape_l1(m_dv, m_lx, sr, post_peak_ms=500.0)
    if env_l1 != env_l1:
        env_l1 = 10.0  # NaN sentinel

    # Stereo correlation on noiseburst
    x_dv, sr = sf.read(dv_files["noiseburst"])
    x_lx, _ = sf.read(lex_files["noiseburst"])
    if x_dv.ndim == 2 and x_lx.ndim == 2:
        L_dv, R_dv = x_dv[:,0], x_dv[:,1]
        L_lx, R_lx = x_lx[:,0], x_lx[:,1]
        pk_dv = int(np.argmax(0.5*(np.abs(L_dv)+np.abs(R_dv))))
        pk_lx = int(np.argmax(0.5*(np.abs(L_lx)+np.abs(R_lx))))
        try:
            r_dv = float(np.corrcoef(L_dv[pk_dv:], R_dv[pk_dv:])[0,1])
            r_lx = float(np.corrcoef(L_lx[pk_lx:], R_lx[pk_lx:])[0,1])
        except Exception:
            r_dv = 0.0; r_lx = 0.0
        stereo_gap = abs(r_dv - r_lx)
    else:
        stereo_gap = 0.0

    # Modulator-induced envelope ripple — measured on noiseburst with
    # detrended 5ms-smoothed log envelope (same fn the gate uses).
    o_dv = osc_envelope_p2p(dv_files["noiseburst"])
    o_lx = osc_envelope_p2p(lex_files["noiseburst"])
    if o_dv is None or o_lx is None:
        osc_gap = 0.0  # below noise floor; skip
    else:
        osc_gap = abs(o_dv - o_lx)

    # Weights: 3 dB env L1 = unit, 0.3 stereo gap = unit, 4 dB osc gap = unit
    # (matches full_check.py gate threshold of ±4 dB on osc P2P).
    # osc weight is balanced 1.0× — over-correction to 2.0× caused Stage 1 to
    # find Mod-Depth-heavy local minima (0.385 on Vocal Hall) that destabilized
    # Stage 2's decay landscape. Param bounds on Mod Depth + Size per category
    # are the right control surface, not loss weight.
    loss = (1.0 * (env_l1 / 3.0) ** 2
            + 0.5 * (stereo_gap / 0.3) ** 2
            + 1.0 * (osc_gap / 4.0) ** 2)
    info = {"env_shape_l1_dB": env_l1, "stereo_gap": stereo_gap,
            "osc_gap_dB": osc_gap}
    if o_dv is not None: info["osc_p2p_dv"] = o_dv
    if o_lx is not None: info["osc_p2p_lx"] = o_lx
    return loss, info


def stage2_loss(dv_files, lex_files):
    """Per-band EDT (t10) + t30 + t60 across sub/low/low_mid/mid/hi.
    Heaviest weight on EDT (perception of weight/hold). low_mid band gets
    a 3× perceptual-priority multiplier — this is the 250-500 Hz vocal
    body region where the engine ceiling was hurting us most on the prior
    sweep (Lex held 126ms EDT; DV collapsed to 43ms)."""
    # (name, lo, hi, band_weight). band_weight applies on top of the t-level
    # weights below. low_mid gets 3× per the 4-refinement spec.
    bands = [('sub',     20,   100, 1.0),
             ('low',     100,  250, 1.0),
             ('low_mid', 250,  500, 3.0),
             ('mid',     500, 2000, 1.0),
             ('hi',     2000, 8000, 1.0)]
    loss = 0.0
    info = {}
    for name, lo, hi, bw in bands:
        for db, w in [(10, 2.0), (30, 1.0), (60, 0.5)]:
            dv_t = _band_decay_time(dv_files["sustained"], lo, hi, db)
            lx_t = _band_decay_time(lex_files["sustained"], lo, hi, db)
            if not dv_t or not lx_t or lx_t < 0.005:
                continue
            d_pct = (dv_t - lx_t) / lx_t
            loss += bw * w * (d_pct / 0.20) ** 2
            info[f"{name}_t{db}_pct"] = d_pct * 100

    # Spectral peak penalty — catches FDN mode resonances that don't show up
    # in band-averaged decay metrics. Vocal Hall v12 had a 7 dB peak at
    # 320 Hz (Low Crossover = 246 sat too close to a mode boundary, creating
    # an audible "muddy" resonance the user flagged in A/B). This term puts
    # the cure where the knobs live (Mults + Crossovers in Stage 2), since
    # Stage 3's Lo/Hi Cut + Sat + Trim cannot reshape mid-band spectrum.
    # Mud band (200-500 Hz) gets asymmetric weight — only penalize DV hotter
    # than Lex, since "muddy" is excess mid-low energy.
    try:
        m_dv = compute_metrics(dv_files["noiseburst"])
        m_lx = compute_metrics(lex_files["noiseburst"])

        # Master peak-aligned tail-time penalty. Per-band t30/t60 above can be
        # satisfied by a longer master decay paired with per-band damping —
        # the exploit Bright Hall v1 exposed (anchor t60=3.51s, optimizer
        # picked Decay Time=6.54s → 86% overshoot but per-band weights happy).
        # Mirror full_check's tail_t30 / tail_t60 measurement so the optimizer
        # sees the same gate it'll be judged against.
        for k, weight in (("tail_t30", 4.0), ("tail_t60", 2.0)):
            dv_t = m_dv.get(k)
            lx_t = m_lx.get(k)
            if dv_t is None or lx_t is None or lx_t <= 0.05:
                continue
            rel = (dv_t - lx_t) / lx_t
            # Squared relative error normalized to 25 % (full_check gate width)
            # so |rel|=25 % contributes 1.0 × weight to the loss surface.
            loss += weight * (rel / 0.25) ** 2
            info[f"master_{k}_pct"] = rel * 100

        oct_dv = m_dv.get("oct_db_norm")
        oct_lx = m_lx.get("oct_db_norm")
        if hasattr(oct_dv, "__len__") and hasattr(oct_lx, "__len__"):
            n = min(len(oct_dv), len(oct_lx))
            diff = np.asarray(oct_dv[:n]) - np.asarray(oct_lx[:n])
            max_dev = float(np.max(np.abs(diff)))
            loss += 2.0 * (max_dev / 5.0) ** 2     # 5 dB = unit
            info["spec_L1_max_dB"] = max_dev
            # Bass-clarity asymmetric penalty (100-500 Hz). Catches the
            # "boomy / muddy / less clear" listening complaint that scalar
            # spec_L1 averages miss.
            #
            # Listening verdict 2026-05-28: 4 of 5 user-audited presets failed
            # by ear despite gates passing. Bright Hall specifically flagged
            # as "boomier and less clear" — DV bass-mid region heavier than
            # VVV. Vocal Hall same family of complaint ("muddy").
            #
            # Math:
            #   For each 1/3-oct bin in 100-500 Hz:
            #     diff = DV_dB - Lex_dB    (positive = DV hotter = boomy)
            #     if diff > 0  → CUBIC penalty (heavy non-linear)
            #     if diff <= 0 → LINEAR penalty (standard gradient)
            #
            # Cubic on the hot side punishes 3-6 dB excursions far more than
            # 1-2 dB ones, forcing the optimizer AWAY from boomy Bass Multiply
            # / oversized Low Crossover combos. Linear on the cold side keeps
            # the gradient smooth so DV doesn't overshoot into a thin sound.
            fcs = m_dv.get("oct_centers")
            if hasattr(fcs, "__len__"):
                bass_hot_cubic = 0.0     # cubic penalty for DV > Lex (boomy)
                bass_cold_sqr  = 0.0     # quadratic for DV < Lex (thin)
                n_hot  = 0
                n_cold = 0
                max_hot_dev = 0.0
                for i, fc in enumerate(fcs[:n]):
                    if 100.0 <= fc <= 500.0:
                        d = float(diff[i])
                        if d > 0.0:
                            # Cubic: small overages cheap, big overages brutal.
                            # Normalized to 3 dB unit so |d|=3 dB contributes 1.0.
                            bass_hot_cubic += (d / 3.0) ** 3
                            n_hot += 1
                            if d > max_hot_dev: max_hot_dev = d
                        else:
                            # Linear-in-energy (quadratic in dB) standard gradient.
                            bass_cold_sqr += (d / 4.0) ** 2
                            n_cold += 1
                if n_hot + n_cold > 0:
                    # Heavy weight on the hot side (5×), light on cold (1×).
                    # Asymmetry ratio explicitly tuned to overpower spec_L1
                    # mean's symmetric pull.
                    bass_loss = (5.0 * bass_hot_cubic / max(n_hot, 1)
                               + 1.0 * bass_cold_sqr / max(n_cold, 1))
                    loss += bass_loss
                    info["bass_clarity_max_hot_dB"] = max_hot_dev
                    info["bass_clarity_loss"]       = bass_loss
    except Exception as e:
        sys.stderr.write(f"stage2 spec-peak term raised: {e}\n")

    # ───────────────────────────────────────────────────────────────────────
    # BOOM term — late-window low-band integrated RMS.
    # Bass-clarity above measures steady-state spectrum (0-100 ms window in
    # third-octave magnitude). Boom catches the temporal envelope: bass that
    # lingers after the anchor's structural damping has attenuated it.
    #
    # 2026-05-29 v17 rebalance: windows moved to 1.0-2.5 s + 2.0-3.5 s
    # (NO overlap with body_sustain 300-800 ms window). v16 broke because
    # boom 500ms-1s + body 300-800ms both wanted the same Decay axis but
    # in opposite directions — optimizer crashed Decay to clamp floor.
    # Separating windows = each term gets its own time-domain axis.
    try:
        windows = [(1.0, 2.5), (2.0, 3.5)]
        bands   = [(40, 100), (80, 200), (100, 300)]
        boom_hot_cubic = 0.0
        boom_cold_sqr  = 0.0
        boom_n_hot = 0
        boom_n_cold = 0
        boom_max_hot = 0.0
        for (t0, t1) in windows:
            for (lo, hi) in bands:
                dv_db = _late_low_rms_db(dv_files["noiseburst"], t0, t1, lo, hi)
                lx_db = _late_low_rms_db(lex_files["noiseburst"], t0, t1, lo, hi)
                if dv_db is None or lx_db is None:
                    continue
                d = dv_db - lx_db
                if d > 0.0:
                    boom_hot_cubic += (d / 2.0) ** 3
                    boom_n_hot += 1
                    if d > boom_max_hot: boom_max_hot = d
                else:
                    boom_cold_sqr += (d / 4.0) ** 2
                    boom_n_cold += 1
        if boom_n_hot + boom_n_cold > 0:
            boom_loss = (6.0 * boom_hot_cubic / max(boom_n_hot, 1)
                       + 1.0 * boom_cold_sqr  / max(boom_n_cold, 1))
            loss += boom_loss
            info["boom_max_hot_dB"] = boom_max_hot
            info["boom_loss"]       = boom_loss
    except Exception as e:
        sys.stderr.write(f"stage2 boom term raised: {e}\n")

    # ───────────────────────────────────────────────────────────────────────
    # TAIL MOD RIPPLE term — detrended dB-envelope std per band.
    # Broadband osc_p2p (full_check) is averaged across the spectrum and
    # missed VH v14's per-band wobble: mid 1-4 kHz ripple std 4.15 dB vs
    # Lex 0.95 dB (+3.20 dB hot — audible slow swell-and-fade).
    # Asymmetric: only DV-hotter is penalised (DV smoother than Lex is fine).
    # Cubic at 1 dB unit + 4× weight on hot side because audibility of
    # tail tremolo is highly non-linear (1 dB barely audible, 3 dB obvious).
    try:
        ripple_bands = [(40, 250, 'bass'),
                        (250, 1000, 'lowmid'),
                        (1000, 4000, 'mid'),
                        (4000, 12000, 'high')]
        mod_hot_cubic = 0.0
        mod_cold_sqr  = 0.0
        mod_n_hot = 0
        mod_n_cold = 0
        mod_max_hot = 0.0
        for (lo, hi, _bname) in ripple_bands:
            dv_std = _tail_envelope_ripple_db(dv_files["noiseburst"], 0.5, 3.0, lo, hi)
            lx_std = _tail_envelope_ripple_db(lex_files["noiseburst"], 0.5, 3.0, lo, hi)
            if dv_std is None or lx_std is None:
                continue
            d = dv_std - lx_std
            if d > 0.0:
                mod_hot_cubic += d ** 3              # 1 dB unit
                mod_n_hot += 1
                if d > mod_max_hot: mod_max_hot = d
            else:
                mod_cold_sqr += (d / 2.0) ** 2
                mod_n_cold += 1
        if mod_n_hot + mod_n_cold > 0:
            mod_loss = (4.0 * mod_hot_cubic / max(mod_n_hot, 1)
                      + 0.5 * mod_cold_sqr  / max(mod_n_cold, 1))
            loss += mod_loss
            info["tail_mod_max_hot_dB"] = mod_max_hot
            info["tail_mod_loss"]       = mod_loss
    except Exception as e:
        sys.stderr.write(f"stage2 tail-mod term raised: {e}\n")

    # ───────────────────────────────────────────────────────────────────────
    # HF BLOOM term — early-window (50-300 ms post-peak) 4-8 kHz hot penalty.
    # VH v15 listening verdict: DV measured +1.6 dB hot at 4-8 kHz during
    # bloom — audible as "brighter". Asymmetric cubic on hot side at 1 dB
    # unit (HF audibility is highly non-linear; 1.5 dB is clearly perceptible).
    try:
        bloom_bands = [(2000, 4000), (4000, 8000), (8000, 12000)]
        bloom_hot_cubic = 0.0
        bloom_cold_sqr  = 0.0
        bloom_n_hot = 0
        bloom_n_cold = 0
        bloom_max_hot = 0.0
        for (lo, hi) in bloom_bands:
            dv_db = _post_peak_band_rms_db(dv_files["noiseburst"], 50, 300, lo, hi)
            lx_db = _post_peak_band_rms_db(lex_files["noiseburst"], 50, 300, lo, hi)
            if dv_db is None or lx_db is None: continue
            d = dv_db - lx_db
            if d > 0.0:
                bloom_hot_cubic += d ** 3       # 1 dB unit
                bloom_n_hot += 1
                if d > bloom_max_hot: bloom_max_hot = d
            else:
                bloom_cold_sqr += (d / 3.0) ** 2
                bloom_n_cold += 1
        if bloom_n_hot + bloom_n_cold > 0:
            bloom_loss = (4.0 * bloom_hot_cubic / max(bloom_n_hot, 1)
                        + 0.5 * bloom_cold_sqr  / max(bloom_n_cold, 1))
            loss += bloom_loss
            info["hf_bloom_max_hot_dB"] = bloom_max_hot
            info["hf_bloom_loss"]       = bloom_loss
    except Exception as e:
        sys.stderr.write(f"stage2 hf-bloom term raised: {e}\n")

    # ───────────────────────────────────────────────────────────────────────
    # BODY SUSTAIN term — mid-window (300-800 ms post-peak) 100-2000 Hz
    # cold penalty. VH v15 listening verdict: VVV measured ~1.4 dB warmer
    # than DV across the body region — audible as "VVV fuller / DV thinner".
    # ASYMMETRIC INVERTED vs bass_clarity: cold-cubic 5× / hot-quad 1×.
    # Forces the optimizer to FILL the body region instead of scoop it.
    try:
        body_bands = [(125, 250), (250, 500), (500, 1000), (1000, 2000)]
        body_cold_cubic = 0.0
        body_hot_sqr    = 0.0
        body_n_cold = 0
        body_n_hot  = 0
        body_max_cold = 0.0
        for (lo, hi) in body_bands:
            dv_db = _post_peak_band_rms_db(dv_files["noiseburst"], 300, 800, lo, hi)
            lx_db = _post_peak_band_rms_db(lex_files["noiseburst"], 300, 800, lo, hi)
            if dv_db is None or lx_db is None: continue
            d = dv_db - lx_db
            if d < 0.0:
                # DV cold = thin body = "less full". CUBIC penalty.
                body_cold_cubic += (-d) ** 3    # 1 dB unit
                body_n_cold += 1
                if -d > body_max_cold: body_max_cold = -d
            else:
                # DV hot in body = OK (means full). Light quadratic for symmetry.
                body_hot_sqr += (d / 3.0) ** 2
                body_n_hot += 1
        if body_n_cold + body_n_hot > 0:
            # 3× hot weight (was 5×) after v16 over-dominated and forced
            # Decay floor. Still asymmetric inverted vs boom but less brutal.
            body_loss = (3.0 * body_cold_cubic / max(body_n_cold, 1)
                       + 1.0 * body_hot_sqr    / max(body_n_hot, 1))
            loss += body_loss
            info["body_max_cold_dB"] = body_max_cold
            info["body_loss"]        = body_loss
    except Exception as e:
        sys.stderr.write(f"stage2 body term raised: {e}\n")

    # ───────────────────────────────────────────────────────────────────────
    # PER-BAND RT60 (Schroeder backward integration on noiseburst tail).
    # The blind spot every prior sweep optimised against — per-band tail_t60
    # / tail_t30 were ungated and the loss surface had no incentive to match
    # frequency-dependent decay. VH Phase 3 audit revealed DV bass 33% cold,
    # low-mid 22% cold, air 26% cold despite passing every prior gate.
    # Squared relative error per band, summed. Bands 63 Hz – 16 kHz.
    try:
        rt_bands = [(44,    88), (88,   177), (177,  355), (355,  710),
                    (710,  1420), (1420, 2840), (2840, 5680),
                    (5680, 11360), (11360, 18000)]
        rt60_err_sq = 0.0
        rt60_n = 0
        rt60_max_pct = 0.0
        for (lo, hi) in rt_bands:
            dv_t = _t60_band_schroeder (dv_files["noiseburst"], lo, hi)
            lx_t = _t60_band_schroeder (lex_files["noiseburst"], lo, hi)
            if dv_t is None or lx_t is None or lx_t <= 0.05:
                continue
            rel = (dv_t - lx_t) / lx_t
            rt60_err_sq += (rel / 0.05) ** 2   # normalized to ±5 % JND
            rt60_n += 1
            if abs(rel) * 100.0 > rt60_max_pct: rt60_max_pct = abs(rel) * 100.0
        if rt60_n > 0:
            # Weight 6.0× per band (hardened from 3.0×). The first un-blinded
            # BH sweep walked away from a hand-set 0.50/1.75 tilt at 3.0×
            # weight, scoring easy Stage-3 spectral points instead. Doubling
            # to 6× makes RT60-band the absolute dominant term in Stage 2 so
            # the sampler cannot trade per-band decay shape for spec_L1 gains.
            rt60_loss = 6.0 * rt60_err_sq / max(rt60_n, 1)
            loss += rt60_loss
            info["rt60_band_max_pct"] = rt60_max_pct
            info["rt60_band_loss"]    = rt60_loss
    except Exception as e:
        sys.stderr.write(f"stage2 rt60-band term raised: {e}\n")

    # ───────────────────────────────────────────────────────────────────────
    # PER-BAND TAIL MOD PEAK FREQUENCY — Hilbert env FFT peak in 0.3-8 Hz.
    # Catches per-band rate inversion (VH v15: DV bass 3.3 Hz vs VVV 1.83 Hz)
    # that the tail_mod_ripple amplitude std cannot see. Squared relative
    # error in Hz, summed across 4 bands.
    try:
        mf_bands = [(40,   250),
                    (250,  1000),
                    (1000, 4000),
                    (4000, 12000)]
        mf_err_sq = 0.0
        mf_n = 0
        for (lo, hi) in mf_bands:
            dv_f = _tail_mod_peak_freq (dv_files["noiseburst"], 1.0, 3.0, lo, hi)
            lx_f = _tail_mod_peak_freq (lex_files["noiseburst"], 1.0, 3.0, lo, hi)
            if dv_f is None or lx_f is None or lx_f < 0.4:
                continue
            rel = (dv_f - lx_f) / lx_f
            mf_err_sq += (rel / 0.30) ** 2   # normalized to ±30 % gate
            mf_n += 1
        if mf_n > 0:
            mf_loss = 2.0 * mf_err_sq / max(mf_n, 1)
            loss += mf_loss
            info["tail_mod_freq_loss"] = mf_loss
    except Exception as e:
        sys.stderr.write(f"stage2 tail-mod-freq term raised: {e}\n")

    return loss, info


def stage3_loss(dv_files, lex_files):
    """Steady-state per-band L1 + cent_50 + cent_500 + snare RMS + spec_L1.
    Pure EQ-shape and level objectives. HF persistence bands (hi 5-10k,
    air 10-20k) carry 3× weight after Vocal Hall v11 listening test reported
    DV "brighter / trashier" — these are the bands the human ear flags for
    fizzy / cymbal-y character. Asymmetric: penalty ONLY when DV > Lex
    (we don't penalize DV being darker than the anchor)."""
    # (lo, hi, name, weight, asymmetric_hot)
    bands = [(20,    50,    'deep_sub', 2.0, False),
             (50,    100,   'sub',      2.0, False),
             (100,   250,   'low',      2.0, False),
             (250,   500,   'low_mid',  1.5, False),
             (500,   2000,  'mid',      1.5, False),
             (2000,  5000,  'umid',     1.0, False),
             (5000,  10000, 'hi',       3.0, True),   # 3× weight + asymmetric
             (10000, 20000, 'air',      3.0, True)]   # — punish HF bloom
    loss = 0.0
    info = {}
    for lo, hi, name, w, asym in bands:
        dv_b = _ss_band_rms_db(dv_files["sustained"], lo, hi)
        lx_b = _ss_band_rms_db(lex_files["sustained"], lo, hi)
        d = dv_b - lx_b
        if asym and d < 0:
            term = 0.5 * (d / 3.0) ** 2     # mild penalty if DV darker
        else:
            term = (d / 3.0) ** 2            # full penalty if DV hotter / off
        loss += w * term
        info[f"ss_{name}_d"] = d

    # cent_50 (50-500 ms early bloom centroid) + cent_500 (500-1500 ms tail).
    # cent_500 added after Vocal Hall v11 — Lex VVV darkens the tail via
    # Color Mode; FDN needs explicit pressure to track this. Both at weight
    # 4 on the 15% tolerance scale (matches the full_check gate ±15%).
    m_dv = compute_metrics(dv_files["noiseburst"])
    m_lx = compute_metrics(lex_files["noiseburst"])
    c50_dv = m_dv.get("cent_50"); c50_lx = m_lx.get("cent_50")
    if c50_dv == c50_dv and c50_lx == c50_lx and c50_lx > 1.0:
        d_pct = abs(c50_dv - c50_lx) / c50_lx
        loss += 4.0 * (d_pct / 0.15) ** 2
        info["cent_50_pct"] = (c50_dv - c50_lx) / c50_lx * 100
    c500_dv = m_dv.get("cent_500"); c500_lx = m_lx.get("cent_500")
    if c500_dv == c500_dv and c500_lx == c500_lx and c500_lx > 1.0:
        d_pct = abs(c500_dv - c500_lx) / c500_lx
        loss += 4.0 * (d_pct / 0.15) ** 2
        info["cent_500_pct"] = (c500_dv - c500_lx) / c500_lx * 100

    # Snare RMS
    def _rms(p):
        x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim>1 else x
        pk = int(np.argmax(np.abs(m)))
        return float(20*np.log10(np.sqrt(np.mean(m[pk:]**2))+1e-30))
    snare_d = _rms(dv_files["snare"]) - _rms(lex_files["snare"])
    loss += 2.0 * (snare_d / 1.5) ** 2
    info["snare_rms_d"] = snare_d

    # spec_L1 mean from compute_metrics
    if hasattr(m_dv["oct_db_norm"], "__len__"):
        n = min(len(m_dv["oct_db_norm"]), len(m_lx["oct_db_norm"]))
        spec_l1 = float(np.mean(np.abs(m_dv["oct_db_norm"][:n] - m_lx["oct_db_norm"][:n])))
        loss += 1.0 * (spec_l1 / 2.0) ** 2
        info["spec_l1_db"] = spec_l1

    return loss, info


# ─── Stage runner ────────────────────────────────────────────────────────────

def run_stage(stage_name, active_params, locked_overrides, loss_fn,
              preset, anchor_files, vst3, n_trials, workers, scratch,
              x0_overrides=None, range_overrides=None, sigma_scale=None):
    """Run one CMA-ES stage. Returns the best params (active subset only).

    Params sampled in NORMALIZED [0,1] space internally so sigma0=0.2 means
    20% of each param's range — uniform exploration regardless of native scale.
    Denormalized only at render time.

    range_overrides={name: (lo, hi)} narrows ALL_PARAMS ranges for THIS stage
    only. Used to clamp Stage 3 shelf gains to [-6, +6] dB, preventing the
    "fix temporal gap with a +14 dB shelf" cheat the prior architecture left
    open.
    """
    ranges = dict(ALL_PARAMS)
    if range_overrides:
        ranges.update(range_overrides)
    print(f"\n══════════ STAGE: {stage_name} ══════════")
    print(f"  Active params: {sorted(active_params)}")
    print(f"  Trials: {n_trials}   Workers: {workers}   Sampler: CMA-ES")
    print(f"  Locked params: {len(locked_overrides)} fixed values")
    sigma0 = 0.2
    if sigma_scale:
        # CmaEsSampler takes a single global sigma0 (anisotropic per-axis sigma
        # is unsupported), so widen from the DECAY scale specifically — using
        # max() over all axes would unintentionally widen every parameter.
        s = sigma_scale.get('decay', 1.0)
        if s > 1.01:
            sigma0 = min(0.5, 0.2 * s)
            print(f"  Sigma scaled:  {sigma0:.3f} (global sigma0 from decay ×{s:.2f}; "
                  f"CMA-ES has no per-axis sigma)")
    print()

    # Sample in normalized [0,1] for every active param. x0 at midpoint (0.5)
    # unless caller passes a target-informed seed for any param (e.g. Stage 1
    # seeding Decay Time from anchor-measured RT60).
    x0_norm = {n: 0.5 for n in active_params}
    if x0_overrides:
        for k, v in x0_overrides.items():
            if k in active_params:
                lo, hi = ranges[k]
                x0_norm[k] = max(0.0, min(1.0, (v - lo) / (hi - lo)))

    sampler = optuna.samplers.CmaEsSampler(
        x0=x0_norm, sigma0=sigma0, seed=42,
        n_startup_trials=max(workers, 8),
    )
    study = optuna.create_study(direction="minimize", sampler=sampler)

    # ASCII-only sanitized stage name for filesystem paths.
    # The harness re-interprets UTF-8 bytes as Latin-1 when constructing
    # output paths, so an em-dash in the stage name creates a divergent
    # directory (UTF-8 "—" vs Latin-1 "â") and the wrapper can't find the
    # rendered WAVs. Strip to alphanumerics + underscores.
    stage_slug = "".join(c if c.isalnum() else "_"
                        for c in stage_name.lower())

    def objective(trial):
        sample = {}
        for name in active_params:
            u = trial.suggest_float(name, 0.0, 1.0)
            lo, hi = ranges[name]
            sample[name] = lo + u * (hi - lo)
        overrides = {**locked_overrides, **sample}
        out = scratch / f"{stage_slug}_{trial.number:04d}"
        files = render(preset, overrides, vst3, out)
        if files is None:
            return 1e6
        try:
            loss, info = loss_fn(files, anchor_files)
        except Exception as e:
            sys.stderr.write(f"stage loss raised: {e}\n")
            return 1e6
        for k, v in info.items():
            if isinstance(v, (int, float)) and v == v:
                trial.set_user_attr(k, float(v))
        # Record denormalized values so the report shows real units.
        for k, v in sample.items():
            trial.set_user_attr(f"denorm_{k}", float(v))
        # Live trial breakdown — first 20 trials + every 25th after — so
        # the bass-clarity penalty's effect on the loss surface is visible
        # in real time during the sweep.
        if trial.number < 20 or trial.number % 25 == 0:
            bc = info.get("bass_clarity_loss", 0.0)
            bch = info.get("bass_clarity_max_hot_dB", 0.0)
            sl1 = (info.get("spec_L1_max_dB")
                   or info.get("spec_l1_db")
                   or info.get("spec_L1") or 0.0)
            print(f"  trial {trial.number:04d}  loss={loss:.4f}  "
                  f"spec={sl1:.3f}  bass_clarity={bc:.4f}  "
                  f"max_hot_dB={bch:+.2f}", flush=True)
        shutil.rmtree(out, ignore_errors=True)
        return loss

    t0 = time.time()
    study.optimize(objective, n_trials=n_trials, n_jobs=workers, show_progress_bar=False)
    elapsed = time.time() - t0
    # Denormalize best params back to native units (using THIS stage's ranges).
    best_denorm = {}
    for k, u in study.best_params.items():
        lo, hi = ranges[k]
        best_denorm[k] = lo + u * (hi - lo)
    print(f"  Stage finished in {elapsed:.1f} s. Best loss = {study.best_value:.4f}")
    print(f"  Best params (denormalized):")
    for k in sorted(best_denorm.keys()):
        print(f"    {k:24s} = {best_denorm[k]:.4f}")
    return best_denorm, study.best_value


# ─── Main ───────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("preset")
    ap.add_argument("--anchor-rendered", required=True,
                    help="Path to Lex anchor noiseburst WAV.")
    ap.add_argument("--vst3", default=str(DEFAULT_VST3))
    ap.add_argument("--workers", type=int, default=6)
    ap.add_argument("--s1-trials", type=int, default=300)
    ap.add_argument("--s2-trials", type=int, default=500)
    ap.add_argument("--s3-trials", type=int, default=500)
    ap.add_argument("--work-dir", default="/tmp/staged_tuner")
    ap.add_argument("--category", default="",
                    help="Preset category (Plates, Halls, Chambers, Rooms, "
                         "Ambient, Springs). Auto-selects the matching profile "
                         "in CATEGORY_RULES: parameter clamps, x0 seeds, and "
                         "has_dpv. No other flags needed.")
    args = ap.parse_args()

    # ─── Resolve category profile + per-preset overrides ──────────────────
    profile = dict(CATEGORY_RULES.get(args.category, CATEGORY_RULES[""]))
    if args.category and args.category not in CATEGORY_RULES:
        sys.stderr.write(
            f"warning: category '{args.category}' has no profile — "
            f"falling back to default (no clamps, has_dpv=False).\n"
        )
    # Per-preset overrides take precedence over category defaults — used
    # when one preset in a category uses a different engine (e.g. VVP is
    # the only DPV preset in the Plates category).
    override = PRESET_OVERRIDES.get(args.preset, {})
    for k, v in override.items():
        profile[k] = v
    print(f"\n══════════ PRE-FLIGHT DIAGNOSTIC ══════════")
    print(f"  Preset:     {args.preset}")
    print(f"  Category:   {args.category or '(default)'}")
    print(f"  Profile:    has_dpv={profile['has_dpv']}, "
          f"{len(profile['stage1_ranges'])} stage-1 clamps")

    anchor_n = Path(args.anchor_rendered)
    anchor_s = anchor_n.with_name(anchor_n.name.replace("_noiseburst", "_sustained"))
    anchor_snare = anchor_n.with_name(anchor_n.name.replace("_noiseburst", "_snare"))
    for p in (anchor_n, anchor_s, anchor_snare):
        if not p.exists():
            sys.exit(f"anchor file missing: {p}")
    anchor_files = {"noiseburst": str(anchor_n),
                    "sustained": str(anchor_s),
                    "snare": str(anchor_snare)}

    slug = "".join(c for c in args.preset if c.isalnum() or c in "+-_'")
    work = Path(args.work_dir) / slug
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)

    # ─── Automated anchor profiling ────────────────────────────────────────
    # Slope-fit tail_t60 on the sustained-pink anchor post input-off, convert
    # to DV Decay Time knob (empirically t60 ≈ knob × 1.8). Also reports
    # slope-fit RMSE — high RMSE means modulator wobble / mode beating
    # contaminates the decay, in which case CMA-ES needs a wider sigma0
    # around the seed.
    anchor_decay, fit_rmse = _anchor_decay_estimate(Path(anchor_files["sustained"]))
    print(f"  Anchor t60→knob:    {anchor_decay:.3f} s "
          f"(slope-fit RMSE {fit_rmse:.2f} dB)")
    # Dynamic sigma scale: clamp RMSE-driven scaling to [1.0, 2.5]. Plates
    # with clean decay get tight sigma (0.2 default × 1.0); halls / ambient
    # with noisier decays get widened sigma (up to 0.2 × 2.5 = 0.5
    # normalized).
    decay_sigma_scale = max(1.0, min(2.5, fit_rmse / 2.0))
    print(f"  Decay sigma scale:  {decay_sigma_scale:.2f}× "
          f"(applied to Stage 2 Decay axis)")

    final = {}

    # ─── Stage 1 ──────────────────────────────────────────────────────────
    # Strict ownership: Stage 1 = Spatial + Modulation ONLY. Decay Time is
    # LOCKED at the pre-flight anchor estimate so envelope_shape_L1 measures
    # spatial/mod params against a target-length tail. Without this lock the
    # loss is decay-invariant (peak-normalized envelope over 500 ms doesn't
    # care about absolute decay), and the optimizer drags Decay short to
    # cheat a smoother local envelope — setting a trap Stage 2 inherits
    # via the s1_best → s2_x0 seed chain. Stage 2 owns Decay.
    s1_active = ["Size", "Diffusion", "Width", "Mod Depth", "Mod Rate",
                 "Pre-Delay", "Early Ref Level", "Early Ref Size"]
    s1_locked = {k: v for k, v in STAGE1_FLAT_DEFAULTS.items()
                 if k not in s1_active}
    s1_locked["Decay Time"] = anchor_decay
    # Stage 1 x0: category-profile spatial seeds + safe defaults.
    s1_x0 = {"Mod Depth": 0.10, "Mod Rate": 1.0,
             "Diffusion": 0.5, "Size": 0.85, "Width": 0.9,
             "Pre-Delay": 15.0, "Early Ref Level": 0.45,
             "Early Ref Size": 0.55}
    s1_x0.update(profile["stage1_x0"])

    s1_best, _ = run_stage("Stage 1 — Spatial + Envelope (Feel)",
                            s1_active, s1_locked, stage1_loss,
                            args.preset, anchor_files, args.vst3,
                            args.s1_trials, args.workers, work / "s1",
                            x0_overrides=s1_x0,
                            range_overrides=profile["stage1_ranges"])
    final.update(s1_best)
    (work / "stage1_best.json").write_text(json.dumps(s1_best, indent=2))

    # ─── Stage 2 ──────────────────────────────────────────────────────────
    # Decay is co-tuned again here under the per-band EDT/t30/t60 loss; the
    # Stage 1 estimate becomes the CMA-ES seed via x0_overrides. DPV Struct
    # HF Damp moved IN from Stage 3 — loop-damping fundamentally couples to
    # frequency-dependent decay times and must be solved alongside the band
    # multipliers, not afterwards. Without this, Stage 3 used HF Shelf as a
    # spectral band-aid to compensate for a still-wrong-shape decay.
    s2_active = ["Decay Time", "Treble Multiply", "Bass Multiply", "Mid Multiply",
                 "Low Crossover", "High Crossover"]
    if profile["has_dpv"]:
        s2_active.append("DPV Struct HF Damp")
    s2_locked = {k: v for k, v in STAGE1_FLAT_DEFAULTS.items()
                 if k not in s2_active and k not in s1_best}
    s2_locked.update({k: v for k, v in s1_best.items() if k not in s2_active})
    s2_x0 = {"Decay Time": s1_best.get("Decay Time", anchor_decay)}
    # Stage 2 sigma scaling — when the anchor decay slope-fit is noisy
    # (modulator wobble, mode beating), widen the Decay axis exploration.
    s2_sigma_scale = {"Decay Time": decay_sigma_scale}
    # Dynamic Decay Time clamp: anchor_decay × [0.85, 1.30].
    # v17 tightened from [0.7, 1.5] after v16 crashed Decay to clamp floor
    # to escape contradictory boom + body pushes. Tighter range blocks
    # that escape — optimizer must balance terms within a narrow band
    # around the actual anchor decay.
    s2_range_overrides = {
        "Decay Time": (max(0.2, anchor_decay * 0.85),
                       min(12.0, anchor_decay * 1.30)),
    }
    print(f"  Decay Time clamp:   [{s2_range_overrides['Decay Time'][0]:.2f}, "
          f"{s2_range_overrides['Decay Time'][1]:.2f}] s "
          f"(anchor {anchor_decay:.2f} × [0.85, 1.30])")
    s2_best, _ = run_stage("Stage 2 — Temporal + Decay (Tail)",
                            s2_active, s2_locked, stage2_loss,
                            args.preset, anchor_files, args.vst3,
                            args.s2_trials, args.workers, work / "s2",
                            x0_overrides=s2_x0,
                            sigma_scale=s2_sigma_scale,
                            range_overrides=s2_range_overrides)
    final.update(s2_best)
    (work / "stage2_best.json").write_text(json.dumps(s2_best, indent=2))

    # ─── Stage 3 ──────────────────────────────────────────────────────────
    # DPV Struct HF Damp lives in Stage 2 (couples to per-band decay).
    # Stage 3 shelf-gain knobs clamped to [-6, +6] dB on DPV presets so
    # Stage 3 is true polish rather than a spectral band-aid (prior
    # architecture allowed Optuna to chase a 12+ dB HF Shelf to compensate
    # for residual temporal error).
    # Phase α (2026-05-29): PostTankEQ 4-band gains added to Stage 3 — the
    # Polish stage is where exit-stage spectral correction belongs (linear
    # filtering post-tank, immune to gBase compression). Optimizer now has
    # 9 spectral axes in Stage 3 (5 legacy + 4 PostTankEQ).
    s3_active = ["Lo Cut", "Hi Cut", "Saturation", "Gain Trim", "Hi Cut Shelf",
                 "PostTankEQ Band 0 Gain", "PostTankEQ Band 1 Gain",
                 "PostTankEQ Band 2 Gain", "PostTankEQ Band 3 Gain",
                 # Phase γ Stage 3 axes — decoupled per-band linear gain
                 # trim (independent of damping coefficients).
                 "Post Band Sub Gain", "Post Band Low-Mid Gain",
                 "Post Band Mid-High Gain", "Post Band Air Gain"]
    # Pull category-profile Stage 3 range overrides (e.g. Halls widen Hi Cut
    # floor to 4000 Hz so the optimizer has room to clamp DV's HF tail).
    s3_range_overrides = dict(profile.get("stage3_ranges", {}))
    if profile["has_dpv"]:
        s3_active += ["DPV HF Shelf Gain", "DPV HF Shelf Freq",
                      "DPV Box Cut Gain", "DPV Box Cut Freq",
                      "DPV Bass Shelf Gain", "DPV Bass Shelf Freq"]
        s3_range_overrides.update({
            "DPV HF Shelf Gain":   (-6.0, 6.0),
            "DPV Box Cut Gain":    (-6.0, 6.0),
            "DPV Bass Shelf Gain": (-6.0, 6.0),
        })
    s3_locked = {k: v for k, v in STAGE1_FLAT_DEFAULTS.items()
                 if k not in s3_active and k not in s1_best and k not in s2_best}
    # Stage 2 wins over Stage 1 for any duplicated param (Decay Time, Damp).
    s3_locked.update(s1_best)
    s3_locked.update(s2_best)
    s3_best, _ = run_stage("Stage 3 — Spectral + EQ (Polish)",
                            s3_active, s3_locked, stage3_loss,
                            args.preset, anchor_files, args.vst3,
                            args.s3_trials, args.workers, work / "s3",
                            range_overrides=s3_range_overrides)
    final.update(s3_best)
    (work / "stage3_best.json").write_text(json.dumps(s3_best, indent=2))

    # ─── Final ─────────────────────────────────────────────────────────────
    final_json = work / "final.json"
    final_json.write_text(json.dumps(final, indent=2))
    print(f"\n══════════ FINAL ══════════")
    print(f"  Combined best from 3 stages → {final_json}")
    print(f"  Params:")
    for k in sorted(final.keys()):
        print(f"    {k:24s} = {final[k]:.4f}")

    # Final render + full_check
    final_dir = work / "final"
    files = render(args.preset, final, args.vst3, final_dir)
    if not files:
        sys.exit("final render failed")
    print(f"\nFinal render: {final_dir}")
    print("Running full_check...")
    cmd = [sys.executable, str(Path(__file__).resolve().parent / "full_check.py"),
           str(final_dir), str(anchor_n.parent), "--name", args.preset]
    if args.category:
        cmd += ["--category", args.category]
    subprocess.call(cmd)


if __name__ == "__main__":
    main()
