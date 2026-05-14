#!/usr/bin/env python3
"""Universal reverb tuner — coarse grid + CMA-ES refinement against pre-
rendered reference WAVs, auto-patches FactoryPresets.h.

Phase 1: --measure-only smoke test (validates config + renderer + metrics
end-to-end without sweeping). Phases 2-5 (grid, optimize, patch, report)
land in follow-up commits.

Usage:
  tune_reverb.py <config.yaml> --measure-only
  tune_reverb.py <config.yaml> --grid-only         (not yet)
  tune_reverb.py <config.yaml>                     (full pipeline, not yet)
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

# Make the tuner package importable when the script is invoked directly
# (python3 plugins/DuskVerb/tools/tune_reverb.py ...).
sys.path.insert(0, str(Path(__file__).resolve().parent))

from tuner import config as cfg_mod
from tuner import grid as grid_mod
from tuner import metrics as metrics_mod
from tuner import optimize as optimize_mod
from tuner import render as render_mod


def cmd_measure_only(cfg: cfg_mod.TuneConfig) -> int:
    """Render once at locked_params (no sweep), measure vs anchor, print report."""
    if not cfg.renderer_bin.exists():
        print(f"  ! renderer binary not found: {cfg.renderer_bin}", file=sys.stderr)
        return 2
    if not cfg.anchor_impulse.exists() or not cfg.anchor_noiseburst.exists():
        print(f"  ! anchor WAVs missing:\n    {cfg.anchor_impulse}\n    "
              f"{cfg.anchor_noiseburst}", file=sys.stderr)
        return 2

    print(f"== {cfg.name} — measure-only smoke test ==")
    print(f"   preset:   {cfg.target_preset!r}")
    print(f"   engine:   algo {cfg.engine_algorithm}")
    print(f"   anchor:   {cfg.anchor_impulse.name}")
    print(f"   n_renders={cfg.n_renders} agg={cfg.aggregate}")
    print()

    # Measure anchor once (uncached — the anchor WAV is the source of truth).
    print("  measuring anchor…")
    anchor = metrics_mod.measure_anchor(cfg.anchor_impulse, cfg.anchor_noiseburst)

    # For measure-only, the sweep axes are not exercised — feed the locked
    # params plus the LOW end of each axis (or its single default) so we
    # produce a deterministic baseline render the user can A/B against the
    # anchor before launching a sweep.
    params = dict(cfg.locked_params)
    for axis_name, ax in cfg.search_space.items():
        params[axis_name] = f"{ax.lo:g}"

    print(f"  rendering {cfg.n_renders}× at locked params…")
    meas = render_mod.measure_params(
        params=params,
        target_preset=cfg.target_preset,
        n_renders=cfg.n_renders,
        aggregate=cfg.aggregate,
        renderer_bin=cfg.renderer_bin,
        output_dir=cfg.run_dir / "renders",
        cache_dir=cfg.cache_dir,
        measure_fn=metrics_mod.measure_pair,
        slug_prefix=f"{cfg.name}_baseline",
    )

    warnings = meas.pop("_warnings", None)
    print()
    print(metrics_mod.format_report(meas, anchor, cfg.metrics))
    if warnings:
        print()
        print("Variance warnings (LFO non-determinism canary):")
        for w in warnings:
            print(f"  ! {w}")
    counters = render_mod.counters()
    print()
    print(f"renders: {counters['renders']}  cache_hits: {counters['cache_hits']}")
    return 0


def cmd_grid_only(cfg: cfg_mod.TuneConfig, workers: int | None) -> int:
    """Run coarse grid sweep → CSV + heatmap PNGs + top-N JSON. No optimizer, no patch."""
    if not cfg.renderer_bin.exists():
        print(f"  ! renderer binary not found: {cfg.renderer_bin}", file=sys.stderr)
        return 2
    if not cfg.anchor_impulse.exists() or not cfg.anchor_noiseburst.exists():
        print(f"  ! anchor WAVs missing", file=sys.stderr)
        return 2
    if not cfg.search_space:
        print(f"  ! search_space is empty — nothing to sweep", file=sys.stderr)
        return 2

    print(f"== {cfg.name} — grid sweep ==")
    print(f"   preset:   {cfg.target_preset!r}")
    print(f"   engine:   algo {cfg.engine_algorithm}")
    print(f"   anchor:   {cfg.anchor_impulse.name}")
    print()
    print("  measuring anchor…")
    anchor = metrics_mod.measure_anchor(cfg.anchor_impulse, cfg.anchor_noiseburst)

    results = grid_mod.run(cfg, anchor, workers=workers)

    csv_path = cfg.run_dir / "grid.csv"
    summary_path = cfg.run_dir / "grid_summary.json"
    heatmap_dir = cfg.run_dir / "grid_heatmaps"
    grid_mod.write_csv(results, cfg, csv_path)
    grid_mod.write_summary(results, cfg, summary_path)
    n_pngs = grid_mod.write_heatmaps(results, cfg, heatmap_dir)

    counters = render_mod.counters()
    print()
    print(f"wrote {csv_path}")
    print(f"wrote {summary_path}")
    print(f"wrote {n_pngs} heatmap(s) to {heatmap_dir}")
    print(f"renders: {counters['renders']}  cache_hits: {counters['cache_hits']}")

    # Print top-5 inline so the user doesn't need to grep the JSON.
    import json as _json
    with open(summary_path) as f:
        summary = _json.load(f)
    print()
    print(f"top {min(5, len(summary['top_n']))} cells by composite loss:")
    for r in summary["top_n"][:5]:
        axes = "  ".join(f"{k}={v:g}" for k, v in r["axes"].items())
        print(f"  loss={r['loss']:.3f}  {axes}")
    return 0


def _load_grid_warm_start(cfg: cfg_mod.TuneConfig) -> dict[str, float] | None:
    """Pick warm-start axes from `grid_summary.json` if present in run_dir.
    None when no prior grid run exists."""
    import json as _json
    summary_path = cfg.run_dir / "grid_summary.json"
    if not summary_path.exists():
        return None
    try:
        with open(summary_path) as f:
            summary = _json.load(f)
        best = summary.get("best")
        if best and "axes" in best:
            return {k: float(v) for k, v in best["axes"].items()}
    except (OSError, _json.JSONDecodeError, KeyError, TypeError):
        pass
    return None


def cmd_optimize(cfg: cfg_mod.TuneConfig, workers: int | None,
                 skip_grid: bool) -> int:
    """Grid (warm-start) + optimizer. No auto-patch — Phase 4."""
    if not cfg.renderer_bin.exists():
        print(f"  ! renderer binary not found: {cfg.renderer_bin}", file=sys.stderr)
        return 2
    if not cfg.anchor_impulse.exists() or not cfg.anchor_noiseburst.exists():
        print(f"  ! anchor WAVs missing", file=sys.stderr)
        return 2
    if not cfg.search_space:
        print(f"  ! search_space is empty", file=sys.stderr)
        return 2
    if not cfg.optimizer_enabled:
        print(f"  ! optimizer.enabled is false in config", file=sys.stderr)
        return 2
    if workers is not None:
        cfg.parallel_workers = workers

    print(f"== {cfg.name} — grid + {cfg.optimizer_algorithm} ==")
    print(f"   preset:   {cfg.target_preset!r}")
    print(f"   engine:   algo {cfg.engine_algorithm}")
    print(f"   anchor:   {cfg.anchor_impulse.name}")
    print()
    print("  measuring anchor…")
    anchor = metrics_mod.measure_anchor(cfg.anchor_impulse, cfg.anchor_noiseburst)

    warm = _load_grid_warm_start(cfg) if skip_grid else None
    if not skip_grid:
        print()
        print("  phase A: coarse grid")
        results = grid_mod.run(cfg, anchor, workers=cfg.parallel_workers)
        grid_mod.write_csv(results, cfg, cfg.run_dir / "grid.csv")
        grid_mod.write_summary(results, cfg, cfg.run_dir / "grid_summary.json")
        grid_mod.write_heatmaps(results, cfg, cfg.run_dir / "grid_heatmaps")
        warm = _load_grid_warm_start(cfg)

    print()
    print(f"  phase B: {cfg.optimizer_algorithm} refinement")
    result = optimize_mod.run(cfg, anchor, warm_start_axes=warm)

    print()
    print("== final report (best vs anchor) ==")
    print(metrics_mod.format_report(result["best"]["meas"], anchor, cfg.metrics))

    counters = render_mod.counters()
    print()
    print(f"renders: {counters['renders']}  cache_hits: {counters['cache_hits']}")
    return 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("config", type=str, help="Path to tuner YAML config")
    ap.add_argument("--measure-only", action="store_true",
                    help="Render once at locked params, report metrics vs anchor, exit.")
    ap.add_argument("--grid-only", action="store_true",
                    help="Coarse grid sweep → CSV + heatmap PNGs. No optimizer, no patch.")
    ap.add_argument("--optimize", action="store_true",
                    help="Run grid + optimizer (CMA-ES or DE). No auto-patch.")
    ap.add_argument("--skip-grid", action="store_true",
                    help="With --optimize: skip the grid phase and warm-start "
                         "the optimizer from the existing grid_summary.json (if any).")
    ap.add_argument("--workers", type=int, default=None,
                    help="Parallel render workers (defaults to optimizer.parallel_workers).")
    ap.add_argument("--clear-cache", action="store_true",
                    help="Wipe the per-job render cache before running.")
    args = ap.parse_args(argv)

    cfg = cfg_mod.load(args.config)
    cfg.run_dir.mkdir(parents=True, exist_ok=True)

    if args.clear_cache:
        n = render_mod.clear_cache(cfg.cache_dir)
        print(f"cleared {n} cache file(s) from {cfg.cache_dir}")

    if args.measure_only:
        return cmd_measure_only(cfg)
    if args.grid_only:
        return cmd_grid_only(cfg, args.workers)
    if args.optimize:
        return cmd_optimize(cfg, args.workers, args.skip_grid)

    print("  ! full pipeline (grid + optimizer + patch) not yet implemented "
          "(Phase 4 patch missing). Use --optimize for grid+optimizer.",
          file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
