#!/usr/bin/env python3
"""
Analyze "improvable" mid-range presets from Velvet 90 v2 batch optimization.

Identifies the 3 worst-scoring normal presets (score >= 40) per category,
re-renders Velvet 90 IRs with their optimized params, and produces detailed
per-dimension analysis to identify which scoring dimensions have the most
room for improvement.
"""

import json
import sys
import os
import numpy as np
from collections import defaultdict

# Ensure we can import local modules
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from ir_analysis import load_ir, analyze_ir, profile_summary, band_key_hz
from ir_compare import compare_profiles, ComparisonResult
from velvet90_capture import Velvet90Params, capture_ir

# --------------------------------------------------------------------------
# Configuration
# --------------------------------------------------------------------------

RESULTS_FILE = os.path.join(os.path.dirname(__file__), "results_v2", "_all_results.json")
NUM_WORST_PER_CATEGORY = 3
MIN_SCORE = 40.0

EXCLUDE_PATTERNS = [
    "Reverse", "Gate", "Inverse", "Non-Lin", "Nonlin",
    "Echo", "Delay", "Slapback", "Spring", "Trampoline",
    "Telephone", "Television", "Infinite",
]

CATEGORIES = ["Halls", "Rooms", "Plates", "Creative"]

# Dimension names matching ComparisonResult fields
DIMENSION_NAMES = [
    "rt60_score", "edt_score", "edc_shape_score",
    "spectral_early_score", "spectral_late_score",
    "spectral_centroid_score", "band_rt60_score",
    "pre_delay_score", "stereo_score",
]

DIMENSION_LABELS = {
    "rt60_score": "RT60",
    "edt_score": "EDT",
    "edc_shape_score": "EDC Shape",
    "spectral_early_score": "Spectral (early)",
    "spectral_late_score": "Spectral (late)",
    "spectral_centroid_score": "Spectral Centroid",
    "band_rt60_score": "Band RT60",
    "pre_delay_score": "Pre-delay",
    "stereo_score": "Stereo",
}

# Weights from ir_compare.py for reference
WEIGHTS = {
    "rt60_score": 0.25,
    "edt_score": 0.05,
    "edc_shape_score": 0.15,
    "spectral_early_score": 0.03,
    "spectral_late_score": 0.05,
    "spectral_centroid_score": 0.05,
    "band_rt60_score": 0.25,
    "pre_delay_score": 0.05,
    "stereo_score": 0.12,
}


def is_normal(name: str) -> bool:
    """Check if a preset name is 'normal' (not a special effect type)."""
    for pat in EXCLUDE_PATTERNS:
        if pat.lower() in name.lower():
            return False
    return True


def select_improvable_presets(results: list) -> list:
    """Select the 3 worst-scoring normal presets per category with score >= MIN_SCORE."""
    by_cat = defaultdict(list)
    for r in results:
        if is_normal(r["target_name"]):
            by_cat[r["category"]].append(r)

    selected = []
    for cat in CATEGORIES:
        items = sorted(by_cat.get(cat, []), key=lambda x: x["score"])
        above_min = [i for i in items if i["score"] >= MIN_SCORE]
        worst = above_min[:NUM_WORST_PER_CATEGORY]
        selected.extend(worst)
    return selected


def analyze_preset(preset_result: dict) -> dict:
    """
    For a single preset result:
    1. Load target IR
    2. Render Velvet 90 IR with optimized params
    3. Compare and return detailed results
    """
    name = preset_result["target_name"]
    ir_path = preset_result["ir_path"]
    params_dict = preset_result["params"]
    category = preset_result["category"]
    original_score = preset_result["score"]

    print(f"\n{'='*70}")
    print(f"  {name} (Category: {category}, Original Score: {original_score:.1f})")
    print(f"{'='*70}")

    # Load target IR
    print(f"  Loading target IR: {os.path.basename(ir_path)}")
    target_data, target_sr = load_ir(ir_path)
    target_profile = analyze_ir(target_data, target_sr, name=f"Target: {name}")

    # Render Velvet 90 IR with optimized params
    print(f"  Rendering Velvet 90 IR with optimized params...")
    params = Velvet90Params.from_dict(params_dict)

    # Determine capture duration based on target RT60 (at least 3x RT60 or 6s)
    rt60 = target_profile.rt60 if target_profile.rt60 and target_profile.rt60 > 0 else 2.0
    capture_duration = max(6.0, rt60 * 3.0)
    velvet90_ir = capture_ir(params, sr=target_sr, duration_s=capture_duration)
    velvet90_profile = analyze_ir(velvet90_ir, target_sr, name=f"Velvet 90: {name}")

    # Compare
    comparison = compare_profiles(target_profile, velvet90_profile)

    # Print per-dimension scores
    print(f"\n  Per-Dimension Scores:")
    print(f"  {'Dimension':<22} {'Score':>7} {'Weight':>7} {'Weighted':>9}")
    print(f"  {'-'*22} {'-'*7} {'-'*7} {'-'*9}")

    for dim in DIMENSION_NAMES:
        score = getattr(comparison, dim)
        weight = WEIGHTS[dim]
        weighted = score * weight
        label = DIMENSION_LABELS[dim]
        print(f"  {label:<22} {score:>7.1f} {weight:>7.2f} {weighted:>9.2f}")

    print(f"  {'-'*22} {'-'*7} {'-'*7} {'-'*9}")
    print(f"  {'Overall':<22} {comparison.overall_score:>7.1f}")

    # RT60 diagnostics
    print(f"\n  RT60 Diagnostics:")
    print(f"    Target RT60:   {target_profile.rt60:.3f}s")
    print(f"    Velvet 90 RT60: {velvet90_profile.rt60:.3f}s")
    print(f"    Diff:          {comparison.rt60_diff_s:+.3f}s ({comparison.rt60_diff_s / max(target_profile.rt60, 0.01) * 100:+.1f}%)")
    print(f"    Target EDT:    {target_profile.edt:.3f}s")
    print(f"    Velvet 90 EDT:  {velvet90_profile.edt:.3f}s")
    print(f"    Diff:          {comparison.edt_diff_s:+.3f}s")

    # Pre-delay diagnostics
    print(f"\n  Pre-delay Diagnostics:")
    print(f"    Target:   {target_profile.pre_delay_ms:.1f}ms")
    print(f"    Velvet 90: {velvet90_profile.pre_delay_ms:.1f}ms")
    print(f"    Diff:     {comparison.pre_delay_diff_ms:+.1f}ms")

    # Stereo diagnostics
    print(f"\n  Stereo Diagnostics:")
    print(f"    Target correlation:   {target_profile.stereo_correlation:.3f}")
    print(f"    Velvet 90 correlation: {velvet90_profile.stereo_correlation:.3f}")
    print(f"    Target width:         {target_profile.width_estimate:.3f}")
    print(f"    Velvet 90 width:       {velvet90_profile.width_estimate:.3f}")

    # Per-band RT60 comparison
    print(f"\n  Per-Band RT60 Comparison:")
    print(f"    {'Band':<8} {'Target':>8} {'Velvet 90':>10} {'Diff':>8} {'Ratio':>7}")
    print(f"    {'-'*8} {'-'*8} {'-'*10} {'-'*8} {'-'*7}")

    all_bands = sorted(
        set(target_profile.band_rt60.keys()) | set(velvet90_profile.band_rt60.keys()),
        key=lambda x: band_key_hz(x)
    )
    for band in all_bands:
        t_val = target_profile.band_rt60.get(band, 0.0)
        s_val = velvet90_profile.band_rt60.get(band, 0.0)
        diff = s_val - t_val
        ratio = s_val / t_val if t_val > 0.01 else float('inf')
        print(f"    {band:<8} {t_val:>8.3f}s {s_val:>10.3f}s {diff:>+8.3f}s {ratio:>7.2f}x")

    # Key Velvet 90 params used
    print(f"\n  Velvet 90 Params:")
    print(f"    Mode: {params.mode}, Color: {params.color}, Size: {params.size}")
    print(f"    Room Size: {params.room_size:.1f}, Damping: {params.damping:.1f}")
    print(f"    Bass Mult: {params.bass_mult_x:.2f}x @ {params.bass_freq_hz:.0f}Hz")
    print(f"    HF Decay: {params.hf_decay_x:.2f}x, Mid Decay: {params.mid_decay_x:.2f}x")
    print(f"    High Freq: {params.high_freq_hz:.0f}Hz")
    print(f"    ER/Late: {params.er_late}, Width: {params.width:.0f}%")
    print(f"    Pre-delay: {params.pre_delay_ms:.1f}ms")

    return {
        "name": name,
        "category": category,
        "original_score": original_score,
        "comparison": comparison,
        "target_profile": target_profile,
        "velvet90_profile": velvet90_profile,
        "params": params,
    }


def print_cross_preset_summary(all_results: list):
    """Print aggregate analysis across all improvable presets."""
    print(f"\n\n{'#'*70}")
    print(f"  CROSS-PRESET SUMMARY")
    print(f"{'#'*70}")

    # Aggregate dimension scores
    print(f"\n  Average Score by Dimension (across {len(all_results)} presets):")
    print(f"  {'Dimension':<22} {'Avg Score':>10} {'Min Score':>10} {'Max Score':>10} {'Weight':>7} {'Impact':>8}")
    print(f"  {'-'*22} {'-'*10} {'-'*10} {'-'*10} {'-'*7} {'-'*8}")

    dim_stats = {}
    for dim in DIMENSION_NAMES:
        scores = [getattr(r["comparison"], dim) for r in all_results]
        avg = np.mean(scores)
        mn = np.min(scores)
        mx = np.max(scores)
        weight = WEIGHTS[dim]
        # "Impact" = how much this dimension drags down the overall score
        # If perfect (100), the weighted contribution would be weight * 100
        # The "lost" points = weight * (100 - avg)
        lost = weight * (100 - avg)
        label = DIMENSION_LABELS[dim]
        dim_stats[dim] = {"avg": avg, "min": mn, "max": mx, "weight": weight, "lost": lost}
        print(f"  {label:<22} {avg:>10.1f} {mn:>10.1f} {mx:>10.1f} {weight:>7.2f} {lost:>8.2f}")

    total_lost = sum(d["lost"] for d in dim_stats.values())
    print(f"  {'-'*22} {'-'*10} {'-'*10} {'-'*10} {'-'*7} {'-'*8}")
    print(f"  {'Total lost points':<22} {'':>10} {'':>10} {'':>10} {'':>7} {total_lost:>8.2f}")

    # Rank dimensions by impact (most room for improvement)
    print(f"\n  Dimensions Ranked by Improvement Potential (lost weighted points):")
    ranked = sorted(dim_stats.items(), key=lambda x: x[1]["lost"], reverse=True)
    for rank, (dim, stats) in enumerate(ranked, 1):
        label = DIMENSION_LABELS[dim]
        print(f"    {rank}. {label:<22} lost {stats['lost']:.2f} pts  (avg score: {stats['avg']:.1f}, weight: {stats['weight']:.2f})")

    # Per-category breakdown
    print(f"\n  Per-Category Breakdown:")
    by_cat = defaultdict(list)
    for r in all_results:
        by_cat[r["category"]].append(r)

    for cat in CATEGORIES:
        if cat not in by_cat:
            continue
        cat_results = by_cat[cat]
        print(f"\n  --- {cat} ({len(cat_results)} presets) ---")
        for r in cat_results:
            comp = r["comparison"]
            print(f"    {r['name']:<25} Overall: {comp.overall_score:.1f}")
            # Show the 3 worst dimensions for this preset
            dim_scores = [(dim, getattr(comp, dim)) for dim in DIMENSION_NAMES]
            dim_scores.sort(key=lambda x: x[1])
            print(f"      Weakest dimensions:")
            for dim, score in dim_scores[:3]:
                label = DIMENSION_LABELS[dim]
                print(f"        {label:<22} {score:.1f}")

    # Common patterns in band RT60
    print(f"\n  Band RT60 Patterns:")
    band_diffs_all = defaultdict(list)
    for r in all_results:
        comp = r["comparison"]
        for band, diff in comp.band_rt60_diffs.items():
            band_diffs_all[band].append(diff)

    print(f"    {'Band':<8} {'Avg Diff':>10} {'Median':>10} {'% Too Long':>12} {'% Too Short':>13}")
    print(f"    {'-'*8} {'-'*10} {'-'*10} {'-'*12} {'-'*13}")
    bands_sorted = sorted(band_diffs_all.keys(), key=band_key_hz)
    for band in bands_sorted:
        diffs = band_diffs_all[band]
        avg_diff = np.mean(diffs)
        med_diff = np.median(diffs)
        too_long = sum(1 for d in diffs if d > 0.05) / len(diffs) * 100
        too_short = sum(1 for d in diffs if d < -0.05) / len(diffs) * 100
        print(f"    {band:<8} {avg_diff:>+10.3f}s {med_diff:>+10.3f}s {too_long:>11.0f}% {too_short:>12.0f}%")

    # Stereo patterns
    print(f"\n  Stereo Patterns:")
    for r in all_results:
        tp = r["target_profile"]
        sp = r["velvet90_profile"]
        print(f"    {r['name']:<25} corr: {tp.stereo_correlation:.3f} -> {sp.stereo_correlation:.3f}"
              f"  width: {tp.width_estimate:.3f} -> {sp.width_estimate:.3f}")


def main():
    print("Loading v2 batch optimization results...")
    with open(RESULTS_FILE) as f:
        data = json.load(f)

    results = data["results"]
    print(f"Total results: {len(results)}")

    # Select improvable presets
    selected = select_improvable_presets(results)
    print(f"\nSelected {len(selected)} improvable presets:")
    for s in selected:
        print(f"  {s['category']:<12} {s['score']:>5.1f}  {s['target_name']}")

    # Analyze each preset
    all_analysis = []
    for preset in selected:
        try:
            result = analyze_preset(preset)
            all_analysis.append(result)
        except Exception as e:
            print(f"\n  ERROR analyzing {preset['target_name']}: {e}")
            import traceback
            traceback.print_exc()

    # Cross-preset summary
    if all_analysis:
        print_cross_preset_summary(all_analysis)

    print(f"\n\nDone. Analyzed {len(all_analysis)} / {len(selected)} presets.")


if __name__ == "__main__":
    main()
