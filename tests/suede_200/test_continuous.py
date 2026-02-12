#!/usr/bin/env python3
"""Test WCS engine with continuous noise input to verify it produces reverb."""
import os
import sys
import ctypes
import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
from wcs_engine import WCSEngine, PROGRAM_NAMES, MICROCODE, _native_lib

SR = 22050
DURATION = 6.0  # seconds — enough for 2 buffer revolutions

BASELINE_COEFFICIENTS = np.array([
    0.45, 0.47, 0.50, 0.75,
    0.35, 0.50, 0.71, 0.55,
    0.45, 0.38, 0.73, 0.75,
    0.35, 0.15, 0.70, 0.39,
])

n_samples = int(SR * DURATION)

print("=" * 70)
print("Continuous Noise Input Test")
print(f"SR={SR}Hz, Duration={DURATION}s, Samples={n_samples}")
print("=" * 70)

# Generate test signals
rng = np.random.RandomState(42)
noise = rng.randn(n_samples) * 0.1  # -20dBFS white noise

for prog_idx in range(6):
    # Process through C engine
    microcode = (ctypes.c_uint * 128)(*MICROCODE[prog_idx])
    coeffs = (ctypes.c_double * 16)(*BASELINE_COEFFICIENTS)
    inp_l = (ctypes.c_double * n_samples)(*noise)
    inp_r = (ctypes.c_double * n_samples)(*noise)
    out_l = (ctypes.c_double * n_samples)()
    out_r = (ctypes.c_double * n_samples)()

    _native_lib.wcs_process_signal(
        microcode, coeffs,
        ctypes.c_double(float(SR)),
        ctypes.c_int(n_samples),
        ctypes.c_double(10000.0),
        inp_l, inp_r, out_l, out_r
    )

    output_l = np.frombuffer(out_l, dtype=np.float64).copy()
    output_r = np.frombuffer(out_r, dtype=np.float64).copy()

    peak = max(np.max(np.abs(output_l)), np.max(np.abs(output_r)))

    # Analyze output in time windows
    windows = [
        ("0-0.5s", 0, int(0.5 * SR)),
        ("0.5-1s", int(0.5 * SR), int(1.0 * SR)),
        ("1-2s", int(1.0 * SR), int(2.0 * SR)),
        ("2-3s", int(2.0 * SR), int(3.0 * SR)),
        ("3-4s", int(3.0 * SR), int(4.0 * SR)),
        ("4-5s", int(4.0 * SR), int(5.0 * SR)),
        ("5-6s", int(5.0 * SR), int(6.0 * SR)),
    ]

    print(f"\n--- Program {prog_idx}: {PROGRAM_NAMES[prog_idx]} ---")
    print(f"  Overall peak: {peak:.6f}")

    if peak < 1e-10:
        print(f"  COMPLETELY SILENT")
        continue

    print(f"  {'Window':10s} {'L_rms':>10s} {'R_rms':>10s} {'L_peak':>10s} {'R_peak':>10s}")
    for label, start, end in windows:
        l_seg = output_l[start:end]
        r_seg = output_r[start:end]
        l_rms = np.sqrt(np.mean(l_seg ** 2))
        r_rms = np.sqrt(np.mean(r_seg ** 2))
        l_peak = np.max(np.abs(l_seg))
        r_peak = np.max(np.abs(r_seg))
        print(f"  {label:10s} {l_rms:10.6f} {r_rms:10.6f} {l_peak:10.6f} {r_peak:10.6f}")

    # Also test: stop input after 3s, observe tail decay
    input_stop = np.zeros(n_samples, dtype=np.float64)
    input_stop[:int(3.0 * SR)] = noise[:int(3.0 * SR)]

    inp_l2 = (ctypes.c_double * n_samples)(*input_stop)
    inp_r2 = (ctypes.c_double * n_samples)(*input_stop)
    out_l2 = (ctypes.c_double * n_samples)()
    out_r2 = (ctypes.c_double * n_samples)()

    _native_lib.wcs_process_signal(
        microcode, coeffs,
        ctypes.c_double(float(SR)),
        ctypes.c_int(n_samples),
        ctypes.c_double(10000.0),
        inp_l2, inp_r2, out_l2, out_r2
    )

    tail_l = np.frombuffer(out_l2, dtype=np.float64).copy()
    tail_r = np.frombuffer(out_r2, dtype=np.float64).copy()

    print(f"\n  With input stopping at 3s:")
    for label, start, end in windows:
        l_seg = tail_l[start:end]
        r_seg = tail_r[start:end]
        l_rms = np.sqrt(np.mean(l_seg ** 2))
        r_rms = np.sqrt(np.mean(r_seg ** 2))
        print(f"  {label:10s} L_rms={l_rms:.6f} R_rms={r_rms:.6f}")
