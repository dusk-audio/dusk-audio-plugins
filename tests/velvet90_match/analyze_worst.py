"""
Analyze the bottom-10 worst-scoring "normal" reverbs from the v2 batch optimization.

For each preset:
  - Loads the target PCM 90 IR
  - Renders Velvet 90 IR using the optimized parameters
  - Runs a full 9-dimension comparison
  - Prints per-dimension score breakdown with diagnostic values
"""

import json
import sys
import os
import numpy as np

# Ensure we can import local modules
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from ir_analysis import load_ir, analyze_ir, profile_summary
from ir_compare import compare_profiles
from velvet90_capture import Velvet90Params, capture_ir


# ─── Configuration ───────────────────────────────────────────────────────────

RESULTS_FILE = os.path.join(os.path.dirname(__file__), 'results', '_all_results.json')
NUM_WORST = 10

# Keywords that identify "special" (non-normal) presets
EXCLUDE_KEYWORDS = [
    'Reverse', 'Gate', 'Inverse', 'Non-Lin', 'Echo', 'Delay',
    'Slapback', 'Spring', 'Trampoline', 'Telephone', 'Television', 'Infinite'
]

# Dimension weights (matching ir_compare.py defaults)
WEIGHTS = {
    'rt60':              0.25,
    'band_rt60':         0.25,
    'edc_shape':         0.15,
    'stereo':            0.12,
    'edt':               0.05,
    'spectral_centroid':  0.05,
    'spectral_late':     0.05,
    'pre_delay':         0.05,
    'spectral_early':    0.03,
}


# ─── Helpers ─────────────────────────────────────────────────────────────────

def is_special(name: str) -> bool:
    """Check if a preset name matches special/excluded categories."""
    return any(kw.lower() in name.lower() for kw in EXCLUDE_KEYWORDS)


def format_band_rt60_table(target_bands: dict, velvet90_bands: dict, diffs: dict) -> str:
    """Format a side-by-side band RT60 comparison table."""
    all_bands = sorted(
        set(list(target_bands.keys()) + list(velvet90_bands.keys())),
        key=lambda b: int(b.replace('Hz', '')) if b.replace('Hz', '').isdigit() else 0
    )
    lines = ["      Band      Target    Velvet 90    Diff"]
    lines.append("      " + "-" * 42)
    for band in all_bands:
        t_val = target_bands.get(band, 0.0)
        s_val = velvet90_bands.get(band, 0.0)
        d_val = diffs.get(band, s_val - t_val)
        pct = (d_val / t_val * 100) if t_val > 0.05 else 0.0
        lines.append(f"      {band:>7s}   {t_val:6.3f}s   {s_val:6.3f}s   {d_val:+.3f}s ({pct:+.0f}%)")
    return "\n".join(lines)


# ─── Main Analysis ───────────────────────────────────────────────────────────

def main():
    # Load results
    print("Loading results...")
    with open(RESULTS_FILE) as f:
        data = json.load(f)

    results = data['results']

    # Filter to normal reverbs and sort by score ascending
    normal = [r for r in results if not is_special(r['target_name'])]
    normal.sort(key=lambda r: r['score'])

    worst = normal[:NUM_WORST]

    print(f"\nTotal presets: {len(results)}")
    print(f"Normal presets: {len(normal)}")
    print(f"Analyzing bottom {NUM_WORST} by score\n")
    print("=" * 100)

    # Collect summary data for final table
    summary_rows = []

    for i, result in enumerate(worst):
        name = result['target_name']
        original_score = result['score']
        ir_path = result['ir_path']
        category = result['category']
        mode_name = result['mode_name']
        params_dict = result['params']

        print(f"\n{'=' * 100}")
        print(f"  #{i+1}: {name}")
        print(f"  Category: {category} | Optimized mode: {mode_name} | Original score: {original_score:.2f}")
        print(f"  IR: {os.path.basename(ir_path)}")
        print(f"{'=' * 100}")

        # Load target IR
        try:
            target_data, target_sr = load_ir(ir_path)
        except Exception as e:
            print(f"  ERROR loading target IR: {e}")
            continue

        target_profile = analyze_ir(target_data, target_sr, name=f"Target ({name})")

        # Reconstruct Velvet 90 params and render IR
        try:
            sv_params = Velvet90Params.from_dict(params_dict)
        except Exception as e:
            print(f"  ERROR creating Velvet 90 params: {e}")
            continue

        print(f"\n  Velvet 90 params: mode={sv_params.mode}, color={sv_params.color}, "
              f"size={sv_params.size}, room_size={sv_params.room_size:.1f}, "
              f"damping={sv_params.damping:.1f}")
        print(f"    bass_mult={sv_params.bass_mult_x:.2f}, bass_freq={sv_params.bass_freq_hz:.0f}Hz, "
              f"hf_decay={sv_params.hf_decay_x:.2f}, pre_delay={sv_params.pre_delay_ms:.1f}ms")
        print(f"    width={sv_params.width:.1f}, er_late={sv_params.er_late}, "
              f"early_diff={sv_params.early_diff:.1f}, late_diff={sv_params.late_diff:.1f}")

        try:
            # Capture duration: at least 2x the size setting + 2s for tail
            size_seconds = float(sv_params.size.replace('s', ''))
            capture_duration = max(6.0, size_seconds * 2 + 2.0)
            sv_ir = capture_ir(sv_params, sr=target_sr, duration_s=capture_duration)
        except Exception as e:
            print(f"  ERROR capturing Velvet 90 IR: {e}")
            import traceback
            traceback.print_exc()
            continue

        sv_profile = analyze_ir(sv_ir, target_sr, name=f"Velvet 90 ({name})")

        # Compare
        comparison = compare_profiles(target_profile, sv_profile)

        # Print dimension breakdown
        print(f"\n  --- Per-Dimension Scores (0-100) ---")
        print(f"  {'Dimension':<22s} {'Score':>6s}  {'Weight':>6s}  {'Weighted':>8s}  Notes")
        print(f"  {'-'*80}")

        dims = [
            ('Band RT60',         comparison.band_rt60_score,         WEIGHTS['band_rt60']),
            ('EDC Shape',         comparison.edc_shape_score,         WEIGHTS['edc_shape']),
            ('Stereo',            comparison.stereo_score,            WEIGHTS['stereo']),
            ('RT60',              comparison.rt60_score,              WEIGHTS['rt60']),
            ('EDT',               comparison.edt_score,               WEIGHTS['edt']),
            ('Spectral Centroid', comparison.spectral_centroid_score, WEIGHTS['spectral_centroid']),
            ('Spectral Late',     comparison.spectral_late_score,     WEIGHTS['spectral_late']),
            ('Pre-delay',         comparison.pre_delay_score,         WEIGHTS['pre_delay']),
            ('Spectral Early',    comparison.spectral_early_score,    WEIGHTS['spectral_early']),
        ]

        for dim_name, score, weight in dims:
            weighted = score * weight
            # Add context notes
            note = ""
            if dim_name == 'RT60':
                note = f"target={target_profile.rt60:.3f}s, sv={sv_profile.rt60:.3f}s, diff={comparison.rt60_diff_s:+.3f}s"
            elif dim_name == 'EDT':
                note = f"target={target_profile.edt:.3f}s, sv={sv_profile.edt:.3f}s, diff={comparison.edt_diff_s:+.3f}s"
            elif dim_name == 'Pre-delay':
                note = f"target={target_profile.pre_delay_ms:.1f}ms, sv={sv_profile.pre_delay_ms:.1f}ms, diff={comparison.pre_delay_diff_ms:+.1f}ms"
            elif dim_name == 'Stereo':
                note = (f"corr: {target_profile.stereo_correlation:.3f} vs {sv_profile.stereo_correlation:.3f}, "
                        f"width: {target_profile.width_estimate:.3f} vs {sv_profile.width_estimate:.3f}")
            elif dim_name == 'Band RT60':
                note = "(see breakdown below)"

            print(f"  {dim_name:<22s} {score:6.1f}  {weight:6.0%}  {weighted:8.2f}  {note}")

        print(f"  {'-'*80}")
        print(f"  {'OVERALL':<22s} {comparison.overall_score:6.1f}  {'100%':>6s}  {comparison.overall_score:8.2f}")
        print(f"  (Original optimizer score: {original_score:.2f})")

        # Band RT60 detail
        print(f"\n  --- Band RT60 Detail ---")
        print(format_band_rt60_table(
            target_profile.band_rt60,
            sv_profile.band_rt60,
            comparison.band_rt60_diffs
        ))

        # Key diagnostics
        print(f"\n  --- Key Diagnostics ---")
        print(f"    Target duration: {target_profile.duration_s:.2f}s")
        print(f"    Velvet 90 duration: {sv_profile.duration_s:.2f}s")
        print(f"    Target channels: {target_profile.num_channels}")
        print(f"    Target peak amplitude: {target_profile.peak_amplitude:.6f}")
        print(f"    Velvet 90 peak amplitude: {sv_profile.peak_amplitude:.6f}")

        # Identify worst dimensions (sorted by weighted contribution loss)
        max_possible = [(name, weight * 100.0) for name, score, weight in dims]
        actual = [(name, score * weight) for name, score, weight in dims]
        losses = [(name, mp - act) for (name, mp), (_, act) in zip(max_possible, actual)]
        losses.sort(key=lambda x: x[1], reverse=True)

        print(f"\n  --- Largest Score Losses (weighted) ---")
        for dim_name, loss in losses[:5]:
            print(f"    {dim_name:<22s}  lost {loss:5.2f} points")

        # Summary row
        summary_rows.append({
            'rank': i + 1,
            'name': name,
            'category': category,
            'mode': mode_name,
            'overall': comparison.overall_score,
            'original': original_score,
            'band_rt60': comparison.band_rt60_score,
            'edc_shape': comparison.edc_shape_score,
            'stereo': comparison.stereo_score,
            'rt60': comparison.rt60_score,
            'edt': comparison.edt_score,
            'spec_centroid': comparison.spectral_centroid_score,
            'spec_late': comparison.spectral_late_score,
            'pre_delay': comparison.pre_delay_score,
            'spec_early': comparison.spectral_early_score,
            'target_rt60': target_profile.rt60,
            'sv_rt60': sv_profile.rt60,
            'target_edt': target_profile.edt,
            'sv_edt': sv_profile.edt,
            'top_loss_1': losses[0][0],
            'top_loss_2': losses[1][0] if len(losses) > 1 else '',
            'top_loss_3': losses[2][0] if len(losses) > 2 else '',
        })

    # Final summary table
    print(f"\n\n{'=' * 140}")
    print("SUMMARY TABLE — Bottom 10 Normal Reverbs")
    print(f"{'=' * 140}")

    # Header
    header = (f"{'#':>2s}  {'Name':<20s}  {'Cat':<10s}  {'Mode':<14s}  "
              f"{'Score':>5s}  {'BandRT':>6s}  {'EDC':>5s}  {'Stereo':>6s}  "
              f"{'RT60':>5s}  {'EDT':>5s}  {'SpCen':>5s}  {'SpLat':>5s}  "
              f"{'PreDl':>5s}  {'SpEar':>5s}  {'Top Loss Dims'}")
    print(header)
    print("-" * 140)

    for row in summary_rows:
        line = (f"{row['rank']:2d}  {row['name']:<20s}  {row['category']:<10s}  {row['mode']:<14s}  "
                f"{row['overall']:5.1f}  {row['band_rt60']:6.1f}  {row['edc_shape']:5.1f}  {row['stereo']:6.1f}  "
                f"{row['rt60']:5.1f}  {row['edt']:5.1f}  {row['spec_centroid']:5.1f}  {row['spec_late']:5.1f}  "
                f"{row['pre_delay']:5.1f}  {row['spec_early']:5.1f}  "
                f"{row['top_loss_1']}, {row['top_loss_2']}, {row['top_loss_3']}")
        print(line)

    # Aggregate analysis
    print(f"\n\n{'=' * 100}")
    print("AGGREGATE ANALYSIS — Common Weakness Patterns")
    print(f"{'=' * 100}")

    if summary_rows:
        dim_names = ['band_rt60', 'edc_shape', 'stereo', 'rt60', 'edt',
                     'spec_centroid', 'spec_late', 'pre_delay', 'spec_early']
        dim_labels = ['Band RT60', 'EDC Shape', 'Stereo', 'RT60', 'EDT',
                      'Spectral Centroid', 'Spectral Late', 'Pre-delay', 'Spectral Early']
        dim_weights = [0.25, 0.15, 0.12, 0.05, 0.05, 0.05, 0.05, 0.05, 0.03]

        print(f"\n  Average dimension scores across bottom 10:")
        print(f"  {'Dimension':<22s}  {'Avg Score':>9s}  {'Weight':>6s}  {'Avg Weighted Loss':>17s}")
        print(f"  {'-'*60}")
        for label, key, w in zip(dim_labels, dim_names, dim_weights):
            avg = np.mean([r[key] for r in summary_rows])
            avg_loss = (100.0 - avg) * w
            print(f"  {label:<22s}  {avg:9.1f}  {w:6.0%}  {avg_loss:17.2f}")

        # Category breakdown
        from collections import Counter
        cat_counts = Counter(r['category'] for r in summary_rows)
        print(f"\n  Category distribution:")
        for cat, count in cat_counts.most_common():
            print(f"    {cat}: {count}/{NUM_WORST}")

        mode_counts = Counter(r['mode'] for r in summary_rows)
        print(f"\n  Mode distribution:")
        for mode, count in mode_counts.most_common():
            print(f"    {mode}: {count}/{NUM_WORST}")

    print(f"\nAnalysis complete.")


if __name__ == '__main__':
    main()
