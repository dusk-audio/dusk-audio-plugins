#!/usr/bin/env python3
"""
Reverb Fingerprint — comprehensive visual analysis of DuskVerb impulse responses.

Generates a multi-page visual report covering:
  Phase 1:  Impulse Response Analysis (RT60, EDT, T20, T30, EDC, waveform, spectrogram)
  Phase 2:  ISO 3382 Acoustic Parameters (C50, C80, D50, Ts, BR, G)
  Phase 3:  Temporal Energy Distribution (fine time-window energy breakdown)
  Phase 4:  Stereo topology, modulation variability, delay structure, late-field stats
  Phase 5:  Real audio comparison (synthetic + custom signals)
  Phase 6:  Phase analysis, cepstrum (8x upsampled), linearity, transfer function, stereo width
  Phase 7:  Spectral centroid, IACC, decay shape, echo density per band
  Phase 8a: Modulation LFO extraction (Hilbert transform on 10kHz carrier)
  Phase 8b: All-pass filter smearing & dispersion (frequency-dependent group delay)
  Phase 8c: Damping topology test (loop vs post-EQ via short/long decay comparison)
  Phase 9:  Parameter sweeping ("Knob Math") — maps reference plugin knob scaling
  Phase 10: THD & saturation profiling (ESS harmonic separation at multiple drive levels)
  Phase 11: Sparse ER extraction (Orthogonal Matching Pursuit for early reflection taps)

Usage:
    python3 reverb_fingerprint.py --mode Hall --save
    python3 reverb_fingerprint.py --mode Hall --compare --save
    python3 reverb_fingerprint.py --mode Hall --compare --save --ess
    python3 reverb_fingerprint.py --mode Room --compare --save --sweep-params
    python3 reverb_fingerprint.py --mode Plate --compare --save --ess --sweep-params
    python3 reverb_fingerprint.py --mode Hall --compare --thd

Flags:
    --mode          Algorithm mode (Room/Hall/Plate/Chamber/Ambient)
    --compare       Render both DuskVerb and VintageVerb (default: DuskVerb only)
    --save          Save plots to disk (default: just print metrics)
    --ess           Use Exponential Swept Sine for IR capture (higher SNR for late tail)
    --sweep-params  Run Phase 9 parameter sweeping on reference plugin (slow)
    --thd           Run Phase 10 THD/saturation profiling (multiple ESS sweeps, slow)
"""

import argparse
import os
import sys
import time
import json

import numpy as np
from pedalboard import load_plugin

from config import (
    SAMPLE_RATE,
    DUSKVERB_PATHS, REFERENCE_REVERB_PATHS,
    MODE_PAIRINGS,
    find_plugin,
    apply_duskverb_params, apply_reference_params,
    discover_params,
)
from generate_test_signals import make_impulse
import reverb_metrics as metrics
from scipy.stats import kurtosis as scipy_kurtosis, skew as scipy_skew
from scipy.signal import butter, sosfilt, sosfiltfilt, hilbert, resample


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------
DV_COLOR = '#4a9eff'
VV_COLOR = '#ff8c42'

IR_DURATION = 5.0       # seconds of IR capture at 48 kHz
FLUSH_DURATION = 3.0    # seconds of silence to flush plugin state

TEMPORAL_WINDOWS_MS = [
    (0, 5), (5, 10), (10, 20), (20, 30), (30, 50),
    (50, 80), (80, 120), (120, 200), (200, 500),
    (500, 1000), (1000, 2000), (2000, 3000),
]


# ---------------------------------------------------------------------------
# Dark theme plot setup
# ---------------------------------------------------------------------------
def setup_dark_theme():
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    plt.rcParams.update({
        'figure.facecolor': '#1a1a2e',
        'axes.facecolor': '#1a1a2e',
        'text.color': 'white',
        'axes.labelcolor': 'white',
        'xtick.color': 'white',
        'ytick.color': 'white',
        'axes.edgecolor': '#444466',
        'grid.color': '#333355',
        'grid.alpha': 0.5,
        'legend.facecolor': '#2a2a4e',
        'legend.edgecolor': '#444466',
        'figure.dpi': 150,
        'savefig.dpi': 150,
    })
    return plt


# ---------------------------------------------------------------------------
# Plugin loading and rendering helpers
# ---------------------------------------------------------------------------
def process_stereo(plugin, mono_signal, sr):
    """Process mono signal through plugin as dual-mono, return (left, right)."""
    stereo_in = np.stack([mono_signal, mono_signal], axis=0).astype(np.float32)
    output = plugin(stereo_in, sr)
    return output[0], output[1]


def process_stereo_input(plugin, stereo_in, sr):
    """Process an explicit stereo input (2, N) through plugin, return (left, right)."""
    output = plugin(stereo_in.astype(np.float32), sr)
    return output[0], output[1]


def flush_plugin(plugin, sr, duration_sec=FLUSH_DURATION):
    """Process silence through plugin to flush internal state."""
    silence = np.zeros(int(sr * duration_sec), dtype=np.float32)
    process_stereo(plugin, silence, sr)


def capture_ir(plugin, sr, duration=IR_DURATION):
    """Capture impulse response: flush, send impulse, return (ir_left, ir_right)."""
    flush_plugin(plugin, sr, FLUSH_DURATION)
    n_samples = int(sr * duration)
    impulse = np.zeros(n_samples, dtype=np.float32)
    impulse[0] = 1.0
    stereo_in = np.stack([impulse, impulse], axis=0).astype(np.float32)
    output = plugin(stereo_in, sr)
    return output[0], output[1]


def capture_ir_ess(plugin, sr, sweep_duration=5.0, ir_duration=IR_DURATION,
                   amplitude_dbfs=-6.0, return_full=False):
    """Capture impulse response using Exponential Swept Sine (ESS) deconvolution.

    Generates a log sine sweep, processes it through the plugin, then
    deconvolves in the frequency domain to extract the linear IR.
    Advantages over Dirac: much higher SNR in the late tail, and harmonic
    distortion products are pushed to negative time (before t=0).

    Args:
        plugin: pedalboard plugin instance
        sr: sample rate
        sweep_duration: length of the sine sweep in seconds
        ir_duration: desired IR length in seconds (tail capture window)
        amplitude_dbfs: peak level of the sweep in dBFS (default -6)
        return_full: if True, also return raw deconvolved buffers and sweep info

    Returns:
        (ir_left, ir_right) — deconvolved impulse responses (trimmed, normalized)
        If return_full=True:
        (ir_left, ir_right, full_left, full_right, sweep_info) where full_*
        are raw pre-trim/pre-normalize buffers and sweep_info is a dict with
        N_sweep, N_fft, f1, f2, duration keys.
    """
    flush_plugin(plugin, sr, FLUSH_DURATION)

    N_sweep = int(sweep_duration * sr)
    N_tail = int(ir_duration * sr)
    N_total = N_sweep + N_tail  # sweep + silence for tail ringout

    # Generate logarithmic sine sweep (Farina method)
    f1 = 20.0       # start frequency
    f2 = sr / 2.2   # end frequency (below Nyquist with margin)
    t = np.arange(N_sweep, dtype=np.float64) / sr
    R = np.log(f2 / f1)
    sweep = np.sin(2.0 * np.pi * f1 * sweep_duration / R
                   * (np.exp(t * R / sweep_duration) - 1.0)).astype(np.float32)

    # Apply 10ms fade-in/out to avoid clicks
    fade_samples = int(0.010 * sr)
    fade_in = np.linspace(0, 1, fade_samples, dtype=np.float32)
    fade_out = np.linspace(1, 0, fade_samples, dtype=np.float32)
    sweep[:fade_samples] *= fade_in
    sweep[-fade_samples:] *= fade_out

    # Scale to requested amplitude
    amp_linear = 10.0 ** (amplitude_dbfs / 20.0)
    sweep *= amp_linear / (np.max(np.abs(sweep)) + 1e-30)

    # Pad with silence for tail capture
    padded_sweep = np.zeros(N_total, dtype=np.float32)
    padded_sweep[:N_sweep] = sweep

    # Process through plugin (stereo)
    stereo_in = np.stack([padded_sweep, padded_sweep], axis=0).astype(np.float32)
    output = plugin(stereo_in, sr)
    recorded_l = output[0].astype(np.float64)
    recorded_r = output[1].astype(np.float64)

    # Build inverse filter for deconvolution
    # The inverse filter for a log sweep has 6 dB/oct amplitude rise
    sweep_64 = padded_sweep.astype(np.float64)
    N_fft = len(sweep_64)
    S = np.fft.rfft(sweep_64, n=N_fft)

    # Regularized inverse: avoid division by near-zero bins
    S_mag = np.abs(S)
    reg = np.max(S_mag) * 1e-6  # regularization floor
    S_inv = np.conj(S) / (S_mag ** 2 + reg ** 2)

    # Apply the 6 dB/oct amplitude compensation (log sweep energy envelope)
    freqs = np.fft.rfftfreq(N_fft, 1.0 / sr)
    freqs[0] = f1  # avoid log(0)
    envelope = f1 / np.maximum(freqs, f1)  # 1/f energy profile of log sweep
    S_inv *= np.sqrt(envelope)

    # Deconvolve both channels
    R_l = np.fft.rfft(recorded_l, n=N_fft)
    R_r = np.fft.rfft(recorded_r, n=N_fft)
    full_l = np.fft.irfft(R_l * S_inv, n=N_fft).astype(np.float32)
    full_r = np.fft.irfft(R_r * S_inv, n=N_fft).astype(np.float32)

    # The linear IR starts at t=0 (sweep start). Harmonic distortion
    # products appear at negative time (wrapped to end of array).
    # Trim to desired IR length.
    ir_samples = int(ir_duration * sr)
    ir_l = full_l[:ir_samples].copy()
    ir_r = full_r[:ir_samples].copy()

    # Normalize so peak matches Dirac IR convention
    peak = max(np.max(np.abs(ir_l)), np.max(np.abs(ir_r)), 1e-30)
    ir_l /= peak
    ir_r /= peak

    if return_full:
        sweep_info = {
            "N_sweep": N_sweep, "N_fft": N_fft,
            "f1": f1, "f2": f2, "duration": sweep_duration,
        }
        return ir_l, ir_r, full_l, full_r, sweep_info

    return ir_l, ir_r


def sparse_deconvolution(ir_l, ir_r, sr, max_taps=20, window_ms=80.0):
    """Extract early reflection taps via Orthogonal Matching Pursuit (OMP).

    Uses a Dirac dictionary (identity matrix) — the correlation step reduces
    to argmax(|residual|).  Parabolic interpolation gives sub-sample delay.

    Args:
        ir_l, ir_r: Left and right IR channels (numpy float arrays)
        sr: sample rate
        max_taps: maximum number of taps to extract per channel
        window_ms: analysis window in milliseconds from IR start

    Returns:
        List of dicts sorted by delay_ms, each with:
            delay_ms, delay_samples, amplitude_l, amplitude_r, sign_l, sign_r, pan_lr
    """
    window_samples = int(window_ms * sr / 1000.0)

    def _extract_channel(ir, n_samples, n_taps):
        """Run OMP on a single channel, return list of (delay_samples, amplitude)."""
        sig = ir[:n_samples].astype(np.float64).copy()
        peak_abs = np.max(np.abs(sig))
        if peak_abs < 1e-30:
            return []

        residual = sig.copy()
        taps = []
        noise_floor = peak_abs * 1e-4

        for _ in range(n_taps):
            abs_res = np.abs(residual)
            best_idx = int(np.argmax(abs_res))
            if abs_res[best_idx] < noise_floor:
                break

            # Parabolic interpolation for sub-sample accuracy
            if 0 < best_idx < len(residual) - 1:
                alpha = residual[best_idx - 1]
                beta = residual[best_idx]
                gamma = residual[best_idx + 1]
                denom = alpha - 2.0 * beta + gamma
                if abs(denom) > 1e-12:
                    p = 0.5 * (alpha - gamma) / denom
                    refined_idx = best_idx + p
                    refined_amp = beta - 0.25 * (alpha - gamma) * p
                else:
                    refined_idx = float(best_idx)
                    refined_amp = float(beta)
            else:
                refined_idx = float(best_idx)
                refined_amp = float(residual[best_idx])

            taps.append((refined_idx, refined_amp))

            # Remove atom: zero peak, dampen neighbors to suppress sinc ringing
            residual[best_idx] = 0.0
            for off in [-2, -1, 1, 2]:
                ni = best_idx + off
                if 0 <= ni < len(residual):
                    residual[ni] *= 0.3

        return taps

    taps_l = _extract_channel(ir_l, window_samples, max_taps)
    taps_r = _extract_channel(ir_r, window_samples, max_taps)

    # Cross-reference L and R taps to compute pan
    # Build merged tap list: for each L tap, find nearest R tap within ±1 sample
    used_r = set()
    merged = []

    for delay_l, amp_l in taps_l:
        best_r_idx = None
        best_r_dist = 2.0  # > 1 sample threshold
        for ri, (delay_r, amp_r) in enumerate(taps_r):
            if ri in used_r:
                continue
            dist = abs(delay_l - delay_r)
            if dist < best_r_dist:
                best_r_dist = dist
                best_r_idx = ri

        if best_r_idx is not None and best_r_dist <= 1.0:
            delay_r, amp_r = taps_r[best_r_idx]
            used_r.add(best_r_idx)
            avg_delay = (delay_l + delay_r) / 2.0
            pan = (abs(amp_r) - abs(amp_l)) / (abs(amp_r) + abs(amp_l) + 1e-30)
        else:
            amp_r = 0.0
            avg_delay = delay_l
            pan = -1.0  # L-only

        merged.append({
            "delay_ms": avg_delay / sr * 1000.0,
            "delay_samples": avg_delay,
            "amplitude_l": float(amp_l),
            "amplitude_r": float(amp_r),
            "sign_l": 1 if amp_l >= 0 else -1,
            "sign_r": 1 if amp_r >= 0 else -1,
            "pan_lr": float(pan),
        })

    # Add unmatched R-only taps
    for ri, (delay_r, amp_r) in enumerate(taps_r):
        if ri not in used_r:
            merged.append({
                "delay_ms": delay_r / sr * 1000.0,
                "delay_samples": delay_r,
                "amplitude_l": 0.0,
                "amplitude_r": float(amp_r),
                "sign_l": 0,
                "sign_r": 1 if amp_r >= 0 else -1,
                "pan_lr": 1.0,  # R-only
            })

    merged.sort(key=lambda t: t["delay_ms"])
    return merged


def load_plugins(mode_name, compare=False):
    """Load DuskVerb (and optionally VintageVerb) for the given mode pairing.

    Returns:
        (dv_plugin, vv_plugin_or_None, pairing_dict)
    """
    # Find the pairing
    pairing = None
    for p in MODE_PAIRINGS:
        if p["name"].lower() == mode_name.lower():
            pairing = p
            break
    if pairing is None:
        valid = [p["name"] for p in MODE_PAIRINGS]
        print(f"ERROR: Unknown mode '{mode_name}'. Choose from: {valid}")
        sys.exit(1)

    # Load DuskVerb
    dv_path = find_plugin(DUSKVERB_PATHS)
    if not dv_path:
        print("ERROR: DuskVerb not found. Build it first:")
        print("  cd build && cmake --build . --target DuskVerb_AU -j8")
        sys.exit(1)
    print(f"Loading DuskVerb: {dv_path}")
    dv_plugin = load_plugin(dv_path)
    apply_duskverb_params(dv_plugin, pairing["duskverb"])
    print(f"  Configured: algorithm={pairing['duskverb']['algorithm']}")

    # Load VintageVerb if comparing
    vv_plugin = None
    if compare:
        vv_path = find_plugin(REFERENCE_REVERB_PATHS)
        if not vv_path:
            print("WARNING: VintageVerb not found — running DuskVerb only.")
        else:
            print(f"Loading VintageVerb: {vv_path}")
            vv_plugin = load_plugin(vv_path)
            apply_reference_params(vv_plugin, pairing["reference"])

            # Warm up VV — some AU plugins need to process audio before params take effect.
            # Force prepareToPlay by processing one buffer, then re-apply params.
            print("  Warming up VintageVerb...")
            sr = SAMPLE_RATE
            warmup = np.random.randn(int(sr * 1.0)).astype(np.float32) * 0.1
            warmup_stereo = np.stack([warmup, warmup], axis=0)
            _ = vv_plugin(warmup_stereo, sr)  # First pass: trigger initialization
            # Re-apply parameters after first processing pass
            apply_reference_params(vv_plugin, pairing["reference"])
            # Flush to clear state
            flush_plugin(vv_plugin, sr, 3.0)

            print(f"  Configured for mode pairing: {pairing['name']}")

    return dv_plugin, vv_plugin, pairing


# ---------------------------------------------------------------------------
# Decay time measurement helpers (EDT, T20, T30)
# ---------------------------------------------------------------------------
def measure_tx0(ir, sr, fit_range_db, bands=None):
    """Measure decay time with custom fit range, extrapolated to -60 dB.

    Args:
        ir: impulse response (1D float array)
        sr: sample rate
        fit_range_db: tuple (upper_db, lower_db), e.g. (-5, -15) for EDT
        bands: dict of {name: center_freq}; defaults to OCTAVE_BANDS

    Returns:
        dict mapping band name -> decay time in seconds (None if unmeasurable)
    """
    if bands is None:
        bands = metrics.OCTAVE_BANDS
    results = {}
    for name, fc in bands.items():
        filtered = metrics._octave_bandpass(ir, sr, fc)
        edc = metrics._compute_edc_raw(filtered)
        if edc is None or len(edc) < 2:
            results[name] = None
            continue
        edc_db = 10.0 * np.log10(np.maximum(edc / max(edc[0], 1e-20), 1e-20))
        noise_floor, noise_idx = metrics._lundeby_noise_floor(edc_db, sr)
        lo_db, hi_db = fit_range_db
        hi_db = max(hi_db, noise_floor + 5.0)
        mask = ((edc_db >= hi_db) & (edc_db <= lo_db)
                & (np.arange(len(edc_db)) < noise_idx))
        indices = np.where(mask)[0]
        if len(indices) < 10:
            results[name] = None
            continue
        times = indices.astype(np.float64) / sr
        values = edc_db[indices]
        coeffs = np.polyfit(times, values, 1)
        slope = coeffs[0]
        if slope >= 0:
            results[name] = None
            continue
        predicted = np.polyval(coeffs, times)
        ss_res = np.sum((values - predicted) ** 2)
        ss_tot = np.sum((values - np.mean(values)) ** 2)
        r_squared = 1.0 - ss_res / max(ss_tot, 1e-20)
        if r_squared < 0.90:
            results[name] = None
            continue
        results[name] = -60.0 / slope
    return results


def measure_edt(ir, sr, bands=None):
    """Early Decay Time: fit (0, -10) extrapolated to -60 dB."""
    return measure_tx0(ir, sr, (0, -10), bands=bands)


def measure_t20(ir, sr, bands=None):
    """T20: fit (-5, -25) extrapolated to -60 dB."""
    return measure_tx0(ir, sr, (-5, -25), bands=bands)


def measure_t30(ir, sr, bands=None):
    """T30: fit (-5, -35) extrapolated to -60 dB."""
    return measure_tx0(ir, sr, (-5, -35), bands=bands)


# ---------------------------------------------------------------------------
# ISO 3382 acoustic parameter helpers
# ---------------------------------------------------------------------------
def compute_iso3382_params(ir, sr):
    """Compute ISO 3382 acoustic parameters from a single-channel IR.

    Returns dict with: C50, C80, D50, Ts, BR, G (all broadband).
    """
    ir64 = ir.astype(np.float64)
    energy = ir64 ** 2
    total_energy = np.sum(energy)
    if total_energy < 1e-30:
        return {"C50": None, "C80": None, "D50": None, "Ts": None,
                "BR": None, "G": None}

    n_50ms = int(sr * 0.050)
    n_80ms = int(sr * 0.080)

    # Clamp to array length
    n_50ms = min(n_50ms, len(energy))
    n_80ms = min(n_80ms, len(energy))

    e_0_50 = np.sum(energy[:n_50ms])
    e_50_inf = np.sum(energy[n_50ms:])
    e_0_80 = np.sum(energy[:n_80ms])
    e_80_inf = np.sum(energy[n_80ms:])

    # C50 = 10*log10(energy_0_50ms / energy_50ms_inf)
    C50 = 10.0 * np.log10(max(e_0_50, 1e-30) / max(e_50_inf, 1e-30))

    # C80 = 10*log10(energy_0_80ms / energy_80ms_inf)
    C80 = 10.0 * np.log10(max(e_0_80, 1e-30) / max(e_80_inf, 1e-30))

    # D50 = energy_0_50ms / total_energy
    D50 = e_0_50 / total_energy

    # Ts = centre time (energy-weighted mean arrival time)
    sample_times = np.arange(len(energy), dtype=np.float64) / sr
    Ts = float(np.sum(sample_times * energy) / total_energy)

    # BR = bass ratio: average RT60 at 125+250 Hz / average RT60 at 500+1000 Hz
    rt60_bands = metrics.measure_rt60_per_band(ir, sr)
    rt60_125 = rt60_bands.get("125 Hz")
    rt60_250 = rt60_bands.get("250 Hz")
    rt60_500 = rt60_bands.get("500 Hz")
    rt60_1k = rt60_bands.get("1 kHz")
    if rt60_125 and rt60_250 and rt60_500 and rt60_1k:
        BR = ((rt60_125 + rt60_250) / 2.0) / ((rt60_500 + rt60_1k) / 2.0)
    else:
        BR = None

    # G = strength: 10*log10(total_energy / energy_of_direct_sound_at_10m)
    # For a unit impulse, reference energy = 1.0 (the impulse itself)
    # So G = 10*log10(total_energy)
    G = 10.0 * np.log10(max(total_energy, 1e-30))

    return {
        "C50": float(C50),
        "C80": float(C80),
        "D50": float(D50),
        "Ts": float(Ts),
        "BR": float(BR) if BR is not None else None,
        "G": float(G),
    }


# ---------------------------------------------------------------------------
# Temporal energy distribution
# ---------------------------------------------------------------------------
def compute_temporal_energy(ir, sr, windows_ms=None):
    """Compute energy in fine time windows.

    Args:
        ir: impulse response (1D)
        sr: sample rate
        windows_ms: list of (start_ms, end_ms) tuples

    Returns:
        list of dicts with keys: start_ms, end_ms, energy_db, energy_linear
    """
    if windows_ms is None:
        windows_ms = TEMPORAL_WINDOWS_MS
    ir64 = ir.astype(np.float64)
    energy_sq = ir64 ** 2
    total_energy = np.sum(energy_sq)
    result = []
    for start_ms, end_ms in windows_ms:
        s0 = int(sr * start_ms / 1000.0)
        s1 = int(sr * end_ms / 1000.0)
        s1 = min(s1, len(energy_sq))
        if s0 >= len(energy_sq):
            e = 0.0
        else:
            e = float(np.sum(energy_sq[s0:s1]))
        e_db = 10.0 * np.log10(max(e, 1e-30))
        e_frac = e / max(total_energy, 1e-30)
        result.append({
            "start_ms": start_ms,
            "end_ms": end_ms,
            "energy_db": e_db,
            "energy_fraction": e_frac,
        })
    return result


# ---------------------------------------------------------------------------
# Phase 0: Parameter Validation & Level Check
# ---------------------------------------------------------------------------
def run_phase0(dv_plugin, vv_plugin, sr, results):
    """Phase 0: Dump all plugin parameters and verify wet output level match.

    Runs a quick pink noise level check and flags warnings if levels differ
    by more than ±2 dB.  Populates results["phase0"].
    """
    print("\n=== Phase 0: Parameter Validation ===")

    phase0 = {}

    # --- Dump DuskVerb parameters ---
    dv_params = {}
    print("\n  DuskVerb parameters:")
    for name, val in discover_params(dv_plugin):
        if name in ('parameters', 'raw_state', 'installed_plugins',
                     'category', 'has_shared_container', 'is_effect',
                     'is_instrument', 'identifier', 'descriptive_name'):
            continue
        dv_params[name] = val
        print(f"    {name} = {val}")

    # --- Dump VintageVerb parameters ---
    vv_params = {}
    if vv_plugin:
        print("\n  VintageVerb parameters:")
        for name, val in discover_params(vv_plugin):
            if name in ('parameters', 'raw_state', 'installed_plugins',
                         'category', 'has_shared_container', 'is_effect',
                         'is_instrument', 'identifier', 'descriptive_name'):
                continue
            vv_params[name] = val
            print(f"    {name} = {val}")

    phase0["dv_params"] = {k: _json_safe(v) for k, v in dv_params.items()}
    phase0["vv_params"] = {k: _json_safe(v) for k, v in vv_params.items()}

    # --- Pink noise level check ---
    print("\n  Level check (3s pink noise @ -12 dBFS):")
    n = int(3 * sr)
    white = np.random.default_rng(42).standard_normal(n).astype(np.float32)
    fft = np.fft.rfft(white)
    freqs = np.fft.rfftfreq(n, 1.0 / sr)
    freqs[0] = 1.0
    fft *= 1.0 / np.sqrt(freqs)
    pink = np.fft.irfft(fft, n=n).astype(np.float32)
    pink *= 0.25 / (np.sqrt(np.mean(pink ** 2)) + 1e-30)
    stereo = np.stack([pink, pink], axis=0)

    dv_out = dv_plugin(stereo, sr)
    dv_rms = float(np.sqrt(np.mean(dv_out ** 2)))
    dv_db = 20.0 * np.log10(max(dv_rms, 1e-30))
    print(f"    DV wet RMS: {dv_db:.1f} dBFS")
    phase0["dv_rms_dbfs"] = round(dv_db, 1)

    if vv_plugin:
        vv_out = vv_plugin(stereo, sr)
        vv_rms = float(np.sqrt(np.mean(vv_out ** 2)))
        vv_db = 20.0 * np.log10(max(vv_rms, 1e-30))
        diff_db = 20.0 * np.log10(max(dv_rms, 1e-30) / max(vv_rms, 1e-30))
        print(f"    VV wet RMS: {vv_db:.1f} dBFS")

        ok = abs(diff_db) <= 2.0
        symbol = "\u2713" if ok else "\u26A0 WARNING"
        print(f"    Level match: {diff_db:+.1f} dB {symbol}")
        if not ok:
            print(f"    *** Level mismatch exceeds \u00b12 dB — check outputGain or parameter config ***")

        phase0["vv_rms_dbfs"] = round(vv_db, 1)
        phase0["level_diff_db"] = round(diff_db, 1)
        phase0["level_ok"] = ok

    # --- Impulse peak level check (Fix 1) ---
    print("\n  Impulse peak level check:")
    flush_plugin(dv_plugin, sr, FLUSH_DURATION)
    ir_len = int(2.0 * sr)
    impulse = np.zeros(ir_len, dtype=np.float32)
    impulse[0] = 1.0
    stereo_imp = np.stack([impulse, impulse], axis=0).astype(np.float32)
    dv_ir = dv_plugin(stereo_imp, sr)
    dv_peak = float(np.max(np.abs(dv_ir)))
    dv_peak_db = 20.0 * np.log10(max(dv_peak, 1e-30))
    dv_ir_rms = float(np.sqrt(np.mean(dv_ir[0] ** 2)))
    dv_crest = dv_peak / max(dv_ir_rms, 1e-30)
    print(f"    DV impulse peak: {dv_peak_db:.1f} dBFS  (crest factor: {dv_crest:.1f})")
    phase0["dv_impulse_peak_dbfs"] = round(dv_peak_db, 1)
    phase0["dv_crest_factor"] = round(dv_crest, 1)

    if vv_plugin:
        flush_plugin(vv_plugin, sr, FLUSH_DURATION)
        vv_ir = vv_plugin(stereo_imp, sr)
        vv_peak = float(np.max(np.abs(vv_ir)))
        vv_peak_db = 20.0 * np.log10(max(vv_peak, 1e-30))
        vv_ir_rms = float(np.sqrt(np.mean(vv_ir[0] ** 2)))
        vv_crest = vv_peak / max(vv_ir_rms, 1e-30)
        print(f"    VV impulse peak: {vv_peak_db:.1f} dBFS  (crest factor: {vv_crest:.1f})")
        peak_diff = dv_peak_db - vv_peak_db
        ok_peak = abs(peak_diff) <= 2.0
        symbol_p = "\u2713" if ok_peak else "\u26A0 WARNING"
        print(f"    Peak level match: {peak_diff:+.1f} dB {symbol_p}")
        if not ok_peak:
            print(f"    *** Peak level mismatch exceeds \u00b12 dB ***")
        phase0["vv_impulse_peak_dbfs"] = round(vv_peak_db, 1)
        phase0["vv_crest_factor"] = round(vv_crest, 1)
        phase0["impulse_peak_diff_db"] = round(peak_diff, 1)
        phase0["impulse_peak_ok"] = ok_peak

    # --- Pre-delay & attack time measurement (Fix 2) ---
    print("\n  Pre-delay & attack time (-40 dBFS threshold):")
    threshold_db = -40.0
    threshold_lin = 10.0 ** (threshold_db / 20.0)

    def measure_onset(ir_signal, label):
        """Find time of first sample exceeding threshold."""
        abs_ir = np.abs(ir_signal)
        above = np.where(abs_ir > threshold_lin)[0]
        if len(above) == 0:
            print(f"    {label}: no sample exceeds {threshold_db:.0f} dBFS")
            return None
        onset_sample = above[0]
        onset_ms = onset_sample / sr * 1000.0
        print(f"    {label} onset: {onset_ms:.1f} ms (sample {onset_sample})")
        return onset_ms

    dv_onset = measure_onset(dv_ir[0], "DV")
    phase0["dv_onset_ms"] = round(dv_onset, 2) if dv_onset is not None else None

    if vv_plugin:
        vv_onset = measure_onset(vv_ir[0], "VV")
        phase0["vv_onset_ms"] = round(vv_onset, 2) if vv_onset is not None else None
        if dv_onset is not None and vv_onset is not None:
            onset_diff = dv_onset - vv_onset
            ok_onset = abs(onset_diff) <= 5.0
            symbol_o = "\u2713" if ok_onset else "\u26A0 WARNING"
            print(f"    Onset diff: {onset_diff:+.1f} ms {symbol_o}")
            if not ok_onset:
                print(f"    *** Attack time difference exceeds \u00b15 ms ***")
            phase0["onset_diff_ms"] = round(onset_diff, 1)
            phase0["onset_ok"] = ok_onset

    results["phase0"] = phase0


def _json_safe(val):
    """Convert a value to something JSON-serialisable."""
    if isinstance(val, (int, float, bool, str, type(None))):
        return val
    if isinstance(val, (np.integer,)):
        return int(val)
    if isinstance(val, (np.floating,)):
        return float(val)
    if isinstance(val, (np.bool_,)):
        return bool(val)
    return str(val)


# ---------------------------------------------------------------------------
# Phase 1: Impulse Response Analysis (plots 01–05)
# ---------------------------------------------------------------------------
def run_phase1(dv_ir_l, dv_ir_r, vv_ir_l, vv_ir_r, sr, results, out_dir, save):
    """Phase 1: IR waveform, EDC, spectrogram, RT60 bars, EDT/T20/T30 table.

    Populates results["phase1"] and saves plots 01–05.
    """
    print("\n=== Phase 1: Impulse Response Analysis ===")
    plt = setup_dark_theme()

    has_vv = vv_ir_l is not None

    # --- Measure decay times ---
    dv_rt60 = metrics.measure_rt60_per_band(dv_ir_l, sr)
    dv_edt = measure_edt(dv_ir_l, sr)
    dv_t20 = measure_t20(dv_ir_l, sr)
    dv_t30 = measure_t30(dv_ir_l, sr)

    vv_rt60 = metrics.measure_rt60_per_band(vv_ir_l, sr) if has_vv else None
    vv_edt = measure_edt(vv_ir_l, sr) if has_vv else None
    vv_t20 = measure_t20(vv_ir_l, sr) if has_vv else None
    vv_t30 = measure_t30(vv_ir_l, sr) if has_vv else None

    results["phase1"] = {
        "dv_rt60": dv_rt60, "dv_edt": dv_edt, "dv_t20": dv_t20, "dv_t30": dv_t30,
        "vv_rt60": vv_rt60, "vv_edt": vv_edt, "vv_t20": vv_t20, "vv_t30": vv_t30,
    }

    # Print decay time summary
    print("\n  RT60 per octave band:")
    header = f"  {'Band':>10s}  {'RT60':>8s}  {'EDT':>8s}  {'T20':>8s}  {'T30':>8s}"
    if has_vv:
        header += f"  {'VV RT60':>8s}  {'VV EDT':>8s}  {'VV T20':>8s}  {'VV T30':>8s}"
    print(header)
    for band in metrics.OCTAVE_BANDS:
        vals = [dv_rt60.get(band), dv_edt.get(band), dv_t20.get(band), dv_t30.get(band)]
        line = f"  {band:>10s}"
        for v in vals:
            line += f"  {v:>7.3f}s" if v else f"  {'N/A':>8s}"
        if has_vv:
            vv_vals = [vv_rt60.get(band), vv_edt.get(band),
                       vv_t20.get(band), vv_t30.get(band)]
            for v in vv_vals:
                line += f"  {v:>7.3f}s" if v else f"  {'N/A':>8s}"
        print(line)

    if not save:
        return

    # --- Plot 01: IR Waveform (0-3s) ---
    fig, ax = plt.subplots(figsize=(12, 4))
    t_axis = np.arange(len(dv_ir_l)) / sr
    ax.plot(t_axis, dv_ir_l, color=DV_COLOR, alpha=0.8, linewidth=0.3, label='DuskVerb L')
    if has_vv:
        t_vv = np.arange(len(vv_ir_l)) / sr
        ax.plot(t_vv, vv_ir_l, color=VV_COLOR, alpha=0.6, linewidth=0.3, label='VintageVerb L')
    ax.set_xlabel('Time (s)')
    ax.set_ylabel('Amplitude')
    ax.set_xlim(0, 3)
    ax.set_title('01 — Impulse Response Waveform')
    ax.legend(loc='upper right')
    ax.grid(True)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, '01_ir_waveform.png'))
    plt.close(fig)
    print("  Saved 01_ir_waveform.png")

    # --- Plot 02: Early Reflections (stem plot 0-150ms) ---
    er_samples = int(0.150 * sr)
    fig, ax = plt.subplots(figsize=(14, 5))
    t_er = np.arange(min(er_samples, len(dv_ir_l))) / sr * 1000  # ms
    dv_er = np.abs(dv_ir_l[:len(t_er)])
    markerline, stemlines, baseline = ax.stem(t_er, dv_er, linefmt=DV_COLOR, markerfmt='o',
                                               basefmt='none', label='DuskVerb')
    stemlines.set_linewidth(0.5)
    stemlines.set_alpha(0.7)
    markerline.set_markersize(2)
    markerline.set_color(DV_COLOR)
    if has_vv:
        vv_er = np.abs(vv_ir_l[:len(t_er)])
        ml2, sl2, bl2 = ax.stem(t_er, vv_er, linefmt=VV_COLOR, markerfmt='s',
                                 basefmt='none', label='VintageVerb')
        sl2.set_linewidth(0.5)
        sl2.set_alpha(0.6)
        ml2.set_markersize(2)
        ml2.set_color(VV_COLOR)
    ax.set_xlabel('Time (ms)')
    ax.set_ylabel('|Amplitude|')
    ax.set_title('02 — Early Reflections (0–150ms)')
    ax.legend(loc='upper right')
    ax.grid(True)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, '02_early_reflections.png'))
    plt.close(fig)
    print("  Saved 02_early_reflections.png")

    # --- Plot 03: Spectrogram Comparison (side-by-side STFTs) ---
    dv_spec_t, dv_spec_f, dv_spec_db = metrics.compute_spectrogram(dv_ir_l, sr)
    fig, axes = plt.subplots(1, 2 if has_vv else 1, figsize=(14 if has_vv else 8, 5))
    if not has_vv:
        axes = [axes]

    def _plot_spectrogram(ax, spec_t, spec_f, spec_db, title, vmin_global=None, vmax_global=None):
        if len(spec_t) == 0:
            ax.text(0.5, 0.5, 'No data', ha='center', va='center',
                    transform=ax.transAxes, color='white')
            ax.set_title(title)
            return
        vmin = vmin_global if vmin_global is not None else np.max(spec_db) - 80
        vmax = vmax_global if vmax_global is not None else np.max(spec_db)
        im = ax.pcolormesh(spec_t, spec_f, spec_db.T, shading='auto',
                           vmin=vmin, vmax=vmax, cmap='inferno')
        ax.set_ylim(20, 20000)
        ax.set_yscale('log')
        ax.set_xlabel('Time (s)')
        ax.set_ylabel('Frequency (Hz)')
        ax.set_title(title)
        fig.colorbar(im, ax=ax, label='dB')

    # Use same color scale for both
    vmax_global = float(np.max(dv_spec_db))
    vmin_global = vmax_global - 80
    if has_vv:
        vv_spec_t, vv_spec_f, vv_spec_db = metrics.compute_spectrogram(vv_ir_l, sr)
        vmax_global = max(vmax_global, float(np.max(vv_spec_db)))
        vmin_global = vmax_global - 80

    _plot_spectrogram(axes[0], dv_spec_t, dv_spec_f, dv_spec_db, 'DuskVerb',
                      vmin_global, vmax_global)
    if has_vv:
        _plot_spectrogram(axes[1], vv_spec_t, vv_spec_f, vv_spec_db, 'VintageVerb',
                          vmin_global, vmax_global)

    fig.suptitle('03 — Spectrogram Comparison', color='white', fontsize=13)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, '03_spectrogram_comparison.png'))
    plt.close(fig)
    print("  Saved 03_spectrogram_comparison.png")

    # --- Plot 04: Energy Decay Relief (EDR) heatmaps ---
    # 1/3 octave band Schroeder integration per band
    third_oct_centers = sorted(metrics.THIRD_OCTAVE_BANDS.values())
    third_oct_names = sorted(metrics.THIRD_OCTAVE_BANDS.keys(),
                             key=lambda k: metrics.THIRD_OCTAVE_BANDS[k])
    n_bands = len(third_oct_centers)
    t_max = 3.0
    n_t = int(t_max * sr)

    def compute_edr(ir, sr):
        edr = np.zeros((n_bands, n_t))
        for i, fc in enumerate(third_oct_centers):
            filtered = metrics._third_octave_bandpass(ir[:n_t], sr, fc)
            energy = filtered.astype(np.float64) ** 2
            edc = np.cumsum(energy[::-1])[::-1]
            edc_db = 10.0 * np.log10(np.maximum(edc / max(edc[0], 1e-30), 1e-30))
            edr[i, :len(edc_db)] = edc_db
        return edr

    dv_edr = compute_edr(dv_ir_l, sr)
    n_sub = 3 if has_vv else 1
    fig, axes = plt.subplots(1, n_sub, figsize=(6 * n_sub, 6))
    if n_sub == 1:
        axes = [axes]

    t_edr = np.arange(n_t) / sr
    # Downsample for display
    step = max(1, n_t // 500)
    t_disp = t_edr[::step]

    def _plot_edr(ax, edr, title, vmin=-80, vmax=0):
        edr_disp = edr[:, ::step]
        im = ax.pcolormesh(t_disp, range(n_bands), edr_disp, shading='auto',
                           vmin=vmin, vmax=vmax, cmap='inferno')
        ax.set_yticks(range(0, n_bands, 3))
        ax.set_yticklabels([third_oct_names[i] for i in range(0, n_bands, 3)], fontsize=7)
        ax.set_xlabel('Time (s)')
        ax.set_ylabel('1/3 Octave Band')
        ax.set_title(title, fontsize=10)
        fig.colorbar(im, ax=ax, label='dB')

    _plot_edr(axes[0], dv_edr, 'DuskVerb EDR')
    if has_vv:
        vv_edr = compute_edr(vv_ir_l, sr)
        _plot_edr(axes[1], vv_edr, 'VintageVerb EDR')
        diff_edr = dv_edr - vv_edr
        _plot_edr(axes[2], diff_edr, 'Difference (DV-VV)', vmin=-20, vmax=20)
        axes[2].collections[0].set_cmap(plt.cm.RdBu_r)

    fig.suptitle('04 — Energy Decay Relief', color='white', fontsize=13)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, '04_edr.png'))
    plt.close(fig)
    print("  Saved 04_edr.png")

    # --- Plot 05: Energy Envelope (RMS 1ms windows, 0-3s) ---
    fig, ax = plt.subplots(figsize=(12, 5))
    dv_env = metrics.rms_envelope(dv_ir_l, sr, window_ms=1)
    t_env = np.arange(len(dv_env)) / sr
    mask_3s = t_env <= 3.0
    ax.plot(t_env[mask_3s], dv_env[mask_3s], color=DV_COLOR, linewidth=0.5,
            alpha=0.8, label='DuskVerb')
    if has_vv:
        vv_env = metrics.rms_envelope(vv_ir_l, sr, window_ms=1)
        t_vv_env = np.arange(len(vv_env)) / sr
        mask_vv = t_vv_env <= 3.0
        ax.plot(t_vv_env[mask_vv], vv_env[mask_vv], color=VV_COLOR, linewidth=0.5,
                alpha=0.7, label='VintageVerb')
    ax.set_xlabel('Time (s)')
    ax.set_ylabel('RMS Level (dB)')
    ax.set_title('05 — Energy Envelope (1ms RMS)')
    ax.set_ylim(-80, 0)
    ax.legend(loc='upper right')
    ax.grid(True)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, '05_energy_envelope.png'))
    plt.close(fig)
    print("  Saved 05_energy_envelope.png")


# ---------------------------------------------------------------------------
# Phase 2: ISO 3382 Acoustic Parameters (plot 06)
# ---------------------------------------------------------------------------
def run_phase2(dv_ir_l, vv_ir_l, sr, results, out_dir, save):
    """Phase 2: ISO 3382 acoustic parameters — C50, C80, D50, Ts, BR, G.

    Populates results["phase2"] and saves plot 06.
    """
    print("\n=== Phase 2: ISO 3382 Acoustic Parameters ===")
    plt = setup_dark_theme()

    has_vv = vv_ir_l is not None

    dv_iso = compute_iso3382_params(dv_ir_l, sr)
    vv_iso = compute_iso3382_params(vv_ir_l, sr) if has_vv else None

    results["phase2"] = {"dv_iso": dv_iso, "vv_iso": vv_iso}

    # Print summary
    params = ["C50", "C80", "D50", "Ts", "BR", "G"]
    units = {"C50": "dB", "C80": "dB", "D50": "", "Ts": "s", "BR": "", "G": "dB"}
    fmt = {"C50": ".1f", "C80": ".1f", "D50": ".3f", "Ts": ".4f", "BR": ".2f", "G": ".1f"}

    header = f"  {'Param':>8s}  {'DuskVerb':>12s}"
    if has_vv:
        header += f"  {'VintageVerb':>14s}  {'Delta':>10s}"
    print(header)
    for p in params:
        dv_v = dv_iso.get(p)
        dv_str = f"{dv_v:{fmt[p]}} {units[p]}" if dv_v is not None else "N/A"
        line = f"  {p:>8s}  {dv_str:>12s}"
        if has_vv:
            vv_v = vv_iso.get(p) if vv_iso else None
            vv_str = f"{vv_v:{fmt[p]}} {units[p]}" if vv_v is not None else "N/A"
            delta_str = ""
            if dv_v is not None and vv_v is not None:
                delta = dv_v - vv_v
                delta_str = f"{delta:+{fmt[p]}} {units[p]}"
            line += f"  {vv_str:>14s}  {delta_str:>10s}"
        print(line)

    if not save:
        return

    # --- Plot 06: ISO 3382 bar chart ---
    fig, axes = plt.subplots(2, 3, figsize=(14, 8))
    axes_flat = axes.flatten()

    for idx, p in enumerate(params):
        ax = axes_flat[idx]
        dv_v = dv_iso.get(p)
        vals = [dv_v if dv_v is not None else 0]
        labels = ['DV']
        colors = [DV_COLOR]
        if has_vv:
            vv_v = vv_iso.get(p) if vv_iso else None
            vals.append(vv_v if vv_v is not None else 0)
            labels.append('VV')
            colors.append(VV_COLOR)

        bar_x = np.arange(len(vals))
        ax.bar(bar_x, vals, color=colors, alpha=0.85)
        ax.set_xticks(bar_x)
        ax.set_xticklabels(labels)
        ax.set_title(f"{p} ({units[p]})" if units[p] else p, fontsize=11)
        ax.grid(True, axis='y')

        # Annotate values
        for bx, bv in zip(bar_x, vals):
            if bv != 0:
                ax.text(bx, bv, f"{bv:{fmt[p]}}", ha='center', va='bottom',
                        color='white', fontsize=9)

    fig.suptitle('06 — ISO 3382 Acoustic Parameters', color='white', fontsize=13)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, '06_iso3382.png'))
    plt.close(fig)
    print("  Saved 06_iso3382.png")


# ---------------------------------------------------------------------------
# Phase 3: Temporal Energy Distribution (plot 07)
# ---------------------------------------------------------------------------
def run_phase3(dv_ir_l, vv_ir_l, sr, results, out_dir, save):
    """Phase 3: Energy distribution across fine time windows.

    Populates results["phase3"] and saves plot 07.
    """
    print("\n=== Phase 3: Temporal Energy Distribution ===")
    plt = setup_dark_theme()

    has_vv = vv_ir_l is not None

    dv_temporal = compute_temporal_energy(dv_ir_l, sr)
    vv_temporal = compute_temporal_energy(vv_ir_l, sr) if has_vv else None

    results["phase3"] = {"dv_temporal": dv_temporal, "vv_temporal": vv_temporal}

    # Print table
    header = f"  {'Window (ms)':>16s}  {'DV Energy dB':>14s}  {'DV Fraction':>12s}"
    if has_vv:
        header += f"  {'VV Energy dB':>14s}  {'VV Fraction':>12s}"
    print(header)
    for i, w in enumerate(dv_temporal):
        label = f"{w['start_ms']:>5.0f}–{w['end_ms']:<5.0f}"
        line = f"  {label:>16s}  {w['energy_db']:>13.1f}  {w['energy_fraction']:>11.4f}"
        if has_vv and vv_temporal:
            vw = vv_temporal[i]
            line += f"  {vw['energy_db']:>13.1f}  {vw['energy_fraction']:>11.4f}"
        print(line)

    if not save:
        return

    # --- Plot 07: Temporal energy stacked bar ---
    window_labels = [f"{w['start_ms']:.0f}–{w['end_ms']:.0f}"
                     for w in dv_temporal]
    dv_fracs = [w['energy_fraction'] for w in dv_temporal]

    x = np.arange(len(window_labels))
    width = 0.35 if has_vv else 0.6

    fig, ax = plt.subplots(figsize=(14, 6))
    ax.bar(x - (width / 2 if has_vv else 0), dv_fracs, width,
           color=DV_COLOR, alpha=0.85, label='DuskVerb')
    if has_vv and vv_temporal:
        vv_fracs = [w['energy_fraction'] for w in vv_temporal]
        ax.bar(x + width / 2, vv_fracs, width,
               color=VV_COLOR, alpha=0.85, label='VintageVerb')

    ax.set_xticks(x)
    ax.set_xticklabels(window_labels, rotation=45, ha='right', fontsize=8)
    ax.set_xlabel('Time Window (ms)')
    ax.set_ylabel('Energy Fraction')
    ax.set_title('07 — Temporal Energy Distribution')
    ax.legend()
    ax.grid(True, axis='y')
    ax.set_yscale('log')
    ax.set_ylim(bottom=1e-6)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, '07_temporal_energy.png'))
    plt.close(fig)
    print("  Saved 07_temporal_energy.png")


# ─────────────────────────────────────────────────────────────────────
# Phase 4: Stereo, Modulation, Delay & Statistics
# ─────────────────────────────────────────────────────────────────────

def run_phase4a(dv_plugin, vv_plugin, sr, results, out_dir, save):
    """Phase 4a: Stereo topology — L/R coupling, cross-correlation."""
    print("\n── Phase 4a: Stereo Topology ──")

    has_vv = vv_plugin is not None
    n_samples = int(sr * IR_DURATION)
    impulse = np.zeros(n_samples, dtype=np.float32)
    impulse[0] = 1.0
    silence = np.zeros(n_samples, dtype=np.float32)

    # L-only and R-only impulses
    l_only = np.stack([impulse, silence], axis=0)
    r_only = np.stack([silence, impulse], axis=0)

    flush_plugin(dv_plugin, sr)
    dv_ll, dv_lr = process_stereo_input(dv_plugin, l_only, sr)
    flush_plugin(dv_plugin, sr)
    dv_rl, dv_rr = process_stereo_input(dv_plugin, r_only, sr)

    vv_ll = vv_lr = vv_rl = vv_rr = None
    if has_vv:
        flush_plugin(vv_plugin, sr)
        vv_ll, vv_lr = process_stereo_input(vv_plugin, l_only, sr)
        flush_plugin(vv_plugin, sr)
        vv_rl, vv_rr = process_stereo_input(vv_plugin, r_only, sr)

    # Compute coupling
    dv_lr_coupling = 10 * np.log10(np.sum(dv_lr**2) / (np.sum(dv_ll**2) + 1e-30))
    print(f"  DuskVerb  L→R coupling: {dv_lr_coupling:.1f} dB")
    phase4a = {"dv_lr_coupling_dB": float(dv_lr_coupling)}

    if has_vv:
        vv_lr_coupling = 10 * np.log10(np.sum(vv_lr**2) / (np.sum(vv_ll**2) + 1e-30))
        print(f"  VintageVerb L→R coupling: {vv_lr_coupling:.1f} dB")
        phase4a["vv_lr_coupling_dB"] = float(vv_lr_coupling)

    if save:
        plt = setup_dark_theme()
        early = int(0.2 * sr)  # first 200ms
        t_ms = np.arange(early) / sr * 1000

        # 08: Stereo topology grid
        fig, axes = plt.subplots(2, 2, figsize=(14, 10))
        titles = ['L→L', 'L→R', 'R→L', 'R→R']
        dv_sigs = [dv_ll, dv_lr, dv_rl, dv_rr]
        vv_sigs = [vv_ll, vv_lr, vv_rl, vv_rr]
        for idx, (ax, title) in enumerate(zip(axes.flat, titles)):
            ax.plot(t_ms, dv_sigs[idx][:early], color=DV_COLOR, alpha=0.9,
                    linewidth=0.6, label='DuskVerb')
            if has_vv:
                ax.plot(t_ms, vv_sigs[idx][:early], color=VV_COLOR, alpha=0.7,
                        linewidth=0.6, linestyle='--', label='VintageVerb')
            ax.set_title(title)
            ax.set_xlabel('Time (ms)')
            ax.set_ylabel('Amplitude')
            ax.legend(fontsize=7)
            ax.grid(True, alpha=0.3)
        fig.suptitle('08 — Stereo Topology (first 200ms)', fontsize=14)
        fig.tight_layout()
        fig.savefig(os.path.join(out_dir, '08_stereo_topology.png'))
        plt.close(fig)
        print("  Saved 08_stereo_topology.png")

        # 09: Stereo decorrelation — use standard stereo IR
        flush_plugin(dv_plugin, sr)
        dv_ir_l, dv_ir_r = capture_ir(dv_plugin, sr)

        vv_ir_l = vv_ir_r = None
        if has_vv:
            flush_plugin(vv_plugin, sr)
            vv_ir_l, vv_ir_r = capture_ir(vv_plugin, sr)

        win_samples = int(0.02 * sr)  # 20ms windows

        def windowed_xcorr(left, right, win_len):
            n_wins = len(left) // win_len
            corrs = []
            times = []
            for i in range(n_wins):
                l_seg = left[i * win_len:(i + 1) * win_len]
                r_seg = right[i * win_len:(i + 1) * win_len]
                l_norm = l_seg - np.mean(l_seg)
                r_norm = r_seg - np.mean(r_seg)
                denom = np.sqrt(np.sum(l_norm**2) * np.sum(r_norm**2))
                if denom > 1e-30:
                    corrs.append(float(np.sum(l_norm * r_norm) / denom))
                else:
                    corrs.append(0.0)
                times.append((i + 0.5) * win_len / sr * 1000)
            return np.array(times), np.array(corrs)

        dv_t, dv_corr = windowed_xcorr(dv_ir_l, dv_ir_r, win_samples)

        fig, ax = plt.subplots(figsize=(12, 5))
        ax.plot(dv_t, dv_corr, color=DV_COLOR, linewidth=0.8, label='DuskVerb')
        # Mark where correlation drops below 0.3
        below_03 = np.where(dv_corr < 0.3)[0]
        if len(below_03) > 0:
            ax.axvline(dv_t[below_03[0]], color=DV_COLOR, linestyle=':',
                       alpha=0.6, label=f'DV < 0.3 @ {dv_t[below_03[0]]:.0f}ms')
            phase4a["dv_decorr_time_ms"] = float(dv_t[below_03[0]])

        if has_vv:
            vv_t, vv_corr = windowed_xcorr(vv_ir_l, vv_ir_r, win_samples)
            ax.plot(vv_t, vv_corr, color=VV_COLOR, linewidth=0.8, label='VintageVerb')
            below_03_vv = np.where(vv_corr < 0.3)[0]
            if len(below_03_vv) > 0:
                ax.axvline(vv_t[below_03_vv[0]], color=VV_COLOR, linestyle=':',
                           alpha=0.6, label=f'VV < 0.3 @ {vv_t[below_03_vv[0]]:.0f}ms')
                phase4a["vv_decorr_time_ms"] = float(vv_t[below_03_vv[0]])

        ax.axhline(0.3, color='white', linestyle='--', alpha=0.4, label='Corr = 0.3')
        ax.set_xlabel('Time (ms)')
        ax.set_ylabel('L/R Correlation')
        ax.set_title('09 — Stereo Decorrelation (20ms windows)')
        ax.legend(fontsize=8)
        ax.grid(True, alpha=0.3)
        fig.tight_layout()
        fig.savefig(os.path.join(out_dir, '09_stereo_decorrelation.png'))
        plt.close(fig)
        print("  Saved 09_stereo_decorrelation.png")

    results["phase4a"] = phase4a


def run_phase4b(dv_plugin, vv_plugin, sr, results, out_dir, save):
    """Phase 4b: Modulation — spectral sidebands and IR variability."""
    print("\n── Phase 4b: Modulation Analysis ──")

    has_vv = vv_plugin is not None
    duration = 3.0
    n_samples = int(sr * duration)
    t = np.arange(n_samples) / sr
    amplitude = 10 ** (-12 / 20)  # -12 dBFS

    test_freqs = [250, 500, 1000, 2000, 4000]
    phase4b = {}

    if save:
        plt = setup_dark_theme()

        # 10: Modulation spectrum — 5 subplots
        fig, axes = plt.subplots(len(test_freqs), 1, figsize=(14, 3 * len(test_freqs)))
        for idx, freq in enumerate(test_freqs):
            ax = axes[idx]
            tone = (amplitude * np.sin(2 * np.pi * freq * t)).astype(np.float32)

            flush_plugin(dv_plugin, sr)
            dv_out_l, _ = process_stereo(dv_plugin, tone, sr)
            dv_fft = np.abs(np.fft.rfft(dv_out_l))
            dv_fft_db = 20 * np.log10(dv_fft / (np.max(dv_fft) + 1e-30) + 1e-30)
            fft_freqs = np.fft.rfftfreq(len(dv_out_l), 1.0 / sr)

            # Zoom ±50Hz around carrier
            mask = (fft_freqs >= freq - 50) & (fft_freqs <= freq + 50)
            ax.plot(fft_freqs[mask], dv_fft_db[mask], color=DV_COLOR,
                    linewidth=0.8, label='DuskVerb')

            if has_vv:
                flush_plugin(vv_plugin, sr)
                vv_out_l, _ = process_stereo(vv_plugin, tone, sr)
                vv_fft = np.abs(np.fft.rfft(vv_out_l))
                vv_fft_db = 20 * np.log10(vv_fft / (np.max(vv_fft) + 1e-30) + 1e-30)
                ax.plot(fft_freqs[mask], vv_fft_db[mask], color=VV_COLOR,
                        linewidth=0.8, linestyle='--', label='VintageVerb')

            ax.set_title(f'{freq} Hz input')
            ax.set_xlabel('Frequency (Hz)')
            ax.set_ylabel('Magnitude (dB)')
            ax.legend(fontsize=7)
            ax.grid(True, alpha=0.3)

        fig.suptitle('10 — Modulation Spectrum (±50Hz around carrier)', fontsize=14)
        fig.tight_layout()
        fig.savefig(os.path.join(out_dir, '10_modulation_spectrum.png'))
        plt.close(fig)
        print("  Saved 10_modulation_spectrum.png")

    # IR variability: 20 repeated captures
    n_captures = 20
    print(f"  Capturing {n_captures} IRs for variability analysis...")

    dv_irs = []
    for i in range(n_captures):
        ir_l, _ = capture_ir(dv_plugin, sr)
        dv_irs.append(ir_l)
    dv_irs = np.array(dv_irs)
    dv_mean = np.mean(dv_irs, axis=0)
    dv_std = np.std(dv_irs, axis=0)

    vv_irs = vv_mean = vv_std = None
    if has_vv:
        vv_irs_list = []
        for i in range(n_captures):
            ir_l, _ = capture_ir(vv_plugin, sr)
            vv_irs_list.append(ir_l)
        vv_irs = np.array(vv_irs_list)
        vv_mean = np.mean(vv_irs, axis=0)
        vv_std = np.std(vv_irs, axis=0)

    # Coefficient of variation at key times
    cv_times = [0.5, 1.0, 2.0]
    for t_sec in cv_times:
        idx = int(t_sec * sr)
        win = int(0.01 * sr)  # 10ms window around point
        lo, hi = max(0, idx - win // 2), min(len(dv_mean), idx + win // 2)
        dv_rms_mean = np.sqrt(np.mean(dv_mean[lo:hi]**2)) + 1e-30
        dv_rms_std = np.sqrt(np.mean(dv_std[lo:hi]**2))
        dv_cv = dv_rms_std / dv_rms_mean
        msg = f"  CV @ {t_sec}s — DuskVerb: {dv_cv:.4f}"
        phase4b[f"dv_cv_{t_sec}s"] = float(dv_cv)
        if has_vv and vv_mean is not None:
            vv_rms_mean = np.sqrt(np.mean(vv_mean[lo:hi]**2)) + 1e-30
            vv_rms_std = np.sqrt(np.mean(vv_std[lo:hi]**2))
            vv_cv = vv_rms_std / vv_rms_mean
            msg += f"  VintageVerb: {vv_cv:.4f}"
            phase4b[f"vv_cv_{t_sec}s"] = float(vv_cv)
        print(msg)

    if save:
        plt = setup_dark_theme()
        n_show = int(2.0 * sr)
        t_axis = np.arange(n_show) / sr

        n_subplots = 2 if has_vv else 1
        fig, axes = plt.subplots(n_subplots, 1, figsize=(14, 5 * n_subplots))
        if n_subplots == 1:
            axes = [axes]

        def plot_envelope(ax, mean_ir, std_ir, label, color):
            env_mean = 20 * np.log10(np.abs(mean_ir[:n_show]) + 1e-30)
            env_upper = 20 * np.log10(np.abs(mean_ir[:n_show]) + std_ir[:n_show] + 1e-30)
            env_lower = 20 * np.log10(np.maximum(
                np.abs(mean_ir[:n_show]) - std_ir[:n_show], 1e-30))
            ax.plot(t_axis, env_mean, color=color, linewidth=0.6, label=f'{label} mean')
            ax.fill_between(t_axis, env_lower, env_upper, color=color,
                            alpha=0.25, label=f'{label} ±σ')
            ax.set_xlabel('Time (s)')
            ax.set_ylabel('Amplitude (dB)')
            ax.set_title(f'{label} IR Variability ({n_captures} captures)')
            ax.legend(fontsize=8)
            ax.grid(True, alpha=0.3)
            ax.set_ylim(-120, 0)

        plot_envelope(axes[0], dv_mean, dv_std, 'DuskVerb', DV_COLOR)
        if has_vv and vv_mean is not None:
            plot_envelope(axes[1], vv_mean, vv_std, 'VintageVerb', VV_COLOR)

        fig.suptitle('11 — IR Variability (mean ± std, dB)', fontsize=14)
        fig.tight_layout()
        fig.savefig(os.path.join(out_dir, '11_ir_variability.png'))
        plt.close(fig)
        print("  Saved 11_ir_variability.png")

    results["phase4b"] = phase4b


def run_phase4c(dv_ir_l, vv_ir_l, sr, results, out_dir, save):
    """Phase 4c: Delay structure — autocorrelation and modal density."""
    print("\n── Phase 4c: Delay Structure ──")

    has_vv = vv_ir_l is not None
    phase4c = {}

    # Highpass at 200Hz
    sos_hp = butter(4, 200, btype='highpass', fs=sr, output='sos')

    def analyze_autocorr(ir, label, color):
        ir_hp = sosfiltfilt(sos_hp, ir)
        # Autocorrelation in 20-200ms range
        lag_lo = int(0.02 * sr)
        lag_hi = int(0.20 * sr)
        ir_norm = ir_hp / (np.max(np.abs(ir_hp)) + 1e-30)
        n = len(ir_norm)
        # FFT-based autocorrelation (O(N log N) instead of O(N²))
        half = ir_norm[:n // 2]
        fft_half = np.fft.rfft(half, n=2 * len(half))
        autocorr = np.fft.irfft(np.abs(fft_half) ** 2)[:len(half)]
        autocorr = autocorr / (autocorr[0] + 1e-30)  # normalize
        return autocorr, lag_lo, lag_hi

    dv_ac, lag_lo, lag_hi = analyze_autocorr(dv_ir_l, 'DuskVerb', DV_COLOR)
    vv_ac = None
    if has_vv:
        vv_ac, _, _ = analyze_autocorr(vv_ir_l, 'VintageVerb', VV_COLOR)

    # Find top 16 peaks in 20-200ms range
    def find_peaks_simple(ac, lag_lo, lag_hi, n_peaks=16):
        segment = ac[lag_lo:lag_hi]
        peaks = []
        for i in range(1, len(segment) - 1):
            if segment[i] > segment[i - 1] and segment[i] > segment[i + 1]:
                peaks.append((i + lag_lo, segment[i]))
        peaks.sort(key=lambda x: x[1], reverse=True)
        return peaks[:n_peaks]

    dv_peaks = find_peaks_simple(dv_ac, lag_lo, lag_hi)
    print(f"  DuskVerb top autocorr peaks:")
    for samp, val in dv_peaks[:5]:
        print(f"    {samp / sr * 1000:.1f}ms (sample {samp}): {val:.4f}")

    if has_vv and vv_ac is not None:
        vv_peaks = find_peaks_simple(vv_ac, lag_lo, lag_hi)
        print(f"  VintageVerb top autocorr peaks:")
        for samp, val in vv_peaks[:5]:
            print(f"    {samp / sr * 1000:.1f}ms (sample {samp}): {val:.4f}")

    # Late tail modal density
    late_lo = int(0.5 * sr)
    late_hi = int(2.0 * sr)

    def modal_density(ir, lo, hi, label):
        segment = ir[lo:hi]
        window = np.hanning(len(segment))
        spectrum = np.abs(np.fft.rfft(segment * window))
        spectrum_db = 20 * np.log10(spectrum + 1e-30)
        freqs = np.fft.rfftfreq(len(segment), 1.0 / sr)
        # Count peaks above median + 3dB per kHz
        mask_10k = freqs <= 10000
        spectrum_db_10k = spectrum_db[mask_10k]
        freqs_10k = freqs[mask_10k]
        median_db = np.median(spectrum_db_10k)
        above_thresh = spectrum_db_10k > (median_db + 3)
        # Count per kHz
        total_peaks = 0
        for fk in range(0, 10):
            band_mask = (freqs_10k >= fk * 1000) & (freqs_10k < (fk + 1) * 1000)
            n_above = np.sum(above_thresh & band_mask)
            total_peaks += n_above
        density = total_peaks / 10.0  # per kHz
        print(f"  {label} modal density: {density:.1f} peaks/kHz (above median+3dB)")
        return spectrum_db_10k, freqs_10k, density

    dv_spec, dv_freqs, dv_density = modal_density(dv_ir_l, late_lo, late_hi, 'DuskVerb')
    phase4c["dv_modal_density"] = float(dv_density)

    vv_spec = vv_freqs = None
    if has_vv:
        vv_spec, vv_freqs, vv_density = modal_density(vv_ir_l, late_lo, late_hi, 'VintageVerb')
        phase4c["vv_modal_density"] = float(vv_density)

    if save:
        plt = setup_dark_theme()

        # 12: Autocorrelation
        fig, ax = plt.subplots(figsize=(14, 5))
        lag_ms = np.arange(lag_hi) / sr * 1000
        ax.plot(lag_ms[lag_lo:lag_hi], dv_ac[lag_lo:lag_hi], color=DV_COLOR,
                linewidth=0.7, label='DuskVerb')
        for samp, val in dv_peaks:
            ax.annotate(f'{samp / sr * 1000:.1f}ms\n({samp})',
                        xy=(samp / sr * 1000, val), fontsize=5, color=DV_COLOR,
                        ha='center', va='bottom')
        if has_vv and vv_ac is not None:
            ax.plot(lag_ms[lag_lo:lag_hi], vv_ac[lag_lo:lag_hi], color=VV_COLOR,
                    linewidth=0.7, linestyle='--', label='VintageVerb')
            for samp, val in vv_peaks:
                ax.annotate(f'{samp / sr * 1000:.1f}ms\n({samp})',
                            xy=(samp / sr * 1000, val), fontsize=5, color=VV_COLOR,
                            ha='center', va='bottom')
        ax.set_xlabel('Lag (ms)')
        ax.set_ylabel('Normalized Autocorrelation')
        ax.set_title('12 — Autocorrelation (20–200ms, highpassed 200Hz)')
        ax.legend(fontsize=8)
        ax.grid(True, alpha=0.3)
        fig.tight_layout()
        fig.savefig(os.path.join(out_dir, '12_autocorrelation.png'))
        plt.close(fig)
        print("  Saved 12_autocorrelation.png")

        # 13: Modal density
        fig, ax = plt.subplots(figsize=(14, 5))
        ax.plot(dv_freqs, dv_spec, color=DV_COLOR, linewidth=0.5,
                alpha=0.8, label=f'DuskVerb ({dv_density:.1f}/kHz)')
        if has_vv and vv_spec is not None:
            ax.plot(vv_freqs, vv_spec, color=VV_COLOR, linewidth=0.5,
                    alpha=0.7, linestyle='--',
                    label=f'VintageVerb ({vv_density:.1f}/kHz)')
        ax.set_xlabel('Frequency (Hz)')
        ax.set_ylabel('Magnitude (dB)')
        ax.set_title('13 — Late Tail Modal Density (500ms–2s)')
        ax.legend(fontsize=8)
        ax.grid(True, alpha=0.3)
        ax.set_xlim(0, 10000)
        fig.tight_layout()
        fig.savefig(os.path.join(out_dir, '13_modal_density.png'))
        plt.close(fig)
        print("  Saved 13_modal_density.png")

    results["phase4c"] = phase4c


def run_phase4d(dv_ir_l, vv_ir_l, sr, results, out_dir, save):
    """Phase 4d: Late field statistics — amplitude distribution, kurtosis, skewness."""
    print("\n── Phase 4d: Late Field Statistics ──")

    has_vv = vv_ir_l is not None
    late_lo = int(0.5 * sr)
    late_hi = int(2.0 * sr)
    phase4d = {}

    dv_late = dv_ir_l[late_lo:late_hi]
    dv_kurt = float(scipy_kurtosis(dv_late, fisher=True))
    dv_skew = float(scipy_skew(dv_late))
    print(f"  DuskVerb    kurtosis (Fisher): {dv_kurt:.4f}, skewness: {dv_skew:.4f}")
    phase4d["dv_kurtosis"] = dv_kurt
    phase4d["dv_skewness"] = dv_skew

    vv_late = None
    if has_vv:
        vv_late = vv_ir_l[late_lo:late_hi]
        vv_kurt = float(scipy_kurtosis(vv_late, fisher=True))
        vv_skew = float(scipy_skew(vv_late))
        print(f"  VintageVerb kurtosis (Fisher): {vv_kurt:.4f}, skewness: {vv_skew:.4f}")
        phase4d["vv_kurtosis"] = vv_kurt
        phase4d["vv_skewness"] = vv_skew

    if save:
        plt = setup_dark_theme()
        from scipy.stats import norm

        n_subplots = 2 if has_vv else 1
        fig, axes = plt.subplots(1, n_subplots, figsize=(7 * n_subplots, 5))
        if n_subplots == 1:
            axes = [axes]

        def plot_hist(ax, data, label, color, kurt, skew_val):
            n_bins = 200
            counts, bin_edges, _ = ax.hist(data, bins=n_bins, density=True,
                                            color=color, alpha=0.6, label=label)
            # Gaussian fit overlay
            mu, sigma = np.mean(data), np.std(data)
            x_fit = np.linspace(bin_edges[0], bin_edges[-1], 500)
            ax.plot(x_fit, norm.pdf(x_fit, mu, sigma), color='white',
                    linewidth=1.2, linestyle='--', label='Gaussian fit')
            ax.set_xlabel('Amplitude')
            ax.set_ylabel('Density')
            ax.set_title(f'{label}\nKurtosis={kurt:.3f}, Skew={skew_val:.3f}')
            ax.legend(fontsize=8)
            ax.grid(True, alpha=0.3)

        plot_hist(axes[0], dv_late, 'DuskVerb', DV_COLOR, dv_kurt, dv_skew)
        if has_vv and vv_late is not None:
            plot_hist(axes[1], vv_late, 'VintageVerb', VV_COLOR,
                      phase4d["vv_kurtosis"], phase4d["vv_skewness"])

        fig.suptitle('14 — Late Field Amplitude Distribution (500ms–2s)', fontsize=14)
        fig.tight_layout()
        fig.savefig(os.path.join(out_dir, '14_late_field_stats.png'))
        plt.close(fig)
        print("  Saved 14_late_field_stats.png")

    results["phase4d"] = phase4d


# ─────────────────────────────────────────────────────────────────────
# Phase 5: Real Audio Comparison
# ─────────────────────────────────────────────────────────────────────

def compute_mfccs(signal, sr, n_mfcc=13, window_ms=50, hop_ms=25, n_mels=40, nfft=2048):
    """Compute MFCCs manually (no librosa dependency).

    Returns:
        mfccs: (n_mfcc, n_frames) array
        times: (n_frames,) array of frame center times in seconds
    """
    win_len = int(window_ms / 1000 * sr)
    hop_len = int(hop_ms / 1000 * sr)
    # Ensure nfft >= win_len
    nfft = max(nfft, win_len)
    # Windowed frames
    n_frames = 1 + (len(signal) - win_len) // hop_len
    if n_frames < 1:
        return np.zeros((n_mfcc, 1)), np.array([0.0])

    frames = np.zeros((n_frames, nfft))
    window = np.hanning(win_len)
    for i in range(n_frames):
        start = i * hop_len
        frame = signal[start:start + win_len] * window
        frames[i, :win_len] = frame

    # Power spectrum
    power = np.abs(np.fft.rfft(frames, n=nfft, axis=1))**2
    freqs = np.fft.rfftfreq(nfft, 1.0 / sr)

    # Mel filterbank
    def hz_to_mel(f):
        return 2595.0 * np.log10(1.0 + f / 700.0)

    def mel_to_hz(m):
        return 700.0 * (10.0 ** (m / 2595.0) - 1.0)

    mel_lo = hz_to_mel(0)
    mel_hi = hz_to_mel(sr / 2)
    mel_points = np.linspace(mel_lo, mel_hi, n_mels + 2)
    hz_points = mel_to_hz(mel_points)

    filterbank = np.zeros((n_mels, len(freqs)))
    for m in range(n_mels):
        lo, center, hi = hz_points[m], hz_points[m + 1], hz_points[m + 2]
        for k, f in enumerate(freqs):
            if lo <= f <= center and center > lo:
                filterbank[m, k] = (f - lo) / (center - lo)
            elif center < f <= hi and hi > center:
                filterbank[m, k] = (hi - f) / (hi - center)

    # Apply filterbank, log, DCT
    mel_spec = power @ filterbank.T  # (n_frames, n_mels)
    mel_spec = np.maximum(mel_spec, 1e-30)
    log_mel = np.log(mel_spec)

    # Type-II DCT (manual)
    n = log_mel.shape[1]
    dct_matrix = np.zeros((n_mfcc, n))
    for k in range(n_mfcc):
        for i in range(n):
            dct_matrix[k, i] = np.cos(np.pi * k * (2 * i + 1) / (2 * n))
    dct_matrix *= np.sqrt(2.0 / n)
    dct_matrix[0, :] *= np.sqrt(0.5)

    mfccs = (log_mel @ dct_matrix.T).T  # (n_mfcc, n_frames)
    times = np.arange(n_frames) * hop_len / sr + win_len / (2 * sr)
    return mfccs, times


def compute_third_octave_spectrum(signal, sr):
    """Compute 1/3-octave band levels using metrics module."""
    levels = {}
    for name, fc in metrics.THIRD_OCTAVE_BANDS.items():
        filtered = metrics._third_octave_bandpass(signal, sr, fc)
        rms = np.sqrt(np.mean(filtered**2))
        levels[name] = 20 * np.log10(rms + 1e-30)
    return levels


def _generate_test_signals(sr):
    """Generate 4 synthetic test signals for Phase 5."""
    signals = {}

    # 1. Snare: 200Hz sine + noise burst, repeating every 1.5s for 6s
    total_secs = 6.0
    n = int(total_secs * sr)
    snare_out = np.zeros(n, dtype=np.float32)
    repeat_samples = int(1.5 * sr)
    t_hit = np.arange(int(0.1 * sr)) / sr  # 100ms envelope
    for start in range(0, n, repeat_samples):
        hit_len = min(int(0.1 * sr), n - start)
        t_h = np.arange(hit_len) / sr
        # Tonal component: 200Hz, 5ms attack, 50ms decay
        attack = np.minimum(t_h / 0.005, 1.0)
        decay = np.exp(-t_h / 0.05)
        tone = 0.5 * np.sin(2 * np.pi * 200 * t_h) * attack * decay
        # Noise component: 2ms attack, 30ms decay, bandpassed 2-8kHz
        noise_env = np.minimum(t_h / 0.002, 1.0) * np.exp(-t_h / 0.03)
        noise_raw = np.random.randn(hit_len).astype(np.float32) * noise_env
        sos_bp = butter(4, [2000, 8000], btype='bandpass', fs=sr, output='sos')
        if hit_len > 20:
            noise_filt = sosfiltfilt(sos_bp, noise_raw).astype(np.float32)
        else:
            noise_filt = noise_raw
        snare_out[start:start + hit_len] += (tone + 0.3 * noise_filt).astype(np.float32)
    snare_out *= 0.5 / (np.max(np.abs(snare_out)) + 1e-30)
    signals['snare'] = snare_out

    # 2. Vocal: 5 tone bursts with harmonics
    vocal_freqs = [150, 200, 250, 180, 220]
    burst_dur = 0.2
    gap_dur = 0.05
    total_vocal = len(vocal_freqs) * (burst_dur + gap_dur)
    n_vocal = int(total_vocal * sr)
    vocal_out = np.zeros(n_vocal, dtype=np.float32)
    pos = 0
    for f0 in vocal_freqs:
        burst_len = int(burst_dur * sr)
        t_b = np.arange(burst_len) / sr
        burst = np.zeros(burst_len, dtype=np.float32)
        for h in range(1, 4):  # 3 harmonics
            burst += (0.5 / h) * np.sin(2 * np.pi * f0 * h * t_b).astype(np.float32)
        # Apply smooth envelope
        env = np.sin(np.pi * t_b / burst_dur).astype(np.float32)
        burst *= env
        end_pos = min(pos + burst_len, n_vocal)
        vocal_out[pos:end_pos] = burst[:end_pos - pos]
        pos += burst_len + int(gap_dur * sr)
    vocal_out *= 0.5 / (np.max(np.abs(vocal_out)) + 1e-30)
    signals['vocal'] = vocal_out

    # 3. Piano: C3+E3+G3+C4 chord with 6 harmonics each, 2s decay
    piano_dur = 4.0
    n_piano = int(piano_dur * sr)
    t_p = np.arange(n_piano) / sr
    piano_freqs = [130.81, 164.81, 196.00, 261.63]  # C3, E3, G3, C4
    piano_out = np.zeros(n_piano, dtype=np.float32)
    decay_env = np.exp(-t_p / 2.0).astype(np.float32)
    for f0 in piano_freqs:
        for h in range(1, 7):  # 6 harmonics
            amp = 0.3 / (h ** 1.5)
            piano_out += (amp * np.sin(2 * np.pi * f0 * h * t_p) * decay_env).astype(
                np.float32)
    piano_out *= 0.5 / (np.max(np.abs(piano_out)) + 1e-30)
    signals['piano'] = piano_out

    # 4. Pink noise: 3s at -18dBFS
    pink_dur = 3.0
    n_pink = int(pink_dur * sr)
    white = np.random.randn(n_pink).astype(np.float32)
    # 1/sqrt(f) filter via cascaded first-order filters (Voss-McCartney approx)
    pink_fft = np.fft.rfft(white)
    freqs_pink = np.fft.rfftfreq(n_pink, 1.0 / sr)
    freqs_pink[0] = 1.0  # avoid div by zero
    pink_fft *= 1.0 / np.sqrt(freqs_pink)
    pink_out = np.fft.irfft(pink_fft, n=n_pink).astype(np.float32)
    target_rms = 10 ** (-18 / 20)
    current_rms = np.sqrt(np.mean(pink_out**2)) + 1e-30
    pink_out *= target_rms / current_rms
    signals['pink_noise'] = pink_out

    return signals


def _compute_spectral_tilt(signal, sr):
    """Compute spectral tilt: energy above 1kHz vs below 1kHz, in dB."""
    from scipy.signal import butter, sosfilt
    # Low band: 20-1000Hz
    sos_lo = butter(4, [20, 1000], btype='bandpass', fs=sr, output='sos')
    lo = sosfilt(sos_lo, signal)
    lo_rms = np.sqrt(np.mean(lo**2))
    # High band: 1000-16000Hz
    hi_limit = min(16000, sr / 2 - 100)
    sos_hi = butter(4, [1000, hi_limit], btype='bandpass', fs=sr, output='sos')
    hi = sosfilt(sos_hi, signal)
    hi_rms = np.sqrt(np.mean(hi**2))
    return 20.0 * np.log10((hi_rms + 1e-30) / (lo_rms + 1e-30))


# 1/3-octave bands extended down to 20Hz for spectral EQ match
_EQ_MATCH_BANDS = [
    ("20", 20), ("25", 25), ("31", 31.5), ("40", 40), ("50", 50),
    ("63", 63), ("80", 80), ("100", 100), ("125", 125), ("160", 160),
    ("200", 200), ("250", 250), ("315", 315), ("400", 400), ("500", 500),
    ("630", 630), ("800", 800), ("1k", 1000), ("1.25k", 1250), ("1.6k", 1600),
    ("2k", 2000), ("2.5k", 2500), ("3.15k", 3150), ("4k", 4000), ("5k", 5000),
    ("6.3k", 6300), ("8k", 8000), ("10k", 10000), ("12.5k", 12500), ("16k", 16000),
]


def _compute_spectral_eq_match(dv_signal, vv_signal, sr):
    """Compute per-band dB difference between DV and VV output spectra.

    Level-normalized: subtracts mean level offset before scoring, so this
    measures EQ shape match (relative frequency balance) not absolute level.
    This matches what Logic Pro's Match EQ displays.

    Returns (band_diffs, mean_offset, centered_mad, score):
        band_diffs: list of (name, center_freq, dv_db, vv_db, raw_diff, centered_diff)
        mean_offset: mean dB difference across all bands (level offset)
        centered_mad: mean |centered_diff| across bands in dB
        score: 0-100 (100 = flat shape match, 0 = >=10dB average deviation)
    """
    raw_diffs = []
    band_data = []
    for name, fc in _EQ_MATCH_BANDS:
        if fc >= sr / 2 - 100:
            continue
        dv_filt = metrics._third_octave_bandpass(dv_signal, sr, fc)
        vv_filt = metrics._third_octave_bandpass(vv_signal, sr, fc)
        dv_rms = np.sqrt(np.mean(dv_filt**2))
        vv_rms = np.sqrt(np.mean(vv_filt**2))
        dv_db = 20.0 * np.log10(max(dv_rms, 1e-30))
        vv_db = 20.0 * np.log10(max(vv_rms, 1e-30))
        diff = dv_db - vv_db
        band_data.append((name, fc, dv_db, vv_db, diff))
        raw_diffs.append(diff)

    # Subtract mean level offset to isolate EQ shape
    mean_offset = float(np.mean(raw_diffs)) if raw_diffs else 0.0
    band_diffs = []
    centered_abs_devs = []
    for name, fc, dv_db, vv_db, raw_diff in band_data:
        centered = raw_diff - mean_offset
        band_diffs.append((name, fc, dv_db, vv_db, raw_diff, centered))
        centered_abs_devs.append(abs(centered))

    centered_mad = float(np.mean(centered_abs_devs)) if centered_abs_devs else 0.0
    score = max(0.0, 100.0 - centered_mad * 10.0)
    return band_diffs, mean_offset, centered_mad, score


def _load_user_audio(path, sr):
    """Load a user audio file, resample if needed, return mono float32."""
    import soundfile as sf
    data, file_sr = sf.read(path, dtype='float32')
    # Stereo → mono
    if data.ndim == 2:
        data = (data[:, 0] + data[:, 1]) / 2.0
    # Resample if needed
    if file_sr != sr:
        from scipy.signal import resample
        new_len = int(len(data) * sr / file_sr)
        data = resample(data, new_len).astype(np.float32)
    # Truncate to 5 seconds max
    max_samples = int(5.0 * sr)
    if len(data) > max_samples:
        data = data[:max_samples]
    return data.astype(np.float32)


def run_phase5(dv_plugin, vv_plugin, sr, results, out_dir, save,
               input_path=None):
    """Phase 5: Real audio comparison — synthetic signals through both reverbs."""
    print("\n── Phase 5: Real Audio Comparison ──")

    has_vv = vv_plugin is not None
    test_signals = _generate_test_signals(sr)

    # Add custom user input if provided
    if input_path:
        try:
            user_sig = _load_user_audio(input_path, sr)
            test_signals['user_input'] = user_sig
            print(f"  Loaded custom input: {input_path} ({len(user_sig)/sr:.2f}s)")
        except Exception as e:
            print(f"  WARNING: Failed to load custom input: {e}")

    phase5 = {}

    summary_rows = []

    for sig_name, dry_signal in test_signals.items():
        print(f"\n  Processing: {sig_name}")

        # Add flush duration of silence after signal
        flush_len = int(FLUSH_DURATION * sr)
        padded = np.concatenate([dry_signal, np.zeros(flush_len, dtype=np.float32)])

        flush_plugin(dv_plugin, sr)
        dv_out_l, dv_out_r = process_stereo(dv_plugin, padded, sr)

        vv_out_l = vv_out_r = None
        if has_vv:
            flush_plugin(vv_plugin, sr)
            vv_out_l, vv_out_r = process_stereo(vv_plugin, padded, sr)

        # Export raw outputs for DAW comparison
        if sig_name == 'user_input' and save:
            import soundfile as sf
            sf.write(os.path.join(out_dir, "dv_output.wav"),
                     np.stack([dv_out_l, dv_out_r], axis=1), sr)
            if has_vv:
                sf.write(os.path.join(out_dir, "vv_output.wav"),
                         np.stack([vv_out_l, vv_out_r], axis=1), sr)
            print(f"    Saved DV/VV output WAVs to {out_dir}")

        # Report level difference (no normalization — outputGain should handle matching)
        dv_rms = float(np.sqrt(np.mean(dv_out_l**2)))
        dv_rms_db = 20.0 * np.log10(max(dv_rms, 1e-30))
        if has_vv:
            vv_rms = float(np.sqrt(np.mean(vv_out_l**2)))
            vv_rms_db = 20.0 * np.log10(max(vv_rms, 1e-30))
            level_diff = dv_rms_db - vv_rms_db
            print(f"    Level: DV {dv_rms_db:.1f} dBFS, VV {vv_rms_db:.1f} dBFS, Δ {level_diff:+.1f} dB")

        sig_results = {}
        sig_results["dv_rms_dbfs"] = round(dv_rms_db, 1)
        if has_vv:
            sig_results["vv_rms_dbfs"] = round(vv_rms_db, 1)
            sig_results["level_diff_db"] = round(level_diff, 1)

        # 1/3 octave spectrum
        dv_spec = compute_third_octave_spectrum(dv_out_l, sr)
        vv_spec = None
        if has_vv:
            vv_spec = compute_third_octave_spectrum(vv_out_l, sr)

        # RMS envelope (5ms windows)
        win_env = int(0.005 * sr)
        def rms_env(sig, win):
            n_wins = len(sig) // win
            env = np.zeros(n_wins)
            for i in range(n_wins):
                env[i] = np.sqrt(np.mean(sig[i * win:(i + 1) * win]**2))
            return env

        dv_env = rms_env(dv_out_l, win_env)
        vv_env = rms_env(vv_out_l, win_env) if has_vv else None

        # Temporal energy
        dv_temporal = compute_temporal_energy(dv_out_l, sr)
        vv_temporal = compute_temporal_energy(vv_out_l, sr) if has_vv else None

        # MFCCs
        dv_mfccs, mfcc_times = compute_mfccs(dv_out_l, sr)
        vv_mfccs = None
        mfcc_distance = None
        mean_mfcc_dist = 0.0
        if has_vv:
            vv_mfccs, _ = compute_mfccs(vv_out_l, sr)
            n_frames = min(dv_mfccs.shape[1], vv_mfccs.shape[1])
            mfcc_distance = np.sqrt(np.sum(
                (dv_mfccs[:, :n_frames] - vv_mfccs[:, :n_frames])**2, axis=0))
            mean_mfcc_dist = float(np.mean(mfcc_distance))
            sig_results["mean_mfcc_distance"] = mean_mfcc_dist

        # Spectral correlation
        spec_corr = 0.0
        if has_vv and vv_spec is not None:
            bands = sorted(dv_spec.keys(), key=lambda x: metrics.THIRD_OCTAVE_BANDS.get(x, 0))
            dv_vals = np.array([dv_spec[b] for b in bands])
            vv_vals = np.array([vv_spec[b] for b in bands])
            if np.std(dv_vals) > 0 and np.std(vv_vals) > 0:
                spec_corr = float(np.corrcoef(dv_vals, vv_vals)[0, 1])
            sig_results["spectral_correlation"] = spec_corr

        # Envelope correlation
        env_corr = 0.0
        if has_vv and vv_env is not None:
            n_min = min(len(dv_env), len(vv_env))
            dv_e = dv_env[:n_min]
            vv_e = vv_env[:n_min]
            if np.std(dv_e) > 0 and np.std(vv_e) > 0:
                env_corr = float(np.corrcoef(dv_e, vv_e)[0, 1])
            sig_results["envelope_correlation"] = env_corr

        # Spectral tilt: energy above 1kHz vs below 1kHz (dB)
        dv_tilt = _compute_spectral_tilt(dv_out_l, sr)
        sig_results["dv_spectral_tilt"] = round(dv_tilt, 2)
        vv_tilt = None
        tilt_diff = 0.0
        if has_vv:
            vv_tilt = _compute_spectral_tilt(vv_out_l, sr)
            tilt_diff = dv_tilt - vv_tilt
            sig_results["vv_spectral_tilt"] = round(vv_tilt, 2)
            sig_results["spectral_tilt_diff"] = round(tilt_diff, 2)

        # Signal level difference (already computed above as level_diff)
        level_d = level_diff if has_vv else 0.0

        # Spectral EQ match (1/3-octave difference curve, like Match EQ)
        eq_match_score = 0.0
        eq_match_mad = 0.0
        eq_mean_offset = 0.0
        eq_band_diffs = []
        if has_vv:
            eq_band_diffs, eq_mean_offset, eq_match_mad, eq_match_score = \
                _compute_spectral_eq_match(dv_out_l, vv_out_l, sr)
            sig_results["eq_match_mean_offset_dB"] = round(eq_mean_offset, 2)
            sig_results["eq_match_centered_mad_dB"] = round(eq_match_mad, 2)
            sig_results["eq_match_score"] = round(eq_match_score, 1)

        # Print per-band EQ curve for user_input
        if sig_name == 'user_input' and eq_band_diffs:
            print(f"\n    Spectral EQ match (level-normalized, like Match EQ):")
            print(f"      Mean level offset: {eq_mean_offset:+.1f} dB "
                  f"({'DV louder' if eq_mean_offset > 0 else 'DV quieter'}"
                  f" — ignored for EQ shape scoring)")
            print(f"      Centered EQ deviations:")
            for bname, bfc, dv_db, vv_db, raw_diff, centered in eq_band_diffs:
                tag = " (DV bass-heavy)" if centered > 3.0 and bfc < 500 else \
                      " (DV bass-thin)" if centered < -3.0 and bfc < 500 else \
                      " (DV bright)" if centered > 3.0 and bfc >= 500 else \
                      " (DV dark)" if centered < -3.0 and bfc >= 500 else ""
                flag = " ⚠" if abs(centered) > 5.0 else ""
                print(f"        {bname:>6s} Hz: DV={dv_db:.1f} VV={vv_db:.1f}  raw={raw_diff:+.1f}  centered={centered:+.1f}{flag}{tag}")
            print(f"      Mean |shape deviation|: {eq_match_mad:.1f} dB → score: {eq_match_score:.0f}/100")

        phase5[sig_name] = sig_results
        summary_rows.append((sig_name, mean_mfcc_dist, spec_corr, env_corr,
                             dv_tilt, vv_tilt, tilt_diff, level_d))

        if save:
            plt = setup_dark_theme()

            # 15_{signal}_spectral.png
            fig, ax = plt.subplots(figsize=(12, 5))
            bands = sorted(dv_spec.keys(), key=lambda x: metrics.THIRD_OCTAVE_BANDS.get(x, 0))
            x_pos = np.arange(len(bands))
            dv_vals = [dv_spec[b] for b in bands]
            ax.plot(x_pos, dv_vals, 'o-', color=DV_COLOR, linewidth=1, label='DuskVerb')
            if has_vv and vv_spec is not None:
                vv_vals = [vv_spec[b] for b in bands]
                ax.plot(x_pos, vv_vals, 's--', color=VV_COLOR, linewidth=1,
                        label='VintageVerb')
            ax.set_xticks(x_pos)
            ax.set_xticklabels(bands, rotation=45, ha='right', fontsize=7)
            ax.set_xlabel('1/3-Octave Band')
            ax.set_ylabel('Level (dB)')
            ax.set_title(f'15 — {sig_name}: 1/3 Octave Spectrum')
            ax.legend(fontsize=8)
            ax.grid(True, alpha=0.3)
            fig.tight_layout()
            fig.savefig(os.path.join(out_dir, f'15_{sig_name}_spectral.png'))
            plt.close(fig)

            # 15_{signal}_envelope.png
            fig, ax = plt.subplots(figsize=(12, 5))
            env_t = np.arange(len(dv_env)) * win_env / sr
            dv_env_db = 20 * np.log10(dv_env + 1e-30)
            ax.plot(env_t, dv_env_db, color=DV_COLOR, linewidth=0.6, label='DuskVerb')
            if has_vv and vv_env is not None:
                vv_env_db = 20 * np.log10(vv_env + 1e-30)
                env_t_vv = np.arange(len(vv_env)) * win_env / sr
                ax.plot(env_t_vv, vv_env_db, color=VV_COLOR, linewidth=0.6,
                        linestyle='--', label='VintageVerb')
            ax.set_xlabel('Time (s)')
            ax.set_ylabel('RMS Level (dB)')
            ax.set_title(f'15 — {sig_name}: RMS Envelope (5ms windows)')
            ax.legend(fontsize=8)
            ax.grid(True, alpha=0.3)
            fig.tight_layout()
            fig.savefig(os.path.join(out_dir, f'15_{sig_name}_envelope.png'))
            plt.close(fig)

            # 15_{signal}_temporal_energy.png
            fig, ax = plt.subplots(figsize=(12, 5))
            window_labels = [f"{w['start_ms']:.0f}-{w['end_ms']:.0f}"
                             for w in dv_temporal]
            x = np.arange(len(window_labels))
            width = 0.35
            dv_fracs = [w['energy_fraction'] for w in dv_temporal]
            ax.bar(x - width / 2, dv_fracs, width, color=DV_COLOR,
                   alpha=0.85, label='DuskVerb')
            if has_vv and vv_temporal is not None:
                vv_fracs = [w['energy_fraction'] for w in vv_temporal]
                ax.bar(x + width / 2, vv_fracs, width, color=VV_COLOR,
                       alpha=0.85, label='VintageVerb')
            ax.set_xticks(x)
            ax.set_xticklabels(window_labels, rotation=45, ha='right', fontsize=8)
            ax.set_xlabel('Time Window (ms)')
            ax.set_ylabel('Energy Fraction')
            ax.set_title(f'15 — {sig_name}: Temporal Energy')
            ax.legend(fontsize=8)
            ax.grid(True, axis='y', alpha=0.3)
            ax.set_yscale('log')
            ax.set_ylim(bottom=1e-6)
            fig.tight_layout()
            fig.savefig(os.path.join(out_dir, f'15_{sig_name}_temporal_energy.png'))
            plt.close(fig)

            # 15_{signal}_mfcc.png
            if has_vv and mfcc_distance is not None:
                fig, axes = plt.subplots(3, 1, figsize=(14, 10))
                n_frames = min(dv_mfccs.shape[1], vv_mfccs.shape[1])
                t_frames = mfcc_times[:n_frames]

                im0 = axes[0].imshow(dv_mfccs[:, :n_frames], aspect='auto',
                                      origin='lower', cmap='inferno',
                                      extent=[t_frames[0], t_frames[-1],
                                              0, dv_mfccs.shape[0]])
                axes[0].set_title('DuskVerb MFCCs')
                axes[0].set_ylabel('MFCC Index')
                fig.colorbar(im0, ax=axes[0])

                im1 = axes[1].imshow(vv_mfccs[:, :n_frames], aspect='auto',
                                      origin='lower', cmap='inferno',
                                      extent=[t_frames[0], t_frames[-1],
                                              0, vv_mfccs.shape[0]])
                axes[1].set_title('VintageVerb MFCCs')
                axes[1].set_ylabel('MFCC Index')
                fig.colorbar(im1, ax=axes[1])

                axes[2].plot(t_frames, mfcc_distance[:n_frames],
                             color='white', linewidth=0.8)
                axes[2].set_title(f'MFCC Distance (mean={mean_mfcc_dist:.2f})')
                axes[2].set_xlabel('Time (s)')
                axes[2].set_ylabel('Euclidean Distance')
                axes[2].grid(True, alpha=0.3)

                fig.suptitle(f'15 — {sig_name}: MFCC Analysis', fontsize=14)
                fig.tight_layout()
                fig.savefig(os.path.join(out_dir, f'15_{sig_name}_mfcc.png'))
                plt.close(fig)

            print(f"    Saved 15_{sig_name}_*.png")

    # Summary table
    print("\n  ┌─────────────┬──────────────┬──────────────┬──────────────┬──────────────┬──────────────┐")
    print("  │ Signal      │ MFCC Dist    │ Spec Corr    │ Env Corr     │ Tilt Δ (dB)  │ Level Δ (dB) │")
    print("  ├─────────────┼──────────────┼──────────────┼──────────────┼──────────────┼──────────────┤")
    for name, mfcc_d, sc, ec, dv_t, vv_t, td, ld in summary_rows:
        tilt_flag = " ⚠" if abs(td) > 3.0 else ""
        print(f"  │ {name:<11s} │ {mfcc_d:>10.3f}  │ {sc:>10.4f}  │ {ec:>10.4f}  │ {td:>+9.1f}{tilt_flag:2s} │ {ld:>+9.1f}    │")
    print("  └─────────────┴──────────────┴──────────────┴──────────────┴──────────────┴──────────────┘")

    # Highlight user_input if present
    for name, mfcc_d, sc, ec, dv_t, vv_t, td, ld in summary_rows:
        if name == 'user_input':
            print(f"\n  ★ Custom Input Results:")
            print(f"    MFCC distance:     {mfcc_d:.1f}")
            print(f"    Spectral corr:     {sc:.4f}")
            print(f"    Envelope corr:     {ec:.4f}")
            print(f"    DV spectral tilt:  {dv_t:+.1f} dB")
            if vv_t is not None:
                print(f"    VV spectral tilt:  {vv_t:+.1f} dB")
                print(f"    Tilt difference:   {td:+.1f} dB" +
                      (" ⚠ WARNING: >±3dB" if abs(td) > 3.0 else ""))
            print(f"    Level difference:  {ld:+.1f} dB")

    results["phase5"] = phase5


# ─────────────────────────────────────────────────────────────────────
# Phase 6: Structural & Transfer Analysis
# ─────────────────────────────────────────────────────────────────────

def run_phase6a(dv_ir_l, vv_ir_l, sr, results, out_dir, save):
    """Phase 6a: Phase analysis — magnitude spectrum and group delay of early IR."""
    print("\n── Phase 6a: Phase Analysis (first 50ms) ──")
    
    has_vv = vv_ir_l is not None
    early_samples = int(0.05 * sr)
    
    def analyze_phase(ir, label):
        segment = ir[:early_samples]
        N = max(2048, len(segment))
        window = np.hanning(len(segment))
        padded = np.zeros(N)
        padded[:len(segment)] = segment * window
        
        spectrum = np.fft.rfft(padded)
        freqs = np.fft.rfftfreq(N, 1.0 / sr)
        
        magnitude_db = 20.0 * np.log10(np.abs(spectrum) + 1e-12)
        
        # Group delay = -d(phase)/d(omega)
        phase = np.unwrap(np.angle(spectrum))
        omega = 2.0 * np.pi * freqs
        d_omega = omega[1] - omega[0]
        group_delay_sec = -np.diff(phase) / d_omega
        # Convert to ms
        group_delay_ms = group_delay_sec * 1000.0
        
        # Smooth group delay for readability
        kernel_size = 15
        if len(group_delay_ms) > kernel_size:
            kernel = np.ones(kernel_size) / kernel_size
            group_delay_ms_smooth = np.convolve(group_delay_ms, kernel, mode='same')
        else:
            group_delay_ms_smooth = group_delay_ms
        
        mag_range = np.ptp(magnitude_db[(freqs > 100) & (freqs < 10000)])
        gd_std = np.std(group_delay_ms_smooth[(freqs[:-1] > 100) & (freqs[:-1] < 10000)])
        print(f"  {label}: magnitude range = {mag_range:.1f} dB, group delay std = {gd_std:.2f} ms")
        
        return freqs, magnitude_db, freqs[:-1], group_delay_ms_smooth, mag_range, gd_std
    
    dv_freqs, dv_mag, dv_gd_freqs, dv_gd, dv_mag_range, dv_gd_std = analyze_phase(dv_ir_l, "DuskVerb")
    
    res = {
        "dv_magnitude_range_dB": float(dv_mag_range),
        "dv_group_delay_std_ms": float(dv_gd_std),
    }
    
    if has_vv:
        vv_freqs, vv_mag, vv_gd_freqs, vv_gd, vv_mag_range, vv_gd_std = analyze_phase(vv_ir_l, "VintageVerb")
        res["vv_magnitude_range_dB"] = float(vv_mag_range)
        res["vv_group_delay_std_ms"] = float(vv_gd_std)
    
    results["phase6a"] = res
    
    if save:
        plt = setup_dark_theme()
        rows = 2 if has_vv else 1
        fig, axes = plt.subplots(rows, 2, figsize=(14, 5 * rows))
        if rows == 1:
            axes = axes.reshape(1, -1)
        
        def plot_phase(ax_mag, ax_gd, freqs, mag, gd_freqs, gd, label, color):
            mask_mag = (freqs > 20) & (freqs < 20000)
            ax_mag.semilogx(freqs[mask_mag], mag[mask_mag], color=color, linewidth=0.8)
            ax_mag.set_title(f'{label} — Magnitude Spectrum (first 50ms)')
            ax_mag.set_xlabel('Frequency (Hz)')
            ax_mag.set_ylabel('Magnitude (dB)')
            ax_mag.set_xlim(20, 20000)
            ax_mag.grid(True, alpha=0.3)
            
            mask_gd = (gd_freqs > 20) & (gd_freqs < 20000)
            ax_gd.semilogx(gd_freqs[mask_gd], gd[mask_gd], color=color, linewidth=0.8)
            ax_gd.set_title(f'{label} — Group Delay (first 50ms)')
            ax_gd.set_xlabel('Frequency (Hz)')
            ax_gd.set_ylabel('Group Delay (ms)')
            ax_gd.set_xlim(20, 20000)
            ax_gd.grid(True, alpha=0.3)
        
        plot_phase(axes[0, 0], axes[0, 1], dv_freqs, dv_mag, dv_gd_freqs, dv_gd, 'DuskVerb', DV_COLOR)
        if has_vv:
            plot_phase(axes[1, 0], axes[1, 1], vv_freqs, vv_mag, vv_gd_freqs, vv_gd, 'VintageVerb', VV_COLOR)
        
        plt.tight_layout()
        plt.savefig(os.path.join(out_dir, '16_phase_analysis.png'), dpi=150)
        plt.close()


def run_phase6b(dv_ir_l, vv_ir_l, sr, results, out_dir, save):
    """Phase 6b: Cepstrum analysis with sub-sample accuracy.

    Upsamples the IR by 8x before cepstral analysis to resolve fractional
    delay tap lengths (algorithmic FDN reverbs use mutually prime, non-integer
    delay lengths). Reports delay taps with ~0.003ms precision at 48kHz.
    """
    print("\n── Phase 6b: Cepstrum Analysis (8x upsampled) ──")

    has_vv = vv_ir_l is not None
    UPSAMPLE_FACTOR = 8
    sr_up = sr * UPSAMPLE_FACTOR

    def compute_cepstrum_upsampled(ir, upsample=UPSAMPLE_FACTOR):
        """Upsample IR then compute real cepstrum for sub-sample peak resolution."""
        # Upsample for sub-sample accuracy
        n_up = len(ir) * upsample
        ir_up = resample(ir.astype(np.float64), n_up)
        N = len(ir_up)
        spectrum = np.fft.fft(ir_up, n=N)
        log_spectrum = np.log(np.abs(spectrum) + 1e-12)
        cepstrum = np.real(np.fft.ifft(log_spectrum))
        return cepstrum, sr_up

    def find_top_peaks(cepstrum, eff_sr, n_peaks=16, min_quefrency_ms=0.5):
        """Find peaks with parabolic interpolation for sub-bin accuracy."""
        min_idx = int(min_quefrency_ms * eff_sr / 1000.0)
        max_idx = len(cepstrum) // 2
        segment = np.abs(cepstrum[min_idx:max_idx])

        peaks = []
        for i in range(1, len(segment) - 1):
            if segment[i] > segment[i - 1] and segment[i] > segment[i + 1]:
                # Parabolic interpolation for sub-bin accuracy
                alpha = segment[i - 1]
                beta = segment[i]
                gamma = segment[i + 1]
                denom = alpha - 2.0 * beta + gamma
                if abs(denom) > 1e-12:
                    p = 0.5 * (alpha - gamma) / denom
                    refined_idx = (i + min_idx) + p
                    refined_amp = beta - 0.25 * (alpha - gamma) * p
                else:
                    refined_idx = float(i + min_idx)
                    refined_amp = beta
                quefrency_ms = refined_idx / eff_sr * 1000.0
                # Convert back to original sample units for delay tap identification
                delay_samples_orig = refined_idx / UPSAMPLE_FACTOR
                peaks.append((quefrency_ms, float(refined_amp), float(delay_samples_orig)))

        peaks.sort(key=lambda x: x[1], reverse=True)
        return peaks[:n_peaks]

    dv_cepstrum, eff_sr = compute_cepstrum_upsampled(dv_ir_l)
    dv_peaks = find_top_peaks(dv_cepstrum, eff_sr)

    print("  DuskVerb top cepstral peaks (sub-sample resolution):")
    print(f"    {'Quefrency':>12s}  {'Delay (smp)':>12s}  {'Freq (Hz)':>10s}  {'Amplitude':>10s}")
    for q, amp, delay_smp in dv_peaks[:10]:
        freq_hz = 1000.0 / q if q > 0 else 0
        print(f"    {q:>10.3f} ms  {delay_smp:>11.2f}   {freq_hz:>9.0f}   {amp:>9.5f}")

    res = {
        "dv_top_peaks_ms": [float(q) for q, _, _ in dv_peaks],
        "dv_delay_taps_samples": [float(d) for _, _, d in dv_peaks],
        "upsample_factor": UPSAMPLE_FACTOR,
    }

    if has_vv:
        vv_cepstrum, _ = compute_cepstrum_upsampled(vv_ir_l)
        vv_peaks = find_top_peaks(vv_cepstrum, eff_sr)
        print("  VintageVerb top cepstral peaks (sub-sample resolution):")
        print(f"    {'Quefrency':>12s}  {'Delay (smp)':>12s}  {'Freq (Hz)':>10s}  {'Amplitude':>10s}")
        for q, amp, delay_smp in vv_peaks[:10]:
            freq_hz = 1000.0 / q if q > 0 else 0
            print(f"    {q:>10.3f} ms  {delay_smp:>11.2f}   {freq_hz:>9.0f}   {amp:>9.5f}")
        res["vv_top_peaks_ms"] = [float(q) for q, _, _ in vv_peaks]
        res["vv_delay_taps_samples"] = [float(d) for _, _, d in vv_peaks]

    results["phase6b"] = res

    if save:
        plt = setup_dark_theme()

        max_quefrency_ms = 50.0
        max_idx = int(max_quefrency_ms * eff_sr / 1000.0)
        quefrency_ms = np.arange(max_idx) / eff_sr * 1000.0

        n_sub = 2 if has_vv else 1
        fig, axes = plt.subplots(n_sub, 1, figsize=(14, 5 * n_sub))
        if n_sub == 1:
            axes = [axes]

        # DuskVerb cepstrum
        axes[0].plot(quefrency_ms, np.abs(dv_cepstrum[:max_idx]), color=DV_COLOR,
                     linewidth=0.8, label='DuskVerb (8x upsampled)', alpha=0.8)
        for q, amp, delay_smp in dv_peaks:
            if q < max_quefrency_ms:
                axes[0].annotate(f'{q:.2f}ms\n({delay_smp:.1f} smp)',
                                 xy=(q, amp), fontsize=6,
                                 color=DV_COLOR, ha='center', va='bottom')
        axes[0].set_title('DuskVerb — Cepstrum (8x upsampled, sub-sample peaks)')
        axes[0].set_xlabel('Quefrency (ms)')
        axes[0].set_ylabel('Amplitude')
        axes[0].legend(fontsize=8)
        axes[0].grid(True, alpha=0.3)

        if has_vv:
            axes[1].plot(quefrency_ms, np.abs(vv_cepstrum[:max_idx]), color=VV_COLOR,
                         linewidth=0.8, label='VintageVerb (8x upsampled)', alpha=0.8)
            for q, amp, delay_smp in vv_peaks:
                if q < max_quefrency_ms:
                    axes[1].annotate(f'{q:.2f}ms\n({delay_smp:.1f} smp)',
                                     xy=(q, amp), fontsize=6,
                                     color=VV_COLOR, ha='center', va='bottom')
            axes[1].set_title('VintageVerb — Cepstrum (8x upsampled, sub-sample peaks)')
            axes[1].set_xlabel('Quefrency (ms)')
            axes[1].set_ylabel('Amplitude')
            axes[1].legend(fontsize=8)
            axes[1].grid(True, alpha=0.3)

        plt.tight_layout()
        plt.savefig(os.path.join(out_dir, '17_cepstrum.png'), dpi=150)
        plt.close()


def run_phase6c(dv_plugin, vv_plugin, sr, results, out_dir, save):
    """Phase 6c: Linearity test — IR at multiple levels."""
    print("\n── Phase 6c: Linearity Test ──")
    
    has_vv = vv_plugin is not None
    levels = [0.001, 0.1, 1.0]  # -60dBFS, -20dBFS, 0dBFS
    level_labels = ['-60 dBFS', '-20 dBFS', '0 dBFS']
    duration = int(IR_DURATION * sr)
    
    def test_linearity(plugin, label):
        irs = []
        for lvl in levels:
            flush_plugin(plugin, sr, FLUSH_DURATION)
            impulse = np.zeros(duration, dtype=np.float32)
            impulse[0] = lvl
            ir_l, ir_r = process_stereo(plugin, impulse, sr)
            # Normalize to same peak
            peak = np.max(np.abs(ir_l))
            if peak > 1e-10:
                ir_l = ir_l / peak
            irs.append(ir_l)
        
        # Compute max deviation between normalized IRs
        min_len = min(len(ir) for ir in irs)
        ref = irs[-1][:min_len]  # 0dBFS as reference
        max_dev = 0.0
        for i, ir in enumerate(irs[:-1]):
            diff = np.abs(ir[:min_len] - ref)
            # Only look at first 2 seconds where signal is meaningful
            check_len = min(int(2.0 * sr), min_len)
            # Avoid silent regions
            active = np.abs(ref[:check_len]) > 1e-6
            if np.any(active):
                dev_db = 20.0 * np.log10(np.max(diff[:check_len][active]) + 1e-12)
                max_dev = max(max_dev, dev_db)
                print(f"  {label} deviation ({level_labels[i]} vs 0dBFS): {dev_db:.1f} dB")
        
        return irs, max_dev
    
    dv_irs, dv_max_dev = test_linearity(dv_plugin, "DuskVerb")
    res = {"dv_max_deviation_dB": float(dv_max_dev)}
    
    if has_vv:
        vv_irs, vv_max_dev = test_linearity(vv_plugin, "VintageVerb")
        res["vv_max_deviation_dB"] = float(vv_max_dev)
    
    results["phase6c"] = res
    
    if save:
        plt = setup_dark_theme()
        rows = 2 if has_vv else 1
        fig, axes = plt.subplots(rows, 1, figsize=(14, 5 * rows))
        if rows == 1:
            axes = [axes]
        
        show_samples = int(0.1 * sr)  # First 100ms
        t_ms = np.arange(show_samples) / sr * 1000.0
        
        colors_lvl = ['#66bb6a', '#ffa726', '#ef5350']
        
        def plot_linearity(ax, irs, label):
            for i, ir in enumerate(irs):
                ax.plot(t_ms, ir[:show_samples], color=colors_lvl[i],
                        linewidth=0.8, label=level_labels[i], alpha=0.7)
            ax.set_title(f'{label} — Linearity (normalized IRs at 3 levels)')
            ax.set_xlabel('Time (ms)')
            ax.set_ylabel('Amplitude (normalized)')
            ax.legend()
            ax.grid(True, alpha=0.3)
        
        plot_linearity(axes[0], dv_irs, 'DuskVerb')
        if has_vv:
            plot_linearity(axes[1], vv_irs, 'VintageVerb')
        
        plt.tight_layout()
        plt.savefig(os.path.join(out_dir, '18_linearity.png'), dpi=150)
        plt.close()


def run_phase6d(dv_plugin, vv_plugin, sr, results, out_dir, save):
    """Phase 6d: Transfer function — output/input spectrum ratio."""
    print("\n── Phase 6d: Transfer Function ──")
    
    has_vv = vv_plugin is not None
    rng = np.random.default_rng(42)
    duration_s = 5.0
    n_samples = int(duration_s * sr)
    noise = (rng.standard_normal(n_samples) * 10 ** (-12.0 / 20.0)).astype(np.float32)
    
    def compute_transfer(plugin, label):
        flush_plugin(plugin, sr, FLUSH_DURATION)
        out_l, out_r = process_stereo(plugin, noise, sr)
        out_mono = out_l

        # Compute spectra
        N = len(noise)
        in_spec = np.abs(np.fft.rfft(noise, n=N))
        out_spec = np.abs(np.fft.rfft(out_mono[:N], n=N))
        freqs = np.fft.rfftfreq(N, 1.0 / sr)

        # Transfer function ratio in dB
        ratio = out_spec / (in_spec + 1e-12)
        ratio_db = 20.0 * np.log10(ratio + 1e-12)

        # Smooth to 1/3 octave
        smoothed_freqs = []
        smoothed_db = []
        for name, fc in sorted(metrics.THIRD_OCTAVE_BANDS.items(),
                                key=lambda x: x[1]):
            lo = fc / (2 ** (1.0 / 6.0))
            hi = fc * (2 ** (1.0 / 6.0))
            mask = (freqs >= lo) & (freqs < hi)
            if np.any(mask):
                smoothed_freqs.append(fc)
                smoothed_db.append(np.mean(ratio_db[mask]))
        
        smoothed_freqs = np.array(smoothed_freqs)
        smoothed_db = np.array(smoothed_db)
        
        # Report tilt
        if len(smoothed_db) > 2:
            low_idx = np.argmin(np.abs(smoothed_freqs - 250))
            high_idx = np.argmin(np.abs(smoothed_freqs - 4000))
            tilt = smoothed_db[high_idx] - smoothed_db[low_idx]
            print(f"  {label}: spectral tilt (250Hz→4kHz) = {tilt:+.1f} dB")
        else:
            tilt = 0.0
        
        return smoothed_freqs, smoothed_db, float(tilt)
    
    dv_freqs, dv_tf, dv_tilt = compute_transfer(dv_plugin, "DuskVerb")
    res = {"dv_spectral_tilt_dB": dv_tilt}
    
    if has_vv:
        vv_freqs, vv_tf, vv_tilt = compute_transfer(vv_plugin, "VintageVerb")
        res["vv_spectral_tilt_dB"] = vv_tilt
        res["tilt_delta_dB"] = abs(dv_tilt - vv_tilt)
    
    results["phase6d"] = res
    
    if save:
        plt = setup_dark_theme()
        fig, ax = plt.subplots(figsize=(14, 6))
        
        ax.semilogx(dv_freqs, dv_tf, color=DV_COLOR, linewidth=2, label='DuskVerb')
        if has_vv:
            ax.semilogx(vv_freqs, vv_tf, color=VV_COLOR, linewidth=2, label='VintageVerb')
        
        ax.set_title('Transfer Function (1/3 Octave Smoothed)')
        ax.set_xlabel('Frequency (Hz)')
        ax.set_ylabel('Gain (dB)')
        ax.set_xlim(20, 20000)
        ax.legend()
        ax.grid(True, alpha=0.3)
        
        plt.tight_layout()
        plt.savefig(os.path.join(out_dir, '19_transfer_function.png'), dpi=150)
        plt.close()


def run_phase6e(dv_ir_l, dv_ir_r, vv_ir_l, vv_ir_r, sr, results, out_dir, save):
    """Phase 6e: Stereo width evolution — Side/Mid energy ratio over time."""
    print("\n── Phase 6e: Stereo Width Evolution ──")
    
    has_vv = vv_ir_l is not None
    window_ms = 50.0
    window_samples = int(window_ms * sr / 1000.0)
    max_time_s = 3.0
    max_samples = int(max_time_s * sr)
    
    def compute_width(ir_l, ir_r, label):
        L = ir_l[:max_samples]
        R = ir_r[:max_samples]
        mid = (L + R) / 2.0
        side = (L - R) / 2.0
        
        times = []
        ratios = []
        n_windows = len(L) // window_samples
        for i in range(n_windows):
            start = i * window_samples
            end = start + window_samples
            mid_energy = np.sum(mid[start:end] ** 2)
            side_energy = np.sum(side[start:end] ** 2)
            ratio = side_energy / (mid_energy + 1e-12)
            times.append((start + window_samples // 2) / sr)
            ratios.append(ratio)
        
        ratios = np.array(ratios)
        times = np.array(times)
        
        # Average width ratio
        avg_ratio = np.mean(ratios[ratios > 0]) if np.any(ratios > 0) else 0.0
        print(f"  {label}: avg Side/Mid energy ratio = {avg_ratio:.3f}")
        
        return times, ratios, float(avg_ratio)
    
    dv_times, dv_ratios, dv_avg = compute_width(dv_ir_l, dv_ir_r, "DuskVerb")
    res = {"dv_avg_side_mid_ratio": dv_avg}
    
    if has_vv:
        vv_times, vv_ratios, vv_avg = compute_width(vv_ir_l, vv_ir_r, "VintageVerb")
        res["vv_avg_side_mid_ratio"] = vv_avg
        res["delta_side_mid_ratio"] = abs(dv_avg - vv_avg)
    
    results["phase6e"] = res
    
    if save:
        plt = setup_dark_theme()
        fig, ax = plt.subplots(figsize=(14, 6))
        
        ax.plot(dv_times, dv_ratios, color=DV_COLOR, linewidth=1.2, label='DuskVerb')
        if has_vv:
            ax.plot(vv_times, vv_ratios, color=VV_COLOR, linewidth=1.2, label='VintageVerb')
        
        ax.set_title('Stereo Width Evolution (Side/Mid Energy Ratio)')
        ax.set_xlabel('Time (s)')
        ax.set_ylabel('Side/Mid Energy Ratio')
        ax.legend()
        ax.grid(True, alpha=0.3)
        
        plt.tight_layout()
        plt.savefig(os.path.join(out_dir, '20_stereo_width.png'), dpi=150)
        plt.close()


# ─────────────────────────────────────────────────────────────────────
# Phase 7: Perceptual & Psychoacoustic
# ─────────────────────────────────────────────────────────────────────

def run_phase7a(dv_ir_l, vv_ir_l, sr, results, out_dir, save):
    """Phase 7a: Time-varying spectral centroid."""
    print("\n── Phase 7a: Spectral Centroid Evolution ──")
    
    has_vv = vv_ir_l is not None
    window_ms = 20.0
    window_samples = int(window_ms * sr / 1000.0)
    max_time_s = 3.0
    max_samples = int(max_time_s * sr)
    
    def compute_centroid(ir, label):
        ir_seg = ir[:max_samples]
        times = []
        centroids = []
        n_windows = len(ir_seg) // window_samples
        
        for i in range(n_windows):
            start = i * window_samples
            end = start + window_samples
            frame = ir_seg[start:end]
            
            windowed = frame * np.hanning(len(frame))
            spectrum = np.abs(np.fft.rfft(windowed))
            freqs = np.fft.rfftfreq(len(frame), 1.0 / sr)
            
            total_energy = np.sum(spectrum)
            if total_energy > 1e-12:
                centroid = np.sum(freqs * spectrum) / total_energy
            else:
                centroid = 0.0
            
            times.append((start + window_samples // 2) / sr)
            centroids.append(centroid)
        
        centroids = np.array(centroids)
        times = np.array(times)
        
        # Report early vs late centroid
        early_mask = times < 0.1
        late_mask = (times > 0.5) & (times < 2.0)
        early_cent = np.mean(centroids[early_mask]) if np.any(early_mask) else 0.0
        late_cent = np.mean(centroids[late_mask]) if np.any(late_mask) else 0.0
        print(f"  {label}: early centroid = {early_cent:.0f} Hz, late centroid = {late_cent:.0f} Hz")
        
        return times, centroids, float(early_cent), float(late_cent)
    
    dv_t, dv_c, dv_early, dv_late = compute_centroid(dv_ir_l, "DuskVerb")
    res = {
        "dv_early_centroid_Hz": dv_early,
        "dv_late_centroid_Hz": dv_late,
    }
    
    if has_vv:
        vv_t, vv_c, vv_early, vv_late = compute_centroid(vv_ir_l, "VintageVerb")
        res["vv_early_centroid_Hz"] = vv_early
        res["vv_late_centroid_Hz"] = vv_late
        res["delta_early_centroid_Hz"] = abs(dv_early - vv_early)
        res["delta_late_centroid_Hz"] = abs(dv_late - vv_late)
    
    results["phase7a"] = res
    
    if save:
        plt = setup_dark_theme()
        fig, ax = plt.subplots(figsize=(14, 6))
        
        ax.plot(dv_t, dv_c, color=DV_COLOR, linewidth=1.2, label='DuskVerb')
        if has_vv:
            ax.plot(vv_t, vv_c, color=VV_COLOR, linewidth=1.2, label='VintageVerb')
        
        ax.set_title('Spectral Centroid Evolution')
        ax.set_xlabel('Time (s)')
        ax.set_ylabel('Spectral Centroid (Hz)')
        ax.legend()
        ax.grid(True, alpha=0.3)
        
        plt.tight_layout()
        plt.savefig(os.path.join(out_dir, '21_spectral_centroid.png'), dpi=150)
        plt.close()


def run_phase7b(dv_ir_l, dv_ir_r, vv_ir_l, vv_ir_r, sr, results, out_dir, save):
    """Phase 7b: IACC per octave band, early vs late."""
    print("\n── Phase 7b: IACC Per Band ──")
    
    has_vv = vv_ir_l is not None
    band_freqs = [125, 250, 500, 1000, 2000, 4000, 8000]
    early_end = int(0.08 * sr)   # 80ms
    late_start = int(0.08 * sr)
    late_end = int(2.0 * sr)
    
    def compute_iacc_band(ir_l, ir_r, start, end):
        """Compute IACC for a segment."""
        seg_l = ir_l[start:end]
        seg_r = ir_r[start:end]
        if len(seg_l) < 10:
            return 0.0
        corr = np.correlate(seg_l, seg_r, mode='full')
        norm = np.sqrt(np.sum(seg_l ** 2) * np.sum(seg_r ** 2))
        if norm < 1e-12:
            return 0.0
        # IACC = max of normalized cross-correlation within +/- 1ms
        max_lag = int(0.001 * sr)
        center = len(seg_l) - 1
        lo = max(0, center - max_lag)
        hi = min(len(corr), center + max_lag + 1)
        return float(np.max(np.abs(corr[lo:hi])) / norm)
    
    def analyze_iacc(ir_l, ir_r, label):
        early_iaccs = []
        late_iaccs = []
        for fc in band_freqs:
            bp_l = metrics._octave_bandpass(ir_l, sr, fc)
            bp_r = metrics._octave_bandpass(ir_r, sr, fc)
            
            iacc_early = compute_iacc_band(bp_l, bp_r, 0, early_end)
            iacc_late = compute_iacc_band(bp_l, bp_r, late_start, late_end)
            early_iaccs.append(iacc_early)
            late_iaccs.append(iacc_late)
        
        # LEF = 1 - IACC_early
        lefs = [1.0 - e for e in early_iaccs]
        print(f"  {label} IACC (early/late) per band:")
        for i, fc in enumerate(band_freqs):
            print(f"    {fc:5d} Hz: early={early_iaccs[i]:.3f}  late={late_iaccs[i]:.3f}  LEF={lefs[i]:.3f}")
        
        return early_iaccs, late_iaccs, lefs
    
    dv_early, dv_late, dv_lef = analyze_iacc(dv_ir_l, dv_ir_r, "DuskVerb")
    res = {
        "dv_iacc_early": [float(x) for x in dv_early],
        "dv_iacc_late": [float(x) for x in dv_late],
        "dv_lef": [float(x) for x in dv_lef],
        "bands_Hz": band_freqs,
    }
    
    if has_vv:
        vv_early, vv_late, vv_lef = analyze_iacc(vv_ir_l, vv_ir_r, "VintageVerb")
        res["vv_iacc_early"] = [float(x) for x in vv_early]
        res["vv_iacc_late"] = [float(x) for x in vv_late]
        res["vv_lef"] = [float(x) for x in vv_lef]
    
    results["phase7b"] = res
    
    if save:
        plt = setup_dark_theme()
        fig, axes = plt.subplots(1, 2, figsize=(14, 6))
        
        x = np.arange(len(band_freqs))
        width = 0.35
        band_labels = [f'{f}' for f in band_freqs]
        
        # Early IACC
        axes[0].bar(x - width / 2, dv_early, width, color=DV_COLOR, label='DuskVerb', alpha=0.8)
        if has_vv:
            axes[0].bar(x + width / 2, vv_early, width, color=VV_COLOR, label='VintageVerb', alpha=0.8)
        axes[0].set_title('IACC — Early (0–80ms)')
        axes[0].set_xlabel('Frequency (Hz)')
        axes[0].set_ylabel('IACC')
        axes[0].set_xticks(x)
        axes[0].set_xticklabels(band_labels)
        axes[0].legend()
        axes[0].grid(True, alpha=0.3, axis='y')
        axes[0].set_ylim(0, 1)
        
        # Late IACC
        axes[1].bar(x - width / 2, dv_late, width, color=DV_COLOR, label='DuskVerb', alpha=0.8)
        if has_vv:
            axes[1].bar(x + width / 2, vv_late, width, color=VV_COLOR, label='VintageVerb', alpha=0.8)
        axes[1].set_title('IACC — Late (80ms–2s)')
        axes[1].set_xlabel('Frequency (Hz)')
        axes[1].set_ylabel('IACC')
        axes[1].set_xticks(x)
        axes[1].set_xticklabels(band_labels)
        axes[1].legend()
        axes[1].grid(True, alpha=0.3, axis='y')
        axes[1].set_ylim(0, 1)
        
        plt.tight_layout()
        plt.savefig(os.path.join(out_dir, '22_iacc_per_band.png'), dpi=150)
        plt.close()


def run_phase7c(dv_ir_l, vv_ir_l, sr, results, out_dir, save):
    """Phase 7c: Decay shape — single-slope vs double-slope EDC fit."""
    print("\n── Phase 7c: Decay Shape Analysis ──")
    
    has_vv = vv_ir_l is not None
    
    def analyze_decay_shape(ir, label):
        # Compute EDC using Schroeder integration
        edc = np.cumsum(ir[::-1] ** 2)[::-1]
        edc_db = 10.0 * np.log10(edc / (edc[0] + 1e-12) + 1e-12)
        
        # Trim to useful range (-60dB or 3 seconds)
        max_samples = min(len(edc_db), int(3.0 * sr))
        valid = np.where(edc_db[:max_samples] > -60)[0]
        if len(valid) < 10:
            valid = np.arange(min(max_samples, len(edc_db)))
        end_idx = valid[-1] + 1
        
        t = np.arange(end_idx) / sr
        edc_seg = edc_db[:end_idx]
        
        # Single-slope fit
        coeffs_single = np.polyfit(t, edc_seg, 1)
        single_fit = np.polyval(coeffs_single, t)
        single_residual = np.sqrt(np.mean((edc_seg - single_fit) ** 2))
        
        # Double-slope fit: try different knee points
        best_double_residual = single_residual
        best_knee_idx = len(t) // 2
        best_coeffs = (coeffs_single, coeffs_single)
        
        for knee_frac in np.linspace(0.15, 0.7, 20):
            knee_idx = int(knee_frac * len(t))
            if knee_idx < 10 or knee_idx > len(t) - 10:
                continue
            
            c1 = np.polyfit(t[:knee_idx], edc_seg[:knee_idx], 1)
            c2 = np.polyfit(t[knee_idx:], edc_seg[knee_idx:], 1)
            
            fit1 = np.polyval(c1, t[:knee_idx])
            fit2 = np.polyval(c2, t[knee_idx:])
            double_fit = np.concatenate([fit1, fit2])
            double_residual = np.sqrt(np.mean((edc_seg - double_fit) ** 2))
            
            if double_residual < best_double_residual:
                best_double_residual = double_residual
                best_knee_idx = knee_idx
                best_coeffs = (c1, c2)
        
        knee_time = t[best_knee_idx]
        improvement = single_residual - best_double_residual
        is_double = improvement > 3.0
        
        slope1 = best_coeffs[0][0]
        slope2 = best_coeffs[1][0]
        
        print(f"  {label}:")
        print(f"    Single-slope residual: {single_residual:.2f} dB")
        print(f"    Double-slope residual: {best_double_residual:.2f} dB (improvement: {improvement:.2f} dB)")
        if is_double:
            print(f"    ** Double-slope is significantly better **")
            print(f"    Knee time: {knee_time*1000:.0f} ms")
            print(f"    Slope 1: {slope1:.1f} dB/s, Slope 2: {slope2:.1f} dB/s")
        else:
            print(f"    Single-slope model is adequate")
            print(f"    Slope: {coeffs_single[0]:.1f} dB/s")
        
        return {
            "t": t,
            "edc_db": edc_seg,
            "single_fit": single_fit,
            "double_fit_knee_idx": best_knee_idx,
            "double_coeffs": best_coeffs,
            "single_residual": float(single_residual),
            "double_residual": float(best_double_residual),
            "improvement_dB": float(improvement),
            "is_double_slope": bool(is_double),
            "knee_time_ms": float(knee_time * 1000),
            "slope1_dBs": float(slope1),
            "slope2_dBs": float(slope2),
            "single_slope_dBs": float(coeffs_single[0]),
        }
    
    dv_res = analyze_decay_shape(dv_ir_l, "DuskVerb")
    res = {
        "dv_single_residual_dB": dv_res["single_residual"],
        "dv_double_residual_dB": dv_res["double_residual"],
        "dv_improvement_dB": dv_res["improvement_dB"],
        "dv_is_double_slope": dv_res["is_double_slope"],
        "dv_knee_time_ms": dv_res["knee_time_ms"],
        "dv_slope1_dBs": dv_res["slope1_dBs"],
        "dv_slope2_dBs": dv_res["slope2_dBs"],
        "dv_single_slope_dBs": dv_res["single_slope_dBs"],
    }
    
    vv_res = None
    if has_vv:
        vv_res = analyze_decay_shape(vv_ir_l, "VintageVerb")
        res["vv_single_residual_dB"] = vv_res["single_residual"]
        res["vv_double_residual_dB"] = vv_res["double_residual"]
        res["vv_improvement_dB"] = vv_res["improvement_dB"]
        res["vv_is_double_slope"] = vv_res["is_double_slope"]
        res["vv_knee_time_ms"] = vv_res["knee_time_ms"]
        res["vv_slope1_dBs"] = vv_res["slope1_dBs"]
        res["vv_slope2_dBs"] = vv_res["slope2_dBs"]
        res["vv_single_slope_dBs"] = vv_res["single_slope_dBs"]
    
    results["phase7c"] = res
    
    if save:
        plt = setup_dark_theme()
        cols = 2 if has_vv else 1
        fig, axes = plt.subplots(1, cols, figsize=(7 * cols, 6))
        if cols == 1:
            axes = [axes]
        
        def plot_decay(ax, data, label, color):
            t = data["t"]
            ax.plot(t, data["edc_db"], color=color, linewidth=1.5, label='EDC')
            ax.plot(t, data["single_fit"], '--', color='#aaaaaa', linewidth=1, label='Single-slope fit')
            
            ki = data["double_fit_knee_idx"]
            c1, c2 = data["double_coeffs"]
            double_fit = np.concatenate([
                np.polyval(c1, t[:ki]),
                np.polyval(c2, t[ki:])
            ])
            ax.plot(t, double_fit, ':', color='#ffeb3b', linewidth=1.2, label='Double-slope fit')
            ax.axvline(t[ki], color='#ffeb3b', alpha=0.5, linestyle='--', linewidth=0.8)
            
            ax.set_title(f'{label} — Decay Shape')
            ax.set_xlabel('Time (s)')
            ax.set_ylabel('Energy (dB)')
            ax.legend(fontsize=8)
            ax.grid(True, alpha=0.3)
        
        plot_decay(axes[0], dv_res, 'DuskVerb', DV_COLOR)
        if has_vv:
            plot_decay(axes[1], vv_res, 'VintageVerb', VV_COLOR)
        
        plt.tight_layout()
        plt.savefig(os.path.join(out_dir, '23_decay_shape.png'), dpi=150)
        plt.close()


def run_phase7d(dv_ir_l, vv_ir_l, sr, results, out_dir, save):
    """Phase 7d: Echo density per band — kurtosis of late tail."""
    print("\n── Phase 7d: Echo Density Per Band (Kurtosis) ──")
    
    has_vv = vv_ir_l is not None
    band_freqs = [125, 250, 500, 1000, 2000, 4000]
    late_start = int(0.5 * sr)
    late_end = int(2.0 * sr)
    
    def compute_kurtosis_per_band(ir, label):
        kurtosis_vals = []
        for fc in band_freqs:
            bp = metrics._octave_bandpass(ir, sr, fc)
            segment = bp[late_start:late_end]
            if len(segment) < 100:
                kurtosis_vals.append(0.0)
                continue
            k = float(scipy_kurtosis(segment, fisher=True))
            kurtosis_vals.append(k)
        
        print(f"  {label} kurtosis per band (0 = Gaussian/diffuse):")
        for i, fc in enumerate(band_freqs):
            status = "diffuse" if abs(kurtosis_vals[i]) < 1.0 else "non-Gaussian"
            print(f"    {fc:5d} Hz: {kurtosis_vals[i]:+.2f}  ({status})")
        
        return kurtosis_vals
    
    dv_kurt = compute_kurtosis_per_band(dv_ir_l, "DuskVerb")
    res = {
        "bands_Hz": band_freqs,
        "dv_kurtosis": [float(k) for k in dv_kurt],
    }
    
    if has_vv:
        vv_kurt = compute_kurtosis_per_band(vv_ir_l, "VintageVerb")
        res["vv_kurtosis"] = [float(k) for k in vv_kurt]
    
    results["phase7d"] = res
    
    if save:
        plt = setup_dark_theme()
        fig, ax = plt.subplots(figsize=(10, 6))
        
        x = np.arange(len(band_freqs))
        width = 0.35
        band_labels = [f'{f}' for f in band_freqs]
        
        ax.bar(x - width / 2, dv_kurt, width, color=DV_COLOR, label='DuskVerb', alpha=0.8)
        if has_vv:
            ax.bar(x + width / 2, vv_kurt, width, color=VV_COLOR, label='VintageVerb', alpha=0.8)
        
        ax.axhline(y=0, color='white', linestyle='--', alpha=0.3, linewidth=0.8)
        ax.set_title('Echo Density Per Band — Late Tail Kurtosis (500ms–2s)')
        ax.set_xlabel('Frequency (Hz)')
        ax.set_ylabel('Excess Kurtosis')
        ax.set_xticks(x)
        ax.set_xticklabels(band_labels)
        ax.legend()
        ax.grid(True, alpha=0.3, axis='y')
        
        plt.tight_layout()
        plt.savefig(os.path.join(out_dir, '24_echo_density_per_band.png'), dpi=150)
        plt.close()


# ─────────────────────────────────────────────────────────────────────
# Phase 8a: Modulation LFO Extraction
# ─────────────────────────────────────────────────────────────────────

def run_phase8a(dv_plugin, vv_plugin, sr, results, out_dir, save):
    """Phase 8a: Extract modulation LFO waveforms from sustained tone analysis.

    Passes a high-frequency sine wave (10 kHz) through the 100% wet reverb,
    extracts the analytic signal via Hilbert transform, unwraps the
    instantaneous phase, and differentiates to reveal the exact LFO
    waveforms, rates, and depths driving the internal delay line modulation.
    """
    print("\n── Phase 8a: Modulation LFO Extraction ──")

    has_vv = vv_plugin is not None
    carrier_freq = 10000.0  # 10 kHz carrier — well above musical content
    duration_s = 8.0        # need several LFO cycles
    n_samples = int(duration_s * sr)
    t = np.arange(n_samples, dtype=np.float64) / sr
    amplitude = 10 ** (-12 / 20)  # -12 dBFS

    tone = (amplitude * np.sin(2.0 * np.pi * carrier_freq * t)).astype(np.float32)

    def extract_lfo(plugin, label, color):
        """Extract instantaneous frequency deviation from carrier."""
        flush_plugin(plugin, sr, FLUSH_DURATION)
        out_l, _ = process_stereo(plugin, tone, sr)
        out_64 = out_l.astype(np.float64)

        # Bandpass around carrier to isolate it from reverb coloration
        bw = 500  # ±500 Hz around carrier
        lo_bp = max(carrier_freq - bw, 100)
        hi_bp = min(carrier_freq + bw, sr / 2 - 100)
        sos_bp = butter(6, [lo_bp, hi_bp], btype='bandpass', fs=sr, output='sos')
        filtered = sosfiltfilt(sos_bp, out_64)

        # Analytic signal via Hilbert transform
        analytic = hilbert(filtered)
        inst_phase = np.unwrap(np.angle(analytic))

        # Instantaneous frequency = d(phase)/dt / (2*pi)
        inst_freq = np.diff(inst_phase) * sr / (2.0 * np.pi)

        # Frequency deviation from carrier
        freq_deviation = inst_freq - carrier_freq

        # Smooth with 5ms window to reduce noise
        smooth_win = int(0.005 * sr)
        if smooth_win > 1 and len(freq_deviation) > smooth_win:
            kernel = np.ones(smooth_win) / smooth_win
            freq_deviation_smooth = np.convolve(freq_deviation, kernel, mode='same')
        else:
            freq_deviation_smooth = freq_deviation

        # Skip first 500ms (buildup transient)
        skip = int(0.5 * sr)
        analysis_region = freq_deviation_smooth[skip:]
        t_analysis = np.arange(len(analysis_region)) / sr

        # Estimate LFO rate via autocorrelation of the deviation signal
        if len(analysis_region) > sr:
            ac = np.correlate(analysis_region[:int(4 * sr)],
                              analysis_region[:int(4 * sr)], mode='full')
            ac = ac[len(ac) // 2:]
            ac = ac / (ac[0] + 1e-30)

            # Find first peak after 0 lag (minimum LFO period ~50ms = 20Hz)
            min_lag = int(0.05 * sr)
            max_lag = min(int(4.0 * sr), len(ac) - 1)  # up to 4s period
            ac_search = ac[min_lag:max_lag]
            lfo_period_samples = 0
            lfo_rate = 0.0
            for i in range(1, len(ac_search) - 1):
                if ac_search[i] > ac_search[i - 1] and ac_search[i] > ac_search[i + 1]:
                    if ac_search[i] > 0.3:  # require decent correlation
                        lfo_period_samples = i + min_lag
                        lfo_rate = sr / lfo_period_samples
                        break
        else:
            lfo_rate = 0.0
            lfo_period_samples = 0

        # LFO depth: peak-to-peak frequency deviation in Hz
        # Use percentile to ignore outliers
        dev_p5 = np.percentile(analysis_region, 5)
        dev_p95 = np.percentile(analysis_region, 95)
        lfo_depth_hz = dev_p95 - dev_p5
        lfo_depth_cents = lfo_depth_hz / carrier_freq * 1200.0 / np.log(2) if carrier_freq > 0 else 0

        print(f"  {label}:")
        print(f"    LFO rate:  {lfo_rate:.3f} Hz ({1000.0/lfo_rate:.0f} ms period)" if lfo_rate > 0
              else f"    LFO rate:  not detected (no periodic modulation)")
        print(f"    LFO depth: ±{lfo_depth_hz/2:.1f} Hz ({lfo_depth_cents:.1f} cents p-p)")

        return {
            "t_analysis": t_analysis,
            "freq_deviation": analysis_region,
            "lfo_rate_hz": float(lfo_rate),
            "lfo_period_ms": float(1000.0 / lfo_rate) if lfo_rate > 0 else None,
            "lfo_depth_hz_pp": float(lfo_depth_hz),
            "lfo_depth_cents_pp": float(lfo_depth_cents),
        }

    dv_lfo = extract_lfo(dv_plugin, "DuskVerb", DV_COLOR)
    res = {
        "dv_lfo_rate_hz": dv_lfo["lfo_rate_hz"],
        "dv_lfo_depth_hz_pp": dv_lfo["lfo_depth_hz_pp"],
        "dv_lfo_depth_cents_pp": dv_lfo["lfo_depth_cents_pp"],
    }

    vv_lfo = None
    if has_vv:
        vv_lfo = extract_lfo(vv_plugin, "VintageVerb", VV_COLOR)
        res["vv_lfo_rate_hz"] = vv_lfo["lfo_rate_hz"]
        res["vv_lfo_depth_hz_pp"] = vv_lfo["lfo_depth_hz_pp"]
        res["vv_lfo_depth_cents_pp"] = vv_lfo["lfo_depth_cents_pp"]

    results["phase8a"] = res

    if save:
        plt = setup_dark_theme()

        n_sub = 2 if has_vv else 1
        fig, axes = plt.subplots(n_sub, 2, figsize=(16, 5 * n_sub))
        if n_sub == 1:
            axes = axes.reshape(1, -1)

        def plot_lfo(row, lfo_data, label, color):
            t_a = lfo_data["t_analysis"]
            dev = lfo_data["freq_deviation"]

            # Time-domain LFO waveform (show 2 seconds)
            show_s = min(2.0, t_a[-1]) if len(t_a) > 0 else 2.0
            show_n = int(show_s * sr)
            axes[row, 0].plot(t_a[:show_n] * 1000, dev[:show_n],
                              color=color, linewidth=0.6)
            axes[row, 0].set_title(f'{label} — LFO Waveform (freq deviation)')
            axes[row, 0].set_xlabel('Time (ms)')
            axes[row, 0].set_ylabel('Frequency Deviation (Hz)')
            axes[row, 0].grid(True, alpha=0.3)

            # Spectrum of the deviation signal to identify LFO harmonics
            if len(dev) > 1024:
                dev_windowed = dev[:int(4 * sr)] * np.hanning(min(len(dev), int(4 * sr)))
                fft_dev = np.abs(np.fft.rfft(dev_windowed))
                fft_dev_db = 20 * np.log10(fft_dev / (np.max(fft_dev) + 1e-30) + 1e-30)
                fft_freqs = np.fft.rfftfreq(len(dev_windowed), 1.0 / sr)
                # Show 0-10 Hz (LFO range)
                mask_lfo = fft_freqs <= 10
                axes[row, 1].plot(fft_freqs[mask_lfo], fft_dev_db[mask_lfo],
                                  color=color, linewidth=1.0)
                axes[row, 1].set_title(f'{label} — LFO Spectrum (0–10 Hz)')
                axes[row, 1].set_xlabel('Frequency (Hz)')
                axes[row, 1].set_ylabel('Magnitude (dB)')
                axes[row, 1].grid(True, alpha=0.3)
                axes[row, 1].set_ylim(-60, 5)

        plot_lfo(0, dv_lfo, 'DuskVerb', DV_COLOR)
        if has_vv:
            plot_lfo(1, vv_lfo, 'VintageVerb', VV_COLOR)

        fig.suptitle('25 — Modulation LFO Extraction (10kHz carrier)', fontsize=14)
        fig.tight_layout()
        fig.savefig(os.path.join(out_dir, '25_lfo_extraction.png'))
        plt.close(fig)
        print("  Saved 25_lfo_extraction.png")


# ─────────────────────────────────────────────────────────────────────
# Phase 8b: All-Pass Filter Smearing & Dispersion
# ─────────────────────────────────────────────────────────────────────

def run_phase8b(dv_ir_l, vv_ir_l, sr, results, out_dir, save):
    """Phase 8b: Analyze frequency-dependent group delay in the early IR.

    Examines the first 10–50ms to reveal all-pass filter (APF) dispersion.
    APFs cause high frequencies to be delayed relative to low frequencies,
    creating the characteristic "chirp" or smearing of transients.
    The group delay profile helps estimate APF count and coefficients.
    """
    print("\n── Phase 8b: All-Pass Filter Smearing & Dispersion ──")

    has_vv = vv_ir_l is not None

    # Analyze multiple early windows to see how dispersion evolves
    windows_ms = [(0, 10), (0, 20), (0, 50), (10, 50)]

    def analyze_apf_dispersion(ir, label):
        """Compute frequency-dependent group delay for early IR windows."""
        window_results = []

        for start_ms, end_ms in windows_ms:
            s0 = int(start_ms * sr / 1000)
            s1 = min(int(end_ms * sr / 1000), len(ir))
            segment = ir[s0:s1].astype(np.float64)

            N = max(4096, len(segment))
            window = np.hanning(len(segment))
            padded = np.zeros(N)
            padded[:len(segment)] = segment * window

            spectrum = np.fft.rfft(padded)
            freqs = np.fft.rfftfreq(N, 1.0 / sr)

            # Group delay = -d(unwrapped_phase)/d(omega)
            phase = np.unwrap(np.angle(spectrum))
            d_omega = 2.0 * np.pi * (freqs[1] - freqs[0]) if len(freqs) > 1 else 1.0
            group_delay_raw = -np.diff(phase) / d_omega  # in seconds
            group_delay_ms = group_delay_raw * 1000.0

            # Smooth for readability
            kernel_size = 31
            if len(group_delay_ms) > kernel_size:
                kernel = np.ones(kernel_size) / kernel_size
                gd_smooth = np.convolve(group_delay_ms, kernel, mode='same')
            else:
                gd_smooth = group_delay_ms

            gd_freqs = freqs[:-1]

            # Measure dispersion: group delay at 8kHz minus group delay at 200Hz
            mask_lo = (gd_freqs >= 150) & (gd_freqs <= 300)
            mask_hi = (gd_freqs >= 6000) & (gd_freqs <= 10000)
            gd_lo = np.median(gd_smooth[mask_lo]) if np.any(mask_lo) else 0
            gd_hi = np.median(gd_smooth[mask_hi]) if np.any(mask_hi) else 0
            dispersion_ms = gd_hi - gd_lo

            window_results.append({
                "window": f"{start_ms}-{end_ms}ms",
                "gd_freqs": gd_freqs,
                "gd_smooth": gd_smooth,
                "gd_lo_ms": float(gd_lo),
                "gd_hi_ms": float(gd_hi),
                "dispersion_ms": float(dispersion_ms),
            })

        # Estimate APF count from total dispersion in the 0-50ms window
        # A single first-order APF with coeff ~0.5 at 48kHz adds ~0.02ms dispersion
        # Nested APFs compound: N stages ≈ total_dispersion / per_stage_dispersion
        total_disp = abs(window_results[-1]["dispersion_ms"])  # 10-50ms window
        estimated_apf_stages = int(total_disp / 0.02) if total_disp > 0.01 else 0

        print(f"  {label}:")
        for wr in window_results:
            print(f"    {wr['window']:>10s}: GD(200Hz)={wr['gd_lo_ms']:+.3f}ms  "
                  f"GD(8kHz)={wr['gd_hi_ms']:+.3f}ms  "
                  f"dispersion={wr['dispersion_ms']:+.3f}ms")
        print(f"    Estimated APF stages: ~{estimated_apf_stages}")

        return window_results, estimated_apf_stages

    dv_wr, dv_apf_count = analyze_apf_dispersion(dv_ir_l, "DuskVerb")
    res = {
        "dv_dispersion_ms": [wr["dispersion_ms"] for wr in dv_wr],
        "dv_estimated_apf_stages": dv_apf_count,
        "windows_ms": [f"{s}-{e}ms" for s, e in windows_ms],
    }

    vv_wr = None
    if has_vv:
        vv_wr, vv_apf_count = analyze_apf_dispersion(vv_ir_l, "VintageVerb")
        res["vv_dispersion_ms"] = [wr["dispersion_ms"] for wr in vv_wr]
        res["vv_estimated_apf_stages"] = vv_apf_count

    results["phase8b"] = res

    if save:
        plt = setup_dark_theme()

        n_windows = len(windows_ms)
        n_sub = 2 if has_vv else 1
        fig, axes = plt.subplots(n_sub, n_windows, figsize=(5 * n_windows, 5 * n_sub))
        if n_sub == 1:
            axes = axes.reshape(1, -1)

        def plot_gd(row, wr_list, label, color):
            for col, wr in enumerate(wr_list):
                ax = axes[row, col]
                mask = (wr["gd_freqs"] > 50) & (wr["gd_freqs"] < 20000)
                ax.semilogx(wr["gd_freqs"][mask], wr["gd_smooth"][mask],
                            color=color, linewidth=0.8)
                ax.set_title(f'{label} [{wr["window"]}]\ndisp={wr["dispersion_ms"]:+.3f}ms',
                             fontsize=9)
                ax.set_xlabel('Frequency (Hz)')
                ax.set_ylabel('Group Delay (ms)')
                ax.set_xlim(50, 20000)
                ax.grid(True, alpha=0.3)

        plot_gd(0, dv_wr, 'DuskVerb', DV_COLOR)
        if has_vv:
            plot_gd(1, vv_wr, 'VintageVerb', VV_COLOR)

        fig.suptitle('26 — All-Pass Dispersion (frequency-dependent group delay)', fontsize=14)
        fig.tight_layout()
        fig.savefig(os.path.join(out_dir, '26_apf_dispersion.png'))
        plt.close(fig)
        print("  Saved 26_apf_dispersion.png")


# ─────────────────────────────────────────────────────────────────────
# Phase 8c: Damping Topology Test (Loop vs. Post-EQ)
# ─────────────────────────────────────────────────────────────────────

def run_phase8c(dv_plugin, vv_plugin, pairing, sr, results, out_dir, save):
    """Phase 8c: Determine if HF damping is inside the FDN loop or post-EQ.

    Captures IRs at two different decay times (short ~1s and long ~5s) and
    compares the HF roll-off slope over time. If the HF slope steepens
    dramatically at longer decay times, the damping filter is inside the
    feedback loop (each recirculation compounds the filtering). If the
    slope stays constant, it's a static post-EQ.
    """
    print("\n── Phase 8c: Damping Topology (Loop vs Post-EQ) ──")

    has_vv = vv_plugin is not None

    # We need to temporarily change decay time, so save/restore params
    short_decay = 0.15   # normalized value for ~1s RT60
    long_decay = 0.60    # normalized value for ~5s RT60

    def measure_hf_slope_over_time(plugin, apply_fn, config, decay_val, label):
        """Capture IR at a given decay and measure HF slope in time windows."""
        mod_config = dict(config)

        # For DuskVerb, decay_time is in seconds; for VV, it's 0-1 normalized
        if "decay_time" in mod_config:
            # DuskVerb: set absolute decay time
            mod_config["decay_time"] = 1.0 if decay_val == short_decay else 5.0
        elif "_decay" in mod_config:
            # VintageVerb: set normalized decay
            mod_config["_decay"] = decay_val

        apply_fn(plugin, mod_config)
        ir_l, ir_r = capture_ir(plugin, sr, duration=6.0)

        # Measure HF roll-off slope in multiple time windows
        analysis_windows = [
            (0.05, 0.15),    # early (50-150ms)
            (0.2, 0.5),      # mid (200-500ms)
            (0.5, 1.5),      # late (500ms-1.5s)
            (1.5, 3.0),      # very late (1.5-3s)
        ]

        slopes = []
        for t_start, t_end in analysis_windows:
            s0 = int(t_start * sr)
            s1 = min(int(t_end * sr), len(ir_l))
            if s1 - s0 < 256:
                slopes.append({"window": f"{t_start}-{t_end}s", "slope_dB_oct": 0.0})
                continue

            segment = ir_l[s0:s1].astype(np.float64)
            N = max(4096, len(segment))
            windowed = np.zeros(N)
            windowed[:len(segment)] = segment * np.hanning(len(segment))
            spectrum = np.abs(np.fft.rfft(windowed, n=N))
            spectrum_db = 20 * np.log10(spectrum + 1e-30)
            freqs = np.fft.rfftfreq(N, 1.0 / sr)

            # Measure 1/3 octave band levels from 500Hz to 10kHz
            band_centers = [500, 1000, 2000, 4000, 8000]
            band_levels = []
            for fc in band_centers:
                lo = fc / (2 ** (1 / 6))
                hi = fc * (2 ** (1 / 6))
                mask = (freqs >= lo) & (freqs < hi)
                if np.any(mask):
                    band_levels.append(np.mean(spectrum_db[mask]))
                else:
                    band_levels.append(-100)

            # Fit linear slope to log-frequency vs dB (slope in dB/octave)
            log_fc = np.log2(np.array(band_centers, dtype=np.float64))
            levels = np.array(band_levels)
            if len(log_fc) > 2:
                coeffs = np.polyfit(log_fc, levels, 1)
                slope_db_oct = float(coeffs[0])
            else:
                slope_db_oct = 0.0

            slopes.append({
                "window": f"{t_start}-{t_end}s",
                "slope_dB_oct": slope_db_oct,
                "band_centers": band_centers,
                "band_levels": [float(l) for l in band_levels],
            })

        return ir_l, slopes

    # Measure DuskVerb at short and long decay
    dv_config = pairing["duskverb"]
    print("  DuskVerb short decay (~1s):")
    dv_ir_short, dv_slopes_short = measure_hf_slope_over_time(
        dv_plugin, apply_duskverb_params, dv_config, short_decay, "DV short")
    print("  DuskVerb long decay (~5s):")
    dv_ir_long, dv_slopes_long = measure_hf_slope_over_time(
        dv_plugin, apply_duskverb_params, dv_config, long_decay, "DV long")

    # Restore original config
    apply_duskverb_params(dv_plugin, dv_config)

    # Compare slope steepening
    print("\n  DuskVerb HF slope (dB/octave):")
    print(f"    {'Window':>14s}  {'Short RT60':>12s}  {'Long RT60':>12s}  {'Delta':>10s}")
    dv_slope_deltas = []
    for ss, sl in zip(dv_slopes_short, dv_slopes_long):
        delta = sl["slope_dB_oct"] - ss["slope_dB_oct"]
        dv_slope_deltas.append(delta)
        print(f"    {ss['window']:>14s}  {ss['slope_dB_oct']:>+10.1f}  "
              f"{sl['slope_dB_oct']:>+10.1f}  {delta:>+8.1f}")

    avg_steepening = np.mean(dv_slope_deltas[1:])  # skip early window
    dv_topology = "in-loop" if avg_steepening < -3.0 else "post-EQ"
    print(f"    Average late steepening: {avg_steepening:+.1f} dB/oct → {dv_topology}")

    res = {
        "dv_slopes_short": [s["slope_dB_oct"] for s in dv_slopes_short],
        "dv_slopes_long": [s["slope_dB_oct"] for s in dv_slopes_long],
        "dv_slope_deltas": dv_slope_deltas,
        "dv_avg_steepening": float(avg_steepening),
        "dv_topology": dv_topology,
    }

    vv_slopes_short = vv_slopes_long = None
    if has_vv:
        vv_config = pairing["reference"]
        print("\n  VintageVerb short decay:")
        _, vv_slopes_short = measure_hf_slope_over_time(
            vv_plugin, apply_reference_params, vv_config, short_decay, "VV short")
        print("  VintageVerb long decay:")
        _, vv_slopes_long = measure_hf_slope_over_time(
            vv_plugin, apply_reference_params, vv_config, long_decay, "VV long")
        # Restore
        apply_reference_params(vv_plugin, vv_config)

        print("\n  VintageVerb HF slope (dB/octave):")
        print(f"    {'Window':>14s}  {'Short RT60':>12s}  {'Long RT60':>12s}  {'Delta':>10s}")
        vv_slope_deltas = []
        for ss, sl in zip(vv_slopes_short, vv_slopes_long):
            delta = sl["slope_dB_oct"] - ss["slope_dB_oct"]
            vv_slope_deltas.append(delta)
            print(f"    {ss['window']:>14s}  {ss['slope_dB_oct']:>+10.1f}  "
                  f"{sl['slope_dB_oct']:>+10.1f}  {delta:>+8.1f}")

        vv_avg = np.mean(vv_slope_deltas[1:])
        vv_topology = "in-loop" if vv_avg < -3.0 else "post-EQ"
        print(f"    Average late steepening: {vv_avg:+.1f} dB/oct → {vv_topology}")
        res["vv_slopes_short"] = [s["slope_dB_oct"] for s in vv_slopes_short]
        res["vv_slopes_long"] = [s["slope_dB_oct"] for s in vv_slopes_long]
        res["vv_slope_deltas"] = vv_slope_deltas
        res["vv_avg_steepening"] = float(vv_avg)
        res["vv_topology"] = vv_topology

    results["phase8c"] = res

    if save:
        plt = setup_dark_theme()

        fig, axes = plt.subplots(1, 2, figsize=(14, 6))
        window_labels = [s["window"] for s in dv_slopes_short]
        x = np.arange(len(window_labels))
        width = 0.35

        # Short vs Long slopes for DuskVerb
        axes[0].bar(x - width / 2,
                     [s["slope_dB_oct"] for s in dv_slopes_short],
                     width, color=DV_COLOR, alpha=0.7, label='DV short (~1s)')
        axes[0].bar(x + width / 2,
                     [s["slope_dB_oct"] for s in dv_slopes_long],
                     width, color=DV_COLOR, alpha=1.0, label='DV long (~5s)')
        if has_vv and vv_slopes_short:
            axes[0].bar(x - width / 2,
                         [s["slope_dB_oct"] for s in vv_slopes_short],
                         width, color=VV_COLOR, alpha=0.4, label='VV short')
            axes[0].bar(x + width / 2,
                         [s["slope_dB_oct"] for s in vv_slopes_long],
                         width, color=VV_COLOR, alpha=0.7, label='VV long')
        axes[0].set_xticks(x)
        axes[0].set_xticklabels(window_labels, fontsize=8)
        axes[0].set_title('HF Slope (dB/oct) — Short vs Long Decay')
        axes[0].set_ylabel('Slope (dB/octave)')
        axes[0].legend(fontsize=7)
        axes[0].grid(True, alpha=0.3, axis='y')

        # Delta (steepening)
        axes[1].bar(x - width / 2, dv_slope_deltas, width,
                     color=DV_COLOR, alpha=0.85, label=f'DuskVerb ({dv_topology})')
        if has_vv and vv_slopes_short:
            axes[1].bar(x + width / 2, vv_slope_deltas, width,
                         color=VV_COLOR, alpha=0.85, label=f'VintageVerb ({vv_topology})')
        axes[1].axhline(-3.0, color='#ffeb3b', linestyle='--', alpha=0.6,
                         label='In-loop threshold')
        axes[1].set_xticks(x)
        axes[1].set_xticklabels(window_labels, fontsize=8)
        axes[1].set_title('HF Slope Steepening (Long − Short)')
        axes[1].set_ylabel('Δ Slope (dB/octave)')
        axes[1].legend(fontsize=7)
        axes[1].grid(True, alpha=0.3, axis='y')

        fig.suptitle('27 — Damping Topology: Loop vs Post-EQ', fontsize=14)
        fig.tight_layout()
        fig.savefig(os.path.join(out_dir, '27_damping_topology.png'))
        plt.close(fig)
        print("  Saved 27_damping_topology.png")


# ─────────────────────────────────────────────────────────────────────
# Phase 9: Automated Parameter Sweeping ("Knob Math")
# ─────────────────────────────────────────────────────────────────────

def run_phase9(vv_plugin, pairing, sr, results, out_dir, save):
    """Phase 9: Sweep reference plugin parameters and measure response curves.

    Steps a parameter (e.g., Decay, Size) from 0.0→1.0 and measures a core
    metric at each step. Reveals whether the knob scaling is linear,
    exponential, or logarithmic — essential for mapping DuskVerb's parameters.
    """
    print("\n── Phase 9: Parameter Sweep ('Knob Math') ──")

    if vv_plugin is None:
        print("  Skipped: no reference plugin loaded (--compare required)")
        results["phase9"] = {"skipped": True}
        return

    vv_config = pairing["reference"]

    # Define parameter sweeps: (semantic_key, display_name, metric_fn_name, steps)
    sweep_defs = [
        ("_decay", "Decay", "rt60", np.linspace(0.05, 0.95, 10)),
        ("_size", "Size", "modal_density", np.linspace(0.1, 0.9, 9)),
    ]

    phase9 = {}

    for sem_key, display_name, metric_type, steps in sweep_defs:
        if sem_key not in vv_config:
            print(f"  Skipping {display_name}: {sem_key} not in reference config")
            continue

        print(f"\n  Sweeping {display_name} ({sem_key}): {len(steps)} steps")
        original_val = vv_config[sem_key]

        sweep_values = []
        sweep_metrics = []

        for val in steps:
            # Apply modified config
            mod_config = dict(vv_config)
            mod_config[sem_key] = float(val)
            apply_reference_params(vv_plugin, mod_config)

            # Capture IR
            ir_l, ir_r = capture_ir(vv_plugin, sr, duration=6.0)

            # Measure metric
            if metric_type == "rt60":
                rt60_bands = metrics.measure_rt60_per_band(ir_l, sr)
                # Use 500Hz + 1kHz average as the canonical RT60
                rt_500 = rt60_bands.get("500 Hz")
                rt_1k = rt60_bands.get("1 kHz")
                vals_ok = [v for v in [rt_500, rt_1k] if v is not None]
                metric_val = float(np.mean(vals_ok)) if vals_ok else 0.0
                metric_unit = "s"

            elif metric_type == "modal_density":
                # Modal density: count spectral peaks in late tail
                late_lo = int(0.3 * sr)
                late_hi = min(int(1.5 * sr), len(ir_l))
                if late_hi - late_lo > 256:
                    segment = ir_l[late_lo:late_hi]
                    window = np.hanning(len(segment))
                    spectrum = np.abs(np.fft.rfft(segment * window))
                    spectrum_db = 20 * np.log10(spectrum + 1e-30)
                    freqs_sp = np.fft.rfftfreq(len(segment), 1.0 / sr)
                    mask_10k = freqs_sp <= 10000
                    sp_10k = spectrum_db[mask_10k]
                    median_db = np.median(sp_10k)
                    n_peaks = np.sum(sp_10k > median_db + 3)
                    metric_val = float(n_peaks) / 10.0  # per kHz
                else:
                    metric_val = 0.0
                metric_unit = "peaks/kHz"

            elif metric_type == "er_timing":
                # Early reflection first peak time
                er_region = ir_l[:int(0.1 * sr)]
                threshold = 0.1 * np.max(np.abs(er_region))
                above = np.where(np.abs(er_region) > threshold)[0]
                metric_val = float(above[0] / sr * 1000) if len(above) > 0 else 0.0
                metric_unit = "ms"

            else:
                metric_val = 0.0
                metric_unit = ""

            sweep_values.append(float(val))
            sweep_metrics.append(metric_val)
            print(f"    {sem_key}={val:.3f} → {metric_type}={metric_val:.3f} {metric_unit}")

        # Restore original config
        mod_config = dict(vv_config)
        mod_config[sem_key] = original_val
        apply_reference_params(vv_plugin, mod_config)

        # Determine scaling type by fitting linear, log, and exponential
        x = np.array(sweep_values)
        y = np.array(sweep_metrics)

        # Filter out zeros/invalid
        valid = y > 0
        if np.sum(valid) < 3:
            scaling = "insufficient_data"
            best_r2 = 0.0
        else:
            x_v = x[valid]
            y_v = y[valid]

            fits = {}
            # Linear: y = a*x + b
            c_lin = np.polyfit(x_v, y_v, 1)
            pred_lin = np.polyval(c_lin, x_v)
            ss_res = np.sum((y_v - pred_lin) ** 2)
            ss_tot = np.sum((y_v - np.mean(y_v)) ** 2)
            fits["linear"] = 1.0 - ss_res / max(ss_tot, 1e-30)

            # Exponential: log(y) = a*x + b → y = e^(ax+b)
            log_y = np.log(y_v)
            c_exp = np.polyfit(x_v, log_y, 1)
            pred_exp = np.exp(np.polyval(c_exp, x_v))
            ss_res_e = np.sum((y_v - pred_exp) ** 2)
            fits["exponential"] = 1.0 - ss_res_e / max(ss_tot, 1e-30)

            # Logarithmic: y = a*log(x) + b
            log_x = np.log(np.maximum(x_v, 1e-6))
            c_log = np.polyfit(log_x, y_v, 1)
            pred_log = np.polyval(c_log, log_x)
            ss_res_l = np.sum((y_v - pred_log) ** 2)
            fits["logarithmic"] = 1.0 - ss_res_l / max(ss_tot, 1e-30)

            # Quadratic: y = a*x^2 + b*x + c
            c_quad = np.polyfit(x_v, y_v, 2)
            pred_quad = np.polyval(c_quad, x_v)
            ss_res_q = np.sum((y_v - pred_quad) ** 2)
            fits["quadratic"] = 1.0 - ss_res_q / max(ss_tot, 1e-30)

            scaling = max(fits, key=fits.get)
            best_r2 = fits[scaling]

            print(f"\n    Scaling analysis (R² values):")
            for fit_name, r2 in sorted(fits.items(), key=lambda x: -x[1]):
                marker = " ◄ best" if fit_name == scaling else ""
                print(f"      {fit_name:>14s}: R²={r2:.4f}{marker}")

        phase9[display_name] = {
            "param_key": sem_key,
            "param_values": sweep_values,
            "metric_type": metric_type,
            "metric_values": sweep_metrics,
            "metric_unit": metric_unit,
            "scaling": scaling,
            "scaling_r2": float(best_r2),
        }

    results["phase9"] = phase9

    if save:
        plt = setup_dark_theme()

        n_sweeps = len(phase9)
        if n_sweeps == 0:
            return

        fig, axes = plt.subplots(1, n_sweeps, figsize=(7 * n_sweeps, 6))
        if n_sweeps == 1:
            axes = [axes]

        for idx, (name, data) in enumerate(phase9.items()):
            ax = axes[idx]
            x = np.array(data["param_values"])
            y = np.array(data["metric_values"])

            ax.plot(x, y, 'o-', color=VV_COLOR, linewidth=2, markersize=6)

            # Overlay best-fit curve
            x_fit = np.linspace(x[0], x[-1], 100)
            valid = np.array(y) > 0
            if np.sum(valid) >= 3:
                x_v = x[valid]
                y_v = y[valid]
                if data["scaling"] == "linear":
                    c = np.polyfit(x_v, y_v, 1)
                    ax.plot(x_fit, np.polyval(c, x_fit), '--', color='white',
                            alpha=0.5, label=f'linear (R²={data["scaling_r2"]:.3f})')
                elif data["scaling"] == "exponential":
                    c = np.polyfit(x_v, np.log(y_v), 1)
                    ax.plot(x_fit, np.exp(np.polyval(c, x_fit)), '--', color='white',
                            alpha=0.5, label=f'exponential (R²={data["scaling_r2"]:.3f})')
                elif data["scaling"] == "logarithmic":
                    c = np.polyfit(np.log(np.maximum(x_v, 1e-6)), y_v, 1)
                    ax.plot(x_fit, np.polyval(c, np.log(np.maximum(x_fit, 1e-6))),
                            '--', color='white', alpha=0.5,
                            label=f'logarithmic (R²={data["scaling_r2"]:.3f})')
                elif data["scaling"] == "quadratic":
                    c = np.polyfit(x_v, y_v, 2)
                    ax.plot(x_fit, np.polyval(c, x_fit), '--', color='white',
                            alpha=0.5, label=f'quadratic (R²={data["scaling_r2"]:.3f})')

            ax.set_title(f'{name} ({data["param_key"]})\nScaling: {data["scaling"]}')
            ax.set_xlabel(f'Parameter Value (0→1)')
            ax.set_ylabel(f'{data["metric_type"]} ({data["metric_unit"]})')
            ax.legend(fontsize=8)
            ax.grid(True, alpha=0.3)

        fig.suptitle('28 — Parameter Sweep ("Knob Math")', fontsize=14)
        fig.tight_layout()
        fig.savefig(os.path.join(out_dir, '28_parameter_sweep.png'))
        plt.close(fig)
        print("  Saved 28_parameter_sweep.png")


# ─────────────────────────────────────────────────────────────────────
# Unified Console Report
# ─────────────────────────────────────────────────────────────────────

def _grade(delta, threshold_good, threshold_warn):
    """Return grade symbol based on delta and thresholds."""
    if abs(delta) <= threshold_good:
        return "\u2713"  # checkmark
    elif abs(delta) <= threshold_warn:
        return "\u26a0"  # warning
    else:
        return "\u2717"  # cross


def _grade_pct(val_a, val_b, pct_good=0.10, pct_warn=0.25):
    """Grade based on percentage difference."""
    if abs(val_b) < 1e-12:
        return "\u2713" if abs(val_a) < 1e-6 else "\u2717"
    pct = abs(val_a - val_b) / abs(val_b)
    return _grade(pct, pct_good, pct_warn)


def _score_abs(delta, good_thresh, bad_thresh):
    """Return 0.0-1.0 score: 1.0 = perfect, 0.0 = terrible."""
    d = abs(delta)
    if d <= good_thresh:
        return 1.0
    elif d >= bad_thresh:
        return 0.0
    else:
        return 1.0 - (d - good_thresh) / (bad_thresh - good_thresh)


def _score_pct(val_a, val_b, good_pct=0.10, bad_pct=0.50):
    """Return 0.0-1.0 score based on percentage match."""
    if abs(val_b) < 1e-12:
        return 1.0 if abs(val_a) < 1e-6 else 0.0
    pct = abs(val_a - val_b) / abs(val_b)
    return _score_abs(pct, good_pct, bad_pct)


def _safe_get(results, *keys, default=None):
    """Safely navigate nested result dicts."""
    d = results
    for k in keys:
        if isinstance(d, dict) and k in d:
            d = d[k]
        else:
            return default
    return d


# ─────────────────────────────────────────────────────────────────────
# Reverb Character Profile (display only — not scored)
# ─────────────────────────────────────────────────────────────────────

def _measure_band_rt60(ir, sr, low_hz, high_hz):
    """Measure RT60 in a specific frequency band via Schroeder integration."""
    nyq = sr / 2
    low = max(low_hz / nyq, 0.001)
    high = min(high_hz / nyq, 0.999)
    sos = butter(4, [low, high], btype='band', output='sos')
    filtered = sosfilt(sos, ir)

    energy = filtered ** 2
    edc = np.cumsum(energy[::-1])[::-1]
    edc_db = 10 * np.log10(edc / (edc[0] + 1e-30) + 1e-30)

    t = np.arange(len(edc_db)) / sr
    mask = (edc_db >= -35) & (edc_db <= -5)
    if np.sum(mask) < 10:
        return 0.0
    coeffs = np.polyfit(t[mask], edc_db[mask], 1)
    if coeffs[0] >= 0:
        return 0.0
    rt60 = -60.0 / coeffs[0]
    return max(0.0, rt60)


def _spectral_centroid_window(ir, sr, start_ms, end_ms):
    """Spectral centroid of a time window of the IR."""
    s0 = int(start_ms * sr / 1000)
    s1 = min(int(end_ms * sr / 1000), len(ir))
    if s1 <= s0:
        return 0.0
    segment = ir[s0:s1]
    spectrum = np.abs(np.fft.rfft(segment))
    freqs = np.fft.rfftfreq(len(segment), 1.0 / sr)
    total = np.sum(spectrum)
    if total < 1e-30:
        return 0.0
    return float(np.sum(freqs * spectrum) / total)


def _envelope_smoothness(ir, sr):
    """Measure deviation from smooth exponential decay (lower = smoother)."""
    from scipy.signal import hilbert
    s0 = int(0.2 * sr)
    s1 = min(int(1.0 * sr), len(ir))
    if s1 - s0 < 100:
        return 0.0
    env = np.abs(hilbert(ir[s0:s1]))
    win = max(1, int(0.01 * sr))
    env_smooth = np.convolve(env, np.ones(win) / win, mode='valid')
    if len(env_smooth) < 2:
        return 0.0
    env_db = 20 * np.log10(env_smooth + 1e-30)
    t = np.arange(len(env_db)) / sr
    coeffs = np.polyfit(t, env_db, 1)
    residual = env_db - np.polyval(coeffs, t)
    return float(np.std(residual))


def _echo_density_at(ir, sr, time_ms, window_ms=20):
    """Zero-crossings per second in a short window around time_ms."""
    center = int(time_ms * sr / 1000)
    half = int(window_ms * sr / 1000 / 2)
    s0 = max(0, center - half)
    s1 = min(len(ir), center + half)
    if s1 - s0 < 4:
        return 0.0
    segment = ir[s0:s1]
    zc = np.sum(np.abs(np.diff(np.sign(segment))) > 0)
    duration = (s1 - s0) / sr
    return float(zc / duration) if duration > 0 else 0.0


def _bar(value, max_val=10):
    """Simple ASCII bar chart."""
    filled = int(round(max(0, min(max_val, value))))
    return "\u2588" * filled + "\u2591" * (max_val - filled)


def run_phase10(dv_plugin, vv_plugin, sr, results, out_dir, save):
    """Phase 10: THD & Saturation Profiling via ESS harmonic separation.

    Captures ESS at multiple drive levels. Harmonic distortion products
    separate to negative time in the deconvolved buffer, allowing direct
    measurement of THD, individual harmonic levels, and even/odd ratio.
    """
    print("\n\u2500\u2500 Phase 10: THD & Saturation Profiling \u2500\u2500")

    levels_dbfs = [-40, -24, -12, -6]
    plugins = [("DuskVerb", dv_plugin)]
    if vv_plugin:
        plugins.append(("VintageVerb", vv_plugin))

    all_results = {}

    for name, plugin in plugins:
        plugin_data = {"thd_pct": {}, "harmonics_db": {}, "even_odd_db": {}}

        for level in levels_dbfs:
            print(f"  {name} @ {level} dBFS ...", end="", flush=True)
            ir_l, ir_r, full_l, full_r, sinfo = capture_ir_ess(
                plugin, sr, amplitude_dbfs=level, return_full=True)

            N_fft = sinfo["N_fft"]
            f1 = sinfo["f1"]
            f2 = sinfo["f2"]
            T = sinfo["duration"]
            ir_samples = int(IR_DURATION * sr)

            # Fundamental IR energy (RMS of trimmed linear IR)
            fund_rms = float(np.sqrt(np.mean(full_l[:ir_samples].astype(np.float64) ** 2)))
            if fund_rms < 1e-30:
                print(" (no signal)")
                continue

            harmonics_db = []
            harmonic_rms_sq = 0.0

            for n in range(2, 8):  # H2..H7
                # Negative-time offset for n-th harmonic
                offset_samples = int(T * np.log(n) / np.log(f2 / f1) * sr)

                # Check that harmonic doesn't overlap with linear IR
                if offset_samples > N_fft - ir_samples:
                    harmonics_db.append(None)
                    continue
                if offset_samples < 2048:
                    harmonics_db.append(None)
                    continue

                # Extract window around harmonic impulse (wrapped to end of buffer)
                center = N_fft - offset_samples
                half_win = 2048
                lo = max(0, center - half_win)
                hi = min(N_fft, center + half_win)
                h_segment = full_l[lo:hi].astype(np.float64)

                h_rms = float(np.sqrt(np.mean(h_segment ** 2)))
                harmonic_rms_sq += h_rms ** 2

                h_db = 20.0 * np.log10(h_rms / fund_rms + 1e-30)
                harmonics_db.append(float(h_db))

            thd_pct = float(np.sqrt(harmonic_rms_sq) / fund_rms * 100.0)

            # Even vs odd harmonic ratio
            even_sq = sum((10 ** (harmonics_db[i] / 10.0)) if harmonics_db[i] is not None else 0
                          for i in [0, 2, 4])  # H2, H4, H6
            odd_sq = sum((10 ** (harmonics_db[i] / 10.0)) if harmonics_db[i] is not None else 0
                         for i in [1, 3, 5])   # H3, H5, H7
            if odd_sq > 1e-30 and even_sq > 1e-30:
                even_odd_db = float(10.0 * np.log10(even_sq / odd_sq))
            else:
                even_odd_db = 0.0

            plugin_data["thd_pct"][level] = thd_pct
            plugin_data["harmonics_db"][level] = harmonics_db
            plugin_data["even_odd_db"][level] = even_odd_db
            print(f" THD={thd_pct:.4f}%")

        all_results[name] = plugin_data

    results["phase10"] = all_results

    # Console output: comparison table
    has_vv = "VintageVerb" in all_results
    dv_data = all_results.get("DuskVerb", {})
    vv_data = all_results.get("VintageVerb", {}) if has_vv else {}

    max_thd = max(
        max((v for v in dv_data.get("thd_pct", {}).values()), default=0),
        max((v for v in vv_data.get("thd_pct", {}).values()), default=0),
    )

    if max_thd < 0.01:
        print("  THD below measurement floor (<0.01%) for both plugins.")
    else:
        hdr = f"  {'Input Level':>12s}    {'DV THD%':>8s}"
        if has_vv:
            hdr += f"    {'VV THD%':>8s}    {'DV E/O':>8s}    {'VV E/O':>8s}"
        print(hdr)

        for level in levels_dbfs:
            dv_thd = dv_data.get("thd_pct", {}).get(level, 0)
            row = f"  {level:>7d} dBFS    {dv_thd:>7.4f}%"
            if has_vv:
                vv_thd = vv_data.get("thd_pct", {}).get(level, 0)
                dv_eo = dv_data.get("even_odd_db", {}).get(level, 0)
                vv_eo = vv_data.get("even_odd_db", {}).get(level, 0)
                row += f"    {vv_thd:>7.4f}%    {dv_eo:>+7.1f} dB    {vv_eo:>+7.1f} dB"
            print(row)

        # Harmonic profile at loudest level
        for name_key in ["DuskVerb", "VintageVerb"]:
            data = all_results.get(name_key)
            if data is None:
                continue
            harms = data.get("harmonics_db", {}).get(-6, [])
            if not harms:
                continue
            parts = []
            for i, h_db in enumerate(harms):
                label = f"H{i+2}"
                if h_db is not None:
                    parts.append(f"{label}={h_db:+.0f}dB")
                else:
                    parts.append(f"{label}=n/a")
            print(f"  {name_key} harmonic profile @ -6 dBFS: {' '.join(parts)}")

    # Plots
    if save and max_thd >= 0.01:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt

        # 29a: THD vs level
        fig, ax = plt.subplots(figsize=(8, 5))
        for name_key, color in [("DuskVerb", "steelblue"), ("VintageVerb", "coral")]:
            data = all_results.get(name_key)
            if data is None:
                continue
            thds = data.get("thd_pct", {})
            if thds:
                lvls = sorted(thds.keys())
                ax.plot(lvls, [thds[l] for l in lvls], 'o-', color=color, label=name_key)
        ax.set_xlabel("Input Level (dBFS)")
        ax.set_ylabel("THD (%)")
        ax.set_title("THD vs Input Level")
        ax.legend()
        ax.grid(True, alpha=0.3)
        fig.tight_layout()
        fig.savefig(os.path.join(out_dir, "29a_thd_vs_level.png"), dpi=150)
        plt.close(fig)

        # 29b: Harmonic spectrum at -6 dBFS
        fig, ax = plt.subplots(figsize=(8, 5))
        bar_width = 0.35
        x = np.arange(6)  # H2..H7
        for ki, (name_key, color) in enumerate([("DuskVerb", "steelblue"), ("VintageVerb", "coral")]):
            data = all_results.get(name_key)
            if data is None:
                continue
            harms = data.get("harmonics_db", {}).get(-6, [])
            vals = [h if h is not None else -120 for h in harms[:6]]
            while len(vals) < 6:
                vals.append(-120)
            ax.bar(x + ki * bar_width, vals, bar_width, label=name_key, color=color)
        ax.set_xticks(x + bar_width / 2)
        ax.set_xticklabels([f"H{i}" for i in range(2, 8)])
        ax.set_ylabel("Level re fundamental (dB)")
        ax.set_title("Harmonic Distortion Profile @ -6 dBFS")
        ax.legend()
        ax.grid(True, alpha=0.3, axis='y')
        fig.tight_layout()
        fig.savefig(os.path.join(out_dir, "29b_harmonic_spectrum.png"), dpi=150)
        plt.close(fig)


def run_phase11(dv_ir_l, dv_ir_r, vv_ir_l, vv_ir_r, sr, results, out_dir, save):
    """Phase 11: Sparse Early Reflection Extraction via OMP.

    Extracts the first ~20 early reflection taps from each plugin's IR
    using Orthogonal Matching Pursuit. Compares DV and VV tap patterns
    and optionally exports VV taps as JSON for hardcoding.
    """
    print("\n\u2500\u2500 Phase 11: Sparse ER Extraction (OMP, first 80ms) \u2500\u2500")

    dv_taps = sparse_deconvolution(dv_ir_l, dv_ir_r, sr)

    has_vv = vv_ir_l is not None
    if has_vv:
        vv_taps = sparse_deconvolution(vv_ir_l, vv_ir_r, sr)
    else:
        vv_taps = []

    print(f"  DuskVerb extracted {len(dv_taps)} taps"
          + (f", VintageVerb extracted {len(vv_taps)} taps" if has_vv else ""))

    # Side-by-side table
    if has_vv:
        # Match VV taps to DV taps (within ±1 sample)
        matched_vv = set()
        matched_dv = set()
        matches = []  # (vv_idx, dv_idx, amp_error_db)

        for vi, vt in enumerate(vv_taps):
            best_di = None
            best_dist = 2.0
            for di, dt in enumerate(dv_taps):
                if di in matched_dv:
                    continue
                dist = abs(vt["delay_samples"] - dt["delay_samples"])
                if dist < best_dist:
                    best_dist = dist
                    best_di = di
            if best_di is not None and best_dist <= 1.0:
                matched_vv.add(vi)
                matched_dv.add(best_di)
                vv_amp = max(abs(vt["amplitude_l"]), abs(vt["amplitude_r"]), 1e-30)
                dv_amp = max(abs(dv_taps[best_di]["amplitude_l"]),
                             abs(dv_taps[best_di]["amplitude_r"]), 1e-30)
                amp_err = abs(20.0 * np.log10(dv_amp / vv_amp))
                matches.append((vi, best_di, amp_err))

        # Print table
        print(f"  {'#':>3s}  {'VV Delay':>9s}  {'VV Amp':>8s}  {'VV Pan':>7s}"
              f"    {'DV Delay':>9s}  {'DV Amp':>8s}  {'DV Pan':>7s}  {'Match':>5s}")
        for vi, vt in enumerate(vv_taps):
            vv_amp = max(abs(vt["amplitude_l"]), abs(vt["amplitude_r"]), 1e-30)
            row = f"  {vi+1:>3d}  {vt['delay_ms']:>8.2f}ms  {vv_amp:>8.4f}  {vt['pan_lr']:>+6.2f}"

            # Find matched DV tap
            match_info = next((m for m in matches if m[0] == vi), None)
            if match_info:
                di = match_info[1]
                dt = dv_taps[di]
                dv_amp = max(abs(dt["amplitude_l"]), abs(dt["amplitude_r"]), 1e-30)
                check = "\u2713"
                row += f"    {dt['delay_ms']:>8.2f}ms  {dv_amp:>8.4f}  {dt['pan_lr']:>+6.2f}    {check}"
            else:
                dash = "\u2014"
                cross = "\u2717"
                row += f"    {dash:>9s}  {dash:>8s}  {dash:>7s}    {cross}"
            print(row)

        n_matched = len(matches)
        n_total = len(vv_taps)
        mean_amp_err = float(np.mean([m[2] for m in matches])) if matches else 0.0
        pct = n_matched / n_total * 100 if n_total > 0 else 0
        print(f"  Tap match: {n_matched}/{n_total} ({pct:.0f}%), "
              f"mean amplitude error: {mean_amp_err:.1f} dB")
    else:
        # DV-only table
        print(f"  {'#':>3s}  {'Delay':>9s}  {'Amp L':>8s}  {'Amp R':>8s}  {'Pan':>7s}")
        for i, t in enumerate(dv_taps):
            print(f"  {i+1:>3d}  {t['delay_ms']:>8.2f}ms  {t['amplitude_l']:>8.4f}"
                  f"  {t['amplitude_r']:>8.4f}  {t['pan_lr']:>+6.2f}")

    # Store results
    phase11 = {
        "dv_taps": dv_taps,
        "dv_tap_count": len(dv_taps),
    }
    if has_vv:
        phase11["vv_taps"] = vv_taps
        phase11["vv_tap_count"] = len(vv_taps)
        phase11["tap_match_count"] = len(matches)
        phase11["tap_match_score"] = len(matches) / len(vv_taps) if vv_taps else 0
        phase11["mean_amplitude_error_db"] = mean_amp_err
    results["phase11"] = phase11

    # Export VV taps as JSON
    if save and has_vv and vv_taps:
        import json
        tap_file = os.path.join(out_dir, "vv_er_taps.json")
        with open(tap_file, 'w') as f:
            json.dump(vv_taps, f, indent=2)
        print(f"  VV taps exported to {tap_file}")

    # Plot
    if save:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt

        fig, ax = plt.subplots(figsize=(12, 5))

        # DV taps
        dv_delays = [t["delay_ms"] for t in dv_taps]
        dv_amps = [max(abs(t["amplitude_l"]), abs(t["amplitude_r"]), 1e-30) for t in dv_taps]
        markerline, stemlines, baseline = ax.stem(
            dv_delays, dv_amps, linefmt='C0-', markerfmt='C0o', basefmt='k-',
            label='DuskVerb')
        stemlines.set_linewidth(1.5)
        markerline.set_markersize(4)

        if has_vv:
            vv_delays = [t["delay_ms"] for t in vv_taps]
            vv_amps = [max(abs(t["amplitude_l"]), abs(t["amplitude_r"]), 1e-30)
                       for t in vv_taps]
            markerline, stemlines, baseline = ax.stem(
                vv_delays, vv_amps, linefmt='C1-', markerfmt='C1s', basefmt='k-',
                label='VintageVerb')
            stemlines.set_linewidth(1.5)
            markerline.set_markersize(4)

            # Highlight matched pairs
            for vi, di, _ in matches:
                vd = vv_taps[vi]["delay_ms"]
                va = max(abs(vv_taps[vi]["amplitude_l"]),
                         abs(vv_taps[vi]["amplitude_r"]), 1e-30)
                ax.plot(vd, va, 'go', markersize=10, alpha=0.4, zorder=0)

        ax.set_xlabel("Delay (ms)")
        ax.set_ylabel("Amplitude")
        ax.set_title("Sparse ER Extraction (OMP)")
        ax.legend()
        ax.grid(True, alpha=0.3)
        fig.tight_layout()
        fig.savefig(os.path.join(out_dir, "30a_sparse_er_stems.png"), dpi=150)
        plt.close(fig)


def print_character_profile(dv_ir_l, dv_ir_r, vv_ir_l, vv_ir_r, sr):
    """Print perceptual character profile translating measurements into musical terms."""
    dv = (dv_ir_l + dv_ir_r) / 2
    has_vv = vv_ir_l is not None and vv_ir_r is not None
    vv = (vv_ir_l + vv_ir_r) / 2 if has_vv else None

    bands = [
        ("Sub/Bass",  20,    200,  "Warmth/weight"),
        ("Low-mid",   200,   500,  "Body/fullness"),
        ("Mid",       500,   2000, "Presence/clarity"),
        ("Upper-mid", 2000,  5000, "Bite/edge"),
        ("Treble",    5000,  10000, "Brilliance"),
        ("Air",       10000, 20000, "Shimmer/openness"),
    ]

    hr = "\u2500"  # horizontal rule character

    print()
    print("\u2550" * 60)
    print("  REVERB CHARACTER PROFILE")
    print("\u2550" * 60)

    # --- Frequency-dependent decay ---
    print("\n  Decay by frequency band (RT60 in seconds):")
    if has_vv:
        print(f"  {'Band':>12s}  {'Character':<18s} {'DV':>6s}  {'VV':>6s}  {'Diff':>6s}")
        print(f"  {hr*12}  {hr*18} {hr*6}  {hr*6}  {hr*6}")
    else:
        print(f"  {'Band':>12s}  {'Character':<18s} {'DV':>6s}")
        print(f"  {hr*12}  {hr*18} {hr*6}")

    dv_band_rt60 = {}
    vv_band_rt60 = {}
    for name, lo, hi, character in bands:
        dv_rt = _measure_band_rt60(dv, sr, lo, hi)
        dv_band_rt60[name] = dv_rt
        if has_vv:
            vv_rt = _measure_band_rt60(vv, sr, lo, hi)
            vv_band_rt60[name] = vv_rt
            diff = dv_rt - vv_rt
            flag = " \u25c4" if abs(diff) > 0.5 else ""
            print(f"  {name:>12s}  {character:<18s} {dv_rt:5.2f}s  {vv_rt:5.2f}s  {diff:+.2f}s{flag}")
        else:
            print(f"  {name:>12s}  {character:<18s} {dv_rt:5.2f}s")

    # --- Spectral evolution ---
    windows = [("Attack", 0, 50), ("Early", 50, 200), ("Body", 200, 1000), ("Tail", 1000, 5000)]
    print(f"\n  Tonal evolution (spectral centroid, Hz):")
    if has_vv:
        print(f"  {'Window':>8s}    {'DV':>6s}  {'VV':>6s}  {'Diff':>6s}")
        print(f"  {hr*8}    {hr*6}  {hr*6}  {hr*6}")
    else:
        print(f"  {'Window':>8s}    {'DV':>6s}")
        print(f"  {hr*8}    {hr*6}")

    for name, t0, t1 in windows:
        dv_c = _spectral_centroid_window(dv, sr, t0, t1)
        if has_vv:
            vv_c = _spectral_centroid_window(vv, sr, t0, t1)
            diff = dv_c - vv_c
            flag = " \u25c4" if abs(diff) > 300 else ""
            print(f"  {name:>8s}    {dv_c:5.0f}   {vv_c:5.0f}   {diff:+.0f}{flag}")
        else:
            print(f"  {name:>8s}    {dv_c:5.0f}")

    # --- Echo density ---
    density_times = [50, 200, 500]
    print(f"\n  Echo density (zero-crossings/sec):")
    if has_vv:
        print(f"  {'Time':>8s}    {'DV':>7s}  {'VV':>7s}")
        print(f"  {hr*8}    {hr*7}  {hr*7}")
    else:
        print(f"  {'Time':>8s}    {'DV':>7s}")
        print(f"  {hr*8}    {hr*7}")

    for t_ms in density_times:
        dv_d = _echo_density_at(dv, sr, t_ms)
        if has_vv:
            vv_d = _echo_density_at(vv, sr, t_ms)
            print(f"  {t_ms:>6d}ms    {dv_d:7.0f}  {vv_d:7.0f}")
        else:
            print(f"  {t_ms:>6d}ms    {dv_d:7.0f}")

    # --- Envelope smoothness ---
    dv_smooth = _envelope_smoothness(dv, sr)
    print(f"\n  Envelope smoothness (lower = smoother tail):")
    if has_vv:
        vv_smooth = _envelope_smoothness(vv, sr)
        print(f"    DuskVerb:     {dv_smooth:.2f} dB deviation")
        print(f"    VintageVerb:  {vv_smooth:.2f} dB deviation")
    else:
        print(f"    DuskVerb:     {dv_smooth:.2f} dB deviation")

    # --- Perceptual summary ---
    def clamp(v, lo, hi):
        return max(lo, min(hi, v))

    dv_mid_rt = dv_band_rt60.get("Mid", 0.01)
    dv_bass_rt = dv_band_rt60.get("Sub/Bass", 0)
    dv_air_rt = dv_band_rt60.get("Air", 0)
    dv_lowmid_rt = dv_band_rt60.get("Low-mid", 0)
    dv_body_centroid = _spectral_centroid_window(dv, sr, 200, 1000)

    dv_warmth = clamp((dv_bass_rt / (dv_mid_rt + 0.01) - 0.5) * 5, 0, 10)
    dv_bright = clamp((dv_body_centroid - 500) / 200, 0, 10)
    dv_air_score = clamp((dv_air_rt / (dv_mid_rt + 0.01)) * 10, 0, 10)
    dv_smooth_score = clamp(10 - dv_smooth * 2, 0, 10)
    dv_body_score = clamp(dv_lowmid_rt * 3, 0, 10)

    dims = [
        ("Warmth",      dv_warmth,       "Bass sustain relative to mids"),
        ("Brightness",  dv_bright,       "Tonal center of the reverb body"),
        ("Air/Shimmer", dv_air_score,    "High-frequency sustain and openness"),
        ("Smoothness",  dv_smooth_score, "Tail decay consistency"),
        ("Body",        dv_body_score,   "Low-mid fullness and weight"),
    ]

    if has_vv:
        vv_mid_rt = vv_band_rt60.get("Mid", 0.01)
        vv_bass_rt = vv_band_rt60.get("Sub/Bass", 0)
        vv_air_rt = vv_band_rt60.get("Air", 0)
        vv_lowmid_rt = vv_band_rt60.get("Low-mid", 0)
        vv_body_centroid = _spectral_centroid_window(vv, sr, 200, 1000)

        vv_warmth = clamp((vv_bass_rt / (vv_mid_rt + 0.01) - 0.5) * 5, 0, 10)
        vv_bright = clamp((vv_body_centroid - 500) / 200, 0, 10)
        vv_air_score = clamp((vv_air_rt / (vv_mid_rt + 0.01)) * 10, 0, 10)
        vv_smooth_score = clamp(10 - vv_smooth * 2, 0, 10)
        vv_body_score = clamp(vv_lowmid_rt * 3, 0, 10)

        vv_scores = [vv_warmth, vv_bright, vv_air_score, vv_smooth_score, vv_body_score]
        dims = [
            ("Warmth",      dv_warmth,      vv_warmth,      "Bass sustain relative to mids"),
            ("Brightness",  dv_bright,      vv_bright,      "Tonal center of the reverb body"),
            ("Air/Shimmer", dv_air_score,   vv_air_score,   "High-frequency sustain and openness"),
            ("Smoothness",  dv_smooth_score, vv_smooth_score, "Tail decay consistency"),
            ("Body",        dv_body_score,  vv_body_score,  "Low-mid fullness and weight"),
        ]

        print(f"\n  Perceptual Character:")
        print(f"  {'Dimension':>14s}  {'DV':>22s}  {'VV':>22s}  {'Gap':>4s}")
        print(f"  {hr*14}  {hr*22}  {hr*22}  {hr*4}")
        for name, dv_val, vv_val, desc in dims:
            dv_b = _bar(dv_val)
            vv_b = _bar(vv_val)
            gap = dv_val - vv_val
            arrow = "\u25c4" if abs(gap) > 2 else ""
            print(f"  {name:>14s}  {dv_b} {dv_val:3.1f}  {vv_b} {vv_val:3.1f}  {gap:+.1f} {arrow}")
    else:
        print(f"\n  Perceptual Character:")
        print(f"  {'Dimension':>14s}  {'DV':>22s}")
        print(f"  {hr*14}  {hr*22}")
        for name, dv_val, desc in dims:
            dv_b = _bar(dv_val)
            print(f"  {name:>14s}  {dv_b} {dv_val:3.1f}")

    print(f"\n  \u25c4 = significant gap (>2 points / >0.5s / >300Hz)")
    print("\u2550" * 60)


def print_summary_report(results, mode_name, pairing):
    """Print comprehensive summary report using actual result keys."""

    vv_name = pairing["name"] if pairing else "?"

    print()
    print("\u2550" * 70)
    print(f"  REVERB FINGERPRINT: DuskVerb ({mode_name}) vs VintageVerb ({vv_name})")
    print("\u2550" * 70)

    scores = {}
    sg = _safe_get

    # ── DECAY & ENERGY ──
    print("\n  DECAY & ENERGY")

    # EDT/T30 at 500Hz from phase1
    dv_edt_500 = sg(results, "phase1", "dv_edt", "500 Hz", default=None)
    vv_edt_500 = sg(results, "phase1", "vv_edt", "500 Hz", default=None)
    if dv_edt_500 and vv_edt_500:
        d = dv_edt_500 - vv_edt_500
        g = _grade_pct(dv_edt_500, vv_edt_500)
        print(f"    EDT (500Hz)      {dv_edt_500:7.3f}s   vs  {vv_edt_500:7.3f}s    \u0394 {d:+.3f}s   {g}")
        scores["edt"] = _score_pct(dv_edt_500, vv_edt_500)

    dv_t30_500 = sg(results, "phase1", "dv_t30", "500 Hz", default=None)
    vv_t30_500 = sg(results, "phase1", "vv_t30", "500 Hz", default=None)
    if dv_t30_500 and vv_t30_500:
        d = dv_t30_500 - vv_t30_500
        g = _grade_pct(dv_t30_500, vv_t30_500)
        print(f"    T30 (500Hz)      {dv_t30_500:7.3f}s   vs  {vv_t30_500:7.3f}s    \u0394 {d:+.3f}s   {g}")
        scores["t30"] = _score_pct(dv_t30_500, vv_t30_500)

    # ISO 3382 from phase2
    dv_iso = sg(results, "phase2", "dv_iso", default={})
    vv_iso = sg(results, "phase2", "vv_iso", default={})
    if dv_iso and vv_iso:
        for p, unit, gt, wt in [
            ("C50", "dB", 1.0, 3.0), ("C80", "dB", 1.0, 3.0),
            ("D50", "", 0.05, 0.15), ("Ts", "s", 0.01, 0.03),
            ("BR", "", 0.1, 0.3), ("G", "dB", 1.0, 3.0),
        ]:
            dv_v = dv_iso.get(p)
            vv_v = vv_iso.get(p)
            if dv_v is not None and vv_v is not None:
                d = dv_v - vv_v
                g = _grade(d, gt, wt)
                print(f"    {p:16s} {dv_v:7.2f}{unit:3s}  vs  {vv_v:7.2f}{unit:3s}   \u0394 {d:+.2f}   {g}")
                if p in ("C50", "C80"):
                    scores[f"c50c80_{p}"] = _score_abs(d, gt, wt * 2)

    # ── STEREO ──
    print("\n  STEREO")
    dv_coup = sg(results, "phase4a", "dv_lr_coupling_dB", default=None)
    vv_coup = sg(results, "phase4a", "vv_lr_coupling_dB", default=None)
    if dv_coup is not None and vv_coup is not None:
        d = dv_coup - vv_coup
        g = _grade(d, 1.0, 3.0)
        print(f"    L\u2192R coupling     {dv_coup:7.1f} dB  vs  {vv_coup:7.1f} dB   \u0394 {d:+.1f}   {g}")
        scores["coupling"] = _score_abs(d, 1.0, 6.0)

    dv_width = sg(results, "phase6e", "dv_avg_side_mid_ratio", default=None)
    vv_width = sg(results, "phase6e", "vv_avg_side_mid_ratio", default=None)
    if dv_width is not None and vv_width is not None:
        d = dv_width - vv_width
        g = _grade_pct(dv_width, vv_width)
        print(f"    Stereo Width     {dv_width:7.3f}     vs  {vv_width:7.3f}      \u0394 {d:+.3f}   {g}")

    # IACC broadband from phase7b
    dv_iacc_early = sg(results, "phase7b", "dv_iacc_early", default=None)
    vv_iacc_early = sg(results, "phase7b", "vv_iacc_early", default=None)
    dv_iacc_late = sg(results, "phase7b", "dv_iacc_late", default=None)
    vv_iacc_late = sg(results, "phase7b", "vv_iacc_late", default=None)
    if dv_iacc_early and vv_iacc_early:
        dv_ie = np.mean(dv_iacc_early)
        vv_ie = np.mean(vv_iacc_early)
        d = dv_ie - vv_ie
        g = _grade(d, 0.1, 0.3)
        print(f"    IACC early       {dv_ie:7.3f}     vs  {vv_ie:7.3f}      \u0394 {d:+.3f}   {g}")
        scores["iacc"] = _score_abs(d, 0.1, 0.4)
    if dv_iacc_late and vv_iacc_late:
        dv_il = np.mean(dv_iacc_late)
        vv_il = np.mean(vv_iacc_late)
        d = dv_il - vv_il
        g = _grade(d, 0.1, 0.3)
        print(f"    IACC late        {dv_il:7.3f}     vs  {vv_il:7.3f}      \u0394 {d:+.3f}   {g}")
        scores["iacc_late"] = _score_abs(d, 0.1, 0.4)

    # ── TEMPORAL STRUCTURE ──
    print("\n  TEMPORAL STRUCTURE")
    dv_temporal = sg(results, "phase3", "dv_temporal", default=None)
    vv_temporal = sg(results, "phase3", "vv_temporal", default=None)
    if dv_temporal and vv_temporal:
        temp_scores = []
        for dv_w, vv_w in zip(dv_temporal, vv_temporal):
            dv_e = dv_w["energy_fraction"]
            vv_e = vv_w["energy_fraction"]
            d = dv_e - vv_e
            temp_scores.append(_score_abs(d, 0.02, 0.10))
        if temp_scores:
            scores["temporal"] = np.mean(temp_scores)
            print(f"    Temporal match   {scores['temporal']*100:5.1f}/100")

    # Modal density from phase4c
    dv_md = sg(results, "phase4c", "dv_modal_density", default=None)
    vv_md = sg(results, "phase4c", "vv_modal_density", default=None)
    if dv_md is not None and vv_md is not None:
        print(f"    Modal density    {dv_md:7.1f}/kHz vs  {vv_md:7.1f}/kHz")

    # ── REAL AUDIO ──
    print("\n  REAL AUDIO SIMILARITY")
    p5 = results.get("phase5", {})
    mfcc_dists = []
    spec_corrs = []
    env_corrs = []
    tilt_diffs = []
    level_diffs = []
    # Use all available signals (including user_input if present)
    sig_names = ["snare", "vocal", "piano", "pink_noise"]
    if "user_input" in p5:
        sig_names.append("user_input")
    print(f"    {'Signal':12s}  {'MFCC Dist':>10s}  {'Spec Corr':>10s}  {'Env Corr':>10s}  {'Tilt Δ':>8s}  {'Level Δ':>8s}")
    for sig_name in sig_names:
        sig_res = p5.get(sig_name, {})
        md = sig_res.get("mean_mfcc_distance", 0)
        sc = sig_res.get("spectral_correlation", 0)
        ec = sig_res.get("envelope_correlation", 0)
        td = sig_res.get("spectral_tilt_diff", 0)
        ld = sig_res.get("level_diff_db", 0)
        mfcc_dists.append(md)
        spec_corrs.append(sc)
        env_corrs.append(ec)
        tilt_diffs.append(td)
        level_diffs.append(ld)
        print(f"    {sig_name:12s}  {md:10.1f}  {sc:10.3f}  {ec:10.3f}  {td:+7.1f}  {ld:+7.1f}")
    if mfcc_dists:
        avg_md = np.mean(mfcc_dists)
        avg_sc = np.mean(spec_corrs)
        avg_ec = np.mean(env_corrs)
        avg_td = np.mean(tilt_diffs)
        avg_ld = np.mean(level_diffs)
        print(f"    {'AVERAGE':12s}  {avg_md:10.1f}  {avg_sc:10.3f}  {avg_ec:10.3f}  {avg_td:+7.1f}  {avg_ld:+7.1f}")
        # MFCC score: 0 = perfect, 50+ = terrible
        scores["mfcc"] = max(0, 1.0 - avg_md / 60.0)
        scores["spectral_corr"] = max(0, (avg_sc - 0.5) / 0.5)
        # Spectral tilt: 0dB diff = perfect, 5dB+ = terrible
        scores["spectral_tilt"] = _score_abs(avg_td, 1.0, 5.0)
        # Signal level: 0dB diff = perfect, 5dB+ = terrible
        scores["signal_level"] = _score_abs(avg_ld, 1.0, 5.0)

    # Spectral EQ match: average across all signals that have it
    eq_match_scores = []
    for sig_name in sig_names:
        sig_res = p5.get(sig_name, {})
        eqs = sig_res.get("eq_match_score")
        if eqs is not None:
            eq_match_scores.append(eqs / 100.0)  # normalize to 0-1
    if eq_match_scores:
        scores["spectral_eq_match"] = float(np.mean(eq_match_scores))

    # ── PERCEPTUAL ──
    print("\n  PERCEPTUAL")
    dv_ec = sg(results, "phase7a", "dv_early_centroid_Hz", default=None)
    vv_ec = sg(results, "phase7a", "vv_early_centroid_Hz", default=None)
    if dv_ec is not None and vv_ec is not None:
        d = dv_ec - vv_ec
        g = _grade_pct(dv_ec, vv_ec, 0.10, 0.25)
        print(f"    Early Centroid   {dv_ec:7.0f} Hz  vs  {vv_ec:7.0f} Hz   \u0394 {d:+.0f}    {g}")
        scores["centroid_early"] = _score_pct(dv_ec, vv_ec, 0.10, 0.40)

    dv_lc = sg(results, "phase7a", "dv_late_centroid_Hz", default=None)
    vv_lc = sg(results, "phase7a", "vv_late_centroid_Hz", default=None)
    if dv_lc is not None and vv_lc is not None:
        d = dv_lc - vv_lc
        g = _grade_pct(dv_lc, vv_lc, 0.10, 0.25)
        print(f"    Late Centroid    {dv_lc:7.0f} Hz  vs  {vv_lc:7.0f} Hz   \u0394 {d:+.0f}    {g}")
        scores["centroid_late"] = _score_pct(dv_lc, vv_lc, 0.10, 0.40)

    dv_slope = sg(results, "phase7c", "dv_single_slope_dBs", default=None)
    vv_slope = sg(results, "phase7c", "vv_single_slope_dBs", default=None)
    if dv_slope is not None and vv_slope is not None:
        d = dv_slope - vv_slope
        g = _grade_pct(dv_slope, vv_slope, 0.10, 0.25)
        print(f"    Decay Slope      {dv_slope:7.1f}dB/s vs  {vv_slope:7.1f}dB/s  \u0394 {d:+.1f}   {g}")
        scores["decay_shape"] = _score_pct(dv_slope, vv_slope, 0.10, 0.40)

    dv_kurt = sg(results, "phase4d", "dv_kurtosis", default=None)
    vv_kurt = sg(results, "phase4d", "vv_kurtosis", default=None)
    if dv_kurt is not None and vv_kurt is not None:
        d = dv_kurt - vv_kurt
        g = _grade_pct(dv_kurt, vv_kurt, 0.25, 0.50)
        print(f"    Late Kurtosis    {dv_kurt:7.2f}     vs  {vv_kurt:7.2f}      \u0394 {d:+.2f}   {g}")
        scores["late_kurtosis"] = _score_pct(dv_kurt, vv_kurt, 0.25, 0.60)

    # Echo density per band (phase7d kurtosis)
    dv_kurt_band = sg(results, "phase7d", "dv_kurtosis", default=None)
    vv_kurt_band = sg(results, "phase7d", "vv_kurtosis", default=None)
    if dv_kurt_band and vv_kurt_band:
        delta_kurt = [abs(d - v) for d, v in zip(dv_kurt_band, vv_kurt_band)]
        mean_dk = np.mean(delta_kurt)
        print(f"    Echo Density    mean Δkurtosis {mean_dk:.2f}")
        # Score: 0 diff = perfect, 2.0 diff = zero score
        scores["echo_density"] = max(0.0, 1.0 - mean_dk / 2.0)

    # DV modulation CV
    dv_cv_vals = [v for k, v in results.get("phase4b", {}).items() if k.startswith("dv_cv_")]
    vv_cv_vals = [v for k, v in results.get("phase4b", {}).items() if k.startswith("vv_cv_")]
    if dv_cv_vals and vv_cv_vals:
        dv_m = np.mean(dv_cv_vals)
        vv_m = np.mean(vv_cv_vals)
        print(f"    IR Variability   {dv_m:7.4f}     vs  {vv_m:7.4f}")
        scores["modulation"] = _score_abs(dv_m - vv_m, 0.3, 2.0)

    # ── OVERALL SCORE ──
    print()
    print("\u2500" * 70)
    weight_map = {
        "mfcc": 0.25, "spectral_eq_match": 0.10,
        "c50c80_C50": 0.06, "c50c80_C80": 0.06,
        "coupling": 0.06, "iacc": 0.06, "iacc_late": 0.02, "edt": 0.06, "t30": 0.06,
        "temporal": 0.07,
        "spectral_tilt": 0.02, "signal_level": 0.01,
        "centroid_early": 0.04, "centroid_late": 0.04,
        "decay_shape": 0.05, "late_kurtosis": 0.02, "modulation": 0.02,
    }
    total_w = sum(weight_map[k] for k in weight_map if k in scores)
    weighted = sum(scores[k] * weight_map[k] for k in weight_map if k in scores)
    overall = (weighted / total_w * 100) if total_w > 0 else 0

    label = "EXCELLENT" if overall >= 80 else "GOOD" if overall >= 60 else "FAIR" if overall >= 40 else "POOR"
    print(f"  OVERALL SIMILARITY SCORE: {overall:.1f}/100  ({label})")
    print()
    print("  Score breakdown:")
    for k, w in sorted(weight_map.items(), key=lambda x: -x[1]):
        if k in scores:
            print(f"    {k:20s}  {scores[k]*100:5.1f}/100  (weight {w*100:.0f}%)")
    missing = [k for k in weight_map if k not in scores]
    if missing:
        print(f"\n  Missing data for: {', '.join(missing)}")
    print("\u2550" * 70)


# ─────────────────────────────────────────────────────────────────────
# JSON Export
# ─────────────────────────────────────────────────────────────────────

def _convert_numpy(obj):
    """Recursively convert numpy types to Python native types."""
    if isinstance(obj, dict):
        return {k: _convert_numpy(v) for k, v in obj.items()}
    elif isinstance(obj, (list, tuple)):
        return [_convert_numpy(item) for item in obj]
    elif isinstance(obj, np.integer):
        return int(obj)
    elif isinstance(obj, np.floating):
        return float(obj)
    elif isinstance(obj, np.ndarray):
        return obj.tolist()
    elif isinstance(obj, np.bool_):
        return bool(obj)
    return obj


def save_json_report(results, out_dir):
    """Save fingerprint_report.json with all numeric data."""
    # Filter out non-serializable data (like raw arrays used for plotting)
    clean = {}
    for phase_key, phase_data in results.items():
        if isinstance(phase_data, dict):
            filtered = {}
            for k, v in phase_data.items():
                # Skip large raw arrays that were only for plotting
                if isinstance(v, np.ndarray) and v.size > 100:
                    continue
                filtered[k] = v
            clean[phase_key] = filtered
        else:
            clean[phase_key] = phase_data
    
    clean = _convert_numpy(clean)
    
    out_path = os.path.join(out_dir, 'fingerprint_report.json')
    with open(out_path, 'w') as f:
        json.dump(clean, f, indent=2, default=str)
    print(f"\n  JSON report saved to {out_path}")


# ─────────────────────────────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description='Reverb Fingerprint Analysis')
    parser.add_argument('--mode', required=True, help='Algorithm mode')
    parser.add_argument('--compare', action='store_true', help='Compare DV vs VV')
    parser.add_argument('--save', action='store_true', help='Save plots')
    parser.add_argument('--input', type=str, default=None,
                        help='Path to custom audio file (WAV/AIFF) to use as additional test signal in Phase 5')
    parser.add_argument('--ess', action='store_true',
                        help='Use Exponential Swept Sine for IR capture (higher SNR)')
    parser.add_argument('--sweep-params', action='store_true',
                        help='Run Phase 9 parameter sweeping on reference plugin (slow)')
    parser.add_argument('--thd', action='store_true',
                        help='Run Phase 10 THD/saturation profiling (multiple ESS sweeps, slow)')
    args = parser.parse_args()

    sr = SAMPLE_RATE
    out_dir = os.path.join(os.path.dirname(__file__), 'fingerprints', args.mode)
    if args.save:
        os.makedirs(out_dir, exist_ok=True)

    dv_plugin, vv_plugin, pairing = load_plugins(args.mode, compare=args.compare)

    results = {}

    # Phase 0: validate parameters and level match before any analysis
    run_phase0(dv_plugin, vv_plugin, sr, results)

    # Capture IRs — ESS or Dirac
    if args.ess:
        print("\nCapturing impulse responses (ESS deconvolution)...")
        dv_ir_l, dv_ir_r = capture_ir_ess(dv_plugin, sr)
        vv_ir_l, vv_ir_r = (capture_ir_ess(vv_plugin, sr) if vv_plugin else (None, None))
        results["ir_method"] = "ess"
    else:
        print("\nCapturing impulse responses (Dirac impulse)...")
        dv_ir_l, dv_ir_r = capture_ir(dv_plugin, sr)
        vv_ir_l, vv_ir_r = (capture_ir(vv_plugin, sr) if vv_plugin else (None, None))
        results["ir_method"] = "dirac"

    # Run all phases
    run_phase1(dv_ir_l, dv_ir_r, vv_ir_l, vv_ir_r, sr, results, out_dir, args.save)
    run_phase2(dv_ir_l, vv_ir_l, sr, results, out_dir, args.save)
    run_phase3(dv_ir_l, vv_ir_l, sr, results, out_dir, args.save)
    run_phase4a(dv_plugin, vv_plugin, sr, results, out_dir, args.save)
    run_phase4b(dv_plugin, vv_plugin, sr, results, out_dir, args.save)
    run_phase4c(dv_ir_l, vv_ir_l, sr, results, out_dir, args.save)
    run_phase4d(dv_ir_l, vv_ir_l, sr, results, out_dir, args.save)
    run_phase5(dv_plugin, vv_plugin, sr, results, out_dir, args.save,
               input_path=args.input)
    run_phase6a(dv_ir_l, vv_ir_l, sr, results, out_dir, args.save)
    run_phase6b(dv_ir_l, vv_ir_l, sr, results, out_dir, args.save)
    run_phase6c(dv_plugin, vv_plugin, sr, results, out_dir, args.save)
    run_phase6d(dv_plugin, vv_plugin, sr, results, out_dir, args.save)
    run_phase6e(dv_ir_l, dv_ir_r, vv_ir_l, vv_ir_r, sr, results, out_dir, args.save)
    run_phase7a(dv_ir_l, vv_ir_l, sr, results, out_dir, args.save)
    run_phase7b(dv_ir_l, dv_ir_r, vv_ir_l, vv_ir_r, sr, results, out_dir, args.save)
    run_phase7c(dv_ir_l, vv_ir_l, sr, results, out_dir, args.save)
    run_phase7d(dv_ir_l, vv_ir_l, sr, results, out_dir, args.save)

    # Phase 8: FDN structural analysis
    run_phase8a(dv_plugin, vv_plugin, sr, results, out_dir, args.save)
    run_phase8b(dv_ir_l, vv_ir_l, sr, results, out_dir, args.save)
    run_phase8c(dv_plugin, vv_plugin, pairing, sr, results, out_dir, args.save)

    # Phase 9: Parameter sweeping (optional, slow)
    if args.sweep_params:
        run_phase9(vv_plugin, pairing, sr, results, out_dir, args.save)

    # Phase 10: THD profiling (optional, slow — multiple ESS sweeps)
    if args.thd:
        run_phase10(dv_plugin, vv_plugin, sr, results, out_dir, args.save)

    # Phase 11: Sparse ER extraction (always runs in compare mode, fast)
    if args.compare and vv_plugin:
        run_phase11(dv_ir_l, dv_ir_r, vv_ir_l, vv_ir_r, sr, results, out_dir, args.save)

    # Character profile (display only, before scored summary)
    print_character_profile(dv_ir_l, dv_ir_r, vv_ir_l, vv_ir_r, sr)

    if args.compare and vv_plugin:
        print_summary_report(results, args.mode, pairing)

    if args.save:
        save_json_report(results, out_dir)

    print("\nDone.")


if __name__ == '__main__':
    main()
