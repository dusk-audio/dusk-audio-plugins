#!/usr/bin/env python3
"""Level calibration: DV vs VV at matched knob positions (no binary search).

For each algorithm × 3 decay settings, sets both plugins to equivalent
parameters and compares RMS output levels in two windows:
  - 0-0.5s (transient + early tail)
  - 0.5-2.0s (steady-state tail)

This reveals whether DV is louder or quieter than VV at identical knob
positions, and whether the offset is constant (→ lateGainScale fix) or
decay-dependent (→ shortDecayBoost fix).
"""

import sys
import os
import numpy as np
from pedalboard import load_plugin

sys.path.insert(0, os.path.dirname(__file__))
from config import (SAMPLE_RATE, find_plugin, DUSKVERB_PATHS, VINTAGEVERB_PATHS,
                    apply_duskverb_params, apply_valhalla_params)
from generate_test_signals import make_impulse
import reverb_metrics as metrics

SR = SAMPLE_RATE

# VV mode floats for each algorithm
VV_MODES = {
    "Plate":   0.0833,
    "Hall":    0.0417,
    "Chamber": 0.1667,
    "Room":    0.1250,
    "Ambient": 0.2917,
}

# Decay settings to test (short, medium, long)
DECAY_SETTINGS = [1.0, 3.0, 8.0]

# Common parameters
TREBLE = 0.7
SIZE = 0.5
MOD_DEPTH = 0.5
MOD_RATE = 0.3


def process_stereo(plugin, mono_signal, sr):
    stereo_in = np.stack([mono_signal, mono_signal], axis=0).astype(np.float32)
    out = plugin(stereo_in, sr)
    return out[0], out[1]


def flush_plugin(plugin, sr, dur=3.0):
    silence = np.zeros(int(sr * dur), dtype=np.float32)
    process_stereo(plugin, silence, sr)


def rms_db(signal, sr, start_s, end_s):
    """RMS in dB for a time window."""
    s = int(sr * start_s)
    e = min(int(sr * end_s), len(signal))
    if e <= s:
        return -120.0
    seg = signal[s:e].astype(np.float64)
    rms = np.sqrt(np.mean(seg ** 2))
    return 20.0 * np.log10(max(rms, 1e-30))


def main():
    dv_path = find_plugin(DUSKVERB_PATHS)
    vv_path = find_plugin(VINTAGEVERB_PATHS)
    if not dv_path or not vv_path:
        print("ERROR: Plugin(s) not found")
        sys.exit(1)

    print(f"DuskVerb:     {dv_path}")
    print(f"VintageVerb:  {vv_path}")
    print(f"Sample rate:  {SR} Hz")
    print(f"Settings:     treble={TREBLE}, size={SIZE}, mod_depth={MOD_DEPTH}")
    print()

    dv = load_plugin(dv_path)
    vv = load_plugin(vv_path)

    imp = make_impulse(12.0)

    # Collect results for summary
    results = []

    for algo in ["Plate", "Hall", "Chamber", "Room", "Ambient"]:
        vv_mode = VV_MODES[algo]
        print(f"{'='*70}")
        print(f"  {algo}")
        print(f"{'='*70}")
        print(f"  {'Decay':>6s}  {'VV RMS 0-0.5s':>14s}  {'DV RMS 0-0.5s':>14s}  {'Δ early':>8s}  "
              f"{'VV RMS 0.5-2s':>14s}  {'DV RMS 0.5-2s':>14s}  {'Δ tail':>8s}  {'VV RT60':>8s}  {'DV RT60':>8s}")
        print(f"  {'-'*110}")

        for decay in DECAY_SETTINGS:
            # VV: set parameters
            # VV decay knob is 0-1, nonlinear. We use a simple mapping:
            # decay_seconds → approximate VV knob position.
            # From our earlier measurements: VV Room decay=0.30 → RT60=1.5s, 0.50→7.0s
            # This mapping is mode-dependent but we'll use the same raw value for comparison
            # Actually, VV's "decay" is a 0-1 knob, not seconds. For fair comparison,
            # we need to map DV's decay_time (seconds) to a VV knob position.
            # Instead, let's just set VV to several knob positions and DV to the
            # corresponding decay_time that the translate_preset would produce.
            # For simplicity: use VV decay knob = 0.15, 0.30, 0.50
            vv_decay_knobs = {1.0: 0.15, 3.0: 0.30, 8.0: 0.50}
            vv_decay = vv_decay_knobs[decay]

            vv_config = {
                '_reverbmode': vv_mode,
                '_colormode': 0.333,  # 1980s neutral
                '_decay': vv_decay,
                '_size': SIZE,
                '_predelay': 0.0,
                '_diffusion_early': 0.7,
                '_diffusion_late': 0.7,
                '_mod_rate': MOD_RATE,
                '_mod_depth': MOD_DEPTH,
                '_high_cut': 1.0,
                '_low_cut': 0.0,
                '_bassmult': 0.5,
                '_bassxover': 0.4,
                '_highshelf': 1.0 - TREBLE,  # treble 0.7 → highshelf 0.3
                '_highfreq': 0.5,
            }

            flush_plugin(vv, SR)
            apply_valhalla_params(vv, vv_config)
            flush_plugin(vv, SR)
            vv_l, _ = process_stereo(vv, imp, SR)
            flush_plugin(vv, SR)

            # DV: equivalent parameters
            dv_config = {
                'algorithm': algo,
                'decay_time': decay,
                'room_size': SIZE,
                'mod_depth': MOD_DEPTH,
                'mod_rate': 1.0,
                'treble_multiply': TREBLE,
                'bass_multiply': 1.0,
                'pre_delay': 0.0,
                'crossover': 1000,
                'diffusion': 0.7,
                'lo_cut': 20,
                'hi_cut': 20000,
                'width': 1.0,
            }

            flush_plugin(dv, SR)
            apply_duskverb_params(dv, dv_config)
            flush_plugin(dv, SR)
            dv_l, _ = process_stereo(dv, imp, SR)
            flush_plugin(dv, SR)

            # Measure
            vv_early = rms_db(vv_l, SR, 0, 0.5)
            dv_early = rms_db(dv_l, SR, 0, 0.5)
            delta_early = dv_early - vv_early

            vv_tail = rms_db(vv_l, SR, 0.5, 2.0)
            dv_tail = rms_db(dv_l, SR, 0.5, 2.0)
            delta_tail = dv_tail - vv_tail

            vv_rt60 = metrics.measure_rt60_per_band(vv_l, SR, {'500': 500}).get('500')
            dv_rt60 = metrics.measure_rt60_per_band(dv_l, SR, {'500': 500}).get('500')

            vv_rt_s = f"{vv_rt60:.2f}s" if vv_rt60 else "N/A"
            dv_rt_s = f"{dv_rt60:.2f}s" if dv_rt60 else "N/A"

            print(f"  {decay:5.1f}s  {vv_early:>14.1f}  {dv_early:>14.1f}  {delta_early:>+7.1f}  "
                  f"{vv_tail:>14.1f}  {dv_tail:>14.1f}  {delta_tail:>+7.1f}  {vv_rt_s:>8s}  {dv_rt_s:>8s}")

            results.append({
                'algo': algo, 'decay': decay, 'vv_decay_knob': vv_decay,
                'delta_early': delta_early, 'delta_tail': delta_tail,
                'vv_rt60': vv_rt60, 'dv_rt60': dv_rt60,
            })

        print()

    # Summary
    print(f"\n{'='*70}")
    print(f"  SUMMARY: Average level deltas (DV - VV) in dB")
    print(f"{'='*70}")
    print(f"  {'Algorithm':>10s}  {'Short (1s)':>12s}  {'Medium (3s)':>12s}  {'Long (8s)':>12s}  {'Average':>10s}")
    print(f"  {'-'*60}")

    for algo in ["Plate", "Hall", "Chamber", "Room", "Ambient"]:
        row = [r for r in results if r['algo'] == algo]
        deltas = []
        cells = []
        for r in row:
            d = r['delta_tail']
            deltas.append(d)
            cells.append(f"{d:>+.1f}")
        avg = np.mean(deltas) if deltas else 0
        print(f"  {algo:>10s}  {'  '.join(f'{c:>12s}' for c in cells)}  {avg:>+10.1f}")

    # Recommendations
    print(f"\n  Recommendations:")
    for algo in ["Plate", "Hall", "Chamber", "Room", "Ambient"]:
        row = [r for r in results if r['algo'] == algo]
        deltas = [r['delta_tail'] for r in row]
        avg = np.mean(deltas)
        spread = max(deltas) - min(deltas)

        if abs(avg) < 2.0:
            print(f"    {algo}: OK (avg {avg:+.1f} dB)")
        elif spread < 3.0:
            # Constant offset → lateGainScale
            gain_change = -avg  # positive avg means DV too loud → decrease gain
            print(f"    {algo}: Constant offset {avg:+.1f} dB (spread {spread:.1f}). "
                  f"Adjust lateGainScale by {gain_change:+.1f} dB")
        else:
            # Decay-dependent → shortDecayBoost
            print(f"    {algo}: Decay-dependent offset (spread {spread:.1f} dB). "
                  f"Consider shortDecayBoost adjustment")

    print("\nDone.")


if __name__ == "__main__":
    main()
