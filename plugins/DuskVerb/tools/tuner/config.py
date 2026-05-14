"""YAML tuning-job loader + schema validation.

Schema mirrors the YAML keys in the plan file. Loader resolves paths
relative to the config file, expands $HOME / ~, and validates every key
before any rendering starts — so a malformed config fails in <100 ms
instead of after an hour of grid sweeping.
"""
from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import yaml


_SCALES = {"linear", "log"}
_OPTIMIZERS = {"cma", "differential_evolution"}
_AGGS = {"median", "mean"}


@dataclass
class AxisSpec:
    lo: float
    hi: float
    step: float
    scale: str = "linear"


@dataclass
class MetricWeight:
    weight: float
    jnd: float


@dataclass
class TuneConfig:
    # job
    name: str
    target_preset: str
    engine_algorithm: int
    notes: str = ""
    # renderer
    renderer_bin: Path = Path("build/tests/duskverb_render/duskverb_render")
    cache_dir: Path = Path("/tmp/tune_reverb_cache")
    output_dir: Path = Path("/tmp/tune_reverb_runs")
    n_renders: int = 3
    aggregate: str = "median"
    # anchor
    anchor_impulse: Path = Path()
    anchor_noiseburst: Path = Path()
    # params
    locked_params: dict[str, str] = field(default_factory=dict)
    search_space: dict[str, AxisSpec] = field(default_factory=dict)
    # metrics
    metrics: dict[str, MetricWeight] = field(default_factory=dict)
    # optimizer
    optimizer_enabled: bool = True
    optimizer_algorithm: str = "cma"
    sigma0: float = 0.15
    max_evals: int = 120
    tolerance: float = 0.05
    parallel_workers: int = 4
    # patch
    patch_enabled: bool = True
    patch_dry_run_first: bool = True
    fields_to_patch: list[str] = field(default_factory=list)

    @property
    def run_dir(self) -> Path:
        return self.output_dir / self.name


def _require(d: dict, key: str, where: str) -> Any:
    if key not in d:
        raise ValueError(f"config: missing required key '{key}' in {where}")
    return d[key]


def _require_mapping(value: Any, where: str) -> dict:
    if not isinstance(value, dict):
        raise ValueError(
            f"config: '{where}' must be a mapping, got {type(value).__name__}"
        )
    return value


def _require_positive(value: float | int, where: str) -> None:
    if not value > 0:
        raise ValueError(f"config: '{where}' must be > 0, got {value!r}")


def _resolve(path_str: str, base: Path) -> Path:
    p = Path(os.path.expanduser(os.path.expandvars(path_str)))
    if not p.is_absolute():
        p = (base / p).resolve()
    return p


def load(yaml_path: str | Path) -> TuneConfig:
    yaml_path = Path(yaml_path).resolve()
    base = yaml_path.parent
    with open(yaml_path) as f:
        raw = yaml.safe_load(f)
    if not isinstance(raw, dict):
        raise ValueError(f"config: top-level must be a mapping, got {type(raw).__name__}")

    job = _require_mapping(_require(raw, "job", "<root>"), "job")
    renderer = _require_mapping(_require(raw, "renderer", "<root>"), "renderer")
    anchor = _require_mapping(_require(raw, "anchor", "<root>"), "anchor")
    search_space_raw = _require_mapping(
        _require(raw, "search_space", "<root>"), "search_space"
    )
    metrics_raw = _require_mapping(
        _require(raw, "metrics", "<root>"), "metrics"
    )

    averaging = _require_mapping(renderer.get("averaging", {}), "renderer.averaging")
    if (agg := averaging.get("aggregate", "median")) not in _AGGS:
        raise ValueError(f"config: renderer.averaging.aggregate must be one of {_AGGS}, got '{agg}'")

    search_space: dict[str, AxisSpec] = {}
    for axis_name, ax in search_space_raw.items():
        if not isinstance(ax, dict):
            raise ValueError(f"config: search_space.{axis_name} must be a mapping")
        scale = ax.get("scale", "linear")
        if scale not in _SCALES:
            raise ValueError(f"config: search_space.{axis_name}.scale must be one of {_SCALES}")
        search_space[axis_name] = AxisSpec(
            lo=float(_require(ax, "lo", f"search_space.{axis_name}")),
            hi=float(_require(ax, "hi", f"search_space.{axis_name}")),
            step=float(_require(ax, "step", f"search_space.{axis_name}")),
            scale=scale,
        )
        if search_space[axis_name].hi <= search_space[axis_name].lo:
            raise ValueError(f"config: search_space.{axis_name}: hi must be > lo")
        if search_space[axis_name].step <= 0:
            raise ValueError(f"config: search_space.{axis_name}: step must be > 0")

    metrics: dict[str, MetricWeight] = {}
    for mkey, mw in metrics_raw.items():
        if not isinstance(mw, dict):
            raise ValueError(f"config: metrics.{mkey} must be a mapping")
        metrics[mkey] = MetricWeight(
            weight=float(_require(mw, "weight", f"metrics.{mkey}")),
            jnd=float(_require(mw, "jnd", f"metrics.{mkey}")),
        )
        if metrics[mkey].jnd <= 0:
            raise ValueError(f"config: metrics.{mkey}.jnd must be > 0")

    locked_raw = raw.get("locked_params")
    if locked_raw is None:
        locked_raw = {}
    elif not isinstance(locked_raw, dict):
        raise ValueError(
            f"config: 'locked_params' must be a mapping, got {type(locked_raw).__name__}"
        )
    locked = {str(k): str(v) for k, v in locked_raw.items()}
    for axis in search_space:
        if axis in locked:
            raise ValueError(f"config: '{axis}' is both locked and in search_space")

    opt = _require_mapping(raw.get("optimizer", {}), "optimizer")
    if (oalg := opt.get("algorithm", "cma")) not in _OPTIMIZERS:
        raise ValueError(f"config: optimizer.algorithm must be one of {_OPTIMIZERS}, got '{oalg}'")

    patch = _require_mapping(raw.get("patch", {}), "patch")
    raw_fields = patch.get("fields_to_patch", [])
    if isinstance(raw_fields, str):
        fields_to_patch = [raw_fields]
    elif isinstance(raw_fields, (list, tuple)):
        fields_to_patch = list(raw_fields)
    else:
        raise ValueError(
            f"config: 'patch.fields_to_patch' must be a list, got {type(raw_fields).__name__}"
        )

    eng_algo = int(_require(job, "engine_algorithm", "job"))
    if not 0 <= eng_algo <= 7:
        raise ValueError(f"config: job.engine_algorithm must be 0..7, got {eng_algo}")

    n_renders = int(averaging.get("n_renders", 3))
    sigma0 = float(opt.get("sigma0", 0.15))
    max_evals = int(opt.get("max_evals", 120))
    tolerance = float(opt.get("tolerance", 0.05))
    parallel_workers = int(opt.get("parallel_workers", 4))
    _require_positive(n_renders, "renderer.averaging.n_renders")
    _require_positive(sigma0, "optimizer.sigma0")
    _require_positive(max_evals, "optimizer.max_evals")
    _require_positive(tolerance, "optimizer.tolerance")
    _require_positive(parallel_workers, "optimizer.parallel_workers")

    return TuneConfig(
        name=str(_require(job, "name", "job")),
        target_preset=str(_require(job, "target_preset", "job")),
        engine_algorithm=eng_algo,
        notes=str(job.get("notes", "")),
        renderer_bin=_resolve(_require(renderer, "bin", "renderer"), base),
        cache_dir=_resolve(renderer.get("cache_dir", "/tmp/tune_reverb_cache"), base),
        output_dir=_resolve(renderer.get("output_dir", "/tmp/tune_reverb_runs"), base),
        n_renders=n_renders,
        aggregate=agg,
        anchor_impulse=_resolve(_require(anchor, "impulse_wav", "anchor"), base),
        anchor_noiseburst=_resolve(_require(anchor, "noiseburst_wav", "anchor"), base),
        locked_params=locked,
        search_space=search_space,
        metrics=metrics,
        optimizer_enabled=bool(opt.get("enabled", True)),
        optimizer_algorithm=oalg,
        sigma0=sigma0,
        max_evals=max_evals,
        tolerance=tolerance,
        parallel_workers=parallel_workers,
        patch_enabled=bool(patch.get("enabled", True)),
        patch_dry_run_first=bool(patch.get("dry_run_first", True)),
        fields_to_patch=fields_to_patch,
    )
