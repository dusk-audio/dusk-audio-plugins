#!/usr/bin/env python3
"""
DuskVerb vs ReferenceReverb — specific preset comparison.

Compares a VV preset against DuskVerb with matched settings.
Reports: level, RT60, EDC, slapback, spectral balance, echo density, ringing.

Usage:
    python3 preset_compare.py                    # Run Fat Snare Room comparison
    python3 preset_compare.py --preset fat_snare # Same
"""

import numpy as np
import pedalboard
from config import (
    SAMPLE_RATE, DUSKVERB_PATHS, REFERENCE_REVERB_PATHS,
    find_plugin, apply_duskverb_params, apply_reference_params,
    REFERENCE_PARAM_MAP,
)
import reverb_metrics as metrics

SR = SAMPLE_RATE


def load_plugin(path):
    try:
        return pedalboard.load_plugin(path)
    except Exception as e:
        print(f"ERROR loading {path}: {e}")
        return None


def flush(plugin, dur=5.0):
    silence = np.zeros((2, int(SR * dur)), dtype=np.float32)
    plugin(silence, SR)


def process_stereo(plugin, mono_signal):
    stereo_in = np.stack([mono_signal, mono_signal], axis=0).astype(np.float32)
    out = plugin(stereo_in, SR)
    return out[0], out[1]


def render_ir(plugin, duration=8.0):
    n = int(SR * duration)
    impulse = np.zeros((2, n), dtype=np.float32)
    impulse[0, 0] = 1.0
    impulse[1, 0] = 1.0
    out = plugin(impulse, SR)
    return out[0], out[1]


def measure_wet_gain(plugin, duration=4.0):
    """Send pink noise and measure wet output RMS relative to input RMS."""
    n = int(SR * duration)
    rng = np.random.default_rng(42)
    white = rng.standard_normal(n).astype(np.float32) * 0.1
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
    """Find discrete peaks in IR above local envelope."""
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
    """EDC at specific time points."""
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


def print_section(title):
    print(f"\n{'='*70}")
    print(f"  {title}")
    print(f"{'='*70}")


# ---------------------------------------------------------------------------
# Preset definitions
# ---------------------------------------------------------------------------

# ReferenceReverb "Fat Snare Room" preset
# MODE: Room (0.1250), COLOR: 1980s (0.333)
# From screenshot: size 38.8%, attack 12.4%, early diff 68%, late diff 100%,
# mod rate 1.50Hz, depth 30.8%, hi-cut 4500Hz, lo-cut 10Hz,
# highshelf -24dB, highfreq 6600Hz, bassmult 1.25X, bassxover 630Hz,
# predelay 9ms, decay 0.70s
VV_FAT_SNARE_ROOM = {
    "_reverbmode": 0.1250,      # Room (VV v4.0.5)
    "_colormode": 0.333,        # 1980s
    "_size": 0.388,             # 38.8%
    "_attack": 0.124,           # 12.4%
    "_diffusion_early": 0.68,   # 68%
    "_diffusion_late": 1.0,     # 100%
    "_mod_rate": 0.30,          # ~1.50 Hz (VV mod_rate is 0-1 non-linear)
    "_mod_depth": 0.308,        # 30.8%
    "_high_cut": 0.26,          # ~4500 Hz (low hi-cut = dark)
    "_low_cut": 0.0,            # ~10 Hz (fully open)
    "_highshelf": 0.50,         # -24 dB (heavy HF damping)
    "_highfreq": 0.50,          # ~6600 Hz
    "_bassmult": 0.58,          # 1.25X (slightly above neutral 0.5)
    "_bassxover": 0.25,         # ~630 Hz
    "_predelay": 0.030,         # 9ms (9/300)
    "_decay": 0.70,             # 0.70 as displayed (VV Room: mainly affects level)
}

# DuskVerb Room — matched to user's screenshot settings
DV_FAT_SNARE_ROOM = {
    "algorithm": "Room",
    "decay_time": 0.7,          # 700ms (but decayTimeScale=10 → 7.0s effective!)
    "size": 0.388,              # 38.8%
    "diffusion": 1.0,           # 100%
    "mod_depth": 0.308,         # 30.8%
    "mod_rate": 1.51,           # 1.51 Hz
    "treble_multiply": 1.0,     # 1.00x
    "bass_multiply": 1.25,      # 1.25x
    "crossover": 631,           # 631 Hz
    "early_ref_level": 0.0,     # 0%
    "early_ref_size": 0.296,    # 29.6%
    "pre_delay": 9.0,           # 9ms
    "lo_cut": 20,               # 20 Hz
    "hi_cut": 4540,             # 4.54 kHz
    "width": 1.0,               # 100%
    "dry_wet": 1.0,             # 100% wet for fair comparison
}


def main():
    # Load plugins
    dv_path = find_plugin(DUSKVERB_PATHS)
    vv_path = find_plugin(REFERENCE_REVERB_PATHS)

    if not dv_path:
        print("ERROR: DuskVerb not found"); return
    dv = load_plugin(dv_path)
    if not dv: return

    vv = load_plugin(vv_path) if vv_path else None

    # -----------------------------------------------------------------------
    # DIAGNOSTIC
    # -----------------------------------------------------------------------
    print_section("Configuration: VV Fat Snare Room vs DV Room")
    print(f"  ReferenceReverb: Room mode, 1980s color")
    print(f"    decay=0.70 (display), size=38.8%, predelay=9ms")
    print(f"    hi-cut=4500Hz, highshelf=-24dB, bassmult=1.25X")
    print()
    print(f"  DuskVerb Room:")
    print(f"    decay_time=0.7s × decayTimeScale=10.0 → effective 7.0s")
    print(f"    dual-slope: fast={0.7*10*0.15:.1f}s / slow={0.7*10:.1f}s")
    print(f"    lateGainScale=0.033 (very low, compensates for dual-slope gain=20)")
    print()
    print(f"  VV Room is a long-sustaining mode (~55s RT60). _decay affects")
    print(f"  mainly the level/gain, not the tail length. DV Room's decayTimeScale=10")
    print(f"  was designed to match that ~55s behavior at UI values of ~5-20s.")
    print(f"  At UI=0.7s, the effective 7.0s is much shorter than VV's ~55s.")

    # Configure
    apply_duskverb_params(dv, DV_FAT_SNARE_ROOM)
    flush(dv, 10.0)  # VV Room needs long flush

    if vv:
        apply_reference_params(vv, VV_FAT_SNARE_ROOM)
        flush(vv, 10.0)

    # Use longer IR capture for VV Room (long tails)
    ir_dur = 30.0

    # -----------------------------------------------------------------------
    # 1. Level
    # -----------------------------------------------------------------------
    print_section("1. Absolute Wet Gain (100% wet, pink noise)")
    in_db, dv_out, dv_gain = measure_wet_gain(dv)
    print(f"  DuskVerb:     input={in_db:+.1f}dB  output={dv_out:+.1f}dB  gain={dv_gain:+.1f}dB")

    if vv:
        flush(vv, 10.0)
        _, vv_out, vv_gain = measure_wet_gain(vv)
        print(f"  ReferenceReverb:  input={in_db:+.1f}dB  output={vv_out:+.1f}dB  gain={vv_gain:+.1f}dB")
        print(f"  Delta: {dv_gain - vv_gain:+.1f}dB")

    # -----------------------------------------------------------------------
    # 2. RT60
    # -----------------------------------------------------------------------
    print_section("2. RT60 per Octave Band")
    flush(dv, 10.0)
    dv_ir_l, dv_ir_r = render_ir(dv, ir_dur)

    dv_rt60 = metrics.measure_rt60_per_band(dv_ir_l, SR)

    vv_rt60 = {}
    vv_ir_l = np.zeros(1, dtype=np.float32)
    vv_ir_r = np.zeros(1, dtype=np.float32)
    if vv:
        flush(vv, 10.0)
        vv_ir_l, vv_ir_r = render_ir(vv, ir_dur)
        vv_rt60 = metrics.measure_rt60_per_band(vv_ir_l, SR)

    print(f"  {'Band':>10s}  {'DV RT60':>8s}  {'VV RT60':>8s}  {'Ratio':>7s}")
    print(f"  {'-'*40}")
    for band in ["125 Hz", "250 Hz", "500 Hz", "1 kHz", "2 kHz", "4 kHz", "8 kHz"]:
        dv_val = dv_rt60.get(band)
        vv_val = vv_rt60.get(band)
        dv_str = f"{dv_val:.2f}s" if dv_val else "N/A"
        vv_str = f"{vv_val:.2f}s" if vv_val else "N/A"
        ratio_str = ""
        if dv_val and vv_val and vv_val > 0:
            ratio_str = f"{dv_val/vv_val:.2f}x"
        print(f"  {band:>10s}  {dv_str:>8s}  {vv_str:>8s}  {ratio_str:>7s}")

    # -----------------------------------------------------------------------
    # 3. EDC shape
    # -----------------------------------------------------------------------
    print_section("3. Energy Decay Curve (Schroeder EDC)")
    edc_times = [0.1, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 20.0]
    dv_edc = compute_edc_at_times(dv_ir_l, edc_times)
    vv_edc = compute_edc_at_times(vv_ir_l, edc_times) if vv else {}

    print(f"  {'Time':>6s}  {'DV EDC':>8s}  {'VV EDC':>8s}  {'Delta':>7s}")
    print(f"  {'-'*35}")
    for t in edc_times:
        dv_val = dv_edc.get(t, -200)
        vv_val = vv_edc.get(t, -200)
        delta = ""
        if vv_val > -100 and dv_val > -100:
            delta = f"{dv_val - vv_val:+.1f}"
        dv_str = f"{dv_val:.1f}" if dv_val > -100 else "< -100"
        vv_str = f"{vv_val:.1f}" if vv_val > -100 else "< -100"
        print(f"  {t:6.1f}s  {dv_str:>8s}  {vv_str:>8s}  {delta:>7s}")

    # -----------------------------------------------------------------------
    # 4. Slapback
    # -----------------------------------------------------------------------
    print_section("4. Slapback / Discrete Echo Analysis")
    dv_peaks = find_slapback_peaks(dv_ir_l)
    print(f"  DuskVerb — top echoes:")
    if not dv_peaks:
        print(f"    (none)")
    else:
        for t, amp, prom in dv_peaks[:6]:
            marker = " *** SLAPBACK" if prom > 6.0 and t > 10 else ""
            print(f"    {t:6.1f}ms  {amp:+6.1f}dB  prominence={prom:.1f}dB{marker}")

    if vv:
        vv_peaks = find_slapback_peaks(vv_ir_l)
        print(f"\n  ReferenceReverb Fat Snare Room — top echoes:")
        if not vv_peaks:
            print(f"    (none)")
        else:
            for t, amp, prom in vv_peaks[:6]:
                marker = " *** SLAPBACK" if prom > 6.0 and t > 10 else ""
                print(f"    {t:6.1f}ms  {amp:+6.1f}dB  prominence={prom:.1f}dB{marker}")

    # -----------------------------------------------------------------------
    # 5. Spectral balance
    # -----------------------------------------------------------------------
    print_section("5. Spectral Balance (early IR)")
    dv_freq = metrics.frequency_response(dv_ir_l, SR)
    vv_freq = metrics.frequency_response(vv_ir_l, SR) if vv else {}

    print(f"  {'Band':>20s}  {'DV (dB)':>8s}  {'VV (dB)':>8s}  {'Delta':>7s}")
    print(f"  {'-'*50}")
    for band in sorted(dv_freq.keys()):
        dv_val = dv_freq[band]
        vv_val = vv_freq.get(band, -100)
        delta = f"{dv_val - vv_val:+.1f}" if vv_val > -100 else ""
        vv_str = f"{vv_val:.1f}" if vv_val > -100 else "N/A"
        print(f"  {band:>20s}  {dv_val:>7.1f}  {vv_str:>8s}  {delta:>7s}")

    # -----------------------------------------------------------------------
    # 6. Ringing
    # -----------------------------------------------------------------------
    print_section("6. Modal Ringing")
    dv_ring = metrics.detect_modal_resonances(dv_ir_l, SR)
    print(f"  DuskVerb:     max prominence = {dv_ring['max_peak_prominence_db']:.1f}dB "
          f"@ {dv_ring['worst_freq_hz']:.0f}Hz")
    if vv:
        vv_ring = metrics.detect_modal_resonances(vv_ir_l, SR)
        print(f"  ReferenceReverb:  max prominence = {vv_ring['max_peak_prominence_db']:.1f}dB "
              f"@ {vv_ring['worst_freq_hz']:.0f}Hz")

    # -----------------------------------------------------------------------
    # 7. Echo density
    # -----------------------------------------------------------------------
    print_section("7. Echo Density Build-Up (Kurtosis)")
    dv_times, dv_kurt = metrics.normalized_echo_density(dv_ir_l, SR)
    print(f"  Gaussian=0 (dense), >3 = sparse")
    print(f"  {'Time':>8s}  {'DV Kurt':>8s}  {'VV Kurt':>8s}")
    print(f"  {'-'*30}")

    vr_kurt_vals = np.array([])
    vr_times = np.array([])
    if vv:
        vr_times, vr_kurt_vals = metrics.normalized_echo_density(vv_ir_l, SR)

    for target_t in [0.025, 0.050, 0.100, 0.200, 0.500, 1.000, 2.000]:
        dv_k = ""
        if len(dv_times) > 0:
            idx = np.argmin(np.abs(dv_times - target_t))
            if abs(dv_times[idx] - target_t) < 0.02:
                dv_k = f"{dv_kurt[idx]:.2f}"
        vv_k = ""
        if len(vr_times) > 0:
            idx = np.argmin(np.abs(vr_times - target_t))
            if abs(vr_times[idx] - target_t) < 0.02:
                vv_k = f"{vr_kurt_vals[idx]:.2f}"
        print(f"  {target_t:8.3f}s  {dv_k:>8s}  {vv_k:>8s}")

    # -----------------------------------------------------------------------
    # 8. Stereo
    # -----------------------------------------------------------------------
    print_section("8. Stereo Decorrelation")
    tail_len = min(int(SR * 2), len(dv_ir_l), len(dv_ir_r))
    dv_corr = float(np.corrcoef(dv_ir_l[:tail_len], dv_ir_r[:tail_len])[0, 1])
    print(f"  DuskVerb:     L-R correlation = {dv_corr:.3f}")

    if vv and len(vv_ir_l) > 100:
        tail_len_vv = min(int(SR * 2), len(vv_ir_l), len(vv_ir_r))
        vv_corr = float(np.corrcoef(vv_ir_l[:tail_len_vv], vv_ir_r[:tail_len_vv])[0, 1])
        print(f"  ReferenceReverb:  L-R correlation = {vv_corr:.3f}")

    # -----------------------------------------------------------------------
    # 9. Tail smoothness
    # -----------------------------------------------------------------------
    print_section("9. Tail Smoothness")
    dv_smooth = metrics.tail_smoothness(dv_ir_l, SR)
    print(f"  DuskVerb:     envelope_std={dv_smooth['envelope_std_db']:.2f}dB  "
          f"decay_rate={dv_smooth['decay_rate_db_per_sec']:.1f}dB/s")

    if vv and len(vv_ir_l) > 100:
        vv_smooth = metrics.tail_smoothness(vv_ir_l, SR)
        print(f"  ReferenceReverb:  envelope_std={vv_smooth['envelope_std_db']:.2f}dB  "
              f"decay_rate={vv_smooth['decay_rate_db_per_sec']:.1f}dB/s")

    # -----------------------------------------------------------------------
    # Summary
    # -----------------------------------------------------------------------
    print_section("Diagnosis")
    print(f"  DV Room decayTimeScale=10.0: UI 0.7s → effective 7.0s")
    print(f"  VV Room is inherently long-sustaining (~55s RT60).")
    print(f"  At _decay=0.70, VV Room's character is dominated by the long tail")
    print(f"  and the _decay control mainly affects the initial level/density.")
    print()
    print(f"  Key question: does DV Room at 0.7s UI sound like VV Room at _decay=0.70?")
    print(f"  The metrics above quantify the structural differences.")
    print()


if __name__ == "__main__":
    main()
