#!/usr/bin/env python3
"""
Debug test: Verify whether the TapeMachine lowpass filter actually works.
"""

import numpy as np
import sys
import os

try:
    from pedalboard import load_plugin
except ImportError:
    print("pedalboard not available")
    sys.exit(1)

SAMPLE_RATE = 44100

def discover_au_path():
    home = os.path.expanduser("~")
    candidate = os.path.join(home, "Library/Audio/Plug-Ins/Components/TapeMachine.component")
    if os.path.exists(candidate):
        return candidate
    return None

au_path = discover_au_path()
if au_path is None:
    print("TapeMachine AU not found")
    sys.exit(1)

print(f"Loading: {au_path}")
plugin = load_plugin(au_path)

# Print all parameter values
print("\nDefault parameter values:")
for name in plugin.parameters.keys():
    try:
        val = getattr(plugin, name)
        print(f"  {name} = {val}")
    except:
        print(f"  {name} = <error reading>")

# Set up parameters
print("\nSetting parameters...")
plugin.tape_machine = 'Swiss 800'
plugin.tape_speed = '30 IPS'
plugin.tape_type = 'Type 456'
plugin.signal_path = 'Repro'
plugin.oversampling = '1x'
plugin.input_gain = 0.0     # 0 dB
plugin.output_gain = 0.0    # 0 dB
plugin.noise_enabled = False
plugin.wow = 0.0
plugin.flutter = 0.0
plugin.mix = 100.0           # 100% wet (percentage, not 0-1!)
plugin.auto_compensation = True
plugin.lowpass_frequency = 5000.0  # Set lowpass to 5 kHz

print("\nAfter setting parameters:")
print(f"  lowpass_frequency = {plugin.lowpass_frequency}")
print(f"  signal_path = {plugin.signal_path}")
print(f"  oversampling = {plugin.oversampling}")
print(f"  input_gain = {plugin.input_gain}")
print(f"  output_gain = {plugin.output_gain}")
print(f"  auto_compensation = {plugin.auto_compensation}")

# Step 1: Process a dummy buffer to trigger parameter updates in processBlock
print("\nProcessing dummy buffer (1024 samples) to trigger internal parameter updates...")
dummy = np.zeros((2, 1024), dtype=np.float32)
_ = plugin.process(dummy, SAMPLE_RATE)

# Step 2: Now process a 10 kHz sine (well above the 5 kHz cutoff)
print("Processing 10 kHz sine at -12 dBFS...")
duration = 2.0
n_samples = int(duration * SAMPLE_RATE)
t = np.arange(n_samples) / SAMPLE_RATE
amplitude = 10.0 ** (-12.0 / 20.0)  # -12 dBFS
test_signal = (amplitude * np.sin(2.0 * np.pi * 10000.0 * t)).astype(np.float32)
stereo = np.stack([test_signal, test_signal])

output = plugin.process(stereo, SAMPLE_RATE)

# Measure output level at 10 kHz
skip = int(0.5 * SAMPLE_RATE)
analysis = output[0, skip:]

# FFT measurement
window = np.hanning(len(analysis))
fft = np.fft.rfft(analysis * window)
freqs = np.fft.rfftfreq(len(analysis), 1.0 / SAMPLE_RATE)
magnitudes = np.abs(fft) * 2.0 / np.sum(window)

# Find peak near 10 kHz
target_bin = int(round(10000.0 * len(analysis) / SAMPLE_RATE))
peak_mag = np.max(magnitudes[max(0, target_bin-3):target_bin+4])
output_db = 20.0 * np.log10(max(peak_mag, 1e-10))

# Expected: With 5kHz 2nd-order lowpass, 10kHz should be ~-12 dB down
# Total expected: -12 dBFS (input) - 12 dB (filter) = -24 dBFS
print(f"\nResults:")
print(f"  Input level: -12.0 dBFS at 10 kHz")
print(f"  Output level: {output_db:.1f} dBFS at 10 kHz")
print(f"  Gain: {output_db - (-12.0):+.1f} dB")
print(f"  Expected gain with LP@5kHz: ~-12 dB (2nd order Butterworth)")

if abs(output_db - (-12.0)) < 1.0:
    print(f"\n  ** LOWPASS FILTER IS NOT WORKING! ** Signal passes at unity.")
elif output_db < -20.0:
    print(f"\n  Lowpass filter IS working (signal attenuated as expected).")
else:
    print(f"\n  Partial attenuation detected. Filter may be partially working.")

# Also test with 1 kHz (below cutoff)
print("\n\nProcessing 1 kHz sine at -12 dBFS (should pass through)...")
test_1k = (amplitude * np.sin(2.0 * np.pi * 1000.0 * t)).astype(np.float32)
stereo_1k = np.stack([test_1k, test_1k])
output_1k = plugin.process(stereo_1k, SAMPLE_RATE)
analysis_1k = output_1k[0, skip:]
fft_1k = np.fft.rfft(analysis_1k * window)
mags_1k = np.abs(fft_1k) * 2.0 / np.sum(window)
target_1k = int(round(1000.0 * len(analysis_1k) / SAMPLE_RATE))
peak_1k = np.max(mags_1k[max(0, target_1k-3):target_1k+4])
out_1k_db = 20.0 * np.log10(max(peak_1k, 1e-10))
print(f"  1 kHz output: {out_1k_db:.1f} dBFS (expected: ~-12.0 dBFS)")
print(f"  1 kHz gain: {out_1k_db - (-12.0):+.1f} dB")
