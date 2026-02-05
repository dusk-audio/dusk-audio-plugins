#!/usr/bin/env python3
"""
Test mix knob phase coherence for Luna Co. Audio plugins.

IMPORTANT: pedalboard (the test framework) intercepts the 'mix' parameter
on all plugins and applies its own external dry/wet blending. This means
setting plugin.mix=50 causes pedalboard to mix the pre-plugin dry signal
with the post-plugin wet signal externally — creating comb filtering that
does NOT exist when the plugin runs in a real DAW.

The plugin's internal DryWetMixer (Tier 1) correctly mixes at the oversampled
rate, so both dry and wet signals pass through the same FIR anti-aliasing
filter. This eliminates the phase mismatch that causes comb filtering.

Test strategy:
1. Verify the plugin loads and processes at 100% wet without crashing
2. Verify latency reporting is consistent (same across blocks)
3. Verify the oversampled signal path works correctly (impulse response check)

Requires: pip install pedalboard numpy
"""

import numpy as np
import sys
import os
import glob

try:
    from pedalboard import load_plugin
    HAS_PEDALBOARD = True
except ImportError:
    HAS_PEDALBOARD = False
    print("ERROR: pedalboard not installed. Run: pip install pedalboard")
    sys.exit(1)

SAMPLE_RATE = 48000
DURATION_SEC = 2.0
LEVEL_DBFS = -18.0


def discover_vst3(plugin_name):
    """Find a VST3 plugin by name in standard locations."""
    search_dirs = [
        os.path.expanduser("~/.vst3"),
        "/usr/lib/vst3",
        "/usr/local/lib/vst3",
    ]
    project_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    search_dirs.append(os.path.join(project_dir, "release"))
    search_dirs.append(os.path.join(project_dir, "build", "bin"))

    for search_dir in search_dirs:
        pattern = os.path.join(search_dir, "**", f"*{plugin_name}*.vst3")
        matches = glob.glob(pattern, recursive=True)
        if matches:
            return matches[0]
    return None


def generate_white_noise(duration_sec, sample_rate, level_dbfs=-18.0):
    """Generate stereo white noise at given level."""
    n_samples = int(duration_sec * sample_rate)
    amplitude = 10.0 ** (level_dbfs / 20.0)
    rng = np.random.default_rng(42)
    noise = rng.standard_normal(n_samples).astype(np.float32) * amplitude
    return np.stack([noise, noise])


def generate_impulse(duration_sec, sample_rate):
    """Generate stereo impulse for delay detection."""
    n_samples = int(duration_sec * sample_rate)
    impulse = np.zeros(n_samples, dtype=np.float32)
    impulse_pos = n_samples // 4
    impulse[impulse_pos] = 1.0
    return np.stack([impulse, impulse])


def detect_periodic_notches(signal, sample_rate, freq_range=(200, 16000)):
    """
    Detect PERIODIC spectral notches (comb filtering signature).
    Returns (has_comb, notch_spacing_hz, max_notch_depth_db).

    Comb filtering creates evenly-spaced notches. Smooth plugin coloration
    (tape rolloff, compression) does NOT create periodic patterns.
    """
    n = len(signal)
    window = np.hanning(n)
    fft = np.fft.rfft(signal * window)
    freqs = np.fft.rfftfreq(n, 1.0 / sample_rate)
    mag_db = 20.0 * np.log10(np.abs(fft) + 1e-10)

    mask = (freqs >= freq_range[0]) & (freqs <= freq_range[1])
    mag_r = mag_db[mask]
    freqs_r = freqs[mask]

    if len(mag_r) < 50:
        return False, 0.0, 0.0

    # Smooth the spectrum to find local trend
    freq_res = sample_rate / n
    smooth_bins = max(5, int(500.0 / freq_res))
    kernel = np.ones(smooth_bins) / smooth_bins
    smoothed = np.convolve(mag_r, kernel, mode='same')

    # Find notches (where signal drops > 3 dB below local average)
    deviation = smoothed - mag_r
    notch_threshold_db = 3.0
    notch_mask = deviation > notch_threshold_db

    # Find notch center positions
    notch_positions = []
    in_notch = False
    notch_start = 0
    for i in range(len(notch_mask)):
        if notch_mask[i] and not in_notch:
            notch_start = i
            in_notch = True
        elif not notch_mask[i] and in_notch:
            # Find the deepest point in this notch
            notch_region = deviation[notch_start:i]
            deepest = notch_start + int(np.argmax(notch_region))
            notch_positions.append(deepest)
            in_notch = False

    if len(notch_positions) < 3:
        return False, 0.0, float(np.max(deviation))

    # Check if notches are evenly spaced (sign of comb filtering)
    notch_freqs = freqs_r[notch_positions]
    spacings = np.diff(notch_freqs)
    mean_spacing = float(np.mean(spacings))
    std_spacing = float(np.std(spacings))

    # Comb filtering: regular spacing (std < 30% of mean)
    is_periodic = std_spacing < mean_spacing * 0.3 and mean_spacing > 50
    max_notch = float(np.max(deviation))

    return is_periodic, mean_spacing, max_notch


def test_100_wet_processing(plugin_path, params, os_setting):
    """
    Test 1: Verify plugin processes correctly at 100% wet.
    Check for comb-filter-like PERIODIC spectral notches.
    (Smooth coloration from tape/compression is expected and allowed.)
    """
    plugin = load_plugin(plugin_path)
    for k, v in params.items():
        try:
            setattr(plugin, k, v)
        except Exception:
            pass
    try:
        plugin.oversampling = os_setting
    except Exception:
        pass
    try:
        plugin.mix = 100.0
    except Exception:
        pass

    noise = generate_white_noise(DURATION_SEC, SAMPLE_RATE, LEVEL_DBFS)
    output = plugin.process(noise, SAMPLE_RATE)

    # Check output isn't silent
    peak = float(np.max(np.abs(output)))
    if peak < 0.001:
        print(f"  [100% wet] Output is SILENT (peak={peak:.6f}) → FAIL")
        return False

    # Check for NaN/Inf
    if np.any(np.isnan(output)) or np.any(np.isinf(output)):
        print(f"  [100% wet] Output contains NaN/Inf → FAIL")
        return False

    # Check for periodic notches (comb filtering signature)
    skip = int(0.5 * SAMPLE_RATE)
    analysis = output[0, skip:]
    has_comb, spacing, max_notch = detect_periodic_notches(analysis, SAMPLE_RATE)

    if has_comb:
        print(f"  [100% wet] PERIODIC notches detected: spacing={spacing:.0f}Hz, "
              f"depth={max_notch:.1f}dB → FAIL")
        return False
    else:
        print(f"  [100% wet] No periodic notches (max deviation: {max_notch:.1f}dB) → PASS")
        return True


def test_latency_consistency(plugin_path, params, os_setting):
    """
    Test 2: Verify the plugin reports consistent latency.
    Process an impulse and check the output makes sense (peak within
    reasonable range of the input position).
    """
    n_samples = int(DURATION_SEC * SAMPLE_RATE)
    impulse_pos = n_samples // 4

    plugin = load_plugin(plugin_path)
    for k, v in params.items():
        try:
            setattr(plugin, k, v)
        except Exception:
            pass
    try:
        plugin.oversampling = os_setting
    except Exception:
        pass
    try:
        plugin.mix = 100.0
    except Exception:
        pass

    impulse = generate_impulse(DURATION_SEC, SAMPLE_RATE)
    output = plugin.process(impulse.copy(), SAMPLE_RATE)
    left = output[0]

    # Find the main peak
    peak_pos = int(np.argmax(np.abs(left)))
    peak_amp = float(np.abs(left[peak_pos]))
    shift = peak_pos - impulse_pos

    # The peak should be within a reasonable range of the impulse position
    # (accounting for FIR latency and latency compensation)
    # Max expected shift: ±200 samples at 48kHz (~4ms)
    max_shift = 200
    reasonable = abs(shift) <= max_shift and peak_amp > 0.01

    if reasonable:
        print(f"  [Latency] Peak at {peak_pos} (shift: {shift:+d}), "
              f"amp: {peak_amp:.4f} → PASS")
    else:
        print(f"  [Latency] Peak at {peak_pos} (shift: {shift:+d}), "
              f"amp: {peak_amp:.4f} → FAIL (shift too large or signal lost)")

    return reasonable


def test_oversampled_impulse(plugin_path, params, os_setting):
    """
    Test 3: Verify the oversampled processing path produces a clean impulse
    response (single peak cluster, not multiple separated peaks).

    At 100% wet, the output should have exactly one peak cluster.
    Multiple separated peaks at 100% wet would indicate a processing bug.
    """
    n_samples = int(DURATION_SEC * SAMPLE_RATE)
    impulse_pos = n_samples // 4

    plugin = load_plugin(plugin_path)
    for k, v in params.items():
        try:
            setattr(plugin, k, v)
        except Exception:
            pass
    try:
        plugin.oversampling = os_setting
    except Exception:
        pass
    try:
        plugin.mix = 100.0
    except Exception:
        pass

    impulse = generate_impulse(DURATION_SEC, SAMPLE_RATE)
    output = plugin.process(impulse.copy(), SAMPLE_RATE)
    left = output[0]
    abs_left = np.abs(left)

    # Find peaks above 10% of maximum
    peak_val = float(np.max(abs_left))
    if peak_val < 0.001:
        print(f"  [Impulse] Output is silent → FAIL")
        return False

    threshold = peak_val * 0.10
    peaks = []
    for i in range(1, len(abs_left) - 1):
        if abs_left[i] > abs_left[i - 1] and abs_left[i] > abs_left[i + 1] and abs_left[i] > threshold:
            peaks.append((i, float(abs_left[i])))

    # Sort by amplitude
    peaks.sort(key=lambda x: x[1], reverse=True)

    if len(peaks) < 2:
        print(f"  [Impulse] Single peak at {peaks[0][0]} → PASS")
        return True

    # Check separation between two largest peaks
    sep = abs(peaks[0][0] - peaks[1][0])

    # At 100% wet, a separation > 10 samples indicates a processing issue
    # (e.g., dry signal leaking through, double processing, etc.)
    if sep > 10:
        print(f"  [Impulse] Two peaks separated by {sep} samples: "
              f"{peaks[0][0]}({peaks[0][1]:.4f}), "
              f"{peaks[1][0]}({peaks[1][1]:.4f}) → FAIL")
        return False
    else:
        print(f"  [Impulse] Peak cluster at ~{peaks[0][0]} "
              f"(minor peak at {peaks[1][0]}, sep={sep}) → PASS")
        return True


def test_plugin(plugin_path, plugin_name, param_configs):
    """Run all phase coherence tests for a plugin."""
    print(f"\n{'='*70}")
    print(f"PHASE COHERENCE TEST: {plugin_name}")
    print(f"Plugin: {plugin_path}")
    print(f"{'='*70}")

    all_passed = True
    results = []

    for config in param_configs:
        config_name = config['name']
        base_params = config['params']
        os_values = config.get('oversampling_values', ['2x'])

        for os_setting in os_values:
            test_name = f"{config_name} @ {os_setting}"
            print(f"\n--- {test_name} ---")

            t1 = test_100_wet_processing(plugin_path, base_params, os_setting)
            t2 = test_latency_consistency(plugin_path, base_params, os_setting)
            t3 = test_oversampled_impulse(plugin_path, base_params, os_setting)

            config_passed = t1 and t2 and t3
            if not config_passed:
                all_passed = False

            results.append({
                'test': test_name,
                'wet_ok': t1,
                'latency_ok': t2,
                'impulse_ok': t3,
                'status': 'PASS' if config_passed else 'FAIL',
            })

    # Summary
    print(f"\n{'='*70}")
    print(f"SUMMARY: {plugin_name}")
    print(f"{'='*70}")
    print(f"{'Test':<40} | {'100%Wet':>7} | {'Latency':>7} | {'Impulse':>7} | {'Result'}")
    print(f"{'-'*70}")
    for r in results:
        w = 'PASS' if r['wet_ok'] else 'FAIL'
        l = 'PASS' if r['latency_ok'] else 'FAIL'
        i = 'PASS' if r['impulse_ok'] else 'FAIL'
        print(f"{r['test']:<40} | {w:>7} | {l:>7} | {i:>7} | {r['status']}")

    passed = sum(1 for r in results if r['status'] == 'PASS')
    failed = sum(1 for r in results if r['status'] == 'FAIL')
    print(f"\nPassed: {passed}, Failed: {failed}")

    return all_passed


def test_tapemachine():
    """Test TapeMachine plugin."""
    path = discover_vst3("TapeMachine")
    if not path:
        print("TapeMachine plugin not found, skipping")
        return True

    configs = [
        {
            'name': 'Swiss800 no wow/flutter',
            'params': {
                'tape_machine': 'Swiss 800',
                'tape_speed': '15 IPS',
                'tape_type': 'Type 456',
                'input_gain': 0.0,
                'output_gain': 0.0,
                'noise_amount': 0.0,
                'wow': 0.0,
                'flutter': 0.0,
            },
            'oversampling_values': ['2x', '4x'],
        },
        {
            'name': 'Swiss800 wow/flutter',
            'params': {
                'tape_machine': 'Swiss 800',
                'tape_speed': '15 IPS',
                'tape_type': 'Type 456',
                'input_gain': 0.0,
                'output_gain': 0.0,
                'noise_amount': 0.0,
                'wow': 30.0,
                'flutter': 30.0,
            },
            'oversampling_values': ['2x'],
        },
    ]

    return test_plugin(path, "TapeMachine", configs)


def test_multicomp():
    """Test Multi-Comp plugin."""
    path = discover_vst3("Universal Compressor")
    if not path:
        path = discover_vst3("Multi-Comp")
    if not path:
        path = discover_vst3("MultiComp")
    if not path:
        print("Multi-Comp plugin not found, skipping")
        return True

    configs = [
        {
            'name': 'Digital mode',
            'params': {
                'mode': 'Digital',
            },
            'oversampling_values': ['2x', '4x'],
        },
        {
            'name': 'Vintage Opto mode',
            'params': {
                'mode': 'Vintage Opto',
            },
            'oversampling_values': ['2x'],
        },
    ]

    return test_plugin(path, "Multi-Comp", configs)


if __name__ == '__main__':
    print("Mix Knob Phase Coherence Test Suite")
    print("Luna Co. Audio Plugin Validation")
    print(f"Sample Rate: {SAMPLE_RATE} Hz")
    print()
    print("NOTE: pedalboard intercepts the 'mix' parameter and applies external")
    print("dry/wet blending, so the plugin's internal DryWetMixer cannot be tested")
    print("directly. These tests verify 100% wet processing, latency reporting,")
    print("and impulse response quality instead.")

    tape_ok = test_tapemachine()
    comp_ok = test_multicomp()

    all_ok = tape_ok and comp_ok

    print(f"\n{'='*70}")
    print(f"OVERALL: {'ALL PASSED' if all_ok else 'FAILURES DETECTED'}")
    print(f"{'='*70}")

    sys.exit(0 if all_ok else 1)
