#!/usr/bin/env python3
"""
Analyze LA-2A vs Multi-Comp Opto comparison captures.

Reads test signal inputs and plugin-processed outputs, then generates
detailed comparison plots and metrics.

Expected file layout:
    ./test_signals/01_step_response.wav          (dry input)
    ./captured/multicomp_01_step_response.wav     (Multi-Comp output)
    ./captured/la2a_01_step_response.wav          (UA LA-2A output)

Usage:
    python3 analyze_comparison.py [--input-dir ./test_signals] [--capture-dir ./captured]
"""

import argparse
import os
import sys
import numpy as np
import soundfile as sf
import matplotlib.pyplot as plt
from matplotlib.gridspec import GridSpec

# --- Utility functions ---

def db(x):
    """Linear to dB, floor at -120dB."""
    return 20 * np.log10(np.maximum(np.abs(x), 1e-6))


def rms_envelope(signal, window_ms=10, sr=48000):
    """Compute RMS envelope with given window size."""
    win_samples = max(1, int(window_ms * sr / 1000))
    squared = signal ** 2
    # Cumulative sum for efficient windowed RMS
    cumsum = np.cumsum(squared)
    cumsum = np.insert(cumsum, 0, 0)
    windowed = (cumsum[win_samples:] - cumsum[:-win_samples]) / win_samples
    # Pad to original length
    pad = np.full(len(signal) - len(windowed), windowed[0] if len(windowed) > 0 else 0)
    windowed = np.concatenate([pad, windowed])
    return np.sqrt(np.maximum(windowed, 0))


def gain_reduction_envelope(dry, wet, window_ms=10, sr=48000):
    """Compute gain reduction in dB (negative = compression)."""
    dry_env = rms_envelope(dry, window_ms, sr)
    wet_env = rms_envelope(wet, window_ms, sr)
    # Avoid division by zero
    gr = np.where(dry_env > 1e-6, wet_env / dry_env, 1.0)
    return 20 * np.log10(np.maximum(gr, 1e-6))


def measure_attack_time(gr_db, sr, threshold_start=0.1, threshold_end=0.9):
    """Measure attack time from 10% to 90% of final GR depth."""
    min_gr = np.min(gr_db)
    if min_gr > -1.0:
        return None  # Not enough compression

    # Find where compression starts (first crossing of 10% of max GR)
    target_start = min_gr * threshold_start
    target_end = min_gr * threshold_end

    idxs_start = np.where(gr_db < target_start)[0]
    idxs_end = np.where(gr_db < target_end)[0]

    if len(idxs_start) == 0 or len(idxs_end) == 0:
        return None

    start_idx = idxs_start[0]
    end_idx = idxs_end[0]

    if end_idx > start_idx:
        return (end_idx - start_idx) / sr * 1000  # ms
    return None


def measure_release_time(gr_db, sr, release_start_idx=None):
    """
    Measure release time from a gain reduction envelope.

    Returns (fast_ms, slow_ms) where:
    - fast_ms: time in ms from release_start_idx to the 50% recovery point
    - slow_ms: time in ms from release_start_idx to the 10% recovery point
    Each may be None if the threshold isn't reached within the signal.

    If release_start_idx is None, uses the index of maximum gain reduction.
    Returns (None, None) if max GR is less than 1 dB.
    """
    if release_start_idx is None:
        release_start_idx = np.argmin(gr_db)

    max_gr = gr_db[release_start_idx]
    if max_gr > -1.0:
        return None, None

    # Slice from release start onward
    release_portion = gr_db[release_start_idx:]

    # Fast release: time to recover to 50% of max GR depth
    target_fast = max_gr * 0.5
    fast_indices = np.where(release_portion > target_fast)[0]
    fast_ms = (fast_indices[0] / sr * 1000) if len(fast_indices) > 0 else None

    # Slow release: time to recover to 10% of max GR depth
    target_slow = max_gr * 0.1
    slow_indices = np.where(release_portion > target_slow)[0]
    slow_ms = (slow_indices[0] / sr * 1000) if len(slow_indices) > 0 else None

    return fast_ms, slow_ms


def compute_thd(signal, fundamental_freq, sr, n_harmonics=10):
    """Compute THD from a steady-state signal."""
    N = len(signal)
    freqs = np.fft.rfftfreq(N, 1.0 / sr)
    spectrum = np.abs(np.fft.rfft(signal * np.hanning(N))) * 2 / N

    # Find fundamental bin
    fund_bin = int(round(fundamental_freq * N / sr))
    search_range = max(3, int(N * 5 / sr))  # ±5 Hz

    fund_power = 0.0
    for b in range(max(0, fund_bin - search_range), min(len(spectrum), fund_bin + search_range)):
        fund_power += spectrum[b] ** 2

    # Sum harmonic power
    harm_power = 0.0
    for h in range(2, n_harmonics + 1):
        harm_bin = int(round(h * fundamental_freq * N / sr))
        if harm_bin >= len(spectrum):
            break
        for b in range(max(0, harm_bin - search_range), min(len(spectrum), harm_bin + search_range)):
            harm_power += spectrum[b] ** 2

    if fund_power < 1e-12:
        return 0.0
    return np.sqrt(harm_power / fund_power) * 100  # percentage


def compute_spectrum(signal, sr, n_fft=None):
    """Compute magnitude spectrum in dB, with optional zero-padding to n_fft."""
    if n_fft is None:
        n_fft = len(signal)
    window = np.hanning(len(signal))
    windowed = signal * window
    if n_fft != len(signal):
        padded = np.zeros(n_fft)
        padded[:len(windowed)] = windowed
        windowed = padded
    spectrum = np.abs(np.fft.rfft(windowed)) * 2 / len(signal)
    freqs = np.fft.rfftfreq(n_fft, 1.0 / sr)
    return freqs, 20 * np.log10(np.maximum(spectrum, 1e-10))


# --- Loading ---

def load_trio(test_name, input_dir, capture_dir):
    """Load dry, multicomp, and la2a files for a test. Returns (dry, mc, la2a, sr)."""
    dry_path = os.path.join(input_dir, f"{test_name}.wav")
    mc_path = os.path.join(capture_dir, f"multicomp_{test_name}.wav")
    la2a_path = os.path.join(capture_dir, f"la2a_{test_name}.wav")

    results = {}
    common_sr = None
    for label, path in [("dry", dry_path), ("multicomp", mc_path), ("la2a", la2a_path)]:
        if os.path.exists(path):
            data, sr = sf.read(path)
            if data.ndim > 1:
                data = data[:, 0]  # Use left channel
            if common_sr is None:
                common_sr = sr
            elif sr != common_sr:
                raise ValueError(
                    f"Sample rate mismatch: '{label}' file ({path}) has sr={sr}, "
                    f"expected {common_sr} (from first loaded file)")
            results[label] = data
        else:
            results[label] = None

    if common_sr is None:
        raise FileNotFoundError(
            f"No audio files found for test '{test_name}'. "
            f"Expected at least one of:\n"
            f"  {dry_path}\n"
            f"  {mc_path}\n"
            f"  {la2a_path}")

    return results.get("dry"), results.get("multicomp"), results.get("la2a"), common_sr

# --- Analysis functions for each test ---

def analyze_step_response(dry, mc, la2a, sr, ax_pair):
    """Test 1: Step response analysis."""
    ax_gr, ax_metrics = ax_pair

    results = {}
    for label, wet, color in [("Multi-Comp", mc, "#2196F3"), ("LA-2A", la2a, "#FF5722")]:
        if wet is None:
            continue

        # Align lengths
        min_len = min(len(dry), len(wet))
        gr = gain_reduction_envelope(dry[:min_len], wet[:min_len], window_ms=5, sr=sr)
        t = np.arange(len(gr)) / sr

        ax_gr.plot(t, gr, label=label, color=color, linewidth=1.2)

        # Measure attack (should be in the 2-4s region where step occurs)
        # Find the step region
        step_start = int(2.0 * sr)
        step_end = int(6.0 * sr)
        if step_end <= len(gr):
            gr_region = gr[step_start:step_end]
            attack_ms = measure_attack_time(gr_region, sr)
            fast_rel, slow_rel = measure_release_time(gr_region, sr)
            results[label] = {
                "attack_ms": attack_ms,
                "fast_release_ms": fast_rel,
                "slow_release_ms": slow_rel,
                "max_gr_db": float(np.min(gr_region))
            }

    ax_gr.set_title("Step Response — Gain Reduction", fontweight="bold")
    ax_gr.set_xlabel("Time (s)")
    ax_gr.set_ylabel("Gain Reduction (dB)")
    ax_gr.legend()
    ax_gr.grid(True, alpha=0.3)
    ax_gr.set_xlim(1.5, 9.0)

    # Metrics table
    ax_metrics.axis("off")
    if results:
        table_data = []
        headers = ["Metric", "Multi-Comp", "LA-2A"]
        for metric in ["attack_ms", "fast_release_ms", "slow_release_ms", "max_gr_db"]:
            row = [metric.replace("_", " ").title()]
            for label in ["Multi-Comp", "LA-2A"]:
                val = results.get(label, {}).get(metric)
                if val is not None:
                    if "db" in metric:
                        row.append(f"{val:.1f} dB")
                    else:
                        row.append(f"{val:.1f} ms")
                else:
                    row.append("N/A")
            table_data.append(row)

        table = ax_metrics.table(cellText=table_data, colLabels=headers,
                                 loc="center", cellLoc="center")
        table.auto_set_font_size(False)
        table.set_fontsize(9)
        table.scale(1.0, 1.5)
        ax_metrics.set_title("Timing Measurements", fontweight="bold")


def analyze_program_dependency(dry, mc, la2a, sr, ax_pair):
    """Test 2: Program dependency analysis."""
    ax_short, ax_long = ax_pair

    for label, wet, color in [("Multi-Comp", mc, "#2196F3"), ("LA-2A", la2a, "#FF5722")]:
        if wet is None:
            continue
        min_len = min(len(dry), len(wet))
        gr = gain_reduction_envelope(dry[:min_len], wet[:min_len], window_ms=5, sr=sr)
        t = np.arange(len(gr)) / sr

        # Short burst region: starts at ~2s, release at ~2.1s, observe until ~5.1s
        short_start = int(1.5 * sr)
        short_end = int(5.5 * sr)
        if short_end <= len(gr):
            ax_short.plot(t[short_start:short_end] - t[short_start],
                         gr[short_start:short_end], label=label, color=color, linewidth=1.2)

        # Long burst region: starts at ~7.1s, release at ~12.1s, observe until ~15.1s
        long_start = int(6.5 * sr)
        long_end = int(15.5 * sr)
        if long_end <= len(gr):
            ax_long.plot(t[long_start:long_end] - t[long_start],
                        gr[long_start:long_end], label=label, color=color, linewidth=1.2)

    ax_short.set_title("Short Burst (100ms) → Release", fontweight="bold")
    ax_short.set_xlabel("Time (s)")
    ax_short.set_ylabel("GR (dB)")
    ax_short.legend()
    ax_short.grid(True, alpha=0.3)

    ax_long.set_title("Long Burst (5s) → Release", fontweight="bold")
    ax_long.set_xlabel("Time (s)")
    ax_long.set_ylabel("GR (dB)")
    ax_long.legend()
    ax_long.grid(True, alpha=0.3)


def analyze_release_curve(dry, mc, la2a, sr, axes):
    """Test 3: Release curves at different drive levels."""
    drive_levels = [-10, -6, -3]

    for i, (ax, drive_db) in enumerate(zip(axes, drive_levels)):
        for label, wet, color in [("Multi-Comp", mc, "#2196F3"), ("LA-2A", la2a, "#FF5722")]:
            if wet is None:
                continue
            min_len = min(len(dry), len(wet))
            gr = gain_reduction_envelope(dry[:min_len], wet[:min_len], window_ms=5, sr=sr)

            # Each burst is at: 2.0 + i*(1.0 + 4.0 + 1.5) = 2.0 + i*6.5
            burst_end = int((2.0 + i * 6.5 + 1.0) * sr)
            release_end = int((2.0 + i * 6.5 + 5.0) * sr)

            if release_end <= len(gr):
                region = gr[burst_end:release_end]
                t_region = np.arange(len(region)) / sr * 1000  # ms
                ax.plot(t_region, region, label=label, color=color, linewidth=1.2)

        ax.set_title(f"Release at {drive_db} dB drive", fontweight="bold")
        ax.set_xlabel("Time after burst (ms)")
        ax.set_ylabel("GR (dB)")
        ax.legend()
        ax.grid(True, alpha=0.3)


def analyze_frequency_response(dry, mc, la2a, sr, ax):
    """Test 5: Frequency-dependent compression."""
    freqs = [100, 300, 1000, 3000, 10000]

    for label, wet, color, marker in [("Multi-Comp", mc, "#2196F3", "o"), ("LA-2A", la2a, "#FF5722", "s")]:
        if wet is None:
            continue
        min_len = min(len(dry), len(wet))
        gr = gain_reduction_envelope(dry[:min_len], wet[:min_len], window_ms=50, sr=sr)

        gr_per_freq = []
        for i, freq in enumerate(freqs):
            # Each tone starts at 2.0 + i*3.0, lasts 2s, then 1s gap
            center = int((2.0 + i * 3.0 + 1.0) * sr)  # 1s into tone
            window = int(0.5 * sr)
            if center + window <= len(gr):
                avg_gr = np.mean(gr[center:center + window])
                gr_per_freq.append(avg_gr)
            else:
                gr_per_freq.append(np.nan)

        ax.plot(freqs, gr_per_freq, label=label, color=color, marker=marker, linewidth=1.5)

    ax.set_title("Frequency-Dependent Compression (Compress Mode)", fontweight="bold")
    ax.set_xlabel("Frequency (Hz)")
    ax.set_ylabel("Avg GR (dB)")
    ax.set_xscale("log")
    ax.legend()
    ax.grid(True, alpha=0.3)


def analyze_thd(dry, mc, la2a, sr, ax_pair):
    """Test 6: THD comparison."""
    ax_spectrum, ax_thd_bar = ax_pair

    # Low-level section: 2s to 12s; Hot section: 14s to 24s
    lo_start, lo_end = int(3 * sr), int(11 * sr)
    hi_start, hi_end = int(15 * sr), int(23 * sr)

    thd_results = {}

    for label, wet, color in [("Multi-Comp", mc, "#2196F3"), ("LA-2A", la2a, "#FF5722")]:
        if wet is None:
            continue

        # THD on hot signal (with compression)
        if hi_end <= len(wet):
            hot = wet[hi_start:hi_end]
            thd_hot = compute_thd(hot, 1000, sr)

            freqs, spec = compute_spectrum(hot[:int(sr * 0.5)], sr)
            ax_spectrum.plot(freqs, spec, label=f"{label} (driven)", color=color,
                           linewidth=1.0, alpha=0.8)

            thd_results[label] = thd_hot

    # Also plot dry for reference
    if dry is not None and hi_end <= len(dry):
        freqs, spec = compute_spectrum(dry[hi_start:hi_start + int(sr * 0.5)], sr)
        ax_spectrum.plot(freqs, spec, label="Dry", color="#999", linewidth=1.0, linestyle="--")

    ax_spectrum.set_title("Harmonic Spectrum (1kHz, driven)", fontweight="bold")
    ax_spectrum.set_xlabel("Frequency (Hz)")
    ax_spectrum.set_ylabel("Level (dB)")
    ax_spectrum.set_xlim(500, 15000)
    ax_spectrum.set_xscale("log")
    ax_spectrum.legend()
    ax_spectrum.grid(True, alpha=0.3)

    # THD bar chart
    if thd_results:
        labels = list(thd_results.keys())
        values = list(thd_results.values())
        colors = ["#2196F3" if "Multi" in l else "#FF5722" for l in labels]
        ax_thd_bar.bar(labels, values, color=colors)
        ax_thd_bar.set_title("THD % (driven 1kHz)", fontweight="bold")
        ax_thd_bar.set_ylabel("THD %")
        for i, v in enumerate(values):
            ax_thd_bar.text(i, v + 0.1, f"{v:.2f}%", ha="center", fontsize=10)


def analyze_gain_curve(dry, mc, la2a, sr, ax):
    """Test 8: Static gain reduction curve."""
    levels_db = list(range(-40, 1, 2))  # -40 to 0 in 2dB steps

    for label, wet, color, marker in [("Multi-Comp", mc, "#2196F3", "o"), ("LA-2A", la2a, "#FF5722", "s")]:
        if wet is None:
            continue

        input_levels = []
        output_levels = []

        for i, level_db in enumerate(levels_db):
            # Each tone is 1s, starting at 2.0s
            start = int((2.0 + i * 1.0 + 0.3) * sr)  # 300ms into each tone (settled)
            end = int((2.0 + i * 1.0 + 0.8) * sr)      # 800ms
            if start < min(len(dry), len(wet)) and end <= min(len(dry), len(wet)):
                in_rms = rms_envelope(dry[start:end], window_ms=50, sr=sr)
                out_rms = rms_envelope(wet[start:end], window_ms=50, sr=sr)
                in_db = 20 * np.log10(np.mean(in_rms) + 1e-10)
                out_db = 20 * np.log10(np.mean(out_rms) + 1e-10)
                input_levels.append(in_db)
                output_levels.append(out_db)

        if input_levels:
            ax.plot(input_levels, output_levels, label=label, color=color,
                   marker=marker, markersize=4, linewidth=1.5)

    # Unity line
    ax.plot([-50, 5], [-50, 5], "--", color="#999", label="Unity", linewidth=0.8)

    ax.set_title("Compression Curve (Input vs Output)", fontweight="bold")
    ax.set_xlabel("Input Level (dB)")
    ax.set_ylabel("Output Level (dB)")
    ax.legend()
    ax.grid(True, alpha=0.3)
    ax.set_xlim(-45, 5)
    ax.set_ylim(-45, 5)
    ax.set_aspect("equal")


# --- Main ---

def main():
    parser = argparse.ArgumentParser(description="Analyze LA-2A vs Multi-Comp comparison")
    parser.add_argument("--input-dir", default="./test_signals", help="Dry signal directory")
    parser.add_argument("--capture-dir", default="./captured", help="Captured output directory")
    parser.add_argument("--output", default="./comparison_report.png", help="Output plot file")
    args = parser.parse_args()

    if not os.path.exists(args.capture_dir):
        print(f"Error: capture directory '{args.capture_dir}' not found.")
        print("Run test signals through both plugins in Logic Pro first.")
        print("See generate_test_signals.py for instructions.")
        sys.exit(1)

    # Check what's available
    available_tests = []
    for test_name in ["01_step_response", "02_program_dependency", "03_release_curve",
                      "04_attack_transients", "05_frequency_response", "06_thd",
                      "07_pink_noise", "08_gain_curve"]:
        try:
            dry, mc, la2a, sr = load_trio(test_name, args.input_dir, args.capture_dir)
        except FileNotFoundError:
            continue
        except ValueError as e:
            print(f"Warning: Skipping {test_name} due to sample rate mismatch: {e}")
            continue
        if dry is not None and (mc is not None or la2a is not None):
            available_tests.append(test_name)

    if not available_tests:
        print("No matching test pairs found. Expected files like:")
        print("  captured/multicomp_01_step_response.wav")
        print("  captured/la2a_01_step_response.wav")

        # Show what IS in the capture dir
        if os.path.exists(args.capture_dir):
            files = os.listdir(args.capture_dir)
            if files:
                print(f"\nFiles found in {args.capture_dir}:")
                for f in sorted(files):
                    print(f"  {f}")
        sys.exit(1)

    print(f"Found {len(available_tests)} test(s) with captures: {', '.join(available_tests)}")
    print()

    # Create comprehensive figure
    fig = plt.figure(figsize=(20, 24))
    fig.suptitle("Multi-Comp Opto vs UA LA-2A Comparison", fontsize=16, fontweight="bold", y=0.98)

    gs = GridSpec(5, 2, figure=fig, hspace=0.4, wspace=0.3,
                  left=0.06, right=0.96, top=0.95, bottom=0.03)

    # Test 1: Step response
    if "01_step_response" in available_tests:
        dry, mc, la2a, sr = load_trio("01_step_response", args.input_dir, args.capture_dir)
        ax1 = fig.add_subplot(gs[0, 0])
        ax1b = fig.add_subplot(gs[0, 1])
        analyze_step_response(dry, mc, la2a, sr, (ax1, ax1b))
        print("✓ Step response analyzed")

    # Test 2: Program dependency
    if "02_program_dependency" in available_tests:
        dry, mc, la2a, sr = load_trio("02_program_dependency", args.input_dir, args.capture_dir)
        ax2a = fig.add_subplot(gs[1, 0])
        ax2b = fig.add_subplot(gs[1, 1])
        analyze_program_dependency(dry, mc, la2a, sr, (ax2a, ax2b))
        print("✓ Program dependency analyzed")

    # Test 3: Release curves
    if "03_release_curve" in available_tests:
        dry, mc, la2a, sr = load_trio("03_release_curve", args.input_dir, args.capture_dir)
        ax3a = fig.add_subplot(gs[2, 0])
        ax3b = fig.add_subplot(gs[2, 1])
        ax3c = fig.add_subplot(gs[3, 0])
        analyze_release_curve(dry, mc, la2a, sr, [ax3a, ax3b, ax3c])
        print("✓ Release curves analyzed")

    # Test 5: Frequency response
    if "05_frequency_response" in available_tests:
        dry, mc, la2a, sr = load_trio("05_frequency_response", args.input_dir, args.capture_dir)
        ax5 = fig.add_subplot(gs[3, 1])
        analyze_frequency_response(dry, mc, la2a, sr, ax5)
        print("✓ Frequency response analyzed")

    # Test 6: THD
    if "06_thd" in available_tests:
        dry, mc, la2a, sr = load_trio("06_thd", args.input_dir, args.capture_dir)
        ax6a = fig.add_subplot(gs[4, 0])
        ax6b = fig.add_subplot(gs[4, 1])
        analyze_thd(dry, mc, la2a, sr, (ax6a, ax6b))
        print("✓ THD analyzed")

    # Test 8: Gain curve (use remaining slot or overlay)
    if "08_gain_curve" in available_tests:
        dry, mc, la2a, sr = load_trio("08_gain_curve", args.input_dir, args.capture_dir)
        # If we have an empty slot, use it; otherwise create separate figure
        if "03_release_curve" not in available_tests:
            ax8 = fig.add_subplot(gs[2, 0])
        elif "05_frequency_response" not in available_tests:
            ax8 = fig.add_subplot(gs[3, 1])
        else:
            # Create a separate figure
            fig2, ax8 = plt.subplots(1, 1, figsize=(8, 8))
            fig2.suptitle("Compression Curve", fontsize=14, fontweight="bold")
        analyze_gain_curve(dry, mc, la2a, sr, ax8)
        # Save separate gain curve figure if it was created
        if "03_release_curve" in available_tests and "05_frequency_response" in available_tests:
            base, ext = os.path.splitext(args.output)
            fig2.savefig(f"{base}_gain_curve{ext}", dpi=150, bbox_inches="tight")
            plt.close(fig2)
        print("✓ Gain curve analyzed")

    plt.savefig(args.output, dpi=150, bbox_inches="tight")
    print(f"\n✓ Report saved to {args.output}")

    # Print text summary
    print(f"\n{'='*60}")
    print("SUMMARY")
    print(f"{'='*60}")

    if "01_step_response" in available_tests:
        dry, mc, la2a, sr = load_trio("01_step_response", args.input_dir, args.capture_dir)
        print("\nStep Response:")
        for label, wet in [("  Multi-Comp", mc), ("  LA-2A     ", la2a)]:
            if wet is None:
                continue
            min_len = min(len(dry), len(wet))
            gr = gain_reduction_envelope(dry[:min_len], wet[:min_len], window_ms=5, sr=sr)
            step_region = gr[int(2 * sr):int(6 * sr)] if len(gr) > int(6 * sr) else gr
            atk = measure_attack_time(step_region, sr)
            fast_rel, slow_rel = measure_release_time(step_region, sr)
            max_gr = float(np.min(step_region))
            print(f"{label}: GR={max_gr:.1f}dB, attack={atk:.1f}ms" if atk is not None else f"{label}: GR={max_gr:.1f}dB, attack=N/A", end="")
            if fast_rel is not None:
                print(f", fast_rel={fast_rel:.0f}ms", end="")
            if slow_rel is not None:
                print(f", full_rel={slow_rel:.0f}ms", end="")
            print()

    plt.show()

if __name__ == "__main__":
    main()
