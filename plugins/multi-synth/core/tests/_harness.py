"""Shared helpers for the Multi-Synth core validation gates."""
import os
import subprocess
import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
BIN = os.path.join(HERE, "build", "render_test")
OUT = "/tmp/msynth_gate"
os.makedirs(OUT, exist_ok=True)


def render(mode, note, seconds, osfactor, name, **params):
    """Render a note and return (samplerate, stereo float array [N,2])."""
    path = os.path.join(OUT, name + ".wav")
    args = [BIN, str(mode), str(note), str(seconds), str(osfactor), path]
    for k, v in params.items():
        args.append(f"{k}={v}")
    subprocess.run(args, check=True, stderr=subprocess.DEVNULL)
    x, sr = sf.read(path, always_2d=True)
    return sr, x


def peak_hz(sig, sr, f_lo=20.0):
    """FFT peak frequency with parabolic interpolation (sub-bin accuracy)."""
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


def cents(f, ref):
    return 1200.0 * np.log2(f / ref)


def rms_envelope(sig, sr, win_ms=5.0):
    """Short-window RMS envelope and its time axis."""
    win = max(1, int(sr * win_ms / 1000.0))
    n = len(sig) // win
    env = np.array([np.sqrt(np.mean(sig[i * win:(i + 1) * win] ** 2)) for i in range(n)])
    t = (np.arange(n) + 0.5) * win / sr
    return t, env


def has_nan_inf(x):
    return bool(np.any(~np.isfinite(x)))


def dc_db(sig):
    dc = abs(np.mean(sig))
    rms = np.sqrt(np.mean(sig ** 2)) + 1e-20
    return 20.0 * np.log10(dc / rms + 1e-20)
