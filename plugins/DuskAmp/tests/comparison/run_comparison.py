#!/usr/bin/env python3
"""DuskAmp DSP Accuracy Comparison.

Processes test signals through DuskAmp DSP mode for each amp model
and compares them against each other and against expected characteristics.

Since NAM profiles can't be loaded via pedalboard (requires file path UI),
this script compares the three DSP amp models against each other and
reports their spectral/dynamic characteristics.

Usage:
    python3 run_comparison.py [--plots]

Prerequisites:
    1. Run generate_test_signals.py first
    2. DuskAmp VST3 must be built and installed
"""

import os
import sys
import argparse
import numpy as np
import soundfile as sf
from pathlib import Path
from scipy.signal import welch, hilbert

SAMPLE_RATE = 44100
SCRIPT_DIR = Path(__file__).parent
SIGNALS_DIR = SCRIPT_DIR / "test_signals"
OUTPUT_DIR = SCRIPT_DIR / "results"

# Amp model names as exposed by the plugin
AMP_MODELS = {
    "Fender": "Blackface",         # amp_model parameter value
    "Marshall": "Plexi",
    "Vox": "British Combo",        # AC30 = "British Combo" in the plugin
}

# Drive levels to test
DRIVE_LEVELS = {
    "clean": 0.2,
    "crunch": 0.5,
    "cranked": 0.8,
}


def find_plugin():
    """Find DuskAmp VST3."""
    vst3 = Path.home() / "Library/Audio/Plug-Ins/VST3/DuskAmp.vst3"
    if vst3.exists():
        return str(vst3)
    return None


def load_duskamp(plugin_path):
    """Load DuskAmp with sane defaults."""
    from pedalboard import load_plugin
    plugin = load_plugin(plugin_path)
    # Disable effects that color the comparison
    plugin.delay = False
    plugin.reverb = False
    plugin.cabinet = False      # Disable cab so we compare raw amp tone
    plugin.bypass = False
    plugin.input_gain = 0.0
    plugin.output_level = 0.0
    plugin.bass = 0.5
    plugin.mid = 0.5
    plugin.treble = 0.5
    plugin.presence = 0.5
    plugin.resonance = 0.5
    return plugin


def process_signal(plugin, audio_stereo, sr):
    """Process stereo audio through the plugin."""
    output = plugin(audio_stereo, sr)
    return output


def spectral_analysis(signal, sr, label=""):
    """Compute spectral characteristics."""
    freqs, psd = welch(signal, sr, nperseg=4096)
    psd_db = 10 * np.log10(psd + 1e-20)

    # Find peak frequency
    guitar_mask = (freqs >= 60) & (freqs <= 8000)
    peak_idx = np.argmax(psd_db[guitar_mask])
    peak_freq = freqs[guitar_mask][peak_idx]

    # Compute spectral centroid (brightness measure)
    mask = freqs > 0
    centroid = np.sum(freqs[mask] * psd[mask]) / (np.sum(psd[mask]) + 1e-20)

    # Compute spectral rolloff (95% energy)
    cumsum = np.cumsum(psd[mask])
    rolloff_idx = np.searchsorted(cumsum, 0.95 * cumsum[-1])
    rolloff = freqs[mask][min(rolloff_idx, len(freqs[mask]) - 1)]

    return {
        "peak_freq": peak_freq,
        "centroid": centroid,
        "rolloff_95": rolloff,
        "psd_db": psd_db,
        "freqs": freqs,
    }


def dynamics_analysis(signal, sr):
    """Compute dynamic characteristics."""
    env = np.abs(hilbert(signal))

    # Smooth with 10ms window
    win = int(0.01 * sr)
    if win > 1:
        kernel = np.ones(win) / win
        env = np.convolve(env, kernel, mode='same')

    rms = np.sqrt(np.mean(signal ** 2) + 1e-20)
    peak = np.max(np.abs(signal))
    crest_factor = peak / (rms + 1e-20)

    # Dynamic range: ratio of loudest to quietest 10% RMS windows
    win_size = int(0.05 * sr)  # 50ms windows
    n_windows = len(signal) // win_size
    if n_windows > 2:
        rms_windows = []
        for i in range(n_windows):
            chunk = signal[i * win_size:(i + 1) * win_size]
            rms_windows.append(np.sqrt(np.mean(chunk ** 2) + 1e-20))
        rms_windows = np.sort(rms_windows)
        # Avoid division by near-zero
        quiet = np.mean(rms_windows[:max(1, n_windows // 10)])
        loud = np.mean(rms_windows[-max(1, n_windows // 10):])
        dynamic_range_db = 20 * np.log10((loud + 1e-10) / (quiet + 1e-10))
    else:
        dynamic_range_db = 0

    return {
        "rms_db": 20 * np.log10(rms + 1e-20),
        "peak_db": 20 * np.log10(peak + 1e-20),
        "crest_factor": crest_factor,
        "dynamic_range_db": dynamic_range_db,
        "envelope": env,
    }


def thd_analysis(signal, sr, fundamental=220.0):
    """Compute Total Harmonic Distortion."""
    n = len(signal)
    fft = np.fft.rfft(signal)
    freqs = np.fft.rfftfreq(n, 1.0 / sr)
    magnitudes = np.abs(fft)

    # Find fundamental and harmonics
    bin_width = sr / n
    fund_bin = int(round(fundamental / bin_width))
    search_range = max(2, int(10 / bin_width))  # ±10Hz search

    fund_mag = 0
    harm_power = 0

    for h in range(1, 11):  # Up to 10th harmonic
        target = fundamental * h
        if target > sr / 2:
            break
        center = int(round(target / bin_width))
        lo = max(0, center - search_range)
        hi = min(len(magnitudes), center + search_range + 1)
        peak_mag = np.max(magnitudes[lo:hi])

        if h == 1:
            fund_mag = peak_mag
        else:
            harm_power += peak_mag ** 2

    if fund_mag > 0:
        thd = np.sqrt(harm_power) / fund_mag * 100
    else:
        thd = 0

    return thd


def spectral_correlation(sig1, sig2, sr):
    """Correlation between two signals' spectra."""
    f1, psd1 = welch(sig1, sr, nperseg=4096)
    _, psd2 = welch(sig2, sr, nperseg=4096)
    psd1_db = 10 * np.log10(psd1 + 1e-20)
    psd2_db = 10 * np.log10(psd2 + 1e-20)
    mask = (f1 >= 60) & (f1 <= 8000)
    return np.corrcoef(psd1_db[mask], psd2_db[mask])[0, 1]


def envelope_correlation(sig1, sig2, sr):
    """Correlation between two signals' envelopes."""
    env1 = np.abs(hilbert(sig1))
    env2 = np.abs(hilbert(sig2))
    win = int(0.01 * sr)
    kernel = np.ones(win) / win
    env1 = np.convolve(env1, kernel, mode='same')
    env2 = np.convolve(env2, kernel, mode='same')
    return np.corrcoef(env1, env2)[0, 1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--plots", action="store_true")
    args = parser.parse_args()

    if not SIGNALS_DIR.exists():
        print("Run generate_test_signals.py first")
        sys.exit(1)

    plugin_path = find_plugin()
    if not plugin_path:
        print("DuskAmp VST3 not found. Build it first.")
        sys.exit(1)

    os.makedirs(OUTPUT_DIR, exist_ok=True)

    signal_files = sorted(SIGNALS_DIR.glob("*.wav"))
    print(f"Test signals: {len(signal_files)}")
    print(f"Plugin: {plugin_path}\n")

    # Process each signal through each amp at each drive level
    all_results = []

    for amp_name, model_value in AMP_MODELS.items():
        for drive_name, drive_val in DRIVE_LEVELS.items():
            print(f"\n{'='*60}")
            print(f"  {amp_name} — {drive_name} (drive={drive_val})")
            print(f"{'='*60}")

            plugin = load_duskamp(plugin_path)
            plugin.amp_model = model_value
            plugin.drive = drive_val

            for sig_file in signal_files:
                sig_name = sig_file.stem
                audio, sr = sf.read(str(sig_file))
                if audio.ndim == 1:
                    audio = np.column_stack([audio, audio])

                # Process
                output = process_signal(plugin, audio.T, sr)
                out_mono = output[0] if output.ndim > 1 else output

                # Skip silent output
                if np.max(np.abs(out_mono)) < 1e-6:
                    print(f"  {sig_name}: SILENT")
                    continue

                # Analyze
                spec = spectral_analysis(out_mono, sr)
                dyn = dynamics_analysis(out_mono, sr)

                # THD for sine-based signals
                thd = 0
                if "sine" in sig_name.lower() or "transient_A3" in sig_name:
                    thd = thd_analysis(out_mono, sr, fundamental=220.0)

                result = {
                    "amp": amp_name,
                    "drive": drive_name,
                    "signal": sig_name,
                    "rms_db": dyn["rms_db"],
                    "peak_db": dyn["peak_db"],
                    "crest": dyn["crest_factor"],
                    "dyn_range_db": dyn["dynamic_range_db"],
                    "centroid": spec["centroid"],
                    "rolloff": spec["rolloff_95"],
                    "thd": thd,
                }
                all_results.append(result)

                print(f"  {sig_name}: RMS {dyn['rms_db']:.1f}dB | "
                      f"Crest {dyn['crest_factor']:.1f} | "
                      f"Centroid {spec['centroid']:.0f}Hz | "
                      f"Rolloff {spec['rolloff_95']:.0f}Hz"
                      + (f" | THD {thd:.1f}%" if thd > 0 else ""))

                # Save output
                amp_dir = OUTPUT_DIR / f"{amp_name}_{drive_name}"
                os.makedirs(amp_dir, exist_ok=True)
                sf.write(str(amp_dir / f"{sig_name}.wav"), out_mono, sr, subtype='FLOAT')

            del plugin

    # Cross-amp comparison: how different do the amps sound from each other?
    print(f"\n\n{'='*70}")
    print("CROSS-AMP SPECTRAL COMPARISON (clean drive)")
    print(f"{'='*70}")

    # Load clean outputs and compare
    for sig_file in signal_files:
        sig_name = sig_file.stem
        outputs = {}
        for amp_name in AMP_MODELS:
            wav_path = OUTPUT_DIR / f"{amp_name}_clean" / f"{sig_name}.wav"
            if wav_path.exists():
                data, _ = sf.read(str(wav_path))
                outputs[amp_name] = data

        if len(outputs) >= 2:
            print(f"\n  {sig_name}:")
            amp_names = list(outputs.keys())
            for i in range(len(amp_names)):
                for j in range(i + 1, len(amp_names)):
                    a, b = amp_names[i], amp_names[j]
                    min_len = min(len(outputs[a]), len(outputs[b]))
                    sc = spectral_correlation(outputs[a][:min_len], outputs[b][:min_len], SAMPLE_RATE)
                    ec = envelope_correlation(outputs[a][:min_len], outputs[b][:min_len], SAMPLE_RATE)
                    print(f"    {a} vs {b}: spectral={sc:.3f}, envelope={ec:.3f}")

    # Summary table
    print(f"\n\n{'='*70}")
    print("SUMMARY: Per-Amp Characteristics")
    print(f"{'='*70}")
    print(f"{'Amp':<10} {'Drive':<8} {'RMS dB':>8} {'Crest':>7} {'Centroid':>9} {'Rolloff':>9} {'THD%':>7}")
    print("-" * 62)
    for r in all_results:
        thd_str = f"{r['thd']:.1f}" if r['thd'] > 0 else "-"
        # Average across signals for this amp+drive

    # Averaged by amp+drive
    from collections import defaultdict
    grouped = defaultdict(list)
    for r in all_results:
        key = (r["amp"], r["drive"])
        grouped[key].append(r)

    for (amp, drive), items in sorted(grouped.items()):
        avg_rms = np.mean([r["rms_db"] for r in items])
        avg_crest = np.mean([r["crest"] for r in items])
        avg_cent = np.mean([r["centroid"] for r in items])
        avg_roll = np.mean([r["rolloff"] for r in items])
        thd_items = [r["thd"] for r in items if r["thd"] > 0]
        avg_thd = np.mean(thd_items) if thd_items else 0
        thd_str = f"{avg_thd:.1f}" if avg_thd > 0 else "-"
        print(f"{amp:<10} {drive:<8} {avg_rms:>8.1f} {avg_crest:>7.1f} "
              f"{avg_cent:>9.0f} {avg_roll:>9.0f} {thd_str:>7}")

    # What to expect:
    print(f"\n{'='*70}")
    print("EXPECTED CHARACTERISTICS (from real amps)")
    print(f"{'='*70}")
    print("""
    Fender Deluxe Reverb:
      - Brightest clean tone (highest centroid)
      - Most headroom (highest crest factor at clean)
      - Heavy NFB = controlled, polished sound
      - GZ34 sag = musical bloom on sustained notes

    Vox AC30 Top Boost:
      - Mid-focused, "chimey" (moderate centroid)
      - Most compressed (lowest crest at all drive levels — Class A)
      - No NFB = raw, harmonically rich
      - Heaviest sag (Class A constant current draw)
      - Even harmonics present (single-ended output)

    Marshall 1959 Plexi:
      - Mid-heavy, aggressive (mid-range centroid)
      - Tight response (silicon rectifier, minimal sag)
      - Moderate NFB = controlled bark
      - Highest THD at cranked settings (EL34 hard clip)
    """)

    if args.plots:
        generate_plots(all_results)


def generate_plots(results):
    """Generate comparison plots."""
    import matplotlib.pyplot as plt
    from collections import defaultdict

    grouped = defaultdict(list)
    for r in results:
        grouped[(r["amp"], r["drive"])].append(r)

    # Plot 1: Centroid by amp and drive (brightness comparison)
    fig, axes = plt.subplots(1, 3, figsize=(14, 5))

    for ax, metric, label in zip(
        axes,
        ["centroid", "crest", "rms_db"],
        ["Spectral Centroid (Hz)\n(higher = brighter)", "Crest Factor\n(higher = more dynamic range)",
         "RMS Level (dBFS)"]
    ):
        for amp in AMP_MODELS:
            drives = []
            values = []
            for drive_name in DRIVE_LEVELS:
                items = grouped.get((amp, drive_name), [])
                if items:
                    drives.append(drive_name)
                    values.append(np.mean([r[metric] for r in items]))
            ax.plot(drives, values, 'o-', label=amp, linewidth=2, markersize=8)
        ax.set_title(label)
        ax.legend()
        ax.grid(True, alpha=0.3)

    plt.suptitle("DuskAmp DSP: Three Amp Models Comparison", fontsize=14)
    plt.tight_layout()
    plot_path = OUTPUT_DIR / "amp_comparison.png"
    plt.savefig(str(plot_path), dpi=150)
    print(f"\nPlot saved: {plot_path}")
    plt.close()


if __name__ == "__main__":
    main()
