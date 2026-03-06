#!/usr/bin/env python3
"""
DuskVerb Room vs ReferenceRoom "SmallWoodRoom" preset comparison.

ReferenceRoom's "Dark Room" is a short, dark room reverb — completely different
from ReferenceReverb Room (which is a long-sustaining mode with ~55s RT60).

This test compares:
  - Level (absolute wet gain)
  - RT60 per octave band
  - EDC shape (Schroeder integral at key time points)
  - Echo density build-up
  - Slapback/discrete echo artifacts
  - Spectral balance (frequency response)
  - Tail smoothness

SmallWoodRoom preset (from user screenshot):
  Type: Dark Room, Decay: 0.60s, Predelay: 0ms, Lo Cut: 0 Hz, Hi Cut: 9300 Hz
  Early: Size 29.5ms, Cross 0.00, Mod Rate 1.07Hz, Mod Depth 0.00,
         Early Send 0.00, Diffusion 0.0%, Space 0.0%
  Late:  Size 0.09, Cross 0.88, Mod Rate 0.42Hz, Mod Depth 0.12,
         Bass Mult 0.90X, Bass Xover 1070Hz, High Mult 0.77X, High Xover 7450Hz
"""

import numpy as np
import pedalboard
from config import (
    SAMPLE_RATE, DUSKVERB_PATHS, REFERENCE_ROOM_PATHS,
    find_plugin, apply_duskverb_params, apply_reference_room_params,
)
import reverb_metrics as metrics

SR = SAMPLE_RATE


def load_plugin(path):
    try:
        return pedalboard.load_plugin(path)
    except Exception as e:
        print(f"ERROR loading {path}: {e}")
        return None


def flush(plugin, dur=3.0):
    silence = np.zeros((2, int(SR * dur)), dtype=np.float32)
    plugin(silence, SR)


def process_stereo(plugin, mono_signal):
    stereo_in = np.stack([mono_signal, mono_signal], axis=0).astype(np.float32)
    out = plugin(stereo_in, SR)
    return out[0], out[1]


def render_ir(plugin, duration=3.0):
    n = int(SR * duration)
    impulse = np.zeros((2, n), dtype=np.float32)
    impulse[0, 0] = 1.0
    impulse[1, 0] = 1.0
    out = plugin(impulse, SR)
    return out[0], out[1]


def measure_wet_gain(plugin, duration=4.0):
    """Send pink noise and measure wet output RMS relative to input RMS."""
    n = int(SR * duration)
    white = np.random.randn(n).astype(np.float32) * 0.1
    pink = np.zeros(n, dtype=np.float32)
    b = [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
    for i in range(n):
        w = white[i]
        b[0] = 0.99886 * b[0] + w * 0.0555179
        b[1] = 0.99332 * b[1] + w * 0.0750759
        b[2] = 0.96900 * b[2] + w * 0.1538520
        b[3] = 0.86650 * b[3] + w * 0.3104856
        b[4] = 0.55000 * b[4] + w * 0.5329522
        b[5] = -0.7616 * b[5] - w * 0.0168980
        pink[i] = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362
        b[6] = w * 0.115926
    pink *= 0.1 / max(np.max(np.abs(pink)), 1e-30)

    stereo_in = np.stack([pink, pink])
    out = plugin(stereo_in, SR)
    mono_out = (out[0] + out[1]) * 0.5

    start = int(SR * 1.0)
    in_rms = np.sqrt(np.mean(pink[start:] ** 2))
    out_rms = np.sqrt(np.mean(mono_out[start:] ** 2))

    in_db = 20 * np.log10(max(in_rms, 1e-30))
    out_db = 20 * np.log10(max(out_rms, 1e-30))
    return in_db, out_db, out_db - in_db


def find_slapback_peaks(ir, min_time_ms=5, max_time_ms=200, threshold_db=-30):
    """Find discrete peaks in the IR that stand out above the local envelope."""
    abs_ir = np.abs(ir)
    peak_val = np.max(abs_ir)
    if peak_val < 1e-30:
        return []
    ir_db = 20 * np.log10(np.maximum(abs_ir, 1e-30) / peak_val)
    min_idx = int(min_time_ms * SR / 1000)
    max_idx = min(int(max_time_ms * SR / 1000), len(ir_db))

    peaks = []
    for i in range(min_idx, max_idx):
        half_ms = int(0.001 * SR)
        lo = max(0, i - half_ms)
        hi = min(len(abs_ir), i + half_ms)
        if abs_ir[i] < np.max(abs_ir[lo:hi]) * 0.99:
            continue
        env_lo = max(0, i - int(0.003 * SR))
        env_hi = min(len(ir), i + int(0.003 * SR))
        exc_lo = max(0, i - int(0.0005 * SR))
        exc_hi = min(len(ir), i + int(0.0005 * SR))
        env_samples = np.concatenate([abs_ir[env_lo:exc_lo], abs_ir[exc_hi:env_hi]])
        if len(env_samples) < 4:
            continue
        local_rms = np.sqrt(np.mean(env_samples ** 2))
        local_rms_db = 20 * np.log10(max(local_rms, 1e-30) / peak_val)
        prominence = ir_db[i] - local_rms_db
        time_ms = i / SR * 1000
        if ir_db[i] > threshold_db and prominence > 3.0:
            peaks.append((time_ms, ir_db[i], prominence))

    if not peaks:
        return peaks
    peaks.sort(key=lambda x: x[0])
    deduped = [peaks[0]]
    for p in peaks[1:]:
        if p[0] - deduped[-1][0] > 2.0:
            deduped.append(p)
        elif p[2] > deduped[-1][2]:
            deduped[-1] = p
    return sorted(deduped, key=lambda x: -x[2])


def compute_edc_at_times(ir, times_sec):
    """Compute Schroeder EDC at specific time points."""
    energy = ir.astype(np.float64) ** 2
    edc = np.cumsum(energy[::-1])[::-1]
    if edc[0] < 1e-30:
        return {t: -200.0 for t in times_sec}
    edc_db = 10.0 * np.log10(np.maximum(edc / edc[0], 1e-30))
    results = {}
    for t in times_sec:
        idx = int(t * SR)
        if idx < len(edc_db):
            results[t] = float(edc_db[idx])
        else:
            results[t] = -200.0
    return results


# ---------------------------------------------------------------------------
# ReferenceRoom "SmallWoodRoom" preset (Dark Room type)
# ---------------------------------------------------------------------------
# Translated from the user's screenshot to pedalboard 0-1 normalized params.
#
# ReferenceRoom type mapping (12 types, evenly spaced 0-1):
#   0.000 = Large Room, 0.083 = Medium Room, 0.167 = Bright Room,
#   0.250 = Large Chamber, 0.333 = Dark Room, 0.417 = Dark Chamber,
#   0.500 = Dark Hall, 0.583 = Bright Hall, 0.667 = Large Hall,
#   0.750 = Great Hall, 0.833 = Huge Hall, 0.917 = Cathedral
#
# Dark Room = type index 4 → _type ≈ 0.333 (but needs discovery)
# Let's try 0.364 (4/11) since there are 12 types over [0,1]
#
# Decay: 0.60s — needs normalized mapping. VRoom decay range is ~0.1-30s.
# Based on logarithmic mapping, 0.6s is roughly 0.10-0.15 normalized.
#
# Predelay: 0ms → 0.0 normalized
#
# Early Size: 29.5ms — need to figure out the normalized range.
# Late Size: 0.09 (already normalized 0-1)
#
# Hi cut: 9300 Hz → normalized. VRoom range likely 500-16000 Hz.
# Approximate: (9300-500)/(16000-500) ≈ 0.57

VROOM_SMALLWOODROOM = {
    "_type": 0.333,           # Dark Room (4th of 12 types)
    "_decay": 0.009,          # ~0.60s RT60 (calibrated: 0.010→0.69s, 0.005→N/A)
    "_predelay": 0.0,         # 0ms
    "_locut": 0.0,            # 0 Hz
    "_hicut": 0.57,           # ~9300 Hz (approximate)
    "_earlysize": 0.295,      # 29.5ms (approximate normalized)
    "_earlycross": 0.0,       # 0.00
    "_earlymodrate": 0.5,     # 1.07 Hz (approximate normalized)
    "_earlymoddepth": 0.0,    # 0.00
    "_earlysend": 0.0,        # 0.00
    "_diffusion": 0.0,        # 0.0%
    "_space": 0.0,            # 0.0%
    "_latesize": 0.09,        # 0.09
    "_latecross": 0.88,       # 0.88
    "_latemodrate": 0.42,     # 0.42Hz (approximate normalized)
    "_latemoddepth": 0.12,    # 0.12
    "_rtbassmultiply": 0.45,  # 0.90X (0.5 = 1.0X, so 0.90X ≈ 0.45)
    "_rtxover": 0.40,         # 1070Hz (approximate normalized)
    "_rthighmultiply": 0.385, # 0.77X (0.5 = 1.0X, so 0.77X ≈ 0.385)
    "_rthighxover": 0.50,     # 7450Hz (approximate normalized)
}

# DuskVerb Room — matched to user's screenshot settings
DV_ROOM_MATCHED = {
    "algorithm": "Room",
    "decay_time": 0.6,
    "size": 0.295,
    "diffusion": 1.0,
    "mod_depth": 0.0,
    "mod_rate": 1.06,
    "treble_multiply": 0.77,
    "bass_multiply": 0.90,
    "crossover": 1070,
    "early_ref_level": 0.0,
    "early_ref_size": 0.296,
    "pre_delay": 0.0,
    "lo_cut": 20,
    "hi_cut": 9320,
    "width": 1.0,
    "dry_wet": 1.0,         # 100% wet for fair comparison
}


def print_section(title):
    print(f"\n{'='*70}")
    print(f"  {title}")
    print(f"{'='*70}")


def main():
    # Load plugins
    dv_path = find_plugin(DUSKVERB_PATHS)
    vr_path = find_plugin(REFERENCE_ROOM_PATHS)

    if not dv_path:
        print("ERROR: DuskVerb not found")
        return

    dv = load_plugin(dv_path)
    if not dv:
        return

    vr = load_plugin(vr_path) if vr_path else None

    # -----------------------------------------------------------------------
    # DIAGNOSTIC: Show effective decay time
    # -----------------------------------------------------------------------
    print_section("Configuration Diagnostic")
    print(f"  DuskVerb Room decayTimeScale = 10.0")
    print(f"  User decay_time = {DV_ROOM_MATCHED['decay_time']:.1f}s")
    print(f"  Effective RT60 = {DV_ROOM_MATCHED['decay_time'] * 10.0:.1f}s")
    print(f"  >>> This is likely WAY too long compared to VRoom's 0.60s decay!")
    print()
    print(f"  DuskVerb dual-slope: ratio=0.15, fastCount=8, gain=20")
    print(f"  Fast-group effective RT60 = {DV_ROOM_MATCHED['decay_time'] * 10.0 * 0.15:.1f}s")
    print(f"  Slow-group effective RT60 = {DV_ROOM_MATCHED['decay_time'] * 10.0:.1f}s")
    print()
    print(f"  Target: ReferenceRoom SmallWoodRoom has ~0.6s decay.")

    # -----------------------------------------------------------------------
    # Configure plugins
    # -----------------------------------------------------------------------
    apply_duskverb_params(dv, DV_ROOM_MATCHED)
    flush(dv)

    if vr:
        apply_reference_room_params(vr, VROOM_SMALLWOODROOM)
        flush(vr)

    # -----------------------------------------------------------------------
    # 1. Level comparison (wet gain)
    # -----------------------------------------------------------------------
    print_section("1. Absolute Wet Gain (100% wet, pink noise)")
    in_db, dv_out, dv_gain = measure_wet_gain(dv)
    print(f"  DuskVerb:     input={in_db:+.1f}dB  output={dv_out:+.1f}dB  gain={dv_gain:+.1f}dB")

    if vr:
        flush(vr)
        _, vr_out, vr_gain = measure_wet_gain(vr)
        print(f"  ReferenceRoom: input={in_db:+.1f}dB  output={vr_out:+.1f}dB  gain={vr_gain:+.1f}dB")
        print(f"  Delta: {dv_gain - vr_gain:+.1f}dB")

    # -----------------------------------------------------------------------
    # 2. Impulse response + RT60
    # -----------------------------------------------------------------------
    print_section("2. RT60 per Octave Band")
    flush(dv)
    dv_ir_l, dv_ir_r = render_ir(dv, 3.0)

    dv_rt60 = metrics.measure_rt60_per_band(dv_ir_l, SR)
    print(f"  {'Band':>10s}  {'DV RT60':>8s}  {'VR RT60':>8s}  {'Ratio':>7s}")
    print(f"  {'-'*40}")

    vr_rt60 = {}
    if vr:
        flush(vr)
        vr_ir_l, vr_ir_r = render_ir(vr, 3.0)
        vr_rt60 = metrics.measure_rt60_per_band(vr_ir_l, SR)

    for band in ["125 Hz", "250 Hz", "500 Hz", "1 kHz", "2 kHz", "4 kHz", "8 kHz"]:
        dv_val = dv_rt60.get(band)
        vr_val = vr_rt60.get(band)
        dv_str = f"{dv_val:.2f}s" if dv_val else "N/A"
        vr_str = f"{vr_val:.2f}s" if vr_val else "N/A"
        ratio_str = ""
        if dv_val and vr_val and vr_val > 0:
            ratio_str = f"{dv_val/vr_val:.2f}x"
        print(f"  {band:>10s}  {dv_str:>8s}  {vr_str:>8s}  {ratio_str:>7s}")

    # -----------------------------------------------------------------------
    # 3. EDC shape
    # -----------------------------------------------------------------------
    print_section("3. Energy Decay Curve (Schroeder EDC)")
    edc_times = [0.1, 0.2, 0.5, 1.0, 1.5, 2.0]
    dv_edc = compute_edc_at_times(dv_ir_l, edc_times)
    vr_edc = compute_edc_at_times(vr_ir_l, edc_times) if vr else {}

    print(f"  {'Time':>6s}  {'DV EDC':>8s}  {'VR EDC':>8s}  {'Delta':>7s}")
    print(f"  {'-'*35}")
    for t in edc_times:
        dv_val = dv_edc.get(t, -200)
        vr_val = vr_edc.get(t, -200)
        delta = ""
        if vr_val > -100 and dv_val > -100:
            delta = f"{dv_val - vr_val:+.1f}"
        dv_str = f"{dv_val:.1f}" if dv_val > -100 else "< -100"
        vr_str = f"{vr_val:.1f}" if vr_val > -100 else "< -100"
        print(f"  {t:6.1f}s  {dv_str:>8s}  {vr_str:>8s}  {delta:>7s}")

    # -----------------------------------------------------------------------
    # 4. Slapback / discrete echo detection
    # -----------------------------------------------------------------------
    print_section("4. Slapback / Discrete Echo Analysis")
    dv_peaks = find_slapback_peaks(dv_ir_l)
    print(f"  DuskVerb Room — top echoes (>{3:.0f}dB prominence):")
    if not dv_peaks:
        print(f"    (none — clean diffuse buildup)")
    else:
        for t, amp, prom in dv_peaks[:6]:
            marker = " *** SLAPBACK" if prom > 6.0 and t > 10 else ""
            print(f"    {t:6.1f}ms  {amp:+6.1f}dB  prominence={prom:.1f}dB{marker}")

    if vr:
        vr_peaks = find_slapback_peaks(vr_ir_l)
        print(f"\n  ReferenceRoom SmallWoodRoom — top echoes:")
        if not vr_peaks:
            print(f"    (none — clean diffuse buildup)")
        else:
            for t, amp, prom in vr_peaks[:6]:
                marker = " *** SLAPBACK" if prom > 6.0 and t > 10 else ""
                print(f"    {t:6.1f}ms  {amp:+6.1f}dB  prominence={prom:.1f}dB{marker}")

    # -----------------------------------------------------------------------
    # 5. Frequency response
    # -----------------------------------------------------------------------
    print_section("5. Spectral Balance (early IR)")
    dv_freq = metrics.frequency_response(dv_ir_l, SR)
    vr_freq = metrics.frequency_response(vr_ir_l, SR) if vr else {}

    print(f"  {'Band':>20s}  {'DV (dB)':>8s}  {'VR (dB)':>8s}  {'Delta':>7s}")
    print(f"  {'-'*50}")
    for band in sorted(dv_freq.keys()):
        dv_val = dv_freq[band]
        vr_val = vr_freq.get(band, -100)
        delta = f"{dv_val - vr_val:+.1f}" if vr_val > -100 else ""
        vr_str = f"{vr_val:.1f}" if vr_val > -100 else "N/A"
        print(f"  {band:>20s}  {dv_val:>7.1f}  {vr_str:>8s}  {delta:>7s}")

    # -----------------------------------------------------------------------
    # 6. Modal ringing
    # -----------------------------------------------------------------------
    print_section("6. Modal Ringing Detection")
    dv_ring = metrics.detect_modal_resonances(dv_ir_l, SR)
    print(f"  DuskVerb:     max prominence = {dv_ring['max_peak_prominence_db']:.1f}dB "
          f"@ {dv_ring['worst_freq_hz']:.0f}Hz")

    if vr:
        vr_ring = metrics.detect_modal_resonances(vr_ir_l, SR)
        print(f"  ReferenceRoom: max prominence = {vr_ring['max_peak_prominence_db']:.1f}dB "
              f"@ {vr_ring['worst_freq_hz']:.0f}Hz")

    # -----------------------------------------------------------------------
    # 7. Echo density build-up
    # -----------------------------------------------------------------------
    print_section("7. Echo Density Build-Up")
    dv_times, dv_kurt = metrics.normalized_echo_density(dv_ir_l, SR)
    print(f"  Kurtosis: Gaussian noise = 0 (fully dense), >3 = sparse/discrete")
    print(f"  {'Time':>8s}  {'DV Kurt':>8s}  {'VR Kurt':>8s}")
    print(f"  {'-'*30}")

    vr_kurt_vals = np.array([])
    vr_times = np.array([])
    if vr:
        vr_times, vr_kurt_vals = metrics.normalized_echo_density(vr_ir_l, SR)

    for target_t in [0.025, 0.050, 0.100, 0.200, 0.500, 1.000]:
        dv_k = ""
        if len(dv_times) > 0:
            idx = np.argmin(np.abs(dv_times - target_t))
            if abs(dv_times[idx] - target_t) < 0.02:
                dv_k = f"{dv_kurt[idx]:.2f}"
        vr_k = ""
        if len(vr_times) > 0:
            idx = np.argmin(np.abs(vr_times - target_t))
            if abs(vr_times[idx] - target_t) < 0.02:
                vr_k = f"{vr_kurt_vals[idx]:.2f}"
        print(f"  {target_t:8.3f}s  {dv_k:>8s}  {vr_k:>8s}")

    # -----------------------------------------------------------------------
    # 8. Tail smoothness
    # -----------------------------------------------------------------------
    print_section("8. Tail Smoothness")
    dv_smooth = metrics.tail_smoothness(dv_ir_l, SR)
    print(f"  DuskVerb:     envelope_std={dv_smooth['envelope_std_db']:.2f}dB  "
          f"decay_rate={dv_smooth['decay_rate_db_per_sec']:.1f}dB/s")

    if vr:
        vr_smooth = metrics.tail_smoothness(vr_ir_l, SR)
        print(f"  ReferenceRoom: envelope_std={vr_smooth['envelope_std_db']:.2f}dB  "
              f"decay_rate={vr_smooth['decay_rate_db_per_sec']:.1f}dB/s")

    # -----------------------------------------------------------------------
    # 9. Stereo
    # -----------------------------------------------------------------------
    print_section("9. Stereo Decorrelation")
    tail_len = min(int(SR * 1), len(dv_ir_l), len(dv_ir_r))
    dv_corr = float(np.corrcoef(dv_ir_l[:tail_len], dv_ir_r[:tail_len])[0, 1])
    print(f"  DuskVerb:     L-R correlation = {dv_corr:.3f}")

    if vr:
        tail_len_vr = min(int(SR * 1), len(vr_ir_l), len(vr_ir_r))
        vr_corr = float(np.corrcoef(vr_ir_l[:tail_len_vr], vr_ir_r[:tail_len_vr])[0, 1])
        print(f"  ReferenceRoom: L-R correlation = {vr_corr:.3f}")

    # -----------------------------------------------------------------------
    # Summary
    # -----------------------------------------------------------------------
    print_section("Summary: Key Differences")
    print(f"  DuskVerb Room has decayTimeScale=10.0, so UI decay 0.6s → effective 6.0s")
    print(f"  Plus dual-slope (fast=0.9s, slow=6.0s) makes it a long reverb at any setting.")
    print(f"  ReferenceRoom Dark Room is a true short room reverb with 0.6s decay.")
    print()
    print(f"  To make DuskVerb Room match short room presets, consider:")
    print(f"    1. Lower decayTimeScale (10.0 → 1.0 or 2.0) to preserve short decay range")
    print(f"    2. Disable dual-slope for short decay (<2s UI) to avoid long tail artifacts")
    print(f"    3. Reduce late gain for short settings (dual-slope gain=20 is too aggressive)")
    print(f"    4. Match the hi-cut / damping curve to VRoom's Dark Room character")
    print()


if __name__ == "__main__":
    main()
