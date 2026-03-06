#!/usr/bin/env python3
"""EDC probe: compare DV Room (dual-slope) vs DV Hall (no dual-slope) vs VV Room."""

import numpy as np
import pedalboard
from config import (SAMPLE_RATE, REFERENCE_PARAM_MAP, apply_duskverb_params,
                     DUSKVERB_PATHS, REFERENCE_REVERB_PATHS, find_plugin)

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

def render_ir(plugin, duration=40.0):
    n = int(SAMPLE_RATE * duration)
    impulse = np.zeros((2, n), dtype=np.float32)
    impulse[0, 0] = 1.0
    impulse[1, 0] = 1.0
    out = plugin(impulse, SAMPLE_RATE)
    return (out[0] + out[1]) * 0.5

def compute_edc(ir):
    energy = ir ** 2
    edc = np.cumsum(energy[::-1])[::-1]
    edc_db = 10.0 * np.log10(edc / max(edc[0], 1e-30))
    return edc_db

def analyze(name, ir):
    edc = compute_edc(ir)
    print(f"\n=== {name} ===")
    print(f"  {'Time':>8s}  {'EDC (dB)':>10s}  {'RMS 100ms':>10s}")
    print(f"  {'-'*35}")
    for t in [0.1, 0.5, 1.0, 2.0, 3.0, 5.0, 8.0, 10.0, 15.0, 20.0]:
        idx = int(t * SAMPLE_RATE)
        win = SAMPLE_RATE // 10
        if idx + win < len(ir) and idx < len(edc):
            rms = 20 * np.log10(max(np.sqrt(np.mean(ir[idx:idx+win]**2)), 1e-30))
            print(f"  {t:8.1f}  {edc[idx]:10.1f}  {rms:10.1f}")
    peak = np.max(np.abs(ir))
    peak_t = np.argmax(np.abs(ir)) / SAMPLE_RATE
    print(f"  Peak: {20*np.log10(max(peak,1e-30)):.1f} dB @ {peak_t*1000:.1f}ms")

def main():
    dv_path = find_plugin(DUSKVERB_PATHS)
    if dv_path is None:
        print("ERROR: DuskVerb plugin not found")
        return
    dv = load_plugin(dv_path)
    if dv is None:
        return

    # Use correct DV parameter names (from dv_preset / apply_duskverb_params)
    configs = [
        ("DV Room (decay=20, dual-slope)", {
            "algorithm": "Room", "decay_time": 20.0, "size": 0.70,
            "diffusion": 0.0, "mod_depth": 0.0, "mod_rate": 0.0,
            "treble_multiply": 1.0, "bass_multiply": 1.0, "crossover": 1000,
            "early_ref_level": 0.0, "early_ref_size": 0.0,
            "pre_delay": 0.0, "lo_cut": 20, "hi_cut": 20000, "width": 1.0,
        }),
        ("DV Hall (decay=20, reference)", {
            "algorithm": "Hall", "decay_time": 20.0, "size": 0.70,
            "diffusion": 0.0, "mod_depth": 0.0, "mod_rate": 0.0,
            "treble_multiply": 1.0, "bass_multiply": 1.0, "crossover": 1000,
            "early_ref_level": 0.0, "early_ref_size": 0.0,
            "pre_delay": 0.0, "lo_cut": 20, "hi_cut": 20000, "width": 1.0,
        }),
        ("DV Room (decay=5, dual-slope)", {
            "algorithm": "Room", "decay_time": 5.0, "size": 0.70,
            "diffusion": 0.0, "mod_depth": 0.0, "mod_rate": 0.0,
            "treble_multiply": 1.0, "bass_multiply": 1.0, "crossover": 1000,
            "early_ref_level": 0.0, "early_ref_size": 0.0,
            "pre_delay": 0.0, "lo_cut": 20, "hi_cut": 20000, "width": 1.0,
        }),
    ]

    for name, params in configs:
        apply_duskverb_params(dv, params)
        flush = np.zeros((2, SAMPLE_RATE * 10), dtype=np.float32)
        dv(flush, SAMPLE_RATE)
        ir = render_ir(dv, 40.0)
        analyze(name, ir)

    # VV Room reference
    vv_path = find_plugin(REFERENCE_REVERB_PATHS)
    vv = load_plugin(vv_path) if vv_path else None
    if vv:
        vv_params = {
            "_reverbmode": 0.1250, "_size": 0.70, "_diffusion_early": 0.0,
            "_diffusion_late": 0.0, "_mod_depth": 0.0, "_mod_rate": 0.0,
            "_predelay": 0.0, "_decay": 0.60, "_colormode": 0.333,
            "_high_cut": 1.0, "_low_cut": 0.0, "_bassmult": 0.50,
            "_bassxover": 0.40, "_highshelf": 0.0, "_highfreq": 0.50,
            "_attack": 0.50,
        }
        apply_vv(vv, vv_params)
        flush = np.zeros((2, SAMPLE_RATE * 10), dtype=np.float32)
        vv(flush, SAMPLE_RATE)
        ir = render_ir(vv, 40.0)
        analyze("VV Room (decay=0.60)", ir)

if __name__ == "__main__":
    main()
