#!/usr/bin/env python3
"""
Generate reverb-optimized test signals for DuskVerb vs VintageVerb comparison.

Signals are designed to reveal specific reverb quality characteristics:
- Impulse: full IR capture (RT60, EDC, stereo, modal analysis)
- Log sweep: high-SNR IR via deconvolution
- Noise burst: broadband decay character
- Snare hit: transient modal excitation
- Tone burst 1500 Hz: targeted ringing probe
- Multitone burst: frequency-selective ringing map

Usage:
    python3 generate_test_signals.py              # Generate all signals
    python3 generate_test_signals.py --outdir .   # Custom output directory
"""

import argparse
import os
import numpy as np
import soundfile as sf

from config import SAMPLE_RATE, SIGNAL_DURATION

TAIL_DURATION = SIGNAL_DURATION  # seconds of silence after excitation


def make_impulse(duration=None):
    """Single-sample Dirac delta — captures the full impulse response.

    Args:
        duration: tail capture in seconds.  Defaults to TAIL_DURATION (config.py).
                  Use longer values for very long RT60 modes (e.g. 40s for 55s RT60).
    """
    dur = duration if duration is not None else TAIL_DURATION
    n = int(SAMPLE_RATE * dur)
    sig = np.zeros(n, dtype=np.float32)
    sig[0] = 1.0
    return sig


def make_log_sweep(duration=4.0, f_start=20, f_end=20000):
    """Logarithmic sine sweep + inverse filter for deconvolution.

    Returns (sweep_with_tail, inverse_filter).
    The sweep is followed by TAIL_DURATION of silence for the reverb tail.
    Convolving the plugin output with inverse_filter yields a high-SNR IR.
    """
    n_sweep = int(SAMPLE_RATE * duration)
    n_total = n_sweep + int(SAMPLE_RATE * TAIL_DURATION)
    t = np.arange(n_sweep, dtype=np.float64) / SAMPLE_RATE

    # Logarithmic sweep
    R = np.log(f_end / f_start)
    sweep = np.sin(2 * np.pi * f_start * duration / R * (np.exp(t * R / duration) - 1))

    # Amplitude envelope: compensate for log sweep's lower energy at low freqs
    # (each octave gets equal time, so HF octaves have more energy per Hz)
    envelope = np.exp(-t * R / (2 * duration))
    sweep *= envelope

    # Fade in/out to avoid clicks
    fade = int(SAMPLE_RATE * 0.01)
    sweep[:fade] *= np.linspace(0, 1, fade)
    sweep[-fade:] *= np.linspace(1, 0, fade)

    # Normalize to -3dBFS
    sweep *= 0.707 / max(np.max(np.abs(sweep)), 1e-10)

    # Build full signal with tail
    sig = np.zeros(n_total, dtype=np.float32)
    sig[:n_sweep] = sweep.astype(np.float32)

    # Inverse filter: time-reversed sweep with amplitude compensation
    inverse = sweep[::-1].copy()
    # Compensate for the amplitude envelope
    inv_envelope = np.exp(t * R / (2 * duration))
    inverse *= inv_envelope
    inverse *= 1.0 / max(np.max(np.abs(inverse)), 1e-10)
    inverse = inverse.astype(np.float32)

    return sig, inverse


def make_noise_burst(burst_ms=100):
    """Short burst of pink noise — tests broadband response.

    Pink noise has equal energy per octave, matching how we hear.
    """
    n = int(SAMPLE_RATE * TAIL_DURATION)
    burst_len = int(SAMPLE_RATE * burst_ms / 1000)

    # Generate white noise
    rng = np.random.default_rng(42)
    white = rng.standard_normal(burst_len).astype(np.float32)

    # Pink filter (Voss-McCartney IIR approximation)
    from scipy.signal import lfilter
    b = np.array([0.049922035, -0.095993537, 0.050612699, -0.004709510], dtype=np.float32)
    a = np.array([1.0, -2.494956002, 2.017265875, -0.522189400], dtype=np.float32)
    pink = lfilter(b, a, white).astype(np.float32)

    # Normalize
    pink *= 0.5 / max(np.max(np.abs(pink)), 1e-10)

    # Envelope (5ms fade in/out)
    env = np.ones(burst_len, dtype=np.float32)
    fade = int(SAMPLE_RATE * 0.005)
    env[:fade] = np.linspace(0, 1, fade, dtype=np.float32)
    env[-fade:] = np.linspace(1, 0, fade, dtype=np.float32)
    pink *= env

    sig = np.zeros(n, dtype=np.float32)
    sig[:burst_len] = pink
    return sig


def make_snare_transient():
    """Synthetic snare: 200Hz body + 5kHz noise burst, ~10ms attack, ~50ms decay.

    Sharp transient that excites modal resonances effectively.
    """
    n = int(SAMPLE_RATE * TAIL_DURATION)
    burst_len = int(SAMPLE_RATE * 0.05)
    t = np.arange(burst_len, dtype=np.float32) / SAMPLE_RATE

    env = np.exp(-t / 0.012)
    body = 0.6 * np.sin(2 * np.pi * 200 * t) * env

    rng = np.random.default_rng(123)
    noise = 0.4 * rng.standard_normal(burst_len).astype(np.float32) * env

    burst = body + noise
    sig = np.zeros(n, dtype=np.float32)
    sig[:burst_len] = burst
    return sig


def make_tone_burst(freq=1500, burst_ms=50):
    """Narrowband tone burst at specified frequency.

    Used to probe specific resonant frequencies in the reverb.
    The 1500 Hz burst directly tests the suspected ringing from
    converging delay-line harmonics in Room mode.
    """
    n = int(SAMPLE_RATE * TAIL_DURATION)
    burst_len = int(SAMPLE_RATE * burst_ms / 1000)
    t = np.arange(burst_len, dtype=np.float32) / SAMPLE_RATE

    tone = np.sin(2 * np.pi * freq * t).astype(np.float32)

    # Tukey window to avoid spectral splatter
    from scipy.signal.windows import tukey
    tone *= tukey(burst_len, alpha=0.2).astype(np.float32)
    tone *= 0.5  # -6dBFS peak

    sig = np.zeros(n, dtype=np.float32)
    sig[:burst_len] = tone
    return sig


def make_multitone_burst(freqs=(500, 1000, 1500, 2000, 4000), burst_ms=50):
    """Multiple simultaneous tone bursts for frequency-selective ringing analysis.

    Each frequency at -12dBFS so the sum stays below 0dBFS.
    After processing, examine each frequency's decay independently
    to create a ringing map.
    """
    n = int(SAMPLE_RATE * TAIL_DURATION)
    burst_len = int(SAMPLE_RATE * burst_ms / 1000)
    t = np.arange(burst_len, dtype=np.float32) / SAMPLE_RATE

    from scipy.signal.windows import tukey
    window = tukey(burst_len, alpha=0.2).astype(np.float32)

    amplitude = 0.25 / len(freqs)  # Scale so sum stays reasonable
    burst = np.zeros(burst_len, dtype=np.float32)
    for freq in freqs:
        burst += amplitude * np.sin(2 * np.pi * freq * t).astype(np.float32)
    burst *= window

    sig = np.zeros(n, dtype=np.float32)
    sig[:burst_len] = burst
    return sig


def save_stereo(path, mono_signal, sr):
    """Save mono signal as stereo dual-mono (matching LA-2A comparison convention)."""
    stereo = np.stack([mono_signal, mono_signal], axis=-1)
    sf.write(path, stereo, sr, subtype="FLOAT")


def main():
    parser = argparse.ArgumentParser(description="Generate reverb test signals")
    parser.add_argument("--outdir", type=str,
                        default=os.path.join(os.path.dirname(__file__), "test_signals"),
                        help="Output directory")
    parser.add_argument("--sr", type=int, default=SAMPLE_RATE, help="Sample rate")
    args = parser.parse_args()

    sr = args.sr
    os.makedirs(args.outdir, exist_ok=True)

    signals = [
        ("impulse.wav", make_impulse, "Dirac delta impulse"),
        ("noise_burst.wav", make_noise_burst, "100ms pink noise burst"),
        ("snare_hit.wav", make_snare_transient, "Synthetic snare transient"),
        ("tone_burst_1500hz.wav", lambda: make_tone_burst(1500), "1500 Hz ringing probe"),
        ("multitone_burst.wav", make_multitone_burst, "5-freq ringing map"),
    ]

    print(f"Generating test signals at {sr} Hz...")
    print(f"Output: {args.outdir}/\n")

    for filename, gen_func, desc in signals:
        sig = gen_func()
        path = os.path.join(args.outdir, filename)
        save_stereo(path, sig, sr)
        dur = len(sig) / sr
        print(f"  {filename:30s} {dur:.1f}s  {desc}")

    # Log sweep + inverse filter (special case: two files)
    sweep, inverse = make_log_sweep()
    sweep_path = os.path.join(args.outdir, "log_sweep.wav")
    inv_path = os.path.join(args.outdir, "log_sweep_inverse.wav")
    save_stereo(sweep_path, sweep, sr)
    save_stereo(inv_path, inverse, sr)
    print(f"  {'log_sweep.wav':30s} {len(sweep)/sr:.1f}s  Log sine sweep 20-20kHz")
    print(f"  {'log_sweep_inverse.wav':30s} {len(inverse)/sr:.1f}s  Inverse filter for deconvolution")

    print(f"\nDone. {len(signals) + 2} files generated.")


if __name__ == "__main__":
    main()
