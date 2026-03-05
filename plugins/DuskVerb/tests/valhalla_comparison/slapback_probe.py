#!/usr/bin/env python3
"""Detect slapback/discrete echo artifacts in DuskVerb vs VintageVerb impulse responses."""

import numpy as np
import pedalboard
from config import (SAMPLE_RATE, DUSKVERB_PATHS, VINTAGEVERB_PATHS,
                     find_plugin, apply_duskverb_params, VALHALLA_PARAM_MAP)

SR = SAMPLE_RATE

def load_plugin(path):
    try:
        return pedalboard.load_plugin(path)
    except Exception as e:
        print(f"ERROR loading {path}: {e}")
        return None

def render_ir(plugin, duration=1.0):
    """Render mono-summed impulse response."""
    n = int(SR * duration)
    impulse = np.zeros((2, n), dtype=np.float32)
    impulse[0, 0] = 1.0
    impulse[1, 0] = 1.0
    out = plugin(impulse, SR)
    return (out[0] + out[1]) * 0.5

def flush(plugin, dur=5.0):
    silence = np.zeros((2, int(SR * dur)), dtype=np.float32)
    plugin(silence, SR)

def apply_vv(plugin, params):
    for key, value in params.items():
        name = VALHALLA_PARAM_MAP.get(key, key.lstrip("_"))
        try:
            setattr(plugin, name, value)
        except Exception:
            pass

def find_peaks(ir, min_time_ms=5, max_time_ms=200, threshold_db=-30):
    """Find discrete peaks in the IR that stand out above the local envelope.

    Returns list of (time_ms, amplitude_dB, prominence_dB) tuples.
    """
    abs_ir = np.abs(ir)
    peak_val = np.max(abs_ir)
    if peak_val < 1e-30:
        return []

    # Work in dB relative to peak
    ir_db = 20 * np.log10(np.maximum(abs_ir, 1e-30) / peak_val)

    min_idx = int(min_time_ms * SR / 1000)
    max_idx = int(max_time_ms * SR / 1000)
    max_idx = min(max_idx, len(ir_db))

    # Compute local RMS envelope (2ms window)
    win = int(0.002 * SR)
    peaks = []

    for i in range(min_idx, max_idx):
        # Check if this sample is a local max within ±1ms
        half_ms = int(0.001 * SR)
        lo = max(0, i - half_ms)
        hi = min(len(abs_ir), i + half_ms)
        if abs_ir[i] < np.max(abs_ir[lo:hi]) * 0.99:
            continue

        # Compute local envelope (RMS in ±3ms window, excluding ±0.5ms around peak)
        env_lo = max(0, i - int(0.003 * SR))
        env_hi = min(len(ir), i + int(0.003 * SR))
        exc_lo = max(0, i - int(0.0005 * SR))
        exc_hi = min(len(ir), i + int(0.0005 * SR))

        env_samples = np.concatenate([abs_ir[env_lo:exc_lo], abs_ir[exc_hi:env_hi]])
        if len(env_samples) < 4:
            continue
        local_rms = np.sqrt(np.mean(env_samples ** 2))
        local_rms_db = 20 * np.log10(max(local_rms, 1e-30) / peak_val)

        prominence = ir_db[i] - local_rms_db
        time_ms = i / SR * 1000

        if ir_db[i] > threshold_db and prominence > 3.0:
            peaks.append((time_ms, ir_db[i], prominence))

    # Deduplicate peaks within 2ms
    if not peaks:
        return peaks
    peaks.sort(key=lambda x: x[0])
    deduped = [peaks[0]]
    for p in peaks[1:]:
        if p[0] - deduped[-1][0] > 2.0:
            deduped.append(p)
        elif p[2] > deduped[-1][2]:
            deduped[-1] = p

    return sorted(deduped, key=lambda x: -x[2])  # Sort by prominence


def analyze_mode(name, ir):
    """Analyze an IR for slapback artifacts."""
    peaks = find_peaks(ir)

    peak_val = np.max(np.abs(ir))
    peak_t = np.argmax(np.abs(ir)) / SR * 1000
    peak_db = 20 * np.log10(max(peak_val, 1e-30))

    print(f"\n  {name}")
    print(f"    Main peak: {peak_db:.1f} dB @ {peak_t:.1f}ms")

    if not peaks:
        print(f"    No discrete echoes found (clean diffuse tail)")
        return

    print(f"    Discrete echoes (>{3:.0f}dB above local envelope):")
    for t, amp, prom in peaks[:8]:
        marker = " *** SLAPBACK" if prom > 6.0 and t > 10 else ""
        print(f"      {t:6.1f}ms  {amp:+6.1f}dB  prominence={prom:.1f}dB{marker}")


def main():
    dv_path = find_plugin(DUSKVERB_PATHS)
    if not dv_path:
        print("ERROR: DuskVerb not found"); return
    dv = load_plugin(dv_path)
    if not dv: return

    vv_path = find_plugin(VINTAGEVERB_PATHS)
    vv = load_plugin(vv_path) if vv_path else None

    # Test with typical percussive preset settings (moderate size, some diffusion)
    dv_modes = {
        "Hall": {"algorithm": "Hall", "decay_time": 2.5, "size": 0.70,
                 "diffusion": 0.75, "mod_depth": 0.35, "mod_rate": 1.0,
                 "pre_delay": 20.0, "early_ref_level": 0.5, "early_ref_size": 0.7,
                 "treble_multiply": 1.0, "bass_multiply": 1.0, "crossover": 1000,
                 "lo_cut": 20, "hi_cut": 20000, "width": 1.0, "dry_wet": 1.0},
        "Chamber": {"algorithm": "Chamber", "decay_time": 2.0, "size": 0.55,
                     "diffusion": 0.70, "mod_depth": 0.25, "mod_rate": 1.0,
                     "pre_delay": 10.0, "early_ref_level": 0.5, "early_ref_size": 0.6,
                     "treble_multiply": 1.0, "bass_multiply": 1.0, "crossover": 1000,
                     "lo_cut": 20, "hi_cut": 20000, "width": 1.0, "dry_wet": 1.0},
        "Room": {"algorithm": "Room", "decay_time": 2.0, "size": 0.60,
                  "diffusion": 0.70, "mod_depth": 0.30, "mod_rate": 1.0,
                  "pre_delay": 10.0, "early_ref_level": 0.5, "early_ref_size": 0.6,
                  "treble_multiply": 1.0, "bass_multiply": 1.0, "crossover": 1000,
                  "lo_cut": 20, "hi_cut": 20000, "width": 1.0, "dry_wet": 1.0},
        "Plate": {"algorithm": "Plate", "decay_time": 2.0, "size": 0.65,
                   "diffusion": 0.85, "mod_depth": 0.25, "mod_rate": 1.0,
                   "pre_delay": 0.0, "early_ref_level": 0.0, "early_ref_size": 0.0,
                   "treble_multiply": 1.0, "bass_multiply": 1.0, "crossover": 1000,
                   "lo_cut": 20, "hi_cut": 20000, "width": 1.0, "dry_wet": 1.0},
        "Ambient": {"algorithm": "Ambient", "decay_time": 3.0, "size": 0.80,
                     "diffusion": 0.90, "mod_depth": 0.45, "mod_rate": 1.0,
                     "pre_delay": 30.0, "early_ref_level": 0.3, "early_ref_size": 0.8,
                     "treble_multiply": 1.0, "bass_multiply": 1.0, "crossover": 1000,
                     "lo_cut": 20, "hi_cut": 20000, "width": 1.0, "dry_wet": 1.0},
    }

    # VV equivalent modes
    vv_modes = {
        "Hall": {"_reverbmode": 0.0417, "_size": 0.70, "_diffusion_early": 0.75,
                 "_diffusion_late": 0.75, "_mod_depth": 0.35, "_mod_rate": 0.50,
                 "_predelay": 0.067, "_decay": 0.35, "_colormode": 0.333,
                 "_high_cut": 1.0, "_low_cut": 0.0, "_bassmult": 0.50,
                 "_bassxover": 0.40, "_highshelf": 0.0, "_highfreq": 0.50},
        "Chamber": {"_reverbmode": 0.1667, "_size": 0.55, "_diffusion_early": 0.70,
                     "_diffusion_late": 0.70, "_mod_depth": 0.25, "_mod_rate": 0.50,
                     "_predelay": 0.033, "_decay": 0.35, "_colormode": 0.333,
                     "_high_cut": 1.0, "_low_cut": 0.0, "_bassmult": 0.50,
                     "_bassxover": 0.40, "_highshelf": 0.0, "_highfreq": 0.50},
        "Room": {"_reverbmode": 0.1250, "_size": 0.60, "_diffusion_early": 0.70,
                  "_diffusion_late": 0.70, "_mod_depth": 0.30, "_mod_rate": 0.50,
                  "_predelay": 0.033, "_decay": 0.35, "_colormode": 0.333,
                  "_high_cut": 1.0, "_low_cut": 0.0, "_bassmult": 0.50,
                  "_bassxover": 0.40, "_highshelf": 0.0, "_highfreq": 0.50},
    }

    print("=" * 70)
    print("  DuskVerb — Slapback/Discrete Echo Analysis")
    print("=" * 70)

    for mode, params in dv_modes.items():
        apply_duskverb_params(dv, params)
        flush(dv)
        ir = render_ir(dv, 0.5)
        analyze_mode(f"DV {mode}", ir)

    if vv:
        print("\n" + "=" * 70)
        print("  VintageVerb — Reference")
        print("=" * 70)

        for mode, params in vv_modes.items():
            apply_vv(vv, params)
            flush(vv)
            ir = render_ir(vv, 0.5)
            analyze_mode(f"VV {mode}", ir)

    print()

if __name__ == "__main__":
    main()
