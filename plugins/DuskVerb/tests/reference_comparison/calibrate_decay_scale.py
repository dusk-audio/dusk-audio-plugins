#!/usr/bin/env python3
"""
Calibrate decayTimeScale for each DuskVerb algorithm.

For each algorithm, sets a known decay_time, measures actual RT60@500Hz via
Schroeder backward integration, and computes the correction factor for
AlgorithmConfig.h's decayTimeScale field.

Usage:
    # Build DuskVerb AU first:
    #   cd build && cmake --build . --config Release --target DuskVerb_AU -j8
    python3 calibrate_decay_scale.py

    # Test at multiple decay times for confidence:
    python3 calibrate_decay_scale.py --decay-times 1.5 3.0 6.0

    # Single algorithm:
    python3 calibrate_decay_scale.py --algorithm Hall
"""

import argparse
import sys

import numpy as np
from pedalboard import load_plugin

from config import SAMPLE_RATE, DUSKVERB_PATHS, find_plugin
from generate_test_signals import make_impulse
import reverb_metrics as metrics


ALGORITHMS = ["Plate", "Hall", "Chamber", "Room", "Ambient"]

# Current values in AlgorithmConfig.h (after Tier 1/2 audit)
CURRENT_SCALE = {
    "Plate":   0.94,
    "Hall":    0.79,
    "Chamber": 0.99,
    "Room":    1.28,
    "Ambient": 0.99,
}


def process_stereo(plugin, mono_signal, sr):
    stereo_in = np.stack([mono_signal, mono_signal], axis=0).astype(np.float32)
    output = plugin(stereo_in, sr)
    return output[0], output[1]


def flush_plugin(plugin, sr, duration_sec=2.0):
    silence = np.zeros(int(sr * duration_sec), dtype=np.float32)
    process_stereo(plugin, silence, sr)


def measure_rt60_at_decay(plugin, algorithm, decay_time, sr):
    """Set algorithm + decay_time, flush, process impulse, measure RT60@500Hz."""
    plugin.algorithm = algorithm
    plugin.decay_time = decay_time
    plugin.dry_wet = 1.0
    plugin.freeze = False
    plugin.bus_mode = False
    plugin.pre_delay_sync = "Free"
    # Neutral settings to isolate decay behavior
    plugin.diffusion = 0.5
    plugin.treble_multiply = 1.0
    plugin.bass_multiply = 1.0
    plugin.crossover = 500
    plugin.mod_depth = 0.3
    plugin.mod_rate = 0.5
    plugin.early_ref_level = 0.0  # Disable ER to measure pure FDN tail
    plugin.size = 0.5
    plugin.pre_delay = 0.0
    plugin.lo_cut = 20
    plugin.hi_cut = 20000
    plugin.width = 1.0

    flush_plugin(plugin, sr)

    impulse = make_impulse()
    out_l, _ = process_stereo(plugin, impulse, sr)

    rt60 = metrics.measure_rt60_per_band(out_l, sr, {"500 Hz": 500}).get("500 Hz")
    return rt60


def main():
    parser = argparse.ArgumentParser(description="Calibrate DuskVerb decayTimeScale")
    parser.add_argument("--decay-times", nargs="+", type=float, default=[2.0, 4.0, 8.0],
                        help="Decay times to test (seconds)")
    parser.add_argument("--algorithm", type=str, default=None,
                        help="Single algorithm to calibrate")
    args = parser.parse_args()

    dv_path = find_plugin(DUSKVERB_PATHS)
    if not dv_path:
        print("ERROR: DuskVerb not found. Build AU first:")
        print("  cd build && cmake --build . --config Release --target DuskVerb_AU -j8")
        sys.exit(1)

    print(f"Loading DuskVerb: {dv_path}")
    plugin = load_plugin(dv_path)

    algorithms = [args.algorithm] if args.algorithm else ALGORITHMS

    print(f"\nTest decay times: {args.decay_times}")
    print(f"Sample rate: {SAMPLE_RATE}")
    print()

    results = {}

    for algo in algorithms:
        print(f"{'='*60}")
        print(f"  {algo}")
        print(f"{'='*60}")

        ratios = []
        for target in args.decay_times:
            rt60 = measure_rt60_at_decay(plugin, algo, target, SAMPLE_RATE)
            if rt60 and rt60 > 0:
                ratio = target / rt60
                ratios.append(ratio)
                status = "OK" if 0.85 < ratio < 1.15 else "NEEDS ADJUSTMENT"
                print(f"  decay_time={target:.1f}s -> measured RT60={rt60:.3f}s "
                      f"(ratio={ratio:.3f}) [{status}]")
            else:
                print(f"  decay_time={target:.1f}s -> RT60 unmeasurable (too short or infinite)")

        if ratios:
            avg_ratio = float(np.mean(ratios))
            current = CURRENT_SCALE[algo]
            new_scale = current * avg_ratio
            print(f"\n  Current decayTimeScale: {current:.2f}")
            print(f"  Average target/measured ratio: {avg_ratio:.3f}")
            print(f"  Recommended decayTimeScale: {new_scale:.2f}")
            results[algo] = {
                "current": current,
                "ratio": avg_ratio,
                "recommended": new_scale,
            }
        else:
            print(f"\n  No valid measurements — cannot calibrate")
        print()

    # Summary
    if results:
        print(f"{'='*60}")
        print(f"  SUMMARY — paste into AlgorithmConfig.h")
        print(f"{'='*60}")
        print()
        for algo, data in results.items():
            change = "unchanged" if abs(data["ratio"] - 1.0) < 0.05 else \
                     f"{'increase' if data['ratio'] > 1.0 else 'decrease'} by {abs(data['ratio']-1.0)*100:.0f}%"
            print(f"  {algo:10s}: {data['current']:.2f} -> {data['recommended']:.2f}  ({change})")


if __name__ == "__main__":
    main()
