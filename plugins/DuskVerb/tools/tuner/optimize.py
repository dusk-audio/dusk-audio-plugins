"""CMA-ES + scipy.differential_evolution refinement — Phase 3 of the tuner.

Both backends operate in a normalized [0, 1]^N search space; `_denormalize`
maps back to display-name param values that duskverb_render expects. CMA-ES
is the primary because of its noise-tolerance on this loss surface; DE is the
zero-extra-dependency fallback.

Warm-start: when grid results are provided, both backends initialize from the
grid's best cell (CMA via `x0`, DE via a seeded `init` population).
"""
from __future__ import annotations

import json
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import Any, Callable

import numpy as np

from . import metrics as metrics_mod
from . import render as render_mod
from .config import AxisSpec, TuneConfig


# ── normalize ────────────────────────────────────────────────────────────────

def _normalize(value: float, ax: AxisSpec) -> float:
    if ax.scale == "log":
        return float(np.log10(value / ax.lo) / np.log10(ax.hi / ax.lo))
    return float((value - ax.lo) / (ax.hi - ax.lo))


def _denormalize(x: float, ax: AxisSpec) -> float:
    x = max(0.0, min(1.0, x))
    if ax.scale == "log":
        return float(ax.lo * (ax.hi / ax.lo) ** x)
    return float(ax.lo + x * (ax.hi - ax.lo))


def _build_params(x_vec: np.ndarray, axis_names: list[str],
                  cfg: TuneConfig) -> dict[str, str]:
    params = dict(cfg.locked_params)
    for name, x in zip(axis_names, x_vec):
        val = _denormalize(float(x), cfg.search_space[name])
        params[name] = f"{val:g}"
    return params


# ── trajectory logger ───────────────────────────────────────────────────────

class TrajectoryLog:
    """Append-only log of every evaluation. Used by report.py + smoke tests."""
    def __init__(self, path: Path):
        self.path = path
        self.entries: list[dict] = []
        self._lock = threading.Lock()
        self._t0 = time.time()

    def add(self, gen: int, x: np.ndarray, axes: dict[str, float],
            params: dict[str, str], loss: float, meas: dict,
            breakdown: dict) -> None:
        with self._lock:
            self.entries.append({
                "t": time.time() - self._t0,
                "gen": gen,
                "eval": len(self.entries),
                "x": x.tolist(),
                "axes": axes,
                "params": params,
                "loss": loss,
                "meas": meas,
                "breakdown": breakdown,
            })

    def best(self) -> dict | None:
        finite = [e for e in self.entries if np.isfinite(e["loss"])]
        return min(finite, key=lambda e: e["loss"]) if finite else None

    def flush(self) -> None:
        with open(self.path, "w") as f:
            json.dump({"entries": self.entries, "best": self.best()},
                      f, indent=2, default=str)


# ── eval fn factory ─────────────────────────────────────────────────────────

def _make_eval_fn(cfg: TuneConfig, anchor: dict, axis_names: list[str],
                  traj: TrajectoryLog, gen_counter: list[int],
                  measure_override: Callable | None = None) -> Callable:
    """Returns eval(x_vec)->loss. measure_override lets tests inject a fake
    measure_params (skipping the renderer entirely)."""
    measure_fn = measure_override or render_mod.measure_params

    def eval_one(x_vec: np.ndarray) -> float:
        params = _build_params(x_vec, axis_names, cfg)
        axes = {n: _denormalize(float(x), cfg.search_space[n])
                for n, x in zip(axis_names, x_vec)}
        try:
            meas = measure_fn(
                params=params,
                target_preset=cfg.target_preset,
                n_renders=cfg.n_renders,
                aggregate=cfg.aggregate,
                renderer_bin=cfg.renderer_bin,
                output_dir=cfg.run_dir / "renders",
                cache_dir=cfg.cache_dir,
                measure_fn=metrics_mod.measure_pair,
                slug_prefix=f"{cfg.name}_opt",
            )
            meas.pop("_warnings", None)
            loss, breakdown, _penalties = metrics_mod.compute_loss(meas, anchor, cfg.metrics)
        except Exception as e:
            print(f"  ! eval failed: {e}", file=sys.stderr)
            loss, breakdown, meas = float("inf"), {}, {}
        traj.add(gen_counter[0], x_vec, axes, params, loss, meas, breakdown)
        return loss

    return eval_one


# ── CMA-ES backend ──────────────────────────────────────────────────────────

def _run_cma(cfg: TuneConfig, anchor: dict, axis_names: list[str],
             x0: np.ndarray, traj: TrajectoryLog,
             measure_override: Callable | None = None) -> dict:
    try:
        import cma
    except ImportError:
        raise RuntimeError(
            "optimizer.algorithm=cma but the `cma` package is not installed. "
            "Install with `pip install cma` or switch to algorithm: "
            "differential_evolution.")

    n = len(axis_names)
    popsize = max(4, min(cfg.parallel_workers * 2, 4 + int(3 * np.log(n))))
    opts = {
        "bounds": [[0.0] * n, [1.0] * n],
        "maxfevals": cfg.max_evals,
        "popsize": popsize,
        "tolfun": cfg.tolerance,
        "tolx": 0.005,
        "verbose": -9,
        "seed": 12345,
    }
    es = cma.CMAEvolutionStrategy(x0.tolist(), cfg.sigma0, opts)
    gen_counter = [0]
    eval_one = _make_eval_fn(cfg, anchor, axis_names, traj, gen_counter,
                             measure_override)

    while not es.stop():
        gen_counter[0] += 1
        solutions = es.ask()
        if cfg.parallel_workers > 1:
            with ThreadPoolExecutor(max_workers=cfg.parallel_workers) as ex:
                losses = list(ex.map(eval_one,
                                     [np.asarray(s) for s in solutions]))
        else:
            losses = [eval_one(np.asarray(s)) for s in solutions]
        es.tell(solutions, losses)
        best_so_far = traj.best()
        best_str = f"{best_so_far['loss']:.3f}" if best_so_far else "NA"
        print(f"    [cma gen {gen_counter[0]:3d} eval {len(traj.entries):4d}/"
              f"{cfg.max_evals}] sigma={es.sigma:.4f}  "
              f"gen_min={min(losses):.3f}  best={best_str}")
    return {"backend": "cma", "stop_reason": str(es.stop()),
            "generations": gen_counter[0]}


# ── scipy differential_evolution backend ────────────────────────────────────

def _run_de(cfg: TuneConfig, anchor: dict, axis_names: list[str],
            x0: np.ndarray, traj: TrajectoryLog,
            measure_override: Callable | None = None) -> dict:
    from scipy.optimize import differential_evolution

    n = len(axis_names)
    bounds = [(0.0, 1.0)] * n
    popsize = max(5, cfg.parallel_workers * 2)
    init_pop = _de_init_population(x0, popsize, n)
    gen_counter = [0]
    eval_one = _make_eval_fn(cfg, anchor, axis_names, traj, gen_counter,
                             measure_override)

    maxiter = max(1, cfg.max_evals // (popsize * n))

    def cb(xk, convergence):
        gen_counter[0] += 1
        best = traj.best()
        if best:
            print(f"    [de iter {gen_counter[0]:3d} eval {len(traj.entries):4d}] "
                  f"convergence={convergence:.4f}  best={best['loss']:.3f}")

    result = differential_evolution(
        lambda x: eval_one(np.asarray(x)), bounds,
        maxiter=maxiter, popsize=popsize, tol=cfg.tolerance,
        init=init_pop, polish=True, workers=1, callback=cb, seed=12345,
        updating="deferred",
    )
    return {"backend": "differential_evolution",
            "stop_reason": result.message,
            "generations": gen_counter[0],
            "scipy_result_x": result.x.tolist(),
            "scipy_result_fun": float(result.fun)}


def _de_init_population(x0: np.ndarray, popsize: int, n: int) -> np.ndarray:
    """Seed first member with x0; remaining via Sobol-like quasi-random."""
    from scipy.stats import qmc
    sobol = qmc.Sobol(d=n, scramble=True, seed=12345)
    pop = sobol.random(popsize)
    pop[0] = np.clip(x0, 0.0, 1.0)
    return pop


# ── entrypoint ──────────────────────────────────────────────────────────────

def run(cfg: TuneConfig, anchor: dict,
        warm_start_axes: dict[str, float] | None = None,
        measure_override: Callable | None = None) -> dict:
    """Top-level optimizer. Returns dict with best params + meas + meta."""
    if not cfg.search_space:
        raise ValueError("optimize: search_space is empty")
    axis_names = list(cfg.search_space.keys())

    # Build x0 in normalized space.
    if warm_start_axes:
        x0 = np.array([_normalize(float(warm_start_axes[n]),
                                  cfg.search_space[n])
                       for n in axis_names])
        print(f"  optimize: warm-start from grid best: "
              + ", ".join(f"{n}={warm_start_axes[n]:g}" for n in axis_names))
    else:
        x0 = np.array([0.5] * len(axis_names))
        print(f"  optimize: cold start (midpoint of all axes)")

    traj_path = cfg.run_dir / "optimizer_traj.json"
    traj = TrajectoryLog(traj_path)

    if cfg.optimizer_algorithm == "cma":
        meta = _run_cma(cfg, anchor, axis_names, x0, traj, measure_override)
    elif cfg.optimizer_algorithm == "differential_evolution":
        meta = _run_de(cfg, anchor, axis_names, x0, traj, measure_override)
    else:
        raise ValueError(f"unknown optimizer.algorithm: {cfg.optimizer_algorithm}")

    traj.flush()
    best = traj.best()
    if best is None:
        raise RuntimeError("optimize: no finite-loss evaluations recorded")

    result = {
        "name": cfg.name,
        "target_preset": cfg.target_preset,
        "engine_algorithm": cfg.engine_algorithm,
        "backend": meta["backend"],
        "meta": meta,
        "n_evals": len(traj.entries),
        "best": best,
        "warm_start": warm_start_axes,
    }
    result_path = cfg.run_dir / "optimize_result.json"
    with open(result_path, "w") as f:
        json.dump(result, f, indent=2, default=str)

    print()
    print(f"  optimize: backend={meta['backend']}  evals={len(traj.entries)}  "
          f"best_loss={best['loss']:.4f}")
    print(f"  best params: " + ", ".join(f"{k}={v:g}" for k, v in best["axes"].items()))
    print(f"  wrote {traj_path}")
    print(f"  wrote {result_path}")
    return result
