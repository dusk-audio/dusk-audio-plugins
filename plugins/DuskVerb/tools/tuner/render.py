"""duskverb_render wrapper with N-render aggregation and sha256 disk cache.

`render.cpp` has no `--seed` flag, so non-determinism from LFO phase / random-
walk modulation must be averaged across multiple renders rather than seeded.
The aggregated measurement dict is cached on disk so grid → optimizer overlap
costs zero re-renders.

Public entrypoints:
    render_one(params, slug, target_preset, bin, output_dir) -> (imp, nb)
    measure_params(params, target_preset, n, agg, bin, output_dir,
                   cache_dir, measure_fn) -> dict
"""
from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Callable

import numpy as np


# Tracks every render so the caller can produce a budget summary at the end.
_render_count = 0
_cache_hits = 0


def reset_counters() -> None:
    global _render_count, _cache_hits
    _render_count = 0
    _cache_hits = 0


def counters() -> dict[str, int]:
    return {"renders": _render_count, "cache_hits": _cache_hits}


def param_hash(params: dict[str, str], target_preset: str,
               extra: dict | None = None) -> str:
    """Stable sha256 over (preset + sorted params + extra). Used as cache key."""
    h = hashlib.sha256()
    h.update(target_preset.encode("utf-8"))
    h.update(b"\x00")
    for k in sorted(params):
        h.update(k.encode("utf-8"))
        h.update(b"=")
        h.update(str(params[k]).encode("utf-8"))
        h.update(b"\x00")
    if extra:
        for k in sorted(extra):
            h.update(k.encode("utf-8"))
            h.update(b"=")
            h.update(str(extra[k]).encode("utf-8"))
            h.update(b"\x00")
    return h.hexdigest()[:16]


def render_one(params: dict[str, str], slug: str, target_preset: str,
               renderer_bin: Path, output_dir: Path,
               timeout_s: float = 60.0) -> tuple[Path, Path]:
    """Invoke duskverb_render once. Returns (impulse_path, noiseburst_path)."""
    global _render_count
    output_dir.mkdir(parents=True, exist_ok=True)
    cmd = [str(renderer_bin),
           "--preset", target_preset,
           "--slug", slug,
           "--output-dir", str(output_dir)]
    for k, v in params.items():
        cmd += ["--param", f"{k}={v}"]
    proc = subprocess.run(cmd, capture_output=True, timeout=timeout_s)
    _render_count += 1
    if proc.returncode != 0:
        raise RuntimeError(
            f"duskverb_render failed (rc={proc.returncode}) for slug={slug!r}\n"
            f"stderr: {proc.stderr.decode('utf-8', errors='replace')[:2000]}")
    # Surface critical stderr warnings even on rc=0. `parameter not found`
    # is the canonical silent-failure mode (YAML axis name doesn't match
    # the plugin's display name) — without this, 308 grid cells could
    # silently run with the same DSP state and the bug only surfaces
    # later when top-N rows tie on identical loss.
    if proc.stderr:
        stderr_text = proc.stderr.decode("utf-8", errors="replace")
        for line in stderr_text.splitlines():
            if "parameter not found" in line or "! --param:" in line:
                print(f"  ! {slug}: {line.strip()}", file=sys.stderr)
    imp = output_dir / f"{slug}_impulse.wav"
    nb  = output_dir / f"{slug}_noiseburst.wav"
    if not imp.exists() or not nb.exists():
        raise RuntimeError(f"renderer succeeded but expected WAVs missing: "
                           f"{imp} / {nb}\nstdout: "
                           f"{proc.stdout.decode('utf-8', errors='replace')[:1000]}")
    return imp, nb


def _aggregate(per_run: list[dict], agg: str) -> dict:
    """Median or mean across per-run measurement dicts. Vectors collapse
    element-wise; scalars collapse to a single float. Adds std for any
    metric whose CoV (std/|median|) > 0.10 — that's the LFO seeding canary
    from duskverb_render_nondeterminism.md.
    """
    if not per_run:
        return {}
    reducer = np.median if agg == "median" else np.mean
    keys = list(per_run[0].keys())
    out: dict = {}
    warnings: list[str] = []
    for k in keys:
        vals = [r[k] for r in per_run if k in r]
        # Filter Nones (e.g. per_band_rt60 returns None for unreachable bands).
        first = vals[0]
        if isinstance(first, (list, tuple)):
            arr = np.asarray([[(np.nan if x is None else float(x)) for x in v]
                              for v in vals], dtype=float)
            collapsed = np.nanmedian(arr, axis=0) if agg == "median" \
                        else np.nanmean(arr, axis=0)
            out[k] = collapsed.tolist()
            if len(vals) >= 2 and arr.shape[1] > 0:
                stds = np.nanstd(arr, axis=0)
                with np.errstate(divide="ignore", invalid="ignore"):
                    cov = stds / np.maximum(np.abs(collapsed), 1e-9)
                if np.any(cov > 0.10):
                    bad = [i for i, c in enumerate(cov) if c > 0.10]
                    warnings.append(f"{k}: high variance bands={bad} cov={cov.tolist()}")
        elif isinstance(first, (int, float)):
            arr = np.asarray([float(v) for v in vals if v is not None], dtype=float)
            if arr.size == 0:
                out[k] = None
                continue
            out[k] = float(reducer(arr))
            if arr.size >= 2 and abs(out[k]) > 1e-9:
                cov = float(arr.std() / max(abs(out[k]), 1e-9))
                if cov > 0.10:
                    warnings.append(f"{k}: high variance cov={cov:.3f} "
                                    f"raw={arr.tolist()}")
        else:
            # Pass through (strings, etc.) — take first.
            out[k] = first
    if warnings:
        out["_warnings"] = warnings
    return out


def measure_params(params: dict[str, str], target_preset: str,
                   n_renders: int, aggregate: str,
                   renderer_bin: Path, output_dir: Path, cache_dir: Path,
                   measure_fn: Callable[[Path, Path], dict],
                   slug_prefix: str = "tune") -> dict:
    """Render n times → measure each → aggregate → cache to disk.

    cache key: sha256(params + preset + n + agg + measure_fn.__name__)
    cache miss: renders, measures, aggregates, writes JSON.
    cache hit: returns cached aggregated dict immediately.
    """
    global _cache_hits
    cache_dir.mkdir(parents=True, exist_ok=True)
    key = param_hash(params, target_preset,
                     extra={"n": n_renders, "agg": aggregate,
                            "fn": getattr(measure_fn, "__name__", "anon")})
    cache_file = cache_dir / f"{key}.json"
    if cache_file.exists():
        try:
            with open(cache_file) as f:
                data = json.load(f)
            _cache_hits += 1
            return data["aggregated"]
        except (OSError, json.JSONDecodeError, KeyError) as e:
            print(f"  ! cache file {cache_file} unreadable ({e}); re-rendering",
                  file=sys.stderr)

    per_run: list[dict] = []
    for r in range(n_renders):
        slug = f"{slug_prefix}_{key}_r{r}"
        imp, nb = render_one(params, slug, target_preset,
                             renderer_bin, output_dir)
        try:
            per_run.append(measure_fn(imp, nb))
        finally:
            # WAVs are big; remove after measurement unless caller wants
            # them. Keep r0 for the final-render audit.
            if r > 0:
                for p in (imp, nb):
                    try: p.unlink()
                    except OSError: pass
    agg_dict = _aggregate(per_run, aggregate)
    with open(cache_file, "w") as f:
        json.dump({"params": params, "preset": target_preset,
                   "n_renders": n_renders, "aggregate": aggregate,
                   "per_run": per_run, "aggregated": agg_dict}, f, indent=2)
    return agg_dict


def clear_cache(cache_dir: Path) -> int:
    """Wipe cache directory. Returns count of files removed."""
    if not cache_dir.exists():
        return 0
    count = 0
    for f in cache_dir.glob("*.json"):
        f.unlink()
        count += 1
    return count
