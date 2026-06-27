#!/usr/bin/env python3
"""
DuskVerb preset-vs-external-IR metrics + loss.

Used by preset_vs_external_optuna.py to score a candidate render
against a reference IR (typically a Valhalla Vintage Verb render at
100% wet).

Public API:
    compute_metrics(wav_path, decay_seek_s=5.0) -> dict
    compare(dv_path, vvv_path, weights=None) -> (loss, breakdown_dict)

The third-octave magnitude vectors are RMS-normalized in dB BEFORE the
L1 spectral loss. Without this, an optimizer with free Bass/Mid/Treble
multipliers can drive level offsets to zero the spectral term without
actually matching the shape. The normalization forces the loss to
score CONTOUR not LEVEL — exactly what we want when Gain Trim is locked.
"""

from __future__ import annotations

import math
import numpy as np
from scipy.io import wavfile
from scipy.signal import hilbert


# ---------------------------------------------------------------------------
# WAV loading
# ---------------------------------------------------------------------------

def _load_stereo(path: str) -> tuple[int, np.ndarray]:
    """Returns (sample_rate, stereo float32 array shape (N, 2))."""
    sr, x = wavfile.read(path)
    if x.dtype.kind == 'i':
        x = x.astype(np.float32) / np.iinfo(x.dtype).max
    elif x.dtype.kind == 'u':
        # Unsigned PCM (e.g. 8-bit WAV) is offset-binary: silence sits at the
        # mid-code, not 0. Center then scale to [-1, 1) like the signed path,
        # else every sample carries a large DC offset.
        mid = (np.iinfo(x.dtype).max + 1) / 2.0
        x = (x.astype(np.float32) - mid) / mid
    elif x.dtype.kind == 'f':
        x = x.astype(np.float32)
    if x.ndim == 1:
        x = np.stack([x, x], axis=1)
    return int(sr), x


# ---------------------------------------------------------------------------
# Helper metrics
# ---------------------------------------------------------------------------

def _envelope(mono: np.ndarray, sr: int, win_ms: float = 10.0) -> np.ndarray:
    """Hilbert-magnitude envelope smoothed with a boxcar of win_ms."""
    env = np.abs(hilbert(mono))
    n = max(1, int(win_ms * 1e-3 * sr))
    cs = np.cumsum(np.concatenate([[0.0], env]))
    return (cs[n:] - cs[:-n]) / n


def _per_band_decay_times(mono: np.ndarray, sr: int) -> dict:
    """Per-band tail decay measurement. Splits the signal into bands and runs
    noise-floor-aware peak-to-(-NdB) measurement on each. Includes t10 (EDT)
    which captures the perceived initial-tail character that t30/t60 hide.

    Returns dict {band_name: {t10, t30, t60}}. t10 = Early Decay Time —
    the time from peak to -10 dB. Critical for matching "weight" / "hold"
    of low bands that mono t60 averages away.
    """
    from scipy.signal import butter, sosfiltfilt
    bands = [('sub',     20,  100),
             ('low',    100,  500),
             ('low_mid', 250,  500),  # extra resolution for the user-perceived gap
             ('mid',    500, 2000),
             ('hi',    2000, 8000)]
    out = {}
    for name, lo, hi in bands:
        hi_c = min(hi, sr * 0.49)
        if lo <= 0:
            sos = butter(4, hi_c, 'low', fs=sr, output='sos')
        else:
            sos = butter(4, [lo, hi_c], 'band', fs=sr, output='sos')
        y = sosfiltfilt(sos, mono)
        decays = _tail_decay_times(y, sr)
        out[name] = {'t10': decays.get('t10'),
                     't30': decays.get('t30'),
                     't60': decays.get('t60')}
    return out


def _envelope_array_db(mono: np.ndarray, sr: int, win_ms: float = 5.0,
                       post_peak_ms: float = 500.0) -> tuple:
    """Smoothed log-envelope array for the first `post_peak_ms` ms after the
    signal peak. Returns (times_array, env_db_array). Used by the comparator
    to compute envelope-shape MSE/L1 between DV and reference — captures
    the ATTACK / EARLY-DECAY contour that scalar t30 misses.

    The envelope is Hilbert magnitude smoothed with a boxcar of `win_ms`,
    converted to dB relative to the peak. Both signals' envelopes are
    self-normalized so the comparison is gain-invariant.
    """
    from scipy.signal import hilbert
    env = np.abs(hilbert(mono))
    win = max(int(win_ms / 1000.0 * sr), 1)
    env_sm = np.convolve(env, np.ones(win) / win, mode='same')
    pidx = int(np.argmax(env_sm))
    end = min(pidx + int(post_peak_ms / 1000.0 * sr), len(env_sm))
    seg = env_sm[pidx:end]
    if len(seg) < 16:
        return np.array([]), np.array([])
    # Normalize to peak (so shape, not absolute level, is compared)
    peak = float(np.max(seg) + 1e-30)
    seg_db = 20.0 * np.log10((seg / peak) + 1e-9)
    # Clamp very low values to -60 dB so noise floor doesn't dominate L1
    seg_db = np.maximum(seg_db, -60.0)
    times = np.arange(len(seg)) / sr
    return times, seg_db


def envelope_shape_l1(dv_mono: np.ndarray, lex_mono: np.ndarray, sr: int,
                      post_peak_ms: float = 500.0) -> float:
    """L1 distance between the peak-normalized envelopes of DV vs reference,
    over [0, post_peak_ms]ms post-peak. Higher = bigger shape mismatch.
    Returns mean |Δ_dB| over the window. Typical good match: < 2 dB.
    Typical "DV drops fast, Lex holds flat" mismatch: > 5 dB.
    """
    _, dv_env = _envelope_array_db(dv_mono, sr, post_peak_ms=post_peak_ms)
    _, lx_env = _envelope_array_db(lex_mono, sr, post_peak_ms=post_peak_ms)
    n = min(len(dv_env), len(lx_env))
    if n < 16:
        return float('nan')
    return float(np.mean(np.abs(dv_env[:n] - lx_env[:n])))


def _tail_decay_times(mono: np.ndarray, sr: int) -> dict:
    """
    Direct noise-floor-aware peak-to-(-NdB) tail decay measurement.

    Returns the time (in seconds, post-peak) at which the smoothed envelope
    drops to peak - {10, 20, 30, 40, 60} dB. Uses a noise-floor estimate
    (median of last 500 ms) to clamp threshold above floor so slope-fit
    artifacts on short tails (<2 s) cannot return spurious large times.

    This is the PERCEPTUALLY MEANINGFUL decay measurement that the old
    `_slope_fit` -> rt60 path failed to deliver: a 0.6 s plate slope-fit
    over a 5 s window returns ~9 s (noise floor slope), driving optimizers
    to pick 3-5x too long a decay.
    """
    win = max(int(0.01 * sr), 1)
    pow_ = mono ** 2
    sm = np.convolve(pow_, np.ones(win) / win, mode='same')
    peak = float(np.max(sm))
    if peak < 1e-12:
        return {f't{db}': None for db in (10, 20, 30, 40, 60)}
    pidx = int(np.argmax(sm))
    # Schroeder backward-integration EDC (ISO 3382). The old first-sample-below-
    # threshold returned spurious tiny times on DIP-THEN-BLOOM signals (a sharp
    # onset/transient followed by a slow reverb SWELL — e.g. a shimmer pad: the
    # transient's initial dip crossed peak−60 dB at ~0.05 s, long before the
    # bloom). The EDC is monotonic and dominated by total remaining energy (the
    # long bloom, not the brief transient), so it measures the TRUE tail decay.
    # Truncated at the noise floor (Lundeby-lite) so integrated noise can't
    # inflate t60. On a normal monotonic decay EDC ≡ the old crossing.
    floor_window = sm[-min(int(0.5 * sr), len(sm)):]
    noise = float(np.median(floor_window)) if len(floor_window) > 0 else peak * 1e-9
    tail = sm[pidx:]
    aud = np.where(tail > noise * 4.0)[0]
    if len(aud) > 1:
        tail = tail[: int(aud[-1]) + 1]
    if len(tail) < win * 2:
        return {f't{db}': None for db in (10, 20, 30, 40, 60)}
    edc = np.cumsum(tail[::-1])[::-1]
    if edc[0] <= 1.0e-30:
        return {f't{db}': None for db in (10, 20, 30, 40, 60)}
    edc_db = 10.0 * np.log10(np.maximum(edc / edc[0], 1.0e-12))
    out = {}
    for db in (10, 20, 30, 40, 60):
        below = np.where(edc_db <= -float(db))[0]
        out[f't{db}'] = int(below[0]) / sr if len(below) else None
    return out


def _slope_fit(env_seg: np.ndarray, sr: int) -> tuple[float, np.ndarray]:
    """Log-linear fit of envelope in dB. Returns (slope_dB_per_s, residual_dB)."""
    seg = env_seg[env_seg > env_seg.max() * 1e-4]
    if len(seg) < 16:
        return 0.0, np.array([0.0])
    t = np.arange(len(seg)) / sr
    y = 20.0 * np.log10(seg + 1e-12)
    A = np.vstack([t, np.ones_like(t)]).T
    sol, *_ = np.linalg.lstsq(A, y, rcond=None)
    slope, intercept = float(sol[0]), float(sol[1])
    residual = y - (slope * t + intercept)
    return slope, residual


def _spectral_centroid(seg: np.ndarray, sr: int) -> float:
    """Spectral centroid (Hz) of a windowed signal."""
    w = np.hanning(len(seg))
    S = np.abs(np.fft.rfft(seg * w))
    f = np.fft.rfftfreq(len(seg), d=1.0 / sr)
    denom = S.sum() + 1e-30
    return float((f * S).sum() / denom)


def _third_octave_magnitude_db(
    mono: np.ndarray, sr: int, f_lo: float = 80.0, f_hi: float = 16000.0
) -> tuple[np.ndarray, np.ndarray]:
    """
    Magnitude (dB) of mono signal in 1/3-octave bands from f_lo to f_hi.

    Returns (band_center_hz, magnitude_db). The magnitude is computed
    from the FFT of the entire signal, integrated over each band.
    """
    # Window the signal so the FFT is well-defined.
    w = np.hanning(len(mono))
    S = np.abs(np.fft.rfft(mono * w)) ** 2  # power spectrum
    f = np.fft.rfftfreq(len(mono), d=1.0 / sr)

    # 1/3-octave band centers: 2^(1/3) ratio.
    band_centers = []
    fc = f_lo
    while fc <= f_hi:
        band_centers.append(fc)
        fc *= 2 ** (1.0 / 3.0)
    band_centers = np.asarray(band_centers, dtype=np.float64)

    mags_db = np.zeros_like(band_centers)
    for i, fc in enumerate(band_centers):
        f_low = fc / 2 ** (1.0 / 6.0)
        f_high = fc * 2 ** (1.0 / 6.0)
        mask = (f >= f_low) & (f < f_high)
        if mask.any():
            power = S[mask].sum()
            mags_db[i] = 10.0 * math.log10(power + 1e-30)
        else:
            mags_db[i] = -120.0  # below floor
    return band_centers, mags_db


def _rms_normalize_db(mags_db: np.ndarray) -> np.ndarray:
    """
    Subtract the mean-power level (in dB) so the array has zero mean
    in the power domain.

    Mathematically: convert dB → linear power, compute mean power,
    convert back to dB, subtract. Equivalent to subtracting
    10·log10(mean(P)) where P = 10**(dB/10). Floor bands (-120 dB from
    `_third_octave_magnitude_db`) contribute negligible power so the
    mean is dominated by the finite bands; if every band is at the
    -120 dB floor the offset is well-defined (~-120 dB).
    """
    # Convert to linear power, compute mean, convert back to dB.
    p = np.power(10.0, mags_db / 10.0)
    rms_db = 10.0 * math.log10(p.mean() + 1e-30)
    return mags_db - rms_db


def _stereo_waveform_corr(L: np.ndarray, R: np.ndarray) -> float:
    """Pearson correlation of L vs R (mean-removed)."""
    Lc = L - L.mean()
    Rc = R - R.mean()
    denom = math.sqrt((Lc ** 2).mean() * (Rc ** 2).mean()) + 1e-30
    return float((Lc * Rc).mean() / denom)


# ---------------------------------------------------------------------------
# Public metrics
# ---------------------------------------------------------------------------

def compute_metrics(wav_path: str, decay_seek_s: float = 5.0, normalize_rms: bool = True) -> dict:
    """
    Extract a vector of comparable metrics from a single render.

    Tail window for RT60/envelope-residual: 50 ms .. min(decay_seek_s, file_end).
    Centroid 50 ms window: 50 ms .. 500 ms.
    Centroid 500 ms window: 500 ms .. 1500 ms.

    Returns:
      sr            sample rate
      rt60          (-60 / slope_db_per_s) on tail, in seconds; inf if undefined
      slope_db_per_s
      env_res_p2p   max-min of log-decay residual (dB)
      env_res_rms   RMS of log-decay residual (dB)
      cent_50       spectral centroid 50..500ms (Hz)
      cent_500      spectral centroid 500..1500ms (Hz)
      stereo_corr   waveform L/R Pearson over tail
      oct_centers   1/3-octave band centers (Hz)
      oct_db_norm   1/3-octave magnitude, RMS-normalized to 0 dB (vector)
    """
    sr, x = _load_stereo(wav_path)
    L, R = x[:, 0], x[:, 1]
    mono = 0.5 * (L + R)

    # ─── PEAK-ALIGN ALL WINDOWS ───
    # Find the absolute amplitude peak. All time windows below are computed
    # relative to this peak so DV and reference signals with different
    # pre-delays (Lex VVP has ~150 ms baked into its fxp; DV typically has
    # 10 ms) compare apples to apples. Without this, the 50-500 ms window
    # measured DV's main bloom against Lex's pre-delay silence, producing
    # garbage centroid + spec_L1 values that misled the optimizer for the
    # entire prior calibration sprint.
    peak_idx = int(np.argmax(np.abs(mono)))
    peak_s = peak_idx / sr

    # Crop everything to peak-aligned coordinates. `mono` etc. are reused
    # below but with origin shifted to peak. Subsequent code treats sample 0
    # as the peak sample.
    L = L[peak_idx:]
    R = R[peak_idx:]
    mono = mono[peak_idx:]

    # Capture the RAW absolute-level signal BEFORE any normalization so the
    # noise-floor gate can make honest decisions in dBFS. Without this
    # snapshot the gate sees the post-normalize signal (boosted by tens of
    # dB) and treats actual noise floor as audible content.
    mono_raw = mono.copy()

    # Level normalize so absolute-amplitude metrics (late_tail) compare across
    # IRs with different output gain. RMS over 50-550 ms (post-peak) set to
    # -36 dBFS reference (arbitrary; the absolute value doesn't matter — only
    # that both DV and anchor get same target). Skip if requested (raw mode).
    if normalize_rms:
        t0 = int(0.05 * sr); t1 = min(int(0.55 * sr), len(mono))
        if t1 - t0 > 16:
            seg_rms = float(np.sqrt(np.mean(mono[t0:t1] ** 2) + 1e-30))
            target_rms_dbfs = -36.0
            target_rms = 10 ** (target_rms_dbfs / 20.0)
            gain = target_rms / max(seg_rms, 1e-30)
            L = L * gain
            R = R * gain
            mono = mono * gain

    # Tail envelope + slope fit. Window is peak-aligned (samples are already
    # peak-cropped above), starting at 50 ms post-peak.
    sm = _envelope(mono, sr, win_ms=10.0)
    t0 = int(0.05 * sr)
    t1 = min(int(decay_seek_s * sr), len(sm))
    seg = sm[t0:t1]
    slope, residual = _slope_fit(seg, sr)
    rt60 = -60.0 / slope if slope < -1.0 else float('inf')

    # Direct, noise-floor-aware decay-time measurements. These replace
    # rt60-slope-fit as the perceptual decay metric — slope-fit on short
    # tails (<2 s) returns garbage (~9 s) because the fit drifts into the
    # noise floor. tail_t30/t60 are the loud, audible part of the decay.
    tail_times = _tail_decay_times(mono, sr)

    # Per-band tail decay: tracks frequency-dependent decay character.
    # Reveals e.g. bass-sustained tail (low_t60 > hi_t60) that mono t60
    # averages away. Critical for perception of "warmth" vs "darkness".
    band_decays = _per_band_decay_times(mono, sr)

    # Centroids in two windows. Each window includes a noise-floor gate:
    # if the segment's RMS drops below NOISE_FLOOR_DBFS the centroid is
    # set to None and propagates as "not measurable" to the comparator —
    # prevents the optimizer from chasing noise-floor spectral content
    # on short-tail plates where the anchor has already decayed.
    NOISE_FLOOR_DBFS = -80.0

    def _seg(a_s, b_s):
        a = int(a_s * sr)
        b = min(int(b_s * sr), len(mono))
        return mono[a:b]

    def _seg_rms_db_raw(a_s, b_s):
        """RMS in dBFS on the RAW pre-normalized signal. The noise-floor
        gate must use this, not the post-normalize mono, otherwise the
        normalize-gain falsely lifts the noise floor above the gate."""
        a = int(a_s * sr); b = min(int(b_s * sr), len(mono_raw))
        if b - a < 16:
            return float('-inf')
        return float(20 * np.log10(np.sqrt(np.mean(mono_raw[a:b] ** 2) + 1e-30) + 1e-30))

    def _gated_centroid(a_s, b_s):
        seg = _seg(a_s, b_s)
        if len(seg) < 16:
            return None
        if _seg_rms_db_raw(a_s, b_s) < NOISE_FLOOR_DBFS:
            return None
        return _spectral_centroid(seg, sr)

    cent_50 = _gated_centroid(0.05, 0.50)
    cent_500 = _gated_centroid(0.50, 1.50)
    # Backward-compat: callers that don't handle None expect a float.
    # Fall back to the early-window centroid only when the late window is
    # below noise floor — this preserves the previous "use cent_50 as
    # fallback" behavior without lying that there's mid-tail content.
    if cent_50 is None:
        cent_50 = float('nan')
    if cent_500 is None:
        cent_500 = float('nan')

    # Stereo waveform correlation over tail.
    Lt = L[t0:t1]
    Rt = R[t0:t1]
    stereo = _stereo_waveform_corr(Lt, Rt) if len(Lt) > 16 else 0.0

    # 1/3-octave magnitude of the entire signal, RMS-normalized.
    oct_centers, oct_db = _third_octave_magnitude_db(mono, sr)
    oct_db_norm = _rms_normalize_db(oct_db)

    # ── Tail-character metrics added 2026-05-25 ──
    # Time-domain crest: peak-of-envelope / RMS-of-envelope past 100 ms.
    # High TDC = specular chatter (MTDL / sparse FDN); low TDC = Gaussian wash.
    e_late = sm[int(0.10 * sr):] if len(sm) > int(0.10 * sr) else np.array([])
    if len(e_late) > 16:
        e_pos = e_late[e_late > e_late.max() * 1e-5]
        tdc = float(e_pos.max() / max(np.sqrt(np.mean(e_pos**2)), 1e-30)) if len(e_pos) > 0 else 0.0
    else:
        tdc = 0.0

    # Centroid drift: cent measured over a 300 ms window starting at
    # 50/150/300/500/1000/2000 ms. Each window is noise-floor gated; below
    # gate the value is None so the comparator skips the term instead of
    # comparing DV-real-signal against anchor's noise floor.
    centroid_drift = {}
    for ms in [50, 150, 300, 500, 1000, 2000]:
        a, b = int((ms / 1000) * sr), min(int((ms / 1000 + 0.3) * sr), len(mono))
        # Gate uses raw signal RMS.
        if b - a > 32 and _seg_rms_db_raw(ms / 1000, ms / 1000 + 0.3) >= NOISE_FLOOR_DBFS:
            centroid_drift[ms] = _spectral_centroid(mono[a:b], sr)
        else:
            centroid_drift[ms] = None

    # Late-tail RMS (dB) in fixed time windows.
    def _rms_db_window(a_s, b_s):
        a, b = int(a_s * sr), min(int(b_s * sr), len(mono))
        if b - a < 16:
            return float('nan')
        r = float(np.sqrt(np.mean(mono[a:b] ** 2) + 1e-30))
        return float(20 * np.log10(r))

    late_tail = {
        '1s_2s': _rms_db_window(1.0, 2.0),
        '2s_3s': _rms_db_window(2.0, 3.0),
        '3s_4s': _rms_db_window(3.0, 4.0),
    }

    # Treble / bass energy ratios over the *audible* tail. Window ends
    # when the signal drops below the noise floor (default 600 ms cap for
    # short plates so we don't average in noise). Without this cap, a
    # 0.6 s plate has 0.6 s of signal + 2.4 s of noise, and the integration
    # mostly measures the noise floor's spectrum.
    a = int(0.05 * sr)
    # Find the end of the audible tail: first index where the RAW-signal
    # moving RMS (pre-normalize) drops below the noise-floor gate.
    win_n = max(int(0.05 * sr), 1)  # 50 ms moving RMS
    sq_raw = mono_raw ** 2
    sq_sm = np.convolve(sq_raw, np.ones(win_n) / win_n, mode='same')
    rms_db = 10.0 * np.log10(sq_sm + 1e-30)
    below = np.where((np.arange(len(rms_db)) > a) & (rms_db < NOISE_FLOOR_DBFS))[0]
    b_audible = int(below[0]) if len(below) > 0 else min(int(3.0 * sr), len(mono))
    b = max(b_audible, a + int(0.1 * sr))  # at least 100 ms window
    b = min(b, len(mono))
    if b - a > 32:
        seg_full = mono[a:b] * np.hanning(b - a)
        S2 = np.abs(np.fft.rfft(seg_full)) ** 2
        ff = np.fft.rfftfreq(b - a, 1 / sr)
        total_e = float(S2.sum() + 1e-30)
        treble_ratio = float(S2[ff > 5000].sum() / total_e)
        bass_ratio = float(S2[ff < 200].sum() / total_e)
    else:
        treble_ratio = 0.0
        bass_ratio = 0.0

    return {
        'sr': sr,
        'rt60': rt60,
        'slope_db_per_s': slope,
        'env_res_p2p': float(residual.max() - residual.min()) if len(residual) > 1 else 0.0,
        'env_res_rms': float(np.sqrt(np.mean(residual ** 2))) if len(residual) > 1 else 0.0,
        'cent_50': cent_50,
        'cent_500': cent_500,
        'stereo_corr': stereo,
        'oct_centers': oct_centers,
        'oct_db_norm': oct_db_norm,
        # Direct tail-decay times (post-peak, noise-floor-aware).
        # tail_t30 / tail_t60 are the canonical perceptual decay metric;
        # rt60-slope-fit above is retained for legacy callers only.
        'tail_t10': tail_times.get('t10'),
        'tail_t20': tail_times.get('t20'),
        'tail_t30': tail_times.get('t30'),
        'tail_t40': tail_times.get('t40'),
        'tail_t60': tail_times.get('t60'),
        # Per-band decay times (post-peak, noise-floor-aware).
        # Reveals frequency-dependent decay that mono t60 hides.
        'band_decays': band_decays,
        # New tail-character metrics:
        'tdc': tdc,
        'centroid_drift': centroid_drift,
        'late_tail': late_tail,
        'treble_ratio': treble_ratio,
        'bass_ratio': bass_ratio,
    }


# ---------------------------------------------------------------------------
# Loss
# ---------------------------------------------------------------------------

DEFAULT_WEIGHTS = {
    # rt60-slope-fit is unreliable on short tails — kept at 0 weight so
    # legacy callers don't break, but the perceptual decay match comes
    # from `tail_shape` (t30/t40/t60 noise-floor-aware times).
    'rt60': 0.0,
    'tail_shape': 6.0,       # raised from 4.0 — late-tail t60 was drifting +30% past gate
    'cent_50': 4.0,          # raised from 1.0 — early-bloom brightness was -35% past gate
    'cent_500': 1.5,         # raised from 1.0
    'spec_l1': 1.0,          # RMS-norm L1 captures EQ-shape match
    'stereo': 0.3,           # reduced — DPV figure-8 can't reach Lex's anti-correlated stereo within Width clamp
    'envelope': 0.5,         # env_p2p
    'tdc': 1.0,              # time-domain crest (specular vs wash)
    'cent_drift': 1.5,       # multi-window centroid drift (HF plunge)
    'late_tail': 0.5,        # late-window RMS match
    'treble_ratio': 0.5,     # high-freq energy ratio
    'bass_ratio': 0.5,       # low-freq energy ratio
    # Asymmetric 100–500 Hz low-mid penalty. Listening verdict on Bright
    # Hall / Vocal Hall / Ambience flagged "boomier and less clear" tails
    # that scalar spec_L1 averaging did not penalise — the optimizer was
    # buying overall EQ-shape fit at the cost of low-mid bloat. Cubic
    # weighting when DV exceeds VVV in this band (hot = boomy = bad);
    # quadratic when DV undershoots (cold = thin, recoverable). 5:1
    # hot:cold ratio matches the staged_tuner.py implementation.
    'bass_clarity': 1.0,
}


def compare(
    dv_path: str,
    vvv_path: str,
    weights: dict | None = None,
    decay_seek_s: float = 5.0,
) -> tuple[float, dict]:
    """
    Multi-metric loss between candidate DV render and reference VVV render.

    Returns (loss, breakdown). All component losses are normalized
    (squared relative error for ratio metrics, squared absolute for
    bounded ones) so weights are comparable.

    Spectral term uses RMS-normalized 1/3-octave dB so the optimizer
    cannot cheat by abusing the level multipliers as gain knobs.
    """
    w = {**DEFAULT_WEIGHTS, **(weights or {})}

    dv = compute_metrics(dv_path, decay_seek_s=decay_seek_s)
    vv = compute_metrics(vvv_path, decay_seek_s=decay_seek_s)

    # RT60 relative-error squared (skip if VVV RT60 is inf/<= 0).
    # Kept for legacy/diagnostic only — default weight is 0 because the
    # slope-fit RT60 on tails <2 s returns noise-floor artifacts.
    if math.isfinite(vv['rt60']) and vv['rt60'] > 0.05 and math.isfinite(dv['rt60']):
        rt60_term = ((dv['rt60'] - vv['rt60']) / vv['rt60']) ** 2
    else:
        rt60_term = 0.0

    # Tail shape: direct, noise-floor-aware decay-time match across
    # t30/t40/t60 (post-peak times to reach -30/-40/-60 dB). Weighted
    # squared relative error, clamped per point so a single "decayed below
    # noise floor" doesn't blow up the term. This is the PERCEPTUAL decay
    # match — replaces rt60-slope-fit which is broken on short tails.
    tail_shape_term = 0.0
    tail_pts = 0
    for k, point_clamp in (('tail_t30', 1.0), ('tail_t40', 1.5), ('tail_t60', 2.0)):
        dv_t = dv.get(k); vv_t = vv.get(k)
        if dv_t is None or vv_t is None or vv_t <= 0:
            continue
        rel = abs(dv_t - vv_t) / max(vv_t, 0.05)
        # Squared, clipped per-point so one bad axis cannot dominate.
        tail_shape_term += min(rel, point_clamp) ** 2
        tail_pts += 1
    tail_shape_term = tail_shape_term / max(tail_pts, 1)

    # Centroid relative-error squared. NaN values (noise-floor gated) skip
    # the term entirely so the optimizer doesn't chase noise.
    def _cent_term(d, v):
        if not math.isfinite(d) or not math.isfinite(v) or v <= 1.0:
            return None
        return ((d - v) / v) ** 2
    cent50_term = _cent_term(dv['cent_50'], vv['cent_50'])
    cent500_term = _cent_term(dv['cent_500'], vv['cent_500'])
    cent50_active = cent50_term is not None
    cent500_active = cent500_term is not None
    if cent50_term is None: cent50_term = 0.0
    if cent500_term is None: cent500_term = 0.0

    # RMS-normalized 1/3-octave dB L1.
    # Both vectors share the same band centers (same sr, same f_lo/f_hi).
    if dv['oct_db_norm'].shape == vv['oct_db_norm'].shape:
        spec_term = float(np.mean(np.abs(dv['oct_db_norm'] - vv['oct_db_norm'])))
    else:
        # Mismatched lengths shouldn't happen at the same sample rate
        # but guard anyway.
        n = min(len(dv['oct_db_norm']), len(vv['oct_db_norm']))
        spec_term = float(np.mean(np.abs(dv['oct_db_norm'][:n] - vv['oct_db_norm'][:n])))

    # Stereo absolute-difference squared (bounded [-1, 1]).
    stereo_term = (dv['stereo_corr'] - vv['stereo_corr']) ** 2

    # Envelope residual P2P relative-error, magnitude only (no sign).
    env_term = abs(dv['env_res_p2p'] - vv['env_res_p2p']) / max(vv['env_res_p2p'], 1.0)

    # ── New tail-character terms ──
    # TDC: squared relative error. High weight for "wash vs chatter" distinction.
    tdc_term = ((dv['tdc'] - vv['tdc']) / max(vv['tdc'], 1.0)) ** 2

    # Centroid drift: sum of squared relative errors across 50/150/300/500/1000/2000 ms windows.
    # Captures whether the tail darkens with the right shape (anchor's centroid plunge).
    # Each window is noise-floor gated; skip the term entirely if either side
    # returns None (segment dropped below -80 dBFS).
    cent_drift_term = 0.0
    cent_drift_active = 0
    for ms in [50, 150, 300, 500, 1000, 2000]:
        dvc = dv['centroid_drift'].get(ms)
        vvc = vv['centroid_drift'].get(ms)
        if dvc is None or vvc is None or vvc <= 1.0:
            continue
        cent_drift_term += ((dvc - vvc) / vvc) ** 2
        cent_drift_active += 1
    cent_drift_term = cent_drift_term / max(cent_drift_active, 1)

    # Late-tail RMS: squared dB-absolute error in the 1-2 / 2-3 / 3-4 s windows.
    # Bounded to keep noise-floor differences from dominating.
    late_term = 0.0
    late_count = 0
    for k in ['1s_2s', '2s_3s', '3s_4s']:
        dvl = dv['late_tail'].get(k, float('nan'))
        vvl = vv['late_tail'].get(k, float('nan'))
        if math.isfinite(dvl) and math.isfinite(vvl):
            # Clamp very low (noise-floor) values
            if dvl < -160 and vvl < -160:
                continue
            d = abs(dvl - vvl)
            late_term += (min(d, 30.0) / 30.0) ** 2  # normalize and clamp
            late_count += 1
    late_term = late_term / max(late_count, 1)

    # Treble / bass ratio: squared absolute error (bounded [0, 1]).
    treble_term = (dv['treble_ratio'] - vv['treble_ratio']) ** 2
    bass_term = (dv['bass_ratio'] - vv['bass_ratio']) ** 2

    # Bass-clarity asymmetric penalty (100–500 Hz).
    #   diff[i] = DV_dB_norm - VVV_dB_norm   (positive = DV hotter = boomy)
    #   diff > 0  → cubic non-linear penalty (heavy push away from bloat)
    #   diff <= 0 → quadratic linear-gradient penalty (DV thin, recoverable)
    # 5:1 hot:cold weighting forces optimizer off boomy bass multipliers and
    # oversized low-crossover regions even when scalar spec_L1 is flat.
    bass_clarity_term = 0.0
    bass_clarity_max_hot_db = 0.0
    fcs = dv.get('oct_centers')
    if fcs is not None and dv['oct_db_norm'].shape == vv['oct_db_norm'].shape:
        diff_oct = dv['oct_db_norm'] - vv['oct_db_norm']
        hot_acc = 0.0
        cold_acc = 0.0
        n_hot = 0
        n_cold = 0
        n = min(len(fcs), len(diff_oct))
        for i in range(n):
            fc = float(fcs[i])
            if 100.0 <= fc <= 500.0:
                d = float(diff_oct[i])
                if d > 0.0:
                    hot_acc += (d / 3.0) ** 3
                    n_hot += 1
                    if d > bass_clarity_max_hot_db:
                        bass_clarity_max_hot_db = d
                else:
                    cold_acc += (d / 4.0) ** 2
                    n_cold += 1
        if n_hot + n_cold > 0:
            bass_clarity_term = (5.0 * hot_acc / max(n_hot, 1)
                               + 1.0 * cold_acc / max(n_cold, 1))

    loss = (
        w['rt60']         * rt60_term
        + w['tail_shape']   * tail_shape_term
        + (w['cent_50']     * cent50_term  if cent50_active  else 0.0)
        + (w['cent_500']    * cent500_term if cent500_active else 0.0)
        + w['spec_l1']      * spec_term
        + w['stereo']       * stereo_term
        + w['envelope']     * env_term
        + w['tdc']          * tdc_term
        + w['cent_drift']   * cent_drift_term
        + w['late_tail']    * late_term
        + w['treble_ratio'] * treble_term
        + w['bass_ratio']   * bass_term
        + w['bass_clarity'] * bass_clarity_term
    )

    breakdown = {
        'loss': loss,
        'rt60_dv': dv['rt60'],
        'rt60_vvv': vv['rt60'],
        'tail_t30_dv': dv.get('tail_t30'),
        'tail_t30_vvv': vv.get('tail_t30'),
        'tail_t60_dv': dv.get('tail_t60'),
        'tail_t60_vvv': vv.get('tail_t60'),
        'cent50_dv': dv['cent_50'],
        'cent50_vvv': vv['cent_50'],
        'cent500_dv': dv['cent_500'],
        'cent500_vvv': vv['cent_500'],
        'stereo_dv': dv['stereo_corr'],
        'stereo_vvv': vv['stereo_corr'],
        'envP2P_dv': dv['env_res_p2p'],
        'envP2P_vvv': vv['env_res_p2p'],
        'spec_l1_db': spec_term,
        'rt60_term': rt60_term,
        'tail_shape_term': tail_shape_term,
        'cent50_term': cent50_term,
        'cent500_term': cent500_term,
        'stereo_term': stereo_term,
        'env_term': env_term,
        'bass_clarity_term': bass_clarity_term,
        'bass_clarity_max_hot_dB': bass_clarity_max_hot_db,
    }
    return loss, breakdown


# ---------------------------------------------------------------------------
# CLI for ad-hoc inspection
# ---------------------------------------------------------------------------

if __name__ == '__main__':
    import argparse

    ap = argparse.ArgumentParser(description="Compare DV render to VVV reference IR.")
    ap.add_argument('--dv', required=True, help='Candidate DV impulse WAV.')
    ap.add_argument('--vvv', required=True, help='Reference VVV impulse WAV.')
    ap.add_argument('--decay-seek', type=float, default=5.0,
                    help='Max tail-fit window in seconds (default 5.0).')
    args = ap.parse_args()

    loss, b = compare(args.dv, args.vvv, decay_seek_s=args.decay_seek)
    print(f"loss        = {loss:.6f}")
    def _fmt(v):
        return f"{v:.3f}s" if isinstance(v, (int, float)) else "none"
    print(f"Tail t30    DV={_fmt(b['tail_t30_dv'])}  VVV={_fmt(b['tail_t30_vvv'])}")
    print(f"Tail t60    DV={_fmt(b['tail_t60_dv'])}  VVV={_fmt(b['tail_t60_vvv'])}  shape_term={b['tail_shape_term']:.5f}")
    print(f"RT60(legacy) DV={b['rt60_dv']:.3f}s   VVV={b['rt60_vvv']:.3f}s   term={b['rt60_term']:.5f}")
    print(f"Cent 50ms   DV={b['cent50_dv']:.0f}Hz  VVV={b['cent50_vvv']:.0f}Hz  term={b['cent50_term']:.5f}")
    print(f"Cent 500ms  DV={b['cent500_dv']:.0f}Hz  VVV={b['cent500_vvv']:.0f}Hz term={b['cent500_term']:.5f}")
    print(f"Stereo r    DV={b['stereo_dv']:+.3f}    VVV={b['stereo_vvv']:+.3f}    term={b['stereo_term']:.5f}")
    print(f"Env P2P     DV={b['envP2P_dv']:.2f}dB  VVV={b['envP2P_vvv']:.2f}dB  term={b['env_term']:.5f}")
    print(f"Spec L1 (norm 1/3-oct dB) = {b['spec_l1_db']:.3f}")
