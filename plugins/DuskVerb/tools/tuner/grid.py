"""Coarse cartesian grid sweep — Phase 2 of the universal tuner.

For each axis in `cfg.search_space`, generate values via np.arange(lo, hi+step/2, step).
Cartesian-product all axes, render+measure each cell with a thread pool (rendering
is subprocess.run, I/O-bound — threads keep the render.py module-level counters
coherent), score against the anchor, write `grid.csv`, emit pairwise heatmap PNGs.

For N-dimensional grids, each axis-pair heatmap min-marginalizes loss over the
remaining axes. That projection answers "what's the best achievable loss as
this 2D slice varies, regardless of the other settings?" — which is the
useful question for choosing CMA-ES warm-start coordinates.
"""
from __future__ import annotations

import csv
import itertools
import json
import sys
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from typing import Any

import numpy as np

from . import metrics as metrics_mod
from . import render as render_mod
from .config import AxisSpec, TuneConfig


def _axis_values(ax: AxisSpec) -> list[float]:
    """Inclusive linear sweep — np.arange with half-step pad to include hi."""
    if ax.scale == "log":
        if ax.lo <= 0:
            raise ValueError(f"log scale axis lo must be > 0, got {ax.lo}")
        n = max(2, int(round((np.log10(ax.hi) - np.log10(ax.lo)) / ax.step)) + 1)
        return np.logspace(np.log10(ax.lo), np.log10(ax.hi), n).tolist()
    vals = np.arange(ax.lo, ax.hi + ax.step * 0.5, ax.step).tolist()
    # Clamp the last value to hi if floating-point drift took us slightly past.
    if vals and vals[-1] > ax.hi + ax.step * 0.01:
        vals = vals[:-1]
    return vals


def _format_param(name: str, val: float) -> str:
    """Stable string repr for --param. %g strips trailing zeros."""
    return f"{val:g}"


def _cell_slug(prefix: str, idx: int) -> str:
    return f"{prefix}_g{idx:05d}"


def enumerate_cells(cfg: TuneConfig) -> tuple[list[str], list[tuple[float, ...]]]:
    """Returns (axis_names, list_of_value_tuples) — Cartesian product."""
    axis_names = list(cfg.search_space.keys())
    axis_vals = [_axis_values(cfg.search_space[n]) for n in axis_names]
    cells = list(itertools.product(*axis_vals))
    return axis_names, cells


def _score_cell(cell_vals: tuple[float, ...], axis_names: list[str],
                cfg: TuneConfig, anchor: dict, cell_idx: int) -> dict:
    """Render+measure+score one grid cell. Thread-safe (subprocess.run is)."""
    params = dict(cfg.locked_params)
    for name, val in zip(axis_names, cell_vals):
        params[name] = _format_param(name, val)
    meas = render_mod.measure_params(
        params=params,
        target_preset=cfg.target_preset,
        n_renders=cfg.n_renders,
        aggregate=cfg.aggregate,
        renderer_bin=cfg.renderer_bin,
        output_dir=cfg.run_dir / "renders",
        cache_dir=cfg.cache_dir,
        measure_fn=metrics_mod.measure_pair,
        slug_prefix=_cell_slug(cfg.name, cell_idx),
    )
    warnings = meas.pop("_warnings", None)
    loss, breakdown = metrics_mod.compute_loss(meas, anchor, cfg.metrics)
    return {
        "idx": cell_idx,
        "axes": dict(zip(axis_names, cell_vals)),
        "params": params,
        "meas": meas,
        "loss": loss,
        "breakdown": breakdown,
        "warnings": warnings,
    }


def run(cfg: TuneConfig, anchor: dict, workers: int | None = None,
        progress_every: int = 1) -> list[dict]:
    """Execute the grid. Returns list of cell-result dicts sorted by cell idx."""
    axis_names, cells = enumerate_cells(cfg)
    if not axis_names:
        raise ValueError("grid: search_space is empty — nothing to sweep")
    n_cells = len(cells)
    w = workers if workers is not None else cfg.parallel_workers
    print(f"  grid: {len(axis_names)} axes × {n_cells} cells "
          f"({' × '.join(str(len(_axis_values(cfg.search_space[a]))) for a in axis_names)}), "
          f"workers={w}")

    results: list[dict | None] = [None] * n_cells
    done = 0
    lock = threading.Lock()

    with ThreadPoolExecutor(max_workers=w) as ex:
        futs = {ex.submit(_score_cell, vals, axis_names, cfg, anchor, i): i
                for i, vals in enumerate(cells)}
        for fut in as_completed(futs):
            i = futs[fut]
            try:
                results[i] = fut.result()
            except Exception as e:
                print(f"  ! cell {i} failed: {e}", file=sys.stderr)
                results[i] = {"idx": i, "axes": dict(zip(axis_names, cells[i])),
                              "loss": float("inf"), "error": str(e)}
            with lock:
                done += 1
                if done % progress_every == 0 or done == n_cells:
                    best = min((r["loss"] for r in results if r is not None
                                and r.get("loss") is not None),
                               default=float("inf"))
                    print(f"    [{done:4d}/{n_cells}] best_loss={best:.3f}")
    return [r for r in results if r is not None]


def write_csv(results: list[dict], cfg: TuneConfig, out_path: Path) -> None:
    """Flatten results to CSV. Vector metrics expanded to indexed columns."""
    if not results:
        return
    axis_names = list(cfg.search_space.keys())
    metric_keys = list(cfg.metrics.keys())

    # Discover the vector-metric column count from the first non-error result.
    template = next((r for r in results if r.get("meas")), None)
    vector_cols: dict[str, int] = {}
    if template:
        for k in metric_keys:
            v = template["meas"].get(k)
            if isinstance(v, list):
                vector_cols[k] = len(v)

    fieldnames = ["idx", "loss"] + axis_names
    for k in metric_keys:
        if k in vector_cols:
            fieldnames += [f"{k}[{i}]" for i in range(vector_cols[k])]
        else:
            fieldnames.append(k)
    fieldnames += [f"d_{k}" for k in metric_keys] + ["warnings", "error"]

    with open(out_path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames, extrasaction="ignore")
        w.writeheader()
        for r in results:
            row: dict[str, Any] = {"idx": r["idx"], "loss": r.get("loss")}
            for axis, val in r.get("axes", {}).items():
                row[axis] = val
            meas = r.get("meas", {}) or {}
            for k in metric_keys:
                v = meas.get(k)
                if k in vector_cols:
                    vlist = v if isinstance(v, list) else [None] * vector_cols[k]
                    for i in range(vector_cols[k]):
                        row[f"{k}[{i}]"] = vlist[i] if i < len(vlist) else None
                else:
                    row[k] = v
            for k in metric_keys:
                row[f"d_{k}"] = (r.get("breakdown") or {}).get(k)
            warns = r.get("warnings")
            row["warnings"] = ";".join(warns) if warns else ""
            row["error"] = r.get("error", "")
            w.writerow(row)


def _project_min(results: list[dict], axis_a: str, axis_b: str,
                 vals_a: list[float], vals_b: list[float]) -> np.ndarray:
    """For each (a, b) cell, find min loss across all other axes. NaN for empty cells."""
    grid = np.full((len(vals_a), len(vals_b)), np.nan, dtype=float)
    idx_a = {round(v, 9): i for i, v in enumerate(vals_a)}
    idx_b = {round(v, 9): i for i, v in enumerate(vals_b)}
    for r in results:
        loss = r.get("loss")
        if loss is None or not np.isfinite(loss):
            continue
        ai = idx_a.get(round(r["axes"].get(axis_a, np.nan), 9))
        bi = idx_b.get(round(r["axes"].get(axis_b, np.nan), 9))
        if ai is None or bi is None:
            continue
        cur = grid[ai, bi]
        if np.isnan(cur) or loss < cur:
            grid[ai, bi] = loss
    return grid


def write_heatmaps(results: list[dict], cfg: TuneConfig, out_dir: Path) -> int:
    """Pairwise loss heatmaps, min-marginalized over other axes. Returns count written."""
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("  ! matplotlib not installed — skipping heatmap generation",
              file=sys.stderr)
        return 0

    axis_names = list(cfg.search_space.keys())
    out_dir.mkdir(parents=True, exist_ok=True)
    written = 0

    if len(axis_names) == 1:
        # 1D: line plot loss vs single axis.
        name = axis_names[0]
        vals = _axis_values(cfg.search_space[name])
        losses = [next((r["loss"] for r in results
                        if round(r["axes"].get(name, np.nan), 9) == round(v, 9)
                        and r.get("loss") is not None), np.nan)
                  for v in vals]
        fig, ax = plt.subplots(figsize=(6, 4))
        ax.plot(vals, losses, "o-")
        best_i = int(np.nanargmin(losses)) if any(np.isfinite(losses)) else None
        if best_i is not None:
            ax.axvline(vals[best_i], color="red", linestyle="--", alpha=0.5)
            ax.set_title(f"loss vs {name}  (best: {vals[best_i]:g}, "
                         f"loss={losses[best_i]:.3f})")
        ax.set_xlabel(name)
        ax.set_ylabel("composite loss")
        ax.grid(True, alpha=0.3)
        fig.tight_layout()
        fig.savefig(out_dir / f"loss_vs_{_safe(name)}.png", dpi=120)
        plt.close(fig)
        return 1

    for a, b in itertools.combinations(axis_names, 2):
        vals_a = _axis_values(cfg.search_space[a])
        vals_b = _axis_values(cfg.search_space[b])
        grid = _project_min(results, a, b, vals_a, vals_b)
        fig, ax = plt.subplots(figsize=(6, 5))
        im = ax.imshow(grid, origin="lower", aspect="auto",
                       extent=(vals_b[0], vals_b[-1], vals_a[0], vals_a[-1]),
                       cmap="viridis")
        fig.colorbar(im, ax=ax, label="min loss (across other axes)")
        ax.set_xlabel(b)
        ax.set_ylabel(a)
        if np.any(np.isfinite(grid)):
            ai, bi = np.unravel_index(np.nanargmin(grid), grid.shape)
            ax.plot(vals_b[bi], vals_a[ai], "r*", markersize=14,
                    markeredgecolor="white", markeredgewidth=1)
            ax.set_title(f"{a} × {b}  (best: {a}={vals_a[ai]:g}, "
                         f"{b}={vals_b[bi]:g}, loss={grid[ai, bi]:.3f})")
        else:
            ax.set_title(f"{a} × {b}  (no finite cells)")
        fig.tight_layout()
        fig.savefig(out_dir / f"{_safe(a)}_x_{_safe(b)}.png", dpi=120)
        plt.close(fig)
        written += 1
    return written


def _safe(s: str) -> str:
    return "".join(c if c.isalnum() else "_" for c in s).strip("_")


def write_summary(results: list[dict], cfg: TuneConfig, out_path: Path,
                  top_n: int = 5) -> None:
    """Top-N cells by composite loss, written as JSON for downstream consumers
    (optimizer warm-start, report.py)."""
    finite = [r for r in results if r.get("loss") is not None
              and np.isfinite(r["loss"])]
    finite.sort(key=lambda r: r["loss"])
    summary = {
        "name": cfg.name,
        "target_preset": cfg.target_preset,
        "engine_algorithm": cfg.engine_algorithm,
        "n_cells": len(results),
        "n_finite": len(finite),
        "best": finite[0] if finite else None,
        "top_n": finite[:top_n],
    }
    with open(out_path, "w") as f:
        json.dump(summary, f, indent=2, default=str)
