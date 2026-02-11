"""
IR Analysis Module — Core analysis functions for impulse response comparison.

Provides: RT60 measurement, spectral decay analysis, energy-time curve (ETC),
pre-delay detection, spectral centroid tracking, and frequency-band energy profiles.
"""

import numpy as np
from scipy import signal
from scipy.fft import rfft, rfftfreq
from dataclasses import dataclass, field
from typing import Optional


@dataclass
class IRProfile:
    """Complete analysis profile of an impulse response."""
    name: str
    sample_rate: int
    duration_s: float
    num_channels: int

    # Time-domain
    rt60: float                          # seconds (T30 extrapolated)
    rt60_t20: float                      # T20 extrapolated RT60
    edt: float                           # Early Decay Time (0 to -10dB, x6)
    pre_delay_ms: float                  # time to first significant energy
    peak_amplitude: float

    # Energy decay
    edc: np.ndarray = field(repr=False)  # Energy Decay Curve (Schroeder integral), dB
    edc_time: np.ndarray = field(repr=False)

    # Spectral
    spectral_centroid_hz: np.ndarray = field(repr=False)  # over time windows
    spectral_centroid_time: np.ndarray = field(repr=False)
    band_rt60: dict = field(default_factory=dict)  # RT60 per octave band
    freq_response_early: np.ndarray = field(repr=False, default=None)  # 0-50ms
    freq_response_late: np.ndarray = field(repr=False, default=None)   # 200ms+
    freq_axis: np.ndarray = field(repr=False, default=None)

    # Stereo
    stereo_correlation: float = 0.0
    width_estimate: float = 0.0


def load_ir(path: str, mono_mix: bool = False) -> tuple[np.ndarray, int]:
    """Load an IR file, return (data, sample_rate). Data shape: (channels, samples)."""
    import soundfile as sf
    data, sr = sf.read(path, dtype='float32')
    if data.ndim == 1:
        data = data[np.newaxis, :]  # (1, N)
    else:
        data = data.T  # (channels, N)
    if mono_mix and data.shape[0] > 1:
        data = np.mean(data, axis=0, keepdims=True)
    return data, sr


def compute_edc(ir: np.ndarray) -> np.ndarray:
    """Compute Energy Decay Curve via backward Schroeder integration. Returns dB."""
    energy = ir ** 2
    edc = np.cumsum(energy[::-1])[::-1]
    edc = np.maximum(edc, 1e-20)
    edc_db = 10.0 * np.log10(edc / edc[0])
    return edc_db


def measure_rt60(ir: np.ndarray, sr: int, method: str = 'T30') -> float:
    """
    Measure RT60 from a mono IR using linear regression on the EDC.
    method: 'T20' (-5 to -25dB, x3), 'T30' (-5 to -35dB, x2), 'EDT' (0 to -10dB, x6)
    """
    edc_db = compute_edc(ir)
    t = np.arange(len(edc_db)) / sr

    if method == 'EDT':
        start_db, end_db, mult = 0.0, -10.0, 6.0
    elif method == 'T20':
        start_db, end_db, mult = -5.0, -25.0, 3.0
    else:  # T30
        start_db, end_db, mult = -5.0, -35.0, 2.0

    # Find indices
    start_idx = np.searchsorted(-edc_db, -start_db)
    end_idx = np.searchsorted(-edc_db, -end_db)

    if end_idx <= start_idx or end_idx >= len(edc_db):
        # Fallback: use whatever range we can
        end_idx = min(len(edc_db) - 1, start_idx + sr)
        if end_idx <= start_idx:
            return 0.0

    # Linear regression on the decay region
    t_region = t[start_idx:end_idx]
    edc_region = edc_db[start_idx:end_idx]

    if len(t_region) < 10:
        return 0.0

    coeffs = np.polyfit(t_region, edc_region, 1)
    slope = coeffs[0]  # dB/s

    if slope >= 0:
        return 0.0

    rt60 = -60.0 / slope * (1.0 / mult) * mult  # = -60 / slope
    return max(0.0, rt60)


def detect_pre_delay(ir: np.ndarray, sr: int, threshold_db: float = -30.0) -> float:
    """Detect pre-delay as time to first sample exceeding threshold below peak."""
    peak = np.max(np.abs(ir))
    if peak < 1e-10:
        return 0.0
    threshold = peak * 10 ** (threshold_db / 20.0)
    above = np.where(np.abs(ir) > threshold)[0]
    if len(above) == 0:
        return 0.0
    return above[0] / sr * 1000.0  # ms


def spectral_centroid_over_time(ir: np.ndarray, sr: int,
                                 window_ms: float = 50.0,
                                 hop_ms: float = 25.0) -> tuple[np.ndarray, np.ndarray]:
    """Track spectral centroid over time using windowed FFT."""
    window_samples = int(sr * window_ms / 1000)
    hop_samples = int(sr * hop_ms / 1000)
    n_fft = max(2048, window_samples)

    freqs = rfftfreq(n_fft, 1.0 / sr)
    centroids = []
    times = []

    pos = 0
    window = signal.windows.hann(window_samples)
    while pos + window_samples <= len(ir):
        chunk = ir[pos:pos + window_samples] * window
        spectrum = np.abs(rfft(chunk, n=n_fft))
        energy = np.sum(spectrum)
        if energy > 1e-10:
            centroid = np.sum(freqs * spectrum) / energy
        else:
            centroid = 0.0
        centroids.append(centroid)
        times.append((pos + window_samples / 2) / sr)
        pos += hop_samples

    return np.array(centroids), np.array(times)


def frequency_response_window(ir: np.ndarray, sr: int,
                               start_ms: float, end_ms: float,
                               n_fft: int = 4096) -> tuple[np.ndarray, np.ndarray]:
    """Compute magnitude frequency response of a time window within the IR."""
    start_sample = int(sr * start_ms / 1000)
    end_sample = int(sr * end_ms / 1000)
    end_sample = min(end_sample, len(ir))

    if start_sample >= len(ir) or start_sample >= end_sample:
        return rfftfreq(n_fft, 1.0 / sr), np.zeros(n_fft // 2 + 1)

    chunk = ir[start_sample:end_sample]
    # Apply window
    w = signal.windows.hann(len(chunk))
    chunk = chunk * w
    spectrum = np.abs(rfft(chunk, n=n_fft))
    spectrum = np.maximum(spectrum, 1e-10)
    spectrum_db = 20 * np.log10(spectrum / np.max(spectrum))
    freqs = rfftfreq(n_fft, 1.0 / sr)
    return freqs, spectrum_db


def band_rt60(ir: np.ndarray, sr: int,
              bands: Optional[list] = None) -> dict[str, float]:
    """Measure RT60 in octave bands using bandpass filtering."""
    if bands is None:
        bands = [125, 250, 500, 1000, 2000, 4000, 8000]

    results = {}
    for fc in bands:
        # Design 1-octave bandpass filter
        low = fc / np.sqrt(2)
        high = fc * np.sqrt(2)
        high = min(high, sr / 2 - 1)
        if low >= high:
            continue
        sos = signal.butter(4, [low, high], btype='band', fs=sr, output='sos')
        filtered = signal.sosfilt(sos, ir)
        rt = measure_rt60(filtered, sr, method='T30')
        results[f'{fc}Hz'] = rt

    return results


def stereo_analysis(ir_l: np.ndarray, ir_r: np.ndarray) -> tuple[float, float]:
    """Compute stereo correlation and width estimate."""
    min_len = min(len(ir_l), len(ir_r))
    l = ir_l[:min_len]
    r = ir_r[:min_len]

    # Correlation
    l_energy = np.sqrt(np.sum(l ** 2))
    r_energy = np.sqrt(np.sum(r ** 2))
    if l_energy < 1e-10 or r_energy < 1e-10:
        return 0.0, 0.0

    correlation = np.sum(l * r) / (l_energy * r_energy)

    # Width: based on M/S ratio
    mid = (l + r) / 2
    side = (l - r) / 2
    mid_energy = np.sum(mid ** 2)
    side_energy = np.sum(side ** 2)
    total = mid_energy + side_energy
    width = side_energy / total if total > 1e-10 else 0.0

    return float(correlation), float(width)


def analyze_ir(data: np.ndarray, sr: int, name: str = "unknown") -> IRProfile:
    """Full analysis of an IR. data shape: (channels, samples)."""
    # Use left channel (or mono) for primary analysis
    ir_mono = data[0] if data.shape[0] >= 1 else data.flatten()

    # Trim trailing silence (below -80dB)
    peak = np.max(np.abs(ir_mono))
    if peak < 1e-10:
        # Silent IR
        return IRProfile(
            name=name, sample_rate=sr, duration_s=len(ir_mono) / sr,
            num_channels=data.shape[0], rt60=0.0, rt60_t20=0.0, edt=0.0,
            pre_delay_ms=0.0, peak_amplitude=0.0,
            edc=np.zeros(1), edc_time=np.zeros(1),
            spectral_centroid_hz=np.zeros(1), spectral_centroid_time=np.zeros(1),
        )

    threshold = peak * 1e-4  # -80dB
    last_above = np.where(np.abs(ir_mono) > threshold)[0]
    if len(last_above) > 0:
        trim_end = min(last_above[-1] + sr // 10, len(ir_mono))  # +100ms padding
    else:
        trim_end = len(ir_mono)
    ir_trimmed = ir_mono[:trim_end]

    # RT60 measurements
    rt60_t30 = measure_rt60(ir_trimmed, sr, 'T30')
    rt60_t20 = measure_rt60(ir_trimmed, sr, 'T20')
    edt = measure_rt60(ir_trimmed, sr, 'EDT')

    # EDC
    edc_db = compute_edc(ir_trimmed)
    edc_time = np.arange(len(edc_db)) / sr

    # Pre-delay
    pre_delay = detect_pre_delay(ir_trimmed, sr)

    # Spectral centroid tracking
    sc_hz, sc_time = spectral_centroid_over_time(ir_trimmed, sr)

    # Frequency responses (early vs late)
    freq_axis, fr_early = frequency_response_window(ir_trimmed, sr, 0, 50)
    _, fr_late = frequency_response_window(ir_trimmed, sr, 200, 500)

    # Band RT60
    b_rt60 = band_rt60(ir_trimmed, sr)

    # Stereo analysis
    correlation = 0.0
    width = 0.0
    if data.shape[0] >= 2:
        correlation, width = stereo_analysis(data[0, :trim_end], data[1, :trim_end])

    return IRProfile(
        name=name,
        sample_rate=sr,
        duration_s=trim_end / sr,
        num_channels=data.shape[0],
        rt60=rt60_t30,
        rt60_t20=rt60_t20,
        edt=edt,
        pre_delay_ms=pre_delay,
        peak_amplitude=float(peak),
        edc=edc_db,
        edc_time=edc_time,
        spectral_centroid_hz=sc_hz,
        spectral_centroid_time=sc_time,
        band_rt60=b_rt60,
        freq_response_early=fr_early,
        freq_response_late=fr_late,
        freq_axis=freq_axis,
        stereo_correlation=correlation,
        width_estimate=width,
    )


def profile_summary(p: IRProfile) -> str:
    """Human-readable summary of an IR profile."""
    lines = [
        f"=== {p.name} ===",
        f"  Duration: {p.duration_s:.2f}s | Channels: {p.num_channels} | SR: {p.sample_rate}",
        f"  RT60 (T30): {p.rt60:.3f}s | RT60 (T20): {p.rt60_t20:.3f}s | EDT: {p.edt:.3f}s",
        f"  Pre-delay: {p.pre_delay_ms:.1f}ms | Peak: {p.peak_amplitude:.4f}",
        f"  Stereo correlation: {p.stereo_correlation:.3f} | Width: {p.width_estimate:.3f}",
        f"  Band RT60:",
    ]
    for band, rt in sorted(p.band_rt60.items(), key=lambda x: int(x[0].replace('Hz', ''))):
        lines.append(f"    {band}: {rt:.3f}s")

    if len(p.spectral_centroid_hz) > 0:
        early_sc = np.mean(p.spectral_centroid_hz[:min(4, len(p.spectral_centroid_hz))])
        late_sc = np.mean(p.spectral_centroid_hz[max(0, len(p.spectral_centroid_hz) - 4):])
        lines.append(f"  Spectral centroid: early={early_sc:.0f}Hz, late={late_sc:.0f}Hz")

    return "\n".join(lines)
