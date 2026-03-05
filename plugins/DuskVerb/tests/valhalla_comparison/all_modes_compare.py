#!/usr/bin/env python3
"""
Comprehensive DuskVerb vs VintageVerb comparison across ALL modes.

Tests each DuskVerb algorithm against its closest VintageVerb equivalent
with 2-3 presets per mode, reports key metrics, and prints a summary.

PARAMETER MATCHING RULES:
  - size: same 0-1 value on both plugins
  - diffusion: DV diffusion = VV earlydiffusion = VV latediffusion
  - mod_depth: DV mod_depth = VV moddepth
  - mod_rate: DV mod_rate (Hz, skew 0.12) ≠ VV modrate (0-1 normalized).
              Both set to "moderate" for their respective scales.
  - pre_delay: DV in ms, VV normalized 0-1 (~0-300ms range). Converted.
  - treble/bass damping: BOTH NEUTRAL (DV treble_multiply=1.0, bass_multiply=1.0;
              VV highshelf=0.0, bassmult=0.50). Algorithm-internal scaling
              (trebleMultScale, bassMultScale) is tested implicitly.
  - EQ: both fully open (DV lo_cut=20, hi_cut=20000; VV lowcut=0, highcut=1.0)
  - RT60: calibrated via binary search to match DV RT60 at 500Hz

VintageVerb mode mapping (v4.0.5 — 22 modes, step 1/24):
  Concert Hall (0.0417) → DuskVerb Hall
  Plate        (0.0833) → DuskVerb Plate
  Room         (0.1250) → DuskVerb Room (long-sustaining, 15-100s+ RT60)
  Chamber      (0.1667) → DuskVerb Chamber
  Ambience     (0.2917) → DuskVerb Ambient
  Hall1984     (0.9167) → DuskVerb Hall (alt)
"""

import numpy as np
from pedalboard import load_plugin

from config import (
    SAMPLE_RATE, DUSKVERB_PATHS, VINTAGEVERB_PATHS,
    find_plugin, apply_duskverb_params, apply_valhalla_params,
    VALHALLA_PARAM_MAP,
)
from generate_test_signals import make_impulse, make_tone_burst
import reverb_metrics as metrics


# ---------------------------------------------------------------------------
# VintageVerb neutral base (shared across all presets)
# All damping/EQ parameters set to NEUTRAL for fair comparison.
# ---------------------------------------------------------------------------
VV_NEUTRAL = {
    "_colormode": 0.333,        # 1980s (neutral era)
    "_high_cut": 1.0,           # Fully open (no HF cut)
    "_low_cut": 0.0,            # Fully open (no LF cut)
    "_bassmult": 0.50,          # Neutral (1x bass sustain)
    "_bassxover": 0.40,         # Default crossover
    "_highshelf": 0.0,          # No HF reduction
    "_highfreq": 0.50,          # Default
    "_attack": 0.50,            # Neutral transient shape
}


def vv_preset(mode, size, diffusion, mod_depth, mod_rate_norm, predelay_ms,
              decay_hint=0.15, **overrides):
    """Build a VV preset with matched parameters.

    Args:
        mode: VV reverbmode (0-1)
        size: 0-1, same value as DV
        diffusion: 0-1, matched to DV diffusion
        mod_depth: 0-1, matched to DV mod_depth
        mod_rate_norm: 0-1 normalized VV mod rate
        predelay_ms: pre-delay in ms, converted to VV 0-1 (~300ms max)
        decay_hint: initial decay guess (calibrated later)
    """
    p = dict(VV_NEUTRAL)
    p["_reverbmode"] = mode
    p["_size"] = size
    p["_diffusion_early"] = diffusion
    p["_diffusion_late"] = diffusion
    p["_mod_depth"] = mod_depth
    p["_mod_rate"] = mod_rate_norm
    p["_predelay"] = min(predelay_ms / 300.0, 1.0)  # VV max ~300ms
    p["_decay"] = decay_hint
    p.update(overrides)
    return p


def dv_preset(algorithm, decay_time, size, diffusion, mod_depth, mod_rate_hz,
              predelay_ms, er_level=0.0, er_size=0.0, width=1.0):
    """Build a DV preset with neutral damping/EQ."""
    return {
        "algorithm": algorithm,
        "decay_time": decay_time,
        "size": size,
        "diffusion": diffusion,
        "mod_depth": mod_depth,
        "mod_rate": mod_rate_hz,
        "treble_multiply": 1.0,    # NEUTRAL (let AlgorithmConfig scale handle it)
        "bass_multiply": 1.0,      # NEUTRAL
        "crossover": 1000,
        "early_ref_level": er_level,
        "early_ref_size": er_size,
        "pre_delay": predelay_ms,
        "lo_cut": 20,              # Fully open
        "hi_cut": 20000,           # Fully open
        "width": width,
    }


# ---------------------------------------------------------------------------
# Mode test definitions — MATCHED parameters
#
# For each preset pair:
#   size, diffusion, mod_depth use IDENTICAL values
#   treble/bass damping = neutral on both
#   EQ = fully open on both
#   RT60 = calibrated at 500Hz
# ---------------------------------------------------------------------------
MODES = [
    # -----------------------------------------------------------------------
    # HALL → VintageVerb Concert Hall (0.0417)
    # -----------------------------------------------------------------------
    {
        "dv_mode": "Hall",
        "presets": [
            {
                "name": "Hall-Short",
                "duskverb": dv_preset("Hall", 1.5, 0.50, 0.70, 0.30, 0.5,
                                      20.0, er_level=0.50, er_size=0.40),
                "valhalla": vv_preset(0.0417, 0.50, 0.70, 0.30, 0.25, 20.0,
                                      decay_hint=0.10),
            },
            {
                "name": "Hall-Medium",
                "duskverb": dv_preset("Hall", 2.5, 0.70, 0.75, 0.35, 0.8,
                                      30.0, er_level=0.55, er_size=0.50),
                "valhalla": vv_preset(0.0417, 0.70, 0.75, 0.35, 0.30, 30.0,
                                      decay_hint=0.18),
            },
            {
                "name": "Hall-Long",
                "duskverb": dv_preset("Hall", 5.0, 0.85, 0.80, 0.40, 0.9,
                                      40.0, er_level=0.60, er_size=0.60,
                                      width=1.2),
                "valhalla": vv_preset(0.0417, 0.85, 0.80, 0.40, 0.35, 40.0,
                                      decay_hint=0.30),
            },
        ],
    },

    # -----------------------------------------------------------------------
    # PLATE → VintageVerb Plate (0.0833)
    # -----------------------------------------------------------------------
    {
        "dv_mode": "Plate",
        "presets": [
            {
                "name": "Plate-Short",
                "duskverb": dv_preset("Plate", 1.0, 0.45, 0.80, 0.20, 0.5,
                                      0.0),
                "valhalla": vv_preset(0.0833, 0.45, 0.80, 0.20, 0.15, 0.0,
                                      decay_hint=0.08),
            },
            {
                "name": "Plate-Medium",
                "duskverb": dv_preset("Plate", 2.0, 0.65, 0.85, 0.25, 0.6,
                                      0.0),
                "valhalla": vv_preset(0.0833, 0.65, 0.85, 0.25, 0.20, 0.0,
                                      decay_hint=0.15),
            },
            {
                "name": "Plate-Long",
                "duskverb": dv_preset("Plate", 4.0, 0.80, 0.90, 0.30, 0.7,
                                      0.0),
                "valhalla": vv_preset(0.0833, 0.80, 0.90, 0.30, 0.25, 0.0,
                                      decay_hint=0.25),
            },
        ],
    },

    # -----------------------------------------------------------------------
    # ROOM → VintageVerb Room (0.1250)
    # VV Room is a long-sustaining mode (~55s RT60 regardless of _decay).
    # _decay mainly affects level, not tail length. RT60 calibration is
    # skipped — tails are inherently matched at ~55s.
    # signal_duration=40s needed for Schroeder regression on 55s tails.
    # -----------------------------------------------------------------------
    {
        "dv_mode": "Room",
        "signal_duration": 40.0,
        "skip_calibration": True,
        "presets": [
            {
                "name": "Room-Short",
                "duskverb": dv_preset("Room", 18.0, 0.50, 0.70, 0.30, 0.5,
                                      10.0, er_level=0.40, er_size=0.40),
                "valhalla": vv_preset(0.1250, 0.50, 0.70, 0.30, 0.25, 10.0,
                                      decay_hint=0.50),
            },
            {
                "name": "Room-Medium",
                "duskverb": dv_preset("Room", 20.0, 0.70, 0.75, 0.35, 0.5,
                                      20.0, er_level=0.30, er_size=0.50),
                "valhalla": vv_preset(0.1250, 0.70, 0.75, 0.35, 0.30, 20.0,
                                      decay_hint=0.60),
            },
            {
                "name": "Room-Long",
                "duskverb": dv_preset("Room", 25.0, 0.85, 0.80, 0.40, 0.5,
                                      30.0, er_level=0.20, er_size=0.60,
                                      width=1.2),
                "valhalla": vv_preset(0.1250, 0.85, 0.80, 0.40, 0.35, 30.0,
                                      decay_hint=0.70),
            },
        ],
    },

    # -----------------------------------------------------------------------
    # CHAMBER → VintageVerb Chamber (0.1667)
    # -----------------------------------------------------------------------
    {
        "dv_mode": "Chamber",
        "presets": [
            {
                "name": "Chamber-Short",
                "duskverb": dv_preset("Chamber", 1.0, 0.45, 0.70, 0.25, 0.6,
                                      10.0, er_level=0.55, er_size=0.40),
                "valhalla": vv_preset(0.1667, 0.45, 0.70, 0.25, 0.22, 10.0,
                                      decay_hint=0.08),
            },
            {
                "name": "Chamber-Medium",
                "duskverb": dv_preset("Chamber", 1.8, 0.60, 0.75, 0.30, 0.7,
                                      20.0, er_level=0.60, er_size=0.50),
                "valhalla": vv_preset(0.1667, 0.60, 0.75, 0.30, 0.25, 20.0,
                                      decay_hint=0.14),
            },
        ],
    },

    # -----------------------------------------------------------------------
    # AMBIENT → VintageVerb Ambience (0.2917)
    # -----------------------------------------------------------------------
    {
        "dv_mode": "Ambient",
        "presets": [
            {
                "name": "Ambient-Long",
                "duskverb": dv_preset("Ambient", 8.0, 0.80, 0.90, 0.45, 0.8,
                                      30.0, width=1.4),
                "valhalla": vv_preset(0.2917, 0.80, 0.90, 0.45, 0.35, 30.0,
                                      decay_hint=0.60),
            },
            {
                "name": "Ambient-Medium",
                "duskverb": dv_preset("Ambient", 6.0, 0.85, 0.95, 0.50, 0.9,
                                      40.0, width=1.6),
                "valhalla": vv_preset(0.2917, 0.85, 0.95, 0.50, 0.40, 40.0,
                                      decay_hint=0.35),
            },
            {
                "name": "Ambient-Subtle",
                "duskverb": dv_preset("Ambient", 4.0, 0.65, 0.85, 0.35, 0.7,
                                      20.0, width=1.2),
                "valhalla": vv_preset(0.2917, 0.65, 0.85, 0.35, 0.30, 20.0,
                                      decay_hint=0.25),
            },
        ],
    },
]


def process_stereo(plugin, mono_signal, sr):
    stereo_in = np.stack([mono_signal, mono_signal], axis=0).astype(np.float32)
    output = plugin(stereo_in, sr)
    return output[0], output[1]


def flush_plugin(plugin, sr, duration_sec=2.0):
    silence = np.zeros(int(sr * duration_sec), dtype=np.float32)
    process_stereo(plugin, silence, sr)


def calibrate_decay_fast(plugin, target_rt60, vv_config, sr,
                         signal_duration=None):
    """Quick 8-iteration binary search to match RT60."""
    impulse = make_impulse(signal_duration)
    flush_dur = max(2.0, (signal_duration or 12.0) * 0.25)
    lo, hi = 0.0, 1.0
    best_decay, best_rt60, best_error = None, None, float('inf')

    for _ in range(8):
        mid = (lo + hi) / 2.0
        trial = dict(vv_config)
        trial["_decay"] = mid
        apply_valhalla_params(plugin, trial)
        flush_plugin(plugin, sr, flush_dur)
        out_l, _ = process_stereo(plugin, impulse, sr)
        rt60 = metrics.measure_rt60_per_band(out_l, sr, {"500 Hz": 500}).get("500 Hz")

        if rt60 is None or rt60 <= 0:
            hi = mid
            continue

        error = abs(rt60 / target_rt60 - 1.0)
        if error < best_error:
            best_error = error
            best_decay = mid
            best_rt60 = rt60

        if error < 0.10:
            return mid, rt60

        if rt60 > target_rt60:
            hi = mid
        else:
            lo = mid

    return best_decay, best_rt60


def main():
    sr = SAMPLE_RATE

    dv_path = find_plugin(DUSKVERB_PATHS)
    vv_path = find_plugin(VINTAGEVERB_PATHS)
    if not dv_path or not vv_path:
        print("ERROR: Need both DuskVerb and VintageVerb.")
        return

    print(f"Loading DuskVerb: {dv_path}")
    duskverb = load_plugin(dv_path)
    print(f"Loading VintageVerb: {vv_path}")
    vintageverb = load_plugin(vv_path)

    all_results = []

    print("\nParameter matching: size, diffusion, mod_depth IDENTICAL on both plugins.")
    print("Damping: NEUTRAL on both (DV treble_multiply=1.0, VV highshelf=0.0).")
    print("EQ: fully open on both. RT60: calibrated at 500Hz.\n")

    for mode_def in MODES:
        dv_mode = mode_def["dv_mode"]
        sig_dur = mode_def.get("signal_duration", None)  # None = default
        flush_dur = max(2.0, (sig_dur or 12.0) * 0.5)
        impulse = make_impulse(sig_dur)
        tone = make_tone_burst(1500)

        if sig_dur:
            print(f"{'='*70}")
            print(f"  DuskVerb: {dv_mode}  (signal_duration={sig_dur}s, flush={flush_dur:.0f}s)")
            print(f"{'='*70}")
        else:
            print(f"{'='*70}")
            print(f"  DuskVerb: {dv_mode}")
            print(f"{'='*70}")

        for preset in mode_def["presets"]:
            name = preset["name"]
            dv_cfg = preset["duskverb"]
            vv_cfg = preset["valhalla"]

            # Print matched params for verification
            print(f"\n  --- {name} ---")
            print(f"    Matched: size={dv_cfg['size']:.2f}  "
                  f"diff={dv_cfg['diffusion']:.2f}  "
                  f"mod_d={dv_cfg['mod_depth']:.2f}  "
                  f"predelay={dv_cfg['pre_delay']:.0f}ms")

            # Configure DuskVerb
            apply_duskverb_params(duskverb, dv_cfg)
            flush_plugin(duskverb, sr, flush_dur)

            # Process DV impulse and measure RT60
            dv_imp_l, dv_imp_r = process_stereo(duskverb, impulse, sr)
            flush_plugin(duskverb, sr, flush_dur)
            dv_rt60 = metrics.measure_rt60_per_band(dv_imp_l, sr)
            dv_rt60_500 = dv_rt60.get("500 Hz", 0)

            # Calibrate VV decay to match DV RT60
            skip_cal = mode_def.get("skip_calibration", False)
            if skip_cal:
                rt60_str = f"{dv_rt60_500:.1f}s" if dv_rt60_500 else ">signal_duration"
                print(f"    DV RT60@500={rt60_str}  "
                      f"(calibration skipped — VV Room RT60 is fixed ~55s)")
            elif dv_rt60_500 and dv_rt60_500 > 0:
                cal_decay, cal_rt60 = calibrate_decay_fast(
                    vintageverb, dv_rt60_500, vv_cfg, sr,
                    signal_duration=sig_dur)
                if cal_decay is not None:
                    vv_cfg["_decay"] = cal_decay
                    print(f"    VV calibrated: _decay={cal_decay:.4f} → "
                          f"RT60={cal_rt60:.2f}s (target={dv_rt60_500:.2f}s)")

            # Configure VV and process
            apply_valhalla_params(vintageverb, vv_cfg)
            flush_plugin(vintageverb, sr, flush_dur)
            vv_imp_l, vv_imp_r = process_stereo(vintageverb, impulse, sr)
            flush_plugin(vintageverb, sr, flush_dur)

            # Time-align impulse responses
            dv_imp_l, vv_imp_l, offset = metrics.align_ir_pair(dv_imp_l, vv_imp_l, sr)
            if offset != 0:
                print(f"    Alignment: {offset} samples ({offset / sr * 1000:.1f} ms)")
                # Also align right channels
                if offset > 0:
                    vv_imp_r = vv_imp_r[offset:offset + len(dv_imp_l)]
                    dv_imp_r = dv_imp_r[:len(dv_imp_l)]
                else:
                    dv_imp_r = dv_imp_r[-offset:-offset + len(vv_imp_l)]
                    vv_imp_r = vv_imp_r[:len(vv_imp_l)]

            # Tone burst
            dv_tone_l, _ = process_stereo(duskverb, tone, sr)
            flush_plugin(duskverb, sr, flush_dur)
            vv_tone_l, _ = process_stereo(vintageverb, tone, sr)
            flush_plugin(vintageverb, sr, flush_dur)

            # Ringing
            dv_imp_ring = metrics.detect_modal_resonances(dv_imp_l, sr)
            vv_imp_ring = metrics.detect_modal_resonances(vv_imp_l, sr)
            dv_tone_ring = metrics.detect_modal_resonances(
                dv_tone_l, sr, time_windows=[(0.05, 0.15), (0.15, 0.4), (0.4, 1.0)])
            vv_tone_ring = metrics.detect_modal_resonances(
                vv_tone_l, sr, time_windows=[(0.05, 0.15), (0.15, 0.4), (0.4, 1.0)])

            # RT60 per band
            vv_rt60 = metrics.measure_rt60_per_band(vv_imp_l, sr)

            # Level (RMS of first 0.5s)
            dv_rms = float(np.sqrt(np.mean(dv_imp_l[:int(sr * 0.5)] ** 2)))
            vv_rms = float(np.sqrt(np.mean(vv_imp_l[:int(sr * 0.5)] ** 2)))
            level_diff = 20 * np.log10(max(dv_rms, 1e-10) / max(vv_rms, 1e-10))

            # Stereo decorrelation
            tail_len = min(int(sr * 2), len(dv_imp_l), len(dv_imp_r))
            dv_corr = float(np.corrcoef(dv_imp_l[:tail_len], dv_imp_r[:tail_len])[0, 1])

            # IACC (ISO 3382-1)
            _, dv_iacc_vals = metrics.measure_iacc(dv_imp_l, dv_imp_r, sr)
            _, vv_iacc_vals = metrics.measure_iacc(vv_imp_l, vv_imp_r, sr)
            dv_iacc_avg = float(np.mean(dv_iacc_vals[len(dv_iacc_vals)//4:])) if len(dv_iacc_vals) > 0 else 0
            vv_iacc_avg = float(np.mean(vv_iacc_vals[len(vv_iacc_vals)//4:])) if len(vv_iacc_vals) > 0 else 0

            # Crest factor
            _, dv_cf_vals = metrics.crest_factor_over_time(dv_imp_l, sr)
            _, vv_cf_vals = metrics.crest_factor_over_time(vv_imp_l, sr)
            dv_cf_avg = float(np.mean(dv_cf_vals[len(dv_cf_vals)//4:])) if len(dv_cf_vals) > 0 else 0
            vv_cf_avg = float(np.mean(vv_cf_vals[len(vv_cf_vals)//4:])) if len(vv_cf_vals) > 0 else 0

            # Spectral MSE (level-normalized)
            dv_rms_val = np.sqrt(np.mean(dv_imp_l.astype(np.float64) ** 2))
            vv_rms_val = np.sqrt(np.mean(vv_imp_l.astype(np.float64) ** 2))
            if dv_rms_val > 1e-10 and vv_rms_val > 1e-10:
                norm_dv = (dv_imp_l * (1.0 / dv_rms_val)).astype(np.float32)
                norm_vv = (vv_imp_l * (1.0 / vv_rms_val)).astype(np.float32)
                mse_result = metrics.spectral_mse(norm_dv, norm_vv, sr)
                avg_mse = float(np.mean(list(mse_result.values()))) if mse_result else 0
            else:
                avg_mse = 0

            # RT60 ratios at key bands
            rt60_ratios = {}
            for band in ["500 Hz", "2 kHz", "4 kHz"]:
                dv_val = dv_rt60.get(band)
                vv_val = vv_rt60.get(band)
                if dv_val and vv_val and vv_val > 0:
                    rt60_ratios[band] = dv_val / vv_val

            row = {
                "mode": dv_mode,
                "name": name,
                "dv_imp_ring": dv_imp_ring["max_peak_prominence_db"],
                "dv_imp_freq": dv_imp_ring["worst_freq_hz"],
                "vv_imp_ring": vv_imp_ring["max_peak_prominence_db"],
                "vv_imp_freq": vv_imp_ring["worst_freq_hz"],
                "dv_tone_ring": dv_tone_ring["max_peak_prominence_db"],
                "vv_tone_ring": vv_tone_ring["max_peak_prominence_db"],
                "level_diff": level_diff,
                "dv_corr": dv_corr,
                "rt60_ratios": rt60_ratios,
                "dv_iacc": dv_iacc_avg,
                "vv_iacc": vv_iacc_avg,
                "dv_crest": dv_cf_avg,
                "vv_crest": vv_cf_avg,
                "spectral_mse": avg_mse,
            }
            all_results.append(row)

            # Per-preset detail
            r500 = rt60_ratios.get("500 Hz", 0)
            r2k = rt60_ratios.get("2 kHz", 0)
            r4k = rt60_ratios.get("4 kHz", 0)
            print(f"    Imp ring:  DV {row['dv_imp_ring']:5.1f}dB @ {row['dv_imp_freq']:5.0f}Hz | "
                  f"VV {row['vv_imp_ring']:5.1f}dB @ {row['vv_imp_freq']:5.0f}Hz")
            print(f"    Tone ring: DV {row['dv_tone_ring']:5.1f}dB | VV {row['vv_tone_ring']:5.1f}dB")
            print(f"    Level:     {row['level_diff']:+.1f} dB  |  "
                  f"RT60: 500={r500:.2f}x  2k={r2k:.2f}x  4k={r4k:.2f}x")
            print(f"    Stereo:    DV corr={row['dv_corr']:.3f}  |  "
                  f"IACC: DV={row['dv_iacc']:.3f} VV={row['vv_iacc']:.3f}")
            print(f"    Crest:     DV={row['dv_crest']:.2f} VV={row['vv_crest']:.2f}  |  "
                  f"Spectral MSE: {row['spectral_mse']:.1f} dB²")

    # -----------------------------------------------------------------------
    # Summary table
    # -----------------------------------------------------------------------
    print(f"\n{'='*100}")
    print(f"  SUMMARY: DuskVerb vs VintageVerb (All Modes, Matched Parameters)")
    print(f"{'='*100}")
    header = (f"  {'Mode':<8s} {'Preset':<22s} "
              f"{'DV Imp':>7s} {'VV Imp':>7s} "
              f"{'DV Tone':>8s} {'VV Tone':>8s} "
              f"{'Level':>7s} "
              f"{'RT@500':>7s} {'RT@2k':>6s} {'RT@4k':>6s} "
              f"{'MSE':>5s} {'IACC':>5s} {'Crest':>6s}")
    print(header)
    print("  " + "-" * 112)

    for r in all_results:
        r500 = r["rt60_ratios"].get("500 Hz", 0)
        r2k = r["rt60_ratios"].get("2 kHz", 0)
        r4k = r["rt60_ratios"].get("4 kHz", 0)
        iacc_delta = r["dv_iacc"] - r["vv_iacc"]
        crest_delta = r["dv_crest"] - r["vv_crest"]
        line = (f"  {r['mode']:<8s} {r['name']:<22s} "
                f"{r['dv_imp_ring']:>6.1f}dB {r['vv_imp_ring']:>6.1f}dB "
                f"{r['dv_tone_ring']:>7.1f}dB {r['vv_tone_ring']:>7.1f}dB "
                f"{r['level_diff']:>+6.1f}dB "
                f"{r500:>6.2f}x {r2k:>5.2f}x {r4k:>5.2f}x "
                f"{r['spectral_mse']:>4.0f} {iacc_delta:>+.2f} {crest_delta:>+5.2f}")
        print(line)

    # Per-mode averages
    print("  " + "-" * 96)
    for mode_def in MODES:
        mode = mode_def["dv_mode"]
        mode_results = [r for r in all_results if r["mode"] == mode]
        avg_dv_imp = np.mean([r["dv_imp_ring"] for r in mode_results])
        avg_vv_imp = np.mean([r["vv_imp_ring"] for r in mode_results])
        avg_dv_tone = np.mean([r["dv_tone_ring"] for r in mode_results])
        avg_vv_tone = np.mean([r["vv_tone_ring"] for r in mode_results])
        avg_level = np.mean([r["level_diff"] for r in mode_results])
        print(f"  {mode:<8s} {'(average)':<22s} "
              f"{avg_dv_imp:>6.1f}dB {avg_vv_imp:>6.1f}dB "
              f"{avg_dv_tone:>7.1f}dB {avg_vv_tone:>7.1f}dB "
              f"{avg_level:>+6.1f}dB")

    # Overall
    avg_all_dv = np.mean([r["dv_imp_ring"] for r in all_results])
    avg_all_vv = np.mean([r["vv_imp_ring"] for r in all_results])
    print(f"\n  Overall avg impulse ringing: DV {avg_all_dv:.1f}dB  VV {avg_all_vv:.1f}dB  "
          f"(gap: {avg_all_dv - avg_all_vv:+.1f}dB)")

    print(f"\n  Target: Impulse ringing < 12 dB, Level within ±5 dB, RT60 ratios 0.7-1.4x")
    print("\nDone.")


if __name__ == "__main__":
    main()
