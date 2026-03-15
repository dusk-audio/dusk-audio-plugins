#!/usr/bin/env python3
"""Measure autogain accuracy: compare input vs output RMS with auto-makeup enabled.
A perfect autogain should yield 0.0dB difference at all compression levels."""

import numpy as np
import os, pathlib
from pedalboard import load_plugin

SAMPLE_RATE = 44100
DURATION = 4.0
NUM_SAMPLES = int(SAMPLE_RATE * DURATION)

np.random.seed(42)

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
    raise FileNotFoundError("Multi-Comp.vst3 not found.")

plugin_path = resolve_plugin_path()
print(f"Loading {plugin_path}...")
plugin = load_plugin(plugin_path)

def make_noise(level_dbfs):
    noise = np.random.randn(1, NUM_SAMPLES).astype(np.float32)
    rms = np.sqrt(np.mean(noise**2))
    target_rms = 10.0 ** (level_dbfs / 20.0)
    noise *= target_rms / rms
    return noise

def measure_rms_db(signal):
    rms = np.sqrt(np.mean(signal[0]**2))
    return 20 * np.log10(rms + 1e-10)

# Mode configs with compression params that produce ~6dB and ~12dB GR
mode_configs = [
    {
        "name": "Vintage Opto",
        "levels": [
            {"label": "Light (~3dB)", "level_dbfs": -12, "params": {"peak_reduction": 30.0, "gain": 50.0, "limit_mode": False}},
            {"label": "Medium (~6dB)", "level_dbfs": -12, "params": {"peak_reduction": 50.0, "gain": 50.0, "limit_mode": False}},
            {"label": "Heavy (~12dB)", "level_dbfs": -6, "params": {"peak_reduction": 80.0, "gain": 50.0, "limit_mode": True}},
        ]
    },
    {
        "name": "Vintage FET",
        "levels": [
            {"label": "Light (~3dB)", "level_dbfs": -12, "params": {"input": 10.0, "output": 0.0}},
            {"label": "Medium (~6dB)", "level_dbfs": -12, "params": {"input": 20.0, "output": 0.0}},
            {"label": "Heavy (~12dB)", "level_dbfs": -6, "params": {"input": 30.0, "output": 0.0}},
        ]
    },
    {
        "name": "Classic VCA",
        "levels": [
            {"label": "Light (~3dB)", "level_dbfs": -12, "params": {"threshold": -3.0, "makeup": 0.0, "output": 0.0}},
            {"label": "Medium (~6dB)", "level_dbfs": -12, "params": {"threshold": -6.0, "makeup": 0.0, "output": 0.0}},
            {"label": "Heavy (~12dB)", "level_dbfs": -6, "params": {"threshold": -12.0, "makeup": 0.0, "output": 0.0}},
        ]
    },
    {
        "name": "Vintage VCA (Bus)",
        "levels": [
            {"label": "Light (~3dB)", "level_dbfs": -12, "params": {"threshold": -3.0, "makeup": 0.0, "output": 0.0}},
            {"label": "Medium (~6dB)", "level_dbfs": -12, "params": {"threshold": -6.0, "makeup": 0.0, "output": 0.0}},
            {"label": "Heavy (~12dB)", "level_dbfs": -6, "params": {"threshold": -12.0, "makeup": 0.0, "output": 0.0}},
        ]
    },
    {
        "name": "Studio FET",
        "levels": [
            {"label": "Light (~3dB)", "level_dbfs": -12, "params": {"input": 10.0, "output": 0.0}},
            {"label": "Medium (~6dB)", "level_dbfs": -12, "params": {"input": 20.0, "output": 0.0}},
            {"label": "Heavy (~12dB)", "level_dbfs": -6, "params": {"input": 30.0, "output": 0.0}},
        ]
    },
    {
        "name": "Studio VCA",
        "levels": [
            {"label": "Light (~3dB)", "level_dbfs": -12, "params": {"threshold": -3.0, "makeup": 0.0, "output": 0.0}},
            {"label": "Medium (~6dB)", "level_dbfs": -12, "params": {"threshold": -6.0, "makeup": 0.0, "output": 0.0}},
            {"label": "Heavy (~12dB)", "level_dbfs": -6, "params": {"threshold": -12.0, "makeup": 0.0, "output": 0.0}},
        ]
    },
    {
        "name": "Digital",
        "levels": [
            {"label": "Light (~3dB)", "level_dbfs": -12, "params": {"threshold": -3.0, "makeup": 0.0, "output": 0.0, "lookahead_ms": 0.0}},
            {"label": "Medium (~6dB)", "level_dbfs": -12, "params": {"threshold": -6.0, "makeup": 0.0, "output": 0.0, "lookahead_ms": 0.0}},
            {"label": "Heavy (~12dB)", "level_dbfs": -6, "params": {"threshold": -12.0, "makeup": 0.0, "output": 0.0, "lookahead_ms": 0.0}},
        ]
    },
    {
        "name": "Multiband",
        "levels": [
            {"label": "Light (~3dB)", "level_dbfs": -12, "params": {"threshold": -3.0, "output": 0.0,
                "low_threshold_db": -3.0, "low_mid_threshold_db": -3.0, "high_mid_threshold_db": -3.0, "high_threshold_db": -3.0}},
            {"label": "Medium (~6dB)", "level_dbfs": -12, "params": {"threshold": -6.0, "output": 0.0,
                "low_threshold_db": -6.0, "low_mid_threshold_db": -6.0, "high_mid_threshold_db": -6.0, "high_threshold_db": -6.0}},
            {"label": "Heavy (~12dB)", "level_dbfs": -6, "params": {"threshold": -12.0, "output": 0.0,
                "low_threshold_db": -12.0, "low_mid_threshold_db": -12.0, "high_mid_threshold_db": -12.0, "high_threshold_db": -12.0}},
        ]
    },
]

print(f"\nAutogain accuracy test — white noise, auto_makeup ON")
print(f"Target: output RMS ≈ input RMS (0.0dB error)")
print(f"{'='*80}")
print(f"{'Mode':<22} {'Scenario':<16} {'In RMS':>8} {'Out RMS':>8} {'Error':>8}  Verdict")
print(f"{'-'*80}")

for config in mode_configs:
    for level_cfg in config["levels"]:
        noise = make_noise(level_cfg["level_dbfs"])
        in_rms = measure_rms_db(noise)

        plugin.reset()
        plugin.mode = config["name"]
        plugin.analog_noise = False
        plugin.mix = 100.0
        plugin.bypass = False
        plugin.auto_makeup = True

        for param, value in level_cfg["params"].items():
            try:
                setattr(plugin, param, value)
            except Exception as e:
                print(f"  WARNING: Could not set {param}={value}: {e}")

        # Warm up to settle smoothed autogain
        for _ in range(3):
            _ = plugin.process(noise.copy(), SAMPLE_RATE)

        # Final measurement pass
        plugin.reset()
        # Need to re-warm autogain after reset
        for _ in range(2):
            _ = plugin.process(noise.copy(), SAMPLE_RATE)
        output = plugin.process(noise.copy(), SAMPLE_RATE)

        out_rms = measure_rms_db(output)
        error = out_rms - in_rms

        if abs(error) < 0.5:
            verdict = "PASS"
        elif abs(error) < 1.0:
            verdict = "WARN"
        elif abs(error) < 2.0:
            verdict = f"{'LOUD' if error > 0 else 'QUIET'} ({error:+.1f}dB)"
        else:
            verdict = f"FAIL ({error:+.1f}dB)"

        print(f"{config['name']:<22} {level_cfg['label']:<16} {in_rms:>+6.1f}dB {out_rms:>+6.1f}dB {error:>+6.1f}dB  {verdict}")

    print()

print(f"{'='*80}")
print("PASS: error < 0.5dB | WARN: 0.5-1.0dB | LOUD/QUIET: 1.0-2.0dB | FAIL: >2.0dB")
