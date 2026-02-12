#!/usr/bin/env python3
"""Quick diagnostic: test wcs_generate_ir with direct-path delay + memory seeding."""
import os
import sys
import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'velvet90_match'))

from wcs_engine import WCSEngine, PROGRAM_NAMES
from ir_analysis import analyze_ir, profile_summary, measure_rt60

SR = 22050
DURATION = 4.0

BASELINE_COEFFICIENTS = np.array([
    0.45, 0.47, 0.50, 0.75,
    0.35, 0.50, 0.71, 0.55,
    0.45, 0.38, 0.73, 0.75,
    0.35, 0.15, 0.70, 0.39,
])

print("=" * 70)
print("Direct-Path Delay + Memory Seeding Diagnostic")
print("=" * 70)

for prog_idx in range(6):
    engine = WCSEngine(program=prog_idx, sr=SR)
    print(f"\n--- Program {prog_idx}: {PROGRAM_NAMES[prog_idx]} ---")
    print(f"  Output steps: L={engine.output_step_l}, R={engine.output_step_r}")

    for ig in [0.0, 0.5, 1.0, 2.0, 5.0]:
        ir = engine.generate_ir(
            duration_s=DURATION,
            coefficients=BASELINE_COEFFICIENTS,
            rolloff_hz=10000.0,
            inject_gain=ig,
        )
        peak = np.max(np.abs(ir))
        rms = np.sqrt(np.mean(ir ** 2))

        # Check first 20ms for early energy
        early_samples = int(0.020 * SR)
        early_peak = np.max(np.abs(ir[:, :early_samples])) if early_samples > 0 else 0
        # Check 100-500ms for reverb buildup
        mid_start = int(0.1 * SR)
        mid_end = int(0.5 * SR)
        mid_rms = np.sqrt(np.mean(ir[:, mid_start:mid_end] ** 2)) if mid_end <= ir.shape[1] else 0
        # Check last 1s for tail
        tail_start = max(0, int((DURATION - 1.0) * SR))
        tail_rms = np.sqrt(np.mean(ir[:, tail_start:] ** 2)) if tail_start < ir.shape[1] else 0
        # Quick RT60 from mono mix
        mono = np.mean(ir, axis=0)
        if peak > 1e-6:
            rt60 = measure_rt60(mono / np.max(np.abs(mono)), SR, 'T30')
        else:
            rt60 = 0.0

        print(f"  ig={ig:4.1f}: peak={peak:.4f} early={early_peak:.4f} "
              f"mid_rms={mid_rms:.6f} tail_rms={tail_rms:.6f} RT60={rt60:.2f}s")

# Also score against a real IR if available
IR_BASE = os.environ.get('SUEDE_IR_BASE')
if IR_BASE:
    from ir_analysis import load_ir
    from ir_compare import compare_profiles

    print(f"\n{'=' * 70}")
    print("Scoring against real IRs")
    print(f"{'=' * 70}")

    test_cases = [
        ('Plate 7_dc.wav', 1),
        ('Hall 1_dc.wav', 0),
    ]

    for ir_file, prog_idx in test_cases:
        ir_path = os.path.join(IR_BASE, ir_file)
        if not os.path.exists(ir_path):
            print(f"  {ir_file}: not found")
            continue

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

        print(f"\n--- {ir_file} (Program {prog_idx}: {PROGRAM_NAMES[prog_idx]}) ---")
        print(f"  Target RT60={target_profile.rt60:.2f}s EDT={target_profile.edt:.2f}s")

        engine = WCSEngine(program=prog_idx, sr=SR)
        for ig in [0.0, 0.5, 1.0, 2.0, 5.0]:
            ir = engine.generate_ir(
                duration_s=max(target_profile.rt60 * 3.0, 3.0),
                coefficients=BASELINE_COEFFICIENTS,
                rolloff_hz=10000.0,
                inject_gain=ig,
            )
            peak = np.max(np.abs(ir))
            if peak > 1e-6:
                ir_norm = ir / peak
            else:
                ir_norm = ir
            cand_profile = analyze_ir(ir_norm, SR, name=f"WCS_ig{ig}")
            result = compare_profiles(target_profile, cand_profile)
            print(f"  ig={ig:4.1f}: score={result.overall_score:.1f} "
                  f"RT60={result.rt60_score:.0f}({cand_profile.rt60:.2f}s) "
                  f"BandRT={result.band_rt60_score:.0f} "
                  f"EDC={result.edc_shape_score:.0f} "
                  f"EDT={result.edt_score:.0f} "
                  f"Stereo={result.stereo_score:.0f} "
                  f"PreDly={result.pre_delay_score:.0f} "
                  f"peak={peak:.4f}")
