#!/usr/bin/env python3
"""
Test TapeMachine plugin frequency response.
Generates sine waves at many frequencies and measures the output level
to identify where signal loss occurs.
"""

import numpy as np
import sys
import os

try:
    from pedalboard import load_plugin
    HAS_PEDALBOARD = True
except ImportError:
    HAS_PEDALBOARD = False
    print("pedalboard not available")
    sys.exit(1)

SAMPLE_RATE = 44100
DURATION_SEC = 2.0
LEVEL_DBFS = -18.0

def discover_au_path():
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

def measure_level(signal, freq_hz, sample_rate):
    """Measure RMS level at the fundamental frequency using FFT."""
    n = len(signal)
    window = np.hanning(n)
    windowed = signal * window
    fft = np.fft.rfft(windowed)
    freqs = np.fft.rfftfreq(n, 1.0 / sample_rate)
    magnitudes = np.abs(fft) * 2.0 / np.sum(window)

    # Find peak near fundamental
    freq_resolution = sample_rate / n
    bin_idx = int(round(freq_hz / freq_resolution))
    start = max(0, bin_idx - 3)
    end = min(len(magnitudes), bin_idx + 4)
    peak_mag = np.max(magnitudes[start:end])
    return 20.0 * np.log10(max(peak_mag, 1e-10))

def test_frequency_response():
    au_path = discover_au_path()
    if au_path is None:
        print("TapeMachine AU not found")
        sys.exit(1)

    print(f"Loading: {au_path}")
    plugin = load_plugin(au_path)

    # Print available parameters
    print(f"\nAvailable parameters:")
    for param in plugin.parameters.keys():
        print(f"  {param}")

    # Test frequencies from 100 Hz to 18 kHz
    test_freqs = [100, 200, 300, 500, 700, 1000, 1500, 2000, 3000, 4000,
                  5000, 6000, 7000, 7500, 7600, 8000, 8500, 9000, 10000,
                  11000, 12000, 13000, 14000, 15000, 16000, 17000, 18000]

    print(f"\nSettings: Swiss 800, Type 456, 30 IPS, NAB, Input=0dB, Noise OFF")
    print(f"Test level: {LEVEL_DBFS} dBFS")
    print(f"{'='*70}")

    # Configure plugin
    plugin.reset()

    def set_plugin_params(p):
        """Set all plugin parameters."""
        try: p.tape_machine = 'Swiss 800'
        except: pass
        try: p.tape_speed = '30 IPS'
        except: pass
        try: p.tape_type = 'Type 456'
        except: pass
        try: p.eq_standard = 'NAB'
        except: pass
        try: p.oversampling = '1x'
        except: pass
        try: p.signal_path = 'Repro'
        except: pass
        try: p.input_gain = 0.0  # 0 dB
        except: pass
        try: p.output_gain = 0.0  # 0 dB
        except: pass
        try: p.noise_enabled = False
        except: pass
        try: p.noise_amount = 0.0
        except: pass
        try: p.wow = 0.0
        except: pass
        try: p.flutter = 0.0
        except: pass
        try: p.mix = 100.0  # 100% wet (percentage parameter!)
        except: pass
        try: p.saturation = 4.0  # Default
        except: pass
        try: p.auto_compensation = True
        except: pass
        try: p.lowpass_frequency = 20000.0  # Bypass (>= 19000)
        except: pass

    set_plugin_params(plugin)

    print(f"\n{'Freq (Hz)':>10} | {'Input (dB)':>10} | {'Output (dB)':>11} | {'Gain (dB)':>10} | {'Status'}")
    print(f"{'-'*70}")

    results = []
    for freq in test_freqs:
        # Generate test signal
        test_signal = generate_sine(freq, LEVEL_DBFS, DURATION_SEC, SAMPLE_RATE)
        stereo_signal = np.stack([test_signal, test_signal])

        # Reset plugin state for each frequency
        plugin.reset()
        set_plugin_params(plugin)

        # Process
        output = plugin.process(stereo_signal, SAMPLE_RATE)

        # Skip first 0.5s for settling
        skip_samples = int(0.5 * SAMPLE_RATE)
        analysis_signal = output[0, skip_samples:]

        # Measure input and output at fundamental
        input_level = measure_level(test_signal[skip_samples:], freq, SAMPLE_RATE)
        output_level = measure_level(analysis_signal, freq, SAMPLE_RATE)
        gain = output_level - input_level

        status = "OK" if abs(gain) < 3.0 else "WARN" if abs(gain) < 6.0 else "PROBLEM"
        if gain < -10.0:
            status = "!! GONE !!"

        print(f"{freq:>10} | {input_level:>10.1f} | {output_level:>11.1f} | {gain:>+10.1f} | {status}")
        results.append((freq, input_level, output_level, gain))

    # Summary
    print(f"\n{'='*70}")
    print("SUMMARY:")
    max_loss = min(r[3] for r in results)
    max_loss_freq = [r[0] for r in results if r[3] == max_loss][0]
    print(f"  Maximum loss: {max_loss:+.1f} dB at {max_loss_freq} Hz")

    problem_freqs = [(r[0], r[3]) for r in results if r[3] < -3.0]
    if problem_freqs:
        print(f"  Frequencies with >3dB loss:")
        for f, g in problem_freqs:
            print(f"    {f} Hz: {g:+.1f} dB")
    else:
        print(f"  No frequencies with >3dB loss detected")

    # Also measure overall RMS
    print(f"\n  Overall RMS levels at each test frequency:")
    for freq, inp, out, gain in results:
        if abs(gain) > 2.0:
            print(f"    {freq:>6} Hz: {gain:+.1f} dB {'<< SIGNIFICANT' if abs(gain) > 6 else ''}")

def test_sweep_no_reset():
    """Test without resetting between frequencies (closer to real-world usage)."""
    au_path = discover_au_path()
    if au_path is None:
        print("TapeMachine AU not found")
        sys.exit(1)

    print(f"\nLoading: {au_path}")
    plugin = load_plugin(au_path)

    print(f"\n{'='*70}")
    print("SWEEP TEST (no reset between frequencies)")
    print(f"{'='*70}")

    # Configure once
    try: plugin.tape_machine = 'Swiss 800'
    except: pass
    try: plugin.tape_speed = '30 IPS'
    except: pass
    try: plugin.tape_type = 'Type 456'
    except: pass
    try: plugin.oversampling = '1x'
    except: pass
    try: plugin.signal_path = 'Repro'
    except: pass
    try: plugin.input_gain = 0.0
    except: pass
    try: plugin.output_gain = 0.0
    except: pass
    try: plugin.noise_enabled = False
    except: pass
    try: plugin.wow = 0.0
    except: pass
    try: plugin.flutter = 0.0
    except: pass
    try: plugin.mix = 100.0  # 100% wet
    except: pass
    try: plugin.auto_compensation = True
    except: pass
    try: plugin.lowpass_frequency = 20000.0
    except: pass

    test_freqs = [500, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 7500, 7600,
                  8000, 9000, 10000, 12000, 15000, 18000, 20000]

    print(f"\n{'Freq':>6} | {'Gain':>8} | {'Peak dBFS':>10} | {'RMS dBFS':>10}")
    print(f"{'-'*50}")

    for freq in test_freqs:
        test_signal = generate_sine(freq, LEVEL_DBFS, 1.0, SAMPLE_RATE)
        stereo_signal = np.stack([test_signal, test_signal])

        # Process WITHOUT reset
        output = plugin.process(stereo_signal, SAMPLE_RATE)

        # Use last 0.5s for measurement
        skip = int(0.5 * SAMPLE_RATE)
        analysis = output[0, skip:]

        out_level = measure_level(analysis, freq, SAMPLE_RATE)
        rms = 20.0 * np.log10(max(np.sqrt(np.mean(analysis**2)), 1e-10))
        gain = out_level - LEVEL_DBFS

        print(f"{freq:>6} | {gain:>+7.1f}dB | {out_level:>9.1f} | {rms:>9.1f}")


def test_lowpass_effect():
    """Test whether the lowpass_frequency parameter is actually applied."""
    au_path = discover_au_path()
    if au_path is None:
        print("TapeMachine AU not found")
        sys.exit(1)

    print(f"\nLoading: {au_path}")
    plugin = load_plugin(au_path)

    print(f"\n{'='*70}")
    print("LOWPASS FILTER TEST: Measuring response with different lowpass settings")
    print(f"{'='*70}")

    # Check what valid values the lowpass_frequency param accepts
    param = plugin.parameters.get('lowpass_frequency')
    if param:
        print(f"  lowpass_frequency range: {param.min_value} - {param.max_value}")
        print(f"  Current value: {plugin.lowpass_frequency}")

    test_freqs = [1000, 3000, 5000, 7000, 7600, 10000, 12000, 15000, 18000]
    lowpass_settings = [5000, 8000, 15000, 20000]

    print(f"\n{'Freq':>6} | ", end="")
    for lp in lowpass_settings:
        print(f"{'LP='+str(lp):>10} | ", end="")
    print()
    print(f"{'-'*70}")

    for freq in test_freqs:
        test_signal = generate_sine(freq, LEVEL_DBFS, 2.0, SAMPLE_RATE)
        stereo_signal = np.stack([test_signal, test_signal])
        skip_samples = int(0.5 * SAMPLE_RATE)
        input_level = measure_level(test_signal[skip_samples:], freq, SAMPLE_RATE)

        print(f"{freq:>6} | ", end="")
        for lp in lowpass_settings:
            plugin.reset()
            try: plugin.tape_machine = 'Swiss 800'
            except: pass
            try: plugin.tape_speed = '30 IPS'
            except: pass
            try: plugin.tape_type = 'Type 456'
            except: pass
            try: plugin.oversampling = '1x'
            except: pass
            try: plugin.input_gain = 0.5
            except: pass
            try: plugin.output_gain = 0.5
            except: pass
            try: plugin.noise_enabled = 0.0
            except: pass
            try: plugin.wow = 0.0
            except: pass
            try: plugin.flutter = 0.0
            except: pass
            try: plugin.mix = 1.0
            except: pass
            try: plugin.auto_compensation = 1.0
            except: pass
            try: plugin.lowpass_frequency = float(lp)
            except Exception as e:
                print(f"ERROR: {e}")
                continue

            output = plugin.process(stereo_signal, SAMPLE_RATE)
            analysis_signal = output[0, skip_samples:]
            output_level = measure_level(analysis_signal, freq, SAMPLE_RATE)
            gain = output_level - input_level
            print(f"{gain:>+9.1f}dB | ", end="")
        print()


def test_4x_oversampling():
    """Test frequency response at 4x oversampling (the default)."""
    au_path = discover_au_path()
    if au_path is None:
        print("TapeMachine AU not found")
        sys.exit(1)

    print(f"\nLoading: {au_path}")
    plugin = load_plugin(au_path)

    print(f"\n{'='*70}")
    print("4x OVERSAMPLING FREQUENCY RESPONSE")
    print(f"{'='*70}")

    test_freqs = [500, 1000, 2000, 3000, 5000, 7000, 7600, 8000,
                  10000, 12000, 15000, 18000, 20000]

    print(f"\n{'Freq':>6} | {'1x Gain':>8} | {'4x Gain':>8} | {'Diff':>6}")
    print(f"{'-'*50}")

    for freq in test_freqs:
        test_signal = generate_sine(freq, LEVEL_DBFS, 2.0, SAMPLE_RATE)
        stereo_signal = np.stack([test_signal, test_signal])
        skip = int(0.5 * SAMPLE_RATE)
        input_level = measure_level(test_signal[skip:], freq, SAMPLE_RATE)

        gains = {}
        for os_setting in ['1x', '4x']:
            plugin.reset()
            try: plugin.tape_machine = 'Swiss 800'
            except: pass
            try: plugin.tape_speed = '30 IPS'
            except: pass
            try: plugin.tape_type = 'Type 456'
            except: pass
            try: plugin.oversampling = os_setting
            except: pass
            try: plugin.signal_path = 'Repro'
            except: pass
            try: plugin.input_gain = 0.0
            except: pass
            try: plugin.output_gain = 0.0
            except: pass
            try: plugin.noise_enabled = False
            except: pass
            try: plugin.wow = 0.0
            except: pass
            try: plugin.flutter = 0.0
            except: pass
            try: plugin.mix = 100.0  # 100% wet
            except: pass
            try: plugin.auto_compensation = True
            except: pass
            try: plugin.lowpass_frequency = 20000.0
            except: pass

            output = plugin.process(stereo_signal, SAMPLE_RATE)
            analysis = output[0, skip:]
            out_level = measure_level(analysis, freq, SAMPLE_RATE)
            gains[os_setting] = out_level - input_level

        diff = gains.get('4x', 0) - gains.get('1x', 0)
        print(f"{freq:>6} | {gains.get('1x', 0):>+7.1f}dB | {gains.get('4x', 0):>+7.1f}dB | {diff:>+5.1f}dB")


if __name__ == '__main__':
    test_frequency_response()
    test_sweep_no_reset()
    test_4x_oversampling()
    test_lowpass_effect()
