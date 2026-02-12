#!/usr/bin/env python3
"""Test ESS IR generation against real Lexicon 200 hardware captures.
Tests multiple coefficient scales and damping values to find best match."""
import os, sys, time
import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'velvet90_match'))

from wcs_engine import WCSEngine, PROGRAM_NAMES
from ir_analysis import analyze_ir, measure_rt60, load_ir
from ir_compare import compare_profiles

IR_BASE = os.environ.get('SUEDE_IR_BASE', os.path.join(os.path.dirname(__file__), 'fixtures', 'lexicon_200_irs'))SR = 22050
SWEEP_DURATION = 4.0
SWEEP_LEVEL = 0.05

BASELINE_COEFFICIENTS = np.array([
    0.45, 0.47, 0.50, 0.75,
    0.35, 0.50, 0.71, 0.55,
    0.45, 0.38, 0.73, 0.75,
    0.35, 0.15, 0.70, 0.39,
])

test_cases = [
    ('Hall 1_dc.wav', 0, 'Concert Hall'),
    ('Plate 7_dc.wav', 1, 'Plate'),
    ('Chamber 1_dc.wav', 2, 'Chamber'),
]

print("=" * 70)
print("ESS vs Real Hardware — Q15 + Fixed I/O")
print("=" * 70)

# First verify ESS stability and stereo
print("\n--- Quick stereo check ---")
for prog_idx in range(3):
    engine = WCSEngine(program=prog_idx, sr=SR)
    ir = engine.generate_ir_ess(
        duration_s=3.0, coefficients=BASELINE_COEFFICIENTS * 0.7,
        rolloff_hz=10000.0, sweep_duration_s=SWEEP_DURATION,
        sweep_level=SWEEP_LEVEL, damping=0.95,
    )
    peak = np.max(np.abs(ir))
    if peak > 1e-8:
        corr = np.corrcoef(ir[0], ir[1])[0, 1]
        print(f"  P{prog_idx} {PROGRAM_NAMES[prog_idx]:15s}: peak={peak:.4f} L/R_corr={corr:.3f}")
    else:
        print(f"  P{prog_idx} {PROGRAM_NAMES[prog_idx]:15s}: SILENT")

# Score against real IRs with grid search over scale and damping
print(f"\n{'=' * 70}")
print("Grid search: coefficient scale × damping")
print("=" * 70)

for ir_file, prog_idx, name in test_cases:
    ir_path = os.path.join(IR_BASE, ir_file)
    if not os.path.exists(ir_path):
        print(f"\n  {ir_file}: not found")
        continue

    # Load target IR
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

    print(f"\n  {ir_file} → P{prog_idx} {name}")
    print(f"  Target: RT60={target_profile.rt60:.2f}s EDT={target_profile.edt:.2f}s "
          f"PreDly={target_profile.pre_delay_ms:.1f}ms "
          f"Stereo={target_profile.stereo_correlation:.3f}")

    best_score = 0
    best_params = None

    for scale in [0.3, 0.5, 0.7, 0.9]:
        for damp in [0.90, 0.95, 0.98, 1.0]:
            coeffs = BASELINE_COEFFICIENTS * scale
            engine = WCSEngine(program=prog_idx, sr=SR)
            duration = max(target_profile.rt60 * 3.0, 4.0)

            try:
                ir = engine.generate_ir_ess(
                    duration_s=duration,
                    coefficients=coeffs,
                    rolloff_hz=10000.0,
                    sweep_duration_s=SWEEP_DURATION,
                    sweep_level=SWEEP_LEVEL,
                    damping=damp,
                )
            except Exception as e:
                continue

            peak = np.max(np.abs(ir))
            if peak < 1e-6:
                continue
            ir_norm = ir / peak

            cand_profile = analyze_ir(ir_norm, SR, name=f"s{scale}_d{damp}")
            result = compare_profiles(target_profile, cand_profile)

            if result.overall_score > best_score:
                best_score = result.overall_score
                best_params = (scale, damp, cand_profile, result)

            print(f"    scale={scale:.1f} damp={damp:.2f}: RT60={cand_profile.rt60:.2f}s "
                  f"EDT={cand_profile.edt:.2f}s score={result.overall_score:.1f}")

    if best_params:
        scale, damp, cp, res = best_params
        print(f"\n  BEST: scale={scale:.1f} damp={damp:.2f} → score={res.overall_score:.1f}/100")
        print(f"    RT60={res.rt60_score:.0f} BandRT={res.band_rt60_score:.0f} "
              f"EDC={res.edc_shape_score:.0f} EDT={res.edt_score:.0f} "
              f"Stereo={res.stereo_score:.0f} PreDly={res.pre_delay_score:.0f}")
