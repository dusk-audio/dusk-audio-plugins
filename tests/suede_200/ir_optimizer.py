#!/usr/bin/env python3
"""
Coefficient Optimizer — Find the 16 WCS coefficients that best match a target IR.

The WCS topology is fixed from ROM. Only the 16 coefficient values (C-codes 0x0–0xF)
are free variables. This is a 16-dimensional continuous optimization problem —
much cleaner than the 41-parameter Velvet90 matching.

Strategy:
  1. Start from hand-tuned baseline coefficients (from C++ updateCoefficients)
  2. Differential evolution for global search (robust for 16D)
  3. Nelder-Mead polish
  4. Coordinate descent refinement

Performance notes:
  - popsize=8 gives population of 8*17=136 (not 510 with popsize=30!)
  - scipy DE popsize is multiplied by parameter count
  - Matching at 22050Hz halves IR generation cost with negligible quality loss
  - Each IR takes ~2-5 min depending on CPU load
"""

import os
import sys
import numpy as np
from scipy.optimize import differential_evolution, minimize
from dataclasses import dataclass
import time

# Add parent directory for ir_analysis imports
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'velvet90_match'))
from ir_analysis import IRProfile, analyze_ir, load_ir, measure_rt60, profile_summary
from ir_compare import compare_profiles

from wcs_engine import WCSEngine, ir_name_to_program, PROGRAM_NAMES


@dataclass
class OptimizationResult:
    """Result of coefficient optimization for one IR."""
    target_name: str
    program: int
    program_name: str
    best_coefficients: np.ndarray
    best_rolloff_hz: float
    best_score: float
    comparison: object  # ComparisonResult
    elapsed_s: float

    def summary(self) -> str:
        lines = [
            f"=== {self.target_name} (Program: {self.program_name}) ===",
            f"  Score: {self.best_score:.1f}/100  ({self.elapsed_s:.1f}s)",
            f"  Rolloff: {self.best_rolloff_hz:.0f} Hz",
            f"  Coefficients:",
        ]
        for i in range(16):
            lines.append(f"    C{i:X}: {self.best_coefficients[i]:+.4f}")
        return "\n".join(lines)

    def to_dict(self) -> dict:
        return {
            'target': self.target_name,
            'program': self.program,
            'program_name': self.program_name,
            'score': self.best_score,
            'rolloff_hz': self.best_rolloff_hz,
            'coefficients': self.best_coefficients.tolist(),
        }


# Coefficient bounds: most are in [-1, 1] range but feedback can be higher
COEFF_BOUNDS = [
    (-0.99, 0.99),  # C0: structural
    (-0.99, 0.99),  # C1: diffusion A
    (-0.99, 0.99),  # C2: diffusion B
    ( 0.10, 0.99),  # C3: main feedback (must be positive for reverb)
    (-0.99, 0.99),  # C4: auxiliary
    (-0.99, 0.99),  # C5: structural tap
    ( 0.10, 0.99),  # C6: secondary feedback
    (-0.99, 0.99),  # C7: allpass diffusion
    (-0.99, 0.99),  # C8: size-dependent
    (-0.99, 0.99),  # C9: HF decay
    ( 0.10, 0.99),  # CA: decay variant
    (-0.99, 0.99),  # CB: LF decay
    (-0.99, 0.99),  # CC: cross-coupling
    (-0.99, 0.99),  # CD: pre-echo/damping
    (-0.99, 0.99),  # CE: output stage
    (-0.99, 0.99),  # CF: reserved
]

# Rolloff frequency bound (appended as 17th parameter)
ROLLOFF_BOUNDS = (2000.0, 12000.0)

# All bounds for optimizer (16 coefficients + rolloff)
ALL_BOUNDS = COEFF_BOUNDS + [ROLLOFF_BOUNDS]

# Baseline coefficients from C++ (RT=2.5s, Size=26m, Diff=Med defaults)
BASELINE_COEFFICIENTS = np.array([
    0.45, 0.47, 0.50, 0.75,   # C0-C3
    0.35, 0.50, 0.71, 0.55,   # C4-C7
    0.45, 0.38, 0.73, 0.75,   # C8-CB
    0.35, 0.15, 0.70, 0.39,   # CC-CF
])

# Default optimization settings
DEFAULT_POPSIZE = 8       # Population = popsize * 17 = 136 per generation
DEFAULT_MAX_ITER = 50     # DE generations
DEFAULT_MATCH_SR = 22050  # Half rate for 2.5x faster IR generation


def _generate_and_compare(params: np.ndarray, engine: WCSEngine,
                           target_profile: IRProfile,
                           duration_s: float) -> float:
    """Cost function: generate IR with given coefficients, compare to target."""
    coefficients = np.clip(params[:16], -0.998, 0.998)
    rolloff_hz = np.clip(params[16], 2000, 12000)

    ir = engine.generate_ir(
        duration_s=duration_s,
        coefficients=coefficients,
        rolloff_hz=rolloff_hz,
        pre_delay_ms=0.0,
    )

    # Check for silent or exploding output
    peak = np.max(np.abs(ir))
    if peak < 1e-6:
        return 0.0  # worst score for silence
    if peak > 100.0 or np.any(np.isnan(ir)):
        return 0.0  # worst score for blowup

    # Normalize
    ir = ir / peak

    candidate_profile = analyze_ir(ir, engine.sr, name="WCS_candidate")
    result = compare_profiles(target_profile, candidate_profile)
    return result.overall_score


def _cost_function(params: np.ndarray, engine: WCSEngine,
                    target_profile: IRProfile, duration_s: float) -> float:
    """Negated score for minimizers."""
    return -_generate_and_compare(params, engine, target_profile, duration_s)


def optimize_for_target(target_path: str,
                        program: int,
                        target_name: str = None,
                        sr: int = DEFAULT_MATCH_SR,
                        max_de_iterations: int = DEFAULT_MAX_ITER,
                        popsize: int = DEFAULT_POPSIZE,
                        verbose: bool = True,
                        warm_start: np.ndarray = None) -> OptimizationResult:
    """
    Optimize 16 WCS coefficients to match a target IR.

    Args:
        target_path: Path to target IR wav file
        program: Program index (0-5)
        target_name: Display name
        sr: Sample rate for matching (lower = faster; 22050 recommended)
        max_de_iterations: Max iterations for differential evolution
        popsize: DE population size multiplier (pop = popsize * 17 params)
        verbose: Print progress
        warm_start: Previous best coefficients (17-element: 16 coeffs + rolloff)

    Returns:
        OptimizationResult with best coefficients and score
    """
    start_time = time.time()

    if target_name is None:
        target_name = os.path.splitext(os.path.basename(target_path))[0]

    # Load and analyze target IR
    target_data, target_sr = load_ir(target_path)
    if target_sr != sr:
        from scipy.signal import resample
        n_new = int(target_data.shape[1] * sr / target_sr)
        target_data = np.array([resample(target_data[c], n_new)
                                for c in range(target_data.shape[0])])

    # Normalize target
    peak = np.max(np.abs(target_data))
    if peak > 0:
        target_data = target_data / peak

    target_profile = analyze_ir(target_data, sr, name=target_name)
    # Match duration: RT60 * 1.2 is sufficient (captures tail shape)
    duration_s = min(target_profile.rt60 * 1.2 if target_profile.rt60 > 0
                     else target_profile.duration_s, 6.0)
    duration_s = max(duration_s, 1.5)  # minimum 1.5s

    total_pop = popsize * len(ALL_BOUNDS)

    if verbose:
        print(f"\n{'='*60}")
        print(f"Optimizing: {target_name} → Program {program} ({PROGRAM_NAMES[program]})")
        print(profile_summary(target_profile))
        print(f"Match SR: {sr}Hz | IR duration: {duration_s:.1f}s | "
              f"DE pop: {total_pop} | max_iter: {max_de_iterations}")
        print(f"{'='*60}\n")

    # Create engine
    engine = WCSEngine(program=program, sr=sr)

    # --- Phase 1: Evaluate baseline ---
    if warm_start is not None:
        x0 = np.array(warm_start, dtype=np.float64)
        # Clip warm start to bounds to prevent DE rejection
        for i, (lo, hi) in enumerate(ALL_BOUNDS):
            x0[i] = np.clip(x0[i], lo, hi)
    else:
        x0 = np.append(BASELINE_COEFFICIENTS.copy(), 10000.0)

    baseline_score = _generate_and_compare(x0, engine, target_profile, duration_s)

    if verbose:
        print(f"Baseline score: {baseline_score:.1f}")

    best_x = x0.copy()
    best_score = baseline_score

    # --- Phase 2: Differential Evolution (global search) ---
    if verbose:
        print(f"\nPhase 2: Differential Evolution "
              f"(pop={total_pop}, max_iter={max_de_iterations})...")

    gen_count = [0]
    stall_count = [0]
    prev_best = [baseline_score]

    def de_callback(xk, convergence=0):
        gen_count[0] += 1
        if verbose and gen_count[0] % 5 == 0:
            score = _generate_and_compare(xk, engine, target_profile, duration_s)
            elapsed = time.time() - start_time
            print(f"  DE gen {gen_count[0]}: score={score:.1f} "
                  f"({elapsed:.0f}s)", flush=True)

            # Early stopping if no improvement for 15 generations
            if score <= prev_best[0] + 0.5:
                stall_count[0] += 5
            else:
                stall_count[0] = 0
                prev_best[0] = score
            if stall_count[0] >= 15:
                if verbose:
                    print(f"  Early stop: no improvement for 15 generations")
                return True  # stop DE

    try:
        de_result = differential_evolution(
            _cost_function,
            bounds=ALL_BOUNDS,
            args=(engine, target_profile, duration_s),
            maxiter=max_de_iterations,
            popsize=popsize,
            mutation=(0.5, 1.5),
            recombination=0.8,
            seed=42,
            tol=0.01,
            callback=de_callback,
            x0=x0,
            polish=False,
        )
        de_score = -de_result.fun
        if de_score > best_score:
            best_score = de_score
            best_x = de_result.x.copy()
            if verbose:
                print(f"  DE best: {best_score:.1f} ({de_result.nfev} evals)")
    except Exception as e:
        if verbose:
            print(f"  DE failed: {e}")

    # --- Phase 3: Nelder-Mead polish ---
    if verbose:
        print(f"\nPhase 3: Nelder-Mead polish...")

    try:
        nm_result = minimize(
            _cost_function,
            best_x,
            args=(engine, target_profile, duration_s),
            method='Nelder-Mead',
            options={
                'maxiter': 150,
                'xatol': 0.005,
                'fatol': 0.1,
                'adaptive': True,
            },
        )
        nm_score = -nm_result.fun
        if nm_score > best_score:
            best_score = nm_score
            best_x = nm_result.x.copy()
            if verbose:
                print(f"  NM improved to: {best_score:.1f} ({nm_result.nfev} evals)")
        elif verbose:
            print(f"  NM no improvement ({nm_result.nfev} evals)")
    except Exception as e:
        if verbose:
            print(f"  NM failed: {e}")

    # --- Phase 4: Coordinate descent refinement ---
    if verbose:
        print(f"\nPhase 4: Coordinate descent...")

    for param_round in range(2):
        improved = False
        for i in range(17):
            lo, hi = ALL_BOUNDS[i]
            current = best_x[i]

            # Narrow search around current best
            search_lo = max(lo, current - (hi - lo) * 0.1)
            search_hi = min(hi, current + (hi - lo) * 0.1)
            values = np.linspace(search_lo, search_hi, 9)

            for val in values:
                trial = best_x.copy()
                trial[i] = val
                score = _generate_and_compare(trial, engine, target_profile, duration_s)
                if score > best_score:
                    best_score = score
                    best_x = trial.copy()
                    improved = True

        if verbose:
            print(f"  Round {param_round + 1}: {best_score:.1f}"
                  f"{'  (improved)' if improved else ''}")
        if not improved:
            break

    # --- Final evaluation ---
    final_coeffs = np.clip(best_x[:16], -0.998, 0.998)
    final_rolloff = np.clip(best_x[16], 2000, 12000)

    ir_final = engine.generate_ir(duration_s=duration_s,
                                  coefficients=final_coeffs,
                                  rolloff_hz=final_rolloff)
    peak = np.max(np.abs(ir_final))
    if peak > 0:
        ir_final = ir_final / peak
    final_profile = analyze_ir(ir_final, sr, name="WCS_optimized")
    final_comparison = compare_profiles(target_profile, final_profile)

    elapsed = time.time() - start_time

    if verbose:
        print(f"\nFinal score: {final_comparison.overall_score:.1f}/100  ({elapsed:.1f}s)")
        print(final_comparison.summary())

    return OptimizationResult(
        target_name=target_name,
        program=program,
        program_name=PROGRAM_NAMES[program],
        best_coefficients=final_coeffs,
        best_rolloff_hz=final_rolloff,
        best_score=final_comparison.overall_score,
        comparison=final_comparison,
        elapsed_s=elapsed,
    )


def batch_optimize(ir_infos: list[tuple[str, str, int]],
                   sr: int = DEFAULT_MATCH_SR,
                   popsize: int = DEFAULT_POPSIZE,
                   max_iter: int = DEFAULT_MAX_ITER,
                   verbose: bool = True) -> list[OptimizationResult]:
    """
    Optimize multiple IRs.

    Args:
        ir_infos: list of (path, name, program_index) tuples
        sr: sample rate for matching
        popsize: DE population size multiplier
        max_iter: DE max iterations
        verbose: print progress

    Returns:
        List of OptimizationResults
    """
    results = []
    prev_coeffs = {}  # per-program warm start

    for i, (path, name, program) in enumerate(ir_infos):
        if verbose:
            print(f"\n{'#'*60}")
            print(f"# [{i+1}/{len(ir_infos)}] {name}")
            print(f"{'#'*60}")

        warm = prev_coeffs.get(program)
        result = optimize_for_target(
            path, program, name, sr=sr, verbose=verbose,
            warm_start=warm, popsize=popsize,
            max_de_iterations=max_iter,
        )
        results.append(result)

        # Save for warm-starting next IR of same program
        prev_coeffs[program] = np.append(result.best_coefficients,
                                         result.best_rolloff_hz)

    return results


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(
        description='Optimize WCS coefficients to match a target IR')
    parser.add_argument('ir_path', help='Path to target IR')
    parser.add_argument('--program', type=int, default=None,
                        help='Program index (auto-detected from filename)')
    parser.add_argument('--sr', type=int, default=DEFAULT_MATCH_SR,
                        help=f'Sample rate for matching (default: {DEFAULT_MATCH_SR})')
    parser.add_argument('--popsize', type=int, default=DEFAULT_POPSIZE,
                        help=f'DE population multiplier (default: {DEFAULT_POPSIZE})')
    parser.add_argument('--max-iter', type=int, default=DEFAULT_MAX_ITER,
                        help=f'DE max iterations (default: {DEFAULT_MAX_ITER})')
    args = parser.parse_args()

    name = os.path.splitext(os.path.basename(args.ir_path))[0]
    program = args.program
    if program is None:
        program = ir_name_to_program(os.path.basename(args.ir_path))

    result = optimize_for_target(args.ir_path, program, name,
                                 sr=args.sr, popsize=args.popsize,
                                 max_de_iterations=args.max_iter)
    print(f"\n{result.summary()}")
