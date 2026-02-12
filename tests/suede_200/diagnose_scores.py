#!/usr/bin/env python3
"""
Diagnostic script — Analyze why EDT and EDC Shape scores are consistently 0.

Generates IRs with optimized coefficients and compares against target IRs,
printing detailed EDC/EDT diagnostics to find the root cause.
"""

import os
import sys
import json
import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'velvet90_match'))
from ir_analysis import (IRProfile, analyze_ir, load_ir, measure_rt60,
                         compute_edc, profile_summary, band_key_hz)
from ir_compare import compare_profiles, _resample_to_common

from wcs_engine import WCSEngine, ir_name_to_program

IR_BASE = os.environ.get('SUEDE_IR_BASE')

# Best coefficients from previous optimization (Plate 7 — highest score 73.6)
PLATE7_COEFFS = np.array([
    -0.025, +0.940, -0.068, +0.503, +0.251, -0.150,
    +0.921, -0.116, +0.583, -0.837, +0.898, -0.713,
    +0.089, +0.246, -0.411, +0.429
])
PLATE7_ROLLOFF = 3132.0

HALL1_COEFFS = np.array([
    -0.895, +0.010, -0.045, +0.812, +0.026, -0.109,
    +0.752, -0.397, +0.947, -0.200, +0.702, -0.255,
    +0.502, +0.735, -0.698, +0.778
])
HALL1_ROLLOFF = 8123.0


def diagnose_ir(target_path, program, coefficients, rolloff_hz, target_name=None):
    """Generate engine IR and do deep comparison with target."""
    if target_name is None:
        target_name = os.path.splitext(os.path.basename(target_path))[0]

    sr = 22050  # match optimization SR

    # Load target
    target_data, target_sr = load_ir(target_path)
    if target_sr != sr:
        from scipy.signal import resample
        n_new = int(target_data.shape[1] * sr / target_sr)
        target_data = np.array([resample(target_data[c], n_new)
                                for c in range(target_data.shape[0])])
    peak = np.max(np.abs(target_data))
    if peak > 0:
        target_data = target_data / peak
    target_profile = analyze_ir(target_data, sr, name=target_name)

    # Generate our IR
    engine = WCSEngine(program=program, sr=sr)
    duration_s = min(target_profile.rt60 * 1.2 if target_profile.rt60 > 0
                     else target_profile.duration_s, 6.0)
    duration_s = max(duration_s, 1.5)

    ir = engine.generate_ir(duration_s=duration_s,
                            coefficients=coefficients,
                            rolloff_hz=rolloff_hz,
                            pre_delay_ms=0.0)
    ir_peak = np.max(np.abs(ir))
    if ir_peak < 1e-6:
        print(f"SILENCE: engine produced no output for {target_name}")
        return
    ir = ir / ir_peak

    candidate_profile = analyze_ir(ir, sr, name="WCS_engine")

    # Full comparison
    result = compare_profiles(target_profile, candidate_profile)
    print(result.summary())
    print()

    # === Deep EDT/EDC Diagnostics ===
    print("=" * 60)
    print("DETAILED EDT / EDC DIAGNOSTICS")
    print("=" * 60)

    # EDT details
    print(f"\n--- EDT ---")
    print(f"  Target EDT:    {target_profile.edt:.4f}s")
    print(f"  Engine EDT:    {candidate_profile.edt:.4f}s")
    print(f"  Difference:    {candidate_profile.edt - target_profile.edt:+.4f}s")
    edt_ref = max(target_profile.edt, 0.2)
    edt_tolerance = edt_ref * 0.50
    print(f"  Tolerance:     {edt_tolerance:.4f}s (50% of max(target, 0.2))")
    print(f"  Error/tol:     {abs(candidate_profile.edt - target_profile.edt) / edt_tolerance:.2f}")

    # RT60 comparison for context
    print(f"\n--- RT60 ---")
    print(f"  Target RT60:   {target_profile.rt60:.4f}s")
    print(f"  Engine RT60:   {candidate_profile.rt60:.4f}s")
    print(f"  Target RT60 (T20): {target_profile.rt60_t20:.4f}s")
    print(f"  Engine RT60 (T20): {candidate_profile.rt60_t20:.4f}s")

    # EDC shape comparison
    print(f"\n--- EDC Shape ---")
    ir_mono_target = target_data[0]
    ir_mono_engine = ir[0]

    edc_target = compute_edc(ir_mono_target)
    edc_engine = compute_edc(ir_mono_engine)

    # Resample to same length for comparison
    edc_a, edc_b = _resample_to_common(edc_target, edc_engine, 300)
    edc_a = np.clip(edc_a, -60, 0)
    edc_b = np.clip(edc_b, -60, 0)
    rms_diff = np.sqrt(np.mean((edc_a - edc_b) ** 2))
    print(f"  EDC RMS diff:  {rms_diff:.2f} dB (tolerance: 8.0 dB)")
    print(f"  EDC score:     {100 * np.exp(-(rms_diff/8.0)**2):.1f}")

    # Print EDC at key timepoints
    print(f"\n  EDC at key relative positions (0=start, 1=end):")
    print(f"  {'Position':>10s}  {'Target dB':>10s}  {'Engine dB':>10s}  {'Diff dB':>10s}")
    for pos in [0.0, 0.05, 0.1, 0.15, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0]:
        idx = min(int(pos * 299), 299)
        print(f"  {pos:10.2f}  {edc_a[idx]:10.2f}  {edc_b[idx]:10.2f}  {edc_b[idx]-edc_a[idx]:+10.2f}")

    # Measure where -10dB is reached (EDT indicator)
    print(f"\n--- Where -10dB is reached in EDC ---")
    t_target = np.arange(len(edc_target)) / sr
    t_engine = np.arange(len(edc_engine)) / sr

    idx_10_target = np.searchsorted(-edc_target, 10.0)
    idx_10_engine = np.searchsorted(-edc_engine, 10.0)

    if idx_10_target < len(edc_target):
        print(f"  Target: -10dB at {t_target[idx_10_target]:.4f}s "
              f"(sample {idx_10_target}/{len(edc_target)})")
    else:
        print(f"  Target: never reaches -10dB!")

    if idx_10_engine < len(edc_engine):
        print(f"  Engine: -10dB at {t_engine[idx_10_engine]:.4f}s "
              f"(sample {idx_10_engine}/{len(edc_engine)})")
    else:
        print(f"  Engine: never reaches -10dB!")

    # Check initial energy profile
    print(f"\n--- Initial Energy Profile (first 100ms) ---")
    samples_100ms = int(sr * 0.1)
    target_initial = ir_mono_target[:min(samples_100ms, len(ir_mono_target))]
    engine_initial = ir_mono_engine[:min(samples_100ms, len(ir_mono_engine))]

    print(f"  Target: peak={np.max(np.abs(target_initial)):.6f}, "
          f"rms={np.sqrt(np.mean(target_initial**2)):.6f}")
    print(f"  Engine: peak={np.max(np.abs(engine_initial)):.6f}, "
          f"rms={np.sqrt(np.mean(engine_initial**2)):.6f}")

    # Energy in segments
    print(f"\n--- Energy by segment ---")
    seg_ms = [0, 10, 50, 100, 200, 500, 1000, 2000]
    for i in range(len(seg_ms) - 1):
        s0 = int(sr * seg_ms[i] / 1000)
        s1 = int(sr * seg_ms[i+1] / 1000)
        s1_t = min(s1, len(ir_mono_target))
        s1_e = min(s1, len(ir_mono_engine))

        if s0 < s1_t:
            e_target = np.sum(ir_mono_target[s0:s1_t] ** 2)
        else:
            e_target = 0

        if s0 < s1_e:
            e_engine = np.sum(ir_mono_engine[s0:s1_e] ** 2)
        else:
            e_engine = 0

        e_target_db = 10 * np.log10(max(e_target, 1e-20))
        e_engine_db = 10 * np.log10(max(e_engine, 1e-20))
        print(f"  {seg_ms[i]:4d}-{seg_ms[i+1]:4d}ms: "
              f"Target={e_target_db:+7.1f}dB  Engine={e_engine_db:+7.1f}dB  "
              f"Diff={e_engine_db-e_target_db:+7.1f}dB")

    # Spectral diagnostics
    print(f"\n--- Spectral Scores ---")
    print(f"  Early (0-50ms):  {result.spectral_early_score:.1f}")
    print(f"  Late (200-500ms): {result.spectral_late_score:.1f}")
    print(f"  Centroid:         {result.spectral_centroid_score:.1f}")

    # Band RT60 detail
    print(f"\n--- Band RT60 ---")
    print(f"  {'Band':>8s}  {'Target':>8s}  {'Engine':>8s}  {'Diff':>8s}  {'Score':>6s}")
    common_bands = set(target_profile.band_rt60.keys()) & set(candidate_profile.band_rt60.keys())
    for band in sorted(common_bands, key=band_key_hz):
        t_val = target_profile.band_rt60[band]
        c_val = candidate_profile.band_rt60[band]
        diff = c_val - t_val
        if t_val > 0.05:
            score = 100 * np.exp(-(abs(diff) / (t_val * 0.40))**2)
        else:
            score = 50.0
        print(f"  {band:>8s}  {t_val:8.3f}  {c_val:8.3f}  {diff:+8.3f}  {score:6.1f}")

    return result


def diagnose_programs_3_5():
    """Check why programs 3-5 produce silence."""
    print("\n" + "=" * 60)
    print("PROGRAMS 3-5 SILENCE DIAGNOSIS")
    print("=" * 60)

    for prog in [3, 4, 5]:
        from wcs_engine import PROGRAM_NAMES, MICROCODE, DecodedStep
        engine = WCSEngine(program=prog, sr=22050)
        print(f"\nProgram {prog}: {PROGRAM_NAMES[prog]}")
        print(f"  Output step L: {engine.output_step_l}")
        print(f"  Output step R: {engine.output_step_r}")

        # Try with baseline coefficients
        from ir_optimizer import BASELINE_COEFFICIENTS
        ir = engine.generate_ir(duration_s=2.0,
                                coefficients=BASELINE_COEFFICIENTS,
                                rolloff_hz=10000.0)
        peak = np.max(np.abs(ir))
        print(f"  Baseline coeffs peak: {peak:.8f}")

        # Try with all-0.5 coefficients
        ir2 = engine.generate_ir(duration_s=2.0,
                                 coefficients=np.full(16, 0.5),
                                 rolloff_hz=10000.0)
        peak2 = np.max(np.abs(ir2))
        print(f"  All-0.5 coeffs peak: {peak2:.8f}")

        # Try with strong coefficients
        strong = np.array([0.9, 0.9, 0.9, 0.95, 0.9, 0.9, 0.95, 0.9,
                          0.9, 0.9, 0.95, 0.9, 0.9, 0.9, 0.95, 0.9])
        ir3 = engine.generate_ir(duration_s=2.0,
                                 coefficients=strong,
                                 rolloff_hz=10000.0)
        peak3 = np.max(np.abs(ir3))
        print(f"  Strong coeffs peak: {peak3:.8f}")

        # Analyze microcode steps
        steps = [DecodedStep.decode(w) for w in MICROCODE[prog]]
        active_steps = sum(1 for s in steps if not s.isNop)
        coeff_steps = sum(1 for s in steps if s.hasCoeff)
        mem_writes = sum(1 for s in steps if (s.ctrl & 0x10) and s.ctrl != 0x1F)
        print(f"  Active steps: {active_steps}/128")
        print(f"  Coefficient steps: {coeff_steps}")
        print(f"  Memory writes: {mem_writes}")

        # Check output step signature
        for half, (start, end, label) in enumerate([(0, 64, 'L'), (64, 128, 'R')]):
            out_step = engine.output_step_l if half == 0 else engine.output_step_r
            s = steps[out_step]
            print(f"  Output {label} (step {out_step}): "
                  f"ctrl=0x{s.ctrl:02X} wai={s.wai} hasCoeff={s.hasCoeff} "
                  f"isNop={s.isNop} ofst=0x{s.ofst:04X}")

        # Check what value is in regs[1] at output step by tracing
        print(f"  Tracing first 5 samples...")
        engine2 = WCSEngine(program=prog, sr=22050)
        engine2.reset()
        coefficients = BASELINE_COEFFICIENTS.copy()
        for n in range(5):
            inp = 1.0 if n == 0 else 0.0
            engine2.regs[2] = inp * 0.25  # input gain
            for s_idx in range(64):
                engine2._execute_step(engine2.steps[s_idx], coefficients)
                if s_idx == engine2.output_step_l:
                    cap_l = engine2.regs[1]
            engine2.regs[2] = inp * 0.25
            for s_idx in range(64, 128):
                engine2._execute_step(engine2.steps[s_idx], coefficients)
                if s_idx == engine2.output_step_r:
                    cap_r = engine2.regs[1]
            engine2.write_ptr = (engine2.write_ptr + 1) % engine2.memory_size
            print(f"    Sample {n}: L={cap_l:.8f}  R={cap_r:.8f}  "
                  f"regs={[f'{r:.6f}' for r in engine2.regs]}")


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description='Diagnose Suede 200 score issues')
    parser.add_argument('--ir', type=str, default='Plate 7',
                        help='IR name to diagnose (default: Plate 7)')
    parser.add_argument('--silence', action='store_true',
                        help='Diagnose programs 3-5 silence')
    parser.add_argument('--all', action='store_true',
                        help='Run all diagnostics')
    args = parser.parse_args()

    if args.silence or args.all:
        diagnose_programs_3_5()

    if not args.silence or args.all:
        if IR_BASE is None:
            print("Error: SUEDE_IR_BASE environment variable not set.")
            print("Set it to the path containing Lexicon 200 impulse responses.")
            sys.exit(1)

        # Find the target IR
        ir_file = None
        for f in sorted(os.listdir(IR_BASE)):
            if not f.endswith('.wav'):
                continue
            name = f.replace('_dc.wav', '').replace('.wav', '')
            if args.ir.lower() in name.lower():
                ir_file = os.path.join(IR_BASE, f)
                ir_name = name
                break

        if ir_file is None:
            print(f"IR '{args.ir}' not found in {IR_BASE}")
            sys.exit(1)

        program = ir_name_to_program(os.path.basename(ir_file))
        print(f"Diagnosing: {ir_name} (Program {program})")

        # Load optimized coefficients from results if available
        results_path = os.path.join(os.path.dirname(__file__), 'match_results', 'coefficients.json')
        coeffs = None
        rolloff = 10000.0

        if os.path.exists(results_path):
            with open(results_path) as f:
                data = json.load(f)
            for entry in data:
                if args.ir.lower() in entry['target'].lower():
                    coeffs = np.array(entry['coefficients'])
                    rolloff = entry['rolloff_hz']
                    print(f"Using optimized coefficients (score: {entry['score']:.1f})")
                    break

        if coeffs is None:
            # Use hardcoded fallback
            if 'Plate 7' in args.ir:
                coeffs = PLATE7_COEFFS
                rolloff = PLATE7_ROLLOFF
            elif 'Hall 1' in args.ir:
                coeffs = HALL1_COEFFS
                rolloff = HALL1_ROLLOFF
            else:
                from ir_optimizer import BASELINE_COEFFICIENTS
                coeffs = BASELINE_COEFFICIENTS
                rolloff = 10000.0
            print("Using fallback coefficients")

        diagnose_ir(ir_file, program, coeffs, rolloff, ir_name)
