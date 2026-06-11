#!/usr/bin/env python3
"""Perceptual comparison: Lex VST2 reference vs DV current render.

Standalone — no DV imports. Loads two rendered IR WAVs + a noise-burst WAV
each side, computes psychoacoustic metrics, prints plain-English summary.

Usage:
  python3 perceptual_diff.py <lex_impulse> <dv_impulse> [lex_noiseburst dv_noiseburst]

Defaults to /tmp/lex_compare/lex_v3_* and /tmp/dv_tune_final_* if no args.
"""
import os
import sys
import numpy as np
from scipy.io import wavfile
from scipy.signal import butter, sosfiltfilt, bilinear, lfilter, hilbert

BANDS = [125, 250, 500, 1000, 2000, 4000, 8000]


# ─── I/O ──────────────────────────────────────────────────────────────────

def load_mono(path):
    sr, x = wavfile.read(path)
    if x.dtype.kind == 'i':
        x = x.astype(np.float32) / np.iinfo(x.dtype).max
    if x.ndim > 1:
        x = x.mean(axis=1)
    return sr, x.astype(np.float64)


def load_stereo(path):
    """Load WAV without mono-collapse. Returns (sr, L, R) — both float64.
    For mono files returns (sr, x, x) so downstream code stays uniform."""
    sr, x = wavfile.read(path)
    if x.dtype.kind == 'i':
        x = x.astype(np.float32) / np.iinfo(x.dtype).max
    if x.ndim == 1:
        return sr, x.astype(np.float64), x.astype(np.float64)
    L = x[:, 0].astype(np.float64)
    R = x[:, 1].astype(np.float64) if x.shape[1] >= 2 else L
    return sr, L, R


# ─── Metrics ──────────────────────────────────────────────────────────────

def c80(ir, sr):
    """Clarity Index — 10·log10(E[0:80ms] / E[80ms:end]) dB.
    Positive = upfront/clear, negative = distant/wash."""
    peak = int(np.argmax(np.abs(ir)))
    tail = ir[peak:]
    split = int(0.080 * sr)
    if len(tail) < split + 1:
        return 0.0
    e_early = np.sum(tail[:split] ** 2)
    e_late = np.sum(tail[split:] ** 2)
    if e_late <= 1e-20 or e_early <= 1e-20:
        return 0.0
    return 10.0 * np.log10(e_early / e_late)


def a_weighting_sos(sr):
    """A-weighting filter (IEC 61672, Class 1). Standard analog design,
    bilinear-transformed to digital."""
    # Analog poles/zeros (rad/s)
    f1 = 20.598997
    f2 = 107.65265
    f3 = 737.86223
    f4 = 12194.217
    A1000 = 1.9997  # gain at 1 kHz to make A-weighting 0 dB there

    # Numerator: (s)^2 * (s)^2 (4 zeros at origin → 12 dB/oct LF rolloff up to f1)
    # Denominator: 4 real poles at -f1, -f2, -f3, -f4 (each pair)
    NUMs = [(2 * np.pi * f4) ** 2 * 10 ** (A1000 / 20.0), 0, 0, 0, 0]
    DENs = np.convolve([1.0, 4 * np.pi * f4, (2 * np.pi * f4) ** 2],
                        [1.0, 4 * np.pi * f1, (2 * np.pi * f1) ** 2])
    DENs = np.convolve(DENs, [1.0, 2 * np.pi * f2])
    DENs = np.convolve(DENs, [1.0, 2 * np.pi * f3])

    b, a = bilinear(NUMs, DENs, sr)
    return b, a


def a_weighted_rms_db(x, sr):
    b, a = a_weighting_sos(sr)
    y = lfilter(b, a, x)
    rms = np.sqrt(np.mean(y ** 2))
    return 20.0 * np.log10(rms + 1e-15)


def rt60_t20(ir, sr, fc):
    lo = max(fc / np.sqrt(2), 20.0)
    hi = min(fc * np.sqrt(2), sr / 2 - 100)
    sos = butter(4, [lo, hi], btype='band', fs=sr, output='sos')
    y = sosfiltfilt(sos, ir)
    e = y[::-1] ** 2
    edc = np.cumsum(e)[::-1]
    if edc.max() <= 0:
        return 0.0
    edc_db = 10 * np.log10(edc / edc.max() + 1e-15)
    try:
        i5 = np.where(edc_db < -5)[0][0]
        i25 = np.where(edc_db < -25)[0][0]
        return (i25 - i5) / sr * 60.0 / 20.0
    except IndexError:
        return 0.0


def bass_ratio(ir, sr):
    """BR = (T60_125 + T60_250) / (T60_500 + T60_1k). Standard concert-hall
    warmth metric. >1.0 = boomy/warm, <1.0 = thin."""
    t125 = rt60_t20(ir, sr, 125)
    t250 = rt60_t20(ir, sr, 250)
    t500 = rt60_t20(ir, sr, 500)
    t1k = rt60_t20(ir, sr, 1000)
    return (t125 + t250) / max(t500 + t1k, 1e-9)


def treble_ratio(ir, sr):
    """TR = (T60_2k + T60_4k) / (T60_500 + T60_1k). High-frequency
    sustain — >1.0 = bright/harsh, <1.0 = dull."""
    t2k = rt60_t20(ir, sr, 2000)
    t4k = rt60_t20(ir, sr, 4000)
    t500 = rt60_t20(ir, sr, 500)
    t1k = rt60_t20(ir, sr, 1000)
    return (t2k + t4k) / max(t500 + t1k, 1e-9)


def octave_band_db(x, sr, fc, t_start=0.1, t_end=None):
    """Absolute RMS in dB inside one octave band (fc/√2 to fc·√2) on the
    window [t_start, t_end] seconds. t_end=None → end of signal.
    Useful for catching broadband shape mismatches AND for comparing
    EARLY vs LATE windows (100-500 ms vs 500-2 s) to expose boom in
    the first half-second that averages out over a full tail."""
    lo = max(fc / np.sqrt(2), 20.0)
    hi = min(fc * np.sqrt(2), sr / 2 - 100)
    sos = butter(4, [lo, hi], btype='band', fs=sr, output='sos')
    s0 = int(t_start * sr)
    s1 = int(t_end * sr) if t_end is not None else len(x)
    s1 = min(s1, len(x))
    seg = x[s0:s1]
    if seg.size < 256:
        return -100.0
    y = sosfiltfilt(sos, seg)
    return float(20 * np.log10(np.sqrt(np.mean(y ** 2)) + 1e-12))


def decade_db(x, sr, lo_hz, hi_hz, t_start=0.1, t_end=2.0):
    """Absolute RMS dB inside a decade band, measured on the t_start..t_end
    window of the FFT magnitude spectrum. Catches the broadband shape
    mismatches that octave-band measurements smooth over."""
    s0 = int(t_start * sr)
    s1 = min(int(t_end * sr), len(x))
    if s1 - s0 < 256:
        return -100.0
    seg = x[s0:s1] * np.hanning(s1 - s0)
    spec = np.abs(np.fft.rfft(seg))
    freqs = np.fft.rfftfreq(len(seg), 1.0 / sr)
    mask = (freqs >= lo_hz) & (freqs < hi_hz)
    if not mask.any():
        return -100.0
    return float(20 * np.log10(np.sqrt(np.mean(spec[mask] ** 2)) + 1e-12))


def d50(ir, sr):
    """Definition (ISO 3382) — 10·log10(E[0:50ms] / E[50ms:end]) dB.
    Catches discrete-echo / very-early energy that C80 (80 ms window)
    smooths over. Lex Echoes 1/2 at 17/151/134 ms appear here."""
    peak = int(np.argmax(np.abs(ir)))
    tail = ir[peak:]
    split = int(0.050 * sr)
    if len(tail) < split + 1:
        return 0.0
    e_early = float(np.sum(tail[:split] ** 2))
    e_late = float(np.sum(tail[split:] ** 2))
    if e_late <= 1e-20 or e_early <= 1e-20:
        return 0.0
    return 10.0 * float(np.log10(e_early / e_late))


def c80_per_octave(ir, sr, fc):
    """Octave-band C80. Single broadband C80 averages across frequency
    and hides per-band differences (e.g. bass C80=+12 vs treble C80=+3)."""
    lo = max(fc / np.sqrt(2), 20.0)
    hi = min(fc * np.sqrt(2), sr / 2 - 100)
    sos = butter(4, [lo, hi], btype='band', fs=sr, output='sos')
    y = sosfiltfilt(sos, ir)
    peak = int(np.argmax(np.abs(y)))
    tail = y[peak:]
    split = int(0.080 * sr)
    if len(tail) < split + 1:
        return 0.0
    e_e = float(np.sum(tail[:split] ** 2))
    e_l = float(np.sum(tail[split:] ** 2))
    if e_l <= 1e-20 or e_e <= 1e-20:
        return 0.0
    return 10.0 * float(np.log10(e_e / e_l))


def edt(ir, sr, fc=None):
    """Early Decay Time — T0 → -10 dB slope × 6 = T60-equivalent.
    The ear hears EDT far more than the full -60 dB decay (which is often
    dominated by noise floor or sympathetic ringing). On plates, EDT can
    be 20-50 % shorter than T20-derived RT60.

    fc=None → broadband. fc set → bandpassed at that octave first."""
    if fc is not None:
        lo = max(fc / np.sqrt(2), 20.0)
        hi = min(fc * np.sqrt(2), sr / 2 - 100)
        sos = butter(4, [lo, hi], btype='band', fs=sr, output='sos')
        y = sosfiltfilt(sos, ir)
    else:
        y = ir
    e = y[::-1] ** 2
    edc = np.cumsum(e)[::-1]
    if edc.max() <= 0:
        return 0.0
    edc_db = 10 * np.log10(edc / edc.max() + 1e-15)
    # T0 → -10 dB extrapolated to -60 dB ⇒ multiply by 6.
    try:
        i0 = int(np.argmax(edc_db <= 0.0))   # first index at peak (0 dB)
        i10 = np.where(edc_db < -10)[0][0]
        return float((i10 - i0) / sr * 6.0)
    except IndexError:
        return 0.0


def stereo_correlation(path, t_start=0.1):
    """Inter-channel L/R correlation on tail. -1 = anti-phase, 0 = uncorrelated
    (widest stereo), +1 = mono. Catches Lex 'Tail Width = Stereo' vs DV
    mono-collapse — invisible to all mono-summed measurements."""
    sr, L, R = load_stereo(path)
    s = int(t_start * sr)
    Ls = L[s:]
    Rs = R[s:]
    n = min(len(Ls), len(Rs))
    if n < 256:
        return 1.0
    a = Ls[:n] - Ls[:n].mean()
    b = Rs[:n] - Rs[:n].mean()
    denom = np.sqrt(np.sum(a * a) * np.sum(b * b))
    if denom <= 1e-20:
        return 1.0
    return float(np.clip(np.sum(a * b) / denom, -1.0, 1.0))


def time_domain_crest(x, sr, t_start=0.1):
    """Peak/RMS of the time-domain envelope (NOT FFT magnitude).
    Spectral crest catches modal peakiness; time-domain crest catches
    'impulsive stab' vs 'continuous wash' perception. Lex's plate with
    discrete echoes should read significantly higher than a smooth FDN tail.

    Smooth envelope over ~5 ms so individual noise spikes don't dominate."""
    seg = x[int(t_start * sr):]
    if seg.size < 256:
        return 0.0
    env = np.abs(hilbert(seg))
    win = max(1, int(0.005 * sr))  # 5 ms smoothing
    env = np.convolve(env, np.ones(win) / win, mode='same')
    pk = float(env.max())
    rms = float(np.sqrt(np.mean(env ** 2)))
    if rms <= 1e-20:
        return 0.0
    return 20.0 * float(np.log10(pk / rms))


# ISO 226 (60 phon) equal-loudness contour. Values = SPL needed for the
# band to sound equally loud as 1 kHz @ 50 dB SPL. Larger = ear attenuates
# more. The *weight* applied to per-band dB differences is `50 - contour`,
# so the weighting expresses "how much the ear hears each dB at this band".
ISO226_60PHON = {
    31:    78.0,  63:    67.0,  125:   60.0,  250:   54.0,  500:   51.0,
    1000:  50.0,  2000:  47.0,  4000:  42.0,  8000:  50.0,  16000: 65.0,
}


def equal_loudness_weight(fc):
    """Convert an octave-band centre frequency to its equal-loudness
    weighting in dB (1 kHz = 0 dB reference). Negative = ear less
    sensitive at this band, so each raw dB matters LESS perceptually."""
    if fc in ISO226_60PHON:
        return 50.0 - ISO226_60PHON[fc]
    # Linear-log interpolate between known points if fc not in table.
    keys = sorted(ISO226_60PHON.keys())
    if fc <= keys[0]:
        return 50.0 - ISO226_60PHON[keys[0]]
    if fc >= keys[-1]:
        return 50.0 - ISO226_60PHON[keys[-1]]
    for k1, k2 in zip(keys, keys[1:]):
        if k1 <= fc <= k2:
            t = (np.log(fc) - np.log(k1)) / (np.log(k2) - np.log(k1))
            v = ISO226_60PHON[k1] + t * (ISO226_60PHON[k2] - ISO226_60PHON[k1])
            return 50.0 - v
    return 0.0


def k_weighting_ba(sr):
    """ITU-R BS.1770 K-weighting filter: pre-filter (high-shelf @ ~1.5 kHz
    +4 dB) + RLB filter (HPF @ ~38 Hz). Standard broadcast/streaming
    loudness measurement; correlates with perceived loudness on music."""
    # Pre-filter (high-shelf)
    f0 = 1681.974450955533
    G = 3.999843853973347
    Q = 0.7071752369554196
    K = np.tan(np.pi * f0 / sr)
    Vh = 10 ** (G / 20.0)
    Vb = Vh ** 0.4996667741545416
    a0 = 1.0 + K / Q + K * K
    b_pre = np.array([
        (Vh + Vb * K / Q + K * K) / a0,
        2.0 * (K * K - Vh) / a0,
        (Vh - Vb * K / Q + K * K) / a0,
    ])
    a_pre = np.array([
        1.0,
        2.0 * (K * K - 1.0) / a0,
        (1.0 - K / Q + K * K) / a0,
    ])
    # RLB filter (HPF)
    f0 = 38.13547087602444
    Q = 0.5003270373238773
    K = np.tan(np.pi * f0 / sr)
    a0 = 1.0 + K / Q + K * K
    b_rlb = np.array([1.0, -2.0, 1.0]) / a0
    a_rlb = np.array([1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0])
    return (b_pre, a_pre), (b_rlb, a_rlb)


def k_weighted_lufs(x, sr):
    """Mono K-weighted loudness in LU (close to dBFS for steady signals).
    Standard BS.1770 mono — channel weighting omitted since we operate
    on mono-summed signals throughout this tool."""
    (b1, a1), (b2, a2) = k_weighting_ba(sr)
    y = lfilter(b1, a1, x)
    y = lfilter(b2, a2, y)
    msq = np.mean(y ** 2)
    if msq <= 1e-20:
        return -100.0
    return -0.691 + 10.0 * float(np.log10(msq))


def momentary_lufs_max(x, sr, window_s=0.4, hop_s=0.1):
    """BS.1770 "momentary" loudness — max LUFS over any sliding window_s
    window. Catches loudness PEAKS during the reverb buildup. Average
    LUFS over a full 4 s tail can match between two reverbs while one
    has a much louder 100-500 ms peak that's the actual perceived
    loudness on real material."""
    (b1, a1), (b2, a2) = k_weighting_ba(sr)
    y = lfilter(b1, a1, x)
    y = lfilter(b2, a2, y)
    win = int(window_s * sr)
    hop = max(1, int(hop_s * sr))
    if len(y) < win:
        return k_weighted_lufs(x, sr)
    peak = -100.0
    for i in range(0, len(y) - win + 1, hop):
        seg = y[i:i + win]
        msq = float(np.mean(seg ** 2))
        if msq <= 1e-20:
            continue
        m = -0.691 + 10.0 * float(np.log10(msq))
        if m > peak:
            peak = m
    return peak


def box_ratio_db(x, sr, t_start=0.1):
    """Peak FFT magnitude in 200-500 Hz over GEOMETRIC MEAN of the
    100-200 Hz and 500-1000 Hz flanks. Catches the "boxy / cardboard"
    perceptual axis — a 250-400 Hz hump invisible to octave-band RMS
    because octave averaging smears narrow peaks across the band.
    Positive dB = boxy hump present."""
    seg = x[int(t_start * sr):]
    if seg.size < 1024:
        return 0.0
    nfft = 1 << int(np.ceil(np.log2(len(seg))))
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n=nfft))
    freqs = np.fft.rfftfreq(nfft, 1.0 / sr)
    box_mask  = (freqs >= 200) & (freqs <= 500)
    lo_mask   = (freqs >= 100) & (freqs <= 200)
    hi_mask   = (freqs >= 500) & (freqs <= 1000)
    if not (box_mask.any() and lo_mask.any() and hi_mask.any()):
        return 0.0
    box_peak  = float(spec[box_mask].max())
    lo_mean   = float(np.sqrt(np.mean(spec[lo_mask] ** 2)))
    hi_mean   = float(np.sqrt(np.mean(spec[hi_mask] ** 2)))
    flank = np.sqrt(lo_mean * hi_mean)
    if flank <= 1e-20 or box_peak <= 1e-20:
        return 0.0
    return 20.0 * float(np.log10(box_peak / flank))


def spectral_crest_db(x, sr, band=(20.0, None)):
    """Peak/RMS of FFT magnitude in a band, expressed in dB.
    High = peaky/piercing spectrum, low = smooth/diffuse."""
    hi = band[1] if band[1] is not None else sr / 2 - 100
    nfft = 1 << int(np.ceil(np.log2(len(x))))
    spec = np.abs(np.fft.rfft(x * np.hanning(len(x)), n=nfft))
    freqs = np.fft.rfftfreq(nfft, 1.0 / sr)
    mask = (freqs >= band[0]) & (freqs <= hi)
    seg = spec[mask]
    if seg.size == 0:
        return 0.0
    pk = seg.max()
    rms = np.sqrt(np.mean(seg ** 2))
    if rms <= 1e-20:
        return 0.0
    return 20.0 * np.log10(pk / rms)


def spectral_flatness_octave(x, sr, fc, t_start=0.1, t_end=None):
    """Wiener entropy (geometric mean / arithmetic mean of FFT magnitude)
    inside one octave band on the window [t_start, t_end]. 1.0 = noise-
    like (smooth, realistic), 0.0 = pure tone (peaky, metallic).

    Distinguishes the "metallic" perceptual axis that spectral_crest
    misses: crest measures single-bin peak vs RMS, flatness measures the
    DISTRIBUTION of spectral energy. A spectrum with 5 narrow peaks at
    equal levels reads low crest (peaks ~ RMS) but very low flatness
    (highly tonal) — exactly the metallic-plate character."""
    lo = max(fc / np.sqrt(2), 20.0)
    hi = min(fc * np.sqrt(2), sr / 2 - 100)
    s0 = int(t_start * sr)
    s1 = int(t_end * sr) if t_end is not None else len(x)
    s1 = min(s1, len(x))
    seg = x[s0:s1]
    if seg.size < 1024:
        return 0.0
    sos = butter(4, [lo, hi], btype='band', fs=sr, output='sos')
    y = sosfiltfilt(sos, seg)
    if np.max(np.abs(y)) < 1e-12:
        return 0.0
    nfft = 1 << int(np.ceil(np.log2(len(y))))
    spec = np.abs(np.fft.rfft(y * np.hanning(len(y)), n=nfft))
    freqs = np.fft.rfftfreq(nfft, 1.0 / sr)
    mask = (freqs >= lo) & (freqs <= hi)
    seg_spec = spec[mask]
    seg_spec = seg_spec[seg_spec > 1e-20]
    if seg_spec.size < 4:
        return 0.0
    geo = float(np.exp(np.mean(np.log(seg_spec))))
    arith = float(np.mean(seg_spec))
    return geo / arith if arith > 1e-20 else 0.0


def comb_periodicity_db(x, sr, t_start=0.1, t_end=None,
                         min_period_ms=5.0, max_period_ms=80.0):
    """Detects periodic comb-teeth structure in the magnitude spectrum.

    Method: take FFT magnitude of the tail; autocorrelate the log-mag
    spectrum; report the strongest non-trivial peak's prominence in dB
    above the median of the autocorr function. A comb at 31 ms shows
    spectral spacing of ~32 Hz, autocorrelation of the spectrum will
    have a strong peak at that spacing. Tonal/metallic = peak prominent
    above median; noisy/realistic = peak near median.

    Returns dB-above-median of the strongest comb-period peak. Also
    returns the period (ms) of that peak so the caller can report which
    comb is dominant."""
    s0 = int(t_start * sr)
    s1 = int(t_end * sr) if t_end is not None else len(x)
    s1 = min(s1, len(x))
    seg = x[s0:s1]
    if seg.size < 2048:
        return 0.0, 0.0
    nfft = 1 << int(np.ceil(np.log2(len(seg))))
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n=nfft))
    log_spec = np.log(spec + 1e-12)
    log_spec -= np.mean(log_spec)
    ac = np.correlate(log_spec, log_spec, mode='full')
    ac = ac[len(ac) // 2:]
    if ac[0] <= 1e-20:
        return 0.0, 0.0
    ac = ac / ac[0]
    bin_hz = sr / nfft
    min_lag_bins = int(round((1000.0 / max_period_ms) / bin_hz))
    max_lag_bins = int(round((1000.0 / min_period_ms) / bin_hz))
    min_lag_bins = max(min_lag_bins, 2)
    max_lag_bins = min(max_lag_bins, len(ac) - 1)
    if max_lag_bins <= min_lag_bins:
        return 0.0, 0.0
    window = ac[min_lag_bins:max_lag_bins]
    if window.size == 0:
        return 0.0, 0.0
    peak_idx = int(np.argmax(window))
    peak_val = float(window[peak_idx])
    median_val = float(np.median(np.abs(window)))
    if median_val <= 1e-20 or peak_val <= 1e-20:
        return 0.0, 0.0
    spacing_hz = (peak_idx + min_lag_bins) * bin_hz
    period_ms = 1000.0 / spacing_hz if spacing_hz > 0 else 0.0
    prom_db = 20.0 * float(np.log10(peak_val / median_val))
    return prom_db, period_ms


def echo_density_at_time(ir, sr, t_sec, window_ms=20.0):
    """Abel-Huang normalized echo density η(t).

    Measures the proportion of samples in a sliding window whose magnitude
    exceeds the window's RMS. For Gaussian (noise-like) signal η→1.0; for
    a small number of impulses (sparse, metallic) η<<1.0.

    Real plate reverb reaches η≈1.0 within 50-150 ms (Schroeder regime —
    densely overlapping modes look like noise). A sparse-mode FDN may
    never cross 0.9 because its discrete modal structure persists into
    the tail.

    Returns η at time t_sec computed over a window of window_ms width."""
    win = int(window_ms * 0.001 * sr)
    center = int(t_sec * sr)
    lo = max(0, center - win // 2)
    hi = min(len(ir), lo + win)
    if hi - lo < 64:
        return 0.0
    seg = ir[lo:hi]
    rms = float(np.sqrt(np.mean(seg ** 2)))
    if rms <= 1e-20:
        return 0.0
    over = float(np.mean(np.abs(seg) > rms))
    # Normalize: Gaussian noise has ~31.7% of samples above RMS (1 - erf(1/sqrt(2)))
    return over / 0.3173


def centroid_drift_db(x, sr, fc, t_early=(0.05, 0.25), t_late=(0.75, 1.75)):
    """Spectral centroid drift across the tail at a single fc band.

    Computes centroid (Hz) inside the octave around fc on TWO windows
    (early, late). Returns the DRIFT in Hz between them. A real plate's
    modes decay at slightly different rates so the centroid drifts as
    fast-decaying modes die first, slow ones persist. An FDN with
    uniform damping per band has zero drift — centroid is static.
    Non-zero drift = realistic; zero drift = synthetic."""
    lo = max(fc / np.sqrt(2), 20.0)
    hi = min(fc * np.sqrt(2), sr / 2 - 100)
    sos = butter(4, [lo, hi], btype='band', fs=sr, output='sos')

    def centroid(seg):
        if seg.size < 256:
            return 0.0
        y = sosfiltfilt(sos, seg)
        nfft = 1 << int(np.ceil(np.log2(len(y))))
        spec = np.abs(np.fft.rfft(y * np.hanning(len(y)), n=nfft))
        freqs = np.fft.rfftfreq(nfft, 1.0 / sr)
        mask = (freqs >= lo) & (freqs <= hi)
        s = spec[mask]
        f = freqs[mask]
        if s.sum() < 1e-20:
            return 0.0
        return float(np.sum(f * s) / np.sum(s))

    e_lo = int(t_early[0] * sr); e_hi = min(int(t_early[1] * sr), len(x))
    l_lo = int(t_late[0]  * sr); l_hi = min(int(t_late[1]  * sr), len(x))
    if e_hi - e_lo < 256 or l_hi - l_lo < 256:
        return 0.0
    c_early = centroid(x[e_lo:e_hi])
    c_late  = centroid(x[l_lo:l_hi])
    return c_late - c_early  # Hz of drift; positive = upward, negative = downward


def stereo_correlation_octave(path, fc, t_start=0.1, t_end=None):
    """Inter-channel correlation in a single octave band. Catches per-
    band coherence mismatches that broadband stereo_correlation hides
    (Lex may have decorrelated highs from plate physics while DV stays
    correlated, or vice-versa)."""
    sr, L, R = load_stereo(path)
    s0 = int(t_start * sr)
    s1 = int(t_end * sr) if t_end is not None else len(L)
    s1 = min(s1, len(L), len(R))
    if s1 - s0 < 1024:
        return 0.0
    lo = max(fc / np.sqrt(2), 20.0)
    hi = min(fc * np.sqrt(2), sr / 2 - 100)
    sos = butter(4, [lo, hi], btype='band', fs=sr, output='sos')
    Lb = sosfiltfilt(sos, L[s0:s1])
    Rb = sosfiltfilt(sos, R[s0:s1])
    if np.std(Lb) < 1e-12 or np.std(Rb) < 1e-12:
        return 1.0
    return float(np.corrcoef(Lb, Rb)[0, 1])


# ─── Sonic translation ────────────────────────────────────────────────────

def describe_br(lex, dv):
    diff = dv - lex
    if abs(diff) < 0.05:
        return "low-end warmth matches"
    if diff > 0.20:
        return f"notably boomier/warmer than Lex (BR +{diff:.2f})"
    if diff > 0.05:
        return f"slightly warmer than Lex (BR +{diff:.2f})"
    if diff < -0.20:
        return f"notably thinner/colder than Lex (BR {diff:.2f})"
    return f"slightly thinner than Lex (BR {diff:.2f})"


def describe_tr(lex, dv):
    diff = dv - lex
    if abs(diff) < 0.05:
        return "treble sustain matches"
    if diff > 0.20:
        return f"notably brighter/harsher (TR +{diff:.2f})"
    if diff > 0.05:
        return f"slightly brighter (TR +{diff:.2f})"
    if diff < -0.20:
        return f"notably duller/darker (TR {diff:.2f})"
    return f"slightly darker (TR {diff:.2f})"


def describe_c80(lex, dv):
    diff = dv - lex
    if abs(diff) < 1.0:
        return "front-to-back balance matches"
    if diff > 3.0:
        return f"more upfront/dry than Lex (+{diff:.1f} dB C80)"
    if diff > 1.0:
        return f"slightly more present (+{diff:.1f} dB C80)"
    if diff < -3.0:
        return f"more distant/washy ({diff:.1f} dB C80)"
    return f"slightly more reverberant ({diff:.1f} dB C80)"


def describe_loudness(lex, dv):
    diff = dv - lex
    if abs(diff) < 0.5:
        return "perceived loudness matches"
    if diff > 2.0:
        return f"louder by {diff:+.1f} dB(A) — noticeable"
    if diff > 0.5:
        return f"slightly louder ({diff:+.1f} dB(A))"
    if diff < -2.0:
        return f"quieter by {diff:.1f} dB(A) — noticeable"
    return f"slightly quieter ({diff:+.1f} dB(A))"


def describe_crest(lex, dv):
    diff = dv - lex
    if abs(diff) < 1.5:
        return "timbre smoothness matches"
    if diff > 3.0:
        return f"piercing/peaky spectrum (+{diff:.1f} dB crest)"
    if diff > 1.5:
        return f"more peaky timbre (+{diff:.1f} dB crest)"
    if diff < -3.0:
        return f"unusually smooth/dull ({diff:.1f} dB crest)"
    return f"slightly smoother ({diff:.1f} dB crest)"


def describe_d50(lex, dv):
    diff = dv - lex
    if abs(diff) < 1.0:
        return "transient definition matches"
    if diff > 2.0:
        return f"more defined / dry transients ({diff:+.1f} dB D50)"
    if diff > 1.0:
        return f"slightly more transient definition ({diff:+.1f} dB D50)"
    if diff < -2.0:
        return f"notably less defined / smeared transients ({diff:+.1f} dB D50)"
    return f"slightly less transient definition ({diff:+.1f} dB D50)"


def describe_stereo(lex, dv):
    diff = dv - lex
    if abs(diff) < 0.10:
        return "stereo width matches"
    if diff > 0.25:
        return f"notably narrower / mono-leaning ({diff:+.2f} corr)"
    if diff > 0.10:
        return f"slightly narrower ({diff:+.2f} corr)"
    if diff < -0.25:
        return f"notably wider / more decorrelated ({diff:+.2f} corr)"
    return f"slightly wider ({diff:+.2f} corr)"


def describe_td_crest(lex, dv):
    diff = dv - lex
    if abs(diff) < 2.0:
        return "envelope impulsiveness matches"
    if diff > 4.0:
        return f"more impulsive / snappy ({diff:+.1f} dB TD-crest)"
    if diff > 2.0:
        return f"slightly snappier ({diff:+.1f} dB TD-crest)"
    if diff < -4.0:
        return f"notably washier / less impulsive ({diff:+.1f} dB TD-crest)"
    return f"slightly washier ({diff:+.1f} dB TD-crest)"


# ─── Main ─────────────────────────────────────────────────────────────────

def main():
    args = sys.argv[1:]
    if len(args) >= 4:
        lex_ir_path, dv_ir_path, lex_nb_path, dv_nb_path = args[:4]
    elif len(args) == 2:
        lex_ir_path, dv_ir_path = args
        lex_nb_path = lex_ir_path.replace('_impulse', '_noiseburst')
        dv_nb_path = dv_ir_path.replace('_impulse', '_noiseburst')
    else:
        lex_ir_path = '/tmp/lex_compare/lex_v3_impulse.wav'
        dv_ir_path = '/tmp/dv_tune_final_impulse.wav'
        lex_nb_path = '/tmp/lex_compare/lex_v3_noiseburst.wav'
        dv_nb_path = '/tmp/dv_tune_final_noiseburst.wav'

    print(f"Lex IR : {lex_ir_path}")
    print(f"DV  IR : {dv_ir_path}")
    print(f"Lex NB : {lex_nb_path}")
    print(f"DV  NB : {dv_nb_path}")

    sr_l, lex_ir = load_mono(lex_ir_path)
    sr_d, dv_ir = load_mono(dv_ir_path)
    sr_lnb, lex_nb = load_mono(lex_nb_path)
    sr_dnb, dv_nb = load_mono(dv_nb_path)

    assert sr_l == sr_d == sr_lnb == sr_dnb, "sample rate mismatch"
    sr = sr_l

    # Trim leading silence so C80 is anchored on the IR peak rather than
    # the pre-delay block (DV pre-delay shifts peak by 20 ms).
    lex_peak = int(np.argmax(np.abs(lex_ir)))
    dv_peak = int(np.argmax(np.abs(dv_ir)))
    lex_ir_t = lex_ir[lex_peak:]
    dv_ir_t = dv_ir[dv_peak:]

    lex_c80 = c80(lex_ir_t, sr)
    dv_c80 = c80(dv_ir_t, sr)

    lex_aw = a_weighted_rms_db(lex_nb, sr)
    dv_aw = a_weighted_rms_db(dv_nb, sr)

    lex_br = bass_ratio(lex_ir_t, sr)
    dv_br = bass_ratio(dv_ir_t, sr)
    lex_tr = treble_ratio(lex_ir_t, sr)
    dv_tr = treble_ratio(dv_ir_t, sr)

    # Spectral crest on the noise-burst tail (after first 100 ms)
    lex_crest = spectral_crest_db(lex_nb[int(0.1 * sr):], sr)
    dv_crest = spectral_crest_db(dv_nb[int(0.1 * sr):], sr)

    # D50 (50 ms clarity), stereo correlation, time-domain crest
    lex_d50 = d50(lex_ir_t, sr)
    dv_d50 = d50(dv_ir_t, sr)
    lex_stereo = stereo_correlation(lex_nb_path)
    dv_stereo  = stereo_correlation(dv_nb_path)
    lex_tdc = time_domain_crest(lex_nb, sr)
    dv_tdc  = time_domain_crest(dv_nb, sr)
    # K-weighted LUFS (perceived loudness on music-like content)
    lex_lufs = k_weighted_lufs(lex_nb, sr)
    dv_lufs  = k_weighted_lufs(dv_nb, sr)
    # momentary LUFS max (peak loudness over any 400 ms window)
    lex_mlufs = momentary_lufs_max(lex_nb, sr)
    dv_mlufs  = momentary_lufs_max(dv_nb,  sr)
    # boxiness (200-500 Hz peak vs flanks)
    lex_box = box_ratio_db(lex_nb, sr)
    dv_box  = box_ratio_db(dv_nb, sr)

    print()
    print("=" * 72)
    print(f"{'metric':<28s}  {'Lex':>10s}  {'DV':>10s}  {'diff':>10s}")
    print("-" * 72)
    print(f"{'C80 clarity (dB)':<28s}  {lex_c80:>+10.2f}  {dv_c80:>+10.2f}  {dv_c80-lex_c80:>+10.2f}")
    print(f"{'D50 definition (dB)':<28s}  {lex_d50:>+10.2f}  {dv_d50:>+10.2f}  {dv_d50-lex_d50:>+10.2f}")
    print(f"{'A-weighted RMS (dBA)':<28s}  {lex_aw:>+10.2f}  {dv_aw:>+10.2f}  {dv_aw-lex_aw:>+10.2f}")
    print(f"{'K-weighted LUFS (BS.1770)':<28s}  {lex_lufs:>+10.2f}  {dv_lufs:>+10.2f}  {dv_lufs-lex_lufs:>+10.2f}")
    print(f"{'Momentary LUFS (400ms max)':<28s}  {lex_mlufs:>+10.2f}  {dv_mlufs:>+10.2f}  {dv_mlufs-lex_mlufs:>+10.2f}")
    print(f"{'Stereo correlation (tail)':<28s}  {lex_stereo:>+10.3f}  {dv_stereo:>+10.3f}  {dv_stereo-lex_stereo:>+10.3f}")
    print(f"{'Time-domain crest (env dB)':<28s}  {lex_tdc:>+10.2f}  {dv_tdc:>+10.2f}  {dv_tdc-lex_tdc:>+10.2f}")
    print(f"{'Boxiness (200-500Hz peak)':<28s}  {lex_box:>+10.2f}  {dv_box:>+10.2f}  {dv_box-lex_box:>+10.2f}")
    print(f"{'Bass Ratio (BR)':<28s}  {lex_br:>+10.3f}  {dv_br:>+10.3f}  {dv_br-lex_br:>+10.3f}")
    print(f"{'Treble Ratio (TR)':<28s}  {lex_tr:>+10.3f}  {dv_tr:>+10.3f}  {dv_tr-lex_tr:>+10.3f}")
    print(f"{'Spectral Crest (dB)':<28s}  {lex_crest:>+10.2f}  {dv_crest:>+10.2f}  {dv_crest-lex_crest:>+10.2f}")
    print("=" * 72)

    # Per-octave absolute-dB shape audit with EARLY (100-500 ms) and LATE
    # (500 ms - 2 s) windows. The full-window measurement averaged out
    # mid-bass boom that's audible only in the first half-second of the
    # tail. Split-window exposes "boom" that hides in long-tail RMS.
    # NOTE: dropped the equal-loudness scaling column — ISO 226 contour
    # was suppressing bass diffs that the ear actually hears as boom
    # in busy material. Raw dB is the honest metric for bass-buildup
    # perception (loudness summation in complex mixes ≠ threshold tones).
    print()
    print("─── Per-octave shape — EARLY 100-500ms vs LATE 500ms-2s ──")
    print(f"  {'band':>7s}  {'Lex_E':>7s}  {'DV_E':>7s}  {'E-Δ':>6s}   "
          f"{'Lex_L':>7s}  {'DV_L':>7s}  {'L-Δ':>6s}")
    band_diffs_early = {}
    band_diffs_late = {}
    for fc in [63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]:
        le = octave_band_db(lex_nb, sr, fc, t_start=0.1, t_end=0.5)
        de = octave_band_db(dv_nb,  sr, fc, t_start=0.1, t_end=0.5)
        ll = octave_band_db(lex_nb, sr, fc, t_start=0.5, t_end=2.0)
        dl = octave_band_db(dv_nb,  sr, fc, t_start=0.5, t_end=2.0)
        diff_e = de - le
        diff_l = dl - ll
        band_diffs_early[fc] = diff_e
        band_diffs_late[fc]  = diff_l
        flag = ''
        if abs(diff_e) > 3.0:
            flag = '  ←boom' if diff_e > 0 else '  ←thin'
        elif abs(diff_l) > 3.0:
            flag = '  ←late' + ('hi' if diff_l > 0 else 'low')
        print(f"  {fc:>5d}Hz  {le:>+7.1f}  {de:>+7.1f}  {diff_e:>+6.1f}   "
              f"{ll:>+7.1f}  {dl:>+7.1f}  {diff_l:>+6.1f}{flag}")
    worst_early = max(band_diffs_early, key=lambda b: abs(band_diffs_early[b]))
    worst_late  = max(band_diffs_late,  key=lambda b: abs(band_diffs_late[b]))
    print(f"  worst EARLY octave: {worst_early} Hz, Δ={band_diffs_early[worst_early]:+.1f} dB  "
          f"|  worst LATE: {worst_late} Hz, Δ={band_diffs_late[worst_late]:+.1f} dB")
    # Compatibility for downstream code that referenced band_diffs / band_perceived.
    band_diffs = band_diffs_late.copy()
    band_perceived = band_diffs_late.copy()

    print()
    print("─── Decade-band shape (broadband shape audit) ──")
    print(f"  {'band':>10s}  {'Lex':>8s}  {'DV':>8s}  {'diff':>8s}")
    for lo, hi, name in [(20, 100, '<100'), (100, 500, '100-500'),
                          (500, 2000, '500-2k'), (2000, 5000, '2k-5k'),
                          (5000, 12000, '5k-12k'), (12000, 24000, '>12k')]:
        lex_d = decade_db(lex_nb, sr, lo, hi)
        dv_d  = decade_db(dv_nb, sr, lo, hi)
        flag = '' if abs(dv_d - lex_d) <= 3.0 else '  ←' + ('low' if dv_d - lex_d < 0 else 'hi')
        print(f"  {name:>10s}  {lex_d:>+8.1f}  {dv_d:>+8.1f}  {dv_d-lex_d:>+8.1f}{flag}")

    # Per-octave C80 — single broadband C80 averages across frequency.
    print()
    print("─── Per-octave C80 (clarity per band, dB) ──")
    print(f"  {'band':>7s}  {'Lex':>8s}  {'DV':>8s}  {'diff':>8s}")
    for fc in [125, 250, 500, 1000, 2000, 4000, 8000]:
        l_c = c80_per_octave(lex_ir, sr, fc)
        d_c = c80_per_octave(dv_ir,  sr, fc)
        flag = '' if abs(d_c - l_c) <= 2.0 else '  ←'
        print(f"  {fc:>5d}Hz  {l_c:>+8.1f}  {d_c:>+8.1f}  {d_c-l_c:>+8.1f}{flag}")

    # EDT (perceived RT60) vs T20 (measured RT60), per octave.
    print()
    print("─── EDT vs T20 (perceived vs measured RT60, seconds) ──")
    print(f"  {'band':>7s}  {'Lex_EDT':>8s}  {'Lex_T20':>8s}  {'DV_EDT':>8s}  {'DV_T20':>8s}  {'EDT_diff':>10s}")
    for fc in [125, 250, 500, 1000, 2000, 4000]:
        le = edt(lex_ir_t, sr, fc)
        lt = rt60_t20(lex_ir_t, sr, fc)
        de = edt(dv_ir_t, sr, fc)
        dt = rt60_t20(dv_ir_t, sr, fc)
        flag = '' if abs(de - le) <= max(le * 0.10, 0.05) else '  ←'
        print(f"  {fc:>5d}Hz  {le:>7.2f}s  {lt:>7.2f}s  {de:>7.2f}s  {dt:>7.2f}s  {de-le:>+9.2f}s{flag}")

    # ── METALLIC vs REALISTIC perceptual axis ──
    # Spectral flatness (Wiener entropy): geometric/arithmetic mean of
    # FFT bins. 1.0 = noise (realistic plate tail), 0.0 = pure tone
    # (metallic ring). Per-octave on the noise-burst tail. Catches the
    # axis that spectral_crest misses: crest spots single-bin peaks,
    # flatness measures the whole spectral distribution.
    print()
    print("─── Spectral flatness per-octave (0=tonal/metallic, 1=noisy/realistic) ──")
    print(f"  {'band':>7s}  {'Lex':>8s}  {'DV':>8s}  {'diff':>8s}")
    flatness_diffs = {}
    for fc in [125, 250, 500, 1000, 2000, 4000, 8000]:
        lf = spectral_flatness_octave(lex_nb, sr, fc, t_start=0.1, t_end=2.0)
        df = spectral_flatness_octave(dv_nb,  sr, fc, t_start=0.1, t_end=2.0)
        flatness_diffs[fc] = df - lf
        flag = '' if abs(df - lf) <= 0.08 else ('  ←tonal' if df < lf else '  ←noisy')
        print(f"  {fc:>5d}Hz  {lf:>8.3f}  {df:>8.3f}  {df-lf:>+8.3f}{flag}")
    worst_flat = max(flatness_diffs, key=lambda b: abs(flatness_diffs[b]))
    print(f"  worst flatness gap: {worst_flat} Hz, Δ={flatness_diffs[worst_flat]:+.3f}  "
          f"({'DV more tonal/metallic' if flatness_diffs[worst_flat] < 0 else 'DV more noisy/realistic'})")

    # Comb-teeth detector — cepstrum-like autocorrelation on the log-mag
    # spectrum. Strong peak = periodic spectral spacing = audible comb.
    # The 31 ms sparse-tap comb shows as ~32 Hz spectral spacing → peak
    # at lag corresponding to 31 ms period.
    print()
    print("─── Comb periodicity (autocorr of log-spectrum) ──")
    lex_comb_db, lex_comb_ms = comb_periodicity_db(lex_nb, sr,
                                                    t_start=0.1, t_end=2.0)
    dv_comb_db,  dv_comb_ms  = comb_periodicity_db(dv_nb,  sr,
                                                    t_start=0.1, t_end=2.0)
    print(f"  {'side':<8s}  {'prom_dB':>9s}  {'period_ms':>10s}")
    print(f"  {'Lex':<8s}  {lex_comb_db:>+9.2f}  {lex_comb_ms:>10.2f}")
    print(f"  {'DV':<8s}  {dv_comb_db:>+9.2f}  {dv_comb_ms:>10.2f}")
    comb_diff = dv_comb_db - lex_comb_db
    flag = ''
    if comb_diff > 2.0:
        flag = '  ←DV has audible comb-tooth ringing not present in Lex'
    elif comb_diff < -2.0:
        flag = '  ←Lex has comb-ringing DV lacks'
    print(f"  diff: {comb_diff:+.2f} dB{flag}")

    # Abel-Huang echo density η(t). η→1.0 = noise-like (Schroeder regime,
    # realistic plate); η<<1.0 = sparse impulses (metallic ringing).
    # Measured on the IMPULSE response (not noise-burst) so we see the
    # natural echo accumulation of the reverb's own modal structure.
    # Times sampled: 25, 50, 100, 200, 400 ms after peak.
    print()
    print("─── Echo density η(t) (1.0 = noise-cloud / 0.0 = sparse modes) ──")
    print(f"  {'t (ms)':>8s}  {'Lex':>8s}  {'DV':>8s}  {'diff':>8s}")
    for t_ms in [25, 50, 100, 200, 400]:
        le = echo_density_at_time(lex_ir_t, sr, t_ms * 0.001)
        de = echo_density_at_time(dv_ir_t,  sr, t_ms * 0.001)
        flag = ''
        if le > 0.85 and de < 0.70:
            flag = '  ←Lex hit Schroeder, DV still sparse'
        elif abs(de - le) > 0.15:
            flag = '  ←gap'
        print(f"  {t_ms:>7d}  {le:>8.3f}  {de:>8.3f}  {de-le:>+8.3f}{flag}")

    # Spectral centroid drift — does the tail's color change with time
    # (real plate modal decay = drift) or stay frozen (FDN uniform damp)?
    print()
    print("─── Centroid drift per band (Hz from 50-250 ms → 750-1750 ms) ──")
    print(f"  {'band':>7s}  {'Lex_drift':>10s}  {'DV_drift':>9s}  {'diff':>8s}")
    for fc in [250, 500, 1000, 2000]:
        ld = centroid_drift_db(lex_ir_t, sr, fc)
        dd = centroid_drift_db(dv_ir_t,  sr, fc)
        print(f"  {fc:>5d}Hz  {ld:>+10.1f}  {dd:>+9.1f}  {dd-ld:>+8.1f}")

    # Per-band stereo correlation — broadband stereo_correlation hides
    # the case where one band is decorrelated while another stays mono.
    # Lex plate physics decorrelates highs (mechanical mode dispersion);
    # DV's FDN may stay correlated across frequency.
    print()
    print("─── Per-octave stereo correlation (1=mono, 0=decorrelated) ──")
    print(f"  {'band':>7s}  {'Lex':>8s}  {'DV':>8s}  {'diff':>8s}")
    corr_diffs = {}
    for fc in [125, 500, 1000, 2000, 4000, 8000]:
        lc = stereo_correlation_octave(lex_nb_path, fc, t_start=0.1, t_end=2.0)
        dc = stereo_correlation_octave(dv_nb_path,  fc, t_start=0.1, t_end=2.0)
        corr_diffs[fc] = dc - lc
        flag = '' if abs(dc - lc) <= 0.15 else ('  ←DV mono-er' if dc > lc else '  ←DV wider')
        print(f"  {fc:>5d}Hz  {lc:>+8.3f}  {dc:>+8.3f}  {dc-lc:>+8.3f}{flag}")
    worst_corr = max(corr_diffs, key=lambda b: abs(corr_diffs[b]))
    print(f"  worst per-band corr gap: {worst_corr} Hz, Δ={corr_diffs[worst_corr]:+.3f}")

    # Snare-burst panel — same metrics on the snare-rendered WAVs. Catches
    # things noise burst hides (boxiness on a transient, perceived loudness
    # on a real hit). Renderer writes lex_*_snare.wav and dv_*_snare.wav
    # automatically alongside _impulse and _noiseburst.
    lex_sn_path = lex_nb_path.replace('_noiseburst', '_snare')
    dv_sn_path  = dv_nb_path.replace('_noiseburst', '_snare')
    if os.path.exists(lex_sn_path) and os.path.exists(dv_sn_path):
        sr_ls, lex_sn = load_mono(lex_sn_path)
        sr_ds, dv_sn  = load_mono(dv_sn_path)
        if sr_ls == sr_ds == sr:
            print()
            print("─── Snare-burst (real-material check) ──")
            ls_aw = a_weighted_rms_db(lex_sn, sr)
            ds_aw = a_weighted_rms_db(dv_sn,  sr)
            ls_lu = k_weighted_lufs(lex_sn, sr)
            ds_lu = k_weighted_lufs(dv_sn,  sr)
            ls_bx = box_ratio_db(lex_sn, sr, t_start=0.0)
            ds_bx = box_ratio_db(dv_sn,  sr, t_start=0.0)
            ls_tdc = time_domain_crest(lex_sn, sr, t_start=0.0)
            ds_tdc = time_domain_crest(dv_sn,  sr, t_start=0.0)
            print(f"  {'metric':<22s}  {'Lex':>9s}  {'DV':>9s}  {'diff':>8s}")
            print(f"  {'A-weighted (dBA)':<22s}  {ls_aw:>+9.2f}  {ds_aw:>+9.2f}  {ds_aw-ls_aw:>+8.2f}")
            print(f"  {'K-weighted (LUFS)':<22s}  {ls_lu:>+9.2f}  {ds_lu:>+9.2f}  {ds_lu-ls_lu:>+8.2f}")
            print(f"  {'Boxiness (200-500)':<22s}  {ls_bx:>+9.2f}  {ds_bx:>+9.2f}  {ds_bx-ls_bx:>+8.2f}")
            print(f"  {'TD-crest (env dB)':<22s}  {ls_tdc:>+9.2f}  {ds_tdc:>+9.2f}  {ds_tdc-ls_tdc:>+8.2f}")
        else:
            print(f"  (snare files sample-rate mismatch; skipping)")
    else:
        print(f"  (snare files not present; skipping snare panel)")

    # Sustained-pink panel — 12 s continuous pink noise; measure on the
    # last 4 s (after ~8 s of settling) to expose comb-feedback / FDN
    # modal-density steady-state buildup that the 100 ms noiseburst can't
    # show. This is the metric the user's ear has been hearing as "DV
    # too loud / too woofy" while noiseburst LUFS reported parity.
    sustained_lufs_diff = 0.0
    sustained_octave_worst = 0.0
    lex_su_path = lex_nb_path.replace('_noiseburst', '_sustained')
    dv_su_path  = dv_nb_path.replace('_noiseburst', '_sustained')
    if os.path.exists(lex_su_path) and os.path.exists(dv_su_path):
        sr_lu, lex_su = load_mono(lex_su_path)
        sr_du, dv_su  = load_mono(dv_su_path)
        if sr_lu == sr_du == sr:
            win_start, win_end = 8.0, 12.0
            s0 = int(win_start * sr)
            s1 = int(win_end   * sr)
            lex_su_w = lex_su[s0:s1]
            dv_su_w  = dv_su[s0:s1]
            print()
            print("─── Sustained-pink steady-state (last 4 s of 12 s pink) ──")
            ls_aw  = a_weighted_rms_db(lex_su_w, sr)
            ds_aw  = a_weighted_rms_db(dv_su_w,  sr)
            ls_lu  = k_weighted_lufs(lex_su_w, sr)
            ds_lu  = k_weighted_lufs(dv_su_w,  sr)
            ls_bx  = box_ratio_db(lex_su_w, sr, t_start=0.0)
            ds_bx  = box_ratio_db(dv_su_w,  sr, t_start=0.0)
            print(f"  {'metric':<22s}  {'Lex':>9s}  {'DV':>9s}  {'diff':>8s}")
            print(f"  {'A-weighted (dBA)':<22s}  {ls_aw:>+9.2f}  {ds_aw:>+9.2f}  {ds_aw-ls_aw:>+8.2f}")
            print(f"  {'K-weighted (LUFS)':<22s}  {ls_lu:>+9.2f}  {ds_lu:>+9.2f}  {ds_lu-ls_lu:>+8.2f}")
            print(f"  {'Boxiness (200-500)':<22s}  {ls_bx:>+9.2f}  {ds_bx:>+9.2f}  {ds_bx-ls_bx:>+8.2f}")
            print()
            print("  Per-octave (steady-state, raw dB):")
            print(f"  {'band':>7s}  {'Lex':>8s}  {'DV':>8s}  {'diff':>8s}")
            su_band_diffs = {}
            for fc in [63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]:
                l = octave_band_db(lex_su, sr, fc, t_start=win_start, t_end=win_end)
                d = octave_band_db(dv_su,  sr, fc, t_start=win_start, t_end=win_end)
                su_band_diffs[fc] = d - l
                flag = '' if abs(d - l) <= 3.0 else ('  ←hi' if d - l > 0 else '  ←low')
                print(f"  {fc:>5d}Hz  {l:>+8.1f}  {d:>+8.1f}  {d-l:>+8.1f}{flag}")
            worst_su = max(su_band_diffs, key=lambda b: abs(su_band_diffs[b]))
            print(f"  worst sustained octave: {worst_su} Hz, Δ={su_band_diffs[worst_su]:+.1f} dB")
            sustained_lufs_diff = ds_lu - ls_lu
            sustained_octave_worst = su_band_diffs[worst_su]
        else:
            print(f"  (sustained files sample-rate mismatch; skipping)")
    else:
        print(f"  (sustained files not present; skipping sustained panel)")

    print()
    print("─── Sonic Translation (DV vs Lex) ──")
    parts = [
        describe_loudness(lex_aw, dv_aw),
        describe_c80(lex_c80, dv_c80),
        describe_d50(lex_d50, dv_d50),
        describe_br(lex_br, dv_br),
        describe_tr(lex_tr, dv_tr),
        describe_crest(lex_crest, dv_crest),
        describe_stereo(lex_stereo, dv_stereo),
        describe_td_crest(lex_tdc, dv_tdc),
    ]
    # Boxiness verdict
    box_diff = dv_box - lex_box
    if abs(box_diff) > 2.0:
        parts.append(f"{'boxy / 250-400 Hz hump' if box_diff > 0 else 'thin / scooped 250-400'} ({box_diff:+.1f} dB)")
    # LUFS — average AND momentary peak
    lufs_diff = dv_lufs - lex_lufs
    if abs(lufs_diff) > 1.0:
        parts.append(f"avg-loudness {lufs_diff:+.1f} LU ({'louder' if lufs_diff > 0 else 'quieter'})")
    mlufs_diff = dv_mlufs - lex_mlufs
    if abs(mlufs_diff) > 1.5:
        parts.append(f"peak-loudness {mlufs_diff:+.1f} LU at 400 ms ({'louder peak' if mlufs_diff > 0 else 'quieter peak'})")
    # Early-window boom callout
    worst_e_band = max(band_diffs_early, key=lambda b: abs(band_diffs_early[b]))
    worst_e_diff = band_diffs_early[worst_e_band]
    if abs(worst_e_diff) > 3.0:
        parts.append(
            f"{'boom' if worst_e_diff > 0 else 'thin'} @ {worst_e_band} Hz "
            f"in first 500 ms ({worst_e_diff:+.1f} dB)")
    # Sustained steady-state callouts — separate from noise-burst LUFS
    # because comb-feedback buildup takes seconds to manifest.
    if abs(sustained_lufs_diff) > 1.0:
        parts.append(
            f"steady-state {'louder' if sustained_lufs_diff > 0 else 'quieter'} "
            f"({sustained_lufs_diff:+.1f} LU sustained)")
    if abs(sustained_octave_worst) > 3.0:
        parts.append(
            f"sustained {'buildup' if sustained_octave_worst > 0 else 'deficit'} "
            f"({sustained_octave_worst:+.1f} dB worst octave on continuous pink)")

    print("DuskVerb " + "; ".join(parts) + ".")

    # Highlight the dominant difference
    differences = {
        'loudness'  : (abs(dv_aw - lex_aw), 'loudness (A-weighted)'),
        'lufs'      : (abs(dv_lufs - lex_lufs) / 1.0, 'loudness on music (LUFS)'),
        'clarity'   : (abs(dv_c80 - lex_c80) / 3.0, 'front/back balance (C80)'),
        'd50'       : (abs(dv_d50 - lex_d50) / 2.0, 'transient definition (D50)'),
        'warmth'    : (abs(dv_br - lex_br) / 0.2, 'low-end warmth (BR)'),
        'brightness': (abs(dv_tr - lex_tr) / 0.2, 'high-end sustain (TR)'),
        'timbre'    : (abs(dv_crest - lex_crest) / 3.0, 'timbre peakiness (crest)'),
        'stereo'    : (abs(dv_stereo - lex_stereo) / 0.20, 'stereo width'),
        'td_crest'  : (abs(dv_tdc - lex_tdc) / 4.0, 'envelope impulsiveness (TD-crest)'),
        'boxiness'  : (abs(dv_box - lex_box) / 2.0, 'boxiness (200-500 Hz hump)'),
        'momentary' : (abs(dv_mlufs - lex_mlufs) / 1.5, 'peak loudness moment (400 ms LUFS)'),
        'octave_late': (max(abs(v) for v in band_diffs_late.values()) / 3.0, 'late-tail octave shape (raw dB)'),
        'octave_early': (max(abs(v) for v in band_diffs_early.values()) / 3.0, 'early-tail octave shape (boom/thin zone)'),
        'sustained_lufs':    (abs(sustained_lufs_diff) / 1.0, 'steady-state loudness (sustained pink)'),
        'sustained_octave':  (abs(sustained_octave_worst) / 3.0, 'steady-state octave shape (continuous-pink buildup)'),
    }
    worst = max(differences.values(), key=lambda kv: kv[0])
    if worst[0] > 1.0:
        print(f"\nDominant audible difference: {worst[1]}.")
    else:
        print("\nAll perceptual metrics within typical listening tolerance.")


if __name__ == '__main__':
    main()
