"""Shared helpers for the Prism FM engine validation gates."""
import os
import subprocess
import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
BIN = os.path.join(HERE, "build", "fm_test")
OUT = "/tmp/prism_fm_gate"
os.makedirs(OUT, exist_ok=True)


def render(algo, note, seconds, name, **params):
    """Render one note through the FM engine; return (sr, mono float array)."""
    path = os.path.join(OUT, name + ".wav")
    args = [BIN, str(algo), str(note), str(seconds), path]
    for k, v in params.items():
        args.append(f"{k}={v}")
    subprocess.run(args, check=True, stderr=subprocess.DEVNULL)
    x, sr = sf.read(path, always_2d=True)
    return sr, x[:, 0]


def spectrum(sig, sr):
    """Windowed magnitude spectrum and its frequency axis."""
    w = np.hanning(len(sig))
    X = np.abs(np.fft.rfft(sig * w))
    f = np.fft.rfftfreq(len(sig), 1.0 / sr)
    return f, X


def thd_pct(sig, sr, f0):
    """Total harmonic distortion (%) relative to the fundamental at f0."""
    f, X = spectrum(sig, sr)

    def bin_energy(fc):
        tol = f0 * 0.5
        m = (f > fc - tol) & (f < fc + tol)
        return np.max(X[m]) if np.any(m) else 0.0

    fund = bin_energy(f0)
    if fund <= 0.0:
        return float("nan")
    harm = 0.0
    k = 2
    while k * f0 < sr * 0.45:
        harm += bin_energy(k * f0) ** 2
        k += 1
    return 100.0 * np.sqrt(harm) / fund


def spectral_centroid(sig, sr, f_lo=20.0):
    """Amplitude-weighted spectral centroid (Hz)."""
    f, X = spectrum(sig, sr)
    m = f >= f_lo
    f, X = f[m], X[m]
    s = np.sum(X)
    return float(np.sum(f * X) / s) if s > 0 else 0.0


def has_nan_inf(x):
    return bool(np.any(~np.isfinite(x)))
