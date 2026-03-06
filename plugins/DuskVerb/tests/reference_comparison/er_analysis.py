#!/usr/bin/env python3
"""
Early Reflections analysis: DuskVerb vs ReferenceReverb.

Captures short IRs and compares ER characteristics:
  - Onset time (ms to first significant reflection)
  - Tap density (reflections per 10ms in first 80ms)
  - Spectral tilt (HF/LF energy ratio of ER portion)
  - Waveform overlay plots

Usage:
    python3 er_analysis.py              # All ER-capable algorithms
    python3 er_analysis.py --algo Hall  # Single algorithm
    python3 er_analysis.py --plot       # Save waveform plots
"""

import argparse
import os
import sys

import numpy as np

from config import (
    SAMPLE_RATE,
    DUSKVERB_PATHS,
    REFERENCE_REVERB_PATHS,
    find_plugin,
    apply_duskverb_params,
    apply_reference_params,
)
from generate_test_signals import make_impulse

# ---------------------------------------------------------------------------
# ER-optimized parameter configs: short decay, max ER, minimal late reverb
# ---------------------------------------------------------------------------
ER_CONFIGS = {
    "Hall": {
        "duskverb": {
            "algorithm": "Hall",
            "decay_time": 0.5,
            "size": 0.50,
            "diffusion": 0.70,
            "treble_multiply": 0.70,
            "bass_multiply": 1.0,
            "crossover": 1000,
            "mod_depth": 0.0,
            "mod_rate": 0.5,
            "early_ref_level": 1.0,
            "early_ref_size": 0.50,
            "pre_delay": 0.0,
            "lo_cut": 20,
            "hi_cut": 20000,
            "width": 1.0,
        },
        "reference": {
            "_reverbmode": 0.0417,      # Concert Hall
            "_colormode": 0.333,        # 1980s
            "_decay": 0.03,             # Minimum decay
            "_size": 0.50,
            "_predelay": 0.0,
            "_diffusion_early": 0.70,
            "_diffusion_late": 0.70,
            "_mod_rate": 0.0,
            "_mod_depth": 0.0,
            "_high_cut": 1.0,
            "_low_cut": 0.0,
            "_bassmult": 0.50,
            "_bassxover": 0.40,
            "_highshelf": 0.0,
            "_highfreq": 0.50,
            "_attack": 0.50,
        },
    },
    "Chamber": {
        "duskverb": {
            "algorithm": "Chamber",
            "decay_time": 0.5,
            "size": 0.50,
            "diffusion": 0.70,
            "treble_multiply": 0.70,
            "bass_multiply": 1.0,
            "crossover": 1000,
            "mod_depth": 0.0,
            "mod_rate": 0.5,
            "early_ref_level": 1.0,
            "early_ref_size": 0.50,
            "pre_delay": 0.0,
            "lo_cut": 20,
            "hi_cut": 20000,
            "width": 1.0,
        },
        "reference": {
            "_reverbmode": 0.1667,      # Chamber
            "_colormode": 0.333,
            "_decay": 0.03,
            "_size": 0.50,
            "_predelay": 0.0,
            "_diffusion_early": 0.70,
            "_diffusion_late": 0.70,
            "_mod_rate": 0.0,
            "_mod_depth": 0.0,
            "_high_cut": 1.0,
            "_low_cut": 0.0,
            "_bassmult": 0.50,
            "_bassxover": 0.40,
            "_highshelf": 0.0,
            "_highfreq": 0.50,
            "_attack": 0.50,
        },
    },
    "Room": {
        "duskverb": {
            "algorithm": "Room",
            "decay_time": 0.5,
            "size": 0.50,
            "diffusion": 0.70,
            "treble_multiply": 0.70,
            "bass_multiply": 1.0,
            "crossover": 1000,
            "mod_depth": 0.0,
            "mod_rate": 0.5,
            "early_ref_level": 1.0,
            "early_ref_size": 0.50,
            "pre_delay": 0.0,
            "lo_cut": 20,
            "hi_cut": 20000,
            "width": 1.0,
        },
        "reference": {
            "_reverbmode": 0.1250,      # Room
            "_colormode": 0.333,
            "_decay": 0.03,
            "_size": 0.50,
            "_predelay": 0.0,
            "_diffusion_early": 0.70,
            "_diffusion_late": 0.70,
            "_mod_rate": 0.0,
            "_mod_depth": 0.0,
            "_high_cut": 1.0,
            "_low_cut": 0.0,
            "_bassmult": 0.50,
            "_bassxover": 0.40,
            "_highshelf": 0.0,
            "_highfreq": 0.50,
            "_attack": 0.50,
        },
    },
}


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def process_stereo(plugin, mono_signal, sr):
    stereo_in = np.stack([mono_signal, mono_signal], axis=0).astype(np.float32)
    output = plugin(stereo_in, sr)
    return output[0], output[1]


def flush_plugin(plugin, sr, duration_sec=1.0):
    silence = np.zeros(int(sr * duration_sec), dtype=np.float32)
    process_stereo(plugin, silence, sr)


def capture_er_ir(plugin, apply_fn, config, sr, ir_duration=0.3):
    """Capture a short IR focused on early reflections."""
    apply_fn(plugin, config)
    flush_plugin(plugin, sr, 2.0)
    impulse = make_impulse(ir_duration)
    ir_l, ir_r = process_stereo(plugin, impulse, sr)
    flush_plugin(plugin, sr, 1.0)
    return ir_l, ir_r


def find_onset_ms(ir, sr, threshold_db=-40):
    """Find the time of the first sample exceeding threshold (relative to peak)."""
    peak = np.max(np.abs(ir))
    if peak < 1e-10:
        return None
    threshold = peak * 10 ** (threshold_db / 20.0)
    above = np.where(np.abs(ir) > threshold)[0]
    if len(above) == 0:
        return None
    return above[0] / sr * 1000.0


def count_er_taps(ir, sr, window_ms=80, min_prominence_db=-30):
    """Count distinct reflection peaks in the ER window."""
    from scipy.signal import find_peaks

    window_samples = int(window_ms / 1000.0 * sr)
    segment = ir[:window_samples]
    peak = np.max(np.abs(segment))
    if peak < 1e-10:
        return 0, []

    # Normalize
    env = np.abs(segment)
    threshold = peak * 10 ** (min_prominence_db / 20.0)

    # Find peaks with minimum distance of 0.5ms
    min_dist = max(1, int(0.0005 * sr))
    peaks, props = find_peaks(env, height=threshold, distance=min_dist,
                              prominence=threshold * 0.3)
    return len(peaks), peaks


def tap_density_per_10ms(ir, sr, window_ms=80, min_prominence_db=-30):
    """Calculate reflection density in 10ms bins."""
    _, peaks = count_er_taps(ir, sr, window_ms, min_prominence_db)
    bin_size_ms = 10.0
    n_bins = int(window_ms / bin_size_ms)
    densities = []
    for b in range(n_bins):
        t_start = b * bin_size_ms / 1000.0 * sr
        t_end = (b + 1) * bin_size_ms / 1000.0 * sr
        count = np.sum((peaks >= t_start) & (peaks < t_end))
        densities.append(int(count))
    return densities


def spectral_tilt(ir, sr, window_ms=80):
    """Compute spectral tilt of ER portion: ratio of HF (2-8kHz) to LF (125-500Hz) energy."""
    window_samples = int(window_ms / 1000.0 * sr)
    segment = ir[:window_samples]

    # Apply Hann window
    segment = segment * np.hanning(len(segment))

    # FFT
    n_fft = max(2048, len(segment))
    spectrum = np.abs(np.fft.rfft(segment, n=n_fft))
    freqs = np.fft.rfftfreq(n_fft, 1.0 / sr)

    # LF energy: 125-500Hz
    lf_mask = (freqs >= 125) & (freqs <= 500)
    lf_energy = np.sum(spectrum[lf_mask] ** 2)

    # HF energy: 2000-8000Hz
    hf_mask = (freqs >= 2000) & (freqs <= 8000)
    hf_energy = np.sum(spectrum[hf_mask] ** 2)

    if lf_energy < 1e-20:
        return float('inf')
    return 10 * np.log10(hf_energy / lf_energy)


def save_er_plot(ir_dv_l, ir_vv_l, sr, algo_name, output_dir):
    """Save overlaid ER waveform plot."""
    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
    except ImportError:
        print("  matplotlib not available, skipping plot")
        return

    window_ms = 100
    window_samples = int(window_ms / 1000.0 * sr)
    time_ms = np.arange(window_samples) / sr * 1000.0

    fig, axes = plt.subplots(2, 1, figsize=(12, 8), sharex=True)

    # Waveform overlay
    axes[0].plot(time_ms, ir_vv_l[:window_samples], alpha=0.7, label='ReferenceReverb', color='blue')
    axes[0].plot(time_ms, ir_dv_l[:window_samples], alpha=0.7, label='DuskVerb', color='red')
    axes[0].set_ylabel('Amplitude')
    axes[0].set_title(f'{algo_name} — Early Reflections Comparison')
    axes[0].legend()
    axes[0].grid(True, alpha=0.3)

    # Envelope overlay
    from scipy.signal import hilbert
    env_vv = np.abs(hilbert(ir_vv_l[:window_samples]))
    env_dv = np.abs(hilbert(ir_dv_l[:window_samples]))
    axes[1].plot(time_ms, 20 * np.log10(env_vv + 1e-10), alpha=0.7, label='ReferenceReverb', color='blue')
    axes[1].plot(time_ms, 20 * np.log10(env_dv + 1e-10), alpha=0.7, label='DuskVerb', color='red')
    axes[1].set_xlabel('Time (ms)')
    axes[1].set_ylabel('Level (dB)')
    axes[1].set_title(f'{algo_name} — ER Envelope')
    axes[1].set_ylim([-80, 0])
    axes[1].legend()
    axes[1].grid(True, alpha=0.3)

    os.makedirs(output_dir, exist_ok=True)
    path = os.path.join(output_dir, f'er_{algo_name.lower()}.png')
    plt.tight_layout()
    plt.savefig(path, dpi=150)
    plt.close()
    print(f"  Plot saved: {path}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def analyze_algo(algo_name, dv_plugin, vv_plugin, do_plot=False):
    """Analyze ER for one algorithm pairing."""
    config = ER_CONFIGS[algo_name]
    sr = SAMPLE_RATE

    print(f"\nCapturing: {algo_name} ...", end=" ", flush=True)
    ir_dv_l, ir_dv_r = capture_er_ir(dv_plugin, apply_duskverb_params,
                                      config["duskverb"], sr)
    ir_vv_l, ir_vv_r = capture_er_ir(vv_plugin, apply_reference_params,
                                      config["reference"], sr)
    print("done")

    # Mono sum for analysis
    ir_dv = (ir_dv_l + ir_dv_r) * 0.5
    ir_vv = (ir_vv_l + ir_vv_r) * 0.5

    # Onset time
    onset_dv = find_onset_ms(ir_dv, sr)
    onset_vv = find_onset_ms(ir_vv, sr)

    # Tap count
    n_taps_dv, _ = count_er_taps(ir_dv, sr)
    n_taps_vv, _ = count_er_taps(ir_vv, sr)

    # Density per 10ms bin
    dens_dv = tap_density_per_10ms(ir_dv, sr)
    dens_vv = tap_density_per_10ms(ir_vv, sr)

    # Spectral tilt
    tilt_dv = spectral_tilt(ir_dv, sr)
    tilt_vv = spectral_tilt(ir_vv, sr)

    # ER energy (first 80ms vs next 80ms)
    w80 = int(0.080 * sr)
    er_energy_dv = np.sqrt(np.mean(ir_dv[:w80] ** 2))
    er_energy_vv = np.sqrt(np.mean(ir_vv[:w80] ** 2))
    late_energy_dv = np.sqrt(np.mean(ir_dv[w80:2*w80] ** 2))
    late_energy_vv = np.sqrt(np.mean(ir_vv[w80:2*w80] ** 2))

    er_to_late_dv = 20 * np.log10(er_energy_dv / max(late_energy_dv, 1e-10))
    er_to_late_vv = 20 * np.log10(er_energy_vv / max(late_energy_vv, 1e-10))

    # Stereo decorrelation in ER window
    er_dv_l = ir_dv_l[:w80]
    er_dv_r = ir_dv_r[:w80]
    er_vv_l = ir_vv_l[:w80]
    er_vv_r = ir_vv_r[:w80]

    def stereo_corr(l, r):
        if np.std(l) < 1e-10 or np.std(r) < 1e-10:
            return 0.0
        return float(np.corrcoef(l, r)[0, 1])

    corr_dv = stereo_corr(er_dv_l, er_dv_r)
    corr_vv = stereo_corr(er_vv_l, er_vv_r)

    # Report
    print(f"\n=== {algo_name} ===")
    print(f"  {'Metric':<30s} {'DuskVerb':>12s} {'ReferenceReverb':>12s} {'Delta':>10s}")
    print(f"  {'-'*64}")

    def row(label, dv_val, vv_val, fmt=".1f", unit=""):
        if dv_val is None or vv_val is None:
            dv_s = "N/A" if dv_val is None else f"{dv_val:{fmt}}{unit}"
            vv_s = "N/A" if vv_val is None else f"{vv_val:{fmt}}{unit}"
            print(f"  {label:<30s} {dv_s:>12s} {vv_s:>12s} {'N/A':>10s}")
        else:
            delta = dv_val - vv_val
            print(f"  {label:<30s} {dv_val:>11{fmt}}{unit} {vv_val:>11{fmt}}{unit} {delta:>+9{fmt}}{unit}")

    row("ER onset (ms)", onset_dv, onset_vv, ".2f", " ms")
    row("Tap count (80ms)", n_taps_dv, n_taps_vv, "d", "")
    row("Spectral tilt HF/LF (dB)", tilt_dv, tilt_vv, ".1f", " dB")
    row("ER-to-late ratio (dB)", er_to_late_dv, er_to_late_vv, ".1f", " dB")
    row("Stereo correlation", corr_dv, corr_vv, ".3f", "")

    print(f"\n  Tap density per 10ms bin:")
    print(f"  {'Bin (ms)':<12s} {'DV':>6s} {'VV':>6s}")
    for i, (d, v) in enumerate(zip(dens_dv, dens_vv)):
        t0 = i * 10
        t1 = t0 + 10
        print(f"  {t0:>3d}-{t1:<3d} ms   {d:>6d} {v:>6d}")

    # Plot
    if do_plot:
        save_er_plot(ir_dv_l, ir_vv_l, sr, algo_name,
                     os.path.join(os.path.dirname(__file__), "plots"))

    return {
        "algo": algo_name,
        "onset_dv": onset_dv, "onset_vv": onset_vv,
        "taps_dv": n_taps_dv, "taps_vv": n_taps_vv,
        "tilt_dv": tilt_dv, "tilt_vv": tilt_vv,
        "er_late_dv": er_to_late_dv, "er_late_vv": er_to_late_vv,
        "corr_dv": corr_dv, "corr_vv": corr_vv,
    }


def main():
    parser = argparse.ArgumentParser(description="DuskVerb vs ReferenceReverb ER analysis")
    parser.add_argument("--algo", choices=list(ER_CONFIGS.keys()),
                        help="Analyze single algorithm")
    parser.add_argument("--plot", action="store_true",
                        help="Save waveform overlay plots")
    args = parser.parse_args()

    # Discover plugins
    dv_path = find_plugin(DUSKVERB_PATHS)
    vv_path = find_plugin(REFERENCE_REVERB_PATHS)
    if not dv_path:
        print("ERROR: DuskVerb not found"); sys.exit(1)
    if not vv_path:
        print("ERROR: ReferenceReverb not found"); sys.exit(1)

    print(f"DuskVerb:     {dv_path}")
    print(f"ReferenceReverb:  {vv_path}")
    print(f"Sample rate:  {SAMPLE_RATE} Hz")

    from pedalboard import load_plugin
    dv = load_plugin(dv_path)
    vv = load_plugin(vv_path)

    algos = [args.algo] if args.algo else list(ER_CONFIGS.keys())
    results = []
    for algo in algos:
        r = analyze_algo(algo, dv, vv, do_plot=args.plot)
        results.append(r)

    # Summary
    if len(results) > 1:
        print("\n" + "=" * 72)
        print("  SUMMARY")
        print("=" * 72)
        print(f"  {'Algorithm':<12s} {'Onset Δ':>10s} {'Taps DV/VV':>12s} {'Tilt Δ':>10s} {'ER/Late Δ':>10s} {'Corr DV/VV':>14s}")
        print(f"  {'-'*68}")
        for r in results:
            onset_d = f"{r['onset_dv'] - r['onset_vv']:+.2f} ms" if r['onset_dv'] and r['onset_vv'] else "N/A"
            taps = f"{r['taps_dv']}/{r['taps_vv']}"
            tilt_d = f"{r['tilt_dv'] - r['tilt_vv']:+.1f} dB"
            erlate_d = f"{r['er_late_dv'] - r['er_late_vv']:+.1f} dB"
            corr = f"{r['corr_dv']:.2f}/{r['corr_vv']:.2f}"
            print(f"  {r['algo']:<12s} {onset_d:>10s} {taps:>12s} {tilt_d:>10s} {erlate_d:>10s} {corr:>14s}")

    print("\nDone.")


if __name__ == "__main__":
    main()
