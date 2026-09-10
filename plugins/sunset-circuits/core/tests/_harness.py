"""Shared helpers for the Multi-Synth core validation gates."""
import os
import shutil
import subprocess
import tempfile
import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
BIN = os.path.join(HERE, "build", "render_test")


def scratch_dir(name):
    """Per-process scratch directory under the system temp dir.

    A FIXED path (the old /tmp/msynth_gate) is shared by every concurrent run
    and by every user on the box, so two suites running at once overwrite each
    other's renders and a root-owned leftover makes the gates unrunnable for
    everyone else. Keyed on uid+pid, and wiped on entry the same way
    user_preset_test.cpp does, so a recycled pid cannot serve stale WAVs while
    a finished run's renders survive for post-mortem.
    """
    d = os.path.join(tempfile.gettempdir(),
                     f"{name}_{os.getuid()}_{os.getpid()}")
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d, exist_ok=True)
    return d


OUT = scratch_dir("msynth_gate")


def render(mode, note, seconds, osfactor, name, **params):
    """Render a note and return (samplerate, stereo float array [N,2])."""
    path = os.path.join(OUT, name + ".wav")
    args = [BIN, str(mode), str(note), str(seconds), str(osfactor), path]
    # Repeatable keys (setat, notifyat, noteon/noteoff) can't be expressed as
    # duplicate dict entries, so any list/tuple value is emitted as repeated args.
    for k, v in params.items():
        if isinstance(v, (list, tuple)):
            for item in v:
                args.append(f"{k}={item}")
        else:
            args.append(f"{k}={v}")
    # Capture stderr so a failing render surfaces the binary's diagnostics
    # instead of a bare CalledProcessError; 120 s timeout matches _acid.py.
    try:
        subprocess.run(args, check=True, capture_output=True, timeout=120)
    except subprocess.CalledProcessError as e:
        raise RuntimeError(
            f"render_test failed (rc={e.returncode}): "
            f"{e.stderr.decode(errors='replace').strip()}") from e
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


def peak_envelope(sig, sr, release_s):
    """Instant-attack / exponential-release peak envelope, vectorised.

    env[i] = max(|sig[i]|, env[i-1] * rel), i.e. env[i] = max_j |sig[j]| *
    rel**(i-j). The per-sample Python loop this replaces dominated the runtime of
    every onset-detecting gate, so this uses the standard trick: divide by
    rel**i, running-max, multiply back. rel**i underflows over a long render, so
    the signal is walked in chunks with the previous chunk's tail carried in.

    NOTE, one deliberate semantic correction. The loop this replaces was written
    `e = a[i] if a[i] > e else e * rel`, which compares the new sample against
    the UN-decayed previous value and so discards a sample that lands in
    (e*rel, e] -- returning e*rel where the follower should have returned a[i].
    The two differ by at most a factor of rel (<= 0.7% at the release times used
    here) and every gate that consumes this reports the same onset times to the
    last printed digit either way (arp 0.5/0.04/95.5 ms, seq 0.02/0.06 ms --
    identical before and after), so this is a correctness tidy, not a
    recalibration.
    """
    a = np.abs(np.asarray(sig, dtype=np.float64))
    rel = float(np.exp(-1.0 / (release_s * sr)))
    env = np.empty_like(a)
    carry = 0.0
    chunk = 50000                       # rel**50000 stays well inside float64
    for s in range(0, len(a), chunk):
        seg = a[s:s + chunk]
        w = rel ** np.arange(len(seg))
        e = w * np.maximum.accumulate(seg / w)
        np.maximum(e, carry * rel * w, out=e)
        env[s:s + chunk] = e
        carry = float(e[-1])
    return env


def rising_edges(env, sr, thresh_frac=0.3, min_gap_s=0.1):
    """Onset times (s) where `env` crosses thresh_frac*max, min_gap_s apart."""
    thr = thresh_frac * float(np.max(env))
    min_gap = int(min_gap_s * sr)
    cross = np.flatnonzero((env[1:] > thr) & (env[:-1] <= thr)) + 1
    onsets, last = [], -min_gap
    for i in cross:
        if i - last >= min_gap:
            onsets.append(i)
            last = i
    return np.array(onsets) / sr


def has_nan_inf(x):
    return bool(np.any(~np.isfinite(x)))


def dc_db(sig):
    dc = abs(np.mean(sig))
    rms = np.sqrt(np.mean(sig ** 2)) + 1e-20
    return 20.0 * np.log10(dc / rms + 1e-20)
