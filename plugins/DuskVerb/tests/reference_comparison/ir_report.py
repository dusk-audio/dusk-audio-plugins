#!/usr/bin/env python3
"""
IR Analysis Report — comprehensive DuskVerb vs ReferenceReverb comparison.

Loads captured IRs, runs all 5 analysis categories, and produces:
  - ir_report.json       (machine-readable)
  - ir_report_summary.txt (human-readable with priority matrix)

Usage:
    python3 ir_report.py                        # Full report
    python3 ir_report.py --ir-dir ./my_irs      # Custom IR directory
    python3 ir_report.py --preset "Fat Snare"   # Single preset
    python3 ir_report.py --json-only            # Skip text summary
    python3 ir_report.py -j 4                   # Worker count
"""

import argparse
import json
import multiprocessing
import os
import sys
import datetime
import numpy as np
import soundfile as sf

import ir_analysis as analysis
from config import SAMPLE_RATE

SR = SAMPLE_RATE

# Gate presets — flag as SKIP, don't include in aggregate scores
GATE_PRESETS = {"Gated Snare", "Tight Ambience Gate", "Big Ambience Gate"}

# Pass criteria per analysis category
PASS_CRITERIA = {
    "er":        {"match_rate": 0.70},
    "diffusion": {"onset_delta_ms": 15.0},
    "spectral":  {"band_pass_rate": 0.80},
    "modulation": {"depth_delta_cents": 5.0},
    "size":      {"iacc_delta": 0.15},
}


# ---------------------------------------------------------------------------
# Single-preset analysis
# ---------------------------------------------------------------------------
def analyze_preset(ir_dir, entry, sr):
    """Run all 5 analyses on a single preset pair.

    Args:
        ir_dir: base directory containing the IR files
        entry: dict from capture_log.json
        sr: sample rate

    Returns:
        dict with all analysis results and pass/fail per category
    """
    name = entry["name"]
    is_gate = entry.get("is_gate", name in GATE_PRESETS)

    dv_path = os.path.join(ir_dir, entry["dv_path"])
    vv_path = os.path.join(ir_dir, entry["vv_path"])

    dv_data, _ = sf.read(dv_path)
    vv_data, _ = sf.read(vv_path)

    dv_l, dv_r = dv_data[:, 0], dv_data[:, 1]
    vv_l, vv_r = vv_data[:, 0], vv_data[:, 1]

    # Time-align
    from reverb_metrics import align_ir_pair
    dv_l_a, vv_l_a, offset = align_ir_pair(dv_l, vv_l, sr)
    if offset > 0:
        dv_r_a = dv_r[:len(dv_l_a)]
        vv_r_a = vv_r[offset:offset + len(dv_l_a)]
    elif offset < 0:
        dv_r_a = dv_r[-offset:-offset + len(vv_l_a)]
        vv_r_a = vv_r[:len(vv_l_a)]
    else:
        min_len = min(len(dv_l), len(vv_l))
        dv_l_a, vv_l_a = dv_l[:min_len], vv_l[:min_len]
        dv_r_a, vv_r_a = dv_r[:min_len], vv_r[:min_len]

    algo = entry.get("dv_algorithm", "")

    # 1. ER comparison
    er = analysis.compare_er(dv_l_a, dv_r_a, vv_l_a, vv_r_a, sr,
                             algo_hint=algo)

    # 2. Diffusion comparison
    diffusion = analysis.compare_diffusion(dv_l_a, vv_l_a, sr)

    # 3. Spectral decay comparison
    spectral = analysis.compare_spectral_decay(dv_l_a, vv_l_a, sr)

    # 4. Modulation comparison
    modulation = analysis.compare_modulation(dv_l_a, vv_l_a, sr)

    # 5. Size/space comparison
    size = analysis.compare_size(dv_l_a, dv_r_a, vv_l_a, vv_r_a, sr)

    # Pass/fail per category
    er_pass = er["match_rate"] >= PASS_CRITERIA["er"]["match_rate"]

    diff_onset = diffusion.get("onset_delta_ms")
    diff_pass = (diff_onset is not None and
                 abs(diff_onset) <= PASS_CRITERIA["diffusion"]["onset_delta_ms"])

    band_total = spectral["bands_total"]
    spectral_pass = (band_total > 0 and
                     spectral["bands_passing"] / band_total >=
                     PASS_CRITERIA["spectral"]["band_pass_rate"])

    mod_depth = modulation.get("depth_delta_cents")
    mod_pass = (mod_depth is not None and
                abs(mod_depth) <= PASS_CRITERIA["modulation"]["depth_delta_cents"])

    iacc_e = size.get("iacc_early_delta")
    iacc_l = size.get("iacc_late_delta")
    size_pass = (iacc_e is not None and iacc_l is not None and
                 abs(iacc_e) <= PASS_CRITERIA["size"]["iacc_delta"] and
                 abs(iacc_l) <= PASS_CRITERIA["size"]["iacc_delta"])

    return {
        "name": name,
        "mode": entry.get("mode", ""),
        "algorithm": algo,
        "is_gate": is_gate,
        "er": _serialize_er(er),
        "diffusion": _serialize_diffusion(diffusion),
        "spectral_decay": _serialize_spectral(spectral),
        "modulation": _serialize_modulation(modulation),
        "size": _serialize_size(size),
        "pass_er": er_pass,
        "pass_diffusion": diff_pass,
        "pass_spectral": spectral_pass,
        "pass_modulation": mod_pass,
        "pass_size": size_pass,
    }


def _serialize_er(er):
    """Extract JSON-safe summary from ER comparison result."""
    return {
        "match_rate": round(er["match_rate"], 3),
        "num_matched": er["num_matched"],
        "num_vv_taps": er["num_vv_taps"],
        "num_dv_taps": er["num_dv_taps"],
        "mean_time_error_ms": round(er["mean_time_error_ms"], 2),
        "mean_level_error_db": round(er["mean_level_error_db"], 1),
        "window_widened": er.get("window_widened", False),
        "match_window_ms": er.get("match_window_ms", 2.0),
    }


def _serialize_diffusion(d):
    return {
        "onset_delta_ms": round(d["onset_delta_ms"], 1) if d["onset_delta_ms"] is not None else None,
        "dv_onset_ms": d["dv"]["onset_ms"],
        "vv_onset_ms": d["vv"]["onset_ms"],
        "plateau_delta": round(d["plateau_delta"], 2) if d["plateau_delta"] is not None else None,
        "dv_time_to_gaussian": d["dv"]["time_to_gaussian"],
        "vv_time_to_gaussian": d["vv"]["time_to_gaussian"],
    }


def _serialize_spectral(s):
    return {
        "bands_passing": s["bands_passing"],
        "bands_total": s["bands_total"],
        "worst_band": s["worst_band"],
        "worst_ratio": s["worst_ratio"],
    }


def _serialize_modulation(m):
    return {
        "both_reliable": m["both_reliable"],
        "rate_delta_hz": m.get("rate_delta_hz"),
        "depth_delta_cents": m.get("depth_delta_cents"),
        "shape_match": m.get("shape_match"),
        "dv_depth": m["dv"]["depth_cents"],
        "vv_depth": m["vv"]["depth_cents"],
        "dv_shape": m["dv"]["shape"],
        "vv_shape": m["vv"]["shape"],
    }


def _serialize_size(s):
    return {
        "iacc_early_delta": s["iacc_early_delta"],
        "iacc_late_delta": s["iacc_late_delta"],
        "free_path_delta_ms": s["free_path_delta_ms"],
        "t_first_delta_ms": s["t_first_delta_ms"],
        "dv_iacc_early": s["dv"]["iacc_early"],
        "vv_iacc_early": s["vv"]["iacc_early"],
    }


# ---------------------------------------------------------------------------
# Parallel worker support
# ---------------------------------------------------------------------------
_worker_ir_dir = None
_worker_sr = None


def _init_report_worker(ir_dir, sr):
    global _worker_ir_dir, _worker_sr
    _worker_ir_dir = ir_dir
    _worker_sr = sr


def _worker_analyze(entry):
    return analyze_preset(_worker_ir_dir, entry, _worker_sr)


# ---------------------------------------------------------------------------
# Report generation
# ---------------------------------------------------------------------------
def generate_text_report(results, mod_unreliable_count, total_non_gate):
    """Generate human-readable text report with priority matrix."""
    lines = []
    L = lines.append

    L("=" * 79)
    L("DuskVerb vs ReferenceReverb — IR Analysis Report")
    L(f"Generated: {datetime.date.today().isoformat()}  |  "
      f"{total_non_gate} presets  |  5 algorithms")
    L("=" * 79)

    # Filter out gate presets for aggregates
    active = [r for r in results if not r["is_gate"]]

    # Overview
    er_pass = sum(1 for r in active if r["pass_er"])
    diff_pass = sum(1 for r in active if r["pass_diffusion"])
    spec_pass = sum(1 for r in active if r["pass_spectral"])
    mod_pass = sum(1 for r in active if r["pass_modulation"])
    size_pass = sum(1 for r in active if r["pass_size"])
    n = len(active)

    L("")
    L("OVERVIEW")
    L(f"  ER Match:        {er_pass}/{n} presets with >70% tap match rate")
    L(f"  Diffusion:       {diff_pass}/{n} presets with onset within ±15ms")
    L(f"  Spectral Decay:  {spec_pass}/{n} presets with >80% bands passing")
    if mod_unreliable_count > n * 0.3:
        L(f"  Modulation:      {mod_pass}/{n} "
          f"(UNRELIABLE — {mod_unreliable_count}/{n} presets had analysis failures)")
    else:
        L(f"  Modulation:      {mod_pass}/{n} presets with depth within ±5 cents")
    L(f"  Size/Space:      {size_pass}/{n} presets with IACC within ±0.15")

    # Per-algorithm summary
    L("")
    L("PER-ALGORITHM SUMMARY")
    L(f"  {'Algorithm':<14s} {'ER Match':>10s} {'Diffusion':>10s} "
      f"{'Spectral':>10s} {'Mod Match':>10s} {'Size Match':>11s}")

    algos_seen = []
    for r in results:
        if r["algorithm"] not in algos_seen:
            algos_seen.append(r["algorithm"])

    per_algo = {}
    for algo in algos_seen:
        algo_results = [r for r in active if r["algorithm"] == algo]
        if not algo_results:
            continue
        na = len(algo_results)
        e = sum(1 for r in algo_results if r["pass_er"])
        d = sum(1 for r in algo_results if r["pass_diffusion"])
        s = sum(1 for r in algo_results if r["pass_spectral"])
        m = sum(1 for r in algo_results if r["pass_modulation"])
        z = sum(1 for r in algo_results if r["pass_size"])
        L(f"  {algo:<14s} {e:>4d}/{na:<5d} {d:>4d}/{na:<5d} "
          f"{s:>4d}/{na:<5d} {m:>4d}/{na:<5d} {z:>5d}/{na:<5d}")
        per_algo[algo] = {
            "er_pass": e, "diff_pass": d, "spec_pass": s,
            "mod_pass": m, "size_pass": z, "total": na}

    # Worst offenders (failing 3+ categories)
    L("")
    L("WORST OFFENDERS (presets failing 3+ categories)")
    worst = []
    for r in active:
        fails = sum(1 for k in ["pass_er", "pass_diffusion", "pass_spectral",
                                 "pass_modulation", "pass_size"]
                     if not r[k])
        if fails >= 3:
            worst.append((r["name"], r["algorithm"], fails, r))

    worst.sort(key=lambda x: -x[2])
    if worst:
        for name, algo, n_fails, r in worst[:10]:
            details = []
            if not r["pass_er"]:
                details.append(f"ER:{r['er']['match_rate']:.0%}")
            if not r["pass_spectral"]:
                details.append(f"Spec:{r['spectral_decay']['bands_passing']}/"
                             f"{r['spectral_decay']['bands_total']}")
            if not r["pass_size"]:
                iacc = r['size'].get('iacc_early_delta', 0)
                details.append(f"IACC:{iacc:+.2f}" if iacc is not None else "IACC:N/A")
            L(f"  {name} ({algo}) — {', '.join(details)}")
    else:
        L("  None — all presets fail fewer than 3 categories")

    # Gate presets note
    gate_presets = [r for r in results if r["is_gate"]]
    if gate_presets:
        L("")
        L(f"EXCLUDED (gate presets, no DV equivalent): "
          f"{', '.join(r['name'] for r in gate_presets)}")

    # Modulation reliability warning
    if mod_unreliable_count > n * 0.3:
        L("")
        L("WARNING: Modulation analysis unreliable — needs alternate approach")
        L(f"  {mod_unreliable_count}/{n} presets returned unreliable modulation data.")
        L("  Hilbert transform phase tracking may not be suitable for these IRs.")
        L("  Consider: autocorrelation-based method, or longer analysis windows.")

    # ===== PRIORITY MATRIX =====
    L("")
    L("=" * 79)
    L("PRIORITY MATRIX — Next Development Actions")
    L("=" * 79)
    L("")
    L("Scoring: Impact (fail count) × 1/Complexity (LOW=1, MED=2, HIGH=4)")
    L(f"  {'#':<4s} {'Action':<55s} {'Impact':>7s} {'Cmplx':>6s} {'Score':>6s}")
    L(f"  {'─'*4} {'─'*55} {'─'*7} {'─'*6} {'─'*6}")

    # Build priority items from analysis data
    priorities = _build_priority_matrix(active, per_algo, mod_unreliable_count, n)
    priorities.sort(key=lambda x: -x["score"])

    for i, item in enumerate(priorities, 1):
        L(f"  {i:<4d} {item['action']:<55s} "
          f"{item['impact']:>7d} {item['complexity']:>6s} {item['score']:>6.1f}")

    L("")
    L("Complexity: LOW=1 (config/param change), MED=2 (algorithm tweak), "
      "HIGH=4 (architecture)")
    L("Score = Impact × (1/Complexity_weight)")

    return "\n".join(lines)


def _build_priority_matrix(active, per_algo, mod_unreliable, total):
    """Build priority items from analysis results."""
    items = []

    # ER-related priorities per algorithm
    for algo, stats in per_algo.items():
        er_fail = stats["total"] - stats["er_pass"]
        if er_fail > 0:
            items.append({
                "action": f"ER timing/pattern: {algo} ({er_fail} presets failing)",
                "impact": er_fail,
                "complexity": "MED",
                "score": er_fail / 2.0,
            })

    # Spectral decay priorities per algorithm
    for algo, stats in per_algo.items():
        spec_fail = stats["total"] - stats["spec_pass"]
        if spec_fail > 0:
            items.append({
                "action": f"HF/LF decay balance: {algo} ({spec_fail} presets failing)",
                "impact": spec_fail,
                "complexity": "LOW",
                "score": spec_fail / 1.0,
            })

    # Diffusion priorities
    for algo, stats in per_algo.items():
        diff_fail = stats["total"] - stats["diff_pass"]
        if diff_fail > 0:
            items.append({
                "action": f"Diffusion onset/buildup: {algo} ({diff_fail} presets failing)",
                "impact": diff_fail,
                "complexity": "MED",
                "score": diff_fail / 2.0,
            })

    # Size/IACC priorities
    for algo, stats in per_algo.items():
        size_fail = stats["total"] - stats["size_pass"]
        if size_fail > 0:
            items.append({
                "action": f"Stereo width/IACC: {algo} ({size_fail} presets failing)",
                "impact": size_fail,
                "complexity": "LOW",
                "score": size_fail / 1.0,
            })

    # Modulation priorities
    if mod_unreliable > total * 0.3:
        items.append({
            "action": "Modulation analysis: replace Hilbert with robust method",
            "impact": mod_unreliable,
            "complexity": "HIGH",
            "score": mod_unreliable / 4.0,
        })
    else:
        for algo, stats in per_algo.items():
            mod_fail = stats["total"] - stats["mod_pass"]
            if mod_fail > 0:
                items.append({
                    "action": f"Modulation depth/rate: {algo} ({mod_fail} presets failing)",
                    "impact": mod_fail,
                    "complexity": "LOW",
                    "score": mod_fail / 1.0,
                })

    # Cross-cutting: gate mode
    gate_count = len(GATE_PRESETS)
    items.append({
        "action": f"Gate mode DSP: implement for {gate_count} presets",
        "impact": gate_count,
        "complexity": "HIGH",
        "score": gate_count / 4.0,
    })

    return items


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="IR Analysis Report")
    parser.add_argument("--ir-dir", type=str,
                        default=os.path.join(os.path.dirname(__file__), "irs"),
                        help="Directory containing captured IRs")
    parser.add_argument("--preset", type=str,
                        help="Analyze a single preset")
    parser.add_argument("--json-only", action="store_true",
                        help="Skip text summary")
    parser.add_argument("-j", "--jobs", type=int, default=0,
                        help="Number of workers (0=auto)")
    parser.add_argument("--serial", action="store_true",
                        help="Single-threaded mode")
    args = parser.parse_args()

    ir_dir = args.ir_dir
    log_path = os.path.join(ir_dir, "capture_log.json")

    if not os.path.exists(log_path):
        print(f"ERROR: {log_path} not found. Run capture_irs.py first.")
        return

    with open(log_path) as f:
        capture_log = json.load(f)

    entries = capture_log.get("presets", [])
    if args.preset:
        entries = [e for e in entries
                   if args.preset.lower() in e["name"].lower()]

    if not entries:
        print("No matching presets found.")
        return

    n_workers = args.jobs if args.jobs > 0 else min(multiprocessing.cpu_count(), 8)
    use_parallel = not args.serial and len(entries) > 1

    print(f"Analyzing {len(entries)} preset pairs...")

    if use_parallel:
        print(f"  Using {n_workers} workers")
        with multiprocessing.Pool(
            processes=n_workers,
            initializer=_init_report_worker,
            initargs=(ir_dir, SR),
        ) as pool:
            results = []
            for result in pool.imap_unordered(_worker_analyze, entries):
                results.append(result)
                sys.stdout.write(
                    f"\r  Analyzed {len(results)}/{len(entries)}: "
                    f"{result['name']:<35s}")
                sys.stdout.flush()
        print()
    else:
        results = []
        for entry in entries:
            result = analyze_preset(ir_dir, entry, SR)
            results.append(result)
            print(f"  {result['name']}")

    # Sort by name for deterministic output
    results.sort(key=lambda r: r["name"])

    # Count modulation reliability
    active = [r for r in results if not r["is_gate"]]
    mod_unreliable = sum(1 for r in active
                         if not r["modulation"]["both_reliable"])

    # Build JSON report
    report = {
        "metadata": {
            "generated": datetime.datetime.now().isoformat(),
            "sample_rate": SR,
            "num_presets": len(active),
            "num_gate_excluded": sum(1 for r in results if r["is_gate"]),
        },
        "presets": {r["name"]: r for r in results},
    }

    json_path = os.path.join(ir_dir, "ir_report.json")
    with open(json_path, "w") as f:
        json.dump(report, f, indent=2, default=str)
    print(f"\nJSON report: {json_path}")

    # Generate text report
    if not args.json_only:
        text = generate_text_report(results, mod_unreliable, len(active))
        txt_path = os.path.join(ir_dir, "ir_report_summary.txt")
        with open(txt_path, "w") as f:
            f.write(text)
        print(f"Text report: {txt_path}")

        # Also print to stdout
        print()
        print(text)

    print("\nDone.")


if __name__ == "__main__":
    main()
