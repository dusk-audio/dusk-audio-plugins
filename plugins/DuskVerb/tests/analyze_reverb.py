#!/usr/bin/env python3
"""
Comprehensive DuskVerb reverb quality analysis.

Loads the DuskVerb AU via pedalboard, processes test signals through each
algorithm, and analyzes: echo density, spectral decay, modal ringing,
stereo width, early reflection quality, and tail smoothness.

Usage:
    python3 analyze_reverb.py                    # Analyze all algorithms
    python3 analyze_reverb.py --algo Room        # Analyze one algorithm
    python3 analyze_reverb.py --algo Room --save # Save WAV outputs for listening
"""

import argparse
import os
import sys
import numpy as np
import soundfile as sf
from pedalboard import load_plugin

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------
SAMPLE_RATE = 48000
PLUGIN_PATH = os.path.expanduser(
    "~/Library/Audio/Plug-Ins/Components/DuskVerb.component")

ALGORITHMS = ["Plate", "Hall", "Chamber", "Room", "Ambient"]

# Reverb settings per algorithm for analysis (100% wet, moderate decay)
ALGO_SETTINGS = {
    "Plate":   dict(decay_time=2.0, size=0.65, diffusion=0.80, treble_multiply=0.70,
                    bass_multiply=1.0, crossover=1200, mod_depth=0.25, mod_rate=0.6,
                    early_ref_level=0.0, early_ref_size=0.0, pre_delay=0.0,
                    lo_cut=20, hi_cut=20000, width=1.0),
    "Hall":    dict(decay_time=2.5, size=0.70, diffusion=0.70, treble_multiply=0.58,
                    bass_multiply=1.15, crossover=900, mod_depth=0.40, mod_rate=0.8,
                    early_ref_level=0.55, early_ref_size=0.50, pre_delay=30.0,
                    lo_cut=20, hi_cut=20000, width=1.0),
    "Chamber": dict(decay_time=1.8, size=0.60, diffusion=0.70, treble_multiply=0.55,
                    bass_multiply=1.10, crossover=1000, mod_depth=0.35, mod_rate=0.7,
                    early_ref_level=0.60, early_ref_size=0.50, pre_delay=20.0,
                    lo_cut=20, hi_cut=20000, width=1.0),
    "Room":    dict(decay_time=0.7, size=0.40, diffusion=0.50, treble_multiply=0.80,
                    bass_multiply=1.0, crossover=1000, mod_depth=0.15, mod_rate=1.5,
                    early_ref_level=0.60, early_ref_size=0.40, pre_delay=5.0,
                    lo_cut=20, hi_cut=20000, width=1.0),
    "Ambient": dict(decay_time=6.0, size=0.85, diffusion=0.90, treble_multiply=0.48,
                    bass_multiply=1.20, crossover=700, mod_depth=0.60, mod_rate=0.9,
                    early_ref_level=0.0, early_ref_size=0.0, pre_delay=40.0,
                    lo_cut=20, hi_cut=20000, width=1.6),
}

# Reference targets (based on ValhallaVintageVerb / Lexicon measurements)
QUALITY_TARGETS = {
    "echo_density_1000ms": 1000,   # echoes/sec at 1s (Schroeder threshold)
    "modal_ringing_db":    -40,    # max peak above noise floor in tail spectrum
    "spectral_tilt_db":    -3,     # gentle HF rolloff per octave in tail
    "stereo_correlation":   0.3,   # decorrelation target (< 0.5 = good stereo)
    "tail_smoothness_std":  2.0,   # dB std of 50ms-windowed RMS envelope in tail
}


# ---------------------------------------------------------------------------
# Signal generation
# ---------------------------------------------------------------------------
def make_impulse(duration_sec=3.0):
    """Single sample impulse (Dirac delta) — reveals the full impulse response."""
    n = int(SAMPLE_RATE * duration_sec)
    sig = np.zeros(n, dtype=np.float32)
    sig[0] = 1.0
    return sig


def make_snare_transient(duration_sec=3.0):
    """Synthetic snare: 200Hz body + 5kHz noise burst, ~10ms attack, ~50ms decay."""
    n = int(SAMPLE_RATE * duration_sec)
    sig = np.zeros(n, dtype=np.float32)
    t = np.arange(int(SAMPLE_RATE * 0.05), dtype=np.float32) / SAMPLE_RATE
    env = np.exp(-t / 0.012)
    body = 0.6 * np.sin(2 * np.pi * 200 * t) * env
    noise = 0.4 * np.random.randn(len(t)).astype(np.float32) * env
    burst = body + noise
    sig[:len(burst)] = burst
    return sig


def make_pink_noise_burst(duration_sec=3.0, burst_ms=100):
    """Short burst of pink noise — tests broadband response."""
    n = int(SAMPLE_RATE * duration_sec)
    burst_len = int(SAMPLE_RATE * burst_ms / 1000)
    # Generate white noise and filter to pink (-3dB/octave)
    white = np.random.randn(burst_len).astype(np.float32)
    # Simple pink filter (Voss-McCartney approximation)
    from scipy.signal import lfilter
    b = np.array([0.049922035, -0.095993537, 0.050612699, -0.004709510], dtype=np.float32)
    a = np.array([1.0, -2.494956002, 2.017265875, -0.522189400], dtype=np.float32)
    pink = lfilter(b, a, white).astype(np.float32)
    pink *= 0.5 / max(np.max(np.abs(pink)), 1e-10)
    # Apply envelope
    env = np.ones(burst_len, dtype=np.float32)
    fade = int(SAMPLE_RATE * 0.005)
    env[:fade] = np.linspace(0, 1, fade)
    env[-fade:] = np.linspace(1, 0, fade)
    pink *= env
    sig = np.zeros(n, dtype=np.float32)
    sig[:burst_len] = pink
    return sig


# ---------------------------------------------------------------------------
# Analysis functions
# ---------------------------------------------------------------------------
def rms_db(x):
    if x.size == 0:
        return -200.0
    return 10 * np.log10(max(np.mean(x ** 2), 1e-20))


def rms_envelope(signal, sr, window_ms=50):
    """RMS envelope in dB."""
    win = max(1, int(sr * window_ms / 1000))
    padded = np.pad(signal ** 2, (win // 2, win - win // 2), mode='edge')
    cumsum = np.cumsum(padded)
    rms_sq = (cumsum[win:] - cumsum[:-win]) / win
    return 10 * np.log10(np.maximum(rms_sq[:len(signal)], 1e-20))


def analyze_echo_density(ir, sr):
    """Estimate echo density (echoes per second) in the tail.
    Uses zero-crossing rate of the IR envelope derivative."""
    # Take absolute value and smooth
    env = np.abs(ir)
    win = int(sr * 0.001)  # 1ms window
    if win < 1:
        win = 1
    kernel = np.ones(win) / win
    env_smooth = np.convolve(env, kernel, mode='same')

    # Analyze in 50ms windows after 100ms (skip early reflections)
    start = int(sr * 0.1)
    window = int(sr * 0.05)
    densities = []

    for offset in range(start, len(env_smooth) - window, window):
        chunk = env_smooth[offset:offset + window]
        if rms_db(chunk) < -80:
            break
        # Count peaks (local maxima)
        peaks = 0
        for i in range(1, len(chunk) - 1):
            if chunk[i] > chunk[i-1] and chunk[i] > chunk[i+1]:
                peaks += 1
        density = peaks / (window / sr)
        densities.append(density)

    if not densities:
        return 0
    return np.mean(densities)


def analyze_spectral_decay(ir, sr):
    """Analyze how the spectrum evolves during decay.
    Returns spectral centroid over time and tilt in tail."""
    window_ms = 50
    hop_ms = 25
    win_samples = int(sr * window_ms / 1000)
    hop_samples = int(sr * hop_ms / 1000)
    nfft = 2048

    centroids = []
    times = []
    tail_spectra = []

    # Start after 50ms (skip direct sound)
    start = int(sr * 0.05)
    for pos in range(start, len(ir) - win_samples, hop_samples):
        chunk = ir[pos:pos + win_samples]
        level = rms_db(chunk)
        if level < -80:
            break

        windowed = chunk * np.hanning(win_samples)
        spec = np.abs(np.fft.rfft(windowed, n=nfft))
        freqs = np.fft.rfftfreq(nfft, 1.0 / sr)

        # Spectral centroid
        spec_sum = np.sum(spec)
        if spec_sum > 0:
            centroid = np.sum(freqs * spec) / spec_sum
            centroids.append(centroid)
            times.append(pos / sr)

        # Collect tail spectra (200ms - 500ms region)
        t = pos / sr
        if 0.2 <= t <= 0.5:
            tail_spectra.append(spec)

    # Compute spectral tilt in tail
    tilt_db_per_octave = 0
    if tail_spectra:
        avg_spec = np.mean(tail_spectra, axis=0)
        freqs = np.fft.rfftfreq(nfft, 1.0 / sr)
        # Measure energy in octave bands
        bands = [(250, 500), (500, 1000), (1000, 2000), (2000, 4000), (4000, 8000)]
        band_energies = []
        for lo, hi in bands:
            mask = (freqs >= lo) & (freqs < hi)
            energy = np.mean(avg_spec[mask] ** 2) if np.any(mask) else 1e-20
            band_energies.append(10 * np.log10(max(energy, 1e-20)))
        # Linear regression for tilt (dB per octave)
        if len(band_energies) >= 2:
            octaves = np.arange(len(band_energies), dtype=float)
            coeffs = np.polyfit(octaves, band_energies, 1)
            tilt_db_per_octave = coeffs[0]

    return {
        "centroids": centroids,
        "times": times,
        "tilt_db_per_octave": tilt_db_per_octave,
    }


def analyze_modal_ringing(ir, sr):
    """Detect metallic ringing: narrow spectral peaks that persist in the tail.
    Returns max peak prominence above local median in the 200-500ms tail window."""
    # Extract tail section (200-500ms)
    start = int(sr * 0.2)
    end = min(int(sr * 0.5), len(ir))
    if end <= start:
        return {"max_peak_prominence_db": -100, "ringing_freqs": []}

    tail = ir[start:end]
    nfft = 4096
    windowed = tail[:min(len(tail), nfft)] * np.hanning(min(len(tail), nfft))
    spec = np.abs(np.fft.rfft(windowed, n=nfft))
    spec_db = 20 * np.log10(np.maximum(spec, 1e-10))
    freqs = np.fft.rfftfreq(nfft, 1.0 / sr)

    # Local median filter (200Hz window) to find peaks above background
    median_window = int(200 / (sr / nfft))
    if median_window < 3:
        median_window = 3
    from scipy.ndimage import median_filter
    baseline = median_filter(spec_db, size=median_window)
    prominence = spec_db - baseline

    # Find peaks in 200-10000 Hz range
    freq_mask = (freqs >= 200) & (freqs <= 10000)
    masked_prom = prominence.copy()
    masked_prom[~freq_mask] = -100

    max_prom = np.max(masked_prom)
    max_idx = np.argmax(masked_prom)
    max_freq = freqs[max_idx]

    # Find all significant ringing frequencies (>6dB above median)
    ringing_freqs = []
    for i in range(len(masked_prom)):
        if masked_prom[i] > 6 and freq_mask[i]:
            ringing_freqs.append((freqs[i], masked_prom[i]))

    return {
        "max_peak_prominence_db": max_prom,
        "worst_freq_hz": max_freq,
        "ringing_freqs": ringing_freqs,
    }


def analyze_stereo(left, right):
    """Analyze stereo field: correlation, width, balance."""
    # Normalized cross-correlation
    if len(left) == 0:
        return {"correlation": 1.0, "width_ratio": 0.0}

    # Use the reverb tail only (skip first 50ms)
    start = int(SAMPLE_RATE * 0.05)
    l = left[start:]
    r = right[start:]

    # Trim to where signal is above noise floor
    env_l = rms_envelope(l, SAMPLE_RATE)
    env_r = rms_envelope(r, SAMPLE_RATE)
    mask = (env_l > -60) | (env_r > -60)
    if not np.any(mask):
        return {"correlation": 1.0, "width_ratio": 0.0}

    l = l[mask]
    r = r[mask]

    # Pearson correlation
    if np.std(l) < 1e-10 or np.std(r) < 1e-10:
        corr = 1.0
    else:
        corr = np.corrcoef(l, r)[0, 1]

    # Mid/Side ratio
    mid = (l + r) / 2
    side = (l - r) / 2
    mid_rms = rms_db(mid)
    side_rms = rms_db(side)
    width_ratio = side_rms - mid_rms  # Higher = wider

    return {"correlation": corr, "width_ratio_db": width_ratio}


def analyze_tail_smoothness(ir, sr):
    """Measure how smooth the decay envelope is.
    A smooth tail has low variance in the RMS envelope.
    Metallic/ringy tails have jagged envelopes with high variance."""
    env = rms_envelope(ir, sr, window_ms=50)

    # Find where the signal starts decaying (past the build-up)
    peak_idx = np.argmax(env)
    start = max(peak_idx, int(sr * 0.1))

    # Find where it drops below -60dB
    end = len(env)
    for i in range(start, len(env)):
        if env[i] < -60:
            end = i
            break

    if end - start < int(sr * 0.1):
        return {"envelope_std_db": 0, "decay_rate_db_per_sec": 0}

    tail_env = env[start:end]
    times = np.arange(len(tail_env)) / sr

    # Fit linear decay
    if len(times) > 1:
        coeffs = np.polyfit(times, tail_env, 1)
        decay_rate = coeffs[0]
        residuals = tail_env - np.polyval(coeffs, times)
        smoothness_std = np.std(residuals)
    else:
        decay_rate = 0
        smoothness_std = 0

    return {
        "envelope_std_db": smoothness_std,
        "decay_rate_db_per_sec": decay_rate,
    }


def analyze_early_reflections(ir, sr):
    """Analyze early reflection pattern (first 100ms)."""
    er_len = int(sr * 0.1)
    er = ir[:min(er_len, len(ir))]

    # Find significant peaks
    threshold = 0.05 * np.max(np.abs(er))
    peaks = []
    for i in range(1, len(er) - 1):
        if abs(er[i]) > threshold and abs(er[i]) > abs(er[i-1]) and abs(er[i]) > abs(er[i+1]):
            peaks.append((i / sr * 1000, 20 * np.log10(max(abs(er[i]), 1e-10))))

    # ER density
    er_density = len(peaks) / 0.1 if peaks else 0

    # Energy ratio: ER vs tail
    er_energy = rms_db(ir[:er_len])
    tail_start = int(sr * 0.15)
    tail_energy = rms_db(ir[tail_start:tail_start + er_len]) if tail_start + er_len <= len(ir) else -100
    er_to_tail_ratio = er_energy - tail_energy

    return {
        "num_peaks": len(peaks),
        "density_per_sec": er_density,
        "er_to_tail_db": er_to_tail_ratio,
        "first_peaks": peaks[:8],
    }


def analyze_frequency_response(ir, sr):
    """Overall frequency response of the reverb."""
    nfft = 8192
    windowed = ir[:min(len(ir), nfft)]
    if len(windowed) < nfft:
        windowed = np.pad(windowed, (0, nfft - len(windowed)))
    spec = np.abs(np.fft.rfft(windowed * np.hanning(nfft), n=nfft))
    spec_db = 20 * np.log10(np.maximum(spec, 1e-10))
    freqs = np.fft.rfftfreq(nfft, 1.0 / sr)

    # Energy in standard bands
    bands = {
        "sub_bass (20-80Hz)": (20, 80),
        "bass (80-250Hz)": (80, 250),
        "low_mid (250-800Hz)": (250, 800),
        "mid (800-2.5kHz)": (800, 2500),
        "hi_mid (2.5-6kHz)": (2500, 6000),
        "treble (6-12kHz)": (6000, 12000),
        "air (12-20kHz)": (12000, 20000),
    }
    band_levels = {}
    for name, (lo, hi) in bands.items():
        mask = (freqs >= lo) & (freqs < hi)
        if np.any(mask):
            band_levels[name] = float(np.mean(spec_db[mask]))
        else:
            band_levels[name] = -100.0

    return band_levels


# ---------------------------------------------------------------------------
# Main analysis
# ---------------------------------------------------------------------------
def process_signal(plugin, signal, algo_name, settings):
    """Process a mono signal through DuskVerb and return stereo output."""
    # Set algorithm
    plugin.algorithm = algo_name

    # Set all parameters
    plugin.dry_wet = 1.0  # 100% wet for analysis
    plugin.freeze = False
    plugin.bus_mode = False
    plugin.pre_delay_sync = "Free"

    for key, value in settings.items():
        setattr(plugin, key, value)

    # Make stereo input (mono duplicated)
    stereo_in = np.stack([signal, signal], axis=0).astype(np.float32)

    # Process with enough tail
    output = plugin(stereo_in, SAMPLE_RATE)
    return output[0], output[1]  # left, right


def run_analysis(plugin, algo_name, save_dir=None):
    """Run full analysis suite on one algorithm."""
    settings = ALGO_SETTINGS[algo_name]
    print(f"\n{'='*72}")
    print(f"  ALGORITHM: {algo_name}")
    print(f"{'='*72}")
    print(f"  Settings: decay={settings['decay_time']}s, size={settings['size']:.0%}, "
          f"diff={settings['diffusion']:.0%}, treble={settings['treble_multiply']:.2f}x")
    print(f"  mod_depth={settings['mod_depth']:.0%}, mod_rate={settings['mod_rate']}Hz")

    # --- Test 1: Impulse Response ---
    print(f"\n  --- Impulse Response Analysis ---")
    impulse = make_impulse(duration_sec=4.0)
    ir_l, ir_r = process_signal(plugin, impulse, algo_name, settings)

    # Use left channel as primary IR
    ir = ir_l

    # Echo density
    density = analyze_echo_density(ir, SAMPLE_RATE)
    target = QUALITY_TARGETS["echo_density_1000ms"]
    status = "PASS" if density >= target * 0.7 else "WARN" if density >= target * 0.4 else "FAIL"
    print(f"  Echo density: {density:.0f} echoes/sec (target: {target}) [{status}]")

    # Modal ringing
    ringing = analyze_modal_ringing(ir, SAMPLE_RATE)
    prom = ringing["max_peak_prominence_db"]
    target_ring = 8.0  # < 8dB prominence = clean
    status = "PASS" if prom < target_ring else "WARN" if prom < 12 else "FAIL"
    print(f"  Modal ringing: {prom:.1f} dB peak prominence at {ringing['worst_freq_hz']:.0f} Hz [{status}]")
    if ringing["ringing_freqs"]:
        print(f"    Ringing frequencies (>6dB):")
        for freq, prom_db in sorted(ringing["ringing_freqs"], key=lambda x: -x[1])[:5]:
            print(f"      {freq:.0f} Hz: +{prom_db:.1f} dB")

    # Tail smoothness
    smoothness = analyze_tail_smoothness(ir, SAMPLE_RATE)
    std = smoothness["envelope_std_db"]
    target_std = QUALITY_TARGETS["tail_smoothness_std"]
    status = "PASS" if std < target_std else "WARN" if std < target_std * 1.5 else "FAIL"
    print(f"  Tail smoothness: {std:.2f} dB std (target: <{target_std}) [{status}]")
    print(f"  Decay rate: {smoothness['decay_rate_db_per_sec']:.1f} dB/sec")

    # Spectral decay
    spectral = analyze_spectral_decay(ir, SAMPLE_RATE)
    tilt = spectral["tilt_db_per_octave"]
    print(f"  Spectral tilt in tail: {tilt:.1f} dB/octave "
          f"({'bright' if tilt > -1 else 'neutral' if tilt > -4 else 'dark'})")

    # Stereo analysis
    stereo = analyze_stereo(ir_l, ir_r)
    corr = stereo["correlation"]
    target_corr = QUALITY_TARGETS["stereo_correlation"]
    status = "PASS" if corr < 0.5 else "WARN" if corr < 0.7 else "FAIL"
    print(f"  Stereo correlation: {corr:.3f} (target: <0.5, lower=wider) [{status}]")
    print(f"  Stereo width ratio: {stereo['width_ratio_db']:.1f} dB (S-M)")

    # Early reflections (if applicable)
    er = analyze_early_reflections(ir, SAMPLE_RATE)
    if settings.get("early_ref_level", 0) > 0:
        print(f"\n  --- Early Reflections ---")
        print(f"  ER peaks: {er['num_peaks']}")
        print(f"  ER density: {er['density_per_sec']:.0f}/sec")
        print(f"  ER-to-tail ratio: {er['er_to_tail_db']:.1f} dB")

    # Frequency response
    freq_resp = analyze_frequency_response(ir, SAMPLE_RATE)
    print(f"\n  --- Frequency Response (impulse) ---")
    for band, level in freq_resp.items():
        print(f"  {band:25s}: {level:.1f} dB")

    # --- Test 2: Snare transient ---
    print(f"\n  --- Snare Transient Response ---")
    snare = make_snare_transient(duration_sec=4.0)
    snare_l, snare_r = process_signal(plugin, snare, algo_name, settings)

    snare_smoothness = analyze_tail_smoothness(snare_l, SAMPLE_RATE)
    snare_ringing = analyze_modal_ringing(snare_l, SAMPLE_RATE)
    print(f"  Tail smoothness: {snare_smoothness['envelope_std_db']:.2f} dB std")
    print(f"  Modal ringing: {snare_ringing['max_peak_prominence_db']:.1f} dB at "
          f"{snare_ringing['worst_freq_hz']:.0f} Hz")

    # --- Test 3: Pink noise burst ---
    print(f"\n  --- Pink Noise Burst Response ---")
    pink = make_pink_noise_burst(duration_sec=4.0)
    pink_l, pink_r = process_signal(plugin, pink, algo_name, settings)

    pink_smoothness = analyze_tail_smoothness(pink_l, SAMPLE_RATE)
    pink_ringing = analyze_modal_ringing(pink_l, SAMPLE_RATE)
    pink_spectral = analyze_spectral_decay(pink_l, SAMPLE_RATE)
    print(f"  Tail smoothness: {pink_smoothness['envelope_std_db']:.2f} dB std")
    print(f"  Modal ringing: {pink_ringing['max_peak_prominence_db']:.1f} dB at "
          f"{pink_ringing['worst_freq_hz']:.0f} Hz")
    print(f"  Spectral tilt: {pink_spectral['tilt_db_per_octave']:.1f} dB/octave")

    # --- Overall Score ---
    print(f"\n  --- Quality Score ---")
    scores = {}
    # Echo density (0-100)
    scores["density"] = min(100, density / target * 100)
    # Ringing (100 = no ringing, 0 = severe)
    scores["ringing"] = max(0, 100 - ringing["max_peak_prominence_db"] * 8)
    # Smoothness (100 = perfectly smooth, 0 = very rough)
    scores["smoothness"] = max(0, 100 - (smoothness["envelope_std_db"] / target_std) * 50)
    # Stereo (100 = well decorrelated, 0 = mono)
    scores["stereo"] = max(0, min(100, (1 - corr) * 200))

    total = np.mean(list(scores.values()))
    for metric, score in scores.items():
        bar = "#" * int(score / 5) + "-" * (20 - int(score / 5))
        print(f"  {metric:15s}: {score:5.1f}/100  [{bar}]")
    print(f"  {'OVERALL':15s}: {total:5.1f}/100")

    # Save outputs if requested
    if save_dir:
        os.makedirs(save_dir, exist_ok=True)
        prefix = os.path.join(save_dir, f"duskverb_{algo_name.lower()}")
        stereo_ir = np.stack([ir_l, ir_r], axis=-1)
        sf.write(f"{prefix}_impulse.wav", stereo_ir, SAMPLE_RATE)
        stereo_snare = np.stack([snare_l, snare_r], axis=-1)
        sf.write(f"{prefix}_snare.wav", stereo_snare, SAMPLE_RATE)
        stereo_pink = np.stack([pink_l, pink_r], axis=-1)
        sf.write(f"{prefix}_pink.wav", stereo_pink, SAMPLE_RATE)
        print(f"\n  Saved WAVs to {save_dir}/")

    return {
        "algorithm": algo_name,
        "echo_density": density,
        "ringing_prominence": ringing["max_peak_prominence_db"],
        "ringing_freq": ringing["worst_freq_hz"],
        "tail_smoothness": smoothness["envelope_std_db"],
        "spectral_tilt": spectral["tilt_db_per_octave"],
        "stereo_correlation": corr,
        "scores": scores,
        "total_score": total,
    }


def main():
    parser = argparse.ArgumentParser(description="DuskVerb reverb quality analysis")
    parser.add_argument("--algo", type=str, default=None,
                        help="Analyze specific algorithm (Plate/Hall/Chamber/Room/Ambient)")
    parser.add_argument("--save", action="store_true",
                        help="Save processed WAV files for listening")
    args = parser.parse_args()

    print(f"Loading DuskVerb from: {PLUGIN_PATH}")
    plugin = load_plugin(PLUGIN_PATH)
    print(f"Loaded: {plugin}")

    save_dir = os.path.join(os.path.dirname(__file__), "output") if args.save else None

    algos = [args.algo] if args.algo else ALGORITHMS
    results = []

    for algo in algos:
        if algo not in ALGORITHMS:
            print(f"Unknown algorithm: {algo}. Choose from: {ALGORITHMS}")
            sys.exit(1)
        result = run_analysis(plugin, algo, save_dir)
        results.append(result)

    # Summary table
    if len(results) > 1:
        print(f"\n{'='*72}")
        print(f"  SUMMARY")
        print(f"{'='*72}")
        print(f"  {'Algorithm':12s} {'Density':>8s} {'Ringing':>8s} {'Smooth':>8s} {'Stereo':>8s} {'TOTAL':>8s}")
        print(f"  {'-'*12} {'-'*8} {'-'*8} {'-'*8} {'-'*8} {'-'*8}")
        for r in results:
            s = r["scores"]
            print(f"  {r['algorithm']:12s} {s['density']:7.1f} {s['ringing']:7.1f} "
                  f"{s['smoothness']:7.1f} {s['stereo']:7.1f} {r['total_score']:7.1f}")


if __name__ == "__main__":
    main()
