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

def compute_metrics(wav_path: str, decay_seek_s: float = 5.0) -> dict:
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

    # Tail envelope + slope fit.
    sm = _envelope(mono, sr, win_ms=10.0)
    t0 = int(0.05 * sr)
    t1 = min(int(decay_seek_s * sr), len(sm))
    seg = sm[t0:t1]
    slope, residual = _slope_fit(seg, sr)
    rt60 = -60.0 / slope if slope < -1.0 else float('inf')

    # Centroids in two windows.
    def _seg(a_s, b_s):
        a = int(a_s * sr)
        b = min(int(b_s * sr), len(mono))
        return mono[a:b]

    cent_50 = _spectral_centroid(_seg(0.05, 0.50), sr)
    cent_500 = _spectral_centroid(_seg(0.50, 1.50), sr) if len(mono) > int(0.50 * sr) + 16 else cent_50

    # Stereo waveform correlation over tail.
    Lt = L[t0:t1]
    Rt = R[t0:t1]
    stereo = _stereo_waveform_corr(Lt, Rt) if len(Lt) > 16 else 0.0

    # 1/3-octave magnitude of the entire signal, RMS-normalized.
    oct_centers, oct_db = _third_octave_magnitude_db(mono, sr)
    oct_db_norm = _rms_normalize_db(oct_db)

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
    }


# ---------------------------------------------------------------------------
# Loss
# ---------------------------------------------------------------------------

DEFAULT_WEIGHTS = {
    'rt60': 1.0,
    'cent_50': 1.0,
    'cent_500': 0.5,
    'spec_l1': 0.5,
    'stereo': 0.5,
    'envelope': 0.3,
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
    if math.isfinite(vv['rt60']) and vv['rt60'] > 0.05 and math.isfinite(dv['rt60']):
        rt60_term = ((dv['rt60'] - vv['rt60']) / vv['rt60']) ** 2
    else:
        rt60_term = 0.0

    # Centroid relative-error squared.
    cent50_term = ((dv['cent_50'] - vv['cent_50']) / max(vv['cent_50'], 1.0)) ** 2
    cent500_term = ((dv['cent_500'] - vv['cent_500']) / max(vv['cent_500'], 1.0)) ** 2

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

    loss = (
        w['rt60']     * rt60_term
        + w['cent_50']  * cent50_term
        + w['cent_500'] * cent500_term
        + w['spec_l1']  * spec_term
        + w['stereo']   * stereo_term
        + w['envelope'] * env_term
    )

    breakdown = {
        'loss': loss,
        'rt60_dv': dv['rt60'],
        'rt60_vvv': vv['rt60'],
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
        'cent50_term': cent50_term,
        'cent500_term': cent500_term,
        'stereo_term': stereo_term,
        'env_term': env_term,
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
    print(f"RT60        DV={b['rt60_dv']:.3f}s   VVV={b['rt60_vvv']:.3f}s   term={b['rt60_term']:.5f}")
    print(f"Cent 50ms   DV={b['cent50_dv']:.0f}Hz  VVV={b['cent50_vvv']:.0f}Hz  term={b['cent50_term']:.5f}")
    print(f"Cent 500ms  DV={b['cent500_dv']:.0f}Hz  VVV={b['cent500_vvv']:.0f}Hz term={b['cent500_term']:.5f}")
    print(f"Stereo r    DV={b['stereo_dv']:+.3f}    VVV={b['stereo_vvv']:+.3f}    term={b['stereo_term']:.5f}")
    print(f"Env P2P     DV={b['envP2P_dv']:.2f}dB  VVV={b['envP2P_vvv']:.2f}dB  term={b['env_term']:.5f}")
    print(f"Spec L1 (norm 1/3-oct dB) = {b['spec_l1_db']:.3f}")
