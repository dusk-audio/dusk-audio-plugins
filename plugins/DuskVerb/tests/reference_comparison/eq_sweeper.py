#!/usr/bin/env python3
"""
EQ Parameter Sweeper — grid search for optimal spectral balance.

Captures VV's 'Fat Snare Hall' IR as the target, then sweeps DV's
hi_cut, treble_multiply, and bass_multiply to find the parameter
combination that minimizes spectral deviation.

Usage:
    python3 eq_sweeper.py                   # Full grid search
    python3 eq_sweeper.py --coarse          # Quick coarse pass (wider steps)
    python3 eq_sweeper.py --top 5           # Show top 5 results
"""

import argparse
import itertools
import sys
import time
import numpy as np
from pedalboard import load_plugin

from config import (
    SAMPLE_RATE, DUSKVERB_PATHS, REFERENCE_REVERB_PATHS,
    find_plugin, apply_duskverb_params, apply_reference_params,
)
from generate_test_signals import make_impulse
import reverb_metrics as metrics

# ---------------------------------------------------------------------------
# Fat Snare Hall — VV preset parameters (from qualifying_presets.json)
# ---------------------------------------------------------------------------
FAT_SNARE_HALL_VV = {
    "_reverbmode": 0.041667,
    "_colormode": 0.333333,
    "_decay": 0.229916,
    "_size": 0.568,
    "_predelay": 0.19325,
    "_attack": 0.74,
    "_bassmult": 0.56595,
    "_bassxover": 0.421871,
    "_highshelf": 0.0,
    "_highfreq": 0.394882,
    "_diffusion_early": 0.748,
    "_diffusion_late": 1.0,
    "_mod_rate": 0.030303,
    "_mod_depth": 1.0,
    "_high_cut": 0.333511,
    "_low_cut": 0.0,
}

# DV baseline params from the existing preset_suite translation
# (everything except hi_cut, treble_multiply, bass_multiply — those are swept)
DV_BASELINE = {
    "algorithm": "Hall",
    "decay_time": 2.06,         # calibrated from transcoder
    "size": 0.568,
    "diffusion": 0.92,          # 0.3*0.748 + 0.7*1.0 (Hall blend)
    "mod_depth": 1.0,
    "mod_rate": 0.4,            # 9.9*0.030303 + 0.1
    "pre_delay": 11.0,          # 500 * 0.19325^2.32
    "lo_cut": 20,
    "early_ref_level": 0.42,    # max(0.15, 1.2 - 0.74*1.05)
    "early_ref_size": 0.43,     # 0.568 * (0.4 + 0.74*0.6)
    "crossover": 401,
    "width": 1.0,
}

SR = SAMPLE_RATE
SIG_DURATION = 6.0  # shorter IR for speed — enough for spectral shape


def capture_vv_target(vv_plugin):
    """Render VV Fat Snare Hall and return mono IR (left channel)."""
    print("Capturing VV 'Fat Snare Hall' target IR...")
    apply_reference_params(vv_plugin, FAT_SNARE_HALL_VV)

    # Flush
    silence = np.zeros(int(SR * 2), dtype=np.float32)
    stereo_in = np.stack([silence, silence], axis=0).astype(np.float32)
    vv_plugin(stereo_in, SR)

    # Capture
    impulse = make_impulse(SIG_DURATION)
    stereo_in = np.stack([impulse, impulse], axis=0).astype(np.float32)
    output = vv_plugin(stereo_in, SR)
    ir = output[0]
    peak = np.max(np.abs(ir))
    print(f"  VV IR peak: {peak:.6f} ({20*np.log10(max(peak, 1e-10)):.1f} dB)")
    return ir


def render_dv(dv_plugin, hi_cut, treble_multiply, bass_multiply):
    """Render DV with swept parameters and return mono IR."""
    params = dict(DV_BASELINE)
    params["hi_cut"] = hi_cut
    params["treble_multiply"] = treble_multiply
    params["bass_multiply"] = bass_multiply

    apply_duskverb_params(dv_plugin, params)

    # Flush
    silence = np.zeros(int(SR * 1), dtype=np.float32)
    stereo_in = np.stack([silence, silence], axis=0).astype(np.float32)
    dv_plugin(stereo_in, SR)

    # Capture
    impulse = make_impulse(SIG_DURATION)
    stereo_in = np.stack([impulse, impulse], axis=0).astype(np.float32)
    output = dv_plugin(stereo_in, SR)
    return output[0]


def score_ir(dv_ir, vv_ir):
    """Compute spectral metrics between DV and VV IRs.

    Returns (total_mse, spectral_env_mean_dev, spectral_env_max_dev).
    """
    # Spectral MSE (1/3-octave band dB envelope MSE)
    mse_bands = metrics.spectral_mse(dv_ir, vv_ir, SR)
    total_mse = sum(mse_bands.values())

    # Spectral envelope match (shape comparison, level-normalized)
    env_match = metrics.spectral_envelope_match(dv_ir, vv_ir, SR,
                                                 start_ms=100, end_ms=3000)
    mean_dev = env_match["mean_deviation"]
    max_dev = env_match["max_deviation"]

    return total_mse, mean_dev, max_dev


def build_grid(coarse=False):
    """Build parameter grid for sweep."""
    if coarse:
        hi_cuts = list(range(2000, 20001, 2000))
        trebles = [round(x, 1) for x in np.arange(0.1, 1.05, 0.2)]
        basses = [round(x, 1) for x in np.arange(0.5, 2.05, 0.3)]
    else:
        hi_cuts = list(range(4000, 18001, 500))
        trebles = [round(x, 2) for x in np.arange(0.10, 1.01, 0.10)]
        basses = [round(x, 2) for x in np.arange(0.50, 2.01, 0.10)]

    grid = list(itertools.product(hi_cuts, trebles, basses))
    return hi_cuts, trebles, basses, grid


def main():
    parser = argparse.ArgumentParser(description="EQ parameter grid search for Fat Snare Hall")
    parser.add_argument("--coarse", action="store_true", help="Use coarser grid for speed")
    parser.add_argument("--top", type=int, default=3, help="Number of top results to show")
    args = parser.parse_args()

    # Load plugins
    dv_path = find_plugin(DUSKVERB_PATHS)
    vv_path = find_plugin(REFERENCE_REVERB_PATHS)
    if not dv_path:
        print("ERROR: DuskVerb plugin not found"); sys.exit(1)
    if not vv_path:
        print("ERROR: VV plugin not found"); sys.exit(1)

    print(f"Loading DuskVerb: {dv_path}")
    dv_plugin = load_plugin(dv_path, plugin_name="DuskVerb")
    print(f"Loading VV: {vv_path}")
    vv_plugin = load_plugin(vv_path)

    # Capture VV target
    vv_ir = capture_vv_target(vv_plugin)

    # Print VV frequency response for reference
    vv_freq = metrics.frequency_response(vv_ir, SR)
    print("\n  VV frequency response:")
    for band, level in vv_freq.items():
        print(f"    {band}: {level:.1f} dB")

    # Build grid
    hi_cuts, trebles, basses, grid = build_grid(args.coarse)
    total = len(grid)
    print(f"\nSweep grid: {len(hi_cuts)} hi_cut × {len(trebles)} treble × {len(basses)} bass = {total} combinations")

    # Sweep
    results = []
    t0 = time.time()

    for i, (hc, tm, bm) in enumerate(grid):
        dv_ir = render_dv(dv_plugin, hc, tm, bm)
        total_mse, mean_dev, max_dev = score_ir(dv_ir, vv_ir)
        results.append((total_mse, mean_dev, max_dev, hc, tm, bm))

        # Progress every 50 combos
        if (i + 1) % 50 == 0 or i == 0:
            elapsed = time.time() - t0
            rate = (i + 1) / elapsed
            eta = (total - i - 1) / rate if rate > 0 else 0
            best_so_far = min(results, key=lambda r: r[0])
            print(f"  [{i+1}/{total}] {rate:.1f} it/s, ETA {eta:.0f}s | "
                  f"best MSE={best_so_far[0]:.0f} @ hi_cut={best_so_far[3]}, "
                  f"treble={best_so_far[4]}, bass={best_so_far[5]}")

    elapsed = time.time() - t0
    print(f"\nSweep complete: {total} combinations in {elapsed:.1f}s ({total/elapsed:.1f} it/s)")

    # Sort by total MSE (primary) then mean spectral envelope deviation (tiebreaker)
    results.sort(key=lambda r: (r[0], r[1]))

    # Print top N
    n = min(args.top, len(results))
    print(f"\n{'='*80}")
    print(f"TOP {n} RESULTS (lowest spectral MSE)")
    print(f"{'='*80}")
    print(f"{'Rank':>4}  {'MSE':>8}  {'SpEnv Mean':>10}  {'SpEnv Max':>9}  {'hi_cut':>8}  {'treble':>7}  {'bass':>6}")
    print(f"{'-'*4}  {'-'*8}  {'-'*10}  {'-'*9}  {'-'*8}  {'-'*7}  {'-'*6}")

    for rank, (mse, mean_d, max_d, hc, tm, bm) in enumerate(results[:n], 1):
        print(f"{rank:>4}  {mse:>8.0f}  {mean_d:>10.2f} dB  {max_d:>7.2f} dB  {hc:>7} Hz  {tm:>7.2f}  {bm:>6.2f}")

    # Also show the winner's frequency response for comparison
    best = results[0]
    print(f"\n{'='*80}")
    print(f"WINNER: hi_cut={best[3]} Hz, treble_multiply={best[4]}, bass_multiply={best[5]}")
    print(f"  MSE={best[0]:.0f}, SpEnv mean={best[1]:.2f} dB, SpEnv max={best[2]:.2f} dB")
    print(f"{'='*80}")

    # Render winner and show its frequency response
    winner_ir = render_dv(dv_plugin, best[3], best[4], best[5])
    dv_freq = metrics.frequency_response(winner_ir, SR)
    print("\n  Band comparison (VV target vs DV winner):")
    print(f"  {'Band':<22} {'VV':>8} {'DV':>8} {'Delta':>8}")
    print(f"  {'-'*22} {'-'*8} {'-'*8} {'-'*8}")
    for band in vv_freq:
        vv_lvl = vv_freq[band]
        dv_lvl = dv_freq.get(band, -100)
        delta = dv_lvl - vv_lvl
        print(f"  {band:<22} {vv_lvl:>7.1f} {dv_lvl:>7.1f} {delta:>+7.1f}")

    # Show spectral envelope per-band deviations for winner
    env = metrics.spectral_envelope_match(winner_ir, vv_ir, SR, start_ms=100, end_ms=3000)
    if env["band_deviations"]:
        print(f"\n  Spectral envelope deviations (DV - VV, level-normalized):")
        for band, dev in env["band_deviations"].items():
            bar = "+" * int(abs(dev) * 2) if dev > 0 else "-" * int(abs(dev) * 2)
            print(f"    {band:<12} {dev:>+6.2f} dB  {bar}")


if __name__ == "__main__":
    main()
