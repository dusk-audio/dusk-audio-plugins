#!/usr/bin/env python3
"""
DuskVerb vs Reference ReferenceReverb comparison tool.

Loads both plugins via pedalboard, processes identical test signals through
each, runs comprehensive reverb analysis metrics, generates visual reports,
and prints actionable tuning recommendations.

Usage:
    python3 compare_reverbs.py                     # All mode pairings
    python3 compare_reverbs.py --mode Room         # Single mode
    python3 compare_reverbs.py --mode Room --save  # Save WAVs for listening
    python3 compare_reverbs.py --duskverb-only     # Analyze DuskVerb alone
    python3 compare_reverbs.py --list-params       # Discover parameter names
"""

import argparse
import json
import os
import sys
import time

import numpy as np
import soundfile as sf
from pedalboard import load_plugin

from config import (
    SAMPLE_RATE, SIGNAL_DURATION,
    DUSKVERB_PATHS, REFERENCE_REVERB_PATHS, REFERENCE_ROOM_PATHS,
    MODE_PAIRINGS, REFERENCE_PARAM_MAP, REFERENCE_ROOM_PARAM_MAP,
    TUNING_MAP,
    find_plugin, discover_params,
    apply_duskverb_params, apply_reference_params, apply_reference_room_params,
)
from generate_test_signals import (
    make_impulse, make_log_sweep, make_noise_burst,
    make_snare_transient, make_tone_burst, make_multitone_burst,
)
import reverb_metrics as metrics

OUTPUT_DIR = os.path.join(os.path.dirname(__file__), "output")


# ---------------------------------------------------------------------------
# Plugin processing
# ---------------------------------------------------------------------------
def process_stereo(plugin, mono_signal, sr):
    """Process mono signal through plugin, return (left, right)."""
    stereo_in = np.stack([mono_signal, mono_signal], axis=0).astype(np.float32)
    output = plugin(stereo_in, sr)
    return output[0], output[1]


def flush_plugin(plugin, sr, duration_sec=2.0):
    """Process silence through plugin to flush internal state."""
    silence = np.zeros(int(sr * duration_sec), dtype=np.float32)
    process_stereo(plugin, silence, sr)


def measure_ref_rt60(plugin, reference_config, decay_val, sr, apply_fn=None):
    """Set reference plugin decay and measure RT60, flushing state first."""
    if apply_fn is None:
        apply_fn = apply_reference_params
    trial_config = dict(reference_config)
    trial_config["_decay"] = decay_val
    apply_fn(plugin, trial_config)

    # Flush internal state
    flush_plugin(plugin, sr)

    # Process impulse
    impulse = make_impulse()
    out_l, _ = process_stereo(plugin, impulse, sr)

    # Measure broadband RT60 (500 Hz band — most reliable)
    rt60 = metrics.measure_rt60_per_band(out_l, sr, {"500 Hz": 500}).get("500 Hz")
    return rt60


def calibrate_decay(plugin, target_rt60, reference_config, sr, max_iter=14, tolerance=0.10,
                    apply_fn=None):
    """Binary search a reference plugin's _decay parameter to match a target RT60.

    First sweeps to understand the decay mapping direction, then binary searches.

    Args:
        plugin: loaded reference plugin (ReferenceReverb or ReferenceRoom)
        target_rt60: desired RT60 in seconds (from DuskVerb config)
        reference_config: dict of VH params (mode, color, etc.)
        sr: sample rate
        max_iter: maximum binary search iterations
        tolerance: acceptable RT60 ratio error (0.10 = ±10%)

    Returns:
        (calibrated_decay, measured_rt60) or (None, None) if calibration fails
    """
    # First: probe 3 points to understand the mapping
    probe_points = [0.1, 0.5, 0.9]
    probe_rt60s = {}
    print(f"    Probing decay mapping...")
    for val in probe_points:
        rt60 = measure_ref_rt60(plugin, reference_config, val, sr, apply_fn=apply_fn)
        probe_rt60s[val] = rt60
        rt60_str = f"{rt60:.2f}s" if rt60 else "N/A"
        print(f"      decay={val:.1f} -> RT60={rt60_str}")

    # Determine if relationship is normal (higher decay = longer RT60)
    # or inverted (higher decay = shorter RT60)
    rt60_low = probe_rt60s.get(0.1)
    rt60_high = probe_rt60s.get(0.9)
    if rt60_low is None and rt60_high is None:
        print(f"    Cannot determine mapping — all measurements failed")
        return None, None

    inverted = False
    if rt60_low is not None and rt60_high is not None:
        inverted = rt60_low > rt60_high
        direction = "inverted (higher decay = shorter RT60)" if inverted else "normal"
        print(f"    Mapping direction: {direction}")

    # Check if target is achievable
    measurable_rt60s = [v for v in probe_rt60s.values() if v is not None and v > 0]
    if measurable_rt60s:
        rt60_min = min(measurable_rt60s)
        rt60_max = max(measurable_rt60s)
        if target_rt60 < rt60_min * 0.5 or target_rt60 > rt60_max * 2.0:
            print(f"    WARNING: Target RT60={target_rt60:.1f}s may be outside VH range "
                  f"[{rt60_min:.1f}s, {rt60_max:.1f}s]")

    # Binary search
    lo, hi = 0.0, 1.0
    best_decay = None
    best_rt60 = None
    best_error = float('inf')

    for iteration in range(max_iter):
        mid = (lo + hi) / 2.0
        rt60 = measure_ref_rt60(plugin, reference_config, mid, sr, apply_fn=apply_fn)

        if rt60 is None or rt60 <= 0:
            # Measurement failed — try the other direction
            if inverted:
                lo = mid
            else:
                hi = mid
            continue

        error = abs(rt60 / target_rt60 - 1.0)
        if error < best_error:
            best_error = error
            best_decay = mid
            best_rt60 = rt60

        print(f"    iter {iteration+1}: decay={mid:.4f} -> RT60={rt60:.2f}s "
              f"(target={target_rt60:.2f}s, error={error*100:.1f}%)")

        if error < tolerance:
            return mid, rt60

        if inverted:
            # Inverted: higher decay = shorter RT60
            if rt60 > target_rt60:
                lo = mid  # Need shorter RT60 → increase decay
            else:
                hi = mid  # Need longer RT60 → decrease decay
        else:
            # Normal: higher decay = longer RT60
            if rt60 > target_rt60:
                hi = mid  # Need shorter RT60 → decrease decay
            else:
                lo = mid  # Need longer RT60 → increase decay

    return best_decay, best_rt60


def generate_test_signals():
    """Generate all test signals for analysis."""
    signals = {
        "impulse": make_impulse(),
        "noise_burst": make_noise_burst(),
        "snare": make_snare_transient(),
        "tone_1500hz": make_tone_burst(1500),
        "multitone": make_multitone_burst(),
    }

    # Log sweep (special: also returns inverse filter)
    sweep, inverse = make_log_sweep()
    signals["log_sweep"] = sweep
    signals["_log_sweep_inverse"] = inverse  # prefix _ = not processed, just stored

    return signals


# ---------------------------------------------------------------------------
# Analysis pipeline
# ---------------------------------------------------------------------------
def analyze_plugin_outputs(outputs, sr):
    """Run all metrics on processed plugin outputs.

    Args:
        outputs: dict of signal_name -> (left, right)
        sr: sample rate

    Returns:
        dict of all analysis results
    """
    results = {}

    # Primary analysis on impulse response
    if "impulse" in outputs:
        ir_l, ir_r = outputs["impulse"]
        results["impulse"] = metrics.analyze_ir(ir_l, ir_r, sr)

    # Snare response (modal excitation)
    if "snare" in outputs:
        snare_l, _ = outputs["snare"]
        results["snare_ringing"] = metrics.detect_modal_resonances(snare_l, sr)
        results["snare_smoothness"] = metrics.tail_smoothness(snare_l, sr)

    # 1500 Hz tone burst (targeted ringing probe)
    if "tone_1500hz" in outputs:
        tone_l, _ = outputs["tone_1500hz"]
        results["tone_1500hz_ringing"] = metrics.detect_modal_resonances(
            tone_l, sr, time_windows=[(0.05, 0.15), (0.15, 0.4), (0.4, 1.0)]
        )
        results["tone_1500hz_decay"] = metrics.spectral_decay_rates(tone_l, sr)

    # Multitone burst (frequency-selective ringing)
    if "multitone" in outputs:
        multi_l, _ = outputs["multitone"]
        results["multitone_ringing"] = metrics.detect_modal_resonances(
            multi_l, sr, time_windows=[(0.05, 0.15), (0.15, 0.4), (0.4, 1.0)]
        )

    # Noise burst (broadband character)
    if "noise_burst" in outputs:
        noise_l, _ = outputs["noise_burst"]
        results["noise_decay_rates"] = metrics.spectral_decay_rates(noise_l, sr)
        results["noise_smoothness"] = metrics.tail_smoothness(noise_l, sr)

    # Deconvolved IR from log sweep (high-SNR)
    if "log_sweep" in outputs and "_log_sweep_inverse" in outputs:
        sweep_l, sweep_r = outputs["log_sweep"]
        inverse = outputs["_log_sweep_inverse"]
        # If inverse is a tuple (from process_stereo), take left channel
        if isinstance(inverse, tuple):
            inverse = inverse[0]
        deconv_l = np.convolve(sweep_l, inverse, mode='full')[:len(sweep_l)]
        deconv_r = np.convolve(sweep_r, inverse, mode='full')[:len(sweep_r)]
        # Normalize
        peak = max(np.max(np.abs(deconv_l)), 1e-10)
        deconv_l = (deconv_l / peak).astype(np.float32)
        deconv_r = (deconv_r / peak).astype(np.float32)
        results["deconv_ir"] = metrics.analyze_ir(deconv_l, deconv_r, sr)

    return results


# ---------------------------------------------------------------------------
# Text report
# ---------------------------------------------------------------------------
def print_comparison_header(mode_name):
    print(f"\n{'='*78}")
    print(f"  MODE: {mode_name}")
    print(f"{'='*78}")


def print_rt60_comparison(dv_rt60, vh_rt60=None, ref_name="ReferenceReverb"):
    """Print RT60 per band comparison table."""
    print(f"\n  --- RT60 Per Octave Band ---")
    if vh_rt60:
        print(f"  {'Band':>10s}  {'DuskVerb':>10s}  {ref_name:>12s}  {'Delta':>8s}  {'Status':>6s}")
        print(f"  {'-'*10}  {'-'*10}  {'-'*12}  {'-'*8}  {'-'*6}")
        for band in metrics.OCTAVE_BANDS:
            dv = dv_rt60.get(band)
            vh = vh_rt60.get(band)
            dv_str = f"{dv:.3f}s" if dv else "N/A"
            vh_str = f"{vh:.3f}s" if vh else "N/A"
            if dv and vh:
                delta = dv - vh
                ratio = dv / vh if vh > 0 else 0
                status = "OK" if 0.7 < ratio < 1.4 else "WARN" if 0.5 < ratio < 2.0 else "FAIL"
                print(f"  {band:>10s}  {dv_str:>10s}  {vh_str:>12s}  {delta:>+7.3f}s  {status:>6s}")
            else:
                print(f"  {band:>10s}  {dv_str:>10s}  {vh_str:>12s}  {'':>8s}  {'':>6s}")
    else:
        print(f"  {'Band':>10s}  {'RT60':>10s}")
        print(f"  {'-'*10}  {'-'*10}")
        for band in metrics.OCTAVE_BANDS:
            dv = dv_rt60.get(band)
            print(f"  {band:>10s}  {dv:.3f}s" if dv else f"  {band:>10s}  N/A")


def print_ringing_analysis(dv_res, vh_res=None, label="Impulse", ref_name="ReferenceReverb"):
    """Print modal resonance analysis."""
    print(f"\n  --- Modal Ringing ({label}) ---")
    print(f"  DuskVerb:     {dv_res['max_peak_prominence_db']:.1f} dB peak at "
          f"{dv_res['worst_freq_hz']:.0f} Hz")
    if vh_res:
        print(f"  {ref_name}:  {vh_res['max_peak_prominence_db']:.1f} dB peak at "
              f"{vh_res['worst_freq_hz']:.0f} Hz")

    if dv_res['persistent_peaks']:
        print(f"\n  Persistent resonances (DuskVerb):")
        for freq, prom, count in dv_res['persistent_peaks'][:5]:
            windows = f"{count}/{len(dv_res['per_window'])}"
            print(f"    {freq:7.0f} Hz: +{prom:.1f} dB (seen in {windows} windows)")


def print_decay_comparison(dv_rates, vh_rates=None, ref_name="ReferenceReverb"):
    """Print frequency-dependent decay rates."""
    print(f"\n  --- Decay Rates (dB/sec per band) ---")
    if vh_rates:
        print(f"  {'Band':>10s}  {'DuskVerb':>10s}  {ref_name:>12s}  {'Diff':>8s}")
        print(f"  {'-'*10}  {'-'*10}  {'-'*12}  {'-'*8}")
        for band in metrics.OCTAVE_BANDS:
            dv = dv_rates.get(band, 0)
            vh = vh_rates.get(band, 0)
            print(f"  {band:>10s}  {dv:>9.1f}  {vh:>11.1f}  {dv-vh:>+7.1f}")
    else:
        print(f"  {'Band':>10s}  {'Rate (dB/s)':>12s}")
        print(f"  {'-'*10}  {'-'*12}")
        for band in metrics.OCTAVE_BANDS:
            dv = dv_rates.get(band, 0)
            print(f"  {band:>10s}  {dv:>11.1f}")


def print_freq_response(dv_fr, vh_fr=None, ref_name="ReferenceReverb"):
    """Print frequency response comparison."""
    print(f"\n  --- Frequency Response ---")
    if vh_fr:
        print(f"  {'Band':>20s}  {'DuskVerb':>10s}  {ref_name:>12s}  {'Diff':>8s}")
        print(f"  {'-'*20}  {'-'*10}  {'-'*12}  {'-'*8}")
        for band in dv_fr:
            dv = dv_fr[band]
            vh = vh_fr.get(band, -100)
            print(f"  {band:>20s}  {dv:>9.1f}  {vh:>11.1f}  {dv-vh:>+7.1f}")
    else:
        print(f"  {'Band':>20s}  {'Level (dB)':>12s}")
        print(f"  {'-'*20}  {'-'*12}")
        for band in dv_fr:
            print(f"  {band:>20s}  {dv_fr[band]:>11.1f}")


def print_stereo_analysis(dv_stereo, vh_stereo=None, ref_name="ReferenceReverb"):
    """Print stereo decorrelation summary."""
    print(f"\n  --- Stereo Decorrelation ---")
    times_dv, corr_dv = dv_stereo
    if len(corr_dv) > 0:
        avg_corr_dv = float(np.mean(corr_dv[len(corr_dv)//4:]))  # Average in tail
        print(f"  DuskVerb avg tail correlation: {avg_corr_dv:.3f} "
              f"({'good' if avg_corr_dv < 0.3 else 'fair' if avg_corr_dv < 0.5 else 'poor'})")
    if vh_stereo:
        times_vh, corr_vh = vh_stereo
        if len(corr_vh) > 0:
            avg_corr_vh = float(np.mean(corr_vh[len(corr_vh)//4:]))
            print(f"  {ref_name} avg tail correlation: {avg_corr_vh:.3f} "
                  f"({'good' if avg_corr_vh < 0.3 else 'fair' if avg_corr_vh < 0.5 else 'poor'})")


def print_tuning_recommendations(mode_name, dv_results, vh_results=None):
    """Print actionable DSP tuning suggestions based on metric deltas."""
    print(f"\n  --- TUNING RECOMMENDATIONS for {mode_name} ---")

    recommendations = []

    if vh_results is None:
        print("  (No ReferenceReverb reference — showing DuskVerb self-analysis only)")
        # Still check for obvious issues
        dv_rt60 = dv_results.get("impulse", {}).get("rt60", {})
        if dv_rt60:
            hf_bands = ["4 kHz", "8 kHz"]
            lf_bands = ["125 Hz", "250 Hz"]
            hf_rt60 = [dv_rt60[b] for b in hf_bands if dv_rt60.get(b)]
            lf_rt60 = [dv_rt60[b] for b in lf_bands if dv_rt60.get(b)]
            if hf_rt60 and lf_rt60:
                ratio = np.mean(hf_rt60) / np.mean(lf_rt60) if np.mean(lf_rt60) > 0 else 0
                if ratio < 0.4:
                    recommendations.append(("rt60_hf_too_short",
                        f"HF/LF RT60 ratio = {ratio:.2f} (very dark, target ~0.5-0.7)"))

        ringing = dv_results.get("impulse", {}).get("resonances", {})
        if ringing.get("max_peak_prominence_db", 0) > 8:
            recommendations.append(("modal_ringing",
                f"Ringing at {ringing['worst_freq_hz']:.0f} Hz: "
                f"+{ringing['max_peak_prominence_db']:.1f} dB (target: <8 dB)"))
    else:
        # Compare RT60 per band
        dv_rt60 = dv_results.get("impulse", {}).get("rt60", {})
        vh_rt60 = vh_results.get("impulse", {}).get("rt60", {})
        if dv_rt60 and vh_rt60:
            for band in ["2 kHz", "4 kHz", "8 kHz"]:
                dv_val = dv_rt60.get(band)
                vh_val = vh_rt60.get(band)
                if dv_val and vh_val and dv_val < vh_val * 0.7:
                    recommendations.append(("rt60_hf_too_short",
                        f"{band}: DV RT60={dv_val:.3f}s vs VH={vh_val:.3f}s "
                        f"({(1 - dv_val/vh_val)*100:.0f}% shorter)"))

            for band in ["125 Hz", "250 Hz"]:
                dv_val = dv_rt60.get(band)
                vh_val = vh_rt60.get(band)
                if dv_val and vh_val and dv_val > vh_val * 1.4:
                    recommendations.append(("rt60_lf_too_long",
                        f"{band}: DV RT60={dv_val:.3f}s vs VH={vh_val:.3f}s "
                        f"({(dv_val/vh_val - 1)*100:.0f}% longer)"))

        # Compare ringing
        dv_ring = dv_results.get("impulse", {}).get("resonances", {})
        vh_ring = vh_results.get("impulse", {}).get("resonances", {})
        if dv_ring and vh_ring:
            dv_prom = dv_ring.get("max_peak_prominence_db", 0)
            vh_prom = vh_ring.get("max_peak_prominence_db", 0)
            if dv_prom > vh_prom + 3:
                recommendations.append(("modal_ringing",
                    f"Ringing: DV={dv_prom:.1f}dB vs VH={vh_prom:.1f}dB "
                    f"(+{dv_prom - vh_prom:.1f}dB worse)"))

        # Compare echo density
        dv_dens = dv_results.get("impulse", {}).get("echo_density", ([], []))
        vh_dens = vh_results.get("impulse", {}).get("echo_density", ([], []))
        _, dv_d = dv_dens
        _, vh_d = vh_dens
        if len(dv_d) > 0 and len(vh_d) > 0:
            dv_avg = np.mean(dv_d)
            vh_avg = np.mean(vh_d)
            if dv_avg < vh_avg * 0.7:
                recommendations.append(("low_echo_density",
                    f"Density: DV={dv_avg:.0f}/s vs VH={vh_avg:.0f}/s"))

        # Compare tail smoothness
        dv_smooth = dv_results.get("impulse", {}).get("smoothness", {})
        vh_smooth = vh_results.get("impulse", {}).get("smoothness", {})
        if dv_smooth and vh_smooth:
            dv_std = dv_smooth.get("envelope_std_db", 0)
            vh_std = vh_smooth.get("envelope_std_db", 0)
            if dv_std > vh_std * 1.5:
                recommendations.append(("tail_roughness",
                    f"Tail roughness: DV={dv_std:.2f}dB vs VH={vh_std:.2f}dB"))

    if not recommendations:
        print("  No major issues detected!")
        return

    seen_categories = set()
    for category, finding in recommendations:
        print(f"\n  FINDING: {finding}")
        if category not in seen_categories:
            seen_categories.add(category)
            for fix in TUNING_MAP.get(category, []):
                print(f"    -> {fix}")


# ---------------------------------------------------------------------------
# WAV output
# ---------------------------------------------------------------------------
def save_outputs(outputs, mode_name, plugin_name, output_dir):
    """Save processed audio outputs as WAV files."""
    mode_dir = os.path.join(output_dir, mode_name.lower())
    os.makedirs(mode_dir, exist_ok=True)

    for sig_name, (left, right) in outputs.items():
        if sig_name.startswith('_'):
            continue
        filename = f"{plugin_name}_{sig_name}.wav"
        stereo = np.stack([left, right], axis=-1)
        sf.write(os.path.join(mode_dir, filename), stereo, SAMPLE_RATE)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(
        description="DuskVerb vs Reference ReferenceReverb comparison")
    parser.add_argument("--mode", type=str, default=None,
                        help="Compare specific mode (Room/Hall/Plate/Chamber/Ambient)")
    parser.add_argument("--duskverb-only", action="store_true",
                        help="Analyze DuskVerb alone (no ReferenceReverb needed)")
    parser.add_argument("--list-params", action="store_true",
                        help="Print all accessible parameters for both plugins")
    parser.add_argument("--save", action="store_true",
                        help="Save processed WAV files for listening")
    parser.add_argument("--plot", action="store_true",
                        help="Generate visual report PNGs")
    parser.add_argument("--json", action="store_true",
                        help="Save results as JSON")
    parser.add_argument("--calibrate", action="store_true",
                        help="Calibrate ReferenceReverb decay to match DuskVerb RT60")
    args = parser.parse_args()

    # --- Load plugins ---
    dv_path = find_plugin(DUSKVERB_PATHS)
    if not dv_path:
        print("ERROR: DuskVerb not found. Build it first:")
        print("  cd build && cmake --build . --target DuskVerb_AU -j8")
        sys.exit(1)

    print(f"Loading DuskVerb: {dv_path}")
    duskverb = load_plugin(dv_path)
    print(f"  Loaded: {duskverb}")

    reference_reverb = None
    reference_room = None
    if not args.duskverb_only:
        vh_path = find_plugin(REFERENCE_REVERB_PATHS)
        if vh_path:
            print(f"Loading ReferenceReverb: {vh_path}")
            reference_reverb = load_plugin(vh_path)
            print(f"  Loaded: {reference_reverb}")
        else:
            print("WARNING: ReferenceReverb not found.")

        vr_path = find_plugin(REFERENCE_ROOM_PATHS)
        if vr_path:
            print(f"Loading ReferenceRoom: {vr_path}")
            reference_room = load_plugin(vr_path)
            print(f"  Loaded: {reference_room}")
        else:
            print("WARNING: ReferenceRoom not found.")

        if not reference_reverb and not reference_room:
            print("No reference plugins found. Running DuskVerb-only analysis.")

    # --- List params mode ---
    if args.list_params:
        print(f"\n{'='*60}")
        print("DuskVerb parameters:")
        print(f"{'='*60}")
        for name, val in discover_params(duskverb):
            print(f"  {name:30s} = {val}")

        if reference_reverb:
            print(f"\n{'='*60}")
            print("ReferenceReverb parameters:")
            print(f"{'='*60}")
            for name, val in discover_params(reference_reverb):
                print(f"  {name:30s} = {val}")

        if reference_room:
            print(f"\n{'='*60}")
            print("ReferenceRoom parameters:")
            print(f"{'='*60}")
            for name, val in discover_params(reference_room):
                print(f"  {name:30s} = {val}")

        if not reference_reverb and not reference_room:
            print("\nNo reference plugins loaded (install them or check paths in config.py)")

        return

    # --- Generate test signals ---
    print("\nGenerating test signals...")
    signals = generate_test_signals()
    processable = {k: v for k, v in signals.items() if not k.startswith('_')}
    print(f"  {len(processable)} signals ready")

    # --- Select mode pairings ---
    pairings = MODE_PAIRINGS
    if args.mode:
        pairings = [p for p in pairings if p["name"].lower() == args.mode.lower()]
        if not pairings:
            valid = [p["name"] for p in MODE_PAIRINGS]
            print(f"ERROR: Unknown mode '{args.mode}'. Choose from: {valid}")
            sys.exit(1)

    # --- Process and analyze ---
    all_results = {}

    for pairing in pairings:
        mode_name = pairing["name"]
        print_comparison_header(mode_name)

        # Configure and process DuskVerb
        print(f"\n  Processing DuskVerb ({mode_name})...")
        apply_duskverb_params(duskverb, pairing["duskverb"])

        dv_outputs = {}
        for sig_name, signal in processable.items():
            dv_outputs[sig_name] = process_stereo(duskverb, signal, SAMPLE_RATE)

        # Store inverse filter for deconvolution (not processed)
        if "_log_sweep_inverse" in signals:
            dv_outputs["_log_sweep_inverse"] = (signals["_log_sweep_inverse"],
                                                 signals["_log_sweep_inverse"])

        # Determine which reference plugin to use for this mode
        ref_type = pairing.get("reference", "reference_reverb")
        if ref_type == "reference_room" and reference_room:
            ref_plugin = reference_room
            ref_config = pairing.get("reference_room", {})
            ref_name = "ReferenceRoom"
            ref_apply = apply_reference_room_params
            ref_param_map = REFERENCE_ROOM_PARAM_MAP
        elif reference_reverb:
            ref_plugin = reference_reverb
            ref_config = pairing.get("reference", {})
            ref_name = "ReferenceReverb"
            ref_apply = apply_reference_params
            ref_param_map = REFERENCE_PARAM_MAP
        else:
            ref_plugin = None
            ref_config = {}
            ref_name = None
            ref_apply = None
            ref_param_map = {}

        # Configure and process reference plugin
        vh_results = None
        vh_outputs = {}
        if ref_plugin and not args.duskverb_only:
            if not ref_param_map:
                print(f"\n  WARNING: {ref_name} param map is empty.")
                print(f"  Skipping {ref_name} processing.")
            else:
                # Calibrate: measure DV's actual RT60, then binary-search ref's
                # _decay to match it. Auto-calibrate if pairing says so.
                should_calibrate = args.calibrate or pairing.get("auto_calibrate", False)
                if should_calibrate:
                    dv_ir_l, _ = dv_outputs.get("impulse", (None, None))
                    dv_rt60_500 = None
                    if dv_ir_l is not None:
                        dv_rt60_map = metrics.measure_rt60_per_band(
                            dv_ir_l, SAMPLE_RATE, {"500 Hz": 500})
                        dv_rt60_500 = dv_rt60_map.get("500 Hz")

                    if dv_rt60_500 and dv_rt60_500 > 0:
                        print(f"\n  Calibrating {ref_name} to match DV RT60={dv_rt60_500:.2f}s...")
                        cal_decay, cal_rt60 = calibrate_decay(
                            ref_plugin, dv_rt60_500,
                            ref_config, SAMPLE_RATE,
                            apply_fn=ref_apply)
                        if cal_decay is not None:
                            ref_config["_decay"] = cal_decay
                            print(f"  {ref_name} _decay calibrated to {cal_decay:.4f} "
                                  f"(RT60={cal_rt60:.2f}s, target={dv_rt60_500:.2f}s)")
                        else:
                            print(f"  {ref_name} calibration failed, using default _decay")
                    else:
                        print(f"\n  DV RT60 measurement failed, using default {ref_name} _decay")

                print(f"\n  Processing {ref_name} ({mode_name})...")
                ref_apply(ref_plugin, ref_config)

                for sig_name, signal in processable.items():
                    vh_outputs[sig_name] = process_stereo(ref_plugin, signal, SAMPLE_RATE)

                if "_log_sweep_inverse" in signals:
                    vh_outputs["_log_sweep_inverse"] = (signals["_log_sweep_inverse"],
                                                         signals["_log_sweep_inverse"])

                # Align impulse responses for fair comparison
                spectral_mse_result = {}
                if "impulse" in dv_outputs and "impulse" in vh_outputs:
                    dv_imp_l, dv_imp_r = dv_outputs["impulse"]
                    vh_imp_l, vh_imp_r = vh_outputs["impulse"]

                    # Time alignment
                    dv_imp_l_a, vh_imp_l_a, offset = metrics.align_ir_pair(
                        dv_imp_l, vh_imp_l, SAMPLE_RATE)
                    if offset != 0:
                        print(f"  Transient alignment: {offset} samples "
                              f"({offset / SAMPLE_RATE * 1000:.1f} ms)")
                        # Also align right channels with same offset
                        if offset > 0:
                            vh_imp_r_a = vh_imp_r[offset:offset + len(dv_imp_l_a)]
                            dv_imp_r_a = dv_imp_r[:len(dv_imp_l_a)]
                        else:
                            dv_imp_r_a = dv_imp_r[-offset:-offset + len(vh_imp_l_a)]
                            vh_imp_r_a = vh_imp_r[:len(vh_imp_l_a)]
                        dv_outputs["impulse"] = (dv_imp_l_a, dv_imp_r_a)
                        vh_outputs["impulse"] = (vh_imp_l_a, vh_imp_r_a)

                    # Compute spectral MSE between aligned, level-normalized IRs
                    dv_rms = np.sqrt(np.mean(dv_imp_l_a.astype(np.float64) ** 2))
                    vh_rms = np.sqrt(np.mean(vh_imp_l_a.astype(np.float64) ** 2))
                    if dv_rms > 1e-10 and vh_rms > 1e-10:
                        norm_dv = (dv_imp_l_a * (1.0 / dv_rms)).astype(np.float32)
                        norm_vh = (vh_imp_l_a * (1.0 / vh_rms)).astype(np.float32)
                        spectral_mse_result = metrics.spectral_mse(
                            norm_dv, norm_vh, SAMPLE_RATE)

                vh_results = analyze_plugin_outputs(vh_outputs, SAMPLE_RATE)
                vh_results["spectral_mse"] = spectral_mse_result

        # Analyze DuskVerb (after potential alignment)
        dv_results = analyze_plugin_outputs(dv_outputs, SAMPLE_RATE)

        ref_label = ref_name or "Reference"

        # --- Print text report ---
        dv_impulse = dv_results.get("impulse", {})
        vh_impulse = vh_results.get("impulse", {}) if vh_results else None

        # RT60
        print_rt60_comparison(
            dv_impulse.get("rt60", {}),
            vh_impulse.get("rt60") if vh_impulse else None,
            ref_name=ref_label,
        )

        # Ringing (impulse)
        print_ringing_analysis(
            dv_impulse.get("resonances", {"max_peak_prominence_db": 0, "worst_freq_hz": 0, "persistent_peaks": [], "per_window": []}),
            vh_impulse.get("resonances") if vh_impulse else None,
            label="Impulse",
            ref_name=ref_label,
        )

        # Ringing (1500 Hz tone burst)
        if "tone_1500hz_ringing" in dv_results:
            vh_tone = vh_results.get("tone_1500hz_ringing") if vh_results else None
            print_ringing_analysis(dv_results["tone_1500hz_ringing"], vh_tone,
                                   label="1500 Hz Tone Burst", ref_name=ref_label)

        # Decay rates
        print_decay_comparison(
            dv_impulse.get("decay_rates", {}),
            vh_impulse.get("decay_rates") if vh_impulse else None,
            ref_name=ref_label,
        )

        # Frequency response
        print_freq_response(
            dv_impulse.get("freq_response", {}),
            vh_impulse.get("freq_response") if vh_impulse else None,
            ref_name=ref_label,
        )

        # Stereo
        print_stereo_analysis(
            dv_impulse.get("stereo", ([], [])),
            vh_impulse.get("stereo") if vh_impulse else None,
            ref_name=ref_label,
        )

        # Tail smoothness
        dv_smooth = dv_impulse.get("smoothness", {})
        vh_smooth = vh_impulse.get("smoothness") if vh_impulse else None
        print(f"\n  --- Tail Smoothness ---")
        print(f"  DuskVerb:     {dv_smooth.get('envelope_std_db', 0):.2f} dB std, "
              f"{dv_smooth.get('decay_rate_db_per_sec', 0):.1f} dB/sec")
        if vh_smooth:
            print(f"  {ref_label}:  {vh_smooth.get('envelope_std_db', 0):.2f} dB std, "
                  f"{vh_smooth.get('decay_rate_db_per_sec', 0):.1f} dB/sec")

        # IACC (stereo width)
        dv_iacc = dv_impulse.get("iacc", ([], []))
        vh_iacc = vh_impulse.get("iacc", ([], [])) if vh_impulse else ([], [])
        print(f"\n  --- IACC (Stereo Width, ISO 3382-1) ---")
        _, dv_iacc_vals = dv_iacc
        if len(dv_iacc_vals) > 0:
            avg_dv_iacc = float(np.mean(dv_iacc_vals[len(dv_iacc_vals)//4:]))
            print(f"  DuskVerb avg tail IACC: {avg_dv_iacc:.3f} "
                  f"({'wide' if avg_dv_iacc < 0.3 else 'medium' if avg_dv_iacc < 0.5 else 'narrow'})")
        _, vh_iacc_vals = vh_iacc
        if len(vh_iacc_vals) > 0:
            avg_vh_iacc = float(np.mean(vh_iacc_vals[len(vh_iacc_vals)//4:]))
            print(f"  {ref_label} avg tail IACC: {avg_vh_iacc:.3f} "
                  f"({'wide' if avg_vh_iacc < 0.3 else 'medium' if avg_vh_iacc < 0.5 else 'narrow'})")

        # Crest factor
        dv_crest = dv_impulse.get("crest_factor", ([], []))
        vh_crest = vh_impulse.get("crest_factor", ([], [])) if vh_impulse else ([], [])
        print(f"\n  --- Crest Factor (Texture Density) ---")
        _, dv_cf_vals = dv_crest
        if len(dv_cf_vals) > 0:
            avg_dv_cf = float(np.mean(dv_cf_vals[len(dv_cf_vals)//4:]))
            print(f"  DuskVerb avg tail crest: {avg_dv_cf:.2f} "
                  f"({'smooth' if avg_dv_cf < 2.0 else 'moderate' if avg_dv_cf < 3.5 else 'grainy'})")
        _, vh_cf_vals = vh_crest
        if len(vh_cf_vals) > 0:
            avg_vh_cf = float(np.mean(vh_cf_vals[len(vh_cf_vals)//4:]))
            print(f"  {ref_label} avg tail crest: {avg_vh_cf:.2f} "
                  f"({'smooth' if avg_vh_cf < 2.0 else 'moderate' if avg_vh_cf < 3.5 else 'grainy'})")

        # Pitch variance
        dv_pv = dv_impulse.get("pitch_variance", {})
        vh_pv = vh_impulse.get("pitch_variance", {}) if vh_impulse else {}
        if dv_pv.get("zcr_variance_ratio", 0) > 0:
            print(f"\n  --- Pitch Variance (Modulation Character) ---")
            print(f"  DuskVerb ZCR: {dv_pv['zcr_mean']:.0f}±{dv_pv['zcr_std']:.0f} Hz "
                  f"(variance ratio: {dv_pv['zcr_variance_ratio']:.3f})")
            if vh_pv.get("zcr_variance_ratio", 0) > 0:
                print(f"  {ref_label} ZCR: {vh_pv['zcr_mean']:.0f}±{vh_pv['zcr_std']:.0f} Hz "
                      f"(variance ratio: {vh_pv['zcr_variance_ratio']:.3f})")

        # Spectral MSE
        if vh_results and "spectral_mse" in vh_results:
            mse = vh_results["spectral_mse"]
            if mse:
                avg_mse = np.mean(list(mse.values()))
                worst_band = max(mse, key=mse.get) if mse else "N/A"
                worst_val = mse.get(worst_band, 0)
                print(f"\n  --- Spectral MSE (1/3-Octave, Level-Normalized) ---")
                print(f"  Average MSE: {avg_mse:.1f} dB²")
                print(f"  Worst band:  {worst_band} ({worst_val:.1f} dB²)")

        # Tuning recommendations
        print_tuning_recommendations(mode_name, dv_results, vh_results)

        all_results[mode_name] = {
            "duskverb": dv_results,
            "reference": vh_results,
            "ref_name": ref_label,
        }

        # Save WAVs
        if args.save:
            save_outputs(dv_outputs, mode_name, "duskverb", OUTPUT_DIR)
            if vh_outputs:
                ref_file_tag = ref_label.lower().replace(" ", "_") if ref_label else "reference"
                save_outputs(vh_outputs, mode_name, ref_file_tag, OUTPUT_DIR)
            print(f"\n  WAVs saved to {OUTPUT_DIR}/{mode_name.lower()}/")

    # --- Visual report ---
    if args.plot:
        try:
            from plot_comparison import generate_report
            generate_report(all_results, OUTPUT_DIR, SAMPLE_RATE)
        except ImportError:
            print("\nWARNING: plot_comparison.py not found, skipping visual report.")

    # --- Summary table ---
    if len(all_results) > 1:
        print(f"\n{'='*78}")
        print(f"  SUMMARY ACROSS ALL MODES")
        print(f"{'='*78}")

        print(f"\n  RT60 Ratio (DuskVerb / Reference) — <1.0 means DuskVerb is darker:")
        print(f"  {'Mode':>10s}", end="")
        for band in metrics.OCTAVE_BANDS:
            print(f"  {band:>8s}", end="")
        print()
        print(f"  {'-'*10}", end="")
        for _ in metrics.OCTAVE_BANDS:
            print(f"  {'-'*8}", end="")
        print()

        for mode_name, data in all_results.items():
            dv_rt60 = data["duskverb"].get("impulse", {}).get("rt60", {})
            vh_rt60 = (data["reference"].get("impulse", {}).get("rt60", {})
                       if data["reference"] else {})
            print(f"  {mode_name:>10s}", end="")
            for band in metrics.OCTAVE_BANDS:
                dv = dv_rt60.get(band)
                vh = vh_rt60.get(band)
                if dv and vh and vh > 0:
                    ratio = dv / vh
                    marker = "*" if ratio < 0.7 else ""
                    print(f"  {ratio:>7.2f}{marker}", end="")
                else:
                    print(f"  {'N/A':>8s}", end="")
            print()

        print(f"\n  (* = DuskVerb >30% shorter decay — needs attention)")

    # --- Save JSON ---
    if args.json:
        json_path = os.path.join(OUTPUT_DIR, "comparison_results.json")
        # Convert non-serializable types
        def make_serializable(obj):
            if isinstance(obj, np.ndarray):
                return obj.tolist()
            if isinstance(obj, (np.float32, np.float64)):
                return float(obj)
            if isinstance(obj, (np.int32, np.int64)):
                return int(obj)
            if isinstance(obj, tuple):
                return [make_serializable(x) for x in obj]
            if isinstance(obj, dict):
                return {k: make_serializable(v) for k, v in obj.items()}
            if isinstance(obj, list):
                return [make_serializable(x) for x in obj]
            return obj

        with open(json_path, 'w') as f:
            json.dump(make_serializable(all_results), f, indent=2)
        print(f"\nResults saved to {json_path}")

    print("\nDone.")


if __name__ == "__main__":
    main()
