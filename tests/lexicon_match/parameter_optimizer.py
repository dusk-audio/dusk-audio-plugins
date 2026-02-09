"""
Parameter Optimizer — Find SilkVerb parameters that best match a target Lexicon IR.

Two-phase strategy:
1. Smart grid search: calibrate RT60 per mode via binary search on size+damping,
   then test all mode/color combinations with calibrated params
2. Fine optimization with Nelder-Mead over continuous parameters
"""

import numpy as np
from scipy.optimize import minimize
from dataclasses import dataclass
from typing import Optional
import time

from ir_analysis import IRProfile, analyze_ir, load_ir, measure_rt60
from ir_compare import compare_profiles
from silkverb_capture import (
    SilkVerbParams, capture_ir, VALID_MODES, VALID_COLORS,
    VALID_SIZES, nearest_size, load_plugin, SIZE_TO_SECONDS,
)


@dataclass
class OptimizationResult:
    """Result of parameter optimization."""
    target_name: str
    best_params: SilkVerbParams
    best_score: float
    comparison: object  # ComparisonResult
    iterations: int
    elapsed_s: float
    search_history: list  # (score, params_dict) tuples

    def summary(self) -> str:
        lines = [
            f"=== Optimization Result: {self.target_name} ===",
            f"  Best Score: {self.best_score:.1f}/100",
            f"  Iterations: {self.iterations} | Time: {self.elapsed_s:.1f}s",
            f"  Parameters:",
        ]
        d = self.best_params.to_dict()
        for k, v in d.items():
            if k != 'mix':
                lines.append(f"    {k}: {v}")
        return "\n".join(lines)


# Subset of ER/Late balance values for optimization
VALID_ER_LATE = [
    'Early', 'E90/L9', 'E80/L19', 'E70/L29', 'E60/L39', 'E50/L50',
    'E40/L59', 'E30/L70', 'E20/L79', 'E10/L90', 'Late',
]


def _er_late_from_ratio(ratio: float) -> str:
    """Convert a 0-1 ratio to the nearest valid er_late string."""
    ratio = np.clip(ratio, 0, 1)
    if ratio <= 0.05:
        return 'Early'
    if ratio >= 0.95:
        return 'Late'
    best = 'E50/L50'
    best_dist = 999
    for v in VALID_ER_LATE:
        if v == 'Early':
            v_ratio = 0.0
        elif v == 'Late':
            v_ratio = 1.0
        else:
            parts = v.split('/')
            e = int(parts[0][1:])
            v_ratio = 1.0 - e / 100.0
        dist = abs(v_ratio - ratio)
        if dist < best_dist:
            best_dist = dist
            best = v
    return best


# Continuous parameter bounds
CONTINUOUS_BOUNDS = {
    'size_seconds': (0.1, 10.0),
    'room_size': (0.0, 100.0),
    'damping': (0.0, 100.0),
    'pre_delay_ms': (0.0, 250.0),
    'mod_rate_hz': (0.1, 5.0),
    'mod_depth': (0.0, 100.0),
    'width': (0.0, 100.0),
    'early_diff': (0.0, 100.0),
    'late_diff': (0.0, 100.0),
    'er_late_ratio': (0.0, 1.0),
    'bass_mult_x': (0.1, 3.0),
    'bass_freq_hz': (100.0, 1000.0),
    'hf_decay_x': (0.25, 4.0),
    'low_cut_hz': (20.0, 500.0),
    'high_cut_hz': (1000.0, 20000.0),
    'mid_decay_x': (0.25, 4.0),
    'high_freq_hz': (1000.0, 12000.0),
    'er_shape': (0.0, 100.0),
    'er_spread': (0.0, 100.0),
    'er_bass_cut_hz': (20.0, 500.0),
}


def _vector_to_params(x: np.ndarray, mode: str, color: str) -> SilkVerbParams:
    """Convert optimization vector to SilkVerbParams."""
    return SilkVerbParams(
        mode=mode,
        color=color,
        size=nearest_size(np.clip(x[0], 0.1, 10.0)),
        room_size=np.clip(x[1], 0, 100),
        damping=np.clip(x[2], 0, 100),
        pre_delay_ms=np.clip(x[3], 0, 250),
        mod_rate_hz=np.clip(x[4], 0.1, 5.0),
        mod_depth=np.clip(x[5], 0, 100),
        width=np.clip(x[6], 0, 100),
        early_diff=np.clip(x[7], 0, 100),
        late_diff=np.clip(x[8], 0, 100),
        er_late=_er_late_from_ratio(np.clip(x[9], 0, 1)),
        bass_mult_x=np.clip(x[10], 0.1, 3.0),
        bass_freq_hz=np.clip(x[11], 100, 1000),
        hf_decay_x=np.clip(x[12], 0.25, 4.0),
        low_cut_hz=np.clip(x[13], 20, 500),
        high_cut_hz=np.clip(x[14], 1000, 20000),
        mid_decay_x=np.clip(x[15], 0.25, 4.0),
        high_freq_hz=np.clip(x[16], 1000, 12000),
        er_shape=np.clip(x[17], 0, 100),
        er_spread=np.clip(x[18], 0, 100),
        er_bass_cut_hz=np.clip(x[19], 20, 500),
        mix=100.0,
    )


def _params_to_vector(p: SilkVerbParams) -> np.ndarray:
    """Convert SilkVerbParams to optimization vector."""
    size_s = SIZE_TO_SECONDS.get(p.size, 2.0)
    if p.er_late == 'Early':
        er_ratio = 0.0
    elif p.er_late == 'Late':
        er_ratio = 1.0
    else:
        try:
            parts = p.er_late.split('/')
            e = int(parts[0][1:])
            er_ratio = 1.0 - e / 100.0
        except (ValueError, IndexError):
            er_ratio = 0.5

    return np.array([
        size_s, p.room_size, p.damping, p.pre_delay_ms,
        p.mod_rate_hz, p.mod_depth, p.width,
        p.early_diff, p.late_diff, er_ratio,
        p.bass_mult_x, p.bass_freq_hz, p.hf_decay_x,
        p.low_cut_hz, p.high_cut_hz,
        p.mid_decay_x, p.high_freq_hz, p.er_shape, p.er_spread,
        p.er_bass_cut_hz,
    ])


def _quick_rt60(mode: str, size: str, damping: float, room_size: float,
                bass_mult: float, hf_decay: float, sr: int = 48000) -> float:
    """Quick RT60 measurement for a given mode/size/damping combo."""
    params = SilkVerbParams(
        mode=mode, color='Now', size=size, mix=100.0,
        damping=damping, room_size=room_size, pre_delay_ms=0.0,
        mod_depth=0.0, bass_mult_x=bass_mult, hf_decay_x=hf_decay,
    )
    ir = capture_ir(params, sr=sr, duration_s=min(SIZE_TO_SECONDS[size] * 3, 12.0),
                    normalize=False)
    return measure_rt60(ir[0], sr, 'T30')


def calibrate_rt60(target_rt60: float, mode: str, sr: int = 48000,
                   verbose: bool = False) -> tuple[str, float, float, float, float, float]:
    """
    Find size + damping + bass_mult + hf_decay that produce target RT60 for a mode.

    Returns (size, damping, room_size, bass_mult, hf_decay, mid_decay).
    Uses iterative search: first sweep damping at max size, then fine-tune size.
    """
    # Start with conservative params
    best_size = '4.9s'
    best_damping = 30.0
    best_room = 60.0
    best_bass = 1.2
    best_hf = 1.0
    best_error = float('inf')

    # Sweep 1: size at moderate damping/room to find ballpark
    for size in ['0.2s', '0.5s', '1.0s', '2.0s', '3.0s', '4.9s', '7.1s', '10.0s']:
        for damping in [15.0, 35.0, 55.0]:
            rt60 = _quick_rt60(mode, size, damping, 60.0, 1.2, 1.0, sr)
            error = abs(rt60 - target_rt60)
            if verbose:
                print(f"    cal {mode} size={size} damp={damping:.0f}: RT60={rt60:.3f}s (target={target_rt60:.3f}s)")
            if error < best_error:
                best_error = error
                best_size = size
                best_damping = damping

    # Sweep 2: fine-tune around best, including room_size and decay multipliers
    best_size_s = SIZE_TO_SECONDS[best_size]
    best_mid = 1.0
    for size_offset in [-1.0, -0.5, 0.0, 0.5, 1.0]:
        size = nearest_size(np.clip(best_size_s + size_offset, 0.1, 10.0))
        for damp_offset in [-10.0, 0.0, 10.0]:
            damping = np.clip(best_damping + damp_offset, 0, 100)
            for bass_mult in [1.0, 1.3, 1.6]:
                for hf_decay in [0.8, 1.0, 1.3]:
                    rt60 = _quick_rt60(mode, size, damping, 60.0, bass_mult, hf_decay, sr)
                    error = abs(rt60 - target_rt60)
                    if error < best_error:
                        best_error = error
                        best_size = size
                        best_damping = damping
                        best_bass = bass_mult
                        best_hf = hf_decay

    if verbose:
        print(f"    -> best: size={best_size} damp={best_damping:.0f} "
              f"bass={best_bass:.1f} hf={best_hf:.1f} error={best_error:.3f}s")

    return best_size, best_damping, best_room, best_bass, best_hf, best_mid


def _evaluate(x: np.ndarray, mode: str, color: str,
              target_profile: IRProfile, sr: int,
              capture_duration: float) -> float:
    """Cost function: negative comparison score."""
    params = _vector_to_params(x, mode, color)
    ir = capture_ir(params, sr=sr, duration_s=capture_duration, normalize=True)
    candidate_profile = analyze_ir(ir, sr, name=f"SilkVerb({mode}/{color})")
    result = compare_profiles(target_profile, candidate_profile)
    return -result.overall_score


def optimize_for_target(target_path: str,
                        target_name: str = None,
                        category: str = 'Halls',
                        fixed_mode: str = None,
                        fixed_color: str = None,
                        max_iterations: int = 80,
                        sr: int = 48000,
                        verbose: bool = True) -> OptimizationResult:
    """
    Optimize SilkVerb parameters to match a target Lexicon IR.

    Strategy:
    1. RT60 calibration per mode (find size/damping that produces target RT60)
    2. Score each mode/color combo with calibrated params
    3. Fine-tune continuous params with Nelder-Mead from best starting point
    """
    start_time = time.time()
    history = []

    if target_name is None:
        import os
        target_name = os.path.splitext(os.path.basename(target_path))[0]

    # Load and analyze target
    target_data, target_sr = load_ir(target_path)
    if target_sr != sr:
        from scipy.signal import resample
        n_new = int(target_data.shape[1] * sr / target_sr)
        target_data = np.array([resample(target_data[c], n_new)
                                for c in range(target_data.shape[0])])

    target_profile = analyze_ir(target_data, sr, name=target_name)
    capture_duration = min(target_profile.duration_s + 2.0, 12.0)

    if verbose:
        from ir_analysis import profile_summary
        print(f"\n{'='*60}")
        print(f"Optimizing for: {target_name}")
        print(profile_summary(target_profile))
        print(f"{'='*60}\n")

    # Phase 1: Mode/Color grid search with RT60 calibration
    modes = [fixed_mode] if fixed_mode else _suggest_modes(category)
    colors = [fixed_color] if fixed_color else VALID_COLORS

    best_score = -999
    best_mode = modes[0]
    best_color = colors[0]
    best_vector = None

    if verbose:
        print(f"Phase 1: RT60 calibration + {len(modes)} modes x {len(colors)} colors...")

    # Calibrate RT60 per mode
    mode_calibrations = {}
    for mode in modes:
        if verbose:
            print(f"\n  Calibrating RT60 for {mode}...")
        cal = calibrate_rt60(target_profile.rt60, mode, sr, verbose=verbose)
        mode_calibrations[mode] = cal

    # Test each mode/color combination with calibrated params
    if verbose:
        print(f"\n  Evaluating combinations...")

    for mode in modes:
        cal_size, cal_damping, cal_room, cal_bass, cal_hf, cal_mid = mode_calibrations[mode]
        for color in colors:
            params = SilkVerbParams(
                mode=mode,
                color=color,
                size=cal_size,
                room_size=cal_room,
                damping=cal_damping,
                pre_delay_ms=min(target_profile.pre_delay_ms, 250.0),
                mod_rate_hz=0.8,
                mod_depth=25.0,
                width=80.0,
                early_diff=55.0,
                late_diff=55.0,
                er_late='E40/L59',
                bass_mult_x=cal_bass,
                bass_freq_hz=400.0,
                hf_decay_x=cal_hf,
                low_cut_hz=20.0,
                high_cut_hz=20000.0,
                mid_decay_x=1.0,
                high_freq_hz=4000.0,
                er_shape=50.0,
                er_spread=50.0,
                mix=100.0,
            )

            ir = capture_ir(params, sr=sr, duration_s=capture_duration, normalize=True)
            cand_profile = analyze_ir(ir, sr, name=f"SilkVerb({mode}/{color})")
            result = compare_profiles(target_profile, cand_profile)
            score = result.overall_score

            history.append((score, params.to_dict()))

            if verbose:
                print(f"    {mode:15s} / {color:5s}: {score:.1f}  "
                      f"(RT60={cand_profile.rt60:.2f}s vs {target_profile.rt60:.2f}s)")

            if score > best_score:
                best_score = score
                best_mode = mode
                best_color = color
                best_vector = _params_to_vector(params)

    if verbose:
        print(f"\n  Best initial: {best_mode}/{best_color} = {best_score:.1f}")

    # Phase 1.5: Sweep new 4-band decay + ER params at best mode/color
    if verbose:
        print(f"\nPhase 1.5: Sweeping decay/ER params at {best_mode}/{best_color}...")

    base_params = _vector_to_params(best_vector, best_mode, best_color)
    for mid_decay in [0.5, 1.0, 2.0]:
        for high_freq in [3000.0, 5000.0, 8000.0]:
            for er_shape in [20.0, 50.0, 80.0]:
                for er_spread in [25.0, 50.0, 75.0]:
                    trial = SilkVerbParams(
                        mode=best_mode, color=best_color,
                        size=base_params.size, room_size=base_params.room_size,
                        damping=base_params.damping, pre_delay_ms=base_params.pre_delay_ms,
                        mod_rate_hz=base_params.mod_rate_hz, mod_depth=base_params.mod_depth,
                        width=base_params.width, early_diff=base_params.early_diff,
                        late_diff=base_params.late_diff, er_late=base_params.er_late,
                        bass_mult_x=base_params.bass_mult_x, bass_freq_hz=base_params.bass_freq_hz,
                        hf_decay_x=base_params.hf_decay_x, low_cut_hz=base_params.low_cut_hz,
                        high_cut_hz=base_params.high_cut_hz,
                        mid_decay_x=mid_decay, high_freq_hz=high_freq,
                        er_shape=er_shape, er_spread=er_spread,
                        mix=100.0,
                    )
                    ir = capture_ir(trial, sr=sr, duration_s=capture_duration, normalize=True)
                    cand = analyze_ir(ir, sr, name=f"SilkVerb(sweep)")
                    res = compare_profiles(target_profile, cand)
                    if res.overall_score > best_score:
                        best_score = res.overall_score
                        best_vector = _params_to_vector(trial)
                        if verbose:
                            print(f"    mid={mid_decay:.1f} hf={high_freq:.0f} "
                                  f"shape={er_shape:.0f} spread={er_spread:.0f}: {best_score:.1f}")

    # Phase 1.5b: Sweep bass control params (bass_mult_x, er_bass_cut_hz)
    base_params = _vector_to_params(best_vector, best_mode, best_color)
    for bass_mult in [0.3, 0.7, 1.0, 1.5]:
        for er_bass_cut in [20.0, 120.0, 300.0]:
            trial = SilkVerbParams(
                mode=best_mode, color=best_color,
                size=base_params.size, room_size=base_params.room_size,
                damping=base_params.damping, pre_delay_ms=base_params.pre_delay_ms,
                mod_rate_hz=base_params.mod_rate_hz, mod_depth=base_params.mod_depth,
                width=base_params.width, early_diff=base_params.early_diff,
                late_diff=base_params.late_diff, er_late=base_params.er_late,
                bass_mult_x=bass_mult, bass_freq_hz=base_params.bass_freq_hz,
                hf_decay_x=base_params.hf_decay_x, low_cut_hz=base_params.low_cut_hz,
                high_cut_hz=base_params.high_cut_hz,
                mid_decay_x=base_params.mid_decay_x, high_freq_hz=base_params.high_freq_hz,
                er_shape=base_params.er_shape, er_spread=base_params.er_spread,
                er_bass_cut_hz=er_bass_cut,
                mix=100.0,
            )
            ir = capture_ir(trial, sr=sr, duration_s=capture_duration, normalize=True)
            cand = analyze_ir(ir, sr, name=f"SilkVerb(bass)")
            res = compare_profiles(target_profile, cand)
            if res.overall_score > best_score:
                best_score = res.overall_score
                best_vector = _params_to_vector(trial)
                if verbose:
                    print(f"    bass={bass_mult:.1f} erBC={er_bass_cut:.0f}: {best_score:.1f}")

    if verbose:
        print(f"  After sweep: {best_score:.1f}")

    # Phase 2: Fine optimization with Nelder-Mead
    if verbose:
        print(f"\nPhase 2: Fine-tuning continuous parameters (max {max_iterations} iters)...")

    iteration_count = [0]
    best_found = [best_score]

    def callback(xk):
        iteration_count[0] += 1
        if verbose and iteration_count[0] % 10 == 0:
            score = -_evaluate(xk, best_mode, best_color, target_profile, sr, capture_duration)
            if score > best_found[0]:
                best_found[0] = score
            print(f"    Iter {iteration_count[0]}: score={score:.1f} (best={best_found[0]:.1f})")

    result = minimize(
        _evaluate,
        best_vector,
        args=(best_mode, best_color, target_profile, sr, capture_duration),
        method='Nelder-Mead',
        options={
            'maxiter': max_iterations,
            'xatol': 0.3,
            'fatol': 0.3,
            'adaptive': True,
        },
        callback=callback,
    )

    best_params = _vector_to_params(result.x, best_mode, best_color)
    final_score = -result.fun

    # Try a few random restarts near the best to avoid local minima
    if verbose:
        print(f"\n  Trying perturbations...")

    for trial in range(10):
        perturbed = result.x.copy()
        # Randomly perturb 4-6 parameters
        n_perturb = np.random.randint(4, 8)
        indices = np.random.choice(len(perturbed), n_perturb, replace=False)
        for idx in indices:
            bounds = list(CONTINUOUS_BOUNDS.values())[idx]
            range_size = bounds[1] - bounds[0]
            perturbed[idx] += np.random.uniform(-0.2, 0.2) * range_size
            perturbed[idx] = np.clip(perturbed[idx], bounds[0], bounds[1])

        trial_score = -_evaluate(perturbed, best_mode, best_color, target_profile, sr, capture_duration)
        if trial_score > final_score:
            final_score = trial_score
            best_params = _vector_to_params(perturbed, best_mode, best_color)
            if verbose:
                print(f"    Perturbation {trial+1}: improved to {final_score:.1f}")

    # Final comparison for detailed report
    final_ir = capture_ir(best_params, sr=sr, duration_s=capture_duration, normalize=True)
    final_profile = analyze_ir(final_ir, sr, name=f"SilkVerb(optimized)")
    final_comparison = compare_profiles(target_profile, final_profile)

    elapsed = time.time() - start_time

    if verbose:
        print(f"\nOptimization complete in {elapsed:.1f}s")
        print(f"Final score: {final_comparison.overall_score:.1f}/100")
        print(final_comparison.summary())

    return OptimizationResult(
        target_name=target_name,
        best_params=best_params,
        best_score=final_comparison.overall_score,
        comparison=final_comparison,
        iterations=iteration_count[0],
        elapsed_s=elapsed,
        search_history=history,
    )


def _suggest_modes(category: str) -> list[str]:
    """Suggest relevant SilkVerb modes for a Lexicon category."""
    if category == 'Halls':
        return ['Hall', 'Bright Hall', 'Cathedral', 'Chamber']
    elif category == 'Plates':
        return ['Plate', 'Bright Hall', 'Ambience']
    elif category == 'Rooms':
        return ['Room', 'Chamber', 'Ambience']
    else:  # Post
        return ['Room', 'Hall', 'Chamber', 'Chorus Space', 'Dirty Hall']


def batch_optimize(ir_paths: list[tuple[str, str, str]],
                   max_iterations: int = 60,
                   verbose: bool = True) -> list[OptimizationResult]:
    """
    Optimize multiple targets in batch.
    ir_paths: list of (path, name, category) tuples
    """
    results = []
    for i, (path, name, category) in enumerate(ir_paths):
        if verbose:
            print(f"\n{'#'*60}")
            print(f"# [{i+1}/{len(ir_paths)}] {name}")
            print(f"{'#'*60}")
        result = optimize_for_target(
            path, name, category,
            max_iterations=max_iterations,
            verbose=verbose,
        )
        results.append(result)
    return results
