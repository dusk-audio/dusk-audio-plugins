#!/usr/bin/env python3
"""Measure absolute wet gain (output vs input) for each DuskVerb mode."""

import numpy as np
import pedalboard
from config import (SAMPLE_RATE, DUSKVERB_PATHS, REFERENCE_REVERB_PATHS,
                     find_plugin, apply_duskverb_params, REFERENCE_PARAM_MAP)

SR = SAMPLE_RATE

def load_plugin(path):
    try:
        return pedalboard.load_plugin(path)
    except Exception as e:
        print(f"ERROR loading {path}: {e}")
        return None

def apply_vv(plugin, params):
    for key, value in params.items():
        name = REFERENCE_PARAM_MAP.get(key, key.lstrip("_"))
        try:
            setattr(plugin, name, value)
        except Exception:
            pass

def flush(plugin, dur=5.0):
    silence = np.zeros((2, int(SR * dur)), dtype=np.float32)
    plugin(silence, SR)

def measure_wet_gain(plugin, duration=4.0):
    """Send pink noise and measure the wet output RMS relative to input RMS."""
    n = int(SR * duration)

    # Generate pink noise (1/f spectrum)
    white = np.random.randn(n).astype(np.float32) * 0.1  # -20 dBFS
    # Simple 1/f filter (Voss-McCartney approximation)
    pink = np.zeros(n, dtype=np.float32)
    b = [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
    for i in range(n):
        w = white[i]
        b[0] = 0.99886 * b[0] + w * 0.0555179
        b[1] = 0.99332 * b[1] + w * 0.0750759
        b[2] = 0.96900 * b[2] + w * 0.1538520
        b[3] = 0.86650 * b[3] + w * 0.3104856
        b[4] = 0.55000 * b[4] + w * 0.5329522
        b[5] = -0.7616 * b[5] - w * 0.0168980
        pink[i] = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362
        b[6] = w * 0.115926
    pink *= 0.1 / max(np.max(np.abs(pink)), 1e-30)  # Normalize to ~-20 dBFS

    stereo_in = np.stack([pink, pink])
    out = plugin(stereo_in, SR)
    mono_out = (out[0] + out[1]) * 0.5

    # Measure RMS over last 3 seconds (skip 1s buildup)
    start = int(SR * 1.0)
    in_rms = np.sqrt(np.mean(pink[start:] ** 2))
    out_rms = np.sqrt(np.mean(mono_out[start:] ** 2))

    in_db = 20 * np.log10(max(in_rms, 1e-30))
    out_db = 20 * np.log10(max(out_rms, 1e-30))
    gain_db = out_db - in_db

    return in_db, out_db, gain_db


def main():
    dv_path = find_plugin(DUSKVERB_PATHS)
    if not dv_path:
        print("ERROR: DuskVerb not found"); return
    dv = load_plugin(dv_path)
    if not dv: return

    vv_path = find_plugin(REFERENCE_REVERB_PATHS)
    vv = load_plugin(vv_path) if vv_path else None

    # Standard settings for each mode (100% wet, moderate decay)
    dv_modes = {
        "Hall":    {"algorithm": "Hall",    "decay_time": 2.5, "size": 0.70, "diffusion": 0.75,
                    "mod_depth": 0.35, "mod_rate": 1.0, "pre_delay": 20.0,
                    "early_ref_level": 0.5, "early_ref_size": 0.7,
                    "treble_multiply": 1.0, "bass_multiply": 1.0, "crossover": 1000,
                    "lo_cut": 20, "hi_cut": 20000, "width": 1.0, "dry_wet": 1.0},
        "Plate":   {"algorithm": "Plate",   "decay_time": 2.0, "size": 0.65, "diffusion": 0.85,
                    "mod_depth": 0.25, "mod_rate": 1.0, "pre_delay": 0.0,
                    "early_ref_level": 0.0, "early_ref_size": 0.0,
                    "treble_multiply": 1.0, "bass_multiply": 1.0, "crossover": 1000,
                    "lo_cut": 20, "hi_cut": 20000, "width": 1.0, "dry_wet": 1.0},
        "Room":    {"algorithm": "Room",    "decay_time": 2.0, "size": 0.60, "diffusion": 0.70,
                    "mod_depth": 0.30, "mod_rate": 1.0, "pre_delay": 10.0,
                    "early_ref_level": 0.5, "early_ref_size": 0.6,
                    "treble_multiply": 1.0, "bass_multiply": 1.0, "crossover": 1000,
                    "lo_cut": 20, "hi_cut": 20000, "width": 1.0, "dry_wet": 1.0},
        "Chamber": {"algorithm": "Chamber", "decay_time": 2.0, "size": 0.55, "diffusion": 0.70,
                    "mod_depth": 0.25, "mod_rate": 1.0, "pre_delay": 10.0,
                    "early_ref_level": 0.5, "early_ref_size": 0.6,
                    "treble_multiply": 1.0, "bass_multiply": 1.0, "crossover": 1000,
                    "lo_cut": 20, "hi_cut": 20000, "width": 1.0, "dry_wet": 1.0},
        "Ambient": {"algorithm": "Ambient", "decay_time": 3.0, "size": 0.80, "diffusion": 0.90,
                    "mod_depth": 0.45, "mod_rate": 1.0, "pre_delay": 30.0,
                    "early_ref_level": 0.3, "early_ref_size": 0.8,
                    "treble_multiply": 1.0, "bass_multiply": 1.0, "crossover": 1000,
                    "lo_cut": 20, "hi_cut": 20000, "width": 1.0, "dry_wet": 1.0},
    }

    vv_modes = {
        "Hall":    {"_reverbmode": 0.0417, "_size": 0.70, "_diffusion_early": 0.75,
                    "_diffusion_late": 0.75, "_mod_depth": 0.35, "_mod_rate": 0.50,
                    "_predelay": 0.067, "_decay": 0.35, "_colormode": 0.333,
                    "_high_cut": 1.0, "_low_cut": 0.0, "_bassmult": 0.50,
                    "_bassxover": 0.40, "_highshelf": 0.0, "_highfreq": 0.50},
        "Room":    {"_reverbmode": 0.1250, "_size": 0.60, "_diffusion_early": 0.70,
                    "_diffusion_late": 0.70, "_mod_depth": 0.30, "_mod_rate": 0.50,
                    "_predelay": 0.033, "_decay": 0.35, "_colormode": 0.333,
                    "_high_cut": 1.0, "_low_cut": 0.0, "_bassmult": 0.50,
                    "_bassxover": 0.40, "_highshelf": 0.0, "_highfreq": 0.50},
        "Chamber": {"_reverbmode": 0.1667, "_size": 0.55, "_diffusion_early": 0.70,
                    "_diffusion_late": 0.70, "_mod_depth": 0.25, "_mod_rate": 0.50,
                    "_predelay": 0.033, "_decay": 0.35, "_colormode": 0.333,
                    "_high_cut": 1.0, "_low_cut": 0.0, "_bassmult": 0.50,
                    "_bassxover": 0.40, "_highshelf": 0.0, "_highfreq": 0.50},
    }

    print("=" * 70)
    print("  Absolute Wet Gain: DuskVerb vs ReferenceReverb (100% wet, pink noise)")
    print("=" * 70)
    print(f"  {'Mode':<12s}  {'Input':>8s}  {'DV Out':>8s}  {'DV Gain':>8s}  {'VV Out':>8s}  {'VV Gain':>8s}")
    print(f"  {'-'*62}")

    for mode in ["Hall", "Plate", "Room", "Chamber", "Ambient"]:
        apply_duskverb_params(dv, dv_modes[mode])
        flush(dv)
        in_db, dv_out, dv_gain = measure_wet_gain(dv)

        vv_out_str = ""
        vv_gain_str = ""
        if vv and mode in vv_modes:
            apply_vv(vv, vv_modes[mode])
            flush(vv)
            _, vv_out, vv_gain = measure_wet_gain(vv)
            vv_out_str = f"{vv_out:+.1f}dB"
            vv_gain_str = f"{vv_gain:+.1f}dB"

        print(f"  {mode:<12s}  {in_db:+.1f}dB  {dv_out:+.1f}dB  {dv_gain:+.1f}dB  {vv_out_str:>8s}  {vv_gain_str:>8s}")

    print()
    print("  Gain = output RMS - input RMS (both at 100% wet)")
    print("  Positive = louder than input, Negative = quieter than input")
    print()

if __name__ == "__main__":
    main()
