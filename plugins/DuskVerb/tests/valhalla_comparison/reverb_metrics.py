"""
Core reverb analysis metrics library.

Pure functions that take numpy arrays and return structured results.
No I/O, no plotting — just math.

Metrics:
  1. RT60 per octave band (frequency-dependent decay time)
  2. Energy Decay Curve (Schroeder integral)
  3. Spectral waterfall (STFT spectrogram)
  4. Modal resonance detection (persistent narrowband peaks)
  5. Echo density over time
  6. Stereo decorrelation over time
  7. Frequency-dependent decay rate
  8. Frequency response (octave band levels)
"""

import numpy as np
from scipy.signal import butter, sosfilt, sosfiltfilt
from scipy.ndimage import median_filter


# ---------------------------------------------------------------------------
# Utility functions
# ---------------------------------------------------------------------------
def rms_db(x):
    """RMS level in dB."""
    if x.size == 0:
        return -200.0
    return 10.0 * np.log10(max(np.mean(x ** 2), 1e-20))


def rms_envelope(signal, sr, window_ms=50):
    """Running RMS envelope in dB."""
    win = max(1, int(sr * window_ms / 1000))
    padded = np.pad(signal ** 2, (win // 2, win - win // 2), mode='edge')
    cumsum = np.cumsum(padded)
    rms_sq = (cumsum[win:] - cumsum[:-win]) / win
    return 10.0 * np.log10(np.maximum(rms_sq[:len(signal)], 1e-20))


def _octave_bandpass(signal, sr, center_freq, order=4):
    """Bandpass filter for one octave band centered at center_freq."""
    lo = center_freq / np.sqrt(2)
    hi = center_freq * np.sqrt(2)
    # Clamp to valid range
    lo = max(lo, 10)
    hi = min(hi, sr / 2 - 1)
    if lo >= hi:
        return np.zeros_like(signal)
    sos = butter(order, [lo, hi], btype='bandpass', fs=sr, output='sos')
    return sosfiltfilt(sos, signal).astype(np.float32)


# ---------------------------------------------------------------------------
# 1. RT60 per octave band
# ---------------------------------------------------------------------------
# Standard octave band center frequencies (ISO 266)
OCTAVE_BANDS = {
    "125 Hz": 125,
    "250 Hz": 250,
    "500 Hz": 500,
    "1 kHz": 1000,
    "2 kHz": 2000,
    "4 kHz": 4000,
    "8 kHz": 8000,
}


def measure_rt60_per_band(ir, sr, bands=None):
    """Compute RT60 for each octave band using Schroeder backward integration.

    Args:
        ir: impulse response (1D float array)
        sr: sample rate
        bands: dict of {name: center_freq}; defaults to OCTAVE_BANDS

    Returns:
        dict mapping band name -> RT60 in seconds (None if unmeasurable)
    """
    if bands is None:
        bands = OCTAVE_BANDS

    results = {}
    for name, fc in bands.items():
        filtered = _octave_bandpass(ir, sr, fc)
        rt60 = _schroeder_rt60(filtered, sr)
        results[name] = rt60
    return results


def _lundeby_noise_floor(edc_db, sr, block_ms=10):
    """Estimate noise floor level using Lundeby's method.

    Divides the EDC into blocks and detects the point where the decay
    curve flattens (noise floor). Returns the noise floor level in dB
    and the sample index where noise dominates.
    """
    block_len = max(1, int(sr * block_ms / 1000))
    n_blocks = len(edc_db) // block_len

    if n_blocks < 5:
        return -100.0, len(edc_db)

    # Compute average level per block
    block_levels = np.array([
        np.mean(edc_db[i * block_len:(i + 1) * block_len])
        for i in range(n_blocks)
    ])

    # Estimate noise floor from the last 10% of blocks
    tail_blocks = max(1, n_blocks // 10)
    noise_floor = float(np.mean(block_levels[-tail_blocks:]))

    # Find where EDC crosses noise_floor + 10 dB (safety margin)
    threshold = noise_floor + 10.0
    crossover_idx = len(edc_db)
    for i in range(len(edc_db)):
        if edc_db[i] <= threshold:
            crossover_idx = i
            break

    return noise_floor, crossover_idx


def _schroeder_rt60(signal, sr, fit_range_db=(-5, -35)):
    """Compute RT60 from Schroeder backward integration.

    Uses Lundeby noise floor detection to exclude noise-dominated
    samples from the regression. Rejects fits with R² < 0.95 to
    avoid false passes on double-slope or non-exponential decays.

    Returns RT60 in seconds, or None if the signal doesn't have enough
    dynamic range or the fit quality is poor.
    """
    edc = _compute_edc_raw(signal)
    if edc is None or len(edc) < 2:
        return None

    # Convert to dB
    edc_db = 10.0 * np.log10(np.maximum(edc / max(edc[0], 1e-20), 1e-20))

    # Detect noise floor and truncate
    noise_floor, noise_idx = _lundeby_noise_floor(edc_db, sr)

    # Find regression range (excluding noise floor region)
    lo_db, hi_db = fit_range_db
    # Clamp lower bound to be above noise floor + 5 dB
    hi_db = max(hi_db, noise_floor + 5.0)

    mask = (edc_db >= hi_db) & (edc_db <= lo_db) & (np.arange(len(edc_db)) < noise_idx)
    indices = np.where(mask)[0]

    if len(indices) < 10:
        # Not enough dynamic range — try wider range
        hi_db_fallback = max(-25, noise_floor + 5.0)
        mask = (edc_db >= hi_db_fallback) & (edc_db <= -5) & (np.arange(len(edc_db)) < noise_idx)
        indices = np.where(mask)[0]
        if len(indices) < 10:
            return None

    times = indices.astype(np.float64) / sr
    values = edc_db[indices]

    # Linear regression
    coeffs = np.polyfit(times, values, 1)
    slope = coeffs[0]  # dB/sec

    if slope >= 0:
        return None  # Not decaying

    # R² goodness-of-fit check
    predicted = np.polyval(coeffs, times)
    ss_res = np.sum((values - predicted) ** 2)
    ss_tot = np.sum((values - np.mean(values)) ** 2)
    r_squared = 1.0 - ss_res / max(ss_tot, 1e-20)

    if r_squared < 0.95:
        return None  # Poor fit (double-slope, non-exponential, etc.)

    # RT60 = time to decay 60 dB
    rt60 = -60.0 / slope
    return max(rt60, 0)


def _compute_edc_raw(signal):
    """Schroeder backward integration (cumulative energy from end)."""
    energy = signal.astype(np.float64) ** 2
    edc = np.cumsum(energy[::-1])[::-1]
    if edc[0] < 1e-20:
        return None
    return edc


# ---------------------------------------------------------------------------
# 2. Energy Decay Curve
# ---------------------------------------------------------------------------
def compute_edc(ir, sr):
    """Compute Schroeder Energy Decay Curve in dB.

    Returns:
        (time_axis, edc_db) — numpy arrays
    """
    edc = _compute_edc_raw(ir)
    if edc is None:
        return np.array([]), np.array([])

    edc_db = 10.0 * np.log10(np.maximum(edc / edc[0], 1e-20))
    time_axis = np.arange(len(edc_db)) / sr
    return time_axis, edc_db


# ---------------------------------------------------------------------------
# 3. Spectral waterfall (STFT spectrogram)
# ---------------------------------------------------------------------------
def compute_spectrogram(ir, sr, window_ms=30, hop_ms=10, nfft=4096):
    """Compute spectrogram for waterfall display.

    Returns:
        (times, freqs, magnitude_db) — 2D magnitude array indexed [time, freq]
    """
    win_samples = int(sr * window_ms / 1000)
    hop_samples = int(sr * hop_ms / 1000)
    window = np.hanning(win_samples).astype(np.float64)

    frames = []
    times = []

    for pos in range(0, len(ir) - win_samples, hop_samples):
        chunk = ir[pos:pos + win_samples].astype(np.float64)
        windowed = chunk * window
        spec = np.abs(np.fft.rfft(windowed, n=nfft))
        frames.append(spec)
        times.append((pos + win_samples // 2) / sr)

    if not frames:
        return np.array([]), np.array([]), np.array([[]])

    magnitude = np.array(frames)
    magnitude_db = 20.0 * np.log10(np.maximum(magnitude, 1e-10))
    freqs = np.fft.rfftfreq(nfft, 1.0 / sr)

    return np.array(times), freqs, magnitude_db


# ---------------------------------------------------------------------------
# 4. Modal resonance detection
# ---------------------------------------------------------------------------
def detect_modal_resonances(ir, sr, time_windows=None):
    """Detect persistent narrowband peaks in the reverb tail.

    Analyzes the spectrum in multiple time windows to distinguish
    true resonances (persistent) from transient spectral features.

    Args:
        ir: impulse response
        sr: sample rate
        time_windows: list of (start_sec, end_sec) tuples.
                      Defaults to [(0.1, 0.2), (0.2, 0.5), (0.5, 1.0)]

    Returns:
        dict with:
            "persistent_peaks": list of (freq_hz, avg_prominence_db, persistence_count)
            "max_peak_prominence_db": worst-case prominence
            "worst_freq_hz": frequency of worst resonance
            "per_window": list of per-window analysis results
    """
    if time_windows is None:
        time_windows = [(0.1, 0.2), (0.2, 0.5), (0.5, 1.0)]

    nfft = 8192
    freqs = np.fft.rfftfreq(nfft, 1.0 / sr)
    freq_mask = (freqs >= 100) & (freqs <= 12000)

    per_window = []
    all_peaks = {}  # freq_bin -> list of prominences

    for t_start, t_end in time_windows:
        start_sample = int(sr * t_start)
        end_sample = min(int(sr * t_end), len(ir))
        if end_sample <= start_sample:
            per_window.append({"peaks": [], "noise_floor_db": -100})
            continue

        segment = ir[start_sample:end_sample]
        if rms_db(segment) < -80:
            per_window.append({"peaks": [], "noise_floor_db": -100})
            continue

        # Window and FFT
        win_len = min(len(segment), nfft)
        windowed = segment[:win_len] * np.hanning(win_len)
        if win_len < nfft:
            windowed = np.pad(windowed, (0, nfft - win_len))
        spec = np.abs(np.fft.rfft(windowed, n=nfft))
        spec_db = 20.0 * np.log10(np.maximum(spec, 1e-10))

        # Median filter to estimate local background
        median_width = max(3, int(300 / (sr / nfft)))  # ~300 Hz window
        baseline = median_filter(spec_db, size=median_width)
        prominence = spec_db - baseline

        # Find peaks above threshold (> 6 dB above local median)
        window_peaks = []
        masked = prominence.copy()
        masked[~freq_mask] = -100

        # Simple peak detection: local maxima above threshold
        for i in range(1, len(masked) - 1):
            if masked[i] > 6 and masked[i] > masked[i-1] and masked[i] > masked[i+1]:
                window_peaks.append((i, freqs[i], masked[i]))
                if i not in all_peaks:
                    all_peaks[i] = []
                all_peaks[i].append(masked[i])

        per_window.append({
            "peaks": [(f, p) for _, f, p in window_peaks],
            "noise_floor_db": float(np.median(spec_db[freq_mask])),
        })

    # Find persistent peaks (appear in 2+ windows)
    persistent = []
    for bin_idx, prominences in all_peaks.items():
        if len(prominences) >= 2:  # Appears in at least 2 time windows
            persistent.append((
                float(freqs[bin_idx]),
                float(np.mean(prominences)),
                len(prominences),
            ))

    # Sort by prominence
    persistent.sort(key=lambda x: -x[1])

    # Overall worst case
    max_prom = 0
    worst_freq = 0
    for freq, prom, _ in persistent:
        if prom > max_prom:
            max_prom = prom
            worst_freq = freq

    # If no persistent peaks, check single-window worst case
    if max_prom == 0:
        for pw in per_window:
            for freq, prom in pw["peaks"]:
                if prom > max_prom:
                    max_prom = prom
                    worst_freq = freq

    return {
        "persistent_peaks": persistent[:10],  # Top 10
        "max_peak_prominence_db": float(max_prom),
        "worst_freq_hz": float(worst_freq),
        "per_window": per_window,
    }


# ---------------------------------------------------------------------------
# 5. Echo density over time
# ---------------------------------------------------------------------------
def echo_density_over_time(ir, sr, window_ms=50, hop_ms=25):
    """Measure echo density (peaks per second) as a function of time.

    Returns:
        (times, densities) — numpy arrays
    """
    win = int(sr * window_ms / 1000)
    hop = int(sr * hop_ms / 1000)

    # Smooth the IR envelope
    env = np.abs(ir)
    smooth_win = max(1, int(sr * 0.001))
    kernel = np.ones(smooth_win) / smooth_win
    env_smooth = np.convolve(env, kernel, mode='same')

    times = []
    densities = []

    for pos in range(0, len(env_smooth) - win, hop):
        chunk = env_smooth[pos:pos + win]
        if rms_db(ir[pos:pos + win]) < -80:
            break

        # Count local maxima (peaks)
        peaks = 0
        for i in range(1, len(chunk) - 1):
            if chunk[i] > chunk[i-1] and chunk[i] > chunk[i+1]:
                peaks += 1

        density = peaks / (win / sr)
        times.append((pos + win // 2) / sr)
        densities.append(density)

    return np.array(times), np.array(densities)


def normalized_echo_density(ir, sr, window_ms=50, hop_ms=25):
    """Measure normalized echo density via kurtosis (Abel & Huang method).

    Kurtosis of the amplitude distribution in each window:
    - Gaussian noise (fully dense): kurtosis ≈ 3.0
    - Discrete echoes (sparse): kurtosis > 3.0 (heavy tails)
    - Pure tone: kurtosis ≈ 1.5

    Returns:
        (times, kurtosis_values) — numpy arrays
    """
    from scipy.stats import kurtosis as scipy_kurtosis

    win = int(sr * window_ms / 1000)
    hop = int(sr * hop_ms / 1000)

    times = []
    kurtosis_values = []

    for pos in range(0, len(ir) - win, hop):
        chunk = ir[pos:pos + win]
        if rms_db(chunk) < -80:
            break

        # Fisher kurtosis (excess kurtosis): Gaussian = 0, not 3
        k = float(scipy_kurtosis(chunk, fisher=True))
        times.append((pos + win // 2) / sr)
        kurtosis_values.append(k)

    return np.array(times), np.array(kurtosis_values)


# ---------------------------------------------------------------------------
# 6. Stereo decorrelation over time
# ---------------------------------------------------------------------------
def stereo_decorrelation_over_time(ir_left, ir_right, sr, window_ms=50, hop_ms=25):
    """Measure L-R correlation coefficient over time.

    Good stereo reverbs should decorrelate quickly (correlation < 0.3 in tail).

    Returns:
        (times, correlations) — numpy arrays
    """
    win = int(sr * window_ms / 1000)
    hop = int(sr * hop_ms / 1000)

    min_len = min(len(ir_left), len(ir_right))
    times = []
    correlations = []

    for pos in range(0, min_len - win, hop):
        l = ir_left[pos:pos + win]
        r = ir_right[pos:pos + win]

        if rms_db(l) < -70 and rms_db(r) < -70:
            break

        std_l = np.std(l)
        std_r = np.std(r)
        if std_l < 1e-10 or std_r < 1e-10:
            corr = 1.0
        else:
            corr = np.corrcoef(l, r)[0, 1]

        times.append((pos + win // 2) / sr)
        correlations.append(corr)

    return np.array(times), np.array(correlations)


# ---------------------------------------------------------------------------
# 7. Frequency-dependent decay rate
# ---------------------------------------------------------------------------
def spectral_decay_rates(ir, sr, bands=None):
    """Measure decay rate (dB/sec) per octave band.

    Unlike RT60 (which extrapolates to -60dB), this gives the average slope
    of the energy decay in each band. Useful for comparing relative decay
    balance between two reverbs.

    Returns:
        dict mapping band name -> decay_rate_db_per_sec
    """
    if bands is None:
        bands = OCTAVE_BANDS

    results = {}
    for name, fc in bands.items():
        filtered = _octave_bandpass(ir, sr, fc)
        env = rms_envelope(filtered, sr, window_ms=30)

        # Find usable range (above noise floor)
        peak_idx = np.argmax(env)
        start = max(peak_idx, int(sr * 0.05))
        end = len(env)
        for i in range(start, len(env)):
            if env[i] < -70:
                end = i
                break

        if end - start < int(sr * 0.05):
            results[name] = 0.0
            continue

        tail = env[start:end]
        times = np.arange(len(tail)) / sr
        if len(times) > 1:
            coeffs = np.polyfit(times, tail, 1)
            results[name] = float(coeffs[0])  # dB/sec
        else:
            results[name] = 0.0

    return results


# ---------------------------------------------------------------------------
# 8. Frequency response (octave band levels)
# ---------------------------------------------------------------------------
def frequency_response(ir, sr):
    """Overall frequency response of the reverb in octave bands.

    Returns:
        dict mapping band name -> level in dB
    """
    nfft = 8192
    windowed = ir[:min(len(ir), nfft)]
    if len(windowed) < nfft:
        windowed = np.pad(windowed, (0, nfft - len(windowed)))
    spec = np.abs(np.fft.rfft(windowed * np.hanning(nfft), n=nfft))
    spec_db = 20.0 * np.log10(np.maximum(spec, 1e-10))
    freqs = np.fft.rfftfreq(nfft, 1.0 / sr)

    bands = {
        "Sub (20-80)": (20, 80),
        "Bass (80-250)": (80, 250),
        "Low-Mid (250-800)": (250, 800),
        "Mid (800-2.5k)": (800, 2500),
        "Hi-Mid (2.5-6k)": (2500, 6000),
        "Treble (6-12k)": (6000, 12000),
        "Air (12-20k)": (12000, 20000),
    }

    result = {}
    for name, (lo, hi) in bands.items():
        mask = (freqs >= lo) & (freqs < hi)
        if np.any(mask):
            result[name] = float(np.mean(spec_db[mask]))
        else:
            result[name] = -100.0
    return result


# ---------------------------------------------------------------------------
# 9. Tail smoothness
# ---------------------------------------------------------------------------
def tail_smoothness(ir, sr):
    """Measure how smooth the decay envelope is.

    Returns:
        dict with envelope_std_db and decay_rate_db_per_sec
    """
    env = rms_envelope(ir, sr, window_ms=50)
    peak_idx = np.argmax(env)
    start = max(peak_idx, int(sr * 0.1))

    end = len(env)
    for i in range(start, len(env)):
        if env[i] < -60:
            end = i
            break

    if end - start < int(sr * 0.1):
        return {"envelope_std_db": 0.0, "decay_rate_db_per_sec": 0.0}

    tail_env = env[start:end]
    times = np.arange(len(tail_env)) / sr

    if len(times) > 1:
        coeffs = np.polyfit(times, tail_env, 1)
        decay_rate = coeffs[0]
        residuals = tail_env - np.polyval(coeffs, times)
        smoothness_std = float(np.std(residuals))
    else:
        decay_rate = 0.0
        smoothness_std = 0.0

    return {
        "envelope_std_db": smoothness_std,
        "decay_rate_db_per_sec": float(decay_rate),
    }


# ---------------------------------------------------------------------------
# 10. Early reflection analysis
# ---------------------------------------------------------------------------
def analyze_early_reflections(ir, sr, er_window_ms=80):
    """Analyze early reflection pattern.

    Returns:
        dict with peak count, density, timing, ER-to-tail ratio
    """
    er_len = int(sr * er_window_ms / 1000)
    er = ir[:min(er_len, len(ir))]

    threshold = 0.05 * np.max(np.abs(er))
    peaks = []
    for i in range(1, len(er) - 1):
        if abs(er[i]) > threshold and abs(er[i]) > abs(er[i-1]) and abs(er[i]) > abs(er[i+1]):
            peaks.append({
                "time_ms": float(i / sr * 1000),
                "level_db": float(20 * np.log10(max(abs(er[i]), 1e-10))),
            })

    er_density = len(peaks) / (er_window_ms / 1000) if peaks else 0

    er_energy = rms_db(ir[:er_len])
    tail_start = int(sr * 0.15)
    tail_energy = rms_db(ir[tail_start:tail_start + er_len]) if tail_start + er_len <= len(ir) else -100

    return {
        "num_peaks": len(peaks),
        "density_per_sec": float(er_density),
        "er_to_tail_db": float(er_energy - tail_energy),
        "peaks": peaks[:16],
    }


# ---------------------------------------------------------------------------
# 11. Transient alignment
# ---------------------------------------------------------------------------
def align_ir_pair(ir_a, ir_b, sr, search_ms=50):
    """Time-align two impulse responses by cross-correlating onsets.

    Finds the lag that maximizes cross-correlation in the first search_ms
    of the signals, then shifts the later signal to align with the earlier.

    Args:
        ir_a, ir_b: impulse response arrays
        sr: sample rate
        search_ms: window for onset detection (ms)

    Returns:
        (aligned_a, aligned_b, offset_samples) — offset is positive if
        ir_b was delayed relative to ir_a.
    """
    search_len = int(sr * search_ms / 1000)
    a = ir_a[:search_len].astype(np.float64)
    b = ir_b[:search_len].astype(np.float64)

    # Cross-correlate to find offset
    correlation = np.correlate(a, b, mode='full')
    lag = np.argmax(np.abs(correlation)) - (len(b) - 1)

    if lag > 0:
        # ir_b is delayed — shift it left (trim start)
        aligned_b = ir_b[lag:]
        aligned_a = ir_a[:len(aligned_b)]
    elif lag < 0:
        # ir_a is delayed — shift it left (trim start)
        aligned_a = ir_a[-lag:]
        aligned_b = ir_b[:len(aligned_a)]
    else:
        min_len = min(len(ir_a), len(ir_b))
        aligned_a = ir_a[:min_len]
        aligned_b = ir_b[:min_len]

    return aligned_a, aligned_b, int(lag)


# ---------------------------------------------------------------------------
# 12. IACC (Inter-Aural Cross-Correlation) per ISO 3382-1
# ---------------------------------------------------------------------------
def measure_iacc(ir_left, ir_right, sr, window_ms=50, hop_ms=25):
    """Measure IACC over time per ISO 3382-1.

    IACC = max of normalized cross-correlation within ±1ms ITD lag window.
    Values near 1.0 = mono-like; near 0.0 = fully decorrelated.

    Returns:
        (times, iacc_values) — numpy arrays
    """
    win = int(sr * window_ms / 1000)
    hop = int(sr * hop_ms / 1000)
    max_lag = int(sr * 0.001)  # ±1ms ITD window

    min_len = min(len(ir_left), len(ir_right))
    times = []
    iacc_values = []

    for pos in range(0, min_len - win, hop):
        l = ir_left[pos:pos + win].astype(np.float64)
        r = ir_right[pos:pos + win].astype(np.float64)

        if rms_db(l) < -70 and rms_db(r) < -70:
            break

        # Normalized cross-correlation in ±1ms lag window
        norm = np.sqrt(np.sum(l ** 2) * np.sum(r ** 2))
        if norm < 1e-20:
            iacc_values.append(1.0)
            times.append((pos + win // 2) / sr)
            continue

        # Compute cross-correlation for lags within ±max_lag
        xcorr = np.correlate(l, r, mode='full')
        center = len(l) - 1
        lo = max(0, center - max_lag)
        hi = min(len(xcorr), center + max_lag + 1)
        iacc = float(np.max(np.abs(xcorr[lo:hi])) / norm)
        iacc = min(iacc, 1.0)

        times.append((pos + win // 2) / sr)
        iacc_values.append(iacc)

    return np.array(times), np.array(iacc_values)


# ---------------------------------------------------------------------------
# 13. Crest factor over time
# ---------------------------------------------------------------------------
def crest_factor_over_time(ir, sr, window_ms=50, hop_ms=25):
    """Measure crest factor (peak/RMS) over time.

    High crest factor = discrete echoes still audible (grainy).
    Low crest factor (~sqrt(2) ≈ 1.41 for Gaussian noise) = smooth, dense.

    Returns:
        (times, crest_values) — numpy arrays (linear scale)
    """
    win = int(sr * window_ms / 1000)
    hop = int(sr * hop_ms / 1000)

    times = []
    crest_values = []

    for pos in range(0, len(ir) - win, hop):
        chunk = ir[pos:pos + win]
        if rms_db(chunk) < -80:
            break

        rms_val = np.sqrt(np.mean(chunk.astype(np.float64) ** 2))
        peak_val = np.max(np.abs(chunk))

        if rms_val < 1e-20:
            continue

        crest = float(peak_val / rms_val)
        times.append((pos + win // 2) / sr)
        crest_values.append(crest)

    return np.array(times), np.array(crest_values)


# ---------------------------------------------------------------------------
# 14. Pitch variance (LFO / shimmer detection)
# ---------------------------------------------------------------------------
def pitch_variance(ir, sr, window_ms=50, hop_ms=25, band=(800, 4000)):
    """Measure pitch variance over time via zero-crossing rate.

    Bandpass-filters to the specified range, then measures ZCR per window.
    High variance in ZCR indicates pitch modulation (LFO wobble, shimmer).

    Returns:
        dict with:
            "zcr_mean": mean zero-crossing rate (Hz)
            "zcr_std": std of zero-crossing rate (Hz)
            "zcr_variance_ratio": std/mean (0 = stable, >0.1 = wobble)
            "times": time axis
            "zcr_values": ZCR per window
    """
    # Bandpass filter
    lo, hi = band
    hi = min(hi, sr / 2 - 1)
    if lo >= hi:
        return {"zcr_mean": 0, "zcr_std": 0, "zcr_variance_ratio": 0,
                "times": np.array([]), "zcr_values": np.array([])}

    sos = butter(4, [lo, hi], btype='bandpass', fs=sr, output='sos')
    filtered = sosfiltfilt(sos, ir).astype(np.float32)

    win = int(sr * window_ms / 1000)
    hop = int(sr * hop_ms / 1000)

    times = []
    zcr_values = []

    for pos in range(0, len(filtered) - win, hop):
        chunk = filtered[pos:pos + win]
        if rms_db(chunk) < -70:
            break

        # Count zero crossings
        signs = np.sign(chunk)
        crossings = np.sum(np.abs(np.diff(signs)) > 0)
        zcr_hz = float(crossings * sr / (2 * win))  # Convert to Hz

        times.append((pos + win // 2) / sr)
        zcr_values.append(zcr_hz)

    times = np.array(times)
    zcr_values = np.array(zcr_values)

    if len(zcr_values) == 0:
        return {"zcr_mean": 0, "zcr_std": 0, "zcr_variance_ratio": 0,
                "times": times, "zcr_values": zcr_values}

    mean = float(np.mean(zcr_values))
    std = float(np.std(zcr_values))
    ratio = std / mean if mean > 0 else 0

    return {
        "zcr_mean": mean,
        "zcr_std": std,
        "zcr_variance_ratio": ratio,
        "times": times,
        "zcr_values": zcr_values,
    }


# ---------------------------------------------------------------------------
# 15. Spectral MSE (per 1/3-octave band)
# ---------------------------------------------------------------------------
# 1/3-octave center frequencies (ISO 266), 100 Hz – 10 kHz
THIRD_OCTAVE_BANDS = {
    "100": 100, "125": 125, "160": 160, "200": 200, "250": 250,
    "315": 315, "400": 400, "500": 500, "630": 630, "800": 800,
    "1k": 1000, "1.25k": 1250, "1.6k": 1600, "2k": 2000, "2.5k": 2500,
    "3.15k": 3150, "4k": 4000, "5k": 5000, "6.3k": 6300, "8k": 8000,
    "10k": 10000,
}


def _third_octave_bandpass(signal, sr, center_freq, order=4):
    """Bandpass filter for one 1/3-octave band."""
    factor = 2 ** (1 / 6)  # 1/3-octave half-width
    lo = center_freq / factor
    hi = center_freq * factor
    lo = max(lo, 10)
    hi = min(hi, sr / 2 - 1)
    if lo >= hi:
        return np.zeros_like(signal)
    sos = butter(order, [lo, hi], btype='bandpass', fs=sr, output='sos')
    return sosfiltfilt(sos, signal).astype(np.float32)


def spectral_mse(ir_a, ir_b, sr, bands=None):
    """Compute MSE between two IRs per 1/3-octave band (in dB domain).

    Both IRs should be time-aligned and level-normalized before calling.

    Args:
        ir_a, ir_b: aligned impulse responses
        sr: sample rate
        bands: dict of {name: center_freq}; defaults to THIRD_OCTAVE_BANDS

    Returns:
        dict mapping band name -> MSE in dB²
    """
    if bands is None:
        bands = THIRD_OCTAVE_BANDS

    min_len = min(len(ir_a), len(ir_b))
    a = ir_a[:min_len]
    b = ir_b[:min_len]

    results = {}
    for name, fc in bands.items():
        fa = _third_octave_bandpass(a, sr, fc)
        fb = _third_octave_bandpass(b, sr, fc)

        # Convert to dB envelopes (50ms RMS)
        env_a = rms_envelope(fa, sr, window_ms=50)
        env_b = rms_envelope(fb, sr, window_ms=50)

        min_env = min(len(env_a), len(env_b))
        if min_env < 10:
            results[name] = 0.0
            continue

        # MSE of dB envelopes (exclude below noise floor)
        mask = (env_a[:min_env] > -70) | (env_b[:min_env] > -70)
        if np.sum(mask) < 10:
            results[name] = 0.0
            continue

        diff = env_a[:min_env][mask] - env_b[:min_env][mask]
        results[name] = float(np.mean(diff ** 2))

    return results


def measure_rt60_third_octave(ir, sr):
    """Compute RT60 per 1/3-octave band using Schroeder backward integration.

    Returns:
        dict mapping band name -> RT60 in seconds (None if unmeasurable)
    """
    return measure_rt60_per_band(ir, sr, bands=THIRD_OCTAVE_BANDS)


# ---------------------------------------------------------------------------
# Comprehensive analysis: run all metrics on one IR
# ---------------------------------------------------------------------------
def analyze_ir(ir_left, ir_right, sr):
    """Run all analysis metrics on a stereo impulse response.

    Returns a dict with all metric results.
    """
    ir = ir_left  # Use left channel as primary

    return {
        "rt60": measure_rt60_per_band(ir, sr),
        "rt60_third_octave": measure_rt60_third_octave(ir, sr),
        "edc": compute_edc(ir, sr),
        "spectrogram": compute_spectrogram(ir, sr),
        "resonances": detect_modal_resonances(ir, sr),
        "echo_density": echo_density_over_time(ir, sr),
        "stereo": stereo_decorrelation_over_time(ir_left, ir_right, sr),
        "iacc": measure_iacc(ir_left, ir_right, sr),
        "crest_factor": crest_factor_over_time(ir, sr),
        "pitch_variance": pitch_variance(ir, sr),
        "decay_rates": spectral_decay_rates(ir, sr),
        "freq_response": frequency_response(ir, sr),
        "smoothness": tail_smoothness(ir, sr),
        "early_reflections": analyze_early_reflections(ir, sr),
    }
