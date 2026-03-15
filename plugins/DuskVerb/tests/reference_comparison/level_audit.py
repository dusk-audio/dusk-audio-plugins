#!/usr/bin/env python3
"""Real-audio level audit: DV vs VV across all factory presets.

Processes snare_hit.wav through both plugins at 100% wet for each preset
and measures output RMS. Reports per-preset dB delta (DV - VV) and
per-algorithm averages.

Usage:
    python3 level_audit.py                    # All presets
    python3 level_audit.py --mode Room        # Single algorithm
    python3 level_audit.py --preset "Vocal Plate"  # Single preset
    python3 level_audit.py --export-trim      # Output gainTrim C++ values
"""

import argparse
import json
import os
import sys
import numpy as np
from pedalboard import load_plugin

sys.path.insert(0, os.path.dirname(__file__))
from config import (
    SAMPLE_RATE, DUSKVERB_PATHS, REFERENCE_REVERB_PATHS,
    find_plugin, apply_duskverb_params, apply_reference_params,
)
from preset_suite import (
    VV_MODE_TO_DV, VV_MODE_NAMES, translate_preset,
    load_qualifying_presets, process_stereo, flush_plugin,
    _vv_display_decay,
)

SR = SAMPLE_RATE


def load_snare():
    """Load snare_hit.wav test signal."""
    import soundfile as sf
    path = os.path.join(os.path.dirname(__file__), "test_signals", "snare_hit.wav")
    data, file_sr = sf.read(path, dtype="float32")
    if data.ndim > 1:
        data = data[:, 0]  # mono
    if file_sr != SR:
        # Simple resample by ratio (good enough for level measurement)
        from scipy.signal import resample
        data = resample(data, int(len(data) * SR / file_sr)).astype(np.float32)
    return data


def rms_db(signal):
    """RMS level in dB."""
    rms = np.sqrt(np.mean(signal.astype(np.float64) ** 2))
    return 20.0 * np.log10(max(rms, 1e-30))


def process_preset(dv_plugin, vv_plugin, preset_info, snare, sr=SR):
    """Process snare through both plugins for one preset, return dB delta."""
    name = preset_info["name"]
    vv_params = preset_info["params"]
    mode_float = preset_info["mode_float"]
    dv_algorithm = VV_MODE_TO_DV.get(mode_float, "Hall")
    mode_name = VV_MODE_NAMES.get(mode_float, "Unknown")

    # Override: plate-like presets in Chamber/Ambience use Plate algorithm
    if "plate" in name.lower() and dv_algorithm in ("Chamber", "Ambient"):
        dv_algorithm = "Plate"

    # Translate VV → DV params
    dv_params = translate_preset(vv_params, dv_algorithm, mode_name, name)

    # Override decay with VV display value (matching FactoryPresets.h)
    vv_decay_norm = vv_params.get("Decay", 0.3)
    if dv_params.get("gate_hold", 0) > 0:
        dv_params["decay_time"] = 2.0
    else:
        dv_params["decay_time"] = _vv_display_decay(vv_decay_norm)

    # Pad snare with tail room (3s for short presets, longer for long decay)
    tail_sec = max(3.0, dv_params["decay_time"] * 2.0)
    padded = np.concatenate([snare, np.zeros(int(sr * tail_sec), dtype=np.float32)])

    # Build VV config
    param_key_map = {
        "ReverbMode": "_reverbmode", "ColorMode": "_colormode",
        "Decay": "_decay", "Size": "_size", "PreDelay": "_predelay",
        "EarlyDiffusion": "_diffusion_early", "LateDiffusion": "_diffusion_late",
        "ModRate": "_mod_rate", "ModDepth": "_mod_depth",
        "HighCut": "_high_cut", "LowCut": "_low_cut",
        "BassMult": "_bassmult", "BassXover": "_bassxover",
        "HighShelf": "_highshelf", "HighFreq": "_highfreq",
        "Attack": "_attack",
    }
    vv_config = {}
    for vv_key, semantic_key in param_key_map.items():
        if vv_key in vv_params:
            vv_config[semantic_key] = vv_params[vv_key]

    # Process VV
    apply_reference_params(vv_plugin, vv_config)
    flush_plugin(vv_plugin, sr, 2.0)
    vv_l, vv_r = process_stereo(vv_plugin, padded, sr)
    flush_plugin(vv_plugin, sr, 2.0)

    # Process DV
    apply_duskverb_params(dv_plugin, dv_params)
    flush_plugin(dv_plugin, sr, 2.0)
    dv_l, dv_r = process_stereo(dv_plugin, padded, sr)
    flush_plugin(dv_plugin, sr, 2.0)

    # Measure RMS of full output (both channels)
    vv_rms = rms_db(np.concatenate([vv_l, vv_r]))
    dv_rms = rms_db(np.concatenate([dv_l, dv_r]))
    delta = dv_rms - vv_rms

    return {
        "name": name,
        "mode": mode_name,
        "algorithm": dv_algorithm,
        "vv_rms": vv_rms,
        "dv_rms": dv_rms,
        "delta": delta,
        "decay_time": dv_params["decay_time"],
    }


def main():
    parser = argparse.ArgumentParser(description="Real-audio level audit")
    parser.add_argument("--mode", help="Filter by VV mode name")
    parser.add_argument("--preset", help="Run single preset by name")
    parser.add_argument("--export-trim", action="store_true",
                        help="Output gainTrim values for FactoryPresets.h")
    args = parser.parse_args()

    dv_path = find_plugin(DUSKVERB_PATHS)
    vv_path = find_plugin(REFERENCE_REVERB_PATHS)
    if not dv_path or not vv_path:
        print("ERROR: Plugin(s) not found")
        sys.exit(1)

    print(f"DuskVerb:    {dv_path}")
    print(f"VintageVerb: {vv_path}")
    print(f"Sample rate: {SR} Hz")

    dv = load_plugin(dv_path)
    vv = load_plugin(vv_path)
    snare = load_snare()
    print(f"Snare: {len(snare)} samples ({len(snare)/SR:.2f}s), RMS={rms_db(snare):.1f} dB")
    print()

    presets = load_qualifying_presets()
    if args.preset:
        presets = [p for p in presets if p["name"] == args.preset]
    elif args.mode:
        presets = [p for p in presets if VV_MODE_NAMES.get(p["mode_float"]) == args.mode]

    if not presets:
        print("No matching presets found.")
        sys.exit(1)

    # Header
    print(f"  {'Mode':<16s} {'Preset':<32s} {'VV RMS':>8s} {'DV RMS':>8s} {'Delta':>8s} {'Decay':>6s}")
    print(f"  {'-'*90}")

    results = []
    for p in presets:
        r = process_preset(dv, vv, p, snare)
        results.append(r)
        print(f"  {r['mode']:<16s} {r['name']:<32s} {r['vv_rms']:>7.1f}  {r['dv_rms']:>7.1f}  "
              f"{r['delta']:>+7.1f}  {r['decay_time']:>5.2f}s")

    print(f"  {'-'*90}")

    # Per-algorithm summary
    print(f"\n  Per-algorithm averages:")
    print(f"  {'Algorithm':<16s} {'Count':>5s} {'Avg Δ':>8s} {'Min Δ':>8s} {'Max Δ':>8s} {'Spread':>8s}")
    print(f"  {'-'*60}")
    algos = sorted(set(r["algorithm"] for r in results))
    for algo in algos:
        group = [r for r in results if r["algorithm"] == algo]
        deltas = [r["delta"] for r in group]
        avg = np.mean(deltas)
        print(f"  {algo:<16s} {len(group):>5d} {avg:>+7.1f}  {min(deltas):>+7.1f}  "
              f"{max(deltas):>+7.1f}  {max(deltas)-min(deltas):>7.1f}")

    overall_avg = np.mean([r["delta"] for r in results])
    print(f"\n  Overall average: {overall_avg:+.1f} dB ({len(results)} presets)")

    # Export gainTrim values
    if args.export_trim:
        print(f"\n  // gainTrim values (negate delta to compensate)")
        # Sort by the order they appear in FactoryPresets.h
        # Group by algorithm category
        algo_order = ["Hall", "Plate", "Room", "Chamber", "Ambient"]
        for algo in algo_order:
            group = [r for r in results if r["algorithm"] == algo]
            for r in group:
                trim = -r["delta"]
                print(f"  // {r['name']}: {trim:+.1f} dB")

    print("\nDone.")


if __name__ == "__main__":
    main()
