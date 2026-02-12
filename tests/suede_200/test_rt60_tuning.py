#!/usr/bin/env python3
"""Find coefficient ranges that produce target RT60 values."""
import os, sys
import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
from wcs_engine import WCSEngine, PROGRAM_NAMES

SR = 22050
SWEEP_DURATION = 4.0
SWEEP_LEVEL = 0.05

def measure_rt60_simple(ir_mono, sr):
    energy = ir_mono ** 2
    cum = np.cumsum(energy[::-1])[::-1]
    cum = cum / (cum[0] + 1e-30)
    cum_db = 10 * np.log10(cum + 1e-30)
    t5 = np.searchsorted(-cum_db, 5)
    t35 = np.searchsorted(-cum_db, 35)
    if t35 <= t5 or t35 >= len(cum_db):
        return 0.0
    return 2.0 * (t35 - t5) / sr

print("=" * 70)
print("RT60 Tuning — Finding coefficient ranges for target RT60 values")
print("=" * 70)

# Use uniform coefficients to understand the relationship
for prog_idx in [0, 1, 2]:
    print(f"\n--- Program {prog_idx}: {PROGRAM_NAMES[prog_idx]} ---")
    print(f"  {'coeff':>7s} {'RT60':>7s} {'peak':>8s} {'status':>10s}")

    for coeff_val in [0.01, 0.03, 0.05, 0.08, 0.10, 0.15, 0.20, 0.30, 0.40, 0.50]:
        coeffs = np.full(16, coeff_val)
        engine = WCSEngine(program=prog_idx, sr=SR)

        try:
            ir = engine.generate_ir_ess(
                duration_s=6.0,
                coefficients=coeffs,
                rolloff_hz=10000.0,
                sweep_duration_s=SWEEP_DURATION,
                sweep_level=SWEEP_LEVEL,
                damping=1.0,
            )
        except Exception:
            print(f"  {coeff_val:>7.3f}  ERROR")
            continue

        peak = np.max(np.abs(ir))
        if peak < 1e-8:
            print(f"  {coeff_val:>7.3f}  SILENT")
            continue

        ir_norm = ir / peak
        mono = np.mean(ir_norm, axis=0)
        rt60 = measure_rt60_simple(mono, SR)

        status = "target" if 1.0 < rt60 < 5.0 else "too long" if rt60 > 5.0 else "too short"
        print(f"  {coeff_val:>7.3f} {rt60:>7.2f}s {peak:>8.4f} {status:>10s}")
