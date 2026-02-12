#!/usr/bin/env python3
"""
PCM 90 / Velvet 90 IR Matching Tool

Main entry point for comparing Velvet 90 against PCM 90 impulse responses
and optimizing parameters to match.

Usage:
    # Analyze a single target IR
    python run_matching.py analyze "path/to/ir.wav"

    # Compare Velvet 90 (default params) against a target IR
    python run_matching.py compare "path/to/ir.wav" --mode Hall --color 1980s

    # Optimize Velvet 90 params to match a target IR
    python run_matching.py optimize "path/to/ir.wav" --category Halls

    # Run representative benchmark across all categories
    python run_matching.py benchmark

    # Generate visual comparison plots
    python run_matching.py plot "path/to/ir.wav" --mode Hall --color 1980s

    # Generate a full report for a batch of IRs
    python run_matching.py report --category Halls --count 5
"""

import argparse
import os
import sys
import json
import numpy as np

# Add script directory to path for local imports
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from ir_analysis import load_ir, analyze_ir, profile_summary
from ir_compare import compare_profiles
from velvet90_capture import (
    Velvet90Params, capture_ir, default_params_for_category, VALID_MODES, VALID_COLORS,
)
from parameter_optimizer import optimize_for_target, batch_optimize

PCM90_IR_BASE = os.environ.get('PCM90_IR_BASE')

def _require_pcm90_base():
    if PCM90_IR_BASE is None:
        print("Error: PCM90_IR_BASE environment variable not set.")
        print("Set it to the path containing PCM 90 impulse responses.")
        sys.exit(1)
    return PCM90_IR_BASE


def get_representative_irs() -> list[tuple[str, str, str]]:
    """Select representative IRs from each category for benchmarking."""
    picks = {
        'Halls': [
            'pcm 90, Concert Hall (574)_dc.wav',
            'pcm 90, Small Hall (562)_dc.wav',
            'pcm 90, Bright Hall (596)_dc.wav',
            'pcm 90, Gothic Hall (573)_dc.wav',
            'pcm 90, Vocal Hall (577)_dc.wav',
        ],
        'Plates': [
            'pcm 90, Bright Plate (712)_dc.wav',
            'pcm 90, Vocal Plate (730)_dc.wav',
            'pcm 90, Dark Plate (709)_dc.wav',
            'pcm 90, Great Plate (721)_dc.wav',
            'pcm 90, Short Plate (752)_dc.wav',
        ],
        'Rooms': [
            'pcm 90, Small Room (669)_dc.wav',
            'pcm 90, Large Room (667)_dc.wav',
            'pcm 90, Medium Room (668)_dc.wav',
            'pcm 90, Vocal Space (677)_dc.wav',
            'pcm 90, Small chamber (672)_dc.wav',
        ],
    }
    result = []
    for category, files in picks.items():
        for f in files:
            path = os.path.join(_require_pcm90_base(), category, f)
            name = f.replace('pcm 90, ', '').replace('_dc.wav', '')
            result.append((path, name, category))
    return result


def cmd_analyze(args):
    """Analyze a single IR file."""
    data, sr = load_ir(args.ir_path)
    name = os.path.splitext(os.path.basename(args.ir_path))[0]
    profile = analyze_ir(data, sr, name=name)
    print(profile_summary(profile))


def cmd_compare(args):
    """Compare Velvet 90 output against a target IR."""
    # Load target
    target_data, target_sr = load_ir(args.ir_path)
    target_name = os.path.splitext(os.path.basename(args.ir_path))[0]
    sr = 48000

    if target_data.ndim == 1:
        target_data = target_data.reshape(1, -1)
    if target_sr != sr:
        from scipy.signal import resample
        n_new = int(target_data.shape[1] * sr / target_sr)
        target_data = np.array([resample(target_data[c], n_new) for c in range(target_data.shape[0])])

    target_profile = analyze_ir(target_data, sr, name=target_name)

    # Capture Velvet 90 IR
    params = Velvet90Params(
        mode=args.mode, color=args.color, size=args.size,
        room_size=args.room_size, damping=args.damping,
        pre_delay_ms=args.pre_delay, mix=100.0,
    )
    duration = min(target_profile.duration_s + 1.0, 8.0)
    velvet_ir = capture_ir(params, sr=sr, duration_s=duration, normalize=True)
    velvet_profile = analyze_ir(velvet_ir, sr, name=f"Velvet90({args.mode}/{args.color})")

    # Compare
    print(profile_summary(target_profile))
    print()
    print(profile_summary(velvet_profile))
    print()

    result = compare_profiles(target_profile, velvet_profile)
    print(result.summary())


def cmd_optimize(args):
    """Optimize Velvet 90 parameters to match a target IR."""
    result = optimize_for_target(
        target_path=args.ir_path,
        category=args.category,
        fixed_mode=args.mode,
        fixed_color=args.color,
        max_iterations=args.max_iter,
        verbose=True,
    )
    print(f"\n{result.summary()}")

    # Save result
    output = {
        'target': result.target_name,
        'score': result.best_score,
        'params': result.best_params.to_dict(),
        'elapsed_s': result.elapsed_s,
    }
    out_path = args.output or f"match_{result.target_name.replace(' ', '_')}.json"
    with open(out_path, 'w') as f:
        json.dump(output, f, indent=2)
    print(f"\nSaved to {out_path}")


def cmd_benchmark(args):
    """Run benchmark across representative PCM 90 IRs."""
    irs = get_representative_irs()
    if args.count:
        irs = irs[:args.count]

    results = batch_optimize(irs, max_iterations=args.max_iter, verbose=True)

    # Summary table
    print(f"\n{'='*70}")
    print(f"BENCHMARK RESULTS")
    print(f"{'='*70}")
    print(f"{'IR Name':30s} {'Category':10s} {'Mode':15s} {'Color':6s} {'Score':>6s}")
    print(f"{'-'*70}")

    for r in results:
        cat = "?"
        for _, _, c in get_representative_irs():
            if r.target_name in _:
                cat = c
                break
        cat_label = next((c for p, n, c in get_representative_irs() if n == r.target_name), '?')
        print(f"{r.target_name:30s} {cat_label:10s} {r.best_params.mode:15s} "
              f"{r.best_params.color:6s} {r.best_score:6.1f}")

    avg_score = np.mean([r.best_score for r in results])
    print(f"{'-'*70}")
    print(f"{'Average':30s} {'':10s} {'':15s} {'':6s} {avg_score:6.1f}")

    # Save all results
    output = {
        'results': [{
            'target': r.target_name,
            'score': r.best_score,
            'params': r.best_params.to_dict(),
        } for r in results],
        'average_score': avg_score,
    }
    out_path = args.output or 'benchmark_results.json'
    with open(out_path, 'w') as f:
        json.dump(output, f, indent=2)
    print(f"\nSaved to {out_path}")


def cmd_plot(args):
    """Generate visual comparison plots."""
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt

    # Load target
    target_data, target_sr = load_ir(args.ir_path)
    target_name = os.path.splitext(os.path.basename(args.ir_path))[0]
    sr = 48000

    if target_data.ndim == 1:
        target_data = target_data.reshape(1, -1)
    if target_sr != sr:
        from scipy.signal import resample
        n_new = int(target_data.shape[1] * sr / target_sr)
        target_data = np.array([resample(target_data[c], n_new) for c in range(target_data.shape[0])])

    target_profile = analyze_ir(target_data, sr, name=target_name)

    # Capture Velvet 90
    params = Velvet90Params(mode=args.mode, color=args.color, size=args.size, mix=100.0,
                            room_size=args.room_size, damping=args.damping, pre_delay_ms=args.pre_delay)
    duration = min(target_profile.duration_s + 1.0, 8.0)
    velvet_ir = capture_ir(params, sr=sr, duration_s=duration, normalize=True)
    velvet_profile = analyze_ir(velvet_ir, sr, name=f"Velvet90({args.mode})")

    fig, axes = plt.subplots(2, 3, figsize=(18, 10))
    fig.suptitle(f'IR Comparison: {target_name} vs Velvet90({args.mode}/{args.color})', fontsize=14)

    # 1. Waveform comparison
    ax = axes[0, 0]
    t_target = np.arange(len(target_data[0])) / sr
    t_silk = np.arange(velvet_ir.shape[1]) / sr
    ax.plot(t_target, target_data[0], alpha=0.7, label='Target', linewidth=0.5)
    ax.plot(t_silk, velvet_ir[0], alpha=0.7, label='Velvet 90', linewidth=0.5)
    ax.set_xlabel('Time (s)')
    ax.set_ylabel('Amplitude')
    ax.set_title('Waveform (L channel)')
    ax.legend()
    ax.set_xlim(0, max(t_target[-1], t_silk[-1]))

    # 2. Energy Decay Curve
    ax = axes[0, 1]
    ax.plot(target_profile.edc_time, target_profile.edc, label='Target', linewidth=1.5)
    ax.plot(velvet_profile.edc_time, velvet_profile.edc, label='Velvet 90', linewidth=1.5)
    ax.set_xlabel('Time (s)')
    ax.set_ylabel('Energy (dB)')
    ax.set_title(f'Energy Decay Curve (RT60: Tgt={target_profile.rt60:.2f}s, V90={velvet_profile.rt60:.2f}s)')
    ax.set_ylim(-70, 5)
    ax.legend()
    ax.grid(True, alpha=0.3)

    # 3. Spectral centroid over time
    ax = axes[0, 2]
    ax.plot(target_profile.spectral_centroid_time, target_profile.spectral_centroid_hz,
            label='Target', linewidth=1.5)
    ax.plot(velvet_profile.spectral_centroid_time, velvet_profile.spectral_centroid_hz,
            label='Velvet 90', linewidth=1.5)
    ax.set_xlabel('Time (s)')
    ax.set_ylabel('Frequency (Hz)')
    ax.set_title('Spectral Centroid Over Time')
    ax.legend()
    ax.grid(True, alpha=0.3)

    # 4. Early frequency response (0-50ms)
    ax = axes[1, 0]
    if target_profile.freq_axis is not None:
        mask = target_profile.freq_axis > 0
        ax.semilogx(target_profile.freq_axis[mask], target_profile.freq_response_early[mask],
                     label='Target', linewidth=1.5)
    if velvet_profile.freq_axis is not None:
        mask = velvet_profile.freq_axis > 0
        ax.semilogx(velvet_profile.freq_axis[mask], velvet_profile.freq_response_early[mask],
                     label='Velvet 90', linewidth=1.5)
    ax.set_xlabel('Frequency (Hz)')
    ax.set_ylabel('Magnitude (dB)')
    ax.set_title('Frequency Response: Early (0-50ms)')
    ax.set_xlim(100, 20000)
    ax.set_ylim(-40, 5)
    ax.legend()
    ax.grid(True, alpha=0.3)

    # 5. Late frequency response (200-500ms)
    ax = axes[1, 1]
    if target_profile.freq_axis is not None:
        mask = target_profile.freq_axis > 0
        ax.semilogx(target_profile.freq_axis[mask], target_profile.freq_response_late[mask],
                     label='Target', linewidth=1.5)
    if velvet_profile.freq_axis is not None:
        mask = velvet_profile.freq_axis > 0
        ax.semilogx(velvet_profile.freq_axis[mask], velvet_profile.freq_response_late[mask],
                     label='Velvet 90', linewidth=1.5)
    ax.set_xlabel('Frequency (Hz)')
    ax.set_ylabel('Magnitude (dB)')
    ax.set_title('Frequency Response: Late (200-500ms)')
    ax.set_xlim(100, 20000)
    ax.set_ylim(-40, 5)
    ax.legend()
    ax.grid(True, alpha=0.3)

    # 6. Band RT60 comparison
    ax = axes[1, 2]
    def _band_sort_key(x):
        try:
            return int(x.replace('Hz', '').replace('k', '000'))
        except ValueError:
            return 0
    bands = sorted(set(target_profile.band_rt60.keys()) & set(velvet_profile.band_rt60.keys()),
                   key=_band_sort_key)
    if bands:
        x_pos = np.arange(len(bands))
        t_vals = [target_profile.band_rt60[b] for b in bands]
        s_vals = [velvet_profile.band_rt60[b] for b in bands]
        w = 0.35
        ax.bar(x_pos - w/2, t_vals, w, label='Target', alpha=0.8)
        ax.bar(x_pos + w/2, s_vals, w, label='Velvet 90', alpha=0.8)
        ax.set_xticks(x_pos)
        ax.set_xticklabels(bands, rotation=45)
        ax.set_ylabel('RT60 (s)')
        ax.set_title('RT60 by Octave Band')
        ax.legend()
        ax.grid(True, alpha=0.3, axis='y')

    plt.tight_layout()
    out_path = args.output or f"plot_{target_name.replace(' ', '_')}.png"
    plt.savefig(out_path, dpi=150)
    print(f"Plot saved to {out_path}")

    # Also print comparison scores
    result = compare_profiles(target_profile, velvet_profile)
    print(result.summary())


def cmd_report(args):
    """Generate a full report for a category of IRs."""
    import matplotlib
    matplotlib.use('Agg')

    category = args.category
    ir_dir = os.path.join(_require_pcm90_base(), category)

    if not os.path.isdir(ir_dir):
        print(f"Error: directory not found: {ir_dir}")
        return

    files = sorted([f for f in os.listdir(ir_dir) if f.endswith('.wav')])
    if args.count:
        files = files[:args.count]

    targets = []
    for f in files:
        path = os.path.join(ir_dir, f)
        name = f.replace('pcm 90, ', '').replace('_dc.wav', '')
        targets.append((path, name, category))

    results = batch_optimize(targets, max_iterations=args.max_iter, verbose=True)

    # Summary
    print(f"\n{'='*70}")
    print(f"REPORT: {category} ({len(results)} IRs)")
    print(f"{'='*70}")
    for r in results:
        print(f"\n{r.summary()}")

    avg = np.mean([r.best_score for r in results])
    print(f"\nAverage score: {avg:.1f}/100")


def main():
    parser = argparse.ArgumentParser(
        description='PCM 90 / Velvet 90 IR Matching Tool',
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    subparsers = parser.add_subparsers(dest='command', help='Command to run')

    # analyze
    p = subparsers.add_parser('analyze', help='Analyze a single IR file')
    p.add_argument('ir_path', help='Path to .wav IR file')

    # compare
    p = subparsers.add_parser('compare', help='Compare Velvet 90 against a target IR')
    p.add_argument('ir_path', help='Path to .wav IR file')
    p.add_argument('--mode', default='Hall')
    p.add_argument('--color', default='1980s')
    p.add_argument('--size', default='2.0s')
    p.add_argument('--room-size', type=float, default=50.0)
    p.add_argument('--damping', type=float, default=50.0)
    p.add_argument('--pre-delay', type=float, default=0.0)

    # optimize
    p = subparsers.add_parser('optimize', help='Optimize Velvet 90 params for a target IR')
    p.add_argument('ir_path', help='Path to .wav IR file')
    p.add_argument('--category', default='Halls', choices=['Halls', 'Plates', 'Rooms', 'Post'])
    p.add_argument('--mode', default=None, help='Fix mode (skip mode search)')
    p.add_argument('--color', default=None, help='Fix color (skip color search)')
    p.add_argument('--max-iter', type=int, default=80)
    p.add_argument('--output', '-o', default=None, help='Output JSON path')

    # benchmark
    p = subparsers.add_parser('benchmark', help='Run representative benchmark')
    p.add_argument('--count', type=int, default=None, help='Limit number of IRs')
    p.add_argument('--max-iter', type=int, default=60)
    p.add_argument('--output', '-o', default=None)

    # plot
    p = subparsers.add_parser('plot', help='Generate visual comparison plots')
    p.add_argument('ir_path', help='Path to .wav IR file')
    p.add_argument('--mode', default='Hall')
    p.add_argument('--color', default='1980s')
    p.add_argument('--size', default='2.0s')
    p.add_argument('--room-size', type=float, default=50.0)
    p.add_argument('--damping', type=float, default=50.0)
    p.add_argument('--pre-delay', type=float, default=0.0)
    p.add_argument('--output', '-o', default=None)

    # report
    p = subparsers.add_parser('report', help='Full report for a category')
    p.add_argument('--category', default='Halls', choices=['Halls', 'Plates', 'Rooms', 'Post'])
    p.add_argument('--count', type=int, default=5)
    p.add_argument('--max-iter', type=int, default=60)

    args = parser.parse_args()

    if args.command is None:
        parser.print_help()
        return

    commands = {
        'analyze': cmd_analyze,
        'compare': cmd_compare,
        'optimize': cmd_optimize,
        'benchmark': cmd_benchmark,
        'plot': cmd_plot,
        'report': cmd_report,
    }
    commands[args.command](args)


if __name__ == '__main__':
    main()
