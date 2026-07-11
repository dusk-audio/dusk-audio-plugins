"""Shared helpers for the Acid engine validation gates.

Drives the standalone acid_test binary (built from AcidEngine.hpp directly).
"""
import os
import subprocess
import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
BIN = os.path.join(HERE, "build", "acid_test")
OUT = "/tmp/acid_gate"
os.makedirs(OUT, exist_ok=True)


def render(cmd, name, **params):
    """Run an acid_test subcommand; return (samplerate, mono float array)."""
    path = os.path.join(OUT, name + ".wav")
    args = [BIN, cmd, path]
    for k, v in params.items():
        args.append(f"{k}={v}")
    res = subprocess.run(args, timeout=120, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"acid_test failed ({res.returncode}): {res.stderr.strip()}")
    x, sr = sf.read(path)
    if x.ndim > 1:
        x = x[:, 0]
    return sr, x


def rolloff_slope_db_oct(sig, sr, f_lo, f_hi):
    """Fit dB/octave slope of the magnitude spectrum between f_lo..f_hi."""
    w = np.hanning(len(sig))
    X = np.abs(np.fft.rfft(sig * w)) + 1e-12
    f = np.fft.rfftfreq(len(sig), 1.0 / sr)
    mask = (f >= f_lo) & (f <= f_hi)
    lf = np.log2(f[mask])
    ldb = 20.0 * np.log10(X[mask])
    # Least-squares line: slope is dB per octave (x already in log2 Hz).
    A = np.vstack([lf, np.ones_like(lf)]).T
    slope, _ = np.linalg.lstsq(A, ldb, rcond=None)[0]
    return slope


def spectral_centroid(sig, sr):
    w = np.hanning(len(sig))
    X = np.abs(np.fft.rfft(sig * w))
    f = np.fft.rfftfreq(len(sig), 1.0 / sr)
    s = np.sum(X)
    if s <= 0:
        return 0.0
    return float(np.sum(f * X) / s)


def peak_hz(sig, sr, f_lo=20.0):
    w = np.hanning(len(sig))
    X = np.abs(np.fft.rfft(sig * w))
    f = np.fft.rfftfreq(len(sig), 1.0 / sr)
    lo = np.searchsorted(f, f_lo)
    X[:lo] = 0.0
    k = int(np.argmax(X))
    if 1 <= k < len(X) - 1:
        a, b, c = np.log(X[k - 1] + 1e-20), np.log(X[k] + 1e-20), np.log(X[k + 1] + 1e-20)
        delta = 0.5 * (a - c) / (a - 2 * b + c + 1e-20)
    else:
        delta = 0.0
    return (k + delta) * sr / len(sig)


def peak_track(sig, sr, win_ms=15.0, hop_ms=2.0, f_lo=40.0):
    """Windowed dominant spectral peak per window — NOT a fundamental estimator.

    Returns the strongest FFT bin per window; a harmonic can dominate the
    fundamental, so do not treat the result as an f0 track.
    """
    win = max(64, int(sr * win_ms / 1000.0))
    hop = max(1, int(sr * hop_ms / 1000.0))
    times, freqs = [], []
    w = np.hanning(win)
    for start in range(0, len(sig) - win, hop):
        seg = sig[start:start + win]
        if np.sqrt(np.mean(seg ** 2)) < 1e-4:
            continue
        f = peak_hz(seg * 1.0, sr, f_lo=f_lo)
        times.append((start + win / 2) / sr)
        freqs.append(f)
    return np.array(times), np.array(freqs)


def f0_zerocross(sig, sr):
    """Instantaneous f0 from upward zero crossings (parabolic-subsample).

    Robust at low pitch (no FFT window smear). Use with a near-sine source (low
    cutoff) so there is exactly one upward crossing per period.
    """
    s = sig.astype(np.float64)
    times, freqs = [], []
    last = None
    for i in range(1, len(s)):
        if s[i - 1] < 0.0 <= s[i]:
            # sub-sample crossing position via linear interpolation
            frac = -s[i - 1] / (s[i] - s[i - 1] + 1e-20)
            tc = (i - 1 + frac) / sr
            if last is not None:
                dt = tc - last
                if dt > 0:
                    times.append(0.5 * (tc + last))
                    freqs.append(1.0 / dt)
            last = tc
    return np.array(times), np.array(freqs)


def has_nan_inf(x):
    return bool(np.any(~np.isfinite(x)))
