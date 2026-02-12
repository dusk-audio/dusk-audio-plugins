"""
Parameter Optimizer — Find Velvet 90 parameters that best match a target IR.

Strategy (with warm-start):
1. Start from previous best parameters
2. Coordinate descent: sweep each parameter independently (9 values each)
3. Nelder-Mead polish with increased iterations
4. Random perturbation trials

Strategy (cold start — no previous result):
1. RT60 calibration + mode/color grid search
2. Coordinate descent on all parameters
3. Nelder-Mead polish
4. Random perturbation trials
"""

import numpy as np
from scipy.optimize import minimize
from dataclasses import dataclass
from typing import Optional
import time

from ir_analysis import IRProfile, analyze_ir, load_ir, measure_rt60
from ir_compare import compare_profiles
from velvet90_capture import (
    Velvet90Params, capture_ir, VALID_MODES, VALID_COLORS,
    VALID_SIZES, nearest_size, load_plugin, SIZE_TO_SECONDS,
)


@dataclass
class OptimizationResult:
    """Result of parameter optimization."""
    target_name: str
    best_params: Velvet90Params
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


# Parameter vector layout (41 dimensions):
#  0: size_seconds    1: room_size       2: damping         3: pre_delay_ms
#  4: mod_rate_hz     5: mod_depth       6: width           7: early_diff
#  8: late_diff       9: er_late_ratio  10: bass_mult_x    11: bass_freq_hz
# 12: hf_decay_x     13: low_cut_hz     14: high_cut_hz    15: mid_decay_x
# 16: high_freq_hz   17: er_shape       18: er_spread      19: er_bass_cut_hz
# 20: treble_ratio   21: stereo_coupling
# 22: low_mid_freq_hz 23: low_mid_decay_x
# 24: env_mode       25: env_hold_ms    26: env_release_ms  27: env_depth
# 28: echo_delay_ms  29: echo_feedback
# 30: out_eq1_freq_hz 31: out_eq1_gain_db 32: out_eq1_q
# 33: out_eq2_freq_hz 34: out_eq2_gain_db 35: out_eq2_q
# 36: stereo_invert  37: resonance
# 38: echo_pingpong  39: dyn_amount     40: dyn_speed

PARAM_NAMES = [
    'size_seconds', 'room_size', 'damping', 'pre_delay_ms',
    'mod_rate_hz', 'mod_depth', 'width', 'early_diff',
    'late_diff', 'er_late_ratio', 'bass_mult_x', 'bass_freq_hz',
    'hf_decay_x', 'low_cut_hz', 'high_cut_hz', 'mid_decay_x',
    'high_freq_hz', 'er_shape', 'er_spread', 'er_bass_cut_hz',
    'treble_ratio', 'stereo_coupling',
    'low_mid_freq_hz', 'low_mid_decay_x',
    'env_mode', 'env_hold_ms', 'env_release_ms', 'env_depth', 'echo_delay_ms',
    'echo_feedback',
    'out_eq1_freq_hz', 'out_eq1_gain_db', 'out_eq1_q',
    'out_eq2_freq_hz', 'out_eq2_gain_db', 'out_eq2_q',
    'stereo_invert', 'resonance',
    'echo_pingpong', 'dyn_amount', 'dyn_speed',
]

# Bounds for each parameter
BOUNDS = [
    (0.1, 10.0),      # size_seconds
    (0.0, 100.0),      # room_size
    (0.0, 100.0),      # damping
    (0.0, 250.0),      # pre_delay_ms
    (0.1, 5.0),        # mod_rate_hz
    (0.0, 100.0),      # mod_depth
    (0.0, 100.0),      # width
    (0.0, 100.0),      # early_diff
    (0.0, 100.0),      # late_diff
    (0.0, 1.0),        # er_late_ratio
    (0.1, 3.0),        # bass_mult_x
    (100.0, 1000.0),   # bass_freq_hz
    (0.25, 4.0),       # hf_decay_x
    (20.0, 500.0),     # low_cut_hz
    (1000.0, 20000.0), # high_cut_hz
    (0.25, 4.0),       # mid_decay_x
    (1000.0, 12000.0), # high_freq_hz
    (0.0, 100.0),      # er_shape
    (0.0, 100.0),      # er_spread
    (20.0, 500.0),     # er_bass_cut_hz
    (0.3, 2.0),        # treble_ratio
    (0.0, 50.0),       # stereo_coupling (pedalboard display %, JUCE 0.0-0.5)
    (100.0, 8000.0),   # low_mid_freq_hz
    (0.25, 4.0),       # low_mid_decay_x
    (0, 4),            # env_mode (discrete: 0=Off, 1=Gate, 2=Reverse, 3=Swell, 4=Ducked)
    (10, 2000),        # env_hold_ms
    (10, 3000),        # env_release_ms
    (0, 100),          # env_depth (pedalboard display %)
    (0, 500),          # echo_delay_ms
    (0, 90),           # echo_feedback (pedalboard display %)
    (100.0, 8000.0),   # out_eq1_freq_hz
    (-12.0, 12.0),     # out_eq1_gain_db
    (0.3, 5.0),        # out_eq1_q
    (100.0, 8000.0),   # out_eq2_freq_hz
    (-12.0, 12.0),     # out_eq2_gain_db
    (0.3, 5.0),        # out_eq2_q
    (0.0, 1.0),        # stereo_invert
    (0.0, 1.0),        # resonance
    (0.0, 1.0),        # echo_pingpong (cross-channel echo feedback)
    (-1.0, 1.0),       # dyn_amount (neg=duck, pos=expand)
    (0.0, 1.0),        # dyn_speed (envelope follower speed)
]

# High-impact parameters for coordinate descent (indices into vector)
# These affect the highest-weighted scoring dimensions
HIGH_IMPACT_PARAMS = [
    0,   # size_seconds → RT60 (25%)
    1,   # room_size → EDC shape (15%), EDT (5%)
    2,   # damping → RT60 (25%), band_rt60 (25%)
    3,   # pre_delay_ms → pre-delay (5%)
    6,   # width → stereo (12%)
    7,   # early_diff → EDC shape (15%)
    8,   # late_diff → EDC shape (15%)
    9,   # er_late_ratio → EDC shape, spectral
    10,  # bass_mult_x → band_rt60 (25%)
    11,  # bass_freq_hz → band_rt60 (25%)
    12,  # hf_decay_x → band_rt60 (25%)
    15,  # mid_decay_x → band_rt60 (25%)
    16,  # high_freq_hz → band_rt60, spectral
    17,  # er_shape → spectral early (3%)
    18,  # er_spread → spectral, EDC
    19,  # er_bass_cut_hz → band_rt60
    20,  # treble_ratio → band_rt60 (25%), spectral (5%)
    21,  # stereo_coupling → stereo (12%)
    22,  # low_mid_freq_hz → band_rt60 (25%)
    23,  # low_mid_decay_x → band_rt60 (25%)
    24,  # env_mode → edc_shape (15%), RT60 (25%)
    25,  # env_hold_ms → edc_shape (15%), RT60 (25%)
    26,  # env_release_ms → edc_shape (15%), RT60 (25%)
    27,  # env_depth → edc_shape (15%), RT60 (25%)
    28,  # echo_delay_ms → edc_shape (15%), pre-delay (5%)
    30,  # out_eq1_freq_hz → spectral (8%), centroid (5%)
    31,  # out_eq1_gain_db → spectral (8%), centroid (5%)
    33,  # out_eq2_freq_hz → spectral (8%), centroid (5%)
    34,  # out_eq2_gain_db → spectral (8%), centroid (5%)
    36,  # stereo_invert → stereo (12%)
    37,  # resonance → spectral (8%), edc_shape (15%)
    38,  # echo_pingpong → stereo (12%), edc_shape (15%)
    39,  # dyn_amount → edc_shape (15%), RT60 (25%)
    40,  # dyn_speed → edc_shape (15%)
]

# Also maintained for backward compat
CONTINUOUS_BOUNDS = dict(zip(PARAM_NAMES, BOUNDS))


ENV_MODE_NAMES = ['Off', 'Gate', 'Reverse', 'Swell', 'Ducked']


def _vector_to_params(x: np.ndarray, mode: str, color: str) -> Velvet90Params:
    """Convert optimization vector to Velvet 90 Params."""
    # Envelope mode: discrete 0-4 (index 24)
    env_mode_idx = int(np.clip(np.round(x[24]), 0, 4)) if len(x) > 24 else 0
    env_mode_str = ENV_MODE_NAMES[env_mode_idx]

    return Velvet90Params(
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
        treble_ratio=np.clip(x[20], 0.3, 2.0),
        stereo_coupling=np.clip(x[21], 0.0, 50.0),
        low_mid_freq_hz=np.clip(x[22], 100, 8000) if len(x) > 22 else 700.0,
        low_mid_decay_x=np.clip(x[23], 0.25, 4.0) if len(x) > 23 else 1.0,
        env_mode=env_mode_str,
        env_hold_ms=np.clip(x[25], 10, 2000) if len(x) > 25 else 500.0,
        env_release_ms=np.clip(x[26], 10, 3000) if len(x) > 26 else 500.0,
        env_depth=np.clip(x[27], 0, 100) if len(x) > 27 else 0.0,
        echo_delay_ms=np.clip(x[28], 0, 500) if len(x) > 28 else 0.0,
        echo_feedback=np.clip(x[29], 0, 90) if len(x) > 29 else 0.0,
        out_eq1_freq_hz=np.clip(x[30], 100, 8000) if len(x) > 30 else 1000.0,
        out_eq1_gain_db=np.clip(x[31], -12, 12) if len(x) > 31 else 0.0,
        out_eq1_q=np.clip(x[32], 0.3, 5.0) if len(x) > 32 else 1.0,
        out_eq2_freq_hz=np.clip(x[33], 100, 8000) if len(x) > 33 else 4000.0,
        out_eq2_gain_db=np.clip(x[34], -12, 12) if len(x) > 34 else 0.0,
        out_eq2_q=np.clip(x[35], 0.3, 5.0) if len(x) > 35 else 1.0,
        stereo_invert=np.clip(x[36], 0.0, 1.0) if len(x) > 36 else 0.0,
        resonance=np.clip(x[37], 0.0, 1.0) if len(x) > 37 else 0.0,
        echo_pingpong=np.clip(x[38], 0.0, 1.0) if len(x) > 38 else 0.0,
        dyn_amount=np.clip(x[39], -1.0, 1.0) if len(x) > 39 else 0.0,
        dyn_speed=np.clip(x[40], 0.0, 1.0) if len(x) > 40 else 0.5,
        mix=100.0,
    )


def _params_to_vector(p: Velvet90Params) -> np.ndarray:
    """Convert Velvet 90 Params to optimization vector."""
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

    # Map env_mode string to int
    env_mode_str = getattr(p, 'env_mode', 'Off')
    env_mode_idx = ENV_MODE_NAMES.index(env_mode_str) if env_mode_str in ENV_MODE_NAMES else 0

    return np.array([
        size_s, p.room_size, p.damping, p.pre_delay_ms,
        p.mod_rate_hz, p.mod_depth, p.width,
        p.early_diff, p.late_diff, er_ratio,
        p.bass_mult_x, p.bass_freq_hz, p.hf_decay_x,
        p.low_cut_hz, p.high_cut_hz,
        p.mid_decay_x, p.high_freq_hz, p.er_shape, p.er_spread,
        p.er_bass_cut_hz,
        p.treble_ratio, p.stereo_coupling,
        getattr(p, 'low_mid_freq_hz', 700.0),
        getattr(p, 'low_mid_decay_x', 1.0),
        float(env_mode_idx),
        getattr(p, 'env_hold_ms', 500.0),
        getattr(p, 'env_release_ms', 500.0),
        getattr(p, 'env_depth', 0.0),
        getattr(p, 'echo_delay_ms', 0.0),
        getattr(p, 'echo_feedback', 0.0),
        getattr(p, 'out_eq1_freq_hz', 1000.0),
        getattr(p, 'out_eq1_gain_db', 0.0),
        getattr(p, 'out_eq1_q', 1.0),
        getattr(p, 'out_eq2_freq_hz', 4000.0),
        getattr(p, 'out_eq2_gain_db', 0.0),
        getattr(p, 'out_eq2_q', 1.0),
        getattr(p, 'stereo_invert', 0.0),
        getattr(p, 'resonance', 0.0),
        getattr(p, 'echo_pingpong', 0.0),
        getattr(p, 'dyn_amount', 0.0),
        getattr(p, 'dyn_speed', 0.5),
    ])


def _quick_rt60(mode: str, size: str, damping: float, room_size: float,
                bass_mult: float, hf_decay: float, sr: int = 48000) -> float:
    """Quick RT60 measurement for a given mode/size/damping combo."""
    params = Velvet90Params(
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

    # Sweep 2: fine-tune around best
    best_size_s = SIZE_TO_SECONDS[best_size]
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

    return best_size, best_damping, best_room, best_bass, best_hf, 1.0  # mid_decay fixed at 1.0


def _evaluate(x: np.ndarray, mode: str, color: str,
              target_profile: IRProfile, sr: int,
              capture_duration: float) -> float:
    """Cost function: negative comparison score."""
    params = _vector_to_params(x, mode, color)
    ir = capture_ir(params, sr=sr, duration_s=capture_duration, normalize=True)
    candidate_profile = analyze_ir(ir, sr, name=f"Velvet90({mode}/{color})")
    result = compare_profiles(target_profile, candidate_profile)
    return -result.overall_score


def _coordinate_descent(vector: np.ndarray, mode: str, color: str,
                        target_profile: IRProfile, sr: int,
                        capture_duration: float, n_rounds: int = 2,
                        n_points: int = 9, verbose: bool = False) -> tuple[np.ndarray, float]:
    """
    Coordinate descent: optimize each high-impact parameter independently.

    For each parameter, sweep n_points values across its range (centered on
    current value) while holding all others fixed. Repeat for n_rounds.

    Much more effective than grid search for high-dimensional spaces because
    it explores each dimension thoroughly (~9 values) rather than coarsely
    (~3 values) in a combinatorial grid.
    """
    best_vec = vector.copy()
    best_score = -_evaluate(best_vec, mode, color, target_profile, sr, capture_duration)

    for round_num in range(n_rounds):
        improved_this_round = False
        for param_idx in HIGH_IMPACT_PARAMS:
            lo, hi = BOUNDS[param_idx]
            current = best_vec[param_idx]

            # Generate sweep values: span from low to high, always include current
            # On round 2+, narrow the range to ±30% of current range for refinement
            if round_num == 0:
                sweep_lo, sweep_hi = lo, hi
            else:
                rng = (hi - lo) * 0.3
                sweep_lo = max(lo, current - rng)
                sweep_hi = min(hi, current + rng)

            values = np.linspace(sweep_lo, sweep_hi, n_points)
            # Always include the current best value
            values = np.unique(np.append(values, current))

            for val in values:
                trial = best_vec.copy()
                trial[param_idx] = val
                score = -_evaluate(trial, mode, color, target_profile, sr, capture_duration)
                if score > best_score:
                    best_score = score
                    best_vec = trial.copy()
                    improved_this_round = True

            if verbose and round_num == 0:
                print(f"    {PARAM_NAMES[param_idx]:16s}: {best_score:.1f}", flush=True)

        if verbose:
            print(f"  Round {round_num + 1}: {best_score:.1f}"
                  f"{'  (improved)' if improved_this_round else ''}", flush=True)

        if not improved_this_round:
            break

    return best_vec, best_score


def optimize_for_target(target_path: str,
                        target_name: str = None,
                        category: str = 'Halls',
                        fixed_mode: str = None,
                        fixed_color: str = None,
                        max_iterations: int = 150,
                        sr: int = 48000,
                        verbose: bool = True,
                        warm_start_params: dict = None) -> OptimizationResult:
    """
    Optimize Velvet 90 parameters to match a target IR.

    With warm_start_params:
      - Skip Phase 1 (RT60 calibration + grid search)
      - Go straight to coordinate descent from warm-start point
      - Much faster: ~200 renders vs ~400

    Without warm_start_params (cold start):
      - Phase 1: RT60 calibration + mode/color grid search
      - Phase 2: Coordinate descent on high-impact params
      - Phase 3: Nelder-Mead polish
      - Phase 4: Perturbation trials
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

    # --- Determine starting point ---
    if warm_start_params is not None:
        # Warm start: use previous best parameters directly
        ws_params = Velvet90Params.from_dict(warm_start_params)
        best_mode = ws_params.mode
        best_color = ws_params.color
        best_vector = _params_to_vector(ws_params)

        # Evaluate warm-start point
        warm_score = -_evaluate(best_vector, best_mode, best_color,
                                target_profile, sr, capture_duration)
        best_score = warm_score

        if verbose:
            print(f"Warm start: {best_mode}/{best_color} = {warm_score:.1f}")
            print(f"  (Skipping Phase 1 — RT60 cal + grid search)")

    else:
        # Cold start: full Phase 1
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
                params = Velvet90Params(
                    mode=mode, color=color, size=cal_size,
                    room_size=cal_room, damping=cal_damping,
                    pre_delay_ms=min(target_profile.pre_delay_ms, 250.0),
                    mod_rate_hz=0.8, mod_depth=25.0, width=80.0,
                    early_diff=55.0, late_diff=55.0, er_late='E40/L59',
                    bass_mult_x=cal_bass, bass_freq_hz=400.0,
                    hf_decay_x=cal_hf, low_cut_hz=20.0,
                    high_cut_hz=20000.0, mid_decay_x=1.0,
                    high_freq_hz=4000.0, er_shape=50.0,
                    er_spread=50.0, mix=100.0,
                )

                ir = capture_ir(params, sr=sr, duration_s=capture_duration, normalize=True)
                cand_profile = analyze_ir(ir, sr, name=f"Velvet90({mode}/{color})")
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

    # --- Phase 1.5: Envelope mode probe ---
    # The envelope params (mode + depth) are coupled — both must change together
    # for any effect. Coordinate descent sweeps one at a time, so it can never
    # discover that (Gate, depth=80%) beats (Off, depth=0%). We probe explicitly.
    if verbose:
        print(f"\nPhase 1.5: Envelope mode probe...")

    env_probe_configs = [
        # (mode_idx, depth%, hold_ms, release_ms, echo_ms, echo_fb%)
        (0, 0, 500, 500, 0, 0),       # Off (baseline)
        (1, 80, 200, 300, 0, 0),      # Gate short
        (1, 80, 500, 500, 0, 0),      # Gate medium
        (1, 80, 1000, 800, 0, 0),     # Gate long
        (1, 100, 300, 200, 0, 0),     # Gate full
        (2, 80, 500, 500, 0, 0),      # Reverse medium
        (2, 80, 1000, 800, 0, 0),     # Reverse long
        (2, 100, 800, 500, 0, 0),     # Reverse full
        (3, 80, 500, 500, 0, 0),      # Swell medium
        (3, 80, 1000, 1000, 0, 0),    # Swell long
        (4, 80, 200, 300, 0, 0),      # Ducked short
        (4, 80, 500, 500, 0, 0),      # Ducked medium
        (4, 80, 1000, 800, 0, 0),     # Ducked long
        (1, 80, 500, 500, 100, 0),    # Gate + echo 100ms
        (1, 80, 500, 500, 200, 0),    # Gate + echo 200ms
        (1, 80, 500, 500, 150, 50),   # Gate + echo + feedback
        (2, 80, 800, 500, 150, 0),    # Reverse + echo 150ms
        (2, 80, 800, 500, 150, 50),   # Reverse + echo + feedback
    ]

    # Probe envelope configs without ping-pong first, then with — so ping-pong
    # improvements can only build on top of the best non-ping-pong baseline.
    for env_mode, env_depth, env_hold, env_release, echo_delay, echo_fb in env_probe_configs:
        trial = best_vector.copy()
        trial[24] = float(env_mode)
        trial[25] = float(env_hold)
        trial[26] = float(env_release)
        trial[27] = float(env_depth)
        trial[28] = float(echo_delay)
        trial[29] = float(echo_fb)
        trial[38] = 0.0  # no ping-pong
        score = -_evaluate(trial, best_mode, best_color, target_profile, sr, capture_duration)
        if score > best_score:
            if verbose:
                mode_name = ENV_MODE_NAMES[env_mode]
                print(f"    {mode_name} d={env_depth}% h={env_hold}ms r={env_release}ms "
                      f"echo={echo_delay}ms: {score:.1f} (+{score - best_score:.1f})")
            best_score = score
            best_vector = trial.copy()

    # Then probe echo ping-pong variants on top of current best
    for env_mode, env_depth, env_hold, env_release, echo_delay, echo_fb in env_probe_configs:
        if echo_delay > 0:
            for pp in [0.5, 1.0]:
                trial = best_vector.copy()
                trial[24] = float(env_mode)
                trial[25] = float(env_hold)
                trial[26] = float(env_release)
                trial[27] = float(env_depth)
                trial[28] = float(echo_delay)
                trial[29] = float(echo_fb)
                trial[38] = pp  # echo_pingpong
                score = -_evaluate(trial, best_mode, best_color, target_profile, sr, capture_duration)
                if score > best_score:
                    if verbose:
                        mode_name = ENV_MODE_NAMES[env_mode]
                        print(f"    {mode_name} echo={echo_delay}ms pp={pp:.1f}: "
                              f"{score:.1f} (+{score - best_score:.1f})")
                    best_score = score
                    best_vector = trial.copy()

    if verbose:
        env_idx = int(np.round(best_vector[24]))
        print(f"  After envelope probe: {best_score:.1f} "
              f"(mode={ENV_MODE_NAMES[env_idx]}, depth={best_vector[27]:.0f}%)")

    # --- Phase 1.6: Stereo invert + resonance probe ---
    # stereo_invert is binary-ish (0 or ~1 for anti-correlated stereo),
    # coordinate descent can find intermediate values but we need to probe
    # the extreme to let it discover large jumps in stereo score.
    if verbose:
        print(f"\nPhase 1.6: Stereo invert + resonance probe...")

    for si_val in [0.0, 0.3, 0.6, 1.0]:
        for res_val in [0.0, 0.3, 0.6, 1.0]:
            if si_val == 0.0 and res_val == 0.0:
                continue  # skip default (already tested)
            trial = best_vector.copy()
            trial[36] = si_val
            trial[37] = res_val
            score = -_evaluate(trial, best_mode, best_color, target_profile, sr, capture_duration)
            if score > best_score:
                if verbose:
                    print(f"    stereo_invert={si_val:.1f} resonance={res_val:.1f}: "
                          f"{score:.1f} (+{score - best_score:.1f})")
                best_score = score
                best_vector = trial.copy()

    if verbose:
        print(f"  After stereo/resonance probe: {best_score:.1f} "
              f"(si={best_vector[36]:.2f}, res={best_vector[37]:.2f})")

    # --- Phase 1.7: Dynamics probe ---
    # Dynamics amount and speed are coupled — amount=0 is bypass regardless of speed.
    # Probe duck/expand with different speeds to discover input-level-dependent presets.
    if verbose:
        print(f"\nPhase 1.7: Dynamics probe...")

    for dyn_amt in [-1.0, -0.5, 0.5, 1.0]:
        for dyn_spd in [0.25, 0.5, 0.75]:
            trial = best_vector.copy()
            trial[39] = dyn_amt
            trial[40] = dyn_spd
            score = -_evaluate(trial, best_mode, best_color, target_profile, sr, capture_duration)
            if score > best_score:
                label = "duck" if dyn_amt < 0 else "expand"
                if verbose:
                    print(f"    {label} amt={dyn_amt:.1f} spd={dyn_spd:.2f}: "
                          f"{score:.1f} (+{score - best_score:.1f})")
                best_score = score
                best_vector = trial.copy()

    if verbose:
        print(f"  After dynamics probe: {best_score:.1f} "
              f"(amt={best_vector[39]:.2f}, spd={best_vector[40]:.2f})")

    # --- Phase 2: Coordinate descent on high-impact parameters ---
    if verbose:
        print(f"\nPhase 2: Coordinate descent ({len(HIGH_IMPACT_PARAMS)} params × 9 values × 2 rounds)...")

    best_vector, best_score = _coordinate_descent(
        best_vector, best_mode, best_color,
        target_profile, sr, capture_duration,
        n_rounds=2, n_points=9, verbose=verbose,
    )

    if verbose:
        print(f"  After coordinate descent: {best_score:.1f}")

    # --- Phase 3: Nelder-Mead polish ---
    if verbose:
        print(f"\nPhase 3: Nelder-Mead polish (max {max_iterations} iters)...")

    iteration_count = [0]
    best_found = [best_score]

    def callback(xk):
        iteration_count[0] += 1
        if verbose and iteration_count[0] % 20 == 0:
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
            'xatol': 0.1,
            'fatol': 0.1,
            'adaptive': True,
        },
        callback=callback,
    )

    nm_score = -result.fun
    if nm_score > best_score:
        best_score = nm_score
        best_vector = result.x.copy()

    if verbose:
        print(f"  After Nelder-Mead: {best_score:.1f}")

    # --- Phase 4: Perturbation trials ---
    if verbose:
        print(f"\n  Trying 25 perturbations...")

    best_params = _vector_to_params(best_vector, best_mode, best_color)
    final_score = best_score

    for trial in range(25):
        perturbed = best_vector.copy()
        # Randomly perturb 3-7 parameters
        n_perturb = np.random.randint(3, 8)
        indices = np.random.choice(len(perturbed), n_perturb, replace=False)
        for idx in indices:
            lo, hi = BOUNDS[idx]
            range_size = hi - lo
            perturbed[idx] += np.random.uniform(-0.15, 0.15) * range_size
            perturbed[idx] = np.clip(perturbed[idx], lo, hi)

        trial_score = -_evaluate(perturbed, best_mode, best_color, target_profile, sr, capture_duration)
        if trial_score > final_score:
            final_score = trial_score
            best_params = _vector_to_params(perturbed, best_mode, best_color)
            best_vector = perturbed.copy()
            if verbose:
                print(f"    Perturbation {trial+1}: improved to {final_score:.1f}")

    # Final comparison for detailed report
    final_ir = capture_ir(best_params, sr=sr, duration_s=capture_duration, normalize=True)
    final_profile = analyze_ir(final_ir, sr, name=f"Velvet90(optimized)")
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
    """Suggest relevant Velvet 90 modes for a PCM 90 category."""
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
