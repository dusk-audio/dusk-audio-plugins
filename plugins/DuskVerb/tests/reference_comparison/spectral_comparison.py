#!/usr/bin/env python3
"""
Spectral comparison: DuskVerb vs VintageVerb (Hall mode).

Captures IRs and pink noise responses from both plugins, computes
magnitude spectra, and plots the difference to identify frequency bands
where DV and VV diverge.

Outputs:
  spectral_comparison.png  — 4-panel plot (IR spectrum, pink noise spectrum,
                              difference spectra, MFCC frame-by-frame distance)
"""

import sys
import os
import numpy as np

# Reuse fingerprint infrastructure
sys.path.insert(0, os.path.dirname(__file__))
from config import SAMPLE_RATE, find_plugin, DUSKVERB_PATHS, REFERENCE_REVERB_PATHS, MODE_PAIRINGS
from reverb_fingerprint import (
    load_plugins, flush_plugin, capture_ir, process_stereo, setup_dark_theme,
    FLUSH_DURATION, IR_DURATION,
)

DV_COLOR = '#00d4ff'
VV_COLOR = '#ff8c42'
DIFF_COLOR = '#ff4466'
SR = SAMPLE_RATE


def smoothed_spectrum(signal, sr, smoothing_octave=1/6):
    """Compute magnitude spectrum with fractional-octave smoothing."""
    n = len(signal)
    fft = np.fft.rfft(signal)
    mag = np.abs(fft)
    freqs = np.fft.rfftfreq(n, 1.0 / sr)

    # Convert to dB
    mag_db = 20 * np.log10(mag + 1e-30)

    # Fractional-octave smoothing in log-frequency domain
    log_freqs = np.log2(freqs[1:] + 1e-30)  # skip DC
    mag_db_no_dc = mag_db[1:]

    half_width = smoothing_octave / 2

    # O(N) smoothing using binary search for window bounds
    smoothed = np.zeros_like(mag_db_no_dc)
    for i in range(len(log_freqs)):
        lo = np.searchsorted(log_freqs, log_freqs[i] - half_width)
        hi = np.searchsorted(log_freqs, log_freqs[i] + half_width, side='right')
        smoothed[i] = np.mean(mag_db_no_dc[lo:hi])

    return freqs[1:], smoothed


def compute_mfcc_per_frame(signal, sr, n_mfcc=13, frame_ms=50, hop_ms=25):
    """Compute MFCCs per frame using simple mel filterbank."""
    frame_len = int(sr * frame_ms / 1000)
    hop_len = int(sr * hop_ms / 1000)
    n_frames = max(1, (len(signal) - frame_len) // hop_len + 1)

    # Mel filterbank (40 filters, 20-16000 Hz)
    n_filt = 40
    low_mel = 2595 * np.log10(1 + 20 / 700)
    high_mel = 2595 * np.log10(1 + min(16000, sr / 2) / 700)
    mel_points = np.linspace(low_mel, high_mel, n_filt + 2)
    hz_points = 700 * (10 ** (mel_points / 2595) - 1)
    fft_bins = np.floor((frame_len + 1) * hz_points / sr).astype(int)

    filterbank = np.zeros((n_filt, frame_len // 2 + 1))
    for j in range(n_filt):
        for k in range(fft_bins[j], fft_bins[j + 1]):
            if k < filterbank.shape[1]:
                filterbank[j, k] = (k - fft_bins[j]) / max(1, fft_bins[j + 1] - fft_bins[j])
        for k in range(fft_bins[j + 1], fft_bins[j + 2]):
            if k < filterbank.shape[1]:
                filterbank[j, k] = (fft_bins[j + 2] - k) / max(1, fft_bins[j + 2] - fft_bins[j + 1])

    from scipy.fft import dct
    window = np.hanning(frame_len)
    mfccs = np.zeros((n_frames, n_mfcc))

    for i in range(n_frames):
        start = i * hop_len
        chunk = signal[start:start + frame_len]
        if len(chunk) < frame_len:
            chunk = np.pad(chunk, (0, frame_len - len(chunk)))
        frame = chunk * window
        power = np.abs(np.fft.rfft(frame)) ** 2
        mel_energy = np.dot(filterbank, power)
        mel_log = np.log(mel_energy + 1e-30)
        mfccs[i] = dct(mel_log, type=2, norm='ortho')[:n_mfcc]

    return mfccs


def per_band_energy(signal, sr, bands=None):
    """Compute energy in octave bands."""
    if bands is None:
        bands = [63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]

    n = len(signal)
    fft = np.fft.rfft(signal)
    power = np.abs(fft) ** 2
    freqs = np.fft.rfftfreq(n, 1.0 / sr)

    energies = {}
    for i, center in enumerate(bands):
        lo = center / np.sqrt(2)
        hi = center * np.sqrt(2)
        mask = (freqs >= lo) & (freqs < hi)
        if np.any(mask):
            energies[center] = 10 * np.log10(np.sum(power[mask]) + 1e-30)
        else:
            energies[center] = -120
    return energies


def main():
    plt = setup_dark_theme()

    print("Loading plugins...")
    dv_plugin, vv_plugin, pairing = load_plugins("Hall", compare=True)
    if vv_plugin is None:
        print("ERROR: VintageVerb not found")
        sys.exit(1)

    sr = SR
    out_dir = os.path.dirname(__file__)

    # ── Capture IRs ──
    print("Capturing impulse responses...")
    flush_plugin(dv_plugin, sr)
    dv_ir_l, dv_ir_r = capture_ir(dv_plugin, sr, duration=3.0)
    flush_plugin(vv_plugin, sr)
    vv_ir_l, vv_ir_r = capture_ir(vv_plugin, sr, duration=3.0)

    # Use left channel mono for spectral analysis
    dv_ir = dv_ir_l
    vv_ir = vv_ir_l

    # ── Capture pink noise responses ──
    print("Capturing pink noise responses...")
    duration_sec = 5.0
    n = int(sr * duration_sec)
    rng = np.random.default_rng(42)
    # Generate pink noise via 1/f filtering of white noise
    white = rng.standard_normal(n).astype(np.float32)
    fft_white = np.fft.rfft(white)
    freqs_pink = np.fft.rfftfreq(n, 1.0 / sr)
    freqs_pink[0] = 1.0  # avoid div by zero
    pink_filter = 1.0 / np.sqrt(freqs_pink)
    pink = np.fft.irfft(fft_white * pink_filter, n).astype(np.float32)
    pink *= 10 ** (-12.0 / 20.0) / (np.sqrt(np.mean(pink ** 2)) + 1e-30)  # normalize to -12 dBFS

    flush_plugin(dv_plugin, sr)
    dv_pink_l, _ = process_stereo(dv_plugin, pink, sr)
    flush_plugin(vv_plugin, sr)
    vv_pink_l, _ = process_stereo(vv_plugin, pink, sr)

    # ── Capture vocal response ──
    print("Capturing vocal response...")
    # Generate a simple vocal-like signal (formant synthesis)
    t = np.arange(n) / sr
    vocal = np.zeros(n, dtype=np.float32)
    f0 = 150  # fundamental
    for h in range(1, 20):
        amp = 1.0 / h
        vocal += (amp * np.sin(2 * np.pi * f0 * h * t)).astype(np.float32)
    vocal *= 10 ** (-12.0 / 20.0) / (np.sqrt(np.mean(vocal ** 2)) + 1e-30)

    flush_plugin(dv_plugin, sr)
    dv_vocal_l, _ = process_stereo(dv_plugin, vocal, sr)
    flush_plugin(vv_plugin, sr)
    vv_vocal_l, _ = process_stereo(vv_plugin, vocal, sr)

    # ═══════════════════════════════════════════════════════════════════
    # Plot 1: IR magnitude spectrum (1/6 octave smoothed)
    # ═══════════════════════════════════════════════════════════════════
    print("Computing spectra...")
    fig, axes = plt.subplots(2, 2, figsize=(16, 12))

    # Panel 1: IR spectrum
    ax = axes[0, 0]
    freqs_dv, spec_dv = smoothed_spectrum(dv_ir, sr)
    freqs_vv, spec_vv = smoothed_spectrum(vv_ir, sr)
    # Normalize to peak
    spec_dv -= np.max(spec_dv)
    spec_vv -= np.max(spec_vv)
    mask = freqs_dv >= 20
    ax.semilogx(freqs_dv[mask], spec_dv[mask], color=DV_COLOR, linewidth=1.2, label='DuskVerb')
    ax.semilogx(freqs_vv[mask], spec_vv[mask], color=VV_COLOR, linewidth=1.2, label='VintageVerb')
    ax.set_xlim(20, 20000)
    ax.set_ylim(-60, 5)
    ax.set_xlabel('Frequency (Hz)')
    ax.set_ylabel('Magnitude (dB, normalized)')
    ax.set_title('IR Magnitude Spectrum (1/6 oct smoothed)')
    ax.legend()
    ax.grid(True, alpha=0.3)

    # Panel 2: Pink noise response spectrum
    ax = axes[0, 1]
    freqs_dv_p, spec_dv_p = smoothed_spectrum(dv_pink_l, sr)
    freqs_vv_p, spec_vv_p = smoothed_spectrum(vv_pink_l, sr)
    spec_dv_p -= np.max(spec_dv_p)
    spec_vv_p -= np.max(spec_vv_p)
    mask = freqs_dv_p >= 20
    ax.semilogx(freqs_dv_p[mask], spec_dv_p[mask], color=DV_COLOR, linewidth=1.2, label='DuskVerb')
    ax.semilogx(freqs_vv_p[mask], spec_vv_p[mask], color=VV_COLOR, linewidth=1.2, label='VintageVerb')
    ax.set_xlim(20, 20000)
    ax.set_ylim(-60, 5)
    ax.set_xlabel('Frequency (Hz)')
    ax.set_ylabel('Magnitude (dB, normalized)')
    ax.set_title('Pink Noise Response Spectrum (1/6 oct smoothed)')
    ax.legend()
    ax.grid(True, alpha=0.3)

    # Panel 3: Difference spectra (DV - VV) for IR, pink, vocal
    ax = axes[1, 0]
    # IR difference
    diff_ir = spec_dv - spec_vv
    mask_ir = freqs_dv >= 20
    ax.semilogx(freqs_dv[mask_ir], diff_ir[mask_ir], color=DV_COLOR, linewidth=1.2, alpha=0.8, label='IR')
    # Pink noise difference
    diff_pink = spec_dv_p - spec_vv_p
    mask_p = freqs_dv_p >= 20
    ax.semilogx(freqs_dv_p[mask_p], diff_pink[mask_p], color=VV_COLOR, linewidth=1.2, alpha=0.8, label='Pink Noise')
    # Vocal difference
    freqs_dv_v, spec_dv_v = smoothed_spectrum(dv_vocal_l, sr)
    freqs_vv_v, spec_vv_v = smoothed_spectrum(vv_vocal_l, sr)
    spec_dv_v -= np.max(spec_dv_v)
    spec_vv_v -= np.max(spec_vv_v)
    diff_vocal = spec_dv_v - spec_vv_v
    mask_v = freqs_dv_v >= 20
    ax.semilogx(freqs_dv_v[mask_v], diff_vocal[mask_v], color='#88ff88', linewidth=1.2, alpha=0.8, label='Vocal')
    ax.axhline(0, color='white', linewidth=0.5, alpha=0.5)
    ax.set_xlim(20, 20000)
    ax.set_ylim(-20, 20)
    ax.set_xlabel('Frequency (Hz)')
    ax.set_ylabel('DV − VV (dB)')
    ax.set_title('Spectral Difference (DV − VV, normalized)')
    ax.legend()
    ax.grid(True, alpha=0.3)

    # Annotate key bands on IR curve
    for freq, label in [(100, 'LF'), (500, 'Mid'), (2000, '2k'), (8000, 'HF')]:
        idx = np.argmin(np.abs(freqs_dv[mask_ir] - freq))
        val = diff_ir[mask_ir][idx]
        ax.annotate(f'{label}\n{val:+.1f}dB', xy=(freq, val),
                    fontsize=8, color='white', ha='center',
                    bbox=dict(boxstyle='round,pad=0.2', facecolor='#333355', alpha=0.8))

    # Panel 4: Per-octave energy comparison (bar chart)
    ax = axes[1, 1]
    bands = [63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]
    dv_energy = per_band_energy(dv_ir, sr, bands)
    vv_energy = per_band_energy(vv_ir, sr, bands)

    x = np.arange(len(bands))
    width = 0.35
    dv_vals = [dv_energy[b] for b in bands]
    vv_vals = [vv_energy[b] for b in bands]
    # Normalize to max
    max_val = max(max(dv_vals), max(vv_vals))
    dv_vals = [v - max_val for v in dv_vals]
    vv_vals = [v - max_val for v in vv_vals]
    diff_vals = [d - v for d, v in zip(dv_vals, vv_vals)]

    bars_dv = ax.bar(x - width / 2, dv_vals, width, color=DV_COLOR, alpha=0.8, label='DuskVerb')
    bars_vv = ax.bar(x + width / 2, vv_vals, width, color=VV_COLOR, alpha=0.8, label='VintageVerb')

    # Add difference annotations
    for i, (d, v, diff) in enumerate(zip(dv_vals, vv_vals, diff_vals)):
        y_pos = max(d, v) + 1
        ax.text(i, y_pos, f'{diff:+.1f}', ha='center', va='bottom',
                fontsize=7, color='white', fontweight='bold')

    ax.set_xticks(x)
    ax.set_xticklabels([f'{b}' for b in bands], fontsize=8)
    ax.set_xlabel('Octave Band Center (Hz)')
    ax.set_ylabel('Energy (dB, normalized)')
    ax.set_title('IR Octave Band Energy')
    ax.legend()
    ax.grid(True, alpha=0.3, axis='y')

    fig.suptitle('DuskVerb vs VintageVerb — Spectral Comparison (Hall)', fontsize=16, y=0.98)
    fig.tight_layout(rect=[0, 0, 1, 0.96])

    out_path = os.path.join(out_dir, 'spectral_comparison.png')
    fig.savefig(out_path)
    plt.close(fig)
    print(f"Saved {out_path}")

    # ═══════════════════════════════════════════════════════════════════
    # Additional analysis: MFCC distance breakdown
    # ═══════════════════════════════════════════════════════════════════
    print("\n" + "=" * 70)
    print("  SPECTRAL ANALYSIS REPORT")
    print("=" * 70)

    # Per-octave energy difference table
    print("\n  Per-Octave IR Energy Difference (DV − VV):")
    print(f"  {'Band':>8s}  {'DV (dB)':>8s}  {'VV (dB)':>8s}  {'Diff':>8s}")
    for b in bands:
        d = dv_energy[b]
        v = vv_energy[b]
        diff = d - v
        marker = " ✗" if abs(diff) > 5 else " ⚠" if abs(diff) > 2 else " ✓"
        print(f"  {b:>7d}Hz  {d:>8.1f}  {v:>8.1f}  {diff:>+7.1f}{marker}")

    # Spectral tilt comparison
    print(f"\n  Spectral Tilt (250Hz→4kHz):")
    # Compute tilt from IR spectrum
    mask_250 = np.argmin(np.abs(freqs_dv - 250))
    mask_4k = np.argmin(np.abs(freqs_dv - 4000))
    dv_tilt = spec_dv[mask_4k] - spec_dv[mask_250]
    vv_tilt = spec_vv[mask_4k] - spec_vv[mask_250]
    print(f"    DuskVerb:     {dv_tilt:+.1f} dB")
    print(f"    VintageVerb:  {vv_tilt:+.1f} dB")
    print(f"    Delta:        {dv_tilt - vv_tilt:+.1f} dB")
    print(f"    → VV is {'brighter' if vv_tilt > dv_tilt else 'darker'} than DV by {abs(vv_tilt - dv_tilt):.1f} dB")

    # MFCC per-frame analysis for pink noise
    print(f"\n  MFCC Frame-by-Frame Analysis (pink noise, 50ms frames):")
    dv_mfcc = compute_mfcc_per_frame(dv_pink_l, sr)
    vv_mfcc = compute_mfcc_per_frame(vv_pink_l, sr)
    n_common = min(len(dv_mfcc), len(vv_mfcc))
    dv_mfcc = dv_mfcc[:n_common]
    vv_mfcc = vv_mfcc[:n_common]
    per_coeff_dist = np.mean(np.abs(dv_mfcc - vv_mfcc), axis=0)
    print(f"    {'Coeff':>6s}  {'Mean |Δ|':>8s}  {'Interpretation':>30s}")
    interp = [
        "overall level/energy",
        "spectral tilt (bright/dark)",
        "spectral shape detail",
        "formant structure",
    ]
    for i, d in enumerate(per_coeff_dist):
        desc = interp[i] if i < len(interp) else "fine spectral detail"
        print(f"    MFCC{i:>2d}  {d:>8.2f}  {desc:>30s}")

    # Per-band difference summary
    print(f"\n  Key Frequency Band Gaps (IR difference):")
    key_freqs = [50, 100, 200, 500, 1000, 2000, 4000, 8000, 12000]
    for freq in key_freqs:
        idx = np.argmin(np.abs(freqs_dv - freq))
        diff = spec_dv[idx] - spec_vv[idx]
        if abs(diff) > 3:
            severity = "LARGE"
        elif abs(diff) > 1:
            severity = "moderate"
        else:
            severity = "small"
        print(f"    {freq:>6d} Hz: DV−VV = {diff:+.1f} dB  ({severity})")

    print("\n" + "=" * 70)


if __name__ == "__main__":
    main()
