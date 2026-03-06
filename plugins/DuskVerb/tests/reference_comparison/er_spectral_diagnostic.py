#!/usr/bin/env python3
"""
ER vs Tail spectral diagnostic: isolate where DuskVerb's darkness originates.

Processes pink noise through both DuskVerb and ReferenceReverb with very short decay
(ER character only, minimal tail). Compares 1/3-octave spectral energy in two
time windows:
  - ER region (0-150ms): captures early reflections and input filtering
  - Tail region (150ms-2s): residual FDN/late reverb tail

If DV is dark in the ER region → input bandwidth filter or ER air absorption.
If DV matches in ER but dark in tail → FDN damping / trebleMultScale issue.
"""

import numpy as np
from scipy.signal import butter, sosfilt
from pedalboard import load_plugin

from config import (SAMPLE_RATE, find_plugin, DUSKVERB_PATHS, REFERENCE_REVERB_PATHS,
                    apply_duskverb_params, apply_reference_params)
from generate_test_signals import make_noise_burst

SR = SAMPLE_RATE

# Algorithm/mode pairs to test
TEST_PAIRS = [
    {
        "name": "Hall",
        "dv_algo": "Hall",
        "vv_mode": 0.0417,       # Concert Hall
        "vv_label": "Concert Hall",
    },
    {
        "name": "Plate",
        "dv_algo": "Plate",
        "vv_mode": 0.0833,
        "vv_label": "Plate",
    },
    {
        "name": "Room",
        "dv_algo": "Room",
        "vv_mode": 0.1250,
        "vv_label": "Room",
    },
    {
        "name": "Chamber",
        "dv_algo": "Chamber",
        "vv_mode": 0.1667,
        "vv_label": "Chamber",
    },
    {
        "name": "Ambient",
        "dv_algo": "Ambient",
        "vv_mode": 0.2917,
        "vv_label": "Ambience",
    },
]

# 1/3-octave center frequencies (ISO 266)
BANDS_HZ = [125, 160, 200, 250, 315, 400, 500, 630, 800,
            1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000,
            6300, 8000, 10000, 12500, 16000]


def process_stereo(plugin, mono_signal, sr):
    stereo_in = np.stack([mono_signal, mono_signal], axis=0).astype(np.float32)
    output = plugin(stereo_in, sr)
    return output[0], output[1]


def flush_plugin(plugin, sr, duration_sec=2.0):
    silence = np.zeros(int(sr * duration_sec), dtype=np.float32)
    process_stereo(plugin, silence, sr)


def band_energy_db(signal, sr, center_hz):
    """Measure RMS energy in a 1/3-octave band around center_hz."""
    factor = 2 ** (1 / 6)  # half a third-octave
    f_lo = center_hz / factor
    f_hi = center_hz * factor
    nyq = sr / 2.0

    # Clamp to valid range
    f_lo = max(f_lo, 10.0)
    f_hi = min(f_hi, nyq * 0.95)

    if f_hi <= f_lo:
        return -120.0

    sos = butter(4, [f_lo / nyq, f_hi / nyq], btype='band', output='sos')
    filtered = sosfilt(sos, signal)
    rms = np.sqrt(np.mean(filtered ** 2))
    if rms < 1e-12:
        return -120.0
    return 20.0 * np.log10(rms)


def configure_dv_for_er(plugin, algo, decay=0.3):
    """Configure DuskVerb for ER isolation: short decay, flat EQ, full ERs."""
    apply_duskverb_params(plugin, {
        "algorithm": algo,
        "decay_time": decay,
        "size": 0.5,
        "diffusion": 0.7,
        "treble_multiply": 1.0,    # Max brightness
        "bass_multiply": 1.0,
        "crossover": 500,
        "mod_depth": 0.0,          # Off for clean measurement
        "mod_rate": 0.5,
        "early_ref_level": 1.0,    # Full ERs
        "early_ref_size": 0.5,
        "pre_delay": 0.0,
        "lo_cut": 20,
        "hi_cut": 20000,
        "width": 1.0,
    })


def configure_vv_for_er(plugin, mode_float, decay=0.05):
    """Configure ReferenceReverb for ER isolation: short decay, flat EQ."""
    apply_reference_params(plugin, {
        "_reverbmode": mode_float,
        "_colormode": 0.666667,    # "Now" — brightest/flattest
        "_decay": decay,
        "_size": 0.5,
        "_predelay": 0.0,
        "_diffusion_early": 0.7,
        "_diffusion_late": 0.7,
        "_mod_rate": 0.0,
        "_mod_depth": 0.0,
        "_high_cut": 1.0,          # Wide open (20kHz)
        "_low_cut": 0.0,           # Wide open (20Hz)
        "_bassmult": 0.5,          # Unity
        "_bassxover": 0.5,
        "_highshelf": 0.0,         # No HF damping
        "_highfreq": 0.5,
        "_attack": 0.5,
    })


def run_diagnostic(dv, vv, dv_decay, vv_decay, signal_dur, tail_window_end, label):
    """Run spectral diagnostic at a specific decay setting."""
    # Generate pink noise burst (100ms)
    noise = make_noise_burst(burst_ms=100)
    signal = np.zeros(int(SR * signal_dur), dtype=np.float32)
    signal[:len(noise)] = noise[:min(len(noise), len(signal))]

    # Time windows (in samples)
    er_end = int(SR * 0.150)      # 0-150ms
    tail_start = int(SR * 0.150)
    tail_end = int(SR * tail_window_end)

    print(f"\n{'='*80}")
    print(f"ER SPECTRAL DIAGNOSTIC: {label}")
    print(f"DV decay_time={dv_decay} | VV decay={vv_decay}")
    print(f"Tail window: 150ms-{tail_window_end}s")
    print(f"{'='*80}")

    dark_threshold = -3.0

    # Collect broadband tail diffs for summary
    tail_diffs = {}

    for pair in TEST_PAIRS:
        print(f"\n{'─'*80}")
        print(f"Algorithm: {pair['name']} (VV: {pair['vv_label']})")
        print(f"{'─'*80}")

        flush_plugin(dv, SR, 3.0)
        configure_dv_for_er(dv, pair["dv_algo"], decay=dv_decay)
        flush_plugin(dv, SR, 3.0)
        dv_l, dv_r = process_stereo(dv, signal, SR)

        flush_plugin(vv, SR, 3.0)
        configure_vv_for_er(vv, pair["vv_mode"], decay=vv_decay)
        flush_plugin(vv, SR, 3.0)
        vv_l, vv_r = process_stereo(vv, signal, SR)

        dv_er = dv_l[:er_end]
        dv_tail = dv_l[tail_start:tail_end]
        vv_er = vv_l[:er_end]
        vv_tail = vv_l[tail_start:tail_end]

        dv_tail_rms = 20 * np.log10(max(np.sqrt(np.mean(dv_tail**2)), 1e-12))
        vv_tail_rms = 20 * np.log10(max(np.sqrt(np.mean(vv_tail**2)), 1e-12))
        tail_diff_bb = dv_tail_rms - vv_tail_rms
        tail_diffs[pair["name"]] = tail_diff_bb

        print(f"  Broadband tail RMS: DV={dv_tail_rms:+.1f} dB  VV={vv_tail_rms:+.1f} dB  diff={tail_diff_bb:+.1f} dB")

        # Per-band (tail only — ER already validated)
        header = f"  {'Band':>8s}    {'DV(dB)':>7s}  {'VV(dB)':>7s}  {'Diff':>6s}"
        print(f"\n  {'':>8s}    {'--- Tail Region ---':^22s}")
        print(header)
        print(f"  {'─'*8}    {'─'*7}  {'─'*7}  {'─'*6}")

        tail_dark_bands = []
        for fc in BANDS_HZ:
            if fc > SR / 2 * 0.9:
                continue
            dv_tail_e = band_energy_db(dv_tail, SR, fc)
            vv_tail_e = band_energy_db(vv_tail, SR, fc)
            diff = dv_tail_e - vv_tail_e
            flag = " DARK" if diff < dark_threshold else ""
            if diff < dark_threshold:
                tail_dark_bands.append((fc, diff))
            print(f"  {fc:>7d}    {dv_tail_e:>+7.1f}  {vv_tail_e:>+7.1f}  {diff:>+6.1f}{flag}")

        print()
        if tail_dark_bands:
            worst = min(tail_dark_bands, key=lambda x: x[1])
            print(f"  DARK at {len(tail_dark_bands)} bands (worst: {worst[0]} Hz at {worst[1]:+.1f} dB)")
        else:
            print(f"  MATCHED (no bands >3 dB dark)")

    return tail_diffs


def main():
    import argparse
    parser = argparse.ArgumentParser(description="ER vs Tail spectral diagnostic")
    parser.add_argument("--decay-compare", action="store_true",
                        help="Run short (0.3s) vs long (2.0s) comparison")
    args = parser.parse_args()

    dv_path = find_plugin(DUSKVERB_PATHS)
    vv_path = find_plugin(REFERENCE_REVERB_PATHS)
    if not dv_path or not vv_path:
        print(f"ERROR: {'DuskVerb' if not dv_path else 'ReferenceReverb'} not found")
        return

    print(f"DuskVerb:     {dv_path}")
    print(f"ReferenceReverb:  {vv_path}")

    dv = load_plugin(dv_path, parameter_values={"dry_wet": 1.0})
    vv = load_plugin(vv_path)

    if args.decay_compare:
        # Short decay: 0.3s DV / 0.05 VV — tail window 150ms-2s
        short_diffs = run_diagnostic(dv, vv, dv_decay=0.3, vv_decay=0.05,
                                     signal_dur=3.0, tail_window_end=2.0,
                                     label="SHORT DECAY (0.3s)")

        # Long decay: 2.0s DV / 0.25 VV — tail window 150ms-4s
        long_diffs = run_diagnostic(dv, vv, dv_decay=2.0, vv_decay=0.25,
                                    signal_dur=6.0, tail_window_end=4.0,
                                    label="LONG DECAY (2.0s)")

        # Summary comparison
        print(f"\n{'='*80}")
        print("DECAY SCALING SUMMARY: Broadband tail diff (DV - VV) in dB")
        print(f"{'='*80}")
        print(f"  {'Algorithm':>12s}    {'Short (0.3s)':>12s}    {'Long (2.0s)':>12s}    {'Delta':>8s}   Interpretation")
        print(f"  {'─'*12}    {'─'*12}    {'─'*12}    {'─'*8}   {'─'*20}")
        for name in [p["name"] for p in TEST_PAIRS]:
            s = short_diffs.get(name, 0)
            l = long_diffs.get(name, 0)
            delta = l - s
            if abs(delta) < 2.0:
                interp = "CONSTANT → static gain"
            elif delta > 0:
                interp = "CONVERGES → feedback coeff"
            else:
                interp = "DIVERGES → compounding loss"
            print(f"  {name:>12s}    {s:>+12.1f}    {l:>+12.1f}    {delta:>+8.1f}   {interp}")

        print(f"\n  If delta ≈ 0: gap is constant → fix with lateGainScale (one-line)")
        print(f"  If delta > 0: gap shrinks at long decay → feedback coefficient issue")
    else:
        run_diagnostic(dv, vv, dv_decay=0.3, vv_decay=0.05,
                       signal_dur=3.0, tail_window_end=2.0,
                       label="SHORT DECAY (0.3s)")

    print(f"\n{'='*80}")
    print("DIAGNOSTIC COMPLETE")
    print(f"{'='*80}")


if __name__ == "__main__":
    main()
