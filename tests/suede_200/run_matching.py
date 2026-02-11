#!/usr/bin/env python3
"""
Suede 200 Coefficient Matching — Batch optimize all 55 IRs.

Finds the 16 WCS coefficients that best match each real hardware IR.
Results are saved as JSON for analysis and curve fitting.

Usage:
    python run_matching.py                    # Run all IRs
    python run_matching.py --program 0        # Only Concert Hall
    python run_matching.py --ir "Hall 1"      # Single IR
    python run_matching.py --analyze results  # Analyze previous results
"""

import os
import sys
import json
import argparse
import numpy as np
from pathlib import Path

from wcs_engine import ir_name_to_program, PROGRAM_NAMES, PROGRAM_MAP
from ir_optimizer import optimize_for_target, batch_optimize, OptimizationResult

IR_BASE = os.environ.get(
    'SUEDE_IR_BASE',
    '/Users/marckorte/Downloads/Lexicon 200 impulse set',
)

RESULTS_DIR = os.path.join(os.path.dirname(__file__), 'match_results')


def discover_irs(base_dir: str = IR_BASE,
                 program_filter: int = None) -> list[tuple[str, str, int]]:
    """
    Discover all IRs in the base directory.
    Returns: list of (path, display_name, program_index)
    """
    irs = []
    for f in sorted(os.listdir(base_dir)):
        if not f.endswith('.wav'):
            continue
        name = f.replace('_dc.wav', '').replace('.wav', '')
        try:
            program = ir_name_to_program(f)
        except ValueError:
            print(f"  Skipping unrecognized: {f}")
            continue

        if program_filter is not None and program != program_filter:
            continue

        irs.append((os.path.join(base_dir, f), name, program))

    return irs


def save_results(results: list[OptimizationResult], output_dir: str = RESULTS_DIR):
    """Save optimization results as JSON."""
    os.makedirs(output_dir, exist_ok=True)

    all_results = []
    for r in results:
        all_results.append(r.to_dict())

    output_path = os.path.join(output_dir, 'coefficients.json')
    with open(output_path, 'w') as f:
        json.dump(all_results, f, indent=2)
    print(f"\nResults saved to {output_path}")

    # Also save per-program summary
    by_program = {}
    for r in results:
        prog = r.program
        if prog not in by_program:
            by_program[prog] = []
        by_program[prog].append(r)

    summary_path = os.path.join(output_dir, 'summary.txt')
    with open(summary_path, 'w') as f:
        f.write("Suede 200 Coefficient Matching Results\n")
        f.write("=" * 60 + "\n\n")

        for prog in sorted(by_program.keys()):
            results_for_prog = by_program[prog]
            scores = [r.best_score for r in results_for_prog]
            f.write(f"Program {prog}: {PROGRAM_NAMES[prog]}\n")
            f.write(f"  IRs matched: {len(results_for_prog)}\n")
            f.write(f"  Score range: {min(scores):.1f} - {max(scores):.1f}\n")
            f.write(f"  Average:     {np.mean(scores):.1f}\n")

            # Coefficient statistics across all IRs of this program
            all_coeffs = np.array([r.best_coefficients for r in results_for_prog])
            f.write(f"  Coefficient ranges:\n")
            for c in range(16):
                lo, hi = all_coeffs[:, c].min(), all_coeffs[:, c].max()
                mean = all_coeffs[:, c].mean()
                std = all_coeffs[:, c].std()
                label = "STABLE" if std < 0.05 else "VARIES" if std > 0.15 else "moderate"
                f.write(f"    C{c:X}: {mean:+.3f} [{lo:+.3f} to {hi:+.3f}] "
                        f"std={std:.3f} ({label})\n")
            f.write("\n")

    print(f"Summary saved to {summary_path}")
    return output_path


def analyze_results(results_path: str):
    """Analyze previously saved results."""
    with open(results_path) as f:
        data = json.load(f)

    by_program = {}
    for entry in data:
        prog = entry['program']
        if prog not in by_program:
            by_program[prog] = []
        by_program[prog].append(entry)

    print("\nSuede 200 Coefficient Analysis")
    print("=" * 70)

    for prog in sorted(by_program.keys()):
        entries = by_program[prog]
        all_coeffs = np.array([e['coefficients'] for e in entries])
        scores = [e['score'] for e in entries]

        print(f"\nProgram {prog}: {PROGRAM_NAMES[prog]} ({len(entries)} IRs)")
        print(f"  Scores: {np.mean(scores):.1f} avg, {min(scores):.1f}-{max(scores):.1f}")
        print(f"  {'C-code':8s} {'Mean':>8s} {'Std':>8s} {'Min':>8s} {'Max':>8s}  Note")
        print(f"  {'-'*55}")

        for c in range(16):
            vals = all_coeffs[:, c]
            mean, std = vals.mean(), vals.std()
            lo, hi = vals.min(), vals.max()
            note = ""
            if std < 0.03:
                note = "FIXED (structural)"
            elif std > 0.15:
                note = "VARIES (parameter-controlled)"
            elif std > 0.08:
                note = "moderate variation"
            print(f"  C{c:X}:     {mean:+.4f} {std:.4f}  {lo:+.4f}  {hi:+.4f}  {note}")

    # Cross-program analysis
    print(f"\n{'='*70}")
    print("Cross-program coefficient comparison:")
    for c in range(16):
        print(f"\n  C{c:X}:")
        for prog in sorted(by_program.keys()):
            vals = np.array([e['coefficients'][c] for e in by_program[prog]])
            print(f"    {PROGRAM_NAMES[prog]:15s}: "
                  f"{vals.mean():+.4f} ± {vals.std():.4f}")


def main():
    from ir_optimizer import DEFAULT_MATCH_SR, DEFAULT_POPSIZE, DEFAULT_MAX_ITER

    parser = argparse.ArgumentParser(description='Suede 200 coefficient matching')
    parser.add_argument('--program', type=int, default=None,
                        help='Only match IRs for this program (0-5)')
    parser.add_argument('--ir', type=str, default=None,
                        help='Match a single IR by name prefix (e.g. "Hall 1")')
    parser.add_argument('--analyze', type=str, default=None,
                        help='Analyze results directory instead of running')
    parser.add_argument('--sr', type=int, default=DEFAULT_MATCH_SR,
                        help=f'Sample rate for matching (default: {DEFAULT_MATCH_SR})')
    parser.add_argument('--popsize', type=int, default=DEFAULT_POPSIZE,
                        help=f'DE population multiplier (default: {DEFAULT_POPSIZE})')
    parser.add_argument('--max-iter', type=int, default=DEFAULT_MAX_ITER,
                        help=f'Max DE iterations per IR (default: {DEFAULT_MAX_ITER})')
    parser.add_argument('--quiet', action='store_true',
                        help='Less verbose output')
    args = parser.parse_args()

    if args.analyze:
        results_path = os.path.join(args.analyze, 'coefficients.json')
        if not os.path.exists(results_path):
            results_path = args.analyze
        analyze_results(results_path)
        return

    # Discover IRs
    irs = discover_irs(program_filter=args.program)

    if args.ir:
        irs = [(p, n, prog) for p, n, prog in irs if args.ir.lower() in n.lower()]

    if not irs:
        print(f"No IRs found in {IR_BASE}")
        print(f"Set SUEDE_IR_BASE env var or check path")
        return

    print(f"Found {len(irs)} IRs to match:")
    for _, name, prog in irs:
        print(f"  {name} → Program {prog} ({PROGRAM_NAMES[prog]})")
    print(f"\nSettings: SR={args.sr}Hz, popsize={args.popsize}, max_iter={args.max_iter}")

    # Run optimization
    results = batch_optimize(irs, sr=args.sr, popsize=args.popsize,
                             max_iter=args.max_iter,
                             verbose=not args.quiet)

    # Save results
    save_results(results)

    # Print summary
    print(f"\n{'='*60}")
    print("RESULTS SUMMARY")
    print(f"{'='*60}")
    for r in results:
        print(f"  {r.target_name:25s} → {r.best_score:.1f}/100  ({r.elapsed_s:.0f}s)")


if __name__ == '__main__':
    main()
