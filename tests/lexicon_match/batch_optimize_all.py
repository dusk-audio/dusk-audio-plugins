#!/usr/bin/env python3
"""
Batch Optimizer — Process all PCM 90 IRs through SilkVerb parameter optimizer.

Reads the IR-to-preset mapping, runs the optimizer for each IR with its
assigned SilkVerb mode, and saves individual JSON results that can be
resumed if interrupted.

Supports parallel workers (-j N) for multi-core speedup. Each worker gets
its own pedalboard plugin instance automatically.

Usage:
    python batch_optimize_all.py [--max-iter 80] [--no-resume] [--output results/]

    # Use 6 parallel workers
    python batch_optimize_all.py -j 6

    # Run a specific category only
    python batch_optimize_all.py --category Halls -j 6

    # Run a single preset by name
    python batch_optimize_all.py --preset "Concert Hall"

    # Dry run — list all IRs without optimizing
    python batch_optimize_all.py --dry-run
"""

import argparse
import json
import os
import sys
import time
from multiprocessing import Pool, current_process

# Prevent numpy/BLAS from spawning internal threads per worker process
# (6 workers × N BLAS threads can exceed pthread limits)
os.environ.setdefault('OMP_NUM_THREADS', '1')
os.environ.setdefault('OPENBLAS_NUM_THREADS', '1')
os.environ.setdefault('MKL_NUM_THREADS', '1')

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from pcm90_ir_mapping import get_all_ir_mappings, MODE_NAMES


def safe_filename(name: str) -> str:
    """Convert preset name to a safe filename."""
    return (name.replace(' ', '_').replace("'", '').replace('/', '_')
            .replace('?', '').replace('#', '').replace('"', ''))


def optimize_single(args_tuple):
    """
    Optimize a single IR. Designed to run in a worker process.

    Each worker process gets its own pedalboard plugin instance via the
    module-level _plugin_cache in silkverb_capture.py.

    Args:
        args_tuple: (mapping_dict, output_dir, max_iterations, resume, index, total)

    Returns:
        Result dict or None if skipped.
    """
    m, output_dir, max_iterations, resume, index, total = args_tuple
    worker = current_process().name

    preset_name = m['preset_name']
    safe_name = safe_filename(preset_name)
    result_file = os.path.join(output_dir, f"{safe_name}.json")

    # Skip if already done (for resume)
    if resume and os.path.exists(result_file):
        try:
            with open(result_file) as f:
                existing = json.load(f)
            if 'score' in existing and 'params' in existing:
                print(f"[{worker}] [{index}/{total}] SKIP {preset_name} "
                      f"(score={existing['score']:.1f})", flush=True)
                return {'skipped': True, **existing}
        except (json.JSONDecodeError, KeyError):
            pass  # Re-run if corrupt

    print(f"\n[{worker}] [{index}/{total}] {preset_name} "
          f"({m['mode_name']}, {m['category']})", flush=True)

    ir_start = time.time()

    try:
        # Import inside worker to ensure each process has its own module state
        from parameter_optimizer import optimize_for_target

        result = optimize_for_target(
            target_path=m['ir_path'],
            target_name=preset_name,
            category=m['category'],
            fixed_mode=m['mode_name'],
            max_iterations=max_iterations,
            verbose=False,  # Quiet in parallel mode — just print summary
        )

        output = {
            'target_name': preset_name,
            'ir_file': m['ir_file'],
            'ir_path': m['ir_path'],
            'category': m['category'],
            'mode_index': m['mode'],
            'mode_name': m['mode_name'],
            'description': m['description'],
            'pcm90_bank': m['pcm90_bank'],
            'score': result.best_score,
            'params': result.best_params.to_dict(),
            'iterations': result.iterations,
            'elapsed_s': result.elapsed_s,
        }

        # Save individual result
        with open(result_file, 'w') as f:
            json.dump(output, f, indent=2)

        elapsed = time.time() - ir_start
        print(f"[{worker}] [{index}/{total}] DONE {preset_name}: "
              f"{result.best_score:.1f}/100 ({elapsed:.0f}s)", flush=True)

        return output

    except Exception as e:
        elapsed = time.time() - ir_start
        print(f"[{worker}] [{index}/{total}] ERROR {preset_name}: {e} "
              f"({elapsed:.0f}s)", flush=True)

        error_entry = {
            'target_name': preset_name,
            'ir_file': m['ir_file'],
            'ir_path': m['ir_path'],
            'category': m['category'],
            'mode_name': m['mode_name'],
            'score': 0.0,
            'error': str(e),
        }

        # Save error result so we don't retry on resume
        with open(result_file, 'w') as f:
            json.dump(error_entry, f, indent=2)

        return error_entry


def run_batch(output_dir='results', max_iterations=80, resume=True,
              workers=1, category_filter=None, preset_filter=None):
    """
    Run batch optimization on all mapped PCM 90 IRs.

    Args:
        output_dir: Directory to save JSON results
        max_iterations: Max Nelder-Mead iterations per IR
        resume: Skip IRs that already have results
        workers: Number of parallel worker processes
        category_filter: Only process this SilkVerb category
        preset_filter: Only process this preset name (partial match)
    """
    os.makedirs(output_dir, exist_ok=True)

    mappings = get_all_ir_mappings()

    # Apply filters
    if category_filter:
        mappings = [m for m in mappings if m['category'] == category_filter]
    if preset_filter:
        mappings = [m for m in mappings
                    if preset_filter.lower() in m['preset_name'].lower()]

    total = len(mappings)
    if total == 0:
        print("No IRs match the filter criteria.")
        return

    # Count already-done for ETA
    already_done = 0
    if resume:
        for m in mappings:
            rf = os.path.join(output_dir, f"{safe_filename(m['preset_name'])}.json")
            if os.path.exists(rf):
                already_done += 1

    print(f"{'='*60}")
    print(f"  SilkVerb Batch Optimizer — PCM 90 IR Matching")
    print(f"{'='*60}")
    print(f"  Total IRs: {total}  (already done: {already_done})")
    print(f"  Workers: {workers}")
    print(f"  Max iterations: {max_iterations}")
    print(f"  Output: {os.path.abspath(output_dir)}")
    print(f"  Resume: {resume}")
    print(f"{'='*60}\n", flush=True)

    start_time = time.time()

    # Build argument tuples for workers
    work_items = [
        (m, output_dir, max_iterations, resume, i + 1, total)
        for i, m in enumerate(mappings)
    ]

    if workers > 1:
        # Parallel execution
        with Pool(processes=workers) as pool:
            all_results = pool.map(optimize_single, work_items)
    else:
        # Sequential (verbose mode)
        all_results = []
        for item in work_items:
            result = optimize_single(item)
            all_results.append(result)

    # Filter out None results and collect stats
    all_results = [r for r in all_results if r is not None]
    skipped = sum(1 for r in all_results if r.get('skipped'))
    errors = sum(1 for r in all_results if 'error' in r)

    # Remove 'skipped' key before saving
    for r in all_results:
        r.pop('skipped', None)

    # Save combined results
    total_elapsed = time.time() - start_time
    scores = [r['score'] for r in all_results
              if r.get('score', 0) > 0 and 'error' not in r]

    combined = {
        'total': len(all_results),
        'skipped': skipped,
        'errors': errors,
        'workers': workers,
        'elapsed_hours': total_elapsed / 3600,
        'average_score': sum(scores) / max(len(scores), 1),
        'min_score': min(scores) if scores else 0,
        'max_score': max(scores) if scores else 0,
        'above_95': sum(1 for s in scores if s >= 95.0),
        'above_90': sum(1 for s in scores if s >= 90.0),
        'results': all_results,
    }

    combined_file = os.path.join(output_dir, '_all_results.json')
    with open(combined_file, 'w') as f:
        json.dump(combined, f, indent=2)

    # Print summary
    print(f"\n{'='*60}")
    print(f"  BATCH COMPLETE")
    print(f"{'='*60}")
    print(f"  Processed: {len(all_results)}  |  Skipped: {skipped}  |  Errors: {errors}")
    print(f"  Workers: {workers}  |  Total time: {total_elapsed/3600:.1f} hours")

    if scores:
        print(f"\n  Score Summary:")
        print(f"    Average: {sum(scores)/len(scores):.1f}")
        print(f"    Min: {min(scores):.1f}  |  Max: {max(scores):.1f}")
        print(f"    >= 95: {sum(1 for s in scores if s >= 95.0)}/{len(scores)}")
        print(f"    >= 90: {sum(1 for s in scores if s >= 90.0)}/{len(scores)}")
        print(f"    >= 85: {sum(1 for s in scores if s >= 85.0)}/{len(scores)}")

        # Per-category breakdown
        print(f"\n  By Category:")
        for cat in ['Halls', 'Rooms', 'Plates', 'Creative']:
            cat_scores = [r['score'] for r in all_results
                          if r.get('category') == cat and r.get('score', 0) > 0
                          and 'error' not in r]
            if cat_scores:
                print(f"    {cat:10s}: avg={sum(cat_scores)/len(cat_scores):.1f}  "
                      f"({len(cat_scores)} presets)")

    print(f"\n  Results: {combined_file}", flush=True)


def dry_run():
    """List all IRs that would be processed."""
    mappings = get_all_ir_mappings()
    print(f"Total IRs: {len(mappings)}\n")

    by_cat = {}
    for m in mappings:
        by_cat.setdefault(m['category'], []).append(m)

    for cat in ['Halls', 'Rooms', 'Plates', 'Creative']:
        items = by_cat.get(cat, [])
        print(f"\n{'='*60}")
        print(f"  {cat}: {len(items)} presets")
        print(f"{'='*60}")
        for m in items:
            exists = os.path.exists(m['ir_path'])
            status = 'OK' if exists else 'MISSING'
            print(f"  [{status:7s}] {m['preset_name']:25s}  "
                  f"{m['mode_name']:15s}  ({m['pcm90_bank']})")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='Batch optimize all PCM 90 IRs for SilkVerb',
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument('-j', '--workers', type=int, default=1,
                        help='Number of parallel workers (default: 1)')
    parser.add_argument('--output', '-o', default='results',
                        help='Output directory for JSON results (default: results/)')
    parser.add_argument('--max-iter', type=int, default=80,
                        help='Max Nelder-Mead iterations per IR (default: 80)')
    parser.add_argument('--no-resume', action='store_true',
                        help='Do not skip existing results (re-run all)')
    parser.add_argument('--category', default=None,
                        choices=['Halls', 'Rooms', 'Plates', 'Creative'],
                        help='Only process this category')
    parser.add_argument('--preset', default=None,
                        help='Only process presets matching this name (partial)')
    parser.add_argument('--dry-run', action='store_true',
                        help='List all IRs without optimizing')

    args = parser.parse_args()

    if args.dry_run:
        dry_run()
    else:
        run_batch(
            output_dir=args.output,
            max_iterations=args.max_iter,
            resume=not args.no_resume,
            workers=args.workers,
            category_filter=args.category,
            preset_filter=args.preset,
        )
