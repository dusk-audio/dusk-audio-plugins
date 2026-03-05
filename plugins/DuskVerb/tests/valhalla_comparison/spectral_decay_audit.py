#!/usr/bin/env python3
"""Spectral decay audit: 1/3-octave RT60 comparison between DuskVerb and VintageVerb.

Captures IRs for representative presets per algorithm, measures RT60 at every
1/3-octave band, and prints a detailed comparison showing where DV's frequency
response diverges from VV.
"""

import json
import os
import sys
import numpy as np
from pedalboard import load_plugin

# Add parent directory for imports
sys.path.insert(0, os.path.dirname(__file__))

from config import (
    SAMPLE_RATE, find_plugin, DUSKVERB_PATHS, VINTAGEVERB_PATHS,
    apply_duskverb_params, apply_valhalla_params,
)
from generate_test_signals import make_impulse
from preset_suite import (
    load_qualifying_presets, translate_preset, calibrate_dv_decay,
    process_stereo, flush_plugin, VV_MODE_TO_DV, VV_MODE_NAMES,
)
import reverb_metrics as metrics

# ---------------------------------------------------------------------------
# Representative presets: 2 per algorithm (one solid pass, one borderline/known issue)
# ---------------------------------------------------------------------------
AUDIT_PRESETS = {
    "Hall":    ["Concert Wave", "Pad Hall"],
    "Plate":   ["Vocal Plate", "Drum Plate"],
    "Room":    ["Fat Snare Room", "Long Dark 70s Snare Room"],
    "Chamber": ["Rich Chamber", "A Plate"],
    "Ambient": ["Short Vocal Ambience", "Big Ambience Gate"],
}

# 1/3-octave band definitions (matching reverb_metrics.THIRD_OCTAVE_BANDS)
THIRD_OCTAVE_BANDS = {
    "100": 100, "125": 125, "160": 160, "200": 200, "250": 250,
    "315": 315, "400": 400, "500": 500, "630": 630, "800": 800,
    "1k": 1000, "1.25k": 1250, "1.6k": 1600, "2k": 2000, "2.5k": 2500,
    "3.15k": 3150, "4k": 4000, "5k": 5000, "6.3k": 6300, "8k": 8000,
    "10k": 10000,
}

BAND_NAMES = list(THIRD_OCTAVE_BANDS.keys())
BAND_FREQS = list(THIRD_OCTAVE_BANDS.values())

# Frequency regions for summary
LOW_RANGE = (100, 500)
MID_RANGE = (500, 2000)
HIGH_RANGE = (2000, 16000)


def classify_ratio(ratio):
    """Classify a DV/VV RT60 ratio."""
    if ratio is None:
        return "N/A"
    if ratio < 0.80:
        return "TOO FAST"
    if ratio > 1.25:
        return "TOO SLOW"
    return "OK"


def band_freq(name):
    """Get the center frequency for a band name."""
    return THIRD_OCTAVE_BANDS[name]


def region_avg(rt60_ratios, freq_lo, freq_hi):
    """Average RT60 ratio for bands in a frequency region."""
    vals = []
    for name, freq in THIRD_OCTAVE_BANDS.items():
        if freq_lo <= freq <= freq_hi and name in rt60_ratios:
            r = rt60_ratios[name]
            if r is not None:
                vals.append(r)
    return np.mean(vals) if vals else None


def divergence_onset(rt60_ratios, threshold=0.80):
    """Find the lowest frequency where ratio drops below threshold."""
    for name in BAND_NAMES:
        freq = THIRD_OCTAVE_BANDS[name]
        r = rt60_ratios.get(name)
        if r is not None and r < threshold:
            return freq
    return None


def capture_ir_pair(dv_plugin, vv_plugin, preset_info, sr=SAMPLE_RATE):
    """Capture matched IR pair from VV and DV for a preset.

    Returns (dv_ir_l, vv_ir_l, dv_params, info_str) or None on failure.
    """
    name = preset_info["name"]
    vv_params = preset_info["params"]
    mode_float = preset_info["mode_float"]
    dv_algorithm = VV_MODE_TO_DV.get(mode_float, "Hall")
    mode_name = VV_MODE_NAMES.get(mode_float, "Unknown")

    # Plate override (same as preset_suite.py)
    if "plate" in name.lower() and dv_algorithm in ("Chamber", "Ambient"):
        dv_algorithm = "Plate"

    is_room = (mode_float == 0.1250)
    sig_dur = 40.0 if is_room else 12.0
    flush_dur = max(2.0, sig_dur * 0.3)

    # Translate VV → DV params
    dv_params = translate_preset(vv_params, dv_algorithm, mode_name, name)

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

    impulse = make_impulse(sig_dur)

    # Capture VV IR
    apply_valhalla_params(vv_plugin, vv_config)
    flush_plugin(vv_plugin, sr, flush_dur)
    vv_ir_l, _ = process_stereo(vv_plugin, impulse, sr)
    flush_plugin(vv_plugin, sr, flush_dur)

    # Measure VV RT60 at 500Hz for calibration
    vv_rt60 = metrics.measure_rt60_per_band(vv_ir_l, sr)
    vv_rt60_500 = vv_rt60.get("500 Hz")

    # Calibrate DV decay to match VV at 500Hz
    info_str = ""
    is_gate = dv_params.get("gate_hold", 0) > 0
    if vv_rt60_500 and vv_rt60_500 > 0 and not is_gate:
        cal_decay, cal_rt60 = calibrate_dv_decay(
            dv_plugin, vv_rt60_500, dv_params, sr, sig_dur)
        if cal_decay is not None:
            dv_params["decay_time"] = cal_decay
            info_str = f"calibrated decay={cal_decay:.2f}s, RT60@500={cal_rt60:.2f}s (target={vv_rt60_500:.2f}s)"
    elif is_gate:
        info_str = f"gate preset: hold={dv_params['gate_hold']:.0f}ms, release={dv_params['gate_release']:.0f}ms"
    else:
        info_str = f"VV RT60@500Hz unmeasurable (gated or very short decay)"

    # Capture DV IR with calibrated params
    apply_duskverb_params(dv_plugin, dv_params)
    flush_plugin(dv_plugin, sr, flush_dur)
    dv_ir_l, _ = process_stereo(dv_plugin, impulse, sr)
    flush_plugin(dv_plugin, sr, flush_dur)

    return dv_ir_l, vv_ir_l, dv_params, info_str


def print_preset_table(name, algo, dv_ir, vv_ir, info_str, sr=SAMPLE_RATE):
    """Print per-band RT60 comparison for a preset. Returns rt60_ratios dict."""
    dv_rt60 = metrics.measure_rt60_third_octave(dv_ir, sr)
    vv_rt60 = metrics.measure_rt60_third_octave(vv_ir, sr)

    print(f"\n=== {algo}: {name} ===")
    if info_str:
        print(f"    ({info_str})")
    print(f"{'Band':>8s}  {'VV RT60(s)':>10s}  {'DV RT60(s)':>10s}  {'Ratio':>6s}   {'Status'}")
    print("    " + "-" * 60)

    rt60_ratios = {}
    for band_name in BAND_NAMES:
        dv_val = dv_rt60.get(band_name)
        vv_val = vv_rt60.get(band_name)

        if dv_val is not None and vv_val is not None and vv_val > 0:
            ratio = dv_val / vv_val
            rt60_ratios[band_name] = ratio
            status = classify_ratio(ratio)
            marker = "  <-" if status != "OK" else ""
            print(f"    {band_name:>6s}  {vv_val:>10.2f}  {dv_val:>10.2f}  {ratio:>6.2f}   {status}{marker}")
        else:
            rt60_ratios[band_name] = None
            dv_s = f"{dv_val:.2f}" if dv_val is not None else "N/A"
            vv_s = f"{vv_val:.2f}" if vv_val is not None else "N/A"
            print(f"    {band_name:>6s}  {vv_s:>10s}  {dv_s:>10s}  {'N/A':>6s}   N/A")

    return rt60_ratios


def print_algorithm_summary(algo_data):
    """Print the per-algorithm summary table."""
    print(f"\n{'='*80}")
    print("ALGORITHM SUMMARY")
    print(f"{'='*80}")
    print(f"{'':>15s}  {'Low (100-500Hz)':>16s}  {'Mid (500-2kHz)':>16s}  {'High (2k-16kHz)':>16s}")
    print("    " + "-" * 65)

    for algo in ["Hall", "Plate", "Room", "Chamber", "Ambient"]:
        if algo not in algo_data:
            continue
        # Average across all presets for this algorithm
        all_ratios = algo_data[algo]
        low_vals, mid_vals, high_vals = [], [], []
        for ratios in all_ratios:
            l = region_avg(ratios, *LOW_RANGE)
            m = region_avg(ratios, *MID_RANGE)
            h = region_avg(ratios, *HIGH_RANGE)
            if l is not None:
                low_vals.append(l)
            if m is not None:
                mid_vals.append(m)
            if h is not None:
                high_vals.append(h)

        def fmt(vals):
            if not vals:
                return "N/A"
            avg = np.mean(vals)
            status = classify_ratio(avg)
            return f"{avg:.2f} {status}"

        print(f"    {algo:<11s}  {fmt(low_vals):>16s}  {fmt(mid_vals):>16s}  {fmt(high_vals):>16s}")

    # Divergence onset
    print(f"\n    Divergence onset frequency (where ratio first drops below 0.80):")
    for algo in ["Hall", "Plate", "Room", "Chamber", "Ambient"]:
        if algo not in algo_data:
            continue
        # Use worst-case (lowest onset) across presets
        onsets = []
        for ratios in algo_data[algo]:
            onset = divergence_onset(ratios)
            if onset is not None:
                onsets.append(onset)
        if onsets:
            worst = min(onsets)
            if worst >= 1000:
                print(f"      {algo:<11s}: {worst/1000:.1f} kHz")
            else:
                print(f"      {algo:<11s}: {worst} Hz")
        else:
            print(f"      {algo:<11s}: N/A (all bands OK)")


def main():
    # Find plugins
    dv_path = find_plugin(DUSKVERB_PATHS)
    vv_path = find_plugin(VINTAGEVERB_PATHS)
    if not dv_path:
        print("ERROR: DuskVerb plugin not found")
        sys.exit(1)
    if not vv_path:
        print("ERROR: VintageVerb plugin not found")
        sys.exit(1)

    print(f"DuskVerb:     {dv_path}")
    print(f"VintageVerb:  {vv_path}")
    print(f"Sample rate:  {SAMPLE_RATE} Hz")

    dv_plugin = load_plugin(dv_path)
    vv_plugin = load_plugin(vv_path)

    # Load presets
    all_presets = load_qualifying_presets()
    preset_by_name = {p["name"]: p for p in all_presets}

    # Collect per-algorithm ratio data for summary
    algo_data = {}  # algo -> [ratios_dict, ...]

    for algo, preset_names in AUDIT_PRESETS.items():
        algo_data[algo] = []
        for pname in preset_names:
            if pname not in preset_by_name:
                print(f"\nWARNING: Preset '{pname}' not found, skipping")
                continue

            preset_info = preset_by_name[pname]
            print(f"\nCapturing: {algo} / {pname} ...", end="", flush=True)

            result = capture_ir_pair(dv_plugin, vv_plugin, preset_info)
            if result is None:
                print(" FAILED")
                continue
            print(" done")

            dv_ir, vv_ir, dv_params, info_str = result
            ratios = print_preset_table(pname, algo, dv_ir, vv_ir, info_str)
            algo_data[algo].append(ratios)

    # Print summary
    print_algorithm_summary(algo_data)
    print("\nDone.")


if __name__ == "__main__":
    main()
