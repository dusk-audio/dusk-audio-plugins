#!/usr/bin/env python3
"""
Test TapeMachine plugin THD measurements.
Generates 1kHz sine at -18dBFS, processes through the AU plugin,
and measures H2, H3, and total THD.
"""

import numpy as np
import os
import sys

try:
    from pedalboard import load_plugin
    HAS_PEDALBOARD = True
except ImportError:
    HAS_PEDALBOARD = False
    print("pedalboard not available, using offline analysis mode")

SAMPLE_RATE = 44100
DURATION_SEC = 2.0
FREQ_HZ = 1000.0
LEVEL_DBFS = -18.0


def discover_au_path():
    """Discover TapeMachine AU plugin path from environment or common locations."""
    # 1. Check environment variable
    env_path = os.environ.get("AU_PATH")
    if env_path and os.path.exists(env_path):
        return env_path

    # 2. Search common AU plugin directories
    search_dirs = []
    home = os.path.expanduser("~")

    if sys.platform == "darwin":
        search_dirs = [
            os.path.join(home, "Library/Audio/Plug-Ins/Components"),
            "/Library/Audio/Plug-Ins/Components",
        ]

    plugin_name = "TapeMachine.component"
    for search_dir in search_dirs:
        candidate = os.path.join(search_dir, plugin_name)
        if os.path.exists(candidate):
            return candidate

    return None


AU_PATH = discover_au_path()


def generate_sine(freq_hz, level_dbfs, duration_sec, sample_rate):
    """Generate a sine wave at given frequency and level."""
    n_samples = int(duration_sec * sample_rate)
    t = np.arange(n_samples) / sample_rate
    amplitude = 10.0 ** (level_dbfs / 20.0)
    signal = amplitude * np.sin(2.0 * np.pi * freq_hz * t)
    return signal.astype(np.float32)


def measure_harmonics(signal, fundamental_hz, sample_rate, n_harmonics=8):
    """Measure harmonic levels using FFT."""
    # Use a window to reduce spectral leakage
    n = len(signal)
    window = np.hanning(n)
    windowed = signal * window

    # FFT
    fft = np.fft.rfft(windowed)
    freqs = np.fft.rfftfreq(n, 1.0 / sample_rate)
    magnitudes = np.abs(fft) * 2.0 / np.sum(window)  # Normalize for window

    # Find fundamental and harmonics
    freq_resolution = sample_rate / n
    results = {}

    for h in range(1, n_harmonics + 1):
        target_freq = fundamental_hz * h
        if target_freq >= sample_rate / 2:
            break

        # Find the bin closest to target frequency
        bin_idx = int(round(target_freq / freq_resolution))
        # Take the max in a small window around the expected bin
        start = max(0, bin_idx - 2)
        end = min(len(magnitudes), bin_idx + 3)
        peak_mag = np.max(magnitudes[start:end])

        results[f'H{h}'] = peak_mag

    return results


def compute_thd(harmonics):
    """Compute THD from harmonic measurements."""
    h1 = harmonics.get('H1', 1e-10)
    harmonic_power = sum(v**2 for k, v in harmonics.items() if k != 'H1')
    thd = np.sqrt(harmonic_power) / h1 * 100.0  # percentage
    return thd


def print_results(label, harmonics, thd):
    """Print formatted results."""
    h1 = harmonics.get('H1', 1e-10)
    print(f"\n{'='*50}")
    print(f"  {label}")
    print(f"{'='*50}")
    print(f"  Fundamental (H1): {20*np.log10(max(h1, 1e-10)):.1f} dBFS")
    for i in range(2, 9):
        key = f'H{i}'
        if key in harmonics:
            level_db = 20 * np.log10(max(harmonics[key], 1e-10))
            relative_db = 20 * np.log10(max(harmonics[key] / h1, 1e-10))
            print(f"  H{i} ({FREQ_HZ*i:.0f}Hz): {level_db:.1f} dBFS ({relative_db:.1f} dB rel)")
    print(f"  THD: {thd:.4f}%")
    if 'H2' in harmonics and 'H3' in harmonics:
        h2_db = 20 * np.log10(max(harmonics['H2'] / h1, 1e-10))
        h3_db = 20 * np.log10(max(harmonics['H3'] / h1, 1e-10))
        print(f"  H2/H3 ratio: {harmonics['H2']/max(harmonics['H3'], 1e-10):.2f} "
              f"(H3-H2 = {h2_db - h3_db:.1f} dB)")
    print(f"{'='*50}")


def test_with_pedalboard():
    """Test using pedalboard to load and process through the AU plugin."""
    if AU_PATH is None:
        raise RuntimeError(
            "TapeMachine AU plugin not found. Set the AU_PATH environment variable "
            "to the full path of TapeMachine.component, or install the plugin to "
            "~/Library/Audio/Plug-Ins/Components/."
        )
    print(f"Loading AU plugin from: {AU_PATH}")
    plugin = load_plugin(AU_PATH)

    print(f"\nAvailable parameters:")
    for param in plugin.parameters.keys():
        print(f"  {param}")

    # Generate test signal
    test_signal = generate_sine(FREQ_HZ, LEVEL_DBFS, DURATION_SEC, SAMPLE_RATE)
    # Make stereo
    stereo_signal = np.stack([test_signal, test_signal])

    # First verify the input signal
    input_harmonics = measure_harmonics(test_signal, FREQ_HZ, SAMPLE_RATE)
    input_thd = compute_thd(input_harmonics)
    print_results("INPUT SIGNAL (reference)", input_harmonics, input_thd)

    # Test configurations
    configs = [
        {
            'name': 'Swiss800 - 0VU (input gain 0dB)',
            'params': {
                'tape_machine': 0.0,     # Swiss800
                'tape_speed': 1.0/2.0,   # 15 IPS (middle of 3 choices)
                'tape_type': 0.0,        # Type 456
                'input_gain': 0.5,       # 0dB (middle of -12 to +12 range)
                'noise_enabled': 0.0,    # Noise off for clean measurement
                'wow_amount': 0.0,       # No wow
                'flutter_amount': 0.0,   # No flutter
                'output_gain': 0.5,      # Unity
                'mix': 1.0,              # Full wet
            }
        },
        {
            'name': 'Classic102 - 0VU (input gain 0dB)',
            'params': {
                'tape_machine': 1.0,     # Classic102
                'tape_speed': 1.0/2.0,   # 15 IPS
                'tape_type': 0.0,        # Type 456
                'input_gain': 0.5,       # 0dB
                'noise_enabled': 0.0,
                'wow_amount': 0.0,
                'flutter_amount': 0.0,
                'output_gain': 0.5,
                'mix': 1.0,
            }
        },
        {
            'name': 'Swiss800 - +6VU (input gain +6dB)',
            'params': {
                'tape_machine': 0.0,
                'tape_speed': 1.0/2.0,
                'tape_type': 0.0,
                'input_gain': 0.75,      # +6dB (75% of range)
                'noise_enabled': 0.0,
                'wow_amount': 0.0,
                'flutter_amount': 0.0,
                'output_gain': 0.5,
                'mix': 1.0,
            }
        },
        {
            'name': 'Classic102 - +6VU (input gain +6dB)',
            'params': {
                'tape_machine': 1.0,
                'tape_speed': 1.0/2.0,
                'tape_type': 0.0,
                'input_gain': 0.75,
                'noise_enabled': 0.0,
                'wow_amount': 0.0,
                'flutter_amount': 0.0,
                'output_gain': 0.5,
                'mix': 1.0,
            }
        },
    ]

    for config in configs:
        # Reset plugin
        plugin.reset()

        # Set parameters
        for param_name, value in config['params'].items():
            try:
                setattr(plugin, param_name, value)
            except (AttributeError, TypeError) as e:
                print(f"  Warning: Could not set {param_name}: {e}")

        # Process
        output = plugin.process(stereo_signal, SAMPLE_RATE)

        # Skip first 0.5s to avoid transients
        skip_samples = int(0.5 * SAMPLE_RATE)
        analysis_signal = output[0, skip_samples:]  # Left channel

        # Measure
        harmonics = measure_harmonics(analysis_signal, FREQ_HZ, SAMPLE_RATE)
        thd = compute_thd(harmonics)
        print_results(config['name'], harmonics, thd)


def test_offline():
    """
    Offline analysis: generate signal and provide instructions.
    If pedalboard isn't available, we can still verify the math.
    """
    print("=== Offline Mode: Verifying drive calculations ===\n")

    # Verify drive values at different input gains
    test_cases = [
        (-12.0, "Minimum input gain"),
        (0.0, "0VU equivalent"),
        (6.0, "+6VU"),
        (12.0, "Maximum input gain"),
    ]

    print(f"{'Input Gain':>12} | {'Sat Depth':>10} | {'Drive (456)':>12} | {'Drive (GP9)':>12} | {'Description'}")
    print("-" * 80)

    # Tape formulation saturation points (from code)
    tape_sat_points = {
        '456': 0.70,
        'GP9': 0.75,
        '911': 0.72,
        '250': 0.65,
    }

    for input_gain_db, desc in test_cases:
        sat_depth = (input_gain_db + 12.0) / 24.0
        for tape_name, sat_point in [('456', 0.70), ('GP9', 0.75)]:
            tape_form_scale = 2.0 * (1.0 - sat_point) + 0.6
            drive = 0.08 * np.exp(4.6 * sat_depth) * tape_form_scale
            if tape_name == '456':
                print(f"{input_gain_db:>10.1f}dB | {sat_depth:>10.3f} | {drive:>12.4f} | ", end="")
            else:
                print(f"{drive:>12.4f} | {desc}")

    # Simulate tanh saturation at different drives
    print(f"\n\n{'='*60}")
    print("  Simulated THD (tanh waveshaper, 1kHz @ -18dBFS)")
    print(f"{'='*60}\n")

    test_signal = generate_sine(FREQ_HZ, LEVEL_DBFS, DURATION_SEC, SAMPLE_RATE)

    # Skip the first part for steady state
    skip = int(0.5 * SAMPLE_RATE)

    configs = [
        ("Swiss800, 0VU, 456", 0.5, 0.70, True),
        ("Classic102, 0VU, 456", 0.5, 0.70, False),
        ("Swiss800, +6VU, 456", 0.75, 0.70, True),
        ("Classic102, +6VU, 456", 0.75, 0.70, False),
        ("Swiss800, 0VU, GP9", 0.5, 0.75, True),
        ("Classic102, 0VU, GP9", 0.5, 0.75, False),
    ]

    for name, sat_depth, sat_point, is_swiss800 in configs:
        tape_form_scale = 2.0 * (1.0 - sat_point) + 0.6
        drive = 0.08 * np.exp(4.6 * sat_depth) * tape_form_scale

        # Swiss800: driveK=2.0, minimal asymmetry
        # Classic102: driveK=1.8, more asymmetry
        driveK = 2.0 if is_swiss800 else 1.8

        # Apply tanh saturation (simplified - no band splitting for this estimate)
        effective_drive = driveK * drive
        if effective_drive > 0.001:
            output = np.tanh(effective_drive * test_signal) / effective_drive
        else:
            output = test_signal.copy()

        # Measure harmonics on steady-state portion
        harmonics = measure_harmonics(output[skip:], FREQ_HZ, SAMPLE_RATE)
        thd = compute_thd(harmonics)
        print_results(name, harmonics, thd)


if __name__ == '__main__':
    if HAS_PEDALBOARD and '--offline' not in sys.argv:
        try:
            test_with_pedalboard()
        except Exception as e:
            print(f"Pedalboard test failed: {e}")
            print("Falling back to offline mode...\n")
            test_offline()
    else:
        test_offline()
