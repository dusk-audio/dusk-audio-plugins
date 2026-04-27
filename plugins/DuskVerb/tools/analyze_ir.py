#!/usr/bin/env python3
"""
DuskVerb IR analyzer — extracts per-octave RT60 + EQ values from an impulse
response WAV and emits a copy-paste-ready C++ constants block.

Output bands match the plugin's EightBandDamping / EightBandPeakingEQ:

    Band  Hz range       Center (geometric)
    0     63 –  125      89
    1    125 –  250      177
    2    250 –  500      354
    3    500 – 1000      707
    4   1000 – 2000      1414
    5   2000 – 4000      2828
    6   4000 – 8000      5657
    7   8000 – 16000     11314

The plugin maps a per-band RT60[s] directly to feedback gain via
    g[n] = exp(-3*ln(10) * loopLength / (sr * RT60[n]))
and a per-band EQ[dB] directly to a 1-octave-Q peaking biquad at the band
center, so the values printed here can be pasted verbatim into the per-preset
.cpp `kBakedRt60` / `kBakedEq` arrays.

Usage:
    python3 analyze_ir.py path/to/ir.wav

Mono or stereo WAV; supports int16, int32, float32. RT60 is measured by
Schroeder backward integration (T20, extrapolated to T60). FR is reported in
dB relative to the 500-1k Hz mid band so the constants represent EQ
correction needed to match the IR's spectral balance.
"""

import argparse
import os
import sys

import numpy as np
from scipy import signal as sig
from scipy.io import wavfile


# ---------------------------------------------------------------------------
# Octave bin definitions — must stay 1:1 with EightBandDamping.kBandUpperHz
# and EightBandPeakingEQ.kCenterHz in plugins/DuskVerb/src/dsp/TwoBandDamping.h.
# ---------------------------------------------------------------------------
BAND_EDGES_HZ = [63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0]
BAND_LABELS   = ["63", "125", "250", "500", "1k", "2k", "4k", "8k"]
NUM_BANDS     = 8
MID_BAND      = 3   # 500–1000 Hz — reference band for relative EQ values


def load_ir(path):
    """Returns (sample_rate, samples) where samples is float32 mono [N] or stereo [N,2]."""
    sr, data = wavfile.read(path)
    if data.dtype == np.int16:
        data = data.astype(np.float32) / 32768.0
    elif data.dtype == np.int32:
        data = data.astype(np.float32) / 2147483648.0
    elif data.dtype == np.float64:
        data = data.astype(np.float32)
    elif data.dtype != np.float32:
        raise RuntimeError(f"Unsupported WAV sample format: {data.dtype}")
    return sr, data


def to_mono(data):
    if data.ndim == 1:
        return data
    return np.mean(data, axis=1)


def find_onset_ms(mono, sr, threshold_db=-40.0):
    """First sample that exceeds threshold relative to peak. ms from start."""
    peak = float(np.max(np.abs(mono)))
    if peak <= 0.0:
        return 0.0
    threshold = peak * (10.0 ** (threshold_db / 20.0))
    above = np.where(np.abs(mono) >= threshold)[0]
    if len(above) == 0:
        return 0.0
    return 1000.0 * above[0] / sr


def rms_dbfs(mono):
    rms = float(np.sqrt(np.mean(mono * mono)))
    if rms <= 1e-12:
        return -120.0
    return 20.0 * np.log10(rms)


def lr_correlation(stereo):
    if stereo.ndim != 2 or stereo.shape[1] < 2:
        return None
    L = stereo[:, 0].astype(np.float64)
    R = stereo[:, 1].astype(np.float64)
    if np.std(L) < 1e-12 or np.std(R) < 1e-12:
        return 0.0
    return float(np.corrcoef(L, R)[0, 1])


def octave_bandpass(low_hz, high_hz, sr, order=4):
    """Butterworth bandpass for one octave bin. Order 4 (8th-order effective)."""
    nyq = 0.5 * sr
    lo = max(low_hz / nyq, 0.001)
    hi = min(high_hz / nyq, 0.999)
    if hi <= lo:
        return None
    return sig.butter(order, [lo, hi], btype="band", output="sos")


def schroeder_rt60(band_signal, sr, fit_lo_db=-5.0, fit_hi_db=-25.0):
    """Schroeder backward integration → log curve → linear fit → RT60.

    Fit window defaults to T20 (-5 to -25 dB, extrapolated ×3) — robust against
    background noise floor in the tail. Returns RT60 seconds, or None if the
    curve never falls into the fit window.
    """
    energy = band_signal.astype(np.float64) ** 2
    # Backward cumulative sum = Schroeder integral
    edc = np.flip(np.cumsum(np.flip(energy)))
    if edc[0] <= 0.0:
        return None
    edc_db = 10.0 * np.log10(edc / edc[0] + 1e-30)

    # Find indices crossing fit_lo and fit_hi
    below_lo = np.where(edc_db <= fit_lo_db)[0]
    below_hi = np.where(edc_db <= fit_hi_db)[0]
    if len(below_lo) == 0 or len(below_hi) == 0:
        return None
    idx_lo = below_lo[0]
    idx_hi = below_hi[0]
    if idx_hi <= idx_lo + 4:
        return None

    t = np.arange(idx_lo, idx_hi) / sr
    y = edc_db[idx_lo:idx_hi]
    # Linear regression: dB per second
    slope, _ = np.polyfit(t, y, 1)
    if slope >= 0.0:
        return None
    return float(-60.0 / slope)


def per_band_rt60(mono, sr):
    """Returns 8 per-octave RT60s in seconds (None for bands that wouldn't fit)."""
    out = []
    for i in range(NUM_BANDS):
        sos = octave_bandpass(BAND_EDGES_HZ[i], BAND_EDGES_HZ[i + 1], sr)
        if sos is None:
            out.append(None)
            continue
        filtered = sig.sosfiltfilt(sos, mono)
        out.append(schroeder_rt60(filtered, sr))
    return out


def per_band_eq_absolute_db(mono, sr):
    """Per-band absolute dB (no mid-band subtraction), measured through the
    plugin-matched biquad filter bank. Same ruler as per_band_eq_db but
    returns absolute values so the caller can build transfer matrices
    without the mid-band coupling artefact."""
    from scipy import signal as sig

    bank = _build_measurement_bank(sr)
    rng = np.random.default_rng(0)
    white = rng.standard_normal(max(len(mono), sr // 2)).astype(np.float64)
    signal_rms = float(np.sqrt(np.mean(mono * mono) + 1e-30))
    white_rms  = float(np.sqrt(np.mean(white * white) + 1e-30))

    db_per_band = []
    for i in range(NUM_BANDS):
        sos = bank[i]
        band_sig   = sig.sosfilt(sos, mono)
        band_white = sig.sosfilt(sos, white)
        rms_sig   = float(np.sqrt(np.mean(band_sig   * band_sig)   + 1e-30))
        rms_white = float(np.sqrt(np.mean(band_white * band_white) + 1e-30))
        ratio = (rms_sig / signal_rms) / (rms_white / white_rms + 1e-30)
        db_per_band.append(20.0 * np.log10(ratio + 1e-30))
    return db_per_band


def _rbj_bpf_sos(fc, Q, sr):
    """RBJ 2nd-order bandpass (constant 0 dB peak gain) as a 1x6 SOS row."""
    w0 = 2.0 * np.pi * fc / sr
    cosw = np.cos(w0)
    sinw = np.sin(w0)
    alpha = sinw / (2.0 * Q)
    b0, b1, b2 =  alpha, 0.0, -alpha
    a0, a1, a2 =  1.0 + alpha, -2.0 * cosw, 1.0 - alpha
    return np.array([[b0/a0, b1/a0, b2/a0, 1.0, a1/a0, a2/a0]])


def _rbj_lpf_sos(fc, Q, sr):
    """RBJ 2nd-order lowpass as a 1x6 SOS row."""
    w0 = 2.0 * np.pi * fc / sr
    cosw = np.cos(w0)
    sinw = np.sin(w0)
    alpha = sinw / (2.0 * Q)
    b0 = (1.0 - cosw) * 0.5
    b1 =  1.0 - cosw
    b2 = (1.0 - cosw) * 0.5
    a0, a1, a2 = 1.0 + alpha, -2.0 * cosw, 1.0 - alpha
    return np.array([[b0/a0, b1/a0, b2/a0, 1.0, a1/a0, a2/a0]])


def _rbj_hpf_sos(fc, Q, sr):
    """RBJ 2nd-order highpass as a 1x6 SOS row."""
    w0 = 2.0 * np.pi * fc / sr
    cosw = np.cos(w0)
    sinw = np.sin(w0)
    alpha = sinw / (2.0 * Q)
    b0 =  (1.0 + cosw) * 0.5
    b1 = -(1.0 + cosw)
    b2 =  (1.0 + cosw) * 0.5
    a0, a1, a2 = 1.0 + alpha, -2.0 * cosw, 1.0 - alpha
    return np.array([[b0/a0, b1/a0, b2/a0, 1.0, a1/a0, a2/a0]])


# ---------------------------------------------------------------------------
# Measurement filter bank matched to the plugin's EightBandPeakingEQ topology.
# Same centers/corners and Q factors — so energy measured here is exactly the
# bandwidth the plugin's per-band EQ biquads act upon. No FFT-bin rectangles,
# no shape mismatch between ruler and tool.
#
# Band 0 (63-125 Hz, low shelf region): 2nd-order LPF at 125 Hz, Q = 0.707.
# Bands 1-6 (peaking region):           2nd-order BPF at geometric center, Q = 1.41.
# Band 7 (8-16 kHz, high shelf region): 2nd-order HPF at 8 kHz, Q = 0.707.
# ---------------------------------------------------------------------------
PLUGIN_EQ_CENTER_HZ = [125.0, 177.0, 354.0, 707.0, 1414.0, 2828.0, 5657.0, 8000.0]
PLUGIN_EQ_PEAK_Q    = 1.41    # peaking biquads (bands 1-6)
PLUGIN_EQ_SHELF_Q   = 0.707   # shelves (bands 0 low, 7 high)


def _build_measurement_bank(sr):
    """Measurement isolation filters matching the plugin's EQ topology:
      Band 0: LPF at 125 Hz (matches plugin's low shelf region)
      Bands 1-6: BPF at geometric center, Q=1.41 (matches peaking biquads)
      Band 7: HPF at 8 kHz (matches plugin's high shelf — 12-20 kHz stays
              in the shelf asymptote, not a peaking bell's downward skirt)"""
    bank = []
    for i in range(NUM_BANDS):
        fc = PLUGIN_EQ_CENTER_HZ[i]
        if i == 0:
            bank.append(_rbj_lpf_sos(fc, PLUGIN_EQ_SHELF_Q, sr))
        elif i == NUM_BANDS - 1:
            bank.append(_rbj_hpf_sos(fc, PLUGIN_EQ_SHELF_Q, sr))
        else:
            bank.append(_rbj_bpf_sos(fc, PLUGIN_EQ_PEAK_Q, sr))
    return bank


def per_band_eq_db(mono, sr):
    """Per-band EQ in dB rel 500-1k mid, measured via the SAME filter topology
    the plugin's EightBandPeakingEQ uses — LPF for band 0, BPF Q=1.41 for
    bands 1-6, HPF for band 7.

    This replaces the previous FFT-bin-rectangular-average method. The old
    method measured "average linear power in a rectangular octave" which
    does not correspond to what a biquad EQ filter actually controls
    (a biquad has a smooth bell/shelf response, not a rectangle).

    Using the plugin's filter topology as the measurement ruler means:
      target_value = measurement-filter RMS(IR) → rel mid
      applied_value = kBakedEq[i] pushed into plugin's EQ biquad at band i
      output measured through same bank → equals target (by construction).
    """
    from scipy import signal as sig  # scoped so unit tests without scipy still import

    bank = _build_measurement_bank(sr)
    # RMS of each filter's output. We normalise by each filter's RMS of
    # white noise at the same length so a flat-spectrum signal reads 0 dB
    # across all bands — otherwise narrower filters (BPFs) read quieter
    # than wider ones (LPF/HPF) simply because they pass less energy.
    rng = np.random.default_rng(0)
    white = rng.standard_normal(max(len(mono), sr // 2)).astype(np.float64)

    db_per_band = []
    for i in range(NUM_BANDS):
        sos = bank[i]
        band_sig   = sig.sosfilt(sos, mono)
        band_white = sig.sosfilt(sos, white)
        rms_sig   = float(np.sqrt(np.mean(band_sig   * band_sig)   + 1e-30))
        rms_white = float(np.sqrt(np.mean(band_white * band_white) + 1e-30))
        signal_rms = float(np.sqrt(np.mean(mono * mono) + 1e-30))
        white_rms  = float(np.sqrt(np.mean(white * white) + 1e-30))
        # Ratio: how much energy this band captures from the signal, relative
        # to how much it would capture from flat spectrum at the same level.
        ratio = (rms_sig / signal_rms) / (rms_white / white_rms + 1e-30)
        db_per_band.append(20.0 * np.log10(ratio + 1e-30))
    mid = db_per_band[MID_BAND]
    return [d - mid for d in db_per_band]


def fmt_band_row(values, fmt):
    parts = []
    for v in values:
        if v is None:
            parts.append("   --")
        else:
            parts.append(fmt.format(v))
    return " ".join(parts)


def fmt_constants_array(values, suffix="f"):
    """{ 1.74f, 1.96f, 1.92f, 2.00f, 1.85f, 1.38f, 1.15f, 0.73f }"""
    items = []
    for v in values:
        if v is None:
            items.append(f"2.0{suffix}")  # neutral fallback
        else:
            items.append(f"{v:.2f}{suffix}")
    return "{ " + ", ".join(items) + " }"


def early_reflection_peaks(mono, sr, num_peaks=8, window_ms=100):
    """Returns the top N peak (time_ms, level_dB) pairs in the first window_ms.
    Uses local-maxima detection on the rectified envelope smoothed over 1 ms.
    """
    win = int(window_ms / 1000 * sr)
    seg = np.abs(mono[:win])
    smooth_n = max(1, int(0.001 * sr))
    if smooth_n > 1:
        kern = np.ones(smooth_n) / smooth_n
        seg = np.convolve(seg, kern, mode="same")
    peaks = []
    for i in range(1, len(seg) - 1):
        if seg[i] > seg[i - 1] and seg[i] >= seg[i + 1] and seg[i] > 1e-6:
            peaks.append((i / sr * 1000.0, 20.0 * np.log10(seg[i] + 1e-12)))
    peaks.sort(key=lambda p: p[1], reverse=True)
    return peaks[:num_peaks]


# ---------------------------------------------------------------------------
# FORENSIC methods — bypass broad-octave averaging / level-dependent threshold
# blocks that were hiding real DSP issues (wide HPF craters, tank blooms that
# miss the first ER slap, seasick-mod phase wobble).
# ---------------------------------------------------------------------------

def forensic_onset_idx(wet_mono, threshold_dbfs=-60.0):
    """First wet sample exceeding an ABSOLUTE -60 dBFS threshold.

    Unlike find_onset_ms (which normalises to the wet peak so quiet IRs report
    artificially-early onsets), this uses a fixed full-scale threshold so the
    reading survives level changes. Returns sample index or None if silent.
    """
    thresh = 10.0 ** (threshold_dbfs / 20.0)
    above  = np.where(np.abs(wet_mono) >= thresh)[0]
    return int(above[0]) if len(above) else None


def forensic_true_predelay_ms(wet_mono, dry_mono, sr):
    """Precise pre-delay: offset from dry transient peak to wet -60 dBFS onset.
    Requires a reference dry impulse (e.g. the input that was fed to the plugin).
    Returns None if wet is silent."""
    dry_peak_idx = int(np.argmax(np.abs(dry_mono)))
    wet_idx      = forensic_onset_idx(wet_mono)
    if wet_idx is None:
        return None
    return 1000.0 * (wet_idx - dry_peak_idx) / sr


def forensic_er_peaks(wet_mono, sr, onset_idx=0, window_ms=100,
                      smooth_ms=0.5, min_sep_ms=1.0, num_peaks=8):
    """Top N envelope peaks in the first `window_ms` post-onset.

    Differences vs early_reflection_peaks():
      - Tight 0.5 ms smoothing instead of 1 ms — preserves the t<1 ms "slap"
        that a 1 ms kernel would merge into later samples.
      - 1 ms minimum separation via scipy.signal.find_peaks so we don't pick
        up 10 adjacent samples of one reflection as 10 "taps".
      - Returned list is TIME-SORTED (not amplitude-sorted) so the reader
        sees the tap PATTERN directly.

    Returns [(time_ms_from_onset, envelope_dBFS)] of length up to num_peaks.
    """
    from scipy.signal import find_peaks
    end = min(onset_idx + int(window_ms / 1000.0 * sr), len(wet_mono))
    seg = wet_mono[onset_idx:end]
    smooth_n = max(1, int(smooth_ms / 1000.0 * sr))
    env = np.convolve(np.abs(seg), np.ones(smooth_n) / smooth_n, mode="same")
    min_dist = max(1, int(min_sep_ms / 1000.0 * sr))
    idx, _ = find_peaks(env, distance=min_dist)
    if len(idx) == 0:
        return []
    amps   = env[idx]
    order  = np.argsort(amps)[::-1][:num_peaks]
    chosen = sorted(order, key=idx.__getitem__)  # back to time order
    return [(1000.0 * idx[o] / sr,
             20.0 * np.log10(env[idx[o]] + 1e-30)) for o in chosen]


def forensic_bloom_time_ms(wet_mono, sr, onset_idx=0, window_ms=5.0):
    """Time of the RMS envelope's absolute maximum, measured from onset.

    A very short bloom time (< 5 ms) means energy is front-loaded via loud ER
    taps (ChromaVerb Vocal Plate blooms at 2.5 ms). A long bloom time (> 50 ms)
    means density builds up slowly inside a tank/FDN before the peak arrives.
    """
    win_n = int(window_ms / 1000.0 * sr)
    if win_n <= 0 or len(wet_mono) < win_n:
        return None
    sq = wet_mono.astype(np.float64) ** 2
    rms = np.sqrt(np.convolve(sq, np.ones(win_n) / win_n, mode="same"))
    bi  = int(np.argmax(rms))
    return 1000.0 * (bi - onset_idx) / sr, 20.0 * np.log10(rms[bi] + 1e-30)


def forensic_sub_bass_psd(wet_mono, sr, nperseg=32768,
                          checkpoints=(30.0, 60.0, 120.0)):
    """Welch PSD, band-integrated in the 20–400 Hz window.

    Returns a dict: {fc: band_db} for each checkpoint Hz. Uses ±20% band
    integration around each centre, so sub-bass craters or resonant bumps
    can't hide between the 8-band octave averages.
    """
    from scipy.signal import welch
    f, Pxx = welch(wet_mono, fs=sr, nperseg=nperseg, noverlap=nperseg // 2,
                   window="hann", scaling="density", return_onesided=True)
    _trapz = getattr(np, "trapezoid", np.trapz)
    out = {}
    for fc in checkpoints:
        lo, hi = fc * 0.8, fc * 1.2
        mask = (f >= lo) & (f <= hi)
        if not mask.any():
            out[fc] = None
            continue
        power = float(_trapz(Pxx[mask], f[mask]))
        out[fc] = 10.0 * np.log10(power + 1e-30)
    return out


def forensic_stereo_wobble_fft(stereo, sr, win_ms=30, hop_ms=10,
                               t_start_ms=200, t_end_ms=2000,
                               min_rate_hz=0.5, max_rate_hz=20.0):
    """Detects 'wobble' / 'spinning' in the early-to-mid tail — periodic
    oscillation of the L/R correlation curve.

    AM-only modulation_character() misses this entirely: a stereo image
    that rotates because L and R have slightly out-of-phase delay LFOs
    has FLAT amplitude but modulating correlation. This is the metric
    that exposed DuskVerb's 7 Hz fluttery wobble during Vocal Plate
    calibration (a stuck mod_rate scaling bug that was delivering ~10 Hz
    tank LFO instead of the calibrated 0.7 Hz). AM detection said "no
    modulation" while the image was visibly spinning at 7 Hz.

    Method (per the calibration playbook):
      1. Slice the early-to-mid tail (200–2000 ms) into short overlapping
         windows (default 30 ms / 10 ms hop → 3× overlap).
      2. Compute Pearson L/R correlation per window → time-series of the
         stereo-width fluctuation, sampled at 100 Hz.
      3. Detrend with a 20-sample moving average so we measure oscillation
         around the slow decorrelation curve, not the curve itself.
      4. Hann-window the detrended series, run an FFT, find the dominant
         peak in the 0.5–20 Hz oscillation band.
      5. Report (peak rate Hz, peak FFT magnitude, mean correlation,
         peak-to-peak correlation range, window count).

    Reference reverbs land at 0.5–3 Hz, peak FFT magnitude well below
    0.05. Anything > 5 Hz with peak magnitude > 0.05 reads as fluttery.

    Returns a dict (or None for mono / too-short input).
    """
    if stereo.ndim != 2 or stereo.shape[1] < 2:
        return None
    win_n = int(win_ms / 1000.0 * sr)
    hop_n = int(hop_ms / 1000.0 * sr)
    s = int(t_start_ms / 1000.0 * sr)
    e = int(min(t_end_ms / 1000.0 * sr, len(stereo)))
    if e - s < win_n * 8:
        return None
    L = stereo[s:e, 0].astype(np.float64)
    R = stereo[s:e, 1].astype(np.float64)

    # Step 1+2: per-window L/R correlation curve.
    corrs = []
    for i in range(0, len(L) - win_n, hop_n):
        Lw, Rw = L[i:i+win_n], R[i:i+win_n]
        if Lw.std() > 1e-9 and Rw.std() > 1e-9:
            corrs.append(float(np.corrcoef(Lw, Rw)[0, 1]))
    if len(corrs) < 20:
        return None
    corr = np.array(corrs)

    # Step 3: detrend with a 20-window moving average.
    smooth = np.convolve(corr, np.ones(20) / 20, mode="same")
    detrended = (corr - smooth)[10:-10]
    if len(detrended) < 16:
        return None

    # Step 4: Hann-window + FFT, find dominant peak.
    fs_curve = 1.0 / (hop_ms / 1000.0)
    n = len(detrended)
    windowed = detrended * np.hanning(n)
    # Normalize FFT magnitude to "unit-input amplitude" — Hann window
    # has ENBW factor 1.5, so we divide by (n * 0.5) to recover
    # peak amplitude of an embedded sinusoid.
    spec = np.abs(np.fft.rfft(windowed)) * (2.0 / n)
    freqs = np.fft.rfftfreq(n, 1.0 / fs_curve)
    mask = (freqs >= min_rate_hz) & (freqs <= max_rate_hz)
    if not mask.any():
        return None
    band_freqs = freqs[mask]
    band_spec = spec[mask]
    peak_i = int(np.argmax(band_spec))
    rate = float(band_freqs[peak_i])
    peak_mag = float(band_spec[peak_i])

    return {
        "mean_corr":         float(corr.mean()),
        "dominant_rate_hz":  rate,
        "peak_fft_mag":      peak_mag,
        "range_pp":          float(corr.max() - corr.min()),
        "n_windows":         int(len(corrs)),
        "win_ms":            win_ms,
        "hop_ms":            hop_ms,
        "t_window":          (t_start_ms, t_end_ms),
    }


def active_decay_end_ms(mono, sr, drop_db=60.0, smooth_ms=20.0):
    """Time (ms from start) at which the smoothed envelope first drops
    `drop_db` below its peak. Falls back to the total IR length if the
    envelope never drops that far (short IR or noisy tail).

    Used by modal_density / diffusion_smoothness to avoid normalising by
    silent trailing samples — otherwise a 2 s decay rendered into a 6 s
    buffer would measure 1/3 the real rate.
    """
    env = np.abs(mono)
    sm = max(1, int(smooth_ms / 1000 * sr))
    env = np.convolve(env, np.ones(sm) / sm, mode="same")
    peak = env.max()
    if peak <= 1e-9:
        return 1000.0 * len(mono) / sr
    floor = peak * (10.0 ** (-drop_db / 20.0))
    peak_idx = int(np.argmax(env))
    below = np.where(env[peak_idx:] < floor)[0]
    if len(below) == 0:
        return 1000.0 * len(mono) / sr
    return 1000.0 * (peak_idx + below[0]) / sr


def modal_density_per_sec(mono, sr, start_ms=50, end_ms=None):
    """Envelope-zero-crossings per second over a tail window. Counts how many
    times the smoothed envelope crosses its own running mean — a proxy for
    the perceptual 'busyness' of the reverb tail.

    end_ms defaults to the active decay end (envelope down 60 dB from peak)
    so a trailing buffer of silence doesn't dilute the per-second rate.
    """
    if end_ms is None:
        end_ms = active_decay_end_ms(mono, sr)
    s = int(start_ms / 1000 * sr)
    e = int(end_ms / 1000 * sr)
    if e <= s + 100:
        return 0.0
    env = np.abs(mono[s:e])
    smooth_n = max(1, int(0.005 * sr))  # 5 ms smoothing
    env = np.convolve(env, np.ones(smooth_n) / smooth_n, mode="same")
    centered = env - env.mean()
    zc = int(np.sum(np.diff(np.sign(centered)) != 0))
    duration = (e - s) / sr
    return zc / duration


def diffusion_smoothness(mono, sr, start_ms=100, end_ms=None):
    """Coefficient of variation of the smoothed tail envelope.
    Smooth, washy tails: low CV. Grainy/bumpy tails: high CV.
    Reported as a 0..1 score: 0 = perfectly smooth, 1 = highly grainy.

    end_ms defaults to the active decay end (envelope down 60 dB from peak)
    so the silent tail — whose normalised envelope is ~0 and inflates the
    CV — doesn't saturate the score at 1.0.
    """
    if end_ms is None:
        end_ms = active_decay_end_ms(mono, sr)
    s = int(start_ms / 1000 * sr)
    e = int(end_ms / 1000 * sr)
    if e <= s + 100:
        return 0.0
    env = np.abs(mono[s:e])
    smooth_n = max(1, int(0.010 * sr))  # 10 ms smoothing
    env = np.convolve(env, np.ones(smooth_n) / smooth_n, mode="same")
    # Detrend by dividing by exponential fit (so we measure ripple, not decay)
    if env.max() > 1e-9:
        env_norm = env / (env.max() + 1e-12)
        if env_norm.mean() > 1e-6:
            cv = float(env_norm.std() / env_norm.mean())
            return min(cv, 2.0) / 2.0  # clamp + normalise to 0..1
    return 0.0


def stereo_corr_over_time(stereo, sr, windows_ms=((0, 100), (100, 300),
                                                  (300, 500), (500, 1000),
                                                  (1000, 2000), (2000, 3000))):
    """L/R correlation per time window. Captures stereo evolution over the IR.
    Plates typically open up (correlation drops) across the tail.
    """
    if stereo.ndim != 2 or stereo.shape[1] < 2:
        return None
    out = []
    for lo_ms, hi_ms in windows_ms:
        s = int(lo_ms / 1000 * sr)
        e = int(min(hi_ms / 1000 * sr, len(stereo)))
        if e - s < 100:
            out.append(None)
            continue
        L = stereo[s:e, 0].astype(np.float64)
        R = stereo[s:e, 1].astype(np.float64)
        if L.std() < 1e-9 or R.std() < 1e-9:
            out.append(0.0)
        else:
            out.append(float(np.corrcoef(L, R)[0, 1]))
    return out


def modulation_character(mono, sr, start_ms=200, end_ms=None,
                         min_mod_hz=0.3, max_mod_hz=15.0):
    """Detects periodic amplitude modulation (chorus / plate shimmer) in the
    tail. Returns (rate_hz, depth_dB) where depth is the amplitude oscillation
    of the envelope around its mean.
    """
    if end_ms is None:
        end_ms = 1000.0 * len(mono) / sr
    s = int(start_ms / 1000 * sr)
    e = int(end_ms / 1000 * sr)
    if e - s < int(2.0 * sr / min_mod_hz):
        return None, None
    env = np.abs(mono[s:e])
    smooth_n = max(1, int(0.020 * sr))  # 20 ms envelope smoothing
    env = np.convolve(env, np.ones(smooth_n) / smooth_n, mode="same")
    # Remove the global decay trend by normalising to a slow moving average
    detrend_n = max(1, int(0.300 * sr))
    trend = np.convolve(env, np.ones(detrend_n) / detrend_n, mode="same") + 1e-12
    flat = env / trend - 1.0  # zero-mean fluctuation around the decay envelope
    # Spectrum of the flat fluctuation reveals AM rates
    spec = np.abs(np.fft.rfft(flat * np.hanning(len(flat))))
    freqs = np.fft.rfftfreq(len(flat), 1 / sr)
    mask = (freqs >= min_mod_hz) & (freqs <= max_mod_hz)
    if not np.any(mask):
        return None, None
    band_spec = spec[mask]
    band_freqs = freqs[mask]
    peak_idx = int(np.argmax(band_spec))
    rate_hz = float(band_freqs[peak_idx])
    # Depth: peak spectral magnitude relative to total (rough proxy)
    peak_mag = band_spec[peak_idx]
    floor    = np.median(band_spec) + 1e-12
    depth_db = float(20.0 * np.log10(peak_mag / floor + 1e-12))
    return rate_hz, depth_db


def analyze(path, dry_path=None):
    sr, data = load_ir(path)
    mono = to_mono(data)

    print(f"=== {path} ===")
    print(f"Sample rate:     {sr} Hz   ({len(mono)} samples, {1000.0*len(mono)/sr:.0f} ms)")
    print(f"Onset:           {find_onset_ms(mono, sr):.0f} ms          → PRE-DELAY")
    print(f"RMS:             {rms_dbfs(mono):.1f} dBFS       → GAIN TRIM")
    corr = lr_correlation(data)
    if corr is not None:
        print(f"L/R correlation: {corr:+.2f}            → STEREO COUPLING (broadband)")
    print()

    rt60 = per_band_rt60(mono, sr)
    eq   = per_band_eq_db(mono, sr)

    print("PER-OCTAVE RT60 (seconds):")
    print("  " + " ".join(f"{l:>5s}" for l in BAND_LABELS))
    print(" " + fmt_band_row(rt60, "{:5.2f}"))
    print()

    print("PER-OCTAVE EQ (dB relative to 500-1k mid):")
    print("  " + " ".join(f"{l:>5s}" for l in BAND_LABELS))
    print(" " + fmt_band_row(eq, "{:5.1f}"))
    print()

    # ---- Voice / character measurements ----
    print("VOICE / CHARACTER:")
    density = modal_density_per_sec(mono, sr)
    print(f"  Modal density:        {density:6.1f} env-crossings/sec  → DENSITY")
    smoothness = diffusion_smoothness(mono, sr)
    print(f"  Diffusion grain:      {smoothness:6.2f} (0=smooth wash, 1=grainy) → DIFFUSION")
    rate, depth = modulation_character(mono, sr)
    if rate is not None:
        print(f"  Modulation rate:      {rate:6.2f} Hz                  → MOD RATE")
        print(f"  Modulation depth:     {depth:6.1f} dB                  → MOD DEPTH")
    else:
        print(f"  Modulation:           (no detectable AM modulation)   → MOD DEPTH = 0")

    stereo_evolution = stereo_corr_over_time(data, sr)
    if stereo_evolution is not None:
        print()
        print("  Stereo correlation over time:")
        windows = [(0, 100), (100, 300), (300, 500), (500, 1000), (1000, 2000), (2000, 3000)]
        for (lo, hi), c in zip(windows, stereo_evolution):
            cstr = f"{c:+.2f}" if c is not None else "  --"
            print(f"    {lo:>5}-{hi:<5} ms:  {cstr}")
        print(f"    (avg = {np.mean([c for c in stereo_evolution if c is not None]):+.2f})")

    print()
    print("EARLY REFLECTION MAP (top 8 peaks in first 100 ms):")
    er_peaks = early_reflection_peaks(mono, sr, num_peaks=8)
    print(f"    {'time (ms)':>10}   {'level (dB)':>10}")
    for t, lv in er_peaks:
        print(f"    {t:>10.1f}   {lv:>+10.1f}")
    print()

    # ------------------------------------------------------------------
    # FORENSIC block — bypasses broad-octave averaging and peak-relative
    # thresholds to surface metrics that wide-stat summaries were hiding:
    #   • True pre-delay (needs --dry; else falls back to abs -60 dBFS)
    #   • Envelope-peak ER map with 0.5 ms smoothing, 1 ms separation
    #   • Bloom time (rolling 5 ms RMS absolute peak, from onset)
    #   • Sub-bass PSD at 30 / 60 / 120 Hz via Welch
    # ------------------------------------------------------------------
    print("FORENSIC METRICS (high-resolution time/freq domain):")
    onset_idx = forensic_onset_idx(mono) or 0

    if dry_path is not None and os.path.isfile(dry_path):
        try:
            _, dry_data = load_ir(dry_path)
            dry_mono = to_mono(dry_data)
            pd_ms = forensic_true_predelay_ms(mono, dry_mono, sr)
            if pd_ms is not None:
                print(f"  True pre-delay:       {pd_ms:+.4f} ms  "
                      f"(vs dry '{os.path.basename(dry_path)}')")
        except Exception as e:
            print(f"  True pre-delay:       (dry load failed: {e})")
    else:
        # Fallback: absolute -60 dBFS onset with no dry reference.
        if forensic_onset_idx(mono) is not None:
            t_ms = 1000.0 * onset_idx / sr
            print(f"  Onset (abs -60 dBFS): {t_ms:.4f} ms   "
                  f"(pass --dry <impulse.wav> for true pre-delay)")

    bloom = forensic_bloom_time_ms(mono, sr, onset_idx=onset_idx)
    if bloom is not None:
        bt, bdb = bloom
        print(f"  Bloom time:           {bt:.2f} ms post-onset  "
              f"(RMS peak = {bdb:+.2f} dBFS)")

    print(f"  Forensic ER peaks (0.5 ms envelope, 1 ms separation, top 8):")
    fe = forensic_er_peaks(mono, sr, onset_idx=onset_idx, num_peaks=8)
    if not fe:
        print("    (no peaks found in 0–100 ms window)")
    else:
        print(f"    {'time (ms)':>10}   {'level (dB env)':>14}")
        for t, lv in fe:
            print(f"    {t:>10.3f}   {lv:>+14.2f}")

    sb = forensic_sub_bass_psd(mono, sr)
    print(f"  Sub-bass PSD (Welch, ±20% band integration):")
    for fc in (30.0, 60.0, 120.0):
        v = sb.get(fc)
        if v is None:
            print(f"    {fc:>5.0f} Hz:  (bin unavailable)")
        else:
            print(f"    {fc:>5.0f} Hz:  {v:+.2f} dB")

    # Stereo wobble — catches the fluttery rotation that AM-only modulation
    # detection misses. 30 ms windowed L/R correlation → FFT to find the
    # dominant oscillation rate + peak magnitude in 0.5-20 Hz band.
    sw = forensic_stereo_wobble_fft(data, sr)
    if sw is None:
        print(f"  Stereo wobble:        (mono input or insufficient tail)")
    else:
        # Heuristic: above 5 Hz with peak FFT magnitude > 0.04 = fluttery.
        # Reference reverbs sit at 0.5-3 Hz with peak < 0.04.
        flag = " ⚠ FAST" if sw["dominant_rate_hz"] > 5.0 \
                              and sw["peak_fft_mag"] > 0.04 \
                       else ""
        print(f"  Stereo wobble FFT ({sw['win_ms']} ms win, "
              f"{sw['t_window'][0]}-{sw['t_window'][1]} ms tail):")
        print(f"    mean L/R corr:        {sw['mean_corr']:+.3f}")
        print(f"    L/R range pk-pk:      {sw['range_pp']:.3f}")
        print(f"    peak rate:            {sw['dominant_rate_hz']:.2f} Hz{flag}")
        print(f"    peak FFT magnitude:   {sw['peak_fft_mag']:.4f}")
    print()

    # The plugin's eq_band_X knob clamps to -30..+12 dB. Anything outside that
    # range gets silently truncated, so flag it loudly here so calibration knows
    # to either widen the range or accept that the IR has more depth than the
    # plugin can express.
    PLUGIN_EQ_MIN, PLUGIN_EQ_MAX = -30.0, 12.0
    over = [(BAND_LABELS[i], v) for i, v in enumerate(eq)
            if v is not None and (v < PLUGIN_EQ_MIN or v > PLUGIN_EQ_MAX)]
    if over:
        print()
        print(f"!! WARNING: {len(over)} band(s) outside plugin's [{PLUGIN_EQ_MIN:.0f}, "
              f"+{PLUGIN_EQ_MAX:.0f}] dB EQ range and will be clamped:")
        for label, v in over:
            print(f"     {label:>4s} Hz: {v:+.1f} dB")
        print(f"     → consider widening eq_band_X range in PluginProcessor.cpp +")
        print(f"       EightBandPeakingEQ.setBandGainDb clamp in TwoBandDamping.h.")
    print()

    name = os.path.splitext(os.path.basename(path))[0]
    print(f"--- Copy-paste into <PresetName>Preset.cpp ---")
    print(f"// From analyze_ir.py on {os.path.basename(path)}")
    print(f"constexpr float kBakedRt60[8] = {fmt_constants_array(rt60)};")
    print(f"constexpr float kBakedEq[8]   = {fmt_constants_array(eq)};")


def envelope_window_db(mono, sr, onset_idx, window_ms, horizon_ms):
    """Per-window RMS in dB, normalized to the IR's own peak.

    Returns:
      times_ms: array of window-start times (ms relative to onset)
      env_db:   array of RMS values in dB (normalized to peak = 0 dB)
    """
    peak = float(np.max(np.abs(mono)))
    if peak <= 1e-12:
        return np.array([]), np.array([])
    norm = mono / peak
    samps_per = max(1, int(round(window_ms / 1000.0 * sr)))
    n_win = max(1, int(horizon_ms / window_ms))
    out = np.full(n_win, -120.0, dtype=np.float64)
    times = np.arange(n_win) * window_ms
    for i in range(n_win):
        s = onset_idx + i * samps_per
        e = s + samps_per
        if e > len(norm):
            break
        chunk = norm[s:e]
        rms = float(np.sqrt(np.mean(chunk * chunk)))
        out[i] = -120.0 if rms <= 1e-9 else 20.0 * np.log10(rms)
    return times, out


def find_first_above_db(mono, sr, threshold_db=-60.0):
    """First sample index where |x| exceeds threshold (absolute, not relative)."""
    thresh = 10.0 ** (threshold_db / 20.0)
    above = np.where(np.abs(mono) > thresh)[0]
    return int(above[0]) if len(above) else 0


def envelope_diff(ref_path, test_path, window_ms=5.0, horizon_ms=200.0):
    """Print first-200ms envelope shape comparison + MSE error score.

    This block runs BEFORE any RT60/EQ stats — the envelope shape is the
    primary indicator of audible character match. Two IRs with the same
    summary metrics can have different envelope shapes and sound completely
    different.
    """
    sr_r, data_r = load_ir(ref_path)
    sr_t, data_t = load_ir(test_path)
    mono_r = to_mono(data_r)
    mono_t = to_mono(data_t)

    on_r = find_first_above_db(mono_r, sr_r, threshold_db=-60.0)
    on_t = find_first_above_db(mono_t, sr_t, threshold_db=-60.0)

    t_r, env_r = envelope_window_db(mono_r, sr_r, on_r, window_ms, horizon_ms)
    t_t, env_t = envelope_window_db(mono_t, sr_t, on_t, window_ms, horizon_ms)

    n = min(len(env_r), len(env_t))
    env_r = env_r[:n]
    env_t = env_t[:n]
    diff = env_t - env_r
    mse = float(np.mean(diff * diff))
    rmse = float(np.sqrt(mse))
    max_dev = float(np.max(np.abs(diff)))

    ref_label  = os.path.basename(ref_path)[:24]
    test_label = os.path.basename(test_path)[:24]

    print("=" * 80)
    print(f"  TIME-DOMAIN ENVELOPE DIFF — first {horizon_ms:.0f} ms, {window_ms:.0f} ms windows")
    print(f"  Reference: {ref_label}")
    print(f"  Test:      {test_label}")
    print("=" * 80)
    print(f"  Onset (first sample > -60 dBFS):  ref={on_r/sr_r*1000:.2f} ms   test={on_t/sr_t*1000:.2f} ms")
    print()
    print(f"  {'t(ms)':>6} | {'REF':>7} | {'DV':>7} | {'delta':>7}  shape (each char = 1.5 dB above -60)")
    print("  " + "-" * 78)
    for i in range(n):
        t = i * window_ms
        a = env_r[i]
        b = env_t[i]
        d = b - a
        bar_a = "S" * max(0, int((a + 60) / 1.5))
        bar_b = "D" * max(0, int((b + 60) / 1.5))
        a_s = f"{a:+.1f}"
        b_s = f"{b:+.1f}"
        d_s = f"{d:+.1f}"
        print(f"  {t:6.1f} | {a_s:>7} | {b_s:>7} | {d_s:>7}  R:{bar_a:<40s}")
        print(f"         |         |         |          T:{bar_b:<40s}")

    print("  " + "-" * 78)
    print(f"  Envelope error:  MSE = {mse:.2f} dB²   RMSE = {rmse:.2f} dB   peak |Δ| = {max_dev:.2f} dB")
    if rmse < 2.0:
        verdict = "✅ EXCELLENT  — within ~JND across the bloom region"
    elif rmse < 4.0:
        verdict = "✓ GOOD        — broad shape match with minor drift"
    elif rmse < 7.0:
        verdict = "⚠ MARGINAL    — visibly different temporal character"
    else:
        verdict = "❌ FAILING     — fundamentally different envelope shape"
    print(f"  Verdict:         {verdict}")
    print("=" * 80)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("wav", nargs="+", help="path(s) to IR WAV file")
    parser.add_argument("--dry", default=None,
                        help="optional dry impulse WAV — enables true pre-delay "
                             "(offset from dry peak to wet -60 dBFS onset)")
    parser.add_argument("--ref", default=None,
                        help="reference IR for time-domain envelope comparison "
                             "(MANDATORY before any RT60/EQ scorecard) — prints "
                             "5 ms RMS envelope diff for the first 200 ms with "
                             "MSE error score")
    args = parser.parse_args()

    # MANDATORY: if --ref is given, print envelope diff FIRST for every IR
    # before any per-octave summary stats. Two IRs with the same RT60/EQ
    # can sound completely different if their bloom envelopes differ.
    for path in args.wav:
        if not os.path.isfile(path):
            print(f"!! not a file: {path}", file=sys.stderr)
            continue
        if args.ref:
            if not os.path.isfile(args.ref):
                print(f"!! reference not a file: {args.ref}", file=sys.stderr)
            else:
                try:
                    envelope_diff(args.ref, path)
                    print()
                except Exception as e:
                    print(f"!! envelope diff failed: {e}", file=sys.stderr)
        try:
            analyze(path, dry_path=args.dry)
        except Exception as e:
            print(f"!! {path}: {e}", file=sys.stderr)
        if len(args.wav) > 1:
            print()


if __name__ == "__main__":
    main()
