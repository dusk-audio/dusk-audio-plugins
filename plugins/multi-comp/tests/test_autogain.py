#!/usr/bin/env python3
"""Test GR-based auto-gain across all compressor modes and oversampling settings."""

import os
import numpy as np
from pedalboard import load_plugin

VST3_PATH = os.environ.get(
    "MULTI_COMP_VST3_PATH",
    os.path.join(os.path.dirname(__file__), "..", "build", "Multi-Comp_artefacts", "VST3", "Multi-Comp.vst3")
)
SAMPLE_RATE = 44100
DURATION = 4.0
LEVEL_DBFS = -12.0

MODES = [
    "Vintage Opto",
    "Vintage FET",
    "Classic VCA",
    "Vintage VCA (Bus)",
    "Studio FET",
    "Studio VCA",
    "Digital",
    "Multiband",
]

OS_SETTINGS = ["Off", "2x", "4x"]

def make_sine(freq=1000.0, duration=DURATION, sr=SAMPLE_RATE, level_db=LEVEL_DBFS):
    t = np.arange(int(sr * duration)) / sr
    amplitude = 10 ** (level_db / 20.0)
    signal = amplitude * np.sin(2 * np.pi * freq * t)
    return signal.astype(np.float32).reshape(1, -1)

def peak_db(signal):
    peak = np.max(np.abs(signal))
    return 20 * np.log10(peak + 1e-10)

def setup_mode(plugin, mode_name):
    """Set heavy compression parameters for each mode.

    Note: pedalboard exposes parameters by display name (e.g., 'input', 'ratio')
    not by internal ID (e.g., 'fet_input', 'fet_ratio'). The same display names
    are shared across modes — the plugin routes them based on the current mode.
    """
    plugin.mode = mode_name
    plugin.auto_makeup = True

    if mode_name == "Vintage Opto":
        plugin.peak_reduction = 80.0
    elif mode_name in ("Vintage FET", "Studio FET"):
        plugin.input = 0.0
        plugin.ratio = "20:1"
        plugin.attack = 0.02
        plugin.release = 300.0
    elif mode_name in ("Classic VCA", "Studio VCA"):
        plugin.threshold = -30.0
        plugin.ratio = "8:1"
    elif mode_name == "Vintage VCA (Bus)":
        plugin.threshold = -25.0
        plugin.ratio = "4:1"
    elif mode_name == "Digital":
        plugin.threshold = -30.0
        plugin.ratio = "8:1"
        plugin.knee = 0.0
    elif mode_name == "Multiband":
        for prefix in ["low", "low_mid", "high_mid", "high"]:
            setattr(plugin, f"{prefix}_threshold_db", -25.0)
            setattr(plugin, f"{prefix}_ratio", 4.0)

def test_mode(mode_name, os_setting, plugin):
    plugin.oversampling = os_setting
    setup_mode(plugin, mode_name)

    input_signal = make_sine()
    input_peak = peak_db(input_signal)

    # Process multiple times to let GR smoothing settle
    for _ in range(3):
        output = plugin(input_signal.copy(), SAMPLE_RATE)

    skip = int(output.shape[1] * 0.1)
    output_peak = peak_db(output[:, skip:])

    return output_peak - input_peak

def main():
    plugin = load_plugin(VST3_PATH)

    print(f"{'Mode':<20} {'OS':<6} {'Error (dB)':>10}  Status")
    print("-" * 50)

    all_pass = True
    for mode_name in MODES:
        for os_name in OS_SETTINGS:
            try:
                error = test_mode(mode_name, os_name, plugin)
                status = "PASS" if abs(error) < 1.5 else "FAIL"
                if status == "FAIL":
                    all_pass = False
                print(f"{mode_name:<20} {os_name:<6} {error:>+10.2f}  {status}")
            except Exception as e:
                print(f"{mode_name:<20} {os_name:<6} {'ERROR':>10}  {e}")
                all_pass = False

    # Test FET with input gain down
    print("\n--- FET Input Gain Compensation ---")
    plugin.mode = "Vintage FET"
    plugin.oversampling = "Off"
    plugin.auto_makeup = True
    plugin.input = -12.0
    plugin.ratio = "20:1"
    plugin.attack = 0.02
    plugin.release = 300.0

    input_signal = make_sine()
    input_peak = peak_db(input_signal)
    for _ in range(3):
        output = plugin(input_signal.copy(), SAMPLE_RATE)
    skip = int(output.shape[1] * 0.1)
    output_peak = peak_db(output[:, skip:])
    error = output_peak - input_peak
    status = "PASS" if abs(error) < 2.0 else "FAIL"
    if status == "FAIL":
        all_pass = False
    print(f"FET -12dB input: error = {error:+.2f} dB  {status}")

    print(f"\n{'ALL TESTS PASSED' if all_pass else 'SOME TESTS FAILED'}")

if __name__ == "__main__":
    main()
