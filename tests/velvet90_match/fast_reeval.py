#!/usr/bin/env python3
"""
Fast Re-evaluation — Re-score previous best params against updated plugin.

Instead of re-running the full optimizer (200+ renders per preset), this script:
1. Loads the previous best parameters from a results directory
2. Renders each preset ONCE through the current plugin build
3. Re-scores against the target IR
4. Reports improvements/degradations

This is ~200x faster than a full optimization run.

Usage:
    # Re-evaluate all previous results with current plugin
    python fast_reeval.py --baseline results_macos -o results_reeval -j 8

    # Only show presets that degraded
    python fast_reeval.py --baseline results_macos --show-degraded
"""

import argparse
import json
import os
import sys
import time

os.environ.setdefault('OMP_NUM_THREADS', '1')
os.environ.setdefault('OPENBLAS_NUM_THREADS', '1')
os.environ.setdefault('MKL_NUM_THREADS', '1')

import numpy as np
from multiprocessing import Pool, current_process

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def reeval_single(args_tuple):
    """Re-evaluate a single preset with its previous best params."""
    result_file, output_dir, index, total = args_tuple
    worker = current_process().name

    try:
        with open(result_file) as f:
            prev = json.load(f)
    except (json.JSONDecodeError, FileNotFoundError) as e:
        return {'error': str(e), 'file': result_file}

    if 'error' in prev or 'params' not in prev:
        return {'skipped': True, **prev}

    preset_name = prev['target_name']
    ir_path = prev['ir_path']
    old_score = prev['score']

    if not os.path.exists(ir_path):
        print(f"[{worker}] [{index}/{total}] MISSING IR: {ir_path}", flush=True)
        return {'error': f'Missing IR: {ir_path}', 'target_name': preset_name}

    print(f"[{worker}] [{index}/{total}] {preset_name}...", flush=True)

    try:
        from ir_analysis import load_ir, analyze_ir
        from ir_compare import compare_profiles
        from velvet90_capture import Velvet90Params, capture_ir

        # Load and analyze target IR (the PCM 90 reference)
        target_data, target_sr = load_ir(ir_path)
        sr = 48000
        if target_sr != sr:
            from scipy.signal import resample
            n_new = int(target_data.shape[1] * sr / target_sr)
            target_data = np.array([resample(target_data[c], n_new)
                                    for c in range(target_data.shape[0])])
        target_profile = analyze_ir(target_data, sr, name=preset_name)
        capture_duration = min(target_profile.duration_s + 2.0, 12.0)

        # Reconstruct params from previous best
        params = Velvet90Params.from_dict(prev['params'])

        # Single render through updated plugin
        ir = capture_ir(params, sr=sr, duration_s=capture_duration, normalize=True)
        cand_profile = analyze_ir(ir, sr, name=f"Velvet90(reeval)")
        comparison = compare_profiles(target_profile, cand_profile)
        new_score = comparison.overall_score

        delta = new_score - old_score
        marker = '+' if delta > 0 else '-' if delta < 0 else '='
        print(f"[{worker}] [{index}/{total}] {preset_name}: "
              f"{old_score:.1f} -> {new_score:.1f} ({marker}{abs(delta):.1f})", flush=True)

        output = {
            **prev,
            'old_score': old_score,
            'score': new_score,
            'delta': delta,
            'dimension_scores': {
                'rt60': comparison.rt60_score,
                'edt': comparison.edt_score,
                'edc_shape': comparison.edc_shape_score,
                'spectral_early': comparison.spectral_early_score,
                'spectral_late': comparison.spectral_late_score,
                'spectral_centroid': comparison.spectral_centroid_score,
                'band_rt60': comparison.band_rt60_score,
                'pre_delay': comparison.pre_delay_score,
                'stereo': comparison.stereo_score,
            },
        }

        if output_dir:
            safe_name = os.path.splitext(os.path.basename(result_file))[0]
            out_file = os.path.join(output_dir, f"{safe_name}.json")
            with open(out_file, 'w') as f:
                json.dump(output, f, indent=2)

        return output

    except Exception as e:
        print(f"[{worker}] [{index}/{total}] ERROR {preset_name}: {e}", flush=True)
        return {'error': str(e), 'target_name': preset_name, 'old_score': prev.get('score')}


def run_reeval(baseline_dir, output_dir=None, workers=1, show_degraded=False):
    """Re-evaluate all results from baseline_dir."""
    # Find all JSON result files (skip _all_results.json)
    result_files = sorted([
        os.path.join(baseline_dir, f)
        for f in os.listdir(baseline_dir)
        if f.endswith('.json') and not f.startswith('_')
    ])

    total = len(result_files)
    print(f"{'='*60}")
    print(f"  Fast Re-evaluation — {total} presets")
    print(f"  Baseline: {baseline_dir}")
    print(f"  Workers: {workers}")
    print(f"{'='*60}\n", flush=True)

    if output_dir:
        os.makedirs(output_dir, exist_ok=True)

    start = time.time()

    work_items = [
        (rf, output_dir, i + 1, total)
        for i, rf in enumerate(result_files)
    ]

    if workers > 1:
        with Pool(processes=workers) as pool:
            all_results = pool.map(reeval_single, work_items)
    else:
        all_results = [reeval_single(item) for item in work_items]

    elapsed = time.time() - start

    # Analyze results
    valid = [r for r in all_results if 'delta' in r]
    errors = [r for r in all_results if 'error' in r]

    if not valid:
        print("No valid results to analyze.")
        return all_results  # Return what we have, even if no valid deltas

    deltas = [r['delta'] for r in valid]
    improved = [r for r in valid if r['delta'] > 1.0]
    degraded = [r for r in valid if r['delta'] < -1.0]
    stable = [r for r in valid if abs(r['delta']) <= 1.0]

    new_scores = [r['score'] for r in valid]
    old_scores = [r['old_score'] for r in valid]

    print(f"\n{'='*60}")
    print(f"  RE-EVALUATION COMPLETE ({elapsed:.0f}s)")
    print(f"{'='*60}")
    print(f"  Presets evaluated: {len(valid)}")
    print(f"  Errors: {len(errors)}")
    print(f"  Improved (>1pt): {len(improved)}")
    print(f"  Degraded (>1pt): {len(degraded)}")
    print(f"  Stable (±1pt):   {len(stable)}")
    print(f"\n  Average score: {np.mean(old_scores):.1f} -> {np.mean(new_scores):.1f} "
          f"({np.mean(deltas):+.1f})")
    print(f"  Median delta: {np.median(deltas):+.1f}")

    if not show_degraded:
        # Per-category breakdown
        print(f"\n  By Category:")
        for cat in ['Halls', 'Rooms', 'Plates', 'Creative']:
            cat_results = [r for r in valid if r.get('category') == cat]
            if cat_results:
                cat_old = np.mean([r['old_score'] for r in cat_results])
                cat_new = np.mean([r['score'] for r in cat_results])
                print(f"    {cat:10s}: {cat_old:.1f} -> {cat_new:.1f} ({cat_new-cat_old:+.1f})  "
                      f"({len(cat_results)} presets)")

    # Show biggest degradations
    if degraded:
        degraded.sort(key=lambda r: r['delta'])
        label = "Degraded presets:" if show_degraded else "Worst degradations:"
        limit = len(degraded) if show_degraded else 10
        print(f"\n  {label}")
        for r in degraded[:limit]:
            print(f"    {r['target_name']:25s}: {r['old_score']:.1f} -> {r['score']:.1f} "
                  f"({r['delta']:+.1f})")
    elif show_degraded:
        print(f"\n  No degraded presets.")

    # Show biggest improvements
    if improved and not show_degraded:
        improved.sort(key=lambda r: -r['delta'])
        print(f"\n  Best improvements:")
        for r in improved[:10]:
            print(f"    {r['target_name']:25s}: {r['old_score']:.1f} -> {r['score']:.1f} "
                  f"({r['delta']:+.1f})")

    # Save summary
    if output_dir:
        summary = {
            'baseline_dir': baseline_dir,
            'elapsed_s': elapsed,
            'total': len(valid),
            'improved': len(improved),
            'degraded': len(degraded),
            'stable': len(stable),
            'avg_old': float(np.mean(old_scores)),
            'avg_new': float(np.mean(new_scores)),
            'avg_delta': float(np.mean(deltas)),
        }
        with open(os.path.join(output_dir, '_summary.json'), 'w') as f:
            json.dump(summary, f, indent=2)

    print(f"\n  Done in {elapsed:.0f}s", flush=True)
    return all_results


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Fast re-evaluation of previous best params')
    parser.add_argument('--baseline', '-b', required=True,
                        help='Directory with previous results to re-evaluate')
    parser.add_argument('--output', '-o', default=None,
                        help='Output directory for re-evaluated results')
    parser.add_argument('-j', '--workers', type=int, default=1,
                        help='Number of parallel workers')
    parser.add_argument('--show-degraded', action='store_true',
                        help='Only show presets that degraded')
    args = parser.parse_args()

    run_reeval(args.baseline, args.output, args.workers, args.show_degraded)
