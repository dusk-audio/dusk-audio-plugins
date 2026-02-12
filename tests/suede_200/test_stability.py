#!/usr/bin/env python3
"""Test engine stability with the Q15 quantization + fixed I/O routing."""
import os, sys, time
import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'lexicon_match'))

from wcs_engine import WCSEngine, PROGRAM_NAMES

SR = 22050
IR_DURATION = 4.0
SWEEP_DURATION = 4.0
SWEEP_LEVEL = 0.05

BASELINE_COEFFICIENTS = np.array([
    0.45, 0.47, 0.50, 0.75,
    0.35, 0.50, 0.71, 0.55,
    0.45, 0.38, 0.73, 0.75,
    0.35, 0.15, 0.70, 0.39,
])

def measure_rt60_simple(ir_mono, sr):
    """Simple RT60 from energy decay."""
    energy = ir_mono ** 2
    # Cumulative energy from end
    cum = np.cumsum(energy[::-1])[::-1]
    cum = cum / (cum[0] + 1e-30)
    cum_db = 10 * np.log10(cum + 1e-30)
    # Find -5dB and -35dB points for T30 extrapolation
    t5 = np.searchsorted(-cum_db, 5)
    t35 = np.searchsorted(-cum_db, 35)
    if t35 <= t5 or t35 >= len(cum_db):
        return 0.0
    rt60 = 2.0 * (t35 - t5) / sr  # T30 extrapolated to T60
    return rt60

print("=" * 70)
print("Stability Test — Q15 quantization + fixed I/O routing")
print("=" * 70)

# Test 1: Raw impulse (cold-start) at different damping values
print("\n--- Cold-start impulse test (Concert Hall) ---")
for damp in [0.85, 0.90, 0.95, 0.98, 0.99, 1.0]:
    engine = WCSEngine(program=0, sr=SR)
    ir = engine.generate_ir(
        duration_s=3.0,
        coefficients=BASELINE_COEFFICIENTS,
        rolloff_hz=10000.0,
        inject_gain=0.1,
        damping=damp,
    )
    peak = np.max(np.abs(ir))
    n_samples = ir.shape[1]
    # RMS in 3 windows
    w1 = int(0.5 * SR)
    w2 = int(1.5 * SR)
    w3 = int(2.5 * SR)
    rms1 = np.sqrt(np.mean(ir[:, w1-500:w1+500] ** 2)) if w1 > 500 else 0
    rms2 = np.sqrt(np.mean(ir[:, w2-500:w2+500] ** 2)) if w2 > 500 else 0
    rms3 = np.sqrt(np.mean(ir[:, w3-500:w3+500] ** 2)) if w3 > 500 else 0
    decay = "DECAYING" if rms3 < rms1 * 0.5 else "SUSTAINED" if rms3 > rms1 * 0.8 else "SLOW DECAY"
    print(f"  damp={damp:.2f}: peak={peak:.4f} RMS@0.5s={rms1:.4f} @1.5s={rms2:.4f} @2.5s={rms3:.4f} [{decay}]")

# Test 2: ESS for all programs at damping=0.95
print(f"\n--- ESS test all programs (damping=0.95) ---")
for prog_idx in range(6):
    engine = WCSEngine(program=prog_idx, sr=SR)
    t0 = time.time()
    try:
        ir = engine.generate_ir_ess(
            duration_s=IR_DURATION,
            coefficients=BASELINE_COEFFICIENTS * 0.7,
            rolloff_hz=10000.0,
            sweep_duration_s=SWEEP_DURATION,
            sweep_level=SWEEP_LEVEL,
            damping=0.95,
        )
    except Exception as e:
        print(f"  P{prog_idx} {PROGRAM_NAMES[prog_idx]:15s}: ERROR {e}")
        continue

    elapsed = time.time() - t0
    peak = np.max(np.abs(ir))
    if peak < 1e-8:
        print(f"  P{prog_idx} {PROGRAM_NAMES[prog_idx]:15s}: SILENT ({elapsed:.1f}s)")
        continue

    ir_norm = ir / peak
    mono = np.mean(ir_norm, axis=0)
    rt60 = measure_rt60_simple(mono, SR)
    print(f"  P{prog_idx} {PROGRAM_NAMES[prog_idx]:15s}: peak={peak:.4f} RT60={rt60:.2f}s ({elapsed:.1f}s)")

# Test 3: Concert Hall with different coefficient scales
print(f"\n--- Concert Hall: coefficient scale sweep (damping=0.95) ---")
for scale in [0.3, 0.5, 0.7, 0.9, 1.0]:
    coeffs = BASELINE_COEFFICIENTS * scale
    engine = WCSEngine(program=0, sr=SR)
    try:
        ir = engine.generate_ir_ess(
            duration_s=IR_DURATION,
            coefficients=coeffs,
            rolloff_hz=10000.0,
            sweep_duration_s=SWEEP_DURATION,
            sweep_level=SWEEP_LEVEL,
            damping=0.95,
        )
    except Exception as e:
        print(f"  scale={scale:.1f}: ERROR {e}")
        continue

    peak = np.max(np.abs(ir))
    if peak < 1e-8:
        print(f"  scale={scale:.1f}: SILENT")
        continue

    ir_norm = ir / peak
    mono = np.mean(ir_norm, axis=0)
    rt60 = measure_rt60_simple(mono, SR)
    print(f"  scale={scale:.1f}: peak={peak:.4f} RT60={rt60:.2f}s")

# Test 4: Ultra-small coefficients (should NOT self-oscillate)
print(f"\n--- Concert Hall: ultra-small coefficients (should not self-oscillate) ---")
for c_val in [0.001, 0.01, 0.1, 0.3]:
    tiny_coeffs = np.full(16, c_val)
    engine = WCSEngine(program=0, sr=SR)
    ir = engine.generate_ir(
        duration_s=3.0,
        coefficients=tiny_coeffs,
        rolloff_hz=10000.0,
        inject_gain=0.1,
        damping=1.0,  # No damping — rely on engine stability
    )
    peak = np.max(np.abs(ir))
    rms_end = np.sqrt(np.mean(ir[:, -2000:] ** 2))
    status = "STABLE" if rms_end < 0.001 else "OSCILLATING" if rms_end > 0.01 else "MARGINAL"
    print(f"  coeff={c_val:.3f}: peak={peak:.6f} end_rms={rms_end:.6f} [{status}]")
