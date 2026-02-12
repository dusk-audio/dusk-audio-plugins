#!/usr/bin/env python3
"""Test with zero coefficients to confirm feedback bypasses coefficient multiply."""
import os, sys
import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
from wcs_engine import WCSEngine, PROGRAM_NAMES

SR = 22050

print("=" * 70)
print("Zero coefficient test — does the engine produce output with coeff=0?")
print("=" * 70)

for prog_idx in range(3):
    zero_coeffs = np.zeros(16)
    engine = WCSEngine(program=prog_idx, sr=SR)

    # Cold-start with inject
    ir = engine.generate_ir(
        duration_s=6.0, coefficients=zero_coeffs, rolloff_hz=10000.0,
        inject_gain=0.5, damping=1.0,
    )
    peak = np.max(np.abs(ir))
    rms_end = np.sqrt(np.mean(ir[:, -2000:] ** 2))
    print(f"  P{prog_idx} {PROGRAM_NAMES[prog_idx]:15s} (cold+inject): "
          f"peak={peak:.6f} end_rms={rms_end:.6f}")

    # ESS
    ir_ess = engine.generate_ir_ess(
        duration_s=6.0, coefficients=zero_coeffs, rolloff_hz=10000.0,
        sweep_duration_s=4.0, sweep_level=0.01, damping=1.0,
    )
    peak_ess = np.max(np.abs(ir_ess))
    print(f"  P{prog_idx} {PROGRAM_NAMES[prog_idx]:15s} (ESS):          "
          f"peak={peak_ess:.6f}")

# Now check with a single non-zero coefficient
print(f"\n--- Single non-zero coefficient test (Concert Hall) ---")
for i in range(16):
    coeffs = np.zeros(16)
    coeffs[i] = 0.5
    engine = WCSEngine(program=0, sr=SR)
    ir = engine.generate_ir_ess(
        duration_s=4.0, coefficients=coeffs, rolloff_hz=10000.0,
        sweep_duration_s=3.0, sweep_level=0.01, damping=1.0,
    )
    peak = np.max(np.abs(ir))
    print(f"  Only C{i:X}=0.5: peak={peak:.6f}")

# Check what happens inside the engine step by step
print(f"\n--- Step-by-step trace: Concert Hall, zero coefficients ---")
from wcs_engine import MICROCODE, DecodedStep
engine = WCSEngine(program=0, sr=SR)

# Simulate one sample with zero coefficients
engine.reset()
coeffs = np.zeros(16)
engine.regs[2] = 0.1  # Simulated input

# Inject into memory
engine.memory[0] = 0.5  # Seed one position

# Run L half
for s_idx in range(64):
    word = MICROCODE[0][s_idx]
    step = DecodedStep.decode(word)
    if step.isNop:
        continue

    old_regs = engine.regs.copy()
    engine._execute_step(step, coeffs, damping=1.0)

    # Check if any register changed
    for r in range(8):
        if abs(engine.regs[r] - old_regs[r]) > 1e-10:
            if step.hasCoeff:
                src = 'mem' if step.rai else f'r{step.rad}'
                acc = '+=' if step.acc0 else '='
                print(f"  Step {s_idx:3d}: r{step.wai}{acc}{src}*c{step.cCode:X} "
                      f"ctrl=0x{step.ctrl:02X} → r{r} changed: {old_regs[r]:.6f}→{engine.regs[r]:.6f}")
            else:
                print(f"  Step {s_idx:3d}: non-coeff ctrl=0x{step.ctrl:02X} wai={step.wai} "
                      f"→ r{r} changed: {old_regs[r]:.6f}→{engine.regs[r]:.6f}")
            break

print(f"\n  Final regs: {[f'{r:.6f}' for r in engine.regs]}")
