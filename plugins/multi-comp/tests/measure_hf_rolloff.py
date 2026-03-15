#!/usr/bin/env python3
"""Measure HF rolloff of each Multi-Comp mode by passing white noise through the plugin.
Uses mode-specific parameter names to ensure NO compression is active."""

import numpy as np
from pedalboard import load_plugin

SAMPLE_RATE = 44100
DURATION = 2.0
NUM_SAMPLES = int(SAMPLE_RATE * DURATION)

# Generate white noise at ~-34dBFS RMS (well below FET -10dB threshold)
np.random.seed(42)
noise = np.random.randn(1, NUM_SAMPLES).astype(np.float32)
noise *= 0.02  # ~-34dBFS RMS

import os, pathlib

def resolve_plugin_path():
    env = os.environ.get("PLUGIN_PATH")
    if env:
        return env
    repo_root = pathlib.Path(__file__).resolve().parents[3]
    for candidate in [
        repo_root / "build" / "bin" / "VST3" / "Multi-Comp.vst3",
        repo_root / "build" / "Multi-Comp_artefacts" / "VST3" / "Multi-Comp.vst3",
    ]:
        if candidate.exists():
            return str(candidate)
    raise FileNotFoundError(
        "Multi-Comp.vst3 not found. Set PLUGIN_PATH env var or build the plugin first."
    )

plugin_path = resolve_plugin_path()
print(f"Loading {plugin_path}...")
plugin = load_plugin(plugin_path)

def measure_spectrum(signal, sr):
    """Compute power spectrum in dB, return band averages."""
    fft = np.fft.rfft(signal[0])
    freqs = np.fft.rfftfreq(len(signal[0]), 1.0/sr)
    power_db = 20 * np.log10(np.abs(fft) + 1e-10)

    bands = [
        ("20-100", 20, 100),
        ("100-500", 100, 500),
        ("500-1k", 500, 1000),
        ("1k-2k", 1000, 2000),
        ("2k-5k", 2000, 5000),
        ("5k-8k", 5000, 8000),
        ("8k-12k", 8000, 12000),
        ("12k-16k", 12000, 16000),
        ("16k-20k", 16000, 20000),
    ]

    result = {}
    for name, lo, hi in bands:
        mask = (freqs >= lo) & (freqs < hi)
        if mask.any():
            result[name] = np.mean(power_db[mask])
    return result

input_spectrum = measure_spectrum(noise, SAMPLE_RATE)

# Mode configs: mode name, then per-mode params to ensure NO compression
# Each mode uses different parameter names!
mode_configs = [
    {
        "name": "Vintage Opto",
        "params": {
            "peak_reduction": 0.0,   # No compression
            "gain": 50.0,            # Unity (50 = 0dB)
            "limit_mode": False,
        }
    },
    {
        "name": "Vintage FET",
        "params": {
            "input": 0.0,            # 0dB input, signal at -34dBFS is below -10dB threshold
            "output": 0.0,
        }
    },
    {
        "name": "Classic VCA",
        "params": {
            "threshold": 12.0,       # Max threshold
            "makeup": 0.0,
            "output": 0.0,
        }
    },
    {
        "name": "Vintage VCA (Bus)",
        "params": {
            "threshold": 12.0,       # Max threshold
            "makeup": 0.0,
            "output": 0.0,
        }
    },
    {
        "name": "Studio FET",
        "params": {
            "input": 0.0,            # 0dB input
            "output": 0.0,
        }
    },
    {
        "name": "Studio VCA",
        "params": {
            "threshold": 12.0,
            "makeup": 0.0,
            "output": 0.0,
        }
    },
    {
        "name": "Digital",
        "params": {
            "threshold": 12.0,
            "makeup": 0.0,
            "output": 0.0,
            "lookahead_ms": 0.0,
        }
    },
    {
        "name": "Multiband",
        "params": {
            "threshold": 12.0,
            "low_threshold_db": 0.0,
            "low_mid_threshold_db": 0.0,
            "high_mid_threshold_db": 0.0,
            "high_threshold_db": 0.0,
            "output": 0.0,
        }
    },
]

print(f"\nWhite noise at ~-34dBFS, all compression DISABLED per mode")
print(f"Oversampling: {plugin.oversampling}")

hdr_bands = ["20-100", "100-500", "500-1k", "1k-2k", "2k-5k", "5k-8k", "8k-12k", "12k-16k", "16k-20k"]
print("\n" + "="*106)
print(f"{'Mode':<20}", end="")
for b in hdr_bands:
    print(f" {b:>9}", end="")
print()
print("-"*106)

for config in mode_configs:
    plugin.reset()
    plugin.mode = config["name"]
    plugin.analog_noise = False
    plugin.mix = 100.0
    plugin.bypass = False

    # Set mode-specific params
    for param, value in config["params"].items():
        try:
            setattr(plugin, param, value)
        except Exception as e:
            print(f"  WARNING: Could not set {param}={value}: {e}")

    # Warm up then measure
    _ = plugin.process(noise.copy(), SAMPLE_RATE)
    plugin.reset()
    output = plugin.process(noise.copy(), SAMPLE_RATE)

    out_spectrum = measure_spectrum(output, SAMPLE_RATE)

    print(f"{config['name']:<20}", end="")
    for band_name in hdr_bands:
        diff = out_spectrum.get(band_name, 0) - input_spectrum[band_name]
        marker = " " if abs(diff) < 1.0 else "*"  # Flag >1dB deviations
        print(f" {diff:>+7.1f}dB{marker}", end="")
    print()

print("="*106)
print("\n* = deviation > 1.0dB (investigate)")
print("Positive = boost, Negative = cut relative to input")
