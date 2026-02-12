#!/usr/bin/env python3
"""
EDT Diagnostic — Understand why the WCS engine produces correct RT60 but
EDT=0, EDC Shape=0, and Stereo=0 when compared to real Lexicon 200 hardware.

Generates comprehensive visual comparison between hardware IR (Plate 7)
and the WCS engine output using the optimized coefficients.
"""

import os
import sys
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from scipy import signal

# Module imports
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wcs_engine import WCSEngine, ir_name_to_program

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'velvet90_match'))
from ir_analysis import analyze_ir, load_ir, compute_edc, measure_rt60

# ── Configuration ──
HARDWARE_IR_PATH = "/Users/marckorte/Downloads/Lexicon 200 impulse set/Plate 7_dc.wav"
OUTPUT_PLOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "edt_diagnostic.png")

# Optimized coefficients from latest matching run
COEFFICIENTS = np.array([
    +0.2080, -0.2285, +0.9727, +0.7369,  # C0-C3
    -0.1235, -0.8853, +0.4857, +0.8716,  # C4-C7
    -0.7308, +0.7080, -0.4155, +0.2305,  # C8-CB
    +0.2506, -0.7978, -0.9562, +0.1141,  # CC-CF
])
ROLLOFF_HZ = 3323.0
DAMPING = 0.9540
PROGRAM = 1  # Plate


def smoothed_envelope_db(ir, sr, window_ms=5.0):
    """Compute smoothed amplitude envelope in dB."""
    window_samples = max(int(sr * window_ms / 1000), 1)
    energy = ir ** 2
    # Moving average
    kernel = np.ones(window_samples) / window_samples
    smoothed = np.convolve(energy, kernel, mode='same')
    smoothed = np.maximum(smoothed, 1e-20)
    return 10.0 * np.log10(smoothed / np.max(smoothed))


def stereo_correlation_over_time(ir_l, ir_r, sr, window_ms=10.0, hop_ms=5.0):
    """Compute sliding-window stereo cross-correlation."""
    window_samples = int(sr * window_ms / 1000)
    hop_samples = int(sr * hop_ms / 1000)
    n = min(len(ir_l), len(ir_r))

    times = []
    correlations = []
    pos = 0
    while pos + window_samples <= n:
        l_chunk = ir_l[pos:pos + window_samples]
        r_chunk = ir_r[pos:pos + window_samples]

        l_energy = np.sqrt(np.sum(l_chunk ** 2))
        r_energy = np.sqrt(np.sum(r_chunk ** 2))

        if l_energy > 1e-10 and r_energy > 1e-10:
            corr = np.sum(l_chunk * r_chunk) / (l_energy * r_energy)
        else:
            corr = 0.0

        correlations.append(corr)
        times.append((pos + window_samples / 2) / sr)
        pos += hop_samples

    return np.array(times), np.array(correlations)


def spectral_centroid_over_time(ir, sr, window_ms=30.0, hop_ms=15.0):
    """Track spectral centroid over time using sliding FFT."""
    window_samples = int(sr * window_ms / 1000)
    hop_samples = int(sr * hop_ms / 1000)
    n_fft = max(2048, window_samples)

    freqs = np.fft.rfftfreq(n_fft, 1.0 / sr)
    times = []
    centroids = []

    window = signal.windows.hann(window_samples)
    pos = 0
    while pos + window_samples <= len(ir):
        chunk = ir[pos:pos + window_samples] * window
        spectrum = np.abs(np.fft.rfft(chunk, n=n_fft))
        total = np.sum(spectrum)
        if total > 1e-10:
            centroid = np.sum(freqs * spectrum) / total
        else:
            centroid = 0.0
        centroids.append(centroid)
        times.append((pos + window_samples / 2) / sr)
        pos += hop_samples

    return np.array(times), np.array(centroids)


def edc_slope_at_time(edc_db, sr, t_sec, window_s=0.05):
    """Measure the local slope (dB/s) of the EDC at a given time."""
    idx = int(t_sec * sr)
    half_win = int(window_s * sr / 2)
    start = max(0, idx - half_win)
    end = min(len(edc_db), idx + half_win)
    if end - start < 10:
        return 0.0
    t = np.arange(start, end) / sr
    segment = edc_db[start:end]
    if len(t) < 2:
        return 0.0
    coeffs = np.polyfit(t, segment, 1)
    return coeffs[0]  # dB/s


def main():
    print("=" * 70)
    print("EDT DIAGNOSTIC: Lexicon 200 Plate 7 vs WCS Engine")
    print("=" * 70)

    # ── 1. Load hardware IR ──
    print(f"\nLoading hardware IR: {HARDWARE_IR_PATH}")
    hw_data, hw_sr = load_ir(HARDWARE_IR_PATH)
    print(f"  Shape: {hw_data.shape}, SR: {hw_sr}")

    # Normalize
    hw_peak = np.max(np.abs(hw_data))
    hw_data = hw_data / hw_peak
    hw_mono = hw_data[0]
    hw_l = hw_data[0] if hw_data.shape[0] >= 2 else hw_data[0]
    hw_r = hw_data[1] if hw_data.shape[0] >= 2 else hw_data[0]

    # ── 2. Generate engine IR via ESS ──
    print(f"\nGenerating engine IR via ESS deconvolution...")
    print(f"  Program: {PROGRAM} (Plate)")
    print(f"  Rolloff: {ROLLOFF_HZ} Hz, Damping: {DAMPING}")
    print(f"  Coefficients: {', '.join(f'C{i:X}={COEFFICIENTS[i]:+.4f}' for i in range(16))}")

    engine = WCSEngine(program=PROGRAM, sr=hw_sr)

    # Duration: match hardware IR length, at least 3x RT60
    hw_profile = analyze_ir(hw_data, hw_sr, name="Hardware_Plate7")
    duration_s = max(hw_profile.rt60 * 3.0, hw_data.shape[1] / hw_sr, 3.0)
    duration_s = min(duration_s, 6.0)
    print(f"  IR Duration: {duration_s:.1f}s at {hw_sr} Hz")

    eng_data = engine.generate_ir_ess(
        duration_s=duration_s,
        coefficients=COEFFICIENTS,
        rolloff_hz=ROLLOFF_HZ,
        sweep_duration_s=4.0,
        sweep_level=0.01,
        damping=DAMPING,
    )

    # Normalize
    eng_peak = np.max(np.abs(eng_data))
    if eng_peak < 1e-10:
        print("ERROR: Engine produced silence!")
        return
    eng_data = eng_data / eng_peak
    print(f"  Engine output shape: {eng_data.shape}, peak before norm: {eng_peak:.6f}")

    eng_mono = eng_data[0]
    eng_l = eng_data[0]
    eng_r = eng_data[1] if eng_data.shape[0] >= 2 else eng_data[0]

    # Trim both to same length for fair comparison
    min_len = min(len(hw_mono), len(eng_mono))

    # ── 3. Full IR analysis ──
    print(f"\n{'─' * 50}")
    print("FULL PROFILE ANALYSIS")
    print(f"{'─' * 50}")

    eng_profile = analyze_ir(eng_data, hw_sr, name="Engine_Plate7")

    print(f"\n{'Metric':<25s} {'Hardware':>12s} {'Engine':>12s} {'Diff':>12s}")
    print(f"{'─' * 61}")
    print(f"{'RT60 (T30)':<25s} {hw_profile.rt60:12.4f}s {eng_profile.rt60:12.4f}s {eng_profile.rt60 - hw_profile.rt60:+12.4f}s")
    print(f"{'RT60 (T20)':<25s} {hw_profile.rt60_t20:12.4f}s {eng_profile.rt60_t20:12.4f}s {eng_profile.rt60_t20 - hw_profile.rt60_t20:+12.4f}s")
    print(f"{'EDT':<25s} {hw_profile.edt:12.4f}s {eng_profile.edt:12.4f}s {eng_profile.edt - hw_profile.edt:+12.4f}s")
    print(f"{'Pre-delay':<25s} {hw_profile.pre_delay_ms:12.1f}ms {eng_profile.pre_delay_ms:12.1f}ms {eng_profile.pre_delay_ms - hw_profile.pre_delay_ms:+12.1f}ms")
    print(f"{'Peak amplitude':<25s} {hw_profile.peak_amplitude:12.6f} {eng_profile.peak_amplitude:12.6f}")
    print(f"{'Stereo correlation':<25s} {hw_profile.stereo_correlation:12.4f} {eng_profile.stereo_correlation:12.4f} {eng_profile.stereo_correlation - hw_profile.stereo_correlation:+12.4f}")
    print(f"{'Stereo width':<25s} {hw_profile.width_estimate:12.4f} {eng_profile.width_estimate:12.4f} {eng_profile.width_estimate - hw_profile.width_estimate:+12.4f}")

    # ── 4. Detailed energy analysis ──
    print(f"\n{'─' * 50}")
    print("ENERGY BY TIME SEGMENT")
    print(f"{'─' * 50}")

    segments = [(0, 10), (10, 50), (50, 100), (100, 200), (200, 500), (500, 1000), (1000, 2000)]
    print(f"{'Segment':<15s} {'HW Energy (dB)':>15s} {'Eng Energy (dB)':>15s} {'Diff (dB)':>12s}")
    for start_ms, end_ms in segments:
        s0 = int(hw_sr * start_ms / 1000)
        s1 = int(hw_sr * end_ms / 1000)

        s1_hw = min(s1, len(hw_mono))
        s1_eng = min(s1, len(eng_mono))

        e_hw = np.sum(hw_mono[s0:s1_hw] ** 2) if s0 < s1_hw else 1e-20
        e_eng = np.sum(eng_mono[s0:s1_eng] ** 2) if s0 < s1_eng else 1e-20

        e_hw_db = 10 * np.log10(max(e_hw, 1e-20))
        e_eng_db = 10 * np.log10(max(e_eng, 1e-20))

        print(f"  {start_ms:4d}-{end_ms:4d}ms   {e_hw_db:+12.1f} dB   {e_eng_db:+12.1f} dB   {e_eng_db - e_hw_db:+10.1f} dB")

    # ── 5. EDC slope analysis ──
    print(f"\n{'─' * 50}")
    print("EDC SLOPE AT KEY TIME POINTS")
    print(f"{'─' * 50}")

    edc_hw = compute_edc(hw_mono)
    edc_eng = compute_edc(eng_mono[:min_len])

    print(f"{'Time':>8s}  {'HW EDC (dB)':>12s}  {'Eng EDC (dB)':>13s}  {'HW Slope':>12s}  {'Eng Slope':>12s}")
    time_points = [0.01, 0.02, 0.05, 0.10, 0.15, 0.20, 0.30, 0.50, 0.70, 1.00, 1.50, 2.00]
    for t in time_points:
        idx_hw = min(int(t * hw_sr), len(edc_hw) - 1)
        idx_eng = min(int(t * hw_sr), len(edc_eng) - 1)

        hw_val = edc_hw[idx_hw] if idx_hw < len(edc_hw) else -99.0
        eng_val = edc_eng[idx_eng] if idx_eng < len(edc_eng) else -99.0

        hw_slope = edc_slope_at_time(edc_hw, hw_sr, t)
        eng_slope = edc_slope_at_time(edc_eng, hw_sr, t)

        print(f"  {t:6.2f}s  {hw_val:12.2f} dB  {eng_val:12.2f} dB  {hw_slope:10.1f} dB/s  {eng_slope:10.1f} dB/s")

    # ── 6. EDT diagnostic (where -10dB is reached) ──
    print(f"\n{'─' * 50}")
    print("EDT DIAGNOSTIC — Where EDC crosses key thresholds")
    print(f"{'─' * 50}")

    for threshold in [0.0, -1.0, -2.0, -5.0, -10.0, -20.0, -30.0, -40.0, -50.0, -60.0]:
        idx_hw = np.searchsorted(-edc_hw, -threshold) if threshold <= 0 else 0
        idx_eng = np.searchsorted(-edc_eng, -threshold) if threshold <= 0 else 0

        t_hw = idx_hw / hw_sr if idx_hw < len(edc_hw) else float('inf')
        t_eng = idx_eng / hw_sr if idx_eng < len(edc_eng) else float('inf')

        hw_str = f"{t_hw:.4f}s" if t_hw < 100 else "NEVER"
        eng_str = f"{t_eng:.4f}s" if t_eng < 100 else "NEVER"

        print(f"  EDC = {threshold:5.0f} dB:  HW = {hw_str:>10s}  Eng = {eng_str:>10s}")

    # ── 7. Band RT60 ──
    print(f"\n{'─' * 50}")
    print("BAND RT60 COMPARISON")
    print(f"{'─' * 50}")

    from ir_analysis import band_key_hz
    common_bands = set(hw_profile.band_rt60.keys()) & set(eng_profile.band_rt60.keys())
    print(f"{'Band':>8s}  {'HW RT60':>10s}  {'Eng RT60':>10s}  {'Diff':>10s}")
    for band in sorted(common_bands, key=band_key_hz):
        hw_rt = hw_profile.band_rt60[band]
        eng_rt = eng_profile.band_rt60[band]
        print(f"  {band:>6s}  {hw_rt:8.3f}s   {eng_rt:8.3f}s   {eng_rt - hw_rt:+8.3f}s")

    # ── 8. Generate plots ──
    print(f"\n{'─' * 50}")
    print("GENERATING PLOTS")
    print(f"{'─' * 50}")

    fig, axes = plt.subplots(2, 2, figsize=(16, 12))
    fig.suptitle('EDT Diagnostic: Lexicon 200 Plate 7 vs WCS Engine', fontsize=14, fontweight='bold')

    # ─── Plot A: EDC overlay ───
    ax = axes[0, 0]
    t_hw_edc = np.arange(len(edc_hw)) / hw_sr
    t_eng_edc = np.arange(len(edc_eng)) / hw_sr

    ax.plot(t_hw_edc, edc_hw, 'b-', linewidth=1.5, label='Hardware', alpha=0.8)
    ax.plot(t_eng_edc, edc_eng, 'r-', linewidth=1.5, label='Engine', alpha=0.8)

    # Mark EDT region (0 to -10dB)
    ax.axhline(-10, color='green', linestyle='--', linewidth=0.8, alpha=0.5, label='-10dB (EDT)')
    ax.axhline(-35, color='orange', linestyle='--', linewidth=0.8, alpha=0.5, label='-35dB (T30 end)')
    ax.axhline(-5, color='purple', linestyle='--', linewidth=0.8, alpha=0.5, label='-5dB (T30 start)')

    ax.set_xlim(0, min(3.0, max(t_hw_edc[-1], t_eng_edc[-1])))
    ax.set_ylim(-70, 5)
    ax.set_xlabel('Time (s)')
    ax.set_ylabel('Energy (dB)')
    ax.set_title('Energy Decay Curve (EDC)')
    ax.legend(fontsize=8, loc='upper right')
    ax.grid(True, alpha=0.3)

    # Add text annotations for RT60 and EDT
    ax.text(0.02, 0.15,
            f'HW: RT60={hw_profile.rt60:.3f}s, EDT={hw_profile.edt:.3f}s\n'
            f'Eng: RT60={eng_profile.rt60:.3f}s, EDT={eng_profile.edt:.3f}s',
            transform=ax.transAxes, fontsize=8,
            bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.8),
            verticalalignment='bottom')

    # ─── Plot B: Envelope (first 200ms) ───
    ax = axes[0, 1]

    # Compute envelopes
    samples_200ms = int(hw_sr * 0.2)
    hw_env = smoothed_envelope_db(hw_mono[:min(samples_200ms, len(hw_mono))], hw_sr, window_ms=2.0)
    eng_env = smoothed_envelope_db(eng_mono[:min(samples_200ms, len(eng_mono))], hw_sr, window_ms=2.0)

    t_hw_env = np.arange(len(hw_env)) / hw_sr * 1000  # ms
    t_eng_env = np.arange(len(eng_env)) / hw_sr * 1000  # ms

    ax.plot(t_hw_env, hw_env, 'b-', linewidth=1.0, label='Hardware', alpha=0.8)
    ax.plot(t_eng_env, eng_env, 'r-', linewidth=1.0, label='Engine', alpha=0.8)

    ax.set_xlim(0, 200)
    ax.set_ylim(-60, 5)
    ax.set_xlabel('Time (ms)')
    ax.set_ylabel('Amplitude (dB)')
    ax.set_title('Early Reflections Envelope (0-200ms)')
    ax.legend(fontsize=8, loc='upper right')
    ax.grid(True, alpha=0.3)

    # ─── Plot C: Stereo correlation over time ───
    ax = axes[1, 0]

    if hw_data.shape[0] >= 2:
        t_hw_corr, hw_corr = stereo_correlation_over_time(hw_l, hw_r, hw_sr)
        ax.plot(t_hw_corr, hw_corr, 'b-', linewidth=1.0, label='Hardware', alpha=0.8)

    if eng_data.shape[0] >= 2:
        t_eng_corr, eng_corr = stereo_correlation_over_time(eng_l, eng_r, hw_sr)
        ax.plot(t_eng_corr, eng_corr, 'r-', linewidth=1.0, label='Engine', alpha=0.8)

    ax.set_xlim(0, min(2.0, duration_s))
    ax.set_ylim(-1.1, 1.1)
    ax.axhline(0, color='gray', linestyle='-', linewidth=0.5)
    ax.set_xlabel('Time (s)')
    ax.set_ylabel('Correlation')
    ax.set_title('Stereo Cross-Correlation (10ms window)')
    ax.legend(fontsize=8, loc='upper right')
    ax.grid(True, alpha=0.3)

    # Add text annotations for overall stereo
    ax.text(0.02, 0.05,
            f'HW: corr={hw_profile.stereo_correlation:.3f}, width={hw_profile.width_estimate:.3f}\n'
            f'Eng: corr={eng_profile.stereo_correlation:.3f}, width={eng_profile.width_estimate:.3f}',
            transform=ax.transAxes, fontsize=8,
            bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.8),
            verticalalignment='bottom')

    # ─── Plot D: Spectral centroid over time ───
    ax = axes[1, 1]

    t_hw_sc, hw_sc = spectral_centroid_over_time(hw_mono, hw_sr)
    t_eng_sc, eng_sc = spectral_centroid_over_time(eng_mono, hw_sr)

    ax.plot(t_hw_sc, hw_sc, 'b-', linewidth=1.0, label='Hardware', alpha=0.8)
    ax.plot(t_eng_sc, eng_sc, 'r-', linewidth=1.0, label='Engine', alpha=0.8)

    ax.set_xlim(0, min(2.0, duration_s))
    ax.set_ylim(0, min(max(np.max(hw_sc) * 1.2, np.max(eng_sc) * 1.2), hw_sr / 2))
    ax.set_xlabel('Time (s)')
    ax.set_ylabel('Spectral Centroid (Hz)')
    ax.set_title('Spectral Centroid Evolution')
    ax.legend(fontsize=8, loc='upper right')
    ax.grid(True, alpha=0.3)

    plt.tight_layout()
    plt.savefig(OUTPUT_PLOT, dpi=150, bbox_inches='tight')
    print(f"  Saved: {OUTPUT_PLOT}")

    # ── 9. Summary diagnosis ──
    print(f"\n{'=' * 70}")
    print("DIAGNOSIS SUMMARY")
    print(f"{'=' * 70}")

    # EDT issue
    edt_ratio = eng_profile.edt / max(hw_profile.edt, 0.001)
    print(f"\n  EDT: Engine = {eng_profile.edt:.4f}s, Hardware = {hw_profile.edt:.4f}s")
    print(f"       Ratio: {edt_ratio:.1f}x")
    if eng_profile.edt > hw_profile.edt * 5:
        print(f"       PROBLEM: Engine EDT is {edt_ratio:.0f}x too long!")
        print(f"       This means the EDC is nearly FLAT in the 0 to -10dB region.")
        print(f"       The energy does not drop quickly enough in the early part.")

        # Check if the issue is the initial buildup
        idx_5db_hw = np.searchsorted(-edc_hw, 5.0)
        idx_5db_eng = np.searchsorted(-edc_eng, 5.0)
        t_5db_hw = idx_5db_hw / hw_sr
        t_5db_eng = idx_5db_eng / hw_sr

        print(f"\n       -5dB crossing: HW={t_5db_hw:.4f}s, Eng={t_5db_eng:.4f}s")

        idx_10db_hw = np.searchsorted(-edc_hw, 10.0)
        idx_10db_eng = np.searchsorted(-edc_eng, 10.0)
        t_10db_hw = idx_10db_hw / hw_sr
        t_10db_eng = idx_10db_eng / hw_sr

        print(f"       -10dB crossing: HW={t_10db_hw:.4f}s, Eng={t_10db_eng:.4f}s")

        if t_10db_eng > 10:
            print(f"       ENGINE NEVER REACHES -10dB in the IR duration!")
            print(f"       This explains EDT=~infinity.")
    elif eng_profile.edt < hw_profile.edt * 0.2:
        print(f"       PROBLEM: Engine EDT is {edt_ratio:.2f}x too short!")
    else:
        print(f"       OK: EDT is within reasonable range.")

    # Stereo issue
    print(f"\n  Stereo: Engine corr = {eng_profile.stereo_correlation:.4f}")
    print(f"          Hardware corr = {hw_profile.stereo_correlation:.4f}")
    if abs(eng_profile.stereo_correlation) > 0.95 and abs(hw_profile.stereo_correlation) < 0.5:
        print(f"       PROBLEM: Engine is nearly mono (corr~1), hardware is decorrelated.")
        print(f"       The L/R channels are too similar in the engine.")

    # Early energy distribution
    early_hw = np.sum(hw_mono[:int(hw_sr * 0.05)] ** 2)
    early_eng = np.sum(eng_mono[:int(hw_sr * 0.05)] ** 2)
    late_hw = np.sum(hw_mono[int(hw_sr * 0.2):int(hw_sr * 0.5)] ** 2) if len(hw_mono) > int(hw_sr * 0.5) else 1e-20
    late_eng = np.sum(eng_mono[int(hw_sr * 0.2):int(hw_sr * 0.5)] ** 2) if len(eng_mono) > int(hw_sr * 0.5) else 1e-20

    early_late_ratio_hw = 10 * np.log10(max(early_hw, 1e-20) / max(late_hw, 1e-20))
    early_late_ratio_eng = 10 * np.log10(max(early_eng, 1e-20) / max(late_eng, 1e-20))

    print(f"\n  Early/Late energy ratio (0-50ms vs 200-500ms):")
    print(f"       Hardware: {early_late_ratio_hw:+.1f} dB")
    print(f"       Engine:   {early_late_ratio_eng:+.1f} dB")
    if early_late_ratio_eng < early_late_ratio_hw - 10:
        print(f"       PROBLEM: Engine has relatively MORE late energy than hardware.")
        print(f"       Early reflections are too weak relative to reverb tail.")

    print(f"\n  RT60: Engine = {eng_profile.rt60:.4f}s, Hardware = {hw_profile.rt60:.4f}s")
    print(f"       Diff = {eng_profile.rt60 - hw_profile.rt60:+.4f}s (good match)")

    print(f"\nDone.")


if __name__ == '__main__':
    main()
