#!/usr/bin/env python3
"""Test ESS with corrected OP/ sign handling and coefficient scaling."""
import os
import sys
import time
import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'velvet90_match'))

from wcs_engine import WCSEngine, PROGRAM_NAMES
from ir_analysis import analyze_ir, measure_rt60

SR = 22050
IR_DURATION = 4.0
SWEEP_DURATION = 4.0
SWEEP_LEVEL = 0.05  # lower to avoid non-linearity

BASELINE_COEFFICIENTS = np.array([
    0.45, 0.47, 0.50, 0.75,
    0.35, 0.50, 0.71, 0.55,
    0.45, 0.38, 0.73, 0.75,
    0.35, 0.15, 0.70, 0.39,
])

print("=" * 70)
print("ESS with OP/ sign handling — coefficient scaling test")
print("=" * 70)

# Test with different coefficient scales for Concert Hall
for scale in [0.3, 0.5, 0.7, 0.8, 0.9, 1.0]:
    coeffs = BASELINE_COEFFICIENTS * scale
    engine = WCSEngine(program=0, sr=SR)

    t0 = time.time()
    try:
        ir = engine.generate_ir_ess(
            duration_s=IR_DURATION,
            coefficients=coeffs,
            rolloff_hz=10000.0,
            sweep_duration_s=SWEEP_DURATION,
            sweep_level=SWEEP_LEVEL,
        )
    except Exception as e:
        print(f"  scale={scale:.1f}: ERROR {e}")
        continue

    elapsed = time.time() - t0
    peak = np.max(np.abs(ir))

    if peak < 1e-8:
        print(f"  scale={scale:.1f}: SILENT")
        continue

    ir_norm = ir / peak
    mono = np.mean(ir_norm, axis=0)
    rt60 = measure_rt60(mono, SR, 'T30')

    early_samp = int(0.020 * SR)
    early_peak = np.max(np.abs(ir_norm[:, :early_samp]))

    profile = analyze_ir(ir_norm, SR, name=f"scale{scale}")

    print(f"  scale={scale:.1f}: peak={peak:.4f} RT60={rt60:.2f}s EDT={profile.edt:.2f}s "
          f"PreDly={profile.pre_delay_ms:.1f}ms early={early_peak:.4f} "
          f"stereo_corr={profile.stereo_correlation:.3f} ({elapsed:.1f}s)")

# Test all programs at scale=0.7
print(f"\n{'=' * 70}")
print("All programs at scale=0.7")
print("=" * 70)

coeffs_07 = BASELINE_COEFFICIENTS * 0.7
for prog_idx in range(6):
    engine = WCSEngine(program=prog_idx, sr=SR)
    try:
        ir = engine.generate_ir_ess(
            duration_s=IR_DURATION,
            coefficients=coeffs_07,
            rolloff_hz=10000.0,
            sweep_duration_s=SWEEP_DURATION,
            sweep_level=SWEEP_LEVEL,
        )
    except Exception as e:
        print(f"  P{prog_idx} {PROGRAM_NAMES[prog_idx]}: ERROR {e}")
        continue

    peak = np.max(np.abs(ir))
    if peak < 1e-8:
        print(f"  P{prog_idx} {PROGRAM_NAMES[prog_idx]}: SILENT")
        continue

    ir_norm = ir / peak
    mono = np.mean(ir_norm, axis=0)
    rt60 = measure_rt60(mono, SR, 'T30')
    profile = analyze_ir(ir_norm, SR, name=f"P{prog_idx}")
    print(f"  P{prog_idx} {PROGRAM_NAMES[prog_idx]:15s}: peak={peak:.4f} "
          f"RT60={rt60:.2f}s EDT={profile.edt:.2f}s "
          f"PreDly={profile.pre_delay_ms:.1f}ms "
          f"corr={profile.stereo_correlation:.3f} width={profile.width_estimate:.3f}")

# Score against real IRs with scale=0.7
IR_BASE = os.environ.get('SUEDE_IR_BASE')
if IR_BASE:
    from ir_analysis import load_ir
    from ir_compare import compare_profiles

    print(f"\n{'=' * 70}")
    print("Scoring against real IRs (scale=0.7)")
    print("=" * 70)

    test_cases = [
        ('Hall 1_dc.wav', 0),
        ('Plate 7_dc.wav', 1),
        ('Chamber 1_dc.wav', 2),
    ]

    for ir_file, prog_idx in test_cases:
        ir_path = os.path.join(IR_BASE, ir_file)
        if not os.path.exists(ir_path):
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

        engine = WCSEngine(program=prog_idx, sr=SR)
        duration = max(target_profile.rt60 * 3.0, 3.0)

        ir = engine.generate_ir_ess(
            duration_s=duration,
            coefficients=coeffs_07,
            rolloff_hz=10000.0,
            sweep_duration_s=SWEEP_DURATION,
            sweep_level=SWEEP_LEVEL,
        )
        peak = np.max(np.abs(ir))
        if peak > 1e-6:
            ir_norm = ir / peak
        else:
            ir_norm = ir

        cand_profile = analyze_ir(ir_norm, SR, name=f"ESS_{PROGRAM_NAMES[prog_idx]}")
        result = compare_profiles(target_profile, cand_profile)

        print(f"\n  {ir_file} → P{prog_idx} {PROGRAM_NAMES[prog_idx]}")
        print(f"  Target: RT60={target_profile.rt60:.2f}s EDT={target_profile.edt:.2f}s")
        print(f"  ESS:    RT60={cand_profile.rt60:.2f}s EDT={cand_profile.edt:.2f}s")
        print(f"  Score: {result.overall_score:.1f}/100")
        print(f"    RT60={result.rt60_score:.0f} BandRT={result.band_rt60_score:.0f} "
              f"EDC={result.edc_shape_score:.0f} EDT={result.edt_score:.0f} "
              f"Stereo={result.stereo_score:.0f} PreDly={result.pre_delay_score:.0f}")
