#!/usr/bin/env python3
"""
Test TapeMachine plugin noise floor at different oversampling rates.
Processes a 290Hz sine at -18dBFS through the plugin at 1x and 4x
and compares the LF noise content (20-200Hz bands).
"""

import numpy as np
import sys

try:
    from pedalboard import load_plugin
    HAS_PEDALBOARD = True
except ImportError:
    HAS_PEDALBOARD = False
    print("pedalboard not available")
    sys.exit(1)

SAMPLE_RATE = 44100
DURATION_SEC = 3.0
FREQ_HZ = 290.0
LEVEL_DBFS = -18.0

def discover_au_path():
    import os
    home = os.path.expanduser("~")
    candidate = os.path.join(home, "Library/Audio/Plug-Ins/Components/TapeMachine.component")
    if os.path.exists(candidate):
        return candidate
    return None

def generate_sine(freq_hz, level_dbfs, duration_sec, sample_rate):
    n_samples = int(duration_sec * sample_rate)
    t = np.arange(n_samples) / sample_rate
    amplitude = 10.0 ** (level_dbfs / 20.0)
    signal = amplitude * np.sin(2.0 * np.pi * freq_hz * t)
    return signal.astype(np.float32)

def measure_band_levels(signal, sample_rate, bands):
    """Measure RMS level in specified frequency bands using FFT."""
    n = len(signal)
    window = np.hanning(n)
    windowed = signal * window
    fft = np.fft.rfft(windowed)
    freqs = np.fft.rfftfreq(n, 1.0 / sample_rate)
    magnitudes = np.abs(fft) * 2.0 / np.sum(window)

    results = {}
    for band_name, (f_low, f_high) in bands.items():
        mask = (freqs >= f_low) & (freqs < f_high)
        # Exclude fundamental and harmonics (290Hz and multiples)
        for h in range(1, 20):
            harmonic = FREQ_HZ * h
            harmonic_mask = (freqs >= harmonic - 5) & (freqs <= harmonic + 5)
            mask = mask & ~harmonic_mask

        if np.any(mask):
            band_power = np.sum(magnitudes[mask] ** 2)
            band_rms = np.sqrt(band_power)
            level_db = 20.0 * np.log10(max(band_rms, 1e-10))
            results[band_name] = level_db
        else:
            results[band_name] = -200.0

    return results

def test_noise_floor():
    au_path = discover_au_path()
    if au_path is None:
        print("TapeMachine AU not found")
        sys.exit(1)

    print(f"Loading: {au_path}")
    plugin = load_plugin(au_path)

    # Generate test signal
    test_signal = generate_sine(FREQ_HZ, LEVEL_DBFS, DURATION_SEC, SAMPLE_RATE)
    stereo_signal = np.stack([test_signal, test_signal])

    # Frequency bands to measure (excluding fundamental and harmonics)
    bands = {
        '20-50Hz': (20, 50),
        '50-100Hz': (50, 100),
        '100-200Hz': (100, 200),
    }

    # Test configurations: different oversampling with no wow/flutter
    # Print available parameter values for oversampling
    print(f"\nOversampling parameter info:")
    if hasattr(plugin, 'oversampling'):
        param = plugin.parameters.get('oversampling')
        if param:
            print(f"  Valid values: {param.valid_values if hasattr(param, 'valid_values') else 'unknown'}")
            print(f"  Range: {param.range if hasattr(param, 'range') else 'unknown'}")
            print(f"  Current: {plugin.oversampling}")

    configs = [
        {
            'name': '1x Oversampling (no wow/flutter)',
            'oversampling': '1.0x',
        },
        {
            'name': '4x Oversampling (no wow/flutter)',
            'oversampling': '4.0x',
        },
    ]

    print(f"\nTest signal: {FREQ_HZ}Hz sine at {LEVEL_DBFS} dBFS")
    print(f"Settings: Swiss 800, GP9, 30 IPS, Wow=0, Flutter=0, Noise=OFF")
    print(f"{'='*70}")

    all_results = {}

    for config in configs:
        plugin.reset()

        # Set parameters using string values where needed
        try:
            plugin.tape_machine = 'Swiss 800'
        except:
            try:
                plugin.tape_machine = 0.0
            except:
                pass

        try:
            plugin.tape_speed = '30 IPS'
        except:
            try:
                plugin.tape_speed = 1.0  # normalized
            except:
                pass

        try:
            plugin.tape_type = 'GP9'
        except:
            try:
                plugin.tape_type = 0.25  # normalized for GP9
            except:
                pass

        # Core settings
        try: plugin.wow = 0.0
        except: pass
        try: plugin.flutter = 0.0
        except: pass
        try: plugin.noise_enabled = 0.0
        except: pass
        try: plugin.noise_amount = 0.0
        except: pass
        try: plugin.input_gain = 0.5  # 0dB (center of range)
        except: pass
        try: plugin.output_gain = 0.5  # 0dB
        except: pass
        try: plugin.mix = 1.0  # Full wet
        except: pass
        try: plugin.saturation = 0.5  # Moderate
        except: pass
        # Try various formats for the oversampling parameter
        os_val = config['oversampling']
        try:
            plugin.oversampling = os_val
        except Exception as e1:
            # Try alternative formats
            try:
                # Normalized: 0.0=1x, 0.5=2x, 1.0=4x
                norm_val = {'1.0x': 0.0, '2.0x': 0.5, '4.0x': 1.0}.get(os_val, 0.0)
                plugin.oversampling = norm_val
            except Exception as e2:
                print(f"  Warning: Could not set oversampling '{os_val}': {e1}; also tried normalized: {e2}")

        # Process (longer signal for better frequency resolution)
        output = plugin.process(stereo_signal, SAMPLE_RATE)

        # Skip first 1s to avoid transients and filter settling
        skip_samples = int(1.0 * SAMPLE_RATE)
        analysis_signal = output[0, skip_samples:]

        # Measure band levels
        results = measure_band_levels(analysis_signal, SAMPLE_RATE, bands)
        all_results[config['name']] = results

        print(f"\n  {config['name']}:")
        for band, level in results.items():
            print(f"    {band}: {level:.1f} dBFS")

    # Compare
    print(f"\n{'='*70}")
    print("  COMPARISON (4x - 1x):")
    print(f"{'='*70}")

    names = list(all_results.keys())
    if len(names) >= 2:
        for band in bands.keys():
            diff = all_results[names[1]][band] - all_results[names[0]][band]
            status = "OK" if abs(diff) < 6.0 else "WARN" if abs(diff) < 12.0 else "FAIL"
            print(f"    {band}: {diff:+.1f} dB [{status}]")

    print()

if __name__ == '__main__':
    test_noise_floor()
