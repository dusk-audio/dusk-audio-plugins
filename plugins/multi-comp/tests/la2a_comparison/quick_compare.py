#!/usr/bin/env python3
"""
Quick comparison of Multi-Comp Opto vs UA LA-2A bounced audio.

Expects:
  - Dry test signals in ./test_signals/
  - Bounced outputs in $CAPTURED_DIR (default: ./captured/)
    Files: LA2A_1.wav and Multi Comp_1.wav

Usage:
    python3 quick_compare.py
"""
import numpy as np
import soundfile as sf
import os

CAPTURED_DIR = os.environ.get("CAPTURED_DIR",
    os.path.join(os.path.dirname(__file__), "captured"))
DRY_DIR = os.path.join(os.path.dirname(__file__), "test_signals")


def db(x):
    return 20 * np.log10(np.maximum(np.abs(x), 1e-10))


def rms_db(x):
    if x.size == 0:
        return -200.0
    return 10 * np.log10(np.maximum(np.mean(x ** 2), 1e-20))


def load_mono(path):
    """Load WAV, return mono (left channel if stereo)."""
    data, sr = sf.read(path)
    if data.ndim == 2:
        data = data[:, 0]
    return data, sr


def envelope_db(signal, sr, window_ms=50):
    """RMS envelope in dB with given window size."""
    win = max(1, int(sr * window_ms / 1000))
    # Pad signal
    left_pad = win // 2
    padded = np.pad(signal ** 2, (left_pad, win - left_pad), mode='edge')
    cumsum = np.cumsum(padded)
    rms_sq = (cumsum[win:] - cumsum[:-win]) / win
    env = 10 * np.log10(np.maximum(rms_sq[:len(signal)], 1e-20))
    return env


def find_test_boundaries(dry_signal, sr):
    """Find where each test segment starts/ends based on silence gaps."""
    env = envelope_db(dry_signal, sr, window_ms=100)
    # Tests are separated by silence regions (<-55 dB)
    is_active = env > -55

    # Find transitions
    boundaries = []
    in_segment = False
    start = 0
    for i in range(len(is_active)):
        if is_active[i] and not in_segment:
            start = i
            in_segment = True
        elif not is_active[i] and in_segment:
            # Only count segments > 0.1s
            if (i - start) / sr > 0.1:
                boundaries.append((start, i))
            in_segment = False
    if in_segment:
        boundaries.append((start, len(is_active)))

    return boundaries


def analyze_segment(dry, la2a, multicomp, sr, name):
    """Analyze one test segment."""
    print(f"\n{'='*70}")
    print(f"  {name}")
    print(f"{'='*70}")

    # Overall RMS levels
    dry_rms = rms_db(dry)
    la2a_rms = rms_db(la2a)
    mc_rms = rms_db(multicomp)

    print(f"  {'Signal':<20} {'RMS (dB)':>10} {'vs Dry':>10} {'vs LA-2A':>12}")
    print(f"  {'-'*52}")
    print(f"  {'Dry':<20} {dry_rms:>10.1f}")
    print(f"  {'LA-2A':<20} {la2a_rms:>10.1f} {la2a_rms-dry_rms:>+10.1f}")
    print(f"  {'Multi-Comp':<20} {mc_rms:>10.1f} {mc_rms-dry_rms:>+10.1f} {mc_rms-la2a_rms:>+12.1f}")

    # Gain reduction (envelope comparison)
    dry_env = envelope_db(dry, sr)
    la2a_env = envelope_db(la2a, sr)
    mc_env = envelope_db(multicomp, sr)

    la2a_gr = la2a_env - dry_env
    mc_gr = mc_env - dry_env

    # Stats on gain reduction where signal is present
    active = dry_env > -50
    if np.any(active):
        print(f"\n  Gain Reduction (where signal > -50 dB):")
        print(f"  {'':20} {'Mean GR':>10} {'Most GR':>10} {'Least GR':>10}")
        print(f"  {'-'*50}")
        print(f"  {'LA-2A':<20} {np.mean(la2a_gr[active]):>+10.1f} {np.min(la2a_gr[active]):>+10.1f} {np.max(la2a_gr[active]):>+10.1f}")
        print(f"  {'Multi-Comp':<20} {np.mean(mc_gr[active]):>+10.1f} {np.min(mc_gr[active]):>+10.1f} {np.max(mc_gr[active]):>+10.1f}")

    return {
        'dry_rms': dry_rms, 'la2a_rms': la2a_rms, 'mc_rms': mc_rms,
        'la2a_gr_mean': np.mean(la2a_gr[active]) if np.any(active) else 0,
        'mc_gr_mean': np.mean(mc_gr[active]) if np.any(active) else 0,
        'la2a_most_gr': float(np.min(la2a_gr[active])) if np.any(active) else 0,
        'mc_most_gr': float(np.min(mc_gr[active])) if np.any(active) else 0,
    }


def thd_analysis(signal, sr, fundamental=1000, n_harmonics=8):
    """Measure THD of a signal segment."""
    N = len(signal)
    spectrum = np.abs(np.fft.rfft(signal * np.hanning(N))) / N
    freqs = np.fft.rfftfreq(N, 1/sr)

    # Find fundamental
    fund_idx = np.argmin(np.abs(freqs - fundamental))
    fund_power = spectrum[fund_idx] ** 2

    # Sum harmonic power
    harm_power = 0
    harmonics = {}
    for h in range(2, n_harmonics + 1):
        h_freq = fundamental * h
        if h_freq > sr / 2:
            break
        h_idx = np.argmin(np.abs(freqs - h_freq))
        harm_power += spectrum[h_idx] ** 2
        harmonics[h] = 20 * np.log10(max(spectrum[h_idx], 1e-10)) - 20 * np.log10(max(spectrum[fund_idx], 1e-10))

    thd = np.sqrt(harm_power / max(fund_power, 1e-20)) * 100
    return thd, harmonics


def main():
    print("=" * 70)
    print("  Multi-Comp Opto vs UA LA-2A — Comparison Analysis")
    print("=" * 70)

    # Load captured outputs
    # Support alternate capture files via command line args or _2 suffix
    import sys
    suffix = sys.argv[1] if len(sys.argv) > 1 else "1"

    la2a_path = os.path.join(CAPTURED_DIR, f"LA2A_{suffix}.wav")
    mc_path = os.path.join(CAPTURED_DIR, f"Multi Comp_{suffix}.wav")

    if not os.path.exists(la2a_path):
        raise RuntimeError(f"LA-2A capture not found: {la2a_path}")
    if not os.path.exists(mc_path):
        raise RuntimeError(f"Multi-Comp capture not found: {mc_path}")

    la2a, sr = load_mono(la2a_path)
    mc, sr2 = load_mono(mc_path)
    if sr != sr2:
        raise ValueError(f"Sample rate mismatch: LA-2A={sr}, Multi-Comp={sr2}")
    print(f"\n  Sample rate: {sr} Hz")
    print(f"  LA-2A:      {len(la2a)/sr:.1f}s ({len(la2a)} samples)")
    print(f"  Multi-Comp: {len(mc)/sr:.1f}s ({len(mc)} samples)")

    # Load and concatenate dry signals
    test_names = [
        "01_step_response", "02_program_dependency", "03_release_curve",
        "04_attack_transients", "05_frequency_response", "06_thd",
        "07_pink_noise", "08_gain_curve"
    ]

    dry_parts = []
    dry_boundaries = []  # (start_sample, end_sample, name)
    offset = 0
    for name in test_names:
        path = os.path.join(DRY_DIR, f"{name}.wav")
        if not os.path.exists(path):
            print(f"  WARNING: Missing dry signal {path}")
            continue
        part, sr_dry = load_mono(path)
        if sr_dry != sr:
            raise ValueError(f"Sample rate mismatch for {name}: expected {sr}, got {sr_dry}")
        dry_boundaries.append((offset, offset + len(part), name))
        dry_parts.append(part)
        offset += len(part)

    if not dry_parts:
        print("  ERROR: No dry signal files found")
        sys.exit(1)
    dry = np.concatenate(dry_parts)
    print(f"  Dry total:  {len(dry)/sr:.1f}s ({len(dry)} samples)")

    # Trim to shortest
    min_len = min(len(dry), len(la2a), len(mc))
    dry = dry[:min_len]
    la2a = la2a[:min_len]
    mc = mc[:min_len]

    print(f"  Aligned to: {min_len/sr:.1f}s")

    # === Overall comparison ===
    print(f"\n{'='*70}")
    print(f"  OVERALL LEVELS")
    print(f"{'='*70}")
    print(f"  Dry RMS:        {rms_db(dry):>8.1f} dB")
    print(f"  LA-2A RMS:      {rms_db(la2a):>8.1f} dB")
    print(f"  Multi-Comp RMS: {rms_db(mc):>8.1f} dB")
    print(f"  LA-2A gain:     {rms_db(la2a)-rms_db(dry):>+8.1f} dB")
    print(f"  Multi-Comp gain:{rms_db(mc)-rms_db(dry):>+8.1f} dB")

    # === Per-test analysis ===
    results = {}
    for start, end, name in dry_boundaries:
        if end > min_len:
            end = min_len
        if start >= min_len:
            break
        d = dry[start:end]
        l = la2a[start:end]
        m = mc[start:end]
        results[name] = analyze_segment(d, l, m, sr, name)

    # === THD comparison (test 06) ===
    for start, end, name in dry_boundaries:
        if name == "06_thd" and end <= min_len:
            print(f"\n{'='*70}")
            print(f"  THD ANALYSIS (1kHz sine)")
            print(f"{'='*70}")

            # Low-level section: 2s silence + 10s at -20dB
            # Grab the -20dB section (skip 2s silence + 1s settling)
            low_start = start + int(3 * sr)
            low_end = start + int(12 * sr)

            if low_end <= min_len:
                la2a_thd_low, la2a_harm_low = thd_analysis(la2a[low_start:low_end], sr)
                mc_thd_low, mc_harm_low = thd_analysis(mc[low_start:low_end], sr)
                dry_thd_low, _ = thd_analysis(dry[low_start:low_end], sr)

                print(f"\n  Low-level (-20 dB, below threshold):")
                print(f"    Dry THD:        {dry_thd_low:.3f}%")
                print(f"    LA-2A THD:      {la2a_thd_low:.3f}%")
                print(f"    Multi-Comp THD: {mc_thd_low:.3f}%")
                print(f"\n    Harmonics relative to fundamental:")
                print(f"    {'Harmonic':<12} {'LA-2A':>10} {'Multi-Comp':>12}")
                for h in sorted(set(list(la2a_harm_low.keys()) + list(mc_harm_low.keys()))):
                    la = la2a_harm_low.get(h, -999)
                    mc_h = mc_harm_low.get(h, -999)
                    print(f"    {h}x ({h*1000}Hz) {la:>+10.1f} dB {mc_h:>+12.1f} dB")

            # Hot section: after 2s silence + 10s low + 2s gap = 14s offset
            hot_start = start + int(15 * sr)
            hot_end = start + int(24 * sr)

            if hot_end <= min_len:
                la2a_thd_hot, la2a_harm_hot = thd_analysis(la2a[hot_start:hot_end], sr)
                mc_thd_hot, mc_harm_hot = thd_analysis(mc[hot_start:hot_end], sr)
                dry_thd_hot, _ = thd_analysis(dry[hot_start:hot_end], sr)

                print(f"\n  High-level (-3 dB, compression engaged):")
                print(f"    Dry THD:        {dry_thd_hot:.3f}%")
                print(f"    LA-2A THD:      {la2a_thd_hot:.3f}%")
                print(f"    Multi-Comp THD: {mc_thd_hot:.3f}%")
                print(f"\n    Harmonics relative to fundamental:")
                print(f"    {'Harmonic':<12} {'LA-2A':>10} {'Multi-Comp':>12}")
                for h in sorted(set(list(la2a_harm_hot.keys()) + list(mc_harm_hot.keys()))):
                    la = la2a_harm_hot.get(h, -999)
                    mc_h = mc_harm_hot.get(h, -999)
                    print(f"    {h}x ({h*1000}Hz) {la:>+10.1f} dB {mc_h:>+12.1f} dB")

    # === Compression curve (test 08) ===
    for start, end, name in dry_boundaries:
        if name == "08_gain_curve" and start < min_len:
            print(f"\n{'='*70}")
            print(f"  COMPRESSION CURVE (Input→Output mapping)")
            print(f"{'='*70}")

            # 2s silence, then 1s tones at -40 to 0 dB in 2dB steps
            tone_start = start + int(2 * sr)
            levels = list(range(-40, 1, 2))

            # First pass: collect measurements
            measurements = []
            for i, level_db in enumerate(levels):
                seg_start = tone_start + i * sr  # 1s per tone
                seg_end = seg_start + sr
                if seg_end > min_len:
                    break

                # Measure middle 0.5s of each 1s tone (skip 0.25s settling)
                meas_start = seg_start + sr // 4
                meas_end = seg_start + 3 * sr // 4

                dry_level = rms_db(dry[meas_start:meas_end])
                la2a_level = rms_db(la2a[meas_start:meas_end])
                mc_level = rms_db(mc[meas_start:meas_end])
                measurements.append((dry_level, la2a_level, mc_level))

            # Find flat-region gain (average of lowest 5 levels where no compression)
            if len(measurements) >= 5:
                la2a_flat_gain = np.mean([m[1] - m[0] for m in measurements[:5]])
                mc_flat_gain_val = np.mean([m[2] - m[0] for m in measurements[:5]])
            else:
                print(f"  WARNING: Only {len(measurements)} measurements, flat-region gain estimation may be inaccurate")
                la2a_flat_gain = np.mean([m[1] - m[0] for m in measurements]) if measurements else 0
                mc_flat_gain_val = np.mean([m[2] - m[0] for m in measurements]) if measurements else 0

            print(f"\n  Flat-region gain: LA-2A={la2a_flat_gain:+.1f} dB, MC={mc_flat_gain_val:+.1f} dB (offset={mc_flat_gain_val-la2a_flat_gain:+.1f} dB)")

            print(f"\n  {'Input dB':>10} {'LA-2A GR':>10} {'MC GR':>10} {'Diff':>8} | {'LA-2A Out':>10} {'MC Out':>10}")
            print(f"  {'-'*68}")

            for dry_level, la2a_level, mc_level in measurements:
                # "Actual GR" = net gain minus flat-region gain (negative = compression)
                la2a_actual_gr = (la2a_level - dry_level) - la2a_flat_gain
                mc_actual_gr = (mc_level - dry_level) - mc_flat_gain_val
                diff = mc_actual_gr - la2a_actual_gr

                print(f"  {dry_level:>10.1f} {la2a_actual_gr:>+10.1f} {mc_actual_gr:>+10.1f} {diff:>+8.1f} | {la2a_level:>10.1f} {mc_level:>10.1f}")

    # === Summary ===
    print(f"\n{'='*70}")
    print(f"  SUMMARY")
    print(f"{'='*70}")

    gr_diffs = []
    for name, r in results.items():
        diff = abs(r['mc_gr_mean'] - r['la2a_gr_mean'])
        gr_diffs.append((name, diff, r['la2a_gr_mean'], r['mc_gr_mean']))

    print(f"\n  Average gain reduction comparison:")
    print(f"  {'Test':<30} {'LA-2A GR':>10} {'MC GR':>10} {'Diff':>8}")
    print(f"  {'-'*58}")
    for name, diff, la2a_gr, mc_gr in gr_diffs:
        marker = " ←" if diff > 3 else ""
        print(f"  {name:<30} {la2a_gr:>+10.1f} {mc_gr:>+10.1f} {diff:>8.1f}{marker}")

    avg_diff = np.mean([d for _, d, _, _ in gr_diffs]) if gr_diffs else 0.0
    print(f"\n  Average GR difference: {avg_diff:.1f} dB")
    print(f"  (Lower is better — 0 = perfect match)")

    # Key metrics
    print(f"\n  Key metrics:")
    for name, r in results.items():
        if name == "04_attack_transients":
            print(f"  - Transient most GR: LA-2A={r['la2a_most_gr']:+.1f} dB, MC={r['mc_most_gr']:+.1f} dB")
    print(f"  - THD (hot): LA-2A=see above, MC=see above")


if __name__ == "__main__":
    main()
