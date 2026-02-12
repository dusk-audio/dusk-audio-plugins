#!/usr/bin/env python3
"""Test ESS (Exponential Sine Sweep) IR generation for all programs."""
import os
import sys
import time
import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'velvet90_match'))

from wcs_engine import WCSEngine, PROGRAM_NAMES
from ir_analysis import analyze_ir, measure_rt60

SR = 22050
IR_DURATION = 3.0  # seconds
SWEEP_DURATION = 3.0  # seconds
SWEEP_LEVEL = 0.1

BASELINE_COEFFICIENTS = np.array([
    0.45, 0.47, 0.50, 0.75,
    0.35, 0.50, 0.71, 0.55,
    0.45, 0.38, 0.73, 0.75,
    0.35, 0.15, 0.70, 0.39,
])

print("=" * 70)
print("ESS IR Generation Test")
print(f"SR={SR}Hz, Sweep={SWEEP_DURATION}s, IR={IR_DURATION}s")
print("=" * 70)

for prog_idx in range(6):
    engine = WCSEngine(program=prog_idx, sr=SR)

    t0 = time.time()
    try:
        ir = engine.generate_ir_ess(
            duration_s=IR_DURATION,
            coefficients=BASELINE_COEFFICIENTS,
            rolloff_hz=10000.0,
            sweep_duration_s=SWEEP_DURATION,
            sweep_level=SWEEP_LEVEL,
        )
    except Exception as e:
        print(f"\n--- Program {prog_idx}: {PROGRAM_NAMES[prog_idx]} --- ERROR: {e}")
        continue

    elapsed = time.time() - t0
    peak = np.max(np.abs(ir))

    print(f"\n--- Program {prog_idx}: {PROGRAM_NAMES[prog_idx]} --- ({elapsed:.1f}s)")
    print(f"  Peak: {peak:.6f}, Shape: {ir.shape}")

    if peak < 1e-8:
        print(f"  SILENT - no output")
        continue

    # Normalize and analyze
    ir_norm = ir / peak

    # Early energy (0-20ms)
    early_samp = int(0.020 * SR)
    early_peak = np.max(np.abs(ir_norm[:, :early_samp]))

    # Mid energy (100-500ms)
    mid_start = int(0.1 * SR)
    mid_end = int(0.5 * SR)
    mid_rms = np.sqrt(np.mean(ir_norm[:, mid_start:mid_end] ** 2))

    # Tail (last 1s)
    tail_start = int((IR_DURATION - 1.0) * SR)
    tail_rms = np.sqrt(np.mean(ir_norm[:, tail_start:] ** 2))

    # RT60
    mono = np.mean(ir_norm, axis=0)
    rt60 = measure_rt60(mono, SR, 'T30')

    print(f"  Early peak (0-20ms): {early_peak:.4f}")
    print(f"  Mid RMS (100-500ms): {mid_rms:.6f}")
    print(f"  Tail RMS (last 1s):  {tail_rms:.6f}")
    print(f"  RT60 (T30):          {rt60:.2f}s")

    # Full analysis
    profile = analyze_ir(ir_norm, SR, name=f"ESS_{PROGRAM_NAMES[prog_idx]}")
    print(f"  EDT:                 {profile.edt:.2f}s")
    print(f"  Pre-delay:           {profile.pre_delay_ms:.1f}ms")
    print(f"  Stereo correlation:  {profile.stereo_correlation:.3f}")
    print(f"  Width:               {profile.width_estimate:.3f}")

# Score against real IRs if available
IR_BASE = os.environ.get('SUEDE_IR_BASE')
if IR_BASE:
    from ir_analysis import load_ir
    from ir_compare import compare_profiles

    print(f"\n{'=' * 70}")
    print("Scoring ESS IRs against real hardware captures")
    print(f"{'=' * 70}")

    test_cases = [
        ('Hall 1_dc.wav', 0),
        ('Plate 7_dc.wav', 1),
        ('Chamber 1_dc.wav', 2),
    ]

    for ir_file, prog_idx in test_cases:
        ir_path = os.path.join(IR_BASE, ir_file)
        if not os.path.exists(ir_path):
            print(f"\n  {ir_file}: not found, skipping")
            continue

        # Load and analyze target
        target_data, target_sr = load_ir(ir_path)
        if target_sr != SR:
            from scipy.signal import resample
            n_new = int(target_data.shape[1] * SR / target_sr)
            target_data = np.array([resample(target_data[c], n_new)
                                    for c in range(target_data.shape[0])])
        peak_t = np.max(np.abs(target_data))
        if peak_t > 0:
            target_data = target_data / peak_t
        target_profile = analyze_ir(target_data, SR, name=ir_file)

        # Generate ESS IR
        engine = WCSEngine(program=prog_idx, sr=SR)
        duration = max(target_profile.rt60 * 3.0, 3.0)

        t0 = time.time()
        ir = engine.generate_ir_ess(
            duration_s=duration,
            coefficients=BASELINE_COEFFICIENTS,
            rolloff_hz=10000.0,
            sweep_duration_s=SWEEP_DURATION,
            sweep_level=SWEEP_LEVEL,
        )
        elapsed = time.time() - t0

        peak = np.max(np.abs(ir))
        if peak > 1e-6:
            ir_norm = ir / peak
        else:
            ir_norm = ir

        cand_profile = analyze_ir(ir_norm, SR, name=f"ESS_{PROGRAM_NAMES[prog_idx]}")
        result = compare_profiles(target_profile, cand_profile)

        print(f"\n--- {ir_file} → Program {prog_idx}: {PROGRAM_NAMES[prog_idx]} ({elapsed:.1f}s) ---")
        print(f"  Target: RT60={target_profile.rt60:.2f}s EDT={target_profile.edt:.2f}s "
              f"PreDly={target_profile.pre_delay_ms:.1f}ms")
        print(f"  ESS:    RT60={cand_profile.rt60:.2f}s EDT={cand_profile.edt:.2f}s "
              f"PreDly={cand_profile.pre_delay_ms:.1f}ms")
        print(f"  Score: {result.overall_score:.1f}/100")
        print(f"    RT60={result.rt60_score:.0f} BandRT={result.band_rt60_score:.0f} "
              f"EDC={result.edc_shape_score:.0f} EDT={result.edt_score:.0f} "
              f"Stereo={result.stereo_score:.0f} PreDly={result.pre_delay_score:.0f} "
              f"SpEarly={result.spectral_early_score:.0f} SpLate={result.spectral_late_score:.0f}")
