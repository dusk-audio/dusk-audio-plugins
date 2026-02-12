#!/usr/bin/env python3
"""Debug EDT measurement — check if ESS deconvolution produces clean IRs."""
import os, sys
import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'velvet90_match'))

from wcs_engine import WCSEngine, PROGRAM_NAMES
from ir_analysis import analyze_ir, measure_rt60

SR = 22050

# Use the optimized coefficients from the quick test
OPTIMIZED_COEFFS = np.array([
    +0.3594, -0.8624, -0.7186, -0.1287,
    +0.9086, +0.6374, +0.2234, -0.9596,
    -0.8032, +0.6413, -0.7821, -0.0768,
    -0.6487, +0.5336, +0.5994, +0.6797,
])

print("=" * 70)
print("EDT Debug — Checking ESS IR quality at different sweep levels")
print("=" * 70)

for sweep_level in [0.001, 0.005, 0.01, 0.02, 0.05, 0.1]:
    engine = WCSEngine(program=1, sr=SR)
    ir = engine.generate_ir_ess(
        duration_s=4.0,
        coefficients=OPTIMIZED_COEFFS,
        rolloff_hz=5832.0,
        sweep_duration_s=4.0,
        sweep_level=sweep_level,
        damping=0.96,
    )
    peak = np.max(np.abs(ir))
    if peak < 1e-8:
        print(f"  sweep={sweep_level:.3f}: SILENT")
        continue

    ir_norm = ir / peak
    mono = np.mean(ir_norm, axis=0)

    # Compute EDC (Energy Decay Curve)
    energy = mono ** 2
    edc = np.cumsum(energy[::-1])[::-1]
    edc = edc / (edc[0] + 1e-30)
    edc_db = 10 * np.log10(edc + 1e-30)

    # Find specific dB points
    t_5 = np.searchsorted(-edc_db, 5) / SR
    t_10 = np.searchsorted(-edc_db, 10) / SR
    t_35 = np.searchsorted(-edc_db, 35) / SR
    t_60 = np.searchsorted(-edc_db, 60) / SR

    rt60 = measure_rt60(mono, SR, 'T30')
    profile = analyze_ir(ir_norm, SR, name=f"sw{sweep_level}")

    # Check for pre-ring (energy before the impulse)
    early_energy = np.sqrt(np.mean(ir_norm[:, :int(0.002*SR)] ** 2))

    print(f"  sweep={sweep_level:.3f}: peak={peak:.4f} RT60={rt60:.2f}s "
          f"EDT={profile.edt:.2f}s "
          f"t_5={t_5:.3f}s t_10={t_10:.3f}s t_35={t_35:.3f}s t_60={t_60:.3f}s "
          f"early_rms={early_energy:.6f}")

# Also check a known-good reference: simple exponential decay
print(f"\n--- Reference: synthetic exponential IR (RT60=0.86s) ---")
n = int(SR * 4)
t = np.arange(n) / SR
# RT60=0.86s → decay rate = 6.9/0.86 = 8.023 per second
decay_rate = 6.9078 / 0.86
noise = np.random.randn(n) * 0.01
ref_ir = noise * np.exp(-decay_rate * t)
ref_ir[0] = 1.0  # Impulse
ref_2ch = np.stack([ref_ir, ref_ir * 0.9 + np.random.randn(n) * 0.001])
peak_ref = np.max(np.abs(ref_2ch))
ref_2ch = ref_2ch / peak_ref
ref_profile = analyze_ir(ref_2ch, SR, name="synthetic")
print(f"  RT60={ref_profile.rt60:.2f}s EDT={ref_profile.edt:.2f}s")

# Compare EDC shapes
print(f"\n--- EDC shape comparison (sweep=0.01 vs synthetic) ---")
engine = WCSEngine(program=1, sr=SR)
ir = engine.generate_ir_ess(
    duration_s=4.0, coefficients=OPTIMIZED_COEFFS, rolloff_hz=5832.0,
    sweep_duration_s=4.0, sweep_level=0.01, damping=0.96,
)
peak = np.max(np.abs(ir))
ir_norm = ir / peak
mono_wcs = np.mean(ir_norm, axis=0)

energy_wcs = mono_wcs ** 2
edc_wcs = np.cumsum(energy_wcs[::-1])[::-1]
edc_wcs_db = 10 * np.log10(edc_wcs / (edc_wcs[0] + 1e-30) + 1e-30)

energy_ref = ref_ir ** 2
edc_ref = np.cumsum(energy_ref[::-1])[::-1]
edc_ref_db = 10 * np.log10(edc_ref / (edc_ref[0] + 1e-30) + 1e-30)

print("  Time(s)  WCS_EDC(dB)  Ref_EDC(dB)")
for t_sec in [0.0, 0.05, 0.1, 0.2, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0]:
    idx = min(int(t_sec * SR), len(edc_wcs_db) - 1)
    idx_ref = min(int(t_sec * SR), len(edc_ref_db) - 1)
    print(f"  {t_sec:>5.2f}    {edc_wcs_db[idx]:>8.1f}     {edc_ref_db[idx_ref]:>8.1f}")
