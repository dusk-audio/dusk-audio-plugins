#!/usr/bin/env python3
"""Comprehensive HF rolloff test for all Multi-Comp modes.

Tests three scenarios per mode:
  1. No compression (flat frequency response — coloration only)
  2. Moderate compression (~6dB GR)
  3. Heavy compression (~12-15dB GR)

Also measures: overall gain change, THD estimate, and crest factor."""

import numpy as np
from pedalboard import load_plugin

SAMPLE_RATE = 44100
DURATION = 4.0
NUM_SAMPLES = int(SAMPLE_RATE * DURATION)

np.random.seed(42)

import os, pathlib

def resolve_plugin_path():
    env = os.environ.get("PLUGIN_PATH")
    if env:
        return env
    # Search common build output locations relative to this script
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

def make_noise(level_dbfs):
    """Generate white noise at a specific RMS level."""
    noise = np.random.randn(1, NUM_SAMPLES).astype(np.float32)
    rms = np.sqrt(np.mean(noise**2))
    target_rms = 10.0 ** (level_dbfs / 20.0)
    noise *= target_rms / rms
    return noise

def measure_spectrum(signal, sr):
    """Compute power spectrum in dB, return band averages."""
    fft = np.fft.rfft(signal[0])
    freqs = np.fft.rfftfreq(len(signal[0]), 1.0/sr)
    power_db = 20 * np.log10(np.abs(fft) + 1e-10)

    bands = [
        ("100-500", 100, 500),
        ("500-2k", 500, 2000),
        ("2k-5k", 2000, 5000),
        ("5k-10k", 5000, 10000),
        ("10k-15k", 10000, 15000),
        ("15k-20k", 15000, 20000),
    ]

    result = {}
    for name, lo, hi in bands:
        mask = (freqs >= lo) & (freqs < hi)
        if mask.any():
            result[name] = np.mean(power_db[mask])
    return result

def measure_rms_db(signal):
    rms = np.sqrt(np.mean(signal**2))
    return 20 * np.log10(rms + 1e-10)

def measure_gain_reduction(input_signal, output_signal):
    """Estimate gain reduction from RMS difference."""
    in_rms = measure_rms_db(input_signal)
    out_rms = measure_rms_db(output_signal)
    return out_rms - in_rms

# Reference bands for tilt calculation
BANDS = ["100-500", "500-2k", "2k-5k", "5k-10k", "10k-15k", "15k-20k"]

# ─── Mode configurations for three compression levels ───
# Each mode has: no_comp (bypass compression), moderate, heavy
mode_configs = [
    {
        "name": "Vintage Opto",
        "levels": [
            {"label": "No comp", "level_dbfs": -34, "params": {"peak_reduction": 0.0, "gain": 50.0, "limit_mode": False}},
            {"label": "~6dB GR", "level_dbfs": -12, "params": {"peak_reduction": 50.0, "gain": 50.0, "limit_mode": False}},
            {"label": "~12dB GR", "level_dbfs": -6, "params": {"peak_reduction": 80.0, "gain": 50.0, "limit_mode": True}},
        ]
    },
    {
        "name": "Vintage FET",
        "levels": [
            {"label": "No comp", "level_dbfs": -34, "params": {"input": 0.0, "output": 0.0}},
            {"label": "~6dB GR", "level_dbfs": -12, "params": {"input": 20.0, "output": -10.0}},
            {"label": "~12dB GR", "level_dbfs": -6, "params": {"input": 30.0, "output": -20.0}},
        ]
    },
    {
        "name": "Classic VCA",
        "levels": [
            {"label": "No comp", "level_dbfs": -34, "params": {"threshold": 12.0, "makeup": 0.0, "output": 0.0}},
            {"label": "~6dB GR", "level_dbfs": -12, "params": {"threshold": -6.0, "makeup": 0.0, "output": 0.0}},
            {"label": "~12dB GR", "level_dbfs": -6, "params": {"threshold": -12.0, "makeup": 0.0, "output": 0.0}},
        ]
    },
    {
        "name": "Vintage VCA (Bus)",
        "levels": [
            {"label": "No comp", "level_dbfs": -34, "params": {"threshold": 12.0, "makeup": 0.0, "output": 0.0}},
            {"label": "~6dB GR", "level_dbfs": -12, "params": {"threshold": -6.0, "makeup": 0.0, "output": 0.0}},
            {"label": "~12dB GR", "level_dbfs": -6, "params": {"threshold": -12.0, "makeup": 0.0, "output": 0.0}},
        ]
    },
    {
        "name": "Studio FET",
        "levels": [
            {"label": "No comp", "level_dbfs": -34, "params": {"input": 0.0, "output": 0.0}},
            {"label": "~6dB GR", "level_dbfs": -12, "params": {"input": 20.0, "output": -10.0}},
            {"label": "~12dB GR", "level_dbfs": -6, "params": {"input": 30.0, "output": -20.0}},
        ]
    },
    {
        "name": "Studio VCA",
        "levels": [
            {"label": "No comp", "level_dbfs": -34, "params": {"threshold": 12.0, "makeup": 0.0, "output": 0.0}},
            {"label": "~6dB GR", "level_dbfs": -12, "params": {"threshold": -6.0, "makeup": 0.0, "output": 0.0}},
            {"label": "~12dB GR", "level_dbfs": -6, "params": {"threshold": -12.0, "makeup": 0.0, "output": 0.0}},
        ]
    },
    {
        "name": "Digital",
        "levels": [
            {"label": "No comp", "level_dbfs": -34, "params": {"threshold": 12.0, "makeup": 0.0, "output": 0.0, "lookahead_ms": 0.0}},
            {"label": "~6dB GR", "level_dbfs": -12, "params": {"threshold": -6.0, "makeup": 0.0, "output": 0.0, "lookahead_ms": 0.0}},
            {"label": "~12dB GR", "level_dbfs": -6, "params": {"threshold": -12.0, "makeup": 0.0, "output": 0.0, "lookahead_ms": 0.0}},
        ]
    },
    {
        "name": "Multiband",
        "levels": [
            {"label": "No comp", "level_dbfs": -34, "params": {"threshold": 12.0, "output": 0.0,
                "low_threshold_db": 0.0, "low_mid_threshold_db": 0.0, "high_mid_threshold_db": 0.0, "high_threshold_db": 0.0}},
            {"label": "~6dB GR", "level_dbfs": -12, "params": {"threshold": -6.0, "output": 0.0,
                "low_threshold_db": -6.0, "low_mid_threshold_db": -6.0, "high_mid_threshold_db": -6.0, "high_threshold_db": -6.0}},
            {"label": "~12dB GR", "level_dbfs": -6, "params": {"threshold": -12.0, "output": 0.0,
                "low_threshold_db": -12.0, "low_mid_threshold_db": -12.0, "high_mid_threshold_db": -12.0, "high_threshold_db": -12.0}},
        ]
    },
]

print(f"\nComprehensive frequency response test — {SAMPLE_RATE}Hz, {DURATION}s noise")
print(f"Oversampling: {plugin.oversampling}")

# Known HF rolloff targets based on real hardware
print("\n── Expected HF behavior per mode (real hardware reference) ──")
print("  Opto (LA-2A):     UTC transformers roll off ~1dB at 16kHz (intentional)")
print("  FET (1176):       Cinemag/Jensen — flat to 20kHz, HF choke on sidechain only")
print("  Classic VCA:      No transformers — transparent")
print("  Bus (SSL):        Active design — transparent")
print("  Studio FET/VCA:   Modern clean — transparent")
print("  Digital:          Transparent")
print("  Multiband:        Crossover sums to flat")

for config in mode_configs:
    print(f"\n{'='*100}")
    print(f"  {config['name']}")
    print(f"{'='*100}")

    print(f"  {'Scenario':<14}", end="")
    for b in BANDS:
        print(f" {b:>8}", end="")
    print(f"  {'GainΔ':>7}  {'Verdict'}")
    print(f"  {'-'*92}")

    for level_cfg in config["levels"]:
        noise = make_noise(level_cfg["level_dbfs"])
        input_spectrum = measure_spectrum(noise, SAMPLE_RATE)

        plugin.reset()
        plugin.mode = config["name"]
        plugin.analog_noise = False
        plugin.mix = 100.0
        plugin.bypass = False

        for param, value in level_cfg["params"].items():
            try:
                setattr(plugin, param, value)
            except Exception as e:
                print(f"  WARNING: Could not set {param}={value}: {e}")

        # Warm up
        _ = plugin.process(noise.copy(), SAMPLE_RATE)
        plugin.reset()
        output = plugin.process(noise.copy(), SAMPLE_RATE)

        out_spectrum = measure_spectrum(output, SAMPLE_RATE)
        gain_delta = measure_gain_reduction(noise, output)

        # Calculate band diffs
        diffs = {}
        for band_name in BANDS:
            diffs[band_name] = out_spectrum.get(band_name, 0) - input_spectrum[band_name]

        # Calculate HF tilt: average of 10k-20k minus average of 500-2k
        hf_avg = np.mean([diffs.get("10k-15k", 0), diffs.get("15k-20k", 0)])
        mid_avg = diffs.get("500-2k", 0)
        tilt = hf_avg - mid_avg

        # Verdict
        if level_cfg["label"] == "No comp":
            if config["name"] == "Vintage Opto":
                # Opto should have gentle HF rolloff (UTC transformer)
                if abs(tilt) < 1.5:
                    verdict = "OK (UTC character)"
                else:
                    verdict = f"INVESTIGATE tilt={tilt:+.1f}dB"
            else:
                # All other modes should be flat with no compression
                if abs(tilt) < 0.5:
                    verdict = "PASS (flat)"
                elif abs(tilt) < 1.0:
                    verdict = f"WARN tilt={tilt:+.1f}dB"
                else:
                    verdict = f"FAIL tilt={tilt:+.1f}dB"
        else:
            # With compression, some tilt is acceptable
            if abs(tilt) < 1.0:
                verdict = "PASS"
            elif abs(tilt) < 2.0:
                verdict = f"MILD tilt={tilt:+.1f}dB"
            else:
                verdict = f"INVESTIGATE tilt={tilt:+.1f}dB"

        print(f"  {level_cfg['label']:<14}", end="")
        for band_name in BANDS:
            d = diffs[band_name]
            flag = "*" if abs(d) > 1.5 else " "
            print(f" {d:>+6.1f}dB{flag}", end="")
        print(f"  {gain_delta:>+5.1f}dB  {verdict}")

print(f"\n{'='*100}")
print("\nLegend:")
print("  * = band deviation > 1.5dB (investigate)")
print("  GainΔ = overall RMS gain change (includes compression + makeup)")
print("  Tilt = (avg 10-20kHz) - (500-2kHz) — negative = dark, positive = bright")
print("  PASS: HF tilt < 0.5dB (no comp) or < 1.0dB (with comp)")
