#!/usr/bin/env python3
"""
IR Capture Pipeline — ground-truth DuskVerb vs ReferenceReverb impulse responses.

Captures stereo IRs for every qualifying preset and a reference set at
diagnostic settings (flat EQ, no mod, 5 decay times × 5 algorithms).

Usage:
    python3 capture_irs.py                        # Capture all 53 + reference
    python3 capture_irs.py --presets-only          # Skip reference set
    python3 capture_irs.py --reference-only        # Skip preset set
    python3 capture_irs.py --preset "Fat Snare"    # Single preset
    python3 capture_irs.py -j 4                    # Worker count
    python3 capture_irs.py --outdir ./my_irs       # Custom output
"""

import argparse
import json
import multiprocessing
import os
import sys
import datetime
import numpy as np
import soundfile as sf
from pedalboard import load_plugin

from config import (
    SAMPLE_RATE, DUSKVERB_PATHS, REFERENCE_REVERB_PATHS,
    find_plugin, apply_duskverb_params, apply_reference_params,
)
from generate_test_signals import make_impulse
from preset_suite import (
    load_qualifying_presets, translate_preset,
    VV_MODE_TO_DV, VV_MODE_NAMES, calibrate_dv_decay,
)
import reverb_metrics as metrics

SR = SAMPLE_RATE

# Gate presets — exclude from analysis (no DV equivalent)
GATE_PRESETS = {"Gated Snare", "Tight Ambience Gate", "Big Ambience Gate"}

# Reference IR settings: 5 algorithms × 5 VV decay values
REFERENCE_DECAYS = [0.05, 0.10, 0.15, 0.25, 0.40]
REFERENCE_ALGOS = {
    "Hall":    {"vv_mode": 0.0417, "vv_label": "Concert Hall"},
    "Plate":   {"vv_mode": 0.0833, "vv_label": "Plate"},
    "Room":    {"vv_mode": 0.1250, "vv_label": "Room"},
    "Chamber": {"vv_mode": 0.1667, "vv_label": "Chamber"},
    "Ambient": {"vv_mode": 0.2917, "vv_label": "Ambience"},
}


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def process_stereo(plugin, mono_signal, sr):
    stereo_in = np.stack([mono_signal, mono_signal], axis=0).astype(np.float32)
    output = plugin(stereo_in, sr)
    return output[0], output[1]


def flush_plugin(plugin, sr, duration_sec=2.0):
    silence = np.zeros(int(sr * duration_sec), dtype=np.float32)
    process_stereo(plugin, silence, sr)


def sanitize_name(name):
    """Convert preset name to filesystem-safe string."""
    return name.replace(" ", "_").replace("/", "-").replace("'", "")


def duration_to_60db(ir, sr):
    """Find time (seconds) where signal drops 60dB below peak."""
    env = np.abs(ir)
    peak = np.max(env)
    if peak < 1e-10:
        return 0.0
    threshold = peak * 1e-3  # -60dB
    for i in range(len(env) - 1, -1, -1):
        if env[i] > threshold:
            return (i + 1) / sr
    return 0.0


def peak_dbfs(ir):
    """Peak level in dBFS."""
    peak = np.max(np.abs(ir))
    if peak < 1e-20:
        return -200.0
    return 20.0 * np.log10(peak)


def save_ir(path, ir_l, ir_r, sr):
    """Save stereo IR as 32-bit float WAV."""
    stereo = np.stack([ir_l, ir_r], axis=-1)
    sf.write(path, stereo, sr, subtype="FLOAT")


# ---------------------------------------------------------------------------
# Preset capture
# ---------------------------------------------------------------------------
def capture_preset(dv, vv, preset_info, sr, output_dir):
    """Capture IR pair for a single preset.

    Returns metadata dict.
    """
    name = preset_info["name"]
    vv_params = preset_info["params"]
    mode_float = preset_info["mode_float"]
    dv_algorithm = VV_MODE_TO_DV.get(mode_float, "Hall")
    mode_name = VV_MODE_NAMES.get(mode_float, "Unknown")

    is_room = (mode_float == 0.1250)
    sig_dur = 40.0 if is_room else 12.0
    flush_dur = max(2.0, sig_dur * 0.3)

    safe_name = sanitize_name(name)
    dv_path = os.path.join(output_dir, "presets", f"{safe_name}_DV.wav")
    vv_path = os.path.join(output_dir, "presets", f"{safe_name}_VV.wav")

    # Translate VV → DV params
    dv_params = translate_preset(vv_params, dv_algorithm, mode_name, name=name)

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
    apply_reference_params(vv, vv_config)
    flush_plugin(vv, sr, flush_dur)
    vv_l, vv_r = process_stereo(vv, impulse, sr)
    flush_plugin(vv, sr, flush_dur)

    # Measure VV RT60 for calibration
    vv_rt60 = metrics.measure_rt60_per_band(vv_l, sr, {"500 Hz": 500})
    vv_rt60_500 = vv_rt60.get("500 Hz")

    # Calibrate DV decay
    cal_decay, cal_rt60 = None, None
    if vv_rt60_500 and vv_rt60_500 > 0:
        cal_decay, cal_rt60 = calibrate_dv_decay(
            dv, vv_rt60_500, dv_params, sr, sig_dur)
        if cal_decay is not None:
            dv_params["decay_time"] = cal_decay

    # Capture DV IR
    apply_duskverb_params(dv, dv_params)
    flush_plugin(dv, sr, flush_dur)
    dv_l, dv_r = process_stereo(dv, impulse, sr)
    flush_plugin(dv, sr, flush_dur)

    # Save
    save_ir(dv_path, dv_l, dv_r, sr)
    save_ir(vv_path, vv_l, vv_r, sr)

    return {
        "name": name,
        "mode": mode_name,
        "dv_algorithm": dv_algorithm,
        "is_gate": name in GATE_PRESETS,
        "dv_path": os.path.relpath(dv_path, output_dir),
        "vv_path": os.path.relpath(vv_path, output_dir),
        "dv_peak_dbfs": round(peak_dbfs(dv_l), 1),
        "vv_peak_dbfs": round(peak_dbfs(vv_l), 1),
        "dv_duration_to_60db": round(duration_to_60db(dv_l, sr), 2),
        "vv_duration_to_60db": round(duration_to_60db(vv_l, sr), 2),
        "calibrated_decay": round(cal_decay, 3) if cal_decay else None,
        "calibrated_rt60": round(cal_rt60, 3) if cal_rt60 else None,
        "target_rt60": round(vv_rt60_500, 3) if vv_rt60_500 else None,
    }


# ---------------------------------------------------------------------------
# Reference capture
# ---------------------------------------------------------------------------
def capture_reference(dv, vv, algo, vv_decay, sr, output_dir):
    """Capture reference IR pair at diagnostic settings.

    Returns metadata dict.
    """
    info = REFERENCE_ALGOS[algo]
    vv_mode = info["vv_mode"]

    is_room = (algo == "Room")
    sig_dur = 40.0 if is_room else 12.0
    flush_dur = max(2.0, sig_dur * 0.3)

    safe_name = f"{algo}_decay{vv_decay:.2f}"
    dv_path = os.path.join(output_dir, "reference", f"{safe_name}_DV.wav")
    vv_path = os.path.join(output_dir, "reference", f"{safe_name}_VV.wav")

    # DV: flat EQ, no mod
    dv_params = {
        "algorithm": algo,
        "decay_time": 1.0,  # placeholder — calibrated below
        "size": 0.5,
        "diffusion": 0.7,
        "treble_multiply": 1.0,
        "bass_multiply": 1.0,
        "crossover": 500,
        "mod_depth": 0.0,
        "mod_rate": 0.5,
        "early_ref_level": 0.0,
        "early_ref_size": 0.0,
        "pre_delay": 0.0,
        "lo_cut": 20,
        "hi_cut": 20000,
        "width": 1.0,
    }

    # VV: flat EQ, no mod
    vv_config = {
        "_reverbmode": vv_mode,
        "_colormode": 0.667,
        "_decay": vv_decay,
        "_size": 0.5,
        "_predelay": 0.0,
        "_diffusion_early": 0.7,
        "_diffusion_late": 0.7,
        "_mod_rate": 0.0,
        "_mod_depth": 0.0,
        "_high_cut": 1.0,
        "_low_cut": 0.0,
        "_bassmult": 0.5,
        "_bassxover": 0.4,
        "_highshelf": 0.0,
        "_highfreq": 0.5,
        "_attack": 0.5,
    }

    impulse = make_impulse(sig_dur)

    # Capture VV
    apply_reference_params(vv, vv_config)
    flush_plugin(vv, sr, flush_dur)
    vv_l, vv_r = process_stereo(vv, impulse, sr)
    flush_plugin(vv, sr, flush_dur)

    # Measure VV RT60
    vv_rt60 = metrics.measure_rt60_per_band(vv_l, sr, {"500 Hz": 500})
    vv_rt60_500 = vv_rt60.get("500 Hz")

    # Calibrate DV
    cal_decay, cal_rt60 = None, None
    if vv_rt60_500 and vv_rt60_500 > 0:
        cal_decay, cal_rt60 = calibrate_dv_decay(
            dv, vv_rt60_500, dv_params, sr, sig_dur)
        if cal_decay is not None:
            dv_params["decay_time"] = cal_decay

    # Capture DV
    apply_duskverb_params(dv, dv_params)
    flush_plugin(dv, sr, flush_dur)
    dv_l, dv_r = process_stereo(dv, impulse, sr)
    flush_plugin(dv, sr, flush_dur)

    # Save
    save_ir(dv_path, dv_l, dv_r, sr)
    save_ir(vv_path, vv_l, vv_r, sr)

    return {
        "algorithm": algo,
        "vv_decay": vv_decay,
        "dv_path": os.path.relpath(dv_path, output_dir),
        "vv_path": os.path.relpath(vv_path, output_dir),
        "dv_peak_dbfs": round(peak_dbfs(dv_l), 1),
        "vv_peak_dbfs": round(peak_dbfs(vv_l), 1),
        "dv_duration_to_60db": round(duration_to_60db(dv_l, sr), 2),
        "vv_duration_to_60db": round(duration_to_60db(vv_l, sr), 2),
        "calibrated_decay": round(cal_decay, 3) if cal_decay else None,
        "calibrated_rt60": round(cal_rt60, 3) if cal_rt60 else None,
        "target_rt60": round(vv_rt60_500, 3) if vv_rt60_500 else None,
    }


# ---------------------------------------------------------------------------
# Parallel worker support
# ---------------------------------------------------------------------------
_worker_dv = None
_worker_vv = None
_worker_outdir = None


def _init_worker(dv_path, vv_path, outdir):
    global _worker_dv, _worker_vv, _worker_outdir
    _worker_dv = load_plugin(dv_path)
    _worker_vv = load_plugin(vv_path)
    _worker_outdir = outdir


def _worker_capture_preset(preset_info):
    return capture_preset(_worker_dv, _worker_vv, preset_info, SR, _worker_outdir)


def _worker_capture_reference(args):
    algo, vv_decay = args
    return capture_reference(_worker_dv, _worker_vv, algo, vv_decay, SR, _worker_outdir)


# ---------------------------------------------------------------------------
# Sanity checks
# ---------------------------------------------------------------------------
def sanity_check(log, output_dir):
    """Print sanity check summary. Returns True if all good."""
    issues = []

    # Count WAVs
    preset_count = len(log.get("presets", []))
    ref_count = len(log.get("reference", []))
    expected_preset_wavs = preset_count * 2
    expected_ref_wavs = ref_count * 2

    actual_preset_wavs = 0
    actual_ref_wavs = 0
    preset_dir = os.path.join(output_dir, "presets")
    ref_dir = os.path.join(output_dir, "reference")
    if os.path.isdir(preset_dir):
        actual_preset_wavs = len([f for f in os.listdir(preset_dir) if f.endswith(".wav")])
    if os.path.isdir(ref_dir):
        actual_ref_wavs = len([f for f in os.listdir(ref_dir) if f.endswith(".wav")])

    print(f"\n{'='*70}")
    print("SANITY CHECK")
    print(f"{'='*70}")
    print(f"  Preset WAVs:    {actual_preset_wavs}/{expected_preset_wavs} "
          f"({'OK' if actual_preset_wavs == expected_preset_wavs else 'MISMATCH'})")
    print(f"  Reference WAVs: {actual_ref_wavs}/{expected_ref_wavs} "
          f"({'OK' if actual_ref_wavs == expected_ref_wavs else 'MISMATCH'})")

    # Check for low peak levels
    low_peak = []
    for entry in log.get("presets", []) + log.get("reference", []):
        name = entry.get("name", entry.get("algorithm", "?"))
        for key in ("dv_peak_dbfs", "vv_peak_dbfs"):
            val = entry.get(key, 0)
            if val < -40.0:
                plugin = "DV" if "dv" in key else "VV"
                low_peak.append(f"{name} ({plugin}): {val:.1f} dBFS")

    if low_peak:
        print(f"\n  LOW PEAK LEVEL (<-40 dBFS) — possible routing/flush issue:")
        for msg in low_peak:
            print(f"    {msg}")
        issues.extend(low_peak)
    else:
        print(f"  Peak levels:    All above -40 dBFS — OK")

    # Check for zero duration
    zero_dur = []
    for entry in log.get("presets", []) + log.get("reference", []):
        name = entry.get("name", entry.get("algorithm", "?"))
        for key in ("dv_duration_to_60db", "vv_duration_to_60db"):
            val = entry.get(key, 0)
            if val == 0:
                plugin = "DV" if "dv" in key else "VV"
                zero_dur.append(f"{name} ({plugin})")

    if zero_dur:
        print(f"\n  ZERO DURATION — capture likely failed:")
        for msg in zero_dur:
            print(f"    {msg}")
        issues.extend(zero_dur)
    else:
        print(f"  Duration check:  All have measurable -60dB duration — OK")

    if issues:
        print(f"\n  {len(issues)} issue(s) found. Fix before proceeding to analysis.")
        return False
    else:
        print(f"\n  All checks passed.")
        return True


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="IR Capture Pipeline")
    parser.add_argument("--presets-only", action="store_true",
                        help="Skip reference IR capture")
    parser.add_argument("--reference-only", action="store_true",
                        help="Skip preset IR capture")
    parser.add_argument("--preset", type=str,
                        help="Capture a single preset by name")
    parser.add_argument("-j", "--jobs", type=int, default=0,
                        help="Number of workers (0=auto)")
    parser.add_argument("--serial", action="store_true",
                        help="Single-threaded mode")
    parser.add_argument("--outdir", type=str,
                        default=os.path.join(os.path.dirname(__file__), "irs"),
                        help="Output directory")
    args = parser.parse_args()

    output_dir = args.outdir
    os.makedirs(os.path.join(output_dir, "presets"), exist_ok=True)
    os.makedirs(os.path.join(output_dir, "reference"), exist_ok=True)

    dv_path = find_plugin(DUSKVERB_PATHS)
    vv_path = find_plugin(REFERENCE_REVERB_PATHS)
    if not dv_path or not vv_path:
        print("ERROR: Need both DuskVerb and ReferenceReverb installed.")
        return

    print(f"DuskVerb:     {dv_path}")
    print(f"ReferenceReverb:  {vv_path}")
    print(f"Output:       {output_dir}")

    n_workers = args.jobs if args.jobs > 0 else min(multiprocessing.cpu_count(), 8)
    use_parallel = not args.serial

    capture_log = {
        "sample_rate": SR,
        "capture_date": datetime.date.today().isoformat(),
        "presets": [],
        "reference": [],
    }

    # --- Preset capture ---
    if not args.reference_only:
        presets = load_qualifying_presets()
        if args.preset:
            presets = [p for p in presets
                       if args.preset.lower() in p["name"].lower()]

        if not presets:
            print("No matching presets found.")
        else:
            print(f"\nCapturing {len(presets)} preset IR pairs...")

            if use_parallel and len(presets) > 1:
                print(f"  Using {n_workers} workers")
                with multiprocessing.Pool(
                    processes=n_workers,
                    initializer=_init_worker,
                    initargs=(dv_path, vv_path, output_dir),
                ) as pool:
                    for result in pool.imap_unordered(
                            _worker_capture_preset, presets):
                        capture_log["presets"].append(result)
                        n = len(capture_log["presets"])
                        sys.stdout.write(
                            f"\r  Captured {n}/{len(presets)}: "
                            f"{result['name']:<35s}")
                        sys.stdout.flush()
                print()
            else:
                dv = load_plugin(dv_path)
                vv = load_plugin(vv_path)
                for i, preset in enumerate(presets):
                    result = capture_preset(dv, vv, preset, SR, output_dir)
                    capture_log["presets"].append(result)
                    print(f"  [{i+1}/{len(presets)}] {result['name']}")

    # --- Reference capture ---
    if not args.presets_only:
        ref_tasks = [(algo, decay)
                     for algo in REFERENCE_ALGOS
                     for decay in REFERENCE_DECAYS]

        print(f"\nCapturing {len(ref_tasks)} reference IR pairs...")

        if use_parallel and len(ref_tasks) > 1:
            print(f"  Using {n_workers} workers")
            with multiprocessing.Pool(
                processes=n_workers,
                initializer=_init_worker,
                initargs=(dv_path, vv_path, output_dir),
            ) as pool:
                for result in pool.imap_unordered(
                        _worker_capture_reference, ref_tasks):
                    capture_log["reference"].append(result)
                    n = len(capture_log["reference"])
                    sys.stdout.write(
                        f"\r  Captured {n}/{len(ref_tasks)}: "
                        f"{result['algorithm']} decay={result['vv_decay']:.2f}")
                    sys.stdout.flush()
            print()
        else:
            dv = load_plugin(dv_path)
            vv = load_plugin(vv_path)
            for i, (algo, decay) in enumerate(ref_tasks):
                result = capture_reference(dv, vv, algo, decay, SR, output_dir)
                capture_log["reference"].append(result)
                print(f"  [{i+1}/{len(ref_tasks)}] {algo} decay={decay:.2f}")

    # Sort results for deterministic output
    capture_log["presets"].sort(key=lambda r: r["name"])
    capture_log["reference"].sort(
        key=lambda r: (r["algorithm"], r["vv_decay"]))

    # Save log
    log_path = os.path.join(output_dir, "capture_log.json")
    with open(log_path, "w") as f:
        json.dump(capture_log, f, indent=2, default=lambda o: float(o) if isinstance(o, np.floating) else int(o) if isinstance(o, np.integer) else str(o))
    print(f"\nCapture log saved to: {log_path}")

    # Sanity check
    sanity_check(capture_log, output_dir)

    # Print capture summary
    print(f"\n{'='*70}")
    print("CAPTURE SUMMARY")
    print(f"{'='*70}")
    for entry in capture_log["presets"]:
        gate = " [GATE]" if entry.get("is_gate") else ""
        print(f"  {entry['name']:<35s} DV:{entry['dv_peak_dbfs']:+5.1f}dBFS  "
              f"VV:{entry['vv_peak_dbfs']:+5.1f}dBFS  "
              f"dur:{entry['dv_duration_to_60db']:.1f}s{gate}")

    print(f"\nDone.")


if __name__ == "__main__":
    main()
