#!/usr/bin/env python3
"""
FDN Calibration: RT60-matched energy density comparison.

For each algorithm × 5 VV decay settings:
1. Measure VV's actual RT60 @ 500 Hz
2. Binary search DV decay_time until RT60 matches within 10%
3. At matched RT60, measure broadband tail RMS (200ms-1000ms window)
4. Report energy difference (DV - VV)

This determines whether the tail energy deficit is a static gain issue
(constant offset → fix lateGainScale) or a feedback coefficient issue
(offset scales with RT60 → fix coefficient formula).
"""

import sys
import numpy as np
from pedalboard import load_plugin

from config import (SAMPLE_RATE, find_plugin, DUSKVERB_PATHS, REFERENCE_REVERB_PATHS,
                    apply_duskverb_params, apply_reference_params)
from generate_test_signals import make_impulse
import reverb_metrics as metrics

SR = SAMPLE_RATE

# Algorithm/mode pairs
TEST_PAIRS = [
    {"name": "Hall",    "dv_algo": "Hall",    "vv_mode": 0.0417, "vv_label": "Concert Hall"},
    {"name": "Plate",   "dv_algo": "Plate",   "vv_mode": 0.0833, "vv_label": "Plate"},
    {"name": "Room",    "dv_algo": "Room",    "vv_mode": 0.1250, "vv_label": "Room"},
    {"name": "Chamber", "dv_algo": "Chamber", "vv_mode": 0.1667, "vv_label": "Chamber"},
    {"name": "Ambient", "dv_algo": "Ambient", "vv_mode": 0.2917, "vv_label": "Ambience"},
]

# VV decay settings to sweep — produce a range of RT60s per mode
VV_DECAY_SETTINGS = [0.05, 0.10, 0.15, 0.25, 0.40]


def process_stereo(plugin, mono_signal, sr):
    stereo_in = np.stack([mono_signal, mono_signal], axis=0).astype(np.float32)
    output = plugin(stereo_in, sr)
    return output[0], output[1]


def flush_plugin(plugin, sr, duration_sec=2.0):
    silence = np.zeros(int(sr * duration_sec), dtype=np.float32)
    process_stereo(plugin, silence, sr)


def measure_rt60_500(ir, sr):
    """Measure RT60 at 500 Hz. Returns seconds or None."""
    result = metrics.measure_rt60_per_band(ir, sr, {"500 Hz": 500})
    return result.get("500 Hz")


def tail_rms_db(ir, sr, start_s=0.200, end_s=1.000):
    """Broadband RMS in dB for a time window of the IR."""
    s = int(sr * start_s)
    e = min(int(sr * end_s), len(ir))
    if e <= s:
        return -120.0
    segment = ir[s:e]
    rms = np.sqrt(np.mean(segment.astype(np.float64) ** 2))
    if rms < 1e-12:
        return -120.0
    return 20.0 * np.log10(rms)


def calibrate_dv_to_rt60(dv_plugin, target_rt60, dv_params, sr,
                          signal_duration=12.0, iterations=12):
    """Binary search DV decay_time to match target RT60 at 500 Hz.
    Returns (best_decay_time, measured_rt60, ir_left)."""
    impulse = make_impulse(signal_duration)
    flush_dur = max(2.0, signal_duration * 0.25)
    lo, hi = 0.2, 30.0
    best_decay, best_rt60, best_error = None, None, float('inf')
    best_ir = None

    for _ in range(iterations):
        mid = (lo + hi) / 2.0
        trial = dict(dv_params)
        trial["decay_time"] = mid
        apply_duskverb_params(dv_plugin, trial)
        flush_plugin(dv_plugin, sr, flush_dur)
        out_l, _ = process_stereo(dv_plugin, impulse, sr)
        rt60 = measure_rt60_500(out_l, sr)

        if rt60 is None or rt60 <= 0:
            hi = mid
            continue

        error = abs(rt60 / target_rt60 - 1.0)
        if error < best_error:
            best_error = error
            best_decay = mid
            best_rt60 = rt60
            best_ir = out_l

        if error < 0.05:  # 5% tolerance
            return mid, rt60, out_l

        if rt60 > target_rt60:
            hi = mid
        else:
            lo = mid

    return best_decay, best_rt60, best_ir


def main():
    dv_path = find_plugin(DUSKVERB_PATHS)
    vv_path = find_plugin(REFERENCE_REVERB_PATHS)
    if not dv_path or not vv_path:
        print(f"ERROR: {'DuskVerb' if not dv_path else 'ReferenceReverb'} not found")
        return

    print(f"DuskVerb:     {dv_path}")
    print(f"ReferenceReverb:  {vv_path}")

    dv = load_plugin(dv_path, parameter_values={"dry_wet": 1.0})
    vv = load_plugin(vv_path)

    print(f"\n{'='*80}")
    print("FDN CALIBRATION: RT60-Matched Energy Comparison")
    print(f"Energy window: 200ms-1000ms | RT60 band: 500 Hz")
    print(f"{'='*80}")

    # Collect summary data for final analysis
    all_results = {}  # algo -> list of (vv_rt60, energy_diff)

    for pair in TEST_PAIRS:
        algo = pair["name"]
        all_results[algo] = []

        # Signal duration: Room needs longer due to decayTimeScale=10x
        is_room = (algo == "Room")
        sig_dur = 40.0 if is_room else 12.0

        print(f"\n{'─'*80}")
        print(f"Algorithm: {algo} (VV: {pair['vv_label']})")
        print(f"{'─'*80}")
        print(f"  {'VV decay':>9s}  {'VV RT60':>8s}  {'DV decay':>9s}  {'DV RT60':>8s}  "
              f"{'Match%':>7s}  {'DV tail':>8s}  {'VV tail':>8s}  {'Diff':>7s}")
        print(f"  {'─'*9}  {'─'*8}  {'─'*9}  {'─'*8}  {'─'*7}  {'─'*8}  {'─'*8}  {'─'*7}")

        # DV base params: flat EQ, no mod, no ER
        dv_base = {
            "algorithm": pair["dv_algo"],
            "decay_time": 1.0,
            "size": 0.5,
            "diffusion": 0.7,
            "treble_multiply": 1.0,
            "bass_multiply": 1.0,
            "crossover": 500,
            "mod_depth": 0.0,
            "mod_rate": 0.5,
            "early_ref_level": 0.0,
            "early_ref_size": 0.5,
            "pre_delay": 0.0,
            "lo_cut": 20,
            "hi_cut": 20000,
            "width": 1.0,
        }

        for vv_decay in VV_DECAY_SETTINGS:
            # 1. Measure VV RT60
            vv_config = {
                "_reverbmode": pair["vv_mode"],
                "_colormode": 0.666667,
                "_decay": vv_decay,
                "_size": 0.5,
                "_predelay": 0.0,
                "_diffusion_early": 0.7,
                "_diffusion_late": 0.7,
                "_mod_rate": 0.0,
                "_mod_depth": 0.0,
                "_high_cut": 1.0,
                "_low_cut": 0.0,
                "_bassmult": 0.5,
                "_bassxover": 0.5,
                "_highshelf": 0.0,
                "_highfreq": 0.5,
                "_attack": 0.5,
            }

            impulse = make_impulse(sig_dur)
            apply_reference_params(vv, vv_config)
            flush_plugin(vv, SR, 3.0)
            vv_ir, _ = process_stereo(vv, impulse, SR)
            flush_plugin(vv, SR, 3.0)
            vv_rt60 = measure_rt60_500(vv_ir, SR)

            if vv_rt60 is None or vv_rt60 <= 0:
                print(f"  {vv_decay:>9.2f}  {'N/A':>8s}  {'---':>9s}  {'---':>8s}  "
                      f"{'---':>7s}  {'---':>8s}  {'---':>8s}  {'---':>7s}")
                continue

            # 2. Calibrate DV to match VV RT60
            dv_decay, dv_rt60, dv_ir = calibrate_dv_to_rt60(
                dv, vv_rt60, dv_base, SR, sig_dur)

            if dv_decay is None or dv_rt60 is None or dv_ir is None:
                print(f"  {vv_decay:>9.2f}  {vv_rt60:>7.2f}s  {'FAIL':>9s}  {'---':>8s}  "
                      f"{'---':>7s}  {'---':>8s}  {'---':>8s}  {'---':>7s}")
                continue

            match_pct = 100.0 * (1.0 - abs(dv_rt60 / vv_rt60 - 1.0))

            # 3. Measure tail energy at matched RT60
            # Adaptive window: use 200ms-1000ms for short RT60, extend for long
            tail_end = min(max(1.0, vv_rt60 * 0.5), 4.0)
            dv_energy = tail_rms_db(dv_ir, SR, 0.200, tail_end)
            vv_energy = tail_rms_db(vv_ir, SR, 0.200, tail_end)
            energy_diff = dv_energy - vv_energy

            all_results[algo].append((vv_rt60, energy_diff, match_pct))

            print(f"  {vv_decay:>9.2f}  {vv_rt60:>7.2f}s  {dv_decay:>9.2f}  {dv_rt60:>7.2f}s  "
                  f"{match_pct:>6.1f}%  {dv_energy:>+7.1f}  {vv_energy:>+7.1f}  {energy_diff:>+6.1f}")

    # Summary analysis
    print(f"\n{'='*80}")
    print("SCALING ANALYSIS: Does energy diff change with RT60?")
    print(f"{'='*80}")
    print(f"  {'Algorithm':>12s}  {'Short RT60':>10s}  {'Short diff':>10s}  "
          f"{'Long RT60':>10s}  {'Long diff':>10s}  {'Delta':>7s}  {'Diagnosis':>20s}")
    print(f"  {'─'*12}  {'─'*10}  {'─'*10}  {'─'*10}  {'─'*10}  {'─'*7}  {'─'*20}")

    for algo in [p["name"] for p in TEST_PAIRS]:
        pts = all_results.get(algo, [])
        valid = [(rt60, diff, m) for rt60, diff, m in pts if m > 85.0]  # good matches only

        if len(valid) < 2:
            print(f"  {algo:>12s}  {'---':>10s}  {'---':>10s}  {'---':>10s}  {'---':>10s}  "
                  f"{'---':>7s}  {'INSUFFICIENT DATA':>20s}")
            continue

        # Shortest and longest RT60 with good match
        valid.sort(key=lambda x: x[0])
        short_rt60, short_diff, _ = valid[0]
        long_rt60, long_diff, _ = valid[-1]
        delta = long_diff - short_diff

        if abs(delta) < 2.0:
            diagnosis = "CONSTANT -> gain"
        elif delta > 0:
            diagnosis = "CONVERGES -> feedback"
        else:
            diagnosis = "DIVERGES -> compound"

        print(f"  {algo:>12s}  {short_rt60:>9.2f}s  {short_diff:>+9.1f}  "
              f"{long_rt60:>9.2f}s  {long_diff:>+9.1f}  {delta:>+6.1f}  {diagnosis:>20s}")

    # Overall recommendation
    print(f"\n  Interpretation:")
    print(f"    CONSTANT (delta < 2 dB): Static gain mismatch → adjust lateGainScale")
    print(f"    CONVERGES (delta > 0):   Feedback coefficient issue → fix formula")
    print(f"    DIVERGES (delta < -2):   Compounding energy loss → investigate loop")

    print(f"\n{'='*80}")
    print("CALIBRATION COMPLETE")
    print(f"{'='*80}")


if __name__ == "__main__":
    main()
