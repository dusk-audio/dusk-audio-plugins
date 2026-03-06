#!/usr/bin/env python3
"""Modulation analysis: DV vs VV LFO rate, depth, and character.

For each algorithm (Hall, Room focus; Plate, Chamber, Ambient secondary):
  1. Captures 6s IRs with mod_depth=1.0 from both plugins
  2. Measures LFO rate via autocorrelation of instantaneous frequency drift
  3. Measures mod depth via spectral spread of a steady-state tone through the reverb
  4. Measures character: ratio of AM (amplitude modulation) to FM (frequency modulation)

Usage:
    python3 modulation_analysis.py [--algo Hall] [--all]
"""

import sys
import os
import argparse
import numpy as np
from pedalboard import load_plugin

sys.path.insert(0, os.path.dirname(__file__))
from config import (SAMPLE_RATE, find_plugin, DUSKVERB_PATHS, REFERENCE_REVERB_PATHS,
                    apply_duskverb_params, apply_reference_params)

SR = SAMPLE_RATE

# VV mode floats
VV_MODES = {
    "Hall":    0.0417,
    "Room":    0.1250,
    "Plate":   0.0833,
    "Chamber": 0.1667,
    "Ambient": 0.2917,
}

# VV ColorMode: 1980s neutral
VV_COLOR = 0.333


def process_stereo(plugin, signal, sr):
    stereo = np.stack([signal, signal], axis=0).astype(np.float32)
    out = plugin(stereo, sr)
    return out[0], out[1]


def flush_plugin(plugin, sr, dur=4.0):
    silence = np.zeros(int(sr * dur), dtype=np.float32)
    process_stereo(plugin, silence, sr)


def make_impulse(dur_s=6.0):
    n = int(SR * dur_s)
    sig = np.zeros(n, dtype=np.float32)
    sig[0] = 1.0
    return sig


def make_tone(freq_hz, dur_s=6.0):
    """Continuous tone for spectral spread measurement."""
    n = int(SR * dur_s)
    t = np.arange(n, dtype=np.float64) / SR
    sig = (0.1 * np.sin(2 * np.pi * freq_hz * t)).astype(np.float32)
    return sig


# ---------------------------------------------------------------------------
# Analysis: LFO Rate via autocorrelation of instantaneous frequency
# ---------------------------------------------------------------------------
def measure_lfo_rate(ir, sr, analysis_start=0.5, analysis_end=4.0):
    """Estimate dominant LFO rate from pitch modulation in the reverb tail.

    Method: Compute short-time phase derivative (instantaneous frequency) in a
    narrow band, then autocorrelate the IF signal to find periodicity.
    """
    s = int(sr * analysis_start)
    e = int(sr * analysis_end)
    tail = ir[s:e].astype(np.float64)

    if len(tail) < sr:
        return None, None

    # Bandpass around 2kHz to isolate a single spectral region
    # Use analytic signal (Hilbert transform) for IF estimation
    from scipy.signal import hilbert, butter, sosfilt

    # Bandpass 1.5-2.5 kHz
    sos = butter(4, [1500, 2500], btype='band', fs=sr, output='sos')
    filtered = sosfilt(sos, tail)

    # Analytic signal
    analytic = hilbert(filtered)
    inst_phase = np.unwrap(np.angle(analytic))
    inst_freq = np.diff(inst_phase) * sr / (2 * np.pi)

    # Remove DC (mean frequency) and high-frequency noise
    if_deviation = inst_freq - np.mean(inst_freq)

    # Lowpass the IF deviation to isolate LFO-range modulation (0.1-10 Hz)
    sos_lp = butter(2, 15, btype='low', fs=sr, output='sos')
    if_smooth = sosfilt(sos_lp, if_deviation)

    # Downsample to 100 Hz for efficient autocorrelation
    ds_factor = sr // 100
    if_ds = if_smooth[::ds_factor]
    ds_rate = sr / ds_factor

    if len(if_ds) < 50:
        return None, None

    # Autocorrelation
    if_ds -= np.mean(if_ds)
    acf = np.correlate(if_ds, if_ds, mode='full')
    acf = acf[len(acf) // 2:]  # positive lags only
    acf /= acf[0] + 1e-30

    # Find first peak after the origin (skip lag 0)
    # Look for peaks in 0.1 Hz to 5 Hz range (lag 20 to 1000 at 100 Hz)
    min_lag = int(ds_rate / 5.0)   # 5 Hz max
    max_lag = int(ds_rate / 0.1)   # 0.1 Hz min
    max_lag = min(max_lag, len(acf) - 1)

    if min_lag >= max_lag:
        return None, None

    search = acf[min_lag:max_lag]
    if len(search) < 3:
        return None, None

    # Find first significant peak
    peak_idx = None
    for i in range(1, len(search) - 1):
        if search[i] > search[i-1] and search[i] > search[i+1] and search[i] > 0.15:
            peak_idx = i
            break

    if peak_idx is None:
        return None, None

    lag_samples = peak_idx + min_lag
    lfo_rate = ds_rate / lag_samples
    peak_strength = search[peak_idx]

    return lfo_rate, peak_strength


# ---------------------------------------------------------------------------
# Analysis: Mod Depth via spectral spread
# ---------------------------------------------------------------------------
def measure_mod_depth(ir_l, ir_r, sr, analysis_start=1.0, analysis_end=4.0):
    """Measure modulation depth from spectral spread in the reverb tail.

    Method: Track the centroid frequency and its standard deviation over
    short windows. More modulation = wider frequency spread.
    Also measure the stereo decorrelation (cross-correlation coefficient).
    """
    s = int(sr * analysis_start)
    e = int(sr * analysis_end)
    tail_l = ir_l[s:e].astype(np.float64)
    tail_r = ir_r[s:e].astype(np.float64)

    if len(tail_l) < sr:
        return {}

    # Short-time spectral analysis
    window_size = 2048
    hop = 512
    n_windows = (len(tail_l) - window_size) // hop

    if n_windows < 10:
        return {}

    centroids = []
    spreads = []
    am_envelope_l = []
    am_envelope_r = []

    for w in range(n_windows):
        start = w * hop
        seg_l = tail_l[start:start + window_size]
        seg_r = tail_r[start:start + window_size]

        # Window
        win = np.hanning(window_size)
        spec_l = np.abs(np.fft.rfft(seg_l * win))
        spec_r = np.abs(np.fft.rfft(seg_r * win))
        spec = (spec_l + spec_r) / 2  # average L+R

        freqs = np.fft.rfftfreq(window_size, 1.0 / sr)

        # Spectral centroid (weighted by power)
        power = spec ** 2
        total_power = np.sum(power) + 1e-30
        centroid = np.sum(freqs * power) / total_power
        spread = np.sqrt(np.sum((freqs - centroid) ** 2 * power) / total_power)

        centroids.append(centroid)
        spreads.append(spread)

        # Amplitude envelope
        am_envelope_l.append(np.sqrt(np.mean(seg_l ** 2)))
        am_envelope_r.append(np.sqrt(np.mean(seg_r ** 2)))

    centroids = np.array(centroids)
    spreads = np.array(spreads)
    am_l = np.array(am_envelope_l)
    am_r = np.array(am_envelope_r)

    # FM depth: standard deviation of centroid frequency (Hz)
    fm_std = np.std(centroids)

    # AM depth: coefficient of variation of envelope
    am_mean_l = np.mean(am_l)
    am_mean_r = np.mean(am_r)
    am_cv_l = np.std(am_l) / (am_mean_l + 1e-30)
    am_cv_r = np.std(am_r) / (am_mean_r + 1e-30)
    am_cv = (am_cv_l + am_cv_r) / 2

    # Stereo decorrelation
    if len(tail_l) > 0:
        corr = np.corrcoef(tail_l[:min(len(tail_l), len(tail_r))],
                           tail_r[:min(len(tail_l), len(tail_r))])[0, 1]
    else:
        corr = 1.0

    return {
        'centroid_mean': np.mean(centroids),
        'centroid_std': fm_std,
        'spread_mean': np.mean(spreads),
        'am_cv': am_cv,
        'stereo_corr': corr,
    }


# ---------------------------------------------------------------------------
# Analysis: Chorus width via tone-through test
# ---------------------------------------------------------------------------
def measure_chorus_width(plugin, sr, tone_freq=1000, dur=6.0):
    """Send a pure tone through the reverb and measure spectral broadening.

    More modulation = wider spectral skirt around the tone frequency.
    Returns the -20dB bandwidth of the spectral peak (Hz).
    """
    tone = make_tone(tone_freq, dur)
    flush_plugin(plugin, sr, 4.0)
    out_l, out_r = process_stereo(plugin, tone, sr)
    flush_plugin(plugin, sr, 4.0)

    # Analyze the last 3 seconds (steady state)
    s = int(sr * 2.0)
    e = int(sr * 5.0)
    wet_l = out_l[s:e].astype(np.float64)
    wet_r = out_r[s:e].astype(np.float64)
    wet = (wet_l + wet_r) / 2

    if len(wet) < sr:
        return None

    # High-resolution FFT
    n_fft = len(wet)
    spec = np.abs(np.fft.rfft(wet * np.hanning(n_fft)))
    freqs = np.fft.rfftfreq(n_fft, 1.0 / sr)

    # Find peak near tone_freq
    tone_idx = np.argmin(np.abs(freqs - tone_freq))
    search_range = int(50 * n_fft / sr)  # ±50 Hz
    lo = max(0, tone_idx - search_range)
    hi = min(len(spec), tone_idx + search_range)
    peak_idx = lo + np.argmax(spec[lo:hi])
    peak_val = spec[peak_idx]

    if peak_val < 1e-10:
        return None

    # Measure -20dB bandwidth
    threshold = peak_val * 0.1  # -20 dB
    spec_db = 20 * np.log10(spec / peak_val + 1e-30)

    # Search outward from peak for -20dB crossings
    left_bw = 0
    for idx in range(peak_idx, lo, -1):
        if spec[idx] < threshold:
            left_bw = freqs[peak_idx] - freqs[idx]
            break

    right_bw = 0
    for idx in range(peak_idx, hi):
        if spec[idx] < threshold:
            right_bw = freqs[idx] - freqs[peak_idx]
            break

    bandwidth = left_bw + right_bw
    return bandwidth


def run_algorithm(dv, vv, algo, vv_mode_float):
    """Run full modulation analysis for one algorithm."""
    print(f"\n{'='*70}")
    print(f"  {algo}")
    print(f"{'='*70}")

    # Common settings: long decay, max mod depth, moderate other params
    decay = 5.0
    treble = 0.7
    size = 0.5
    mod_depth_ui = 1.0  # Max modulation
    mod_rate_vv = 0.30  # VV knob position (~1 Hz)

    # VV mod rate mapping: 0.1 * 100^val
    vv_mod_rate_hz = 0.1 * (100.0 ** mod_rate_vv)

    dv_config = {
        'algorithm': algo,
        'decay_time': decay,
        'room_size': size,
        'mod_depth': mod_depth_ui,
        'mod_rate': vv_mod_rate_hz,  # Match the Hz value
        'treble_multiply': treble,
        'bass_multiply': 1.0,
        'pre_delay': 0.0,
        'crossover': 1000,
        'diffusion': 0.7,
        'lo_cut': 20,
        'hi_cut': 20000,
        'width': 1.0,
    }

    vv_config = {
        '_reverbmode': vv_mode_float,
        '_colormode': VV_COLOR,
        '_decay': 0.40,  # ~5s decay for most modes
        '_size': size,
        '_predelay': 0.0,
        '_diffusion_early': 0.7,
        '_diffusion_late': 0.7,
        '_mod_rate': mod_rate_vv,
        '_mod_depth': mod_depth_ui,  # Max modulation
        '_high_cut': 1.0,
        '_low_cut': 0.0,
        '_bassmult': 0.5,
        '_bassxover': 0.4,
        '_highshelf': 0.3,  # Moderate HF damping
        '_highfreq': 0.5,
    }

    # --- Capture IRs ---
    imp = make_impulse(6.0)

    print(f"\n  Capturing DV IR (mod_depth=1.0, mod_rate={vv_mod_rate_hz:.2f} Hz)...")
    flush_plugin(dv, SR, 5.0)
    apply_duskverb_params(dv, dv_config)
    flush_plugin(dv, SR, 5.0)
    dv_l, dv_r = process_stereo(dv, imp, SR)
    flush_plugin(dv, SR, 5.0)

    print(f"  Capturing VV IR (mod_depth=1.0, mod_rate={mod_rate_vv})...")
    flush_plugin(vv, SR, 5.0)
    apply_reference_params(vv, vv_config)
    flush_plugin(vv, SR, 5.0)
    vv_l, vv_r = process_stereo(vv, imp, SR)
    flush_plugin(vv, SR, 5.0)

    # --- 1) LFO Rate ---
    print(f"\n  --- LFO Rate (from IF autocorrelation) ---")
    dv_rate, dv_strength = measure_lfo_rate(dv_l, SR)
    vv_rate, vv_strength = measure_lfo_rate(vv_l, SR)

    dv_rate_s = f"{dv_rate:.3f} Hz (acf={dv_strength:.2f})" if dv_rate else "N/A"
    vv_rate_s = f"{vv_rate:.3f} Hz (acf={vv_strength:.2f})" if vv_rate else "N/A"
    print(f"    DV LFO rate: {dv_rate_s}")
    print(f"    VV LFO rate: {vv_rate_s}")
    if dv_rate and vv_rate:
        ratio = dv_rate / vv_rate
        print(f"    Ratio DV/VV: {ratio:.2f}x")
        if abs(ratio - 1.0) < 0.15:
            print(f"    → Rates match well")
        elif ratio > 1.0:
            print(f"    → DV mod rate is FASTER than VV (need to lower modRateScale)")
        else:
            print(f"    → DV mod rate is SLOWER than VV (need to increase modRateScale)")

    # --- 2) Mod Depth (spectral analysis) ---
    print(f"\n  --- Modulation Depth (spectral spread) ---")
    dv_depth = measure_mod_depth(dv_l, dv_r, SR)
    vv_depth = measure_mod_depth(vv_l, vv_r, SR)

    if dv_depth and vv_depth:
        print(f"    {'Metric':>20s}  {'DV':>10s}  {'VV':>10s}  {'Ratio':>8s}")
        print(f"    {'-'*52}")
        for key in ['centroid_std', 'spread_mean', 'am_cv', 'stereo_corr']:
            dv_val = dv_depth.get(key, 0)
            vv_val = vv_depth.get(key, 0)
            ratio = dv_val / (vv_val + 1e-30)
            labels = {
                'centroid_std': 'FM depth (Hz)',
                'spread_mean': 'Spectral spread',
                'am_cv': 'AM depth (CV)',
                'stereo_corr': 'Stereo corr',
            }
            print(f"    {labels[key]:>20s}  {dv_val:>10.2f}  {vv_val:>10.2f}  {ratio:>7.2f}x")

        # Interpret
        fm_ratio = dv_depth['centroid_std'] / (vv_depth['centroid_std'] + 1e-30)
        am_ratio = dv_depth['am_cv'] / (vv_depth['am_cv'] + 1e-30)
        print()
        if fm_ratio > 1.3:
            print(f"    → DV has MORE pitch modulation than VV ({fm_ratio:.1f}x)")
        elif fm_ratio < 0.7:
            print(f"    → DV has LESS pitch modulation than VV ({fm_ratio:.1f}x)")
        else:
            print(f"    → FM depth matches well ({fm_ratio:.1f}x)")

        if am_ratio > 1.3:
            print(f"    → DV has MORE amplitude modulation than VV ({am_ratio:.1f}x)")
        elif am_ratio < 0.7:
            print(f"    → DV has LESS amplitude modulation than VV ({am_ratio:.1f}x)")
        else:
            print(f"    → AM depth matches well ({am_ratio:.1f}x)")

    # --- 3) Chorus Width (tone through reverb) ---
    print(f"\n  --- Chorus Width (1kHz tone through reverb, -20dB BW) ---")
    dv_bw = measure_chorus_width(dv, SR)
    vv_bw = measure_chorus_width(vv, SR)

    dv_bw_s = f"{dv_bw:.1f} Hz" if dv_bw else "N/A"
    vv_bw_s = f"{vv_bw:.1f} Hz" if vv_bw else "N/A"
    print(f"    DV chorus BW: {dv_bw_s}")
    print(f"    VV chorus BW: {vv_bw_s}")
    if dv_bw and vv_bw:
        bw_ratio = dv_bw / vv_bw
        print(f"    Ratio DV/VV: {bw_ratio:.2f}x")
        if bw_ratio > 1.5:
            print(f"    → DV chorus is WIDER — reduce modDepthScale")
        elif bw_ratio < 0.67:
            print(f"    → DV chorus is NARROWER — increase modDepthScale")
        else:
            print(f"    → Chorus width matches well")

    # --- Also test with lower mod depth to check linearity ---
    print(f"\n  --- Mod Depth Scaling Check (depth=0.3 vs 1.0) ---")
    dv_config_low = dict(dv_config, mod_depth=0.3)
    vv_config_low = dict(vv_config, **{'_mod_depth': 0.3})

    flush_plugin(dv, SR, 5.0)
    apply_duskverb_params(dv, dv_config_low)
    flush_plugin(dv, SR, 5.0)
    dv_low_l, dv_low_r = process_stereo(dv, imp, SR)
    flush_plugin(dv, SR, 5.0)

    flush_plugin(vv, SR, 5.0)
    apply_reference_params(vv, vv_config_low)
    flush_plugin(vv, SR, 5.0)
    vv_low_l, vv_low_r = process_stereo(vv, imp, SR)
    flush_plugin(vv, SR, 5.0)

    dv_depth_low = measure_mod_depth(dv_low_l, dv_low_r, SR)
    vv_depth_low = measure_mod_depth(vv_low_l, vv_low_r, SR)

    if dv_depth_low and vv_depth_low and dv_depth and vv_depth:
        dv_fm_ratio = dv_depth_low['centroid_std'] / (dv_depth['centroid_std'] + 1e-30)
        vv_fm_ratio = vv_depth_low['centroid_std'] / (vv_depth['centroid_std'] + 1e-30)
        print(f"    DV FM at 0.3/1.0: {dv_fm_ratio:.2f}x  (linear would be 0.30)")
        print(f"    VV FM at 0.3/1.0: {vv_fm_ratio:.2f}x  (linear would be 0.30)")
        if abs(dv_fm_ratio - vv_fm_ratio) > 0.15:
            print(f"    → Scaling curves differ — VV may use nonlinear depth mapping")

    return {
        'algo': algo,
        'dv_rate': dv_rate,
        'vv_rate': vv_rate,
        'dv_fm': dv_depth.get('centroid_std') if dv_depth else None,
        'vv_fm': vv_depth.get('centroid_std') if vv_depth else None,
        'dv_am': dv_depth.get('am_cv') if dv_depth else None,
        'vv_am': vv_depth.get('am_cv') if vv_depth else None,
        'dv_bw': dv_bw,
        'vv_bw': vv_bw,
    }


def main():
    parser = argparse.ArgumentParser(description='Modulation analysis: DV vs VV')
    parser.add_argument('--algo', type=str, default=None,
                        help='Single algorithm to test (Hall, Room, Plate, Chamber, Ambient)')
    parser.add_argument('--all', action='store_true',
                        help='Test all algorithms')
    args = parser.parse_args()

    dv_path = find_plugin(DUSKVERB_PATHS)
    vv_path = find_plugin(REFERENCE_REVERB_PATHS)
    if not dv_path or not vv_path:
        print("ERROR: Plugin(s) not found")
        sys.exit(1)

    print(f"DuskVerb:     {dv_path}")
    print(f"ReferenceReverb:  {vv_path}")
    print(f"Sample rate:  {SR} Hz")

    dv = load_plugin(dv_path)
    vv = load_plugin(vv_path)

    if args.algo:
        algos = [args.algo]
    elif args.all:
        algos = ["Hall", "Room", "Plate", "Chamber", "Ambient"]
    else:
        # Default: focus on Hall and Room
        algos = ["Hall", "Room"]

    results = []
    for algo in algos:
        if algo not in VV_MODES:
            print(f"Unknown algorithm: {algo}")
            continue
        r = run_algorithm(dv, vv, algo, VV_MODES[algo])
        results.append(r)

    # --- Summary ---
    print(f"\n\n{'='*70}")
    print(f"  SUMMARY")
    print(f"{'='*70}")
    print(f"  {'Algorithm':>10s}  {'LFO Rate':>20s}  {'FM Depth':>20s}  {'Chorus BW':>20s}")
    print(f"  {'-'*74}")

    for r in results:
        rate_s = "N/A"
        if r['dv_rate'] and r['vv_rate']:
            ratio = r['dv_rate'] / r['vv_rate']
            rate_s = f"DV/VV={ratio:.2f}x"

        fm_s = "N/A"
        if r['dv_fm'] and r['vv_fm']:
            ratio = r['dv_fm'] / r['vv_fm']
            fm_s = f"DV/VV={ratio:.2f}x"

        bw_s = "N/A"
        if r['dv_bw'] and r['vv_bw']:
            ratio = r['dv_bw'] / r['vv_bw']
            bw_s = f"DV/VV={ratio:.2f}x"

        print(f"  {r['algo']:>10s}  {rate_s:>20s}  {fm_s:>20s}  {bw_s:>20s}")

    # Recommendations
    print(f"\n  Recommendations:")
    for r in results:
        algo = r['algo']
        changes = []

        if r['dv_rate'] and r['vv_rate']:
            ratio = r['dv_rate'] / r['vv_rate']
            if ratio > 1.15:
                changes.append(f"modRateScale: decrease (DV {ratio:.1f}x faster)")
            elif ratio < 0.85:
                changes.append(f"modRateScale: increase (DV {ratio:.1f}x slower)")

        if r['dv_fm'] and r['vv_fm']:
            ratio = r['dv_fm'] / r['vv_fm']
            if ratio > 1.3:
                changes.append(f"modDepthScale: decrease (DV FM {ratio:.1f}x wider)")
            elif ratio < 0.7:
                changes.append(f"modDepthScale: increase (DV FM {ratio:.1f}x narrower)")

        if r['dv_bw'] and r['vv_bw']:
            ratio = r['dv_bw'] / r['vv_bw']
            if ratio > 1.5:
                changes.append(f"chorus too wide ({ratio:.1f}x) → reduce modDepthScale")
            elif ratio < 0.67:
                changes.append(f"chorus too narrow ({ratio:.1f}x) → increase modDepthScale")

        if changes:
            print(f"    {algo}: {'; '.join(changes)}")
        else:
            print(f"    {algo}: OK — modulation matches well")

    print("\nDone.")


if __name__ == "__main__":
    main()
