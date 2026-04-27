#!/usr/bin/env python3
"""
Analyze a rendered DuskVerb WAV for tail artefacts.

Two analyses:
  1. Impulse response — autocorrelation of the envelope to find any periodic
     repetitions in the tail (the "single delay" the user hears).
  2. Noise burst response — once the input stops, measure modulation depth
     by tracking the RMS envelope's variation over time.

Usage:
  python3 analyze_render.py <impulse_wav> [<noiseburst_wav>]
"""

import sys
from pathlib import Path

import numpy as np
from scipy.io import wavfile
from scipy.signal import hilbert


def load_wav(path):
    """Load a WAV file (handles both PCM and 32-bit float)."""
    sr, raw = wavfile.read(str(path))
    if raw.dtype == np.float32 or raw.dtype == np.float64:
        samples = raw.astype(np.float32)
    elif raw.dtype == np.int16:
        samples = raw.astype(np.float32) / 32768.0
    elif raw.dtype == np.int32:
        samples = raw.astype(np.float32) / 2147483648.0
    else:
        raise ValueError(f"Unsupported dtype: {raw.dtype}")
    if samples.ndim == 1:
        samples = samples[np.newaxis, :]
    else:
        samples = samples.T  # (channels, n_samples)
    return samples, sr


def envelope(signal):
    """Hilbert-transform envelope, then short-window smoothing."""
    analytic = hilbert(signal)
    env = np.abs(analytic)
    # 1 ms moving average
    win = max(1, int(0.001 * sr_global))
    return np.convolve(env, np.ones(win) / win, mode='same')


def find_first_significant_event(env, sr, threshold_db=-40.0, ignore_first_ms=5.0):
    """Find the first sample where envelope rises above threshold (after a small
    initial silence). Returns sample index and amplitude (linear)."""
    peak = env.max()
    thresh_lin = peak * (10 ** (threshold_db / 20))
    skip = int(ignore_first_ms / 1000 * sr)
    above = np.where(env[skip:] > thresh_lin)[0]
    if len(above) == 0:
        return None, None
    return above[0] + skip, env[above[0] + skip]


def analyze_impulse(wav_path):
    """Find autocorrelation peaks in the impulse response tail to identify
    discrete delay artefacts."""
    samples, sr = load_wav(wav_path)
    global sr_global
    sr_global = sr

    print(f"\n=== Impulse Response Analysis: {wav_path.name} ===")
    print(f"Sample rate: {sr} Hz")
    print(f"Duration:    {samples.shape[1] / sr:.2f} s")
    print(f"Channels:    {samples.shape[0]}")

    # Use mid (L+R)/2 for analysis. load_wav returns shape (1, n) for mono
    # files; downmix degrades gracefully by treating the single channel as
    # both L and R (no IndexError on samples[1]).
    left  = samples[0]
    right = samples[1] if samples.shape[0] > 1 else samples[0]
    mid = (left + right) * 0.5

    env = envelope(mid)
    peak_db = 20 * np.log10(env.max() + 1e-12)
    print(f"Peak level:  {peak_db:.2f} dBFS")

    # Find onset of the wet tail — skip the first 5ms which contains the
    # impulse + immediate input diffusion.
    onset_idx, onset_amp = find_first_significant_event(env, sr, threshold_db=-50.0, ignore_first_ms=5.0)
    if onset_idx is not None:
        print(f"Tail onset:  {onset_idx / sr * 1000:.2f} ms ({20*np.log10(onset_amp + 1e-12):.1f} dB)")

    # Look at the tail: 50ms to 1.5s after start
    t_start = int(0.05 * sr)
    t_end   = min(int(1.5 * sr), len(env))
    tail = env[t_start:t_end]
    n = len(tail)

    # Compute autocorrelation of the envelope (DC-removed)
    tail_dc = tail - tail.mean()
    ac = np.correlate(tail_dc, tail_dc, mode='full')[n - 1:]
    ac /= ac[0]  # normalize so lag-0 = 1

    # Find peaks in the autocorrelation between 5ms and 500ms lag
    min_lag = int(0.005 * sr)
    max_lag = int(0.5 * sr)
    candidate = ac[min_lag:max_lag]

    # Sliding window peak-pick: a peak must exceed both neighbours by some
    # margin and be > 0.05 (i.e. >5% similarity to lag-0).
    peaks = []
    for i in range(50, len(candidate) - 50):
        if candidate[i] > 0.05 and candidate[i] >= candidate[i - 50:i + 50].max() - 1e-9:
            peaks.append((i + min_lag, candidate[i]))

    # De-duplicate peaks within 5ms of each other (keep highest)
    cleaned = []
    last_idx = -10000
    last_val = -1
    min_separation = int(0.005 * sr)
    for idx, val in sorted(peaks, key=lambda x: x[0]):
        if idx - last_idx < min_separation:
            if val > last_val:
                cleaned[-1] = (idx, val)
                last_idx = idx
                last_val = val
        else:
            cleaned.append((idx, val))
            last_idx = idx
            last_val = val

    print(f"\n--- Autocorrelation peaks in tail envelope (50ms - 1.5s window) ---")
    print(f"  These indicate periodic events in the tail. A SINGLE strong peak")
    print(f"  = single discrete echo. Multiple peaks at integer multiples of a")
    print(f"  base period = loop circulation. No peaks = smooth wash.")
    print()
    if not cleaned:
        print("  (no significant peaks found — tail is smooth)")
    else:
        print(f"  {'Lag (samples)':>14}  {'Lag (ms)':>10}  {'Strength':>10}")
        for idx, val in cleaned[:15]:
            print(f"  {idx:>14d}  {idx / sr * 1000:>10.2f}  {val:>10.4f}")

    return cleaned, sr


def analyze_noise_burst(wav_path):
    """Track RMS envelope after the input stops. Modulation depth shows up as
    fluctuation in the RMS over the tail."""
    samples, sr = load_wav(wav_path)
    global sr_global
    sr_global = sr

    print(f"\n=== Noise Burst Tail Analysis: {wav_path.name} ===")

    # Same mono-safe downmix as analyze_impulse.
    left  = samples[0]
    right = samples[1] if samples.shape[0] > 1 else samples[0]
    mid = (left + right) * 0.5

    # The burst is 100ms. Pre-delay is 35ms. So the WET tail starts becoming
    # visible around 100+35 = 135ms. The PURE tail (input ended) begins at
    # 100ms input end + ~50ms predelay grace = 150ms onwards.
    tail_start = int(0.15 * sr)
    tail_end   = int(2.5 * sr)
    if tail_end > len(mid):
        tail_end = len(mid)
    tail = mid[tail_start:tail_end]

    # 10ms RMS frames
    frame = max(1, int(0.010 * sr))
    n_frames = len(tail) // frame
    rms = np.array([np.sqrt(np.mean(tail[i*frame:(i+1)*frame]**2) + 1e-12)
                    for i in range(n_frames)])

    # Convert to dB, normalised to peak
    rms_db = 20 * np.log10(rms / (rms.max() + 1e-12) + 1e-12)

    # Modulation depth = std deviation of detrended RMS (in dB) over a
    # sliding window. A clean exponential decay has near-zero deviation
    # from the trend; a heavily-modulated tail has visible ripples.
    # Detrend by subtracting a linear fit (since the decay is linear in dB).
    t_axis = np.arange(n_frames)
    coeffs = np.polyfit(t_axis, rms_db, 1)
    fit = np.polyval(coeffs, t_axis)
    residual = rms_db - fit

    print(f"Tail decay slope:   {coeffs[0] / (frame / sr):.2f} dB/sec "
          f"(expected ~{-60.0/3.8:.2f} for 3.8s RT60)")
    print(f"Modulation residual std: {residual.std():.2f} dB (one std around trend)")
    print(f"Modulation peak-peak:    {residual.max() - residual.min():.2f} dB")
    print()
    print("Interpretation:")
    print("  > 1.5 dB std = strong modulation (chorus/wobble audible)")
    print("  0.5-1.5 dB   = moderate modulation (subtle movement)")
    print("  < 0.5 dB     = essentially static tail (modal locking risk)")


def main():
    if len(sys.argv) < 2:
        # Use defaults from output dir
        out = Path(__file__).parent / "output"
        impulse = out / "LushDarkHall_impulse.wav"
        noise = out / "LushDarkHall_noiseburst.wav"
    else:
        impulse = Path(sys.argv[1])
        noise = Path(sys.argv[2]) if len(sys.argv) > 2 else None

    if not impulse.exists():
        print(f"ERROR: impulse WAV not found: {impulse}")
        sys.exit(1)

    analyze_impulse(impulse)

    if noise and noise.exists():
        analyze_noise_burst(noise)


if __name__ == "__main__":
    main()
