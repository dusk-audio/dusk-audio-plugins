#!/usr/bin/env python3
"""
Optuna optimizer: tune a DuskVerb factory preset to match an external
reference impulse (e.g. a Valhalla Vintage Verb vpreset render).

Per-trial flow:
  1. Optuna TPE samples values for the 14 free APVTS params.
  2. Subprocess runs the render harness with the parent factory preset
     selected + the 14 --param overrides, into a per-trial output dir
     so parallel workers do not collide.
  3. metrics_external.compare() scores the resulting impulse WAV
     against the target reference IR.
  4. The combined multi-metric loss is returned to Optuna.

Locked params (forced for fair A/B):
  Dry/Wet = 1.0, Bus Mode = 1, Pre-Delay = factory, Early Ref Level/Size
  = factory, Freeze = 0, Mono Below = factory, Gain Trim = 0.

Free params (14):
  Decay Time, Size, Mod Depth, Mod Rate, Treble Multiply, Bass Multiply,
  Mid Multiply, Low Crossover, High Crossover, Diffusion, Lo Cut,
  Hi Cut, Width, Saturation.

CLI:
  --target-ir   path to reference WAV (e.g. VVV vpreset render)
  --dv-preset   DV preset name (must exist in render.cpp's getPresetByName)
  --trials      Optuna budget (default 1500)
  --workers     parallel workers via Optuna n_jobs (default 4)
  --study-name  optional name for sqlite study persistence
  --storage     optional sqlite URL ('sqlite:///tuner.db') for resuming
  --vst3        path to DuskVerb VST3 (default ~/.vst3/DuskVerb.vst3)
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import optuna

# Make sibling module importable when invoked as a script.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from metrics_external import compare, compute_metrics  # noqa: E402


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

REPO_ROOT = Path(__file__).resolve().parents[4]
RENDER_BIN = REPO_ROOT / "build" / "tests" / "duskverb_render" / "duskverb_render"
DEFAULT_VST3 = Path.home() / ".vst3" / "DuskVerb.vst3"

# Free parameter search space. Ranges align with the plugin's APVTS
# RangedAudioParameter clamps (see PluginProcessor.cpp). Sampling outside
# the APVTS range wastes trial budget because the setter clamps and many
# trials produce identical effective values.
FREE_PARAMS = {
    "Decay Time":      (0.20,   12.00),    # APVTS [0.2, 30.0] — 12s ceiling matches longest current preset
    "Size":            (0.10,    1.00),    # APVTS [0.0, 1.0]
    "Mod Depth":       (0.00,    0.60),    # APVTS [0.0, 1.0] — 0.6 keeps tail from over-modulating
    "Mod Rate":        (0.10,    3.00),    # APVTS [0.10, 10.0]
    # Decay multipliers — clamped to gentle contouring range. Wider ranges
    # let Optuna abuse them as massive spectral gap-fillers (e.g. Bass Mult
    # 1.97 + Low Crossover 3740 forced the bass band to cover most of the
    # spectrum, mangling decay trajectories). Forcing EQ work onto bassLift_
    # / hfLift_ shelves keeps decay physics intact.
    "Treble Multiply": (0.50,    1.50),    # APVTS [0.10, 1.50] — clamped
    "Bass Multiply":   (0.50,    1.50),    # APVTS [0.30, 2.50] — clamped
    "Mid Multiply":    (0.50,    1.50),    # APVTS [0.30, 2.50] — clamped
    # Crossovers — clamped to physically realistic bass/treble band edges.
    # Below ~80 Hz the bass band stops separating from sub; above ~600 Hz it
    # collapses the mid band. High crossover above 10 kHz collapses HF into mid.
    "Low Crossover":   (80.0,   600.0),    # APVTS [200, 4000] — clamped
    "High Crossover":  (3000.0, 10000.0),  # APVTS [1000, 12000] — clamped
    "Diffusion":       (0.00,    1.00),    # APVTS [0.0, 1.0]
    "Lo Cut":          (20.0,   500.0),    # APVTS [5, 500] — 20Hz practical floor
    "Hi Cut":          (10000.0, 20000.0), # APVTS [1000, 20000] — clamped above 10 kHz so Optuna can't kill the bright-bloom centroid
    "Width":           (0.50,    1.05),    # APVTS [0.0, 2.0] — clamped to mono-compatible range; >1.05 with sparse Dattorro taps produces anti-correlated L/R
    "Saturation":      (0.00,    0.40),    # APVTS [0.0, 1.0] — 0.4 cap; above is destructive
    # DattorroPlateVintage corrective EQ + brightness (algo=1 only). Optimizer
    # samples these unconditionally; on non-DPV engines the setters are no-ops
    # via DuskVerbEngine glue, so the values are wasted but harmless.
    "DPV HF Shelf Gain":   (-12.0,   24.0),    # APVTS [-12, 24] dB
    "DPV HF Shelf Freq":   (2000.0, 20000.0),  # APVTS [2000, 20000] Hz — extended to reach >12 kHz air band
    "DPV Struct HF Damp":  (2000.0, 18000.0),  # APVTS [2000, 18000] Hz
    "DPV Box Cut Gain":    (-12.0,    6.0),    # APVTS [-12, 6] dB (negative = corrective cut)
    "DPV Box Cut Freq":    (100.0,  800.0),    # APVTS [100, 800] Hz
    "DPV Bass Shelf Gain": (-6.0,   18.0),     # APVTS [-6, 18] dB
    "DPV Bass Shelf Freq": (60.0,   500.0),    # APVTS [60, 500] Hz
    # Gain Trim is now a FREE parameter — Optuna jointly optimizes loudness
    # match with every other axis. Previously locked at 0 + level-matched
    # post-sweep, which pushed absolute band energies hot (because the trim
    # adjustment happened AFTER the band loss was evaluated). Free range
    # [-12, +24] covers all observed snare-match offsets.
    "Gain Trim":            (-12.0, 24.0),     # APVTS [-48, 48] dB — clamped
}

# Locked overrides applied on top of the preset's factory baseline.
# Forces 100% wet bus rendering with neutral gain trim so the optimizer
# cannot game volume via Gain Trim or mix.
LOCKED_OVERRIDES = {
    "Dry/Wet": 1.0,
    "Bus Mode": 1,
    "Freeze": 0,
    # Gain Trim was previously locked at 0 (post-sweep level-match handled it).
    # Now it's in FREE_PARAMS so Optuna optimizes loudness jointly with spectrum.
    # Post-sweep auto-trim is no longer needed and would re-introduce the
    # band-energy shift bug.
}


# ---------------------------------------------------------------------------
# Subprocess render
# ---------------------------------------------------------------------------

def render_trial(
    preset_name: str,
    overrides: dict,
    vst3_path: Path,
    out_dir: Path,
    stimulus: str = "noiseburst",
    prerun_seconds: float = 5.0,
    timeout_s: float = 120.0,
) -> Path | None:
    """
    Run the harness once with the given param overrides. Returns the path
    to the resulting <preset>_{stimulus}.wav, or None on failure/timeout.

    `stimulus` selects which harness-rendered stimulus file the optimizer
    measures against:
      'noiseburst' (default) — 50 ms pink noise burst then silence. The
        broadband + temporally-bounded stimulus that best matches what
        a listener actually hears (snare/vocal-chop response). Avoids the
        Dirac-impulse trap where time-variant reverbs respond differently
        to a 1-sample click than to real-world transients.
      'impulse' — legacy LTI fingerprint. Use only for static engines.
      'snare' — real percussive transient.

    `prerun_seconds` controls the warm-up silence before each stimulus
    fires. 5 s is the convergence-tested standard; <3 s misses steady-
    state modulator drift and produces measurements that don't match
    DAW perception.
    """
    out_dir.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(RENDER_BIN),
        "--vst3", str(vst3_path),
        "--output-dir", str(out_dir),
        "--prerun-seconds", str(prerun_seconds),
        # Render sustained-pink stimulus alongside noiseburst/snare/impulse.
        # Sustained-pink = 4s continuous input + 4s tail; captures the
        # engine's steady-state response under musical-content excitation.
        # The 100 ms noiseburst doesn't reach modal steady-state, so it
        # missed Lex's sustained low-end and HF damping. Cost: ~8s extra
        # render time per trial (acceptable on 1500-trial sweeps).
        "--sustained-pink-seconds", "4.0",
    ]
    for name, value in overrides.items():
        cmd += ["--param", f"{name}={value}"]
    cmd.append(preset_name)

    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=timeout_s,
        )
    except subprocess.TimeoutExpired:
        return None

    if result.returncode != 0:
        # First failure of a worker should be visible; subsequent failures
        # are suppressed by Optuna's trial-failure handling.
        sys.stderr.write(f"render failed (rc={result.returncode}):\n")
        sys.stderr.write(result.stderr[-500:] + "\n")
        return None

    # Harness writes "<PresetTokenName>_<stimulus>.wav" — strip spaces and quotes.
    preset_token = "".join(c for c in preset_name if c.isalnum() or c in "+-_'")
    impulse = out_dir / f"{preset_token}_{stimulus}.wav"
    if not impulse.exists():
        # Try the slug form (lowercased, hyphens). Some harness branches do this.
        candidates = sorted(out_dir.glob(f"*_{stimulus}.wav"))
        if candidates:
            impulse = candidates[0]
        else:
            return None
    return impulse


# ---------------------------------------------------------------------------
# Optuna objective
# ---------------------------------------------------------------------------

def make_objective(
    target_ir: Path,
    preset_name: str,
    vst3_path: Path,
    trial_root: Path,
    fail_loss: float = 1000.0,
    stimulus: str = "noiseburst",
    prerun_seconds: float = 5.0,
):
    """
    Returns an Optuna objective closure that renders one trial via
    the harness and returns the multi-metric loss vs the target IR.

    `stimulus` and `prerun_seconds` control what kind of rendered audio
    the optimizer measures — see render_trial() docstring.
    """
    target_ir = str(target_ir)

    def objective(trial: optuna.Trial) -> float:
        # Sample free params.
        overrides = dict(LOCKED_OVERRIDES)
        for name, (lo, hi) in FREE_PARAMS.items():
            overrides[name] = trial.suggest_float(name, lo, hi)

        # Per-trial output dir keeps parallel workers from colliding.
        out_dir = trial_root / f"trial_{trial.number:05d}"
        impulse = render_trial(
            preset_name=preset_name,
            overrides=overrides,
            vst3_path=vst3_path,
            out_dir=out_dir,
            stimulus=stimulus,
            prerun_seconds=prerun_seconds,
        )
        if impulse is None:
            return fail_loss

        try:
            loss, breakdown = compare(str(impulse), target_ir)
        except Exception as exc:
            sys.stderr.write(f"compare() raised: {exc}\n")
            return fail_loss

        # ── EQ-extremes regularization (DISABLED for peak-aligned sweep) ──
        # Previously penalized DPV shelf magnitudes above ±6 dB to keep the
        # optimizer from chasing the 1/3-oct contour with extreme multipliers.
        # Disabled 2026-05-27: with peak-aligned + noise-gated metrics, the
        # shelves NEED full ±24 dB freedom to close the +16 dB bass deficit
        # at 101 Hz and the bright-bloom centroid gap. Re-enable only if the
        # post-sweep listening test reveals colored/unmusical artifacts.
        trial.set_user_attr('eq_penalty', 0.0)

        # ── Tail-length sanity check ──
        # Reject t30 or t60 outside gate range. tail_shape_term in compare()
        # is too gentle alone (averages 3 points with clamp); a single t30
        # miss at -37% only contributes ~0.14 to the term. Hard caps below
        # make Optuna feel the miss.
        dv_t30 = breakdown.get('tail_t30_dv')
        vv_t30 = breakdown.get('tail_t30_vvv')
        if dv_t30 and vv_t30 and vv_t30 > 0:
            d_pct = abs(dv_t30 - vv_t30) / vv_t30
            if d_pct > 0.15:   # mirrors full_check tail_t30 gate ±15%
                loss += 8.0 * (d_pct - 0.15) ** 1.5
            # original 2× ratio bomb retained for catastrophic outliers
            ratio = dv_t30 / vv_t30
            if ratio > 2.0 or ratio < 0.5:
                loss += 10.0 * (max(ratio, 1.0/ratio) - 2.0)
        dv_t60 = breakdown.get('tail_t60_dv')
        vv_t60 = breakdown.get('tail_t60_vvv')
        if dv_t60 and vv_t60 and vv_t60 > 0:
            d_pct = abs(dv_t60 - vv_t60) / vv_t60
            if d_pct > 0.25:   # mirrors full_check tail_t60 gate ±25%
                loss += 4.0 * (d_pct - 0.25) ** 1.5

        # ── cent_50 hard cap (mirrors full_check gate ±15%) ──
        # cent_50 weight 4.0 alone got out-traded by tail_shape; add a stiff
        # additive when outside ±15% so Optuna can't trade brightness for
        # tail length.
        dv_c50 = breakdown.get('cent50_dv')
        vv_c50 = breakdown.get('cent50_vvv')
        if dv_c50 and vv_c50 and vv_c50 > 0:
            d_pct = abs(dv_c50 - vv_c50) / vv_c50
            if d_pct > 0.15:
                loss += 6.0 * (d_pct - 0.15) ** 1.5

        # ── env_p2p hard cap (mirrors full_check gate ±5 dB) ──
        dv_env = breakdown.get('envP2P_dv')
        vv_env = breakdown.get('envP2P_vvv')
        if dv_env and vv_env:
            d = abs(dv_env - vv_env)
            if d > 5.0:
                loss += 1.0 * (d - 5.0)

        # ── Sustained-pink steady-state penalty ──
        # User perception lives in musical content (sustained input → reverb).
        # The noiseburst loss above matches impulse/burst response. Sustained
        # pink reveals the steady-state per-band energy + per-band decay times
        # that musical playback exposes. Diagnostic (2026-05-27) showed Lex VVP
        # had +3 dB sub-bass and 30%-slower sub-decay than DV on this stimulus
        # — the user's "more low end + darker" perception was real but invisible
        # to noiseburst-only loss.
        try:
            import soundfile as _sfs, numpy as _nps
            from scipy.signal import butter as _bts, sosfiltfilt as _ss
            # Anchor sustained path: derived from target_ir by substituting
            # the suffix. Falls back gracefully if anchor doesn't have it.
            anchor_sustained = target_ir.replace("_noiseburst", "_sustained")
            sustained_dv = impulse.parent / impulse.name.replace("_noiseburst", "_sustained")
            if sustained_dv.exists() and Path(anchor_sustained).exists():
                def _ss_band_rms_db(p, lo, hi, t0=2.5, t1=4.0):
                    x, sr = _sfs.read(p); m = x.mean(axis=1) if x.ndim>1 else x
                    a, b = int(t0*sr), min(int(t1*sr), len(m))
                    hi_c = min(hi, sr*0.49)
                    if lo <= 0:
                        sos = _bts(4, hi_c, 'low', fs=sr, output='sos')
                    else:
                        sos = _bts(4, [lo, hi_c], 'band', fs=sr, output='sos')
                    y = _ss(sos, m)
                    return float(20*_nps.log10(_nps.sqrt(_nps.mean(y[a:b]**2))+1e-30))
                ss_band_penalty = 0.0
                ss_band_max = 0.0
                # Finer sub + air resolution exposed user-perceived gaps that the
                # 20-100 Hz / 2-8k averaging hid (deep sub <50 Hz was -5 dB but
                # the 20-100 band averaged to -2 dB; air 10-20k was -6 dB but
                # absent from the previous spec). 2026-05-27.
                ss_specs = [(20,    50,  'deep_sub', 2.0),
                            (50,   100,  'sub',      2.0),
                            (100,  250,  'low',      2.0),
                            (250,  500,  'low_mid',  1.5),
                            (500, 2000,  'mid',      1.5),
                            (2000,5000,  'umid',     1.0),
                            (5000,10000, 'hi',       1.0),
                            (10000,20000,'air',      1.0)]
                for lo, hi, name, w in ss_specs:
                    d_dv = _ss_band_rms_db(str(sustained_dv), lo, hi)
                    d_lx = _ss_band_rms_db(anchor_sustained, lo, hi)
                    d = d_dv - d_lx
                    term = w * (d / 3.0) ** 2
                    ss_band_penalty += term
                    ss_band_max = max(ss_band_max, abs(d))
                    trial.set_user_attr(f'ss_band_{name}_db', float(d))
                # Hard cap on worst-band steady-state delta — matches
                # full_check gate ±2 dB.
                if ss_band_max > 2.0:
                    ss_band_penalty += 2.0 * (ss_band_max - 2.0)
                loss += ss_band_penalty
                trial.set_user_attr('ss_band_penalty', float(ss_band_penalty))

                # ── Per-band decay times (input-off at t=4.0s) ──
                # Both t10 (EDT — early decay, holds the perceived "weight" of
                # low bands that t30 averages away) AND t30 (mid-decay).
                # t10 is the GOLDEN-EAR metric the user identified — a band
                # that drops 10 dB fast loses its initial "thump" even if
                # t30/t60 match.
                def _band_decay_time(p, lo, hi, target_db):
                    x, sr = _sfs.read(p); m = x.mean(axis=1) if x.ndim>1 else x
                    off = int(4.0 * sr)
                    if off >= len(m): return None
                    tail = m[off:]
                    hi_c = min(hi, sr*0.49)
                    if lo <= 0:
                        sos = _bts(4, hi_c, 'low', fs=sr, output='sos')
                    else:
                        sos = _bts(4, [lo, hi_c], 'band', fs=sr, output='sos')
                    y = _ss(sos, tail)
                    pwr = y ** 2
                    win = max(int(0.01*sr), 1)
                    sm = _nps.convolve(pwr, _nps.ones(win)/win, mode='same')
                    peak = float(_nps.max(sm))
                    if peak < 1e-12: return None
                    pidx = int(_nps.argmax(sm))
                    floor = float(_nps.median(sm[-min(int(0.5*sr), len(sm)):]))
                    thr = max(peak * 10**(-target_db/10), floor * 4.0)
                    below = _nps.where((_nps.arange(len(sm)) > pidx) & (sm < thr))[0]
                    return (int(below[0]) - pidx) / sr if len(below) else None
                decay_penalty = 0.0
                decay_max = 0.0
                # Bands include low_mid 250-500 which was the user-perceived gap.
                # Each band contributes t10 (EDT, weight 2.0) + t30 (decay, weight 1.0).
                band_set = [(20,100,'sub'), (100,250,'low'),
                            (250,500,'low_mid'),  # NEW — captures the user gap
                            (500,2000,'mid'), (2000,8000,'hi')]
                for lo, hi, name in band_set:
                    # t10 — EDT, the "early hold" perception (2x weight)
                    dv_t10 = _band_decay_time(str(sustained_dv), lo, hi, 10)
                    lx_t10 = _band_decay_time(anchor_sustained,  lo, hi, 10)
                    if dv_t10 and lx_t10 and lx_t10 > 0.005:
                        d_pct = (dv_t10 - lx_t10) / lx_t10
                        decay_penalty += 2.0 * (d_pct / 0.20) ** 2
                        decay_max = max(decay_max, abs(d_pct))
                        trial.set_user_attr(f'edt_{name}_pct', float(d_pct*100))
                    # t30 — mid decay (1x weight)
                    dv_t30 = _band_decay_time(str(sustained_dv), lo, hi, 30)
                    lx_t30 = _band_decay_time(anchor_sustained,  lo, hi, 30)
                    if dv_t30 and lx_t30 and lx_t30 > 0.01:
                        d_pct = (dv_t30 - lx_t30) / lx_t30
                        decay_penalty += 1.0 * (d_pct / 0.20) ** 2
                        trial.set_user_attr(f'decay_{name}_pct', float(d_pct*100))
                loss += decay_penalty
                trial.set_user_attr('decay_penalty', float(decay_penalty))
                trial.set_user_attr('decay_max_pct', float(decay_max*100))

                # ── Envelope-shape L1 (snare-stimulus, first 500 ms post-peak) ──
                # Traces the contour of the attack-into-early-decay arc. Catches
                # "Lex holds flat, DV drops fast" mismatches that scalar t30
                # is blind to. Mean |Δ_dB| over the 5ms-smoothed Hilbert
                # envelope, gain-normalized to peak. Typical clean match < 2 dB;
                # mismatched contour > 5 dB. Weight 3.0 — this is the
                # perception-critical contour metric.
                try:
                    snare_dv_path = impulse.parent / impulse.name.replace("_noiseburst", "_snare")
                    anchor_snare_path = target_ir.replace("_noiseburst", "_snare")
                    if snare_dv_path.exists() and Path(anchor_snare_path).exists():
                        from metrics_external import envelope_shape_l1
                        x_dv, sr_dv = _sfs.read(str(snare_dv_path))
                        x_lx, _     = _sfs.read(anchor_snare_path)
                        m_dv = x_dv.mean(axis=1) if x_dv.ndim>1 else x_dv
                        m_lx = x_lx.mean(axis=1) if x_lx.ndim>1 else x_lx
                        env_l1 = envelope_shape_l1(m_dv, m_lx, sr_dv, post_peak_ms=500.0)
                        if env_l1 == env_l1:  # not NaN
                            # 3 dB envelope drift = unit penalty
                            env_term_v = 3.0 * (env_l1 / 3.0) ** 2
                            loss += env_term_v
                            trial.set_user_attr('env_shape_l1_dB', float(env_l1))
                            trial.set_user_attr('env_shape_term', float(env_term_v))
                except Exception as exc:
                    sys.stderr.write(f"envelope-shape failed: {exc}\n")
        except Exception as exc:
            sys.stderr.write(f"sustained-pink penalty failed: {exc}\n")

        # ── Snare-stimulus RMS match (perceptual loudness reference) ──
        # User listens on music (not noiseburst). Snare-stimulus loudness
        # is the perceptual loudness reference per memory:volume-match-first.
        # Optuna optimizes on noiseburst → snare RMS can drift. Render snare
        # too and penalize the gap.
        try:
            import soundfile as _sf2, numpy as _np2
            # Snare WAV is in same trial dir as noiseburst.
            snare_path = impulse.parent / impulse.name.replace("_noiseburst", "_snare")
            # Anchor snare: substitute "_noiseburst" → "_snare" in target_ir.
            anchor_snare = target_ir.replace("_noiseburst", "_snare")
            if snare_path.exists() and Path(anchor_snare).exists():
                def _rms(p):
                    x, sr = _sf2.read(p); m = x.mean(axis=1) if x.ndim>1 else x
                    pk = int(_np2.argmax(_np2.abs(m)))
                    return float(20*_np2.log10(_np2.sqrt(_np2.mean(m[pk:]**2))+1e-30))
                snare_dv = _rms(str(snare_path))
                snare_lx = _rms(anchor_snare)
                snare_d = snare_dv - snare_lx
                # 1.5 dB unit penalty (matches full_check gate).
                snare_term = (snare_d / 1.5) ** 2
                loss += 2.0 * snare_term
                trial.set_user_attr('snare_rms_dv', snare_dv)
                trial.set_user_attr('snare_rms_lex', snare_lx)
                trial.set_user_attr('snare_rms_d', snare_d)
        except Exception as exc:
            sys.stderr.write(f"snare penalty failed: {exc}\n")

        # ── ABSOLUTE band-energy match ──
        # spec_L1 is RMS-normalized — hides absolute level mismatches in
        # specific bands. User's listening test on the v8/v9 sweeps caught
        # the sub-bass surplus (DV +5 dB hot at <100 Hz) that the normalized
        # metric missed entirely. Compute filtered-band RMS on both DV and
        # anchor (peak-aligned, post-peak) and penalize per-band gap.
        # Calibrated so a 3 dB single-band gap contributes ~0.25 to total
        # loss (small but not invisible); a 6 dB gap contributes ~1.0 (visible);
        # 12 dB contributes ~4.0 (Optuna actively avoids).
        try:
            import soundfile as _sf, numpy as _np
            from scipy.signal import butter as _butter, sosfiltfilt as _sosfilt
            def _band_rms_db(p, lo, hi):
                x, sr = _sf.read(p); m = x.mean(axis=1) if x.ndim>1 else x
                pk = int(_np.argmax(_np.abs(m)))
                hi = min(hi, sr * 0.49)
                if lo <= 0:
                    sos = _butter(4, hi, 'low', fs=sr, output='sos')
                else:
                    sos = _butter(4, [lo, hi], 'band', fs=sr, output='sos')
                y = _sosfilt(sos, m)
                return float(20*_np.log10(_np.sqrt(_np.mean(y[pk:]**2))+1e-30))
            band_penalty = 0.0
            band_max = 0.0
            # Weights tuned 2026-05-27 after v10 sweep produced +4 dB mid surplus
            # with weak per-band terms. Normalized so 3 dB gap = 1.0 per band
            # (was 6 dB → unit). Every band weighted ≥1.0 so no band can drift
            # silently. Sub/low slightly higher since user listening identified
            # these as the most perceptually critical for plate character.
            band_specs = [(20, 100, 2.0, 'sub'), (100, 250, 2.0, 'low'),
                          (250, 1000, 1.5, 'mid'), (1000, 4000, 1.5, 'umid'),
                          (4000, 12000, 1.0, 'hi')]
            for lo, hi, w, name in band_specs:
                d_dv = _band_rms_db(str(impulse), lo, hi)
                d_lx = _band_rms_db(target_ir, lo, hi)
                d = d_dv - d_lx
                # Squared dB delta normalized so 3 dB gap = 1.0 per band.
                term = w * (d / 3.0) ** 2
                band_penalty += term
                band_max = max(band_max, abs(d))
                trial.set_user_attr(f'band_{name}_db', float(d))
            # Hard ceiling: any band >5 dB hot adds an extra penalty so the
            # optimizer cannot trade away a clean band for a +5 dB other-band hump.
            if band_max > 5.0:
                band_penalty += 2.0 * (band_max - 5.0)
            loss += band_penalty
            trial.set_user_attr('band_penalty', float(band_penalty))
            trial.set_user_attr('band_max_dB', float(band_max))
        except Exception as exc:
            sys.stderr.write(f"band penalty failed: {exc}\n")

        # ── Tail oscillation match ──
        # Detect modulator-induced envelope ripple. Optuna previously chose
        # mod_depth values that produced audible 10 Hz tail oscillation not
        # present in the reference. Compare detrended envelope P2P between
        # DV and anchor; penalize deviation.
        try:
            import soundfile as _sf, numpy as _np
            from scipy.signal import hilbert as _hilbert
            def _osc_p2p(p):
                x, sr = _sf.read(p); m = x.mean(axis=1) if x.ndim>1 else x
                env = _np.abs(_hilbert(m))
                win = max(int(0.005*sr), 1)
                env_sm = _np.convolve(env, _np.ones(win)/win, mode='same')
                env_db = 20*_np.log10(env_sm + 1e-30)
                pidx = int(_np.argmax(env_db))
                ts = _np.arange(0.05, 1.5, 0.005)
                arr = _np.array([env_db[pidx+int(t*sr)] for t in ts if pidx+int(t*sr) < len(env_db)])
                if len(arr) < 30: return None
                tt = _np.arange(len(arr))*0.005
                A = _np.vstack([tt, _np.ones_like(tt)]).T
                sl, ic = _np.linalg.lstsq(A, arr, rcond=None)[0]
                res = arr - (sl*tt + ic)
                return float(res.max() - res.min())
            o_dv = _osc_p2p(str(impulse))
            o_lx = _osc_p2p(target_ir)
            if o_dv is not None and o_lx is not None:
                d = o_dv - o_lx
                # 4 dB ripple gap = unit penalty; weight 0.5.
                osc_term = 0.5 * (d / 4.0) ** 2
                loss += osc_term
                trial.set_user_attr('osc_dv_dB', float(o_dv))
                trial.set_user_attr('osc_lex_dB', float(o_lx))
                trial.set_user_attr('osc_term', float(osc_term))
        except Exception as exc:
            sys.stderr.write(f"osc penalty failed: {exc}\n")

        # Tag the trial with breakdown for inspection.
        for k, v in breakdown.items():
            if isinstance(v, (int, float)):
                trial.set_user_attr(k, float(v))

        # Keep the impulse WAV for the current best trial so callers can
        # audit the audio without re-rendering. For non-best trials drop
        # the whole per-trial dir to bound scratch usage. Optuna's
        # best_value raises ValueError until at least one trial completes;
        # treat that case as "this is the first complete, keep it".
        try:
            study_best = trial.study.best_value
            is_best = loss < study_best
        except ValueError:
            is_best = True
        if is_best:
            trial.set_user_attr('impulse_path', str(impulse))
        else:
            shutil.rmtree(out_dir, ignore_errors=True)

        return loss

    return objective


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target-ir", required=True,
                    help="Reference IR WAV (e.g. VVV vpreset render).")
    ap.add_argument("--dv-preset", required=True,
                    help="DuskVerb preset name as known to the render harness.")
    ap.add_argument("--trials", type=int, default=1500,
                    help="Optuna trial budget (default 1500).")
    ap.add_argument("--workers", type=int, default=4,
                    help="Parallel worker count via Optuna n_jobs (default 4).")
    ap.add_argument("--vst3", default=str(DEFAULT_VST3),
                    help=f"DuskVerb VST3 path (default {DEFAULT_VST3}).")
    ap.add_argument("--study-name", default=None,
                    help="Study name for sqlite persistence.")
    ap.add_argument("--storage", default=None,
                    help="Optuna storage URL (sqlite:///path.db). Default: in-memory.")
    ap.add_argument("--seed", type=int, default=42,
                    help="TPE sampler seed (default 42).")
    ap.add_argument("--stimulus", default="noiseburst",
                    choices=["impulse", "noiseburst", "snare"],
                    help="Which rendered stimulus to compare (default noiseburst).")
    ap.add_argument("--prerun-seconds", type=float, default=5.0,
                    help="Warm-up silence before each stimulus (default 5.0).")
    args = ap.parse_args()

    target_ir = Path(args.target_ir)
    if not target_ir.is_file():
        sys.exit(f"target IR not found: {target_ir}")
    vst3 = Path(args.vst3)
    if not vst3.exists():
        sys.exit(f"VST3 not found: {vst3}")
    if not RENDER_BIN.exists():
        sys.exit(f"render binary not found: {RENDER_BIN}  (build duskverb_render first)")

    # Compute target metrics once so we can print them up front.
    print(f"Loading target IR: {target_ir}")
    target = compute_metrics(str(target_ir))
    print(f"  Target RT60       = {target['rt60']:.3f} s")
    print(f"  Target Cent  50ms = {target['cent_50']:.0f} Hz")
    print(f"  Target Cent 500ms = {target['cent_500']:.0f} Hz")
    print(f"  Target Stereo r   = {target['stereo_corr']:+.3f}")
    print(f"  Target Env P2P    = {target['env_res_p2p']:.2f} dB")
    print()

    trial_root = Path(tempfile.mkdtemp(prefix="dv_optuna_"))
    print(f"Trial scratch dir: {trial_root}")

    sampler = optuna.samplers.TPESampler(seed=args.seed)
    study_kwargs = {"direction": "minimize", "sampler": sampler}
    if args.study_name:
        study_kwargs["study_name"] = args.study_name
    if args.storage:
        study_kwargs["storage"] = args.storage
        study_kwargs["load_if_exists"] = True
    study = optuna.create_study(**study_kwargs)

    objective = make_objective(
        target_ir=target_ir,
        preset_name=args.dv_preset,
        vst3_path=vst3,
        trial_root=trial_root,
        stimulus=args.stimulus,
        prerun_seconds=args.prerun_seconds,
    )

    print(f"Starting {args.trials} trials with {args.workers} parallel workers...")
    t0 = time.time()
    study.optimize(
        objective,
        n_trials=args.trials,
        n_jobs=args.workers,
        show_progress_bar=False,
    )
    elapsed = time.time() - t0
    print(f"\nFinished in {elapsed/60:.1f} min ({args.trials} trials, {args.workers} workers)")

    # Report
    best = study.best_trial
    print()
    print("=" * 70)
    print(f"BEST TRIAL #{best.number}   loss = {best.value:.6f}")
    print("=" * 70)
    print("\nBest params:")
    for k, v in sorted(best.params.items()):
        print(f"  {k:18s} = {v:.4f}")

    ua = best.user_attrs
    if ua:
        print("\nBest metrics vs target:")
        print(f"  RT60         DV={ua.get('rt60_dv', 0):.3f}s   VVV={ua.get('rt60_vvv', 0):.3f}s    Δ={((ua.get('rt60_dv', 0) - ua.get('rt60_vvv', 0)) / max(ua.get('rt60_vvv', 1), 1e-6) * 100):+.1f}%")
        print(f"  Cent 50ms    DV={ua.get('cent50_dv', 0):.0f}Hz  VVV={ua.get('cent50_vvv', 0):.0f}Hz   Δ={((ua.get('cent50_dv', 0) - ua.get('cent50_vvv', 0)) / max(ua.get('cent50_vvv', 1), 1e-6) * 100):+.1f}%")
        print(f"  Cent 500ms   DV={ua.get('cent500_dv', 0):.0f}Hz  VVV={ua.get('cent500_vvv', 0):.0f}Hz   Δ={((ua.get('cent500_dv', 0) - ua.get('cent500_vvv', 0)) / max(ua.get('cent500_vvv', 1), 1e-6) * 100):+.1f}%")
        print(f"  Stereo r     DV={ua.get('stereo_dv', 0):+.3f}    VVV={ua.get('stereo_vvv', 0):+.3f}    Δ={(ua.get('stereo_dv', 0) - ua.get('stereo_vvv', 0)):+.3f}")
        print(f"  Env P2P      DV={ua.get('envP2P_dv', 0):.2f}dB  VVV={ua.get('envP2P_vvv', 0):.2f}dB  Δ={(ua.get('envP2P_dv', 0) - ua.get('envP2P_vvv', 0)):+.2f}dB")
        print(f"  Spec L1      = {ua.get('spec_l1_db', 0):.3f} dB")

    # Dump the best params as JSON for downstream tooling.
    best_json = trial_root / "best.json"
    payload = {
        "preset": args.dv_preset,
        "target_ir": str(target_ir),
        "best_loss": best.value,
        "best_params": best.params,
        "best_metrics": {k: v for k, v in best.user_attrs.items()},
        "n_trials": args.trials,
        "elapsed_sec": elapsed,
    }
    best_json.write_text(json.dumps(payload, indent=2))
    print(f"\nBest trial JSON: {best_json}")

    # Done — leave trial_root in /tmp so caller can re-render the winner.
    print(f"Scratch dir kept at: {trial_root}")


if __name__ == "__main__":
    main()
