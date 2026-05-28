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
    "Diffusion":        (0.00,   1.00),
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
        oct_dv = m_dv.get("oct_db_norm")
        oct_lx = m_lx.get("oct_db_norm")
        if hasattr(oct_dv, "__len__") and hasattr(oct_lx, "__len__"):
            n = min(len(oct_dv), len(oct_lx))
            diff = np.asarray(oct_dv[:n]) - np.asarray(oct_lx[:n])
            max_dev = float(np.max(np.abs(diff)))
            loss += 2.0 * (max_dev / 5.0) ** 2     # 5 dB = unit
            info["spec_L1_max_dB"] = max_dev
            # Asymmetric mud-band penalty: 1/3-oct fc grid is logarithmic
            # starting at 20 Hz; 200-500 Hz indices are roughly 10-13 on the
            # standard 30-band grid. Treat any positive diff > 3 dB in this
            # zone as a real mud excess and penalize heavily.
            fcs = m_dv.get("oct_centers")
            if hasattr(fcs, "__len__"):
                mud_excess = 0.0
                count = 0
                for i, fc in enumerate(fcs[:n]):
                    if 200.0 <= fc <= 500.0 and diff[i] > 0:
                        mud_excess += diff[i] ** 2
                        count += 1
                if count > 0:
                    mud_rms = (mud_excess / count) ** 0.5
                    loss += 3.0 * (mud_rms / 3.0) ** 2
                    info["mud_band_rms_dB"] = mud_rms
    except Exception as e:
        sys.stderr.write(f"stage2 spec-peak term raised: {e}\n")
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
        s = max(sigma_scale.values())
        if s > 1.01:
            sigma0 = min(0.5, 0.2 * s)
            print(f"  Sigma scaled:  {sigma0:.3f} (×{s:.2f} from base 0.2 — "
                  f"widens decay axis when anchor slope-fit noisy)")
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
    s2_best, _ = run_stage("Stage 2 — Temporal + Decay (Tail)",
                            s2_active, s2_locked, stage2_loss,
                            args.preset, anchor_files, args.vst3,
                            args.s2_trials, args.workers, work / "s2",
                            x0_overrides=s2_x0,
                            sigma_scale=s2_sigma_scale)
    final.update(s2_best)
    (work / "stage2_best.json").write_text(json.dumps(s2_best, indent=2))

    # ─── Stage 3 ──────────────────────────────────────────────────────────
    # DPV Struct HF Damp lives in Stage 2 (couples to per-band decay).
    # Stage 3 shelf-gain knobs clamped to [-6, +6] dB on DPV presets so
    # Stage 3 is true polish rather than a spectral band-aid (prior
    # architecture allowed Optuna to chase a 12+ dB HF Shelf to compensate
    # for residual temporal error).
    s3_active = ["Lo Cut", "Hi Cut", "Saturation", "Gain Trim", "Hi Cut Shelf"]
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
