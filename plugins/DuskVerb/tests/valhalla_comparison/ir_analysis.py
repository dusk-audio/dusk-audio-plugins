"""
IR Analysis Library — reusable analysis functions for DuskVerb vs VintageVerb.

Pure functions: numpy arrays in, dicts out. No I/O, no plotting.

Sections:
  1. Early Reflection analysis (stereo-aware)
  2. Diffusion analysis (onset, plateau, buildup)
  3. Spectral Decay analysis (1/3-octave RT60)
  4. Modulation analysis (Hilbert-based phase tracking)
  5. Size/Space analysis (IACC, mean free path, T_first)
"""

import numpy as np
from scipy.signal import butter, sosfiltfilt, find_peaks, hilbert
import reverb_metrics as metrics


# ---------------------------------------------------------------------------
# 1. Early Reflection Analysis
# ---------------------------------------------------------------------------
def analyze_er(ir_l, ir_r, sr, window_ms=150):
    """Detect ER taps with timing, level, and stereo pan.

    Args:
        ir_l, ir_r: left/right impulse response channels
        sr: sample rate
        window_ms: ER analysis window in ms

    Returns:
        dict with:
            "taps": list of (time_ms, level_db, pan) sorted by time
                pan: -1.0 = full left, +1.0 = full right
            "density_per_10ms": np.array of tap counts per 10ms bin
            "num_taps": int
            "first_tap_ms": float or None
            "last_tap_ms": float or None
    """
    window_samples = int(sr * window_ms / 1000)
    mono = (ir_l[:window_samples] + ir_r[:window_samples]) / 2.0

    # Threshold: -40dBFS relative to direct sound (first peak)
    direct_peak = np.max(np.abs(mono[:int(sr * 0.002)]))  # first 2ms
    if direct_peak < 1e-10:
        direct_peak = np.max(np.abs(mono))
    if direct_peak < 1e-10:
        return {"taps": [], "density_per_10ms": np.array([]),
                "num_taps": 0, "first_tap_ms": None, "last_tap_ms": None}

    threshold = direct_peak * 0.01  # -40dB below direct
    min_distance = int(sr * 0.001)  # 1ms minimum separation

    peaks, properties = find_peaks(
        np.abs(mono), height=threshold, distance=min_distance)

    # Build tap list with pan
    half_win = int(sr * 0.0005)  # ±0.5ms for pan measurement
    taps = []
    for idx in peaks:
        time_ms = idx / sr * 1000.0
        level_db = 20.0 * np.log10(max(np.abs(mono[idx]), 1e-20))

        # Pan from L/R energy in ±0.5ms window
        lo = max(0, idx - half_win)
        hi = min(len(ir_l[:window_samples]), idx + half_win + 1)
        l_energy = np.sqrt(np.mean(ir_l[lo:hi] ** 2))
        r_energy = np.sqrt(np.mean(ir_r[lo:hi] ** 2))
        total = l_energy + r_energy
        pan = float((r_energy - l_energy) / total) if total > 1e-20 else 0.0

        taps.append((float(time_ms), float(level_db), float(pan)))

    # Density per 10ms bin
    n_bins = int(window_ms / 10) + 1
    density = np.zeros(n_bins)
    for t_ms, _, _ in taps:
        bin_idx = int(t_ms / 10)
        if 0 <= bin_idx < n_bins:
            density[bin_idx] += 1

    return {
        "taps": taps,
        "density_per_10ms": density,
        "num_taps": len(taps),
        "first_tap_ms": taps[0][0] if taps else None,
        "last_tap_ms": taps[-1][0] if taps else None,
    }


def compare_er(ir_dv_l, ir_dv_r, ir_vv_l, ir_vv_r, sr,
               match_window_ms=2.0, algo_hint=None):
    """Match DV ER taps to VV taps.

    Args:
        match_window_ms: maximum time difference for a match (default ±2ms,
            widened to ±4ms for Room/Chamber if match rate < 40%)
        algo_hint: algorithm name for adaptive window widening

    Returns:
        dict with matched/unmatched taps and summary statistics
    """
    dv_er = analyze_er(ir_dv_l, ir_dv_r, sr)
    vv_er = analyze_er(ir_vv_l, ir_vv_r, sr)

    result = _match_taps(dv_er["taps"], vv_er["taps"], match_window_ms)

    # Adaptive window: widen for Room/Chamber if match rate < 40%
    if (result["match_rate"] < 0.40
            and algo_hint in ("Room", "Chamber")
            and match_window_ms < 4.0):
        result = _match_taps(dv_er["taps"], vv_er["taps"], 4.0)
        result["window_widened"] = True
        result["match_window_ms"] = 4.0
    else:
        result["window_widened"] = False
        result["match_window_ms"] = match_window_ms

    result["dv_er"] = dv_er
    result["vv_er"] = vv_er
    return result


def _match_taps(dv_taps, vv_taps, window_ms):
    """Core tap matching: for each VV tap, find nearest DV tap within window."""
    matched = []
    unmatched_vv = []
    used_dv = set()

    for vv_tap in vv_taps:
        vv_time = vv_tap[0]
        best_idx = None
        best_err = float("inf")

        for i, dv_tap in enumerate(dv_taps):
            if i in used_dv:
                continue
            err = abs(dv_tap[0] - vv_time)
            if err <= window_ms and err < best_err:
                best_err = err
                best_idx = i

        if best_idx is not None:
            dv_tap = dv_taps[best_idx]
            matched.append({
                "vv_tap": vv_tap,
                "dv_tap": dv_tap,
                "time_err_ms": dv_tap[0] - vv_tap[0],
                "level_err_db": dv_tap[1] - vv_tap[1],
            })
            used_dv.add(best_idx)
        else:
            unmatched_vv.append(vv_tap)

    unmatched_dv = [dv_taps[i] for i in range(len(dv_taps)) if i not in used_dv]

    time_errors = [m["time_err_ms"] for m in matched]
    level_errors = [m["level_err_db"] for m in matched]
    total_vv = len(vv_taps)

    return {
        "matched": matched,
        "unmatched_vv": unmatched_vv,
        "unmatched_dv": unmatched_dv,
        "num_matched": len(matched),
        "num_vv_taps": total_vv,
        "num_dv_taps": len(dv_taps),
        "mean_time_error_ms": float(np.mean(time_errors)) if time_errors else 0.0,
        "mean_level_error_db": float(np.mean(level_errors)) if level_errors else 0.0,
        "match_rate": len(matched) / total_vv if total_vv > 0 else 0.0,
    }


# ---------------------------------------------------------------------------
# 2. Diffusion Analysis
# ---------------------------------------------------------------------------
def analyze_diffusion(ir, sr):
    """Measure diffusion buildup characteristics.

    Returns:
        dict with:
            "onset_ms": when density first exceeds 10 peaks/10ms
            "plateau_density": median density in 200-500ms window (peaks/10ms)
            "buildup_slope": peaks/10ms per 10ms from onset to plateau
            "time_to_gaussian": when kurtosis drops below 4.0 (ms)
    """
    # Echo density at fine resolution
    times, densities = metrics.echo_density_over_time(
        ir, sr, window_ms=10, hop_ms=5)

    if len(times) == 0:
        return {"onset_ms": None, "plateau_density": 0.0,
                "buildup_slope": 0.0, "time_to_gaussian": None}

    # Convert peaks/sec to peaks/10ms
    density_per_10ms = densities / 100.0

    # Onset: first time density > 10 peaks/10ms
    onset_ms = None
    onset_idx = None
    for i, (t, d) in enumerate(zip(times, density_per_10ms)):
        if d > 10.0:
            onset_ms = float(t * 1000)
            onset_idx = i
            break

    # Plateau: median in 200-500ms window
    mask_200_500 = (times >= 0.200) & (times <= 0.500)
    if np.any(mask_200_500):
        plateau_density = float(np.median(density_per_10ms[mask_200_500]))
    else:
        plateau_density = 0.0

    # Buildup slope
    buildup_slope = 0.0
    if onset_idx is not None and onset_ms is not None:
        # Find first sample at plateau level
        plateau_thresh = plateau_density * 0.8
        for i in range(onset_idx, len(density_per_10ms)):
            if density_per_10ms[i] >= plateau_thresh:
                dt_ms = times[i] * 1000 - onset_ms
                if dt_ms > 1.0:
                    buildup_slope = (density_per_10ms[i] - density_per_10ms[onset_idx]) / (dt_ms / 10.0)
                break

    # Time to Gaussian: kurtosis-based
    k_times, kurtosis = metrics.normalized_echo_density(ir, sr, window_ms=50, hop_ms=10)
    time_to_gaussian = None
    for t, k in zip(k_times, kurtosis):
        if t > 0.010 and k < 4.0:  # skip first 10ms (direct sound)
            time_to_gaussian = float(t * 1000)
            break

    return {
        "onset_ms": onset_ms,
        "plateau_density": plateau_density,
        "buildup_slope": float(buildup_slope),
        "time_to_gaussian": time_to_gaussian,
    }


def compare_diffusion(ir_dv, ir_vv, sr):
    """Compare diffusion characteristics."""
    dv = analyze_diffusion(ir_dv, sr)
    vv = analyze_diffusion(ir_vv, sr)

    def delta(a, b):
        if a is None or b is None:
            return None
        return a - b

    return {
        "dv": dv,
        "vv": vv,
        "onset_delta_ms": delta(dv["onset_ms"], vv["onset_ms"]),
        "plateau_delta": delta(dv["plateau_density"], vv["plateau_density"]),
        "slope_delta": delta(dv["buildup_slope"], vv["buildup_slope"]),
        "gaussian_delta_ms": delta(dv["time_to_gaussian"], vv["time_to_gaussian"]),
    }


# ---------------------------------------------------------------------------
# 3. Spectral Decay Analysis
# ---------------------------------------------------------------------------
def analyze_spectral_decay(ir, sr):
    """RT60 per 1/3-octave band.

    Wraps reverb_metrics.measure_rt60_third_octave().
    """
    return metrics.measure_rt60_third_octave(ir, sr)


def compare_spectral_decay(ir_dv, ir_vv, sr):
    """Per-band RT60 comparison with pass/fail at ±20%.

    Returns:
        dict with per_band detail, summary counts, and worst offender.
    """
    dv_rt60 = analyze_spectral_decay(ir_dv, sr)
    vv_rt60 = analyze_spectral_decay(ir_vv, sr)

    per_band = {}
    passing = 0
    total = 0
    worst_band = None
    worst_ratio = 1.0
    worst_deviation = 0.0

    for band in dv_rt60:
        dv_val = dv_rt60.get(band)
        vv_val = vv_rt60.get(band)

        if dv_val is None or vv_val is None or vv_val <= 0:
            per_band[band] = {
                "dv": dv_val, "vv": vv_val, "ratio": None, "pass": False}
            total += 1
            continue

        ratio = dv_val / vv_val
        band_pass = 0.80 <= ratio <= 1.25
        per_band[band] = {
            "dv": round(dv_val, 3), "vv": round(vv_val, 3),
            "ratio": round(ratio, 3), "pass": band_pass}

        total += 1
        if band_pass:
            passing += 1

        deviation = abs(ratio - 1.0)
        if deviation > worst_deviation:
            worst_deviation = deviation
            worst_band = band
            worst_ratio = ratio

    return {
        "per_band": per_band,
        "bands_passing": passing,
        "bands_total": total,
        "worst_band": worst_band,
        "worst_ratio": round(worst_ratio, 3) if worst_ratio else None,
    }


# ---------------------------------------------------------------------------
# 4. Modulation Analysis
# ---------------------------------------------------------------------------
def analyze_modulation(ir, sr):
    """Extract modulation signature via Hilbert transform phase tracking.

    Bandpasses 800-1200Hz, tracks instantaneous frequency in the
    200ms-2000ms tail window.

    Returns:
        dict with rate_hz, depth_cents, shape, freq_deviation_rms_hz,
        and a "reliable" flag.
    """
    # Bandpass 800-1200Hz
    lo, hi = 800.0, 1200.0
    hi = min(hi, sr / 2 - 1)
    if lo >= hi:
        return _mod_fallback("bandpass range invalid")

    sos = butter(4, [lo, hi], btype='bandpass', fs=sr, output='sos')
    filtered = sosfiltfilt(sos, ir).astype(np.float64)

    # Tail window
    start = int(sr * 0.200)
    end = int(sr * 2.000)
    end = min(end, len(filtered))

    # Truncate at -60dB
    env = np.abs(filtered)
    peak = np.max(env[start:end]) if end > start else 0
    if peak < 1e-10:
        return _mod_fallback("tail too quiet")

    threshold = peak * 1e-3
    actual_end = end
    for i in range(end - 1, start, -1):
        if env[i] > threshold:
            actual_end = i + 1
            break
    tail = filtered[start:actual_end]

    if len(tail) < int(sr * 0.1):  # need at least 100ms
        return _mod_fallback("tail too short")

    # Analytic signal
    analytic = hilbert(tail)
    inst_phase = np.unwrap(np.angle(analytic))

    # Instantaneous frequency
    inst_freq = np.diff(inst_phase) * sr / (2 * np.pi)

    # Remove outliers (Hilbert can produce spikes at low amplitude)
    median_freq = np.median(inst_freq)
    if median_freq < 100 or median_freq > 5000:
        return _mod_fallback("median freq out of range")

    # Deviation from median
    deviation = inst_freq - median_freq
    dev_rms = float(np.sqrt(np.mean(deviation ** 2)))

    # Depth in cents
    depth_cents = float(1200.0 * np.log2(1.0 + dev_rms / median_freq))

    # Rate: FFT of deviation signal, find dominant peak in 0.1-10Hz
    n_fft = len(deviation)
    if n_fft < 256:
        return _mod_fallback("deviation signal too short for FFT")

    dev_fft = np.abs(np.fft.rfft(deviation * np.hanning(n_fft)))
    fft_freqs = np.fft.rfftfreq(n_fft, 1.0 / sr)

    mask = (fft_freqs >= 0.1) & (fft_freqs <= 10.0)
    if not np.any(mask):
        return _mod_fallback("no valid FFT bins in 0.1-10Hz range")

    masked_fft = dev_fft.copy()
    masked_fft[~mask] = 0
    peak_idx = np.argmax(masked_fft)
    rate_hz = float(fft_freqs[peak_idx])

    # Shape classification via spectral flatness
    valid_bins = dev_fft[mask]
    if np.max(valid_bins) < 1e-10:
        shape = "none"
    else:
        geometric_mean = np.exp(np.mean(np.log(valid_bins + 1e-20)))
        arithmetic_mean = np.mean(valid_bins)
        flatness = geometric_mean / (arithmetic_mean + 1e-20)

        if flatness > 0.5:
            shape = "random"
        elif flatness < 0.2:
            shape = "sine"
        else:
            shape = "mixed"

    return {
        "rate_hz": round(rate_hz, 3),
        "depth_cents": round(depth_cents, 2),
        "shape": shape,
        "freq_deviation_rms_hz": round(dev_rms, 2),
        "reliable": True,
    }


def _mod_fallback(reason):
    """Return empty modulation result with unreliable flag."""
    return {
        "rate_hz": 0.0,
        "depth_cents": 0.0,
        "shape": "unknown",
        "freq_deviation_rms_hz": 0.0,
        "reliable": False,
        "unreliable_reason": reason,
    }


def compare_modulation(ir_dv, ir_vv, sr):
    """Compare modulation characteristics."""
    dv = analyze_modulation(ir_dv, sr)
    vv = analyze_modulation(ir_vv, sr)

    both_reliable = dv["reliable"] and vv["reliable"]

    return {
        "dv": dv,
        "vv": vv,
        "both_reliable": both_reliable,
        "rate_delta_hz": round(dv["rate_hz"] - vv["rate_hz"], 3) if both_reliable else None,
        "depth_delta_cents": round(dv["depth_cents"] - vv["depth_cents"], 2) if both_reliable else None,
        "shape_match": dv["shape"] == vv["shape"] if both_reliable else None,
    }


# ---------------------------------------------------------------------------
# 5. Size/Space Analysis
# ---------------------------------------------------------------------------
def analyze_size(ir_l, ir_r, sr):
    """Measure spatial characteristics.

    Returns:
        dict with iacc_early, iacc_late, mean_free_path_ms, t_first_ms
    """
    # IACC via existing function
    iacc_times, iacc_values = metrics.measure_iacc(ir_l, ir_r, sr,
                                                    window_ms=20, hop_ms=10)

    # Early IACC: 0-80ms
    early_mask = iacc_times <= 0.080
    iacc_early = float(np.mean(iacc_values[early_mask])) if np.any(early_mask) else 1.0

    # Late IACC: 80-500ms
    late_mask = (iacc_times > 0.080) & (iacc_times <= 0.500)
    iacc_late = float(np.mean(iacc_values[late_mask])) if np.any(late_mask) else 1.0

    # Mean free path from ER tap spacing
    er = analyze_er(ir_l, ir_r, sr, window_ms=150)
    taps = er["taps"]
    if len(taps) >= 3:
        intervals = [taps[i+1][0] - taps[i][0] for i in range(len(taps) - 1)]
        mean_free_path_ms = float(np.median(intervals))
    else:
        mean_free_path_ms = 0.0

    # T_first: time to first reflection above -20dBFS (skip direct sound at t=0)
    mono = (ir_l + ir_r) / 2.0
    peak = np.max(np.abs(mono))
    threshold = peak * 0.1  # -20dB
    t_first_ms = None
    # Skip first 1ms (direct sound impulse)
    search_start = int(sr * 0.001)
    for i in range(search_start, min(len(mono), int(sr * 0.150))):
        if np.abs(mono[i]) > threshold:
            t_first_ms = float(i / sr * 1000)
            break

    return {
        "iacc_early": round(iacc_early, 3),
        "iacc_late": round(iacc_late, 3),
        "mean_free_path_ms": round(mean_free_path_ms, 2),
        "t_first_ms": round(t_first_ms, 2) if t_first_ms is not None else None,
    }


def compare_size(ir_dv_l, ir_dv_r, ir_vv_l, ir_vv_r, sr):
    """Compare size/space characteristics."""
    dv = analyze_size(ir_dv_l, ir_dv_r, sr)
    vv = analyze_size(ir_vv_l, ir_vv_r, sr)

    def delta(a, b):
        if a is None or b is None:
            return None
        return round(a - b, 3)

    return {
        "dv": dv,
        "vv": vv,
        "iacc_early_delta": delta(dv["iacc_early"], vv["iacc_early"]),
        "iacc_late_delta": delta(dv["iacc_late"], vv["iacc_late"]),
        "free_path_delta_ms": delta(dv["mean_free_path_ms"], vv["mean_free_path_ms"]),
        "t_first_delta_ms": delta(dv["t_first_ms"], vv["t_first_ms"]),
    }
