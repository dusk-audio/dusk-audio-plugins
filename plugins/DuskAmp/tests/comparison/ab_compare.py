#!/usr/bin/env python3
"""
ab_compare.py — A/B comparison of two amp sim renders.

Loads two WAV files (e.g. AmpliTube vs DuskAmp through the same DI), aligns
levels, and prints concrete metrics:

  • Long-term average spectrum (octave-band tilt)
  • Spectral centroid + 80/20 percentile bands over time
  • Transient envelope (attack peak, decay τ, sustain level)
  • Crest factor + RMS distribution
  • THD-ish ratio (harmonic content above 2 kHz vs fundamental band)
  • Difference RMS in matched-level dB

Optional: --plot writes PNGs to the same directory.

Usage:
  ab_compare.py <reference.wav> <test.wav> [--plot]
"""

import argparse
import sys
from pathlib import Path

import numpy as np
import scipy.signal as signal

# soundfile handles 24-bit WAV (which Logic bounces by default and scipy doesn't).
try:
    import soundfile as sf
    _HAVE_SF = True
except ImportError:
    _HAVE_SF = False
    import scipy.io.wavfile as wavfile


def load_mono(path: Path):
    if _HAVE_SF:
        x, sr = sf.read(str(path), always_2d=False, dtype='float32')
    else:
        sr, x = wavfile.read(str(path))
        if x.dtype.kind == "i":
            max_val = np.iinfo(x.dtype).max
            x = x.astype(np.float32) / float(max_val)
        else:
            x = x.astype(np.float32)
    if x.ndim == 2:
        x = x.mean(axis=1)
    return sr, x


def align_levels(ref, test):
    """Scale `test` so its broadband RMS matches `ref`. Returns scaled test
    and the dB gain that was applied."""
    ref_rms = np.sqrt(np.mean(ref ** 2)) + 1e-12
    test_rms = np.sqrt(np.mean(test ** 2)) + 1e-12
    gain = ref_rms / test_rms
    return test * gain, 20.0 * np.log10(gain)


def trim_to_min(*xs):
    n = min(len(x) for x in xs)
    return [x[:n] for x in xs]


# ============================================================================
# Spectrum
# ============================================================================

def average_spectrum(x, sr, fft_size=4096, hop=2048):
    """Welch-style PSD average — returns (freqs, magnitude_dB)."""
    f, pxx = signal.welch(x, fs=sr, nperseg=fft_size, noverlap=fft_size - hop,
                          scaling='spectrum', detrend=False)
    mag = np.sqrt(pxx + 1e-20)
    mag_db = 20.0 * np.log10(mag + 1e-12)
    return f, mag_db


def octave_band_levels(f, mag_db, centres):
    """For each centre frequency, return the average dB in a 1/3-octave band."""
    out = []
    for fc in centres:
        lo = fc / (2 ** (1/6))
        hi = fc * (2 ** (1/6))
        mask = (f >= lo) & (f <= hi)
        if not mask.any():
            out.append(float('nan'))
            continue
        # Energy-average across band
        band = 10 ** (mag_db[mask] / 10)
        out.append(10.0 * np.log10(band.mean() + 1e-20))
    return np.array(out)


# ============================================================================
# Transient envelope
# ============================================================================

def envelope(x, sr, release_ms=80.0):
    """Peak follower: instantaneous attack, exp release."""
    release_a = np.exp(-1.0 / (release_ms * 1e-3 * sr))
    env = np.zeros_like(x)
    e = 0.0
    for i, v in enumerate(np.abs(x)):
        if v > e:
            e = v
        else:
            e = release_a * e + (1.0 - release_a) * v
        env[i] = e
    return env


def find_transients(env, sr, threshold_db=-12.0, min_gap_ms=120.0):
    """Find peak indices in the envelope above a threshold, separated by min_gap."""
    peak = env.max() + 1e-12
    th = peak * 10 ** (threshold_db / 20)
    min_gap = int(sr * min_gap_ms / 1000.0)
    candidates = signal.find_peaks(env, height=th, distance=min_gap)[0]
    return candidates


def attack_decay(x, sr, peak_idx, win_pre_ms=10.0, win_post_ms=200.0):
    """Around a peak, measure attack time (10%→peak) and 60% decay τ."""
    pre  = int(sr * win_pre_ms  / 1000.0)
    post = int(sr * win_post_ms / 1000.0)
    s = max(0, peak_idx - pre)
    e = min(len(x), peak_idx + post)
    seg = np.abs(x[s:e])
    if len(seg) < 4:
        return float('nan'), float('nan')

    peak = seg.max()
    peak_local = int(np.argmax(seg))
    # Attack: time from 10% threshold to peak
    th = 0.1 * peak
    pre_seg = seg[:peak_local]
    above = np.where(pre_seg > th)[0]
    attack_ms = (peak_local - above[0]) * 1000.0 / sr if len(above) else float('nan')

    # Decay τ — find sample where amplitude drops to peak/e (≈37%)
    target = peak / np.e
    post_seg = seg[peak_local:]
    below = np.where(post_seg < target)[0]
    decay_ms = below[0] * 1000.0 / sr if len(below) else float('nan')

    return attack_ms, decay_ms


# ============================================================================
# Driver
# ============================================================================

def main():
    p = argparse.ArgumentParser(description="A/B compare two amp-sim renders.")
    p.add_argument("reference", help="Reference WAV (e.g. AmpliTube)")
    p.add_argument("test",      help="Test WAV (e.g. DuskAmp)")
    p.add_argument("--plot",    action="store_true", help="Write PNG plots next to inputs")
    p.add_argument("--align",   action="store_true", help="RMS-align before comparing (default: on)")
    p.add_argument("--no-align", dest="align", action="store_false")
    p.set_defaults(align=True)
    args = p.parse_args()

    ref_path  = Path(args.reference)
    test_path = Path(args.test)
    print(f"Reference: {ref_path}")
    print(f"Test:      {test_path}")
    print()

    ref_sr,  ref  = load_mono(ref_path)
    test_sr, test = load_mono(test_path)

    if ref_sr != test_sr:
        print(f"ERROR: sample rates differ ({ref_sr} vs {test_sr})")
        sys.exit(2)
    sr = ref_sr

    ref, test = trim_to_min(ref, test)
    print(f"Length: {len(ref)/sr:.2f} s @ {sr} Hz")

    # ----- RMS / level
    ref_rms  = np.sqrt(np.mean(ref ** 2))
    test_rms_raw = np.sqrt(np.mean(test ** 2))
    print(f"RMS: ref={20*np.log10(ref_rms+1e-12):.1f} dB  "
          f"test={20*np.log10(test_rms_raw+1e-12):.1f} dB  "
          f"diff={20*np.log10((test_rms_raw+1e-12)/(ref_rms+1e-12)):+.1f} dB")

    if args.align:
        test, gain_db = align_levels(ref, test)
        print(f"Level-aligned test by {gain_db:+.1f} dB before spectral analysis.")
    print()

    # ----- Spectrum (octave bands)
    f, ref_db  = average_spectrum(ref, sr)
    _, test_db = average_spectrum(test, sr)

    # 1/3-octave centres covering the guitar band
    centres = np.array([60, 80, 100, 125, 160, 200, 250, 315,
                        400, 500, 630, 800, 1000, 1250, 1600, 2000,
                        2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500])
    ref_bands  = octave_band_levels(f, ref_db,  centres)
    test_bands = octave_band_levels(f, test_db, centres)
    diff_bands = test_bands - ref_bands

    print("Spectral balance — TEST minus REFERENCE (negative = TEST is darker):")
    print("  freq |  ref(dB) | test(dB) |   diff")
    print("  -----+----------+----------+-------")
    for fc, r, t, d in zip(centres, ref_bands, test_bands, diff_bands):
        bar = ""
        if not np.isnan(d):
            n = int(round(np.clip(d, -10, 10)))
            if n > 0:   bar = " " * 10 + "+" * n
            elif n < 0: bar = " " * (10 + n) + "-" * (-n)
            else:       bar = " " * 10 + "·"
        print(f"  {fc:>5d} | {r:>+7.1f}  | {t:>+7.1f}  | {d:>+5.1f}  {bar}")

    # Spectral tilt summary
    bass_diff = np.nanmean(diff_bands[(centres >= 80)  & (centres <= 250)])
    mid_diff  = np.nanmean(diff_bands[(centres >= 400) & (centres <= 2000)])
    high_diff = np.nanmean(diff_bands[(centres >= 3150) & (centres <= 10000)])
    print()
    print("Spectral tilt summary (test − ref, average dB by region):")
    print(f"  bass  (80-250 Hz):    {bass_diff:+.1f} dB")
    print(f"  mids  (400-2k Hz):    {mid_diff:+.1f} dB")
    print(f"  highs (3.15-10k Hz):  {high_diff:+.1f} dB")
    print()

    # ----- Transient response
    print("Transient envelope:")
    ref_env  = envelope(ref,  sr)
    test_env = envelope(test, sr)
    ref_peaks = find_transients(ref_env, sr, threshold_db=-9.0, min_gap_ms=200.0)
    test_peaks = find_transients(test_env, sr, threshold_db=-9.0, min_gap_ms=200.0)
    print(f"  ref peaks:  {len(ref_peaks)}    test peaks: {len(test_peaks)}")

    if len(ref_peaks) > 0 and len(test_peaks) > 0:
        # Pair peaks by nearest-time match
        n_pairs = min(len(ref_peaks), len(test_peaks), 12)
        ref_attacks, ref_decays  = [], []
        test_attacks, test_decays = [], []
        for i in range(n_pairs):
            a, d = attack_decay(ref,  sr, ref_peaks[i])
            ref_attacks.append(a); ref_decays.append(d)
            a, d = attack_decay(test, sr, test_peaks[i] if i < len(test_peaks) else ref_peaks[i])
            test_attacks.append(a); test_decays.append(d)
        print(f"  attack ms (median):  ref={np.nanmedian(ref_attacks):.1f}   "
              f"test={np.nanmedian(test_attacks):.1f}")
        print(f"  decay  ms (median):  ref={np.nanmedian(ref_decays):.1f}   "
              f"test={np.nanmedian(test_decays):.1f}")
    print()

    # ----- Crest factor / dynamics
    ref_crest  = 20*np.log10(np.max(np.abs(ref))  / (ref_rms + 1e-12))
    test_crest = 20*np.log10(np.max(np.abs(test)) / (np.sqrt(np.mean(test**2))  + 1e-12))
    print(f"Crest factor: ref={ref_crest:.1f} dB   test={test_crest:.1f} dB   "
          f"(higher = more dynamic / less compressed)")
    print()

    # ----- Distortion-like metric: HF energy ratio
    # Energy above 2 kHz vs energy in fundamental band (80-1000 Hz).
    def hf_ratio(mag_db, freqs):
        fund = (freqs >= 80) & (freqs <= 1000)
        hf   = (freqs >= 2000) & (freqs <= 10000)
        # Convert dB → linear energy
        fund_e = 10 ** (mag_db[fund] / 10)
        hf_e   = 10 ** (mag_db[hf] / 10)
        return 10 * np.log10((hf_e.mean() + 1e-20) / (fund_e.mean() + 1e-20))
    print(f"HF/fundamental energy ratio:  ref={hf_ratio(ref_db, f):+.1f} dB   "
          f"test={hf_ratio(test_db, f):+.1f} dB   "
          f"(less negative = brighter / more harmonic energy)")
    print()

    # ----- Sample diff (after RMS alignment)
    diff = test - ref
    diff_rms = 20 * np.log10(np.sqrt(np.mean(diff**2)) / (ref_rms + 1e-12) + 1e-12)
    print(f"Post-alignment difference RMS: {diff_rms:+.1f} dB below reference RMS")
    print(f"  (-30 dB = very close,  0 dB = unrelated)")

    if args.plot:
        try:
            import matplotlib
            matplotlib.use("Agg")
            import matplotlib.pyplot as plt

            fig, axes = plt.subplots(2, 1, figsize=(11, 7))
            axes[0].semilogx(f, ref_db,  label='ref',  color='tab:blue', alpha=0.8)
            axes[0].semilogx(f, test_db, label='test', color='tab:orange', alpha=0.8)
            axes[0].set_xlim(40, 16000); axes[0].set_xlabel("Hz")
            axes[0].set_ylabel("dB"); axes[0].set_title("Avg spectrum")
            axes[0].grid(True, alpha=0.3); axes[0].legend()

            axes[1].bar(np.arange(len(centres)), diff_bands, color=[
                'tab:red' if d < 0 else 'tab:green' for d in diff_bands
            ])
            axes[1].set_xticks(np.arange(len(centres)))
            axes[1].set_xticklabels([str(c) for c in centres], rotation=60)
            axes[1].axhline(0, color='black', lw=0.5)
            axes[1].set_ylabel("dB (test − ref)")
            axes[1].set_title("Per-band difference")
            axes[1].grid(True, alpha=0.3)
            plt.tight_layout()
            out = ref_path.with_suffix("").name + "_vs_" + test_path.with_suffix("").name + ".png"
            out_path = ref_path.parent / out
            plt.savefig(str(out_path), dpi=120)
            print(f"\nWrote plot: {out_path}")
        except ImportError:
            print("\n(matplotlib not installed; skipping plot)")


if __name__ == "__main__":
    main()
