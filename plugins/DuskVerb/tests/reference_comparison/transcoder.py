#!/usr/bin/env python3
"""
Reverb Parameter Transcoder: VV multi-band → DV QuadTank bi-linear.

Maps Valhalla VintageVerb's complex multi-band damping model to DuskVerb's
simpler two-band (bass_multiply / treble_multiply / crossover) architecture
using least-squares optimization.

Usage:
    from transcoder import transcode_damping, transcode_preset

    # From measured RT60 values:
    result = transcode_damping(
        rt60_125=1.45, rt60_500=1.18, rt60_1k=1.23,
        rt60_4k=0.94, rt60_8k=0.81,
        vv_highshelf=0.0,  # 0.0 = -24dB max damping
    )
    print(result)
    # {'bass_multiply': 0.92, 'treble_multiply': 0.38, 'crossover_freq': 850,
    #  'decay_time': 1.20, 'fit_error': 0.02}

    # From full VV preset:
    result = transcode_preset(vv_params, dv_algorithm, mode_name)
"""

import numpy as np
from scipy.optimize import minimize


def _quadtank_rt60_model(decay_time, bass_multiply, treble_multiply,
                          crossover_freq, freqs):
    """Predict RT60 at given frequencies for DV's two-band damping model.

    In the QuadTank, per-loop gain is:
        gBase = 10^(-3 * loopLength / (decayTime * sr))
        gLow  = gBase^(1/bassMultiply)
        gHigh = gBase^(1/trebleMultiply)

    The effective RT60 at a frequency depends on whether it's below or above
    the crossover. The crossover is a first-order filter, so the transition
    is gradual (6 dB/octave).

    For a first-order crossover at fc:
        effective_mult(f) = bassMultiply * w(f) + trebleMultiply * (1 - w(f))
        where w(f) = 1 / (1 + (f/fc)^2)  (power response of 1st-order LP)

    Then RT60(f) = decay_time * effective_mult(f)
    """
    rt60s = np.zeros(len(freqs))
    for i, f in enumerate(freqs):
        # First-order crossover weight (power domain)
        w_low = 1.0 / (1.0 + (f / crossover_freq) ** 2)
        w_high = 1.0 - w_low
        effective_mult = bass_multiply * w_low + treble_multiply * w_high
        rt60s[i] = decay_time * effective_mult
    return rt60s


def transcode_damping(rt60_125, rt60_500, rt60_1k, rt60_4k, rt60_8k,
                       vv_highshelf=0.0, vv_highfreq_hz=4000.0):
    """Find optimal DV parameters to match VV's per-band RT60 values.

    Uses least-squares optimization to find (decay_time, bass_multiply,
    treble_multiply, crossover_freq) that minimize the weighted RT60 error
    across all measured bands.

    Args:
        rt60_125..rt60_8k: VV's measured RT60 at each frequency (seconds)
        vv_highshelf: VV's HighShelf parameter (0.0 = -24dB, 1.0 = 0dB)
        vv_highfreq_hz: VV's HighFreq damping frequency (Hz)

    Returns:
        dict with bass_multiply, treble_multiply, crossover_freq, decay_time,
        fit_error, predicted_rt60s
    """
    # Target RT60 values and their frequencies
    freqs = np.array([125.0, 500.0, 1000.0, 4000.0, 8000.0])
    targets = np.array([rt60_125, rt60_500, rt60_1k, rt60_4k, rt60_8k])

    # Weights: penalize mid-range (500-1k) errors more since they're most audible.
    # Also penalize 8kHz strongly since that's where VV's shelf damping matters most.
    weights = np.array([1.0, 2.0, 2.0, 1.5, 1.5])

    # Use 1kHz as the reference decay time (most "neutral" band)
    ref_decay = rt60_1k

    # --- High-Shelf Hack ---
    # VV's HighShelf=0 means -24dB shelf at HighFreq. DV's treble_multiply can't
    # replicate a shelf — it scales the entire HF decay rate uniformly.
    # When HighShelf is active (< -12dB → raw value < 0.5), apply a non-linear
    # correction that makes treble_multiply more aggressive.
    highshelf_db = -24.0 * (1.0 - vv_highshelf)  # 0.0 → -24dB, 1.0 → 0dB
    shelf_correction = 1.0
    if highshelf_db < -12.0:
        # Stronger correction for deeper shelves
        # At -24dB: correction = 0.70 (30% more aggressive treble damping)
        # At -12dB: correction = 1.0 (no correction)
        shelf_correction = 0.70 + 0.30 * (highshelf_db + 24.0) / 12.0
        shelf_correction = max(shelf_correction, 0.60)

    # --- Bass-Skew Correction ---
    # If VV's 125Hz decays significantly shorter than 500Hz, the bass needs more
    # damping. Shift crossover higher to let bass_multiply affect more bandwidth.
    bass_ratio = rt60_125 / rt60_500 if rt60_500 > 0 else 1.0
    bass_skew_offset = 0.0
    if bass_ratio < 1.0:
        # Bass decays faster than mid — need higher crossover to damp bass more
        bass_skew_offset = (1.0 - bass_ratio) * 500.0  # shift crossover up

    def objective(params):
        decay_time, bass_mult, treble_mult_raw, crossover = params

        # Apply shelf correction to treble
        treble_mult = treble_mult_raw * shelf_correction

        # Clamp to valid ranges
        decay_time = max(decay_time, 0.1)
        bass_mult = max(bass_mult, 0.3)
        treble_mult = max(treble_mult, 0.1)
        crossover = max(crossover, 100.0)

        predicted = _quadtank_rt60_model(decay_time, bass_mult, treble_mult,
                                          crossover, freqs)

        # Weighted relative error (log-domain for perceptual scaling)
        errors = weights * (np.log(predicted + 1e-6) - np.log(targets + 1e-6)) ** 2
        return np.sum(errors)

    # Initial guess based on the targets
    bass_init = rt60_125 / ref_decay if ref_decay > 0 else 1.0
    treble_init = (rt60_8k / ref_decay if ref_decay > 0 else 0.5) / shelf_correction
    crossover_init = 800.0 + bass_skew_offset

    x0 = [ref_decay, bass_init, treble_init, crossover_init]

    # Bounds
    bounds = [
        (0.1, 30.0),     # decay_time
        (0.3, 2.5),      # bass_multiply
        (0.1, 1.5),      # treble_multiply (pre-correction)
        (100.0, 4000.0),  # crossover_freq
    ]

    result = minimize(objective, x0, method='L-BFGS-B', bounds=bounds,
                      options={'maxiter': 1000, 'ftol': 1e-12})

    decay_time, bass_mult, treble_mult_raw, crossover = result.x
    treble_mult = treble_mult_raw * shelf_correction

    # Clamp to DV's valid parameter ranges
    decay_time = float(np.clip(decay_time, 0.2, 30.0))
    bass_mult = float(np.clip(bass_mult, 0.5, 2.0))
    treble_mult = float(np.clip(treble_mult, 0.1, 1.0))
    crossover = float(np.clip(crossover, 200.0, 4000.0))

    # Compute fit quality
    predicted = _quadtank_rt60_model(decay_time, bass_mult, treble_mult,
                                      crossover, freqs)
    per_band_error = {f"{int(f)} Hz": float(abs(p - t) / t * 100)
                      for f, p, t in zip(freqs, predicted, targets)}
    mean_error = float(np.mean(list(per_band_error.values())))

    return {
        "decay_time": decay_time,
        "bass_multiply": bass_mult,
        "treble_multiply": treble_mult,
        "crossover_freq": crossover,
        "fit_error_pct": mean_error,
        "per_band_error_pct": per_band_error,
        "predicted_rt60": {f"{int(f)} Hz": float(p) for f, p in zip(freqs, predicted)},
        "target_rt60": {f"{int(f)} Hz": float(t) for f, t in zip(freqs, targets)},
        "shelf_correction": shelf_correction,
        "bass_skew_offset": bass_skew_offset,
    }


def transcode_preset(vv_params, dv_algorithm, mode_name, name="",
                      vv_rt60=None, sr=48000):
    """Full preset transcoding: VV parameters → optimized DV parameters.

    If vv_rt60 is provided (dict from measure_rt60_per_band), uses measured
    RT60 values for optimization. Otherwise falls back to parameter mapping.

    Args:
        vv_params: dict of VV parameter values (from qualifying_presets.json)
        dv_algorithm: target DV algorithm name
        mode_name: VV mode name
        name: preset name
        vv_rt60: optional measured RT60 dict from VV impulse response
        sr: sample rate

    Returns:
        dict of DV parameters with optimized damping values
    """
    from preset_suite import (translate_preset, vv_bassmult_to_mult,
                               vv_bassxover_to_hz, vv_highfreq_to_hz,
                               color_treble_offset)

    # Start with the standard translation for non-damping parameters
    dv = translate_preset(vv_params, dv_algorithm, mode_name, name)

    # If we have measured RT60 values, optimize the damping parameters
    if vv_rt60 is not None:
        rt60_125 = vv_rt60.get("125 Hz")
        rt60_500 = vv_rt60.get("500 Hz")
        rt60_1k = vv_rt60.get("1 kHz")
        rt60_4k = vv_rt60.get("4 kHz")
        rt60_8k = vv_rt60.get("8 kHz")

        if all(v and v > 0 for v in [rt60_125, rt60_500, rt60_1k, rt60_4k, rt60_8k]):
            vv_highshelf = vv_params.get("HighShelf", 0.0)
            vv_highfreq = vv_highfreq_to_hz(vv_params.get("HighFreq", 0.5))

            opt = transcode_damping(
                rt60_125=rt60_125, rt60_500=rt60_500, rt60_1k=rt60_1k,
                rt60_4k=rt60_4k, rt60_8k=rt60_8k,
                vv_highshelf=vv_highshelf,
                vv_highfreq_hz=vv_highfreq,
            )

            dv["decay_time"] = opt["decay_time"]
            dv["bass_multiply"] = opt["bass_multiply"]
            dv["treble_multiply"] = opt["treble_multiply"]
            dv["crossover"] = int(opt["crossover_freq"])

    # Diffusion blending (always applied)
    early_diff = vv_params.get("EarlyDiffusion", 0.7)
    late_diff = vv_params.get("LateDiffusion", 0.7)
    if dv_algorithm == "Hall":
        dv["diffusion"] = 0.3 * early_diff + 0.7 * late_diff
    else:
        dv["diffusion"] = max(early_diff, late_diff)

    return dv


# ---------------------------------------------------------------------------
# CLI: test transcoder on Fat Snare Hall
# ---------------------------------------------------------------------------
if __name__ == "__main__":
    print("=== Reverb Transcoder Test: Fat Snare Hall ===\n")

    # VV's measured RT60 values (from our analysis)
    result = transcode_damping(
        rt60_125=1.45, rt60_500=1.18, rt60_1k=1.23,
        rt60_4k=0.94, rt60_8k=0.81,
        vv_highshelf=0.0,  # -24dB max damping
        vv_highfreq_hz=4000.0,
    )

    print(f"Optimized DV parameters:")
    print(f"  decay_time:      {result['decay_time']:.3f}s")
    print(f"  bass_multiply:   {result['bass_multiply']:.3f}")
    print(f"  treble_multiply: {result['treble_multiply']:.3f}")
    print(f"  crossover_freq:  {result['crossover_freq']:.0f} Hz")
    print(f"  shelf_correction: {result['shelf_correction']:.3f}")
    print(f"  bass_skew_offset: {result['bass_skew_offset']:.0f} Hz")
    print(f"\nFit quality: {result['fit_error_pct']:.1f}% mean error")
    print(f"\nPer-band comparison:")
    print(f"  {'Band':>8s}  {'Target':>8s}  {'Predicted':>10s}  {'Error':>8s}")
    for band in result['target_rt60']:
        t = result['target_rt60'][band]
        p = result['predicted_rt60'][band]
        e = result['per_band_error_pct'][band]
        print(f"  {band:>8s}  {t:>7.2f}s  {p:>9.2f}s  {e:>6.1f}%")
