#!/usr/bin/env python3
"""Test actual decay behavior with different coefficient approaches."""
import os, sys
import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'velvet90_match'))

from wcs_engine import WCSEngine, PROGRAM_NAMES
from ir_analysis import analyze_ir, measure_rt60

SR = 22050

def edc_db(mono, sr, times):
    """Compute EDC at specific time points."""
    energy = mono ** 2
    edc = np.cumsum(energy[::-1])[::-1]
    edc_norm = edc / (edc[0] + 1e-30)
    edc_db_arr = 10 * np.log10(edc_norm + 1e-30)
    result = {}
    for t in times:
        idx = min(int(t * sr), len(edc_db_arr) - 1)
        result[t] = edc_db_arr[idx]
    return result

print("=" * 70)
print("Decay Test — Longer IRs + different coefficient strategies")
print("=" * 70)

# Test 1: Longer ESS with uniform coefficients
print("\n--- Plate (P1) with uniform coefficients, 10s IR ---")
for coeff in [0.03, 0.05, 0.10, 0.20, 0.30]:
    coeffs = np.full(16, coeff)
    engine = WCSEngine(program=1, sr=SR)
    ir = engine.generate_ir_ess(
        duration_s=10.0, coefficients=coeffs, rolloff_hz=10000.0,
        sweep_duration_s=6.0, sweep_level=0.01, damping=0.95,
    )
    peak = np.max(np.abs(ir))
    if peak < 1e-8:
        print(f"  coeff={coeff:.2f}: SILENT")
        continue
    ir_norm = ir / peak
    mono = np.mean(ir_norm, axis=0)
    edc = edc_db(mono, SR, [0.5, 1.0, 2.0, 3.0, 5.0, 7.0, 9.0])
    rt60 = measure_rt60(mono, SR, 'T30')
    print(f"  coeff={coeff:.2f}: peak={peak:.4f} RT60={rt60:.2f}s "
          f"EDC: 0.5s={edc[0.5]:.1f}dB 1s={edc[1.0]:.1f}dB "
          f"3s={edc[3.0]:.1f}dB 5s={edc[5.0]:.1f}dB 9s={edc[9.0]:.1f}dB")

# Test 2: Mixed coefficients — large range
print("\n--- Plate (P1) with mixed negative/positive coefficients ---")
test_sets = [
    ("all_small", np.full(16, 0.05)),
    ("all_neg", np.full(16, -0.05)),
    ("alt_sign", np.array([0.05, -0.05] * 8)),
    ("decay_curve", np.array([0.8, 0.6, 0.4, 0.2, 0.15, 0.1, 0.08, 0.05,
                              0.8, 0.6, 0.4, 0.2, 0.15, 0.1, 0.08, 0.05])),
    ("neg_decay", np.array([-0.8, -0.6, -0.4, -0.2, -0.15, -0.1, -0.08, -0.05,
                             0.8, 0.6, 0.4, 0.2, 0.15, 0.1, 0.08, 0.05])),
]

for name, coeffs in test_sets:
    engine = WCSEngine(program=1, sr=SR)
    ir = engine.generate_ir_ess(
        duration_s=10.0, coefficients=coeffs, rolloff_hz=10000.0,
        sweep_duration_s=6.0, sweep_level=0.01, damping=0.95,
    )
    peak = np.max(np.abs(ir))
    if peak < 1e-8:
        print(f"  {name:12s}: SILENT")
        continue
    ir_norm = ir / peak
    mono = np.mean(ir_norm, axis=0)
    edc = edc_db(mono, SR, [0.5, 1.0, 3.0, 5.0])
    rt60 = measure_rt60(mono, SR, 'T30')
    profile = analyze_ir(ir_norm, SR, name=name)
    print(f"  {name:12s}: peak={peak:.4f} RT60={rt60:.2f}s EDT={profile.edt:.2f}s "
          f"EDC: 0.5s={edc[0.5]:.1f}dB 3s={edc[3.0]:.1f}dB")

# Test 3: Compare cold-start with inject_gain vs ESS
print("\n--- Concert Hall (P0): Cold-start vs ESS comparison ---")
coeffs = np.full(16, 0.1)
engine = WCSEngine(program=0, sr=SR)

# Cold-start with inject
ir_cold = engine.generate_ir(
    duration_s=10.0, coefficients=coeffs, rolloff_hz=10000.0,
    inject_gain=0.5, damping=0.95,
)
peak_cold = np.max(np.abs(ir_cold))
if peak_cold > 1e-8:
    ir_cold_norm = ir_cold / peak_cold
    mono_cold = np.mean(ir_cold_norm, axis=0)
    edc_cold = edc_db(mono_cold, SR, [0.5, 1.0, 3.0, 5.0, 9.0])
    rt60_cold = measure_rt60(mono_cold, SR, 'T30')
    print(f"  Cold-start: peak={peak_cold:.4f} RT60={rt60_cold:.2f}s "
          f"EDC: 0.5s={edc_cold[0.5]:.1f}dB 3s={edc_cold[3.0]:.1f}dB "
          f"9s={edc_cold[9.0]:.1f}dB")

# ESS
ir_ess = engine.generate_ir_ess(
    duration_s=10.0, coefficients=coeffs, rolloff_hz=10000.0,
    sweep_duration_s=6.0, sweep_level=0.01, damping=0.95,
)
peak_ess = np.max(np.abs(ir_ess))
if peak_ess > 1e-8:
    ir_ess_norm = ir_ess / peak_ess
    mono_ess = np.mean(ir_ess_norm, axis=0)
    edc_ess = edc_db(mono_ess, SR, [0.5, 1.0, 3.0, 5.0, 9.0])
    rt60_ess = measure_rt60(mono_ess, SR, 'T30')
    print(f"  ESS:        peak={peak_ess:.4f} RT60={rt60_ess:.2f}s "
          f"EDC: 0.5s={edc_ess[0.5]:.1f}dB 3s={edc_ess[3.0]:.1f}dB "
          f"9s={edc_ess[9.0]:.1f}dB")
