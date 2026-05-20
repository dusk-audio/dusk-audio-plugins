"""Target anchor JSON loader for DuskVerb calibration graders.

Decouples the optimizer / iter scripts from live VST2 reference renders
so calibration runs anywhere (Mac/CI/Linux without yabridge). Static
snapshots live at `plugins/DuskVerb/tools/targets/lex_*.json`; see
`export_targets.py` for the one-shot capture tool that produces them.

Schema is `duskverb-target-v1`. One file per anchor. Each metric carries
its value + unit + JND threshold + (for vector metrics) bin frequency
metadata so downstream code can self-verify it's indexing the right
octave.

The `Anchor.as_measure_pair_dict()` method returns the flat-dict shape
that `metrics.measure_pair()` returns, so existing scoring code
(`compute_loss`, `count_pass`) consumes the snapshot bit-identically.
"""
from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


@dataclass
class Anchor:
    """Loaded anchor snapshot. Carries all 19 graded metrics + provenance.

    Use `as_measure_pair_dict()` to drop into any scoring path that
    currently consumes `metrics.measure_pair()` output.
    """
    name: str
    schema_version: int
    scalars: dict[str, float]
    vectors: dict[str, list[Any]]   # list elements may be None (rt60) or NaN (peaks)
    jnd:     dict[str, float]
    units:   dict[str, str]
    bin_freqs_hz: dict[str, list[float]]   # only populated for vector keys
    meta:    dict[str, Any] = field (default_factory=dict)

    def as_measure_pair_dict (self) -> dict[str, Any]:
        """Return scalars+vectors merged as a flat dict, matching the
        shape `metrics.measure_pair()` returns. Lets existing scoring
        pipelines (`compute_loss`, `count_pass`) consume the snapshot
        without any awareness of the JSON wrapper."""
        out: dict[str, Any] = {}
        out.update (self.scalars)
        out.update (self.vectors)
        return out


def load_target (path: Path | str) -> Anchor:
    """Load a `lex_*.json` snapshot. Raises if schema_version unknown."""
    p = Path (path)
    if not p.exists():
        raise FileNotFoundError (f"target file not found: {p}")
    j = json.loads (p.read_text())
    sv = j.get ('schema_version')
    if sv != 1:
        raise ValueError (f"unsupported target schema_version {sv} in {p} (expected 1)")

    metrics = j.get ('metrics') or {}
    scalars: dict[str, float] = {}
    vectors: dict[str, list[Any]] = {}
    jnd_map: dict[str, float] = {}
    units:   dict[str, str] = {}
    bins:    dict[str, list[float]] = {}

    for key, entry in metrics.items():
        val = entry.get ('value')
        jnd_map[key] = float (entry.get ('jnd', 0.0))
        units[key]   = str (entry.get ('unit', ''))
        if isinstance (val, list):
            # Preserve None (rt60) + NaN (peak_locations) — count_pass
            # already handles those as fail cases. json round-trip turns
            # JSON null → Python None, "NaN" string → Python NaN below.
            cleaned: list[Any] = []
            for v in val:
                if v is None:
                    cleaned.append (None)
                elif isinstance (v, str) and v.lower() in ('nan', 'inf', '-inf'):
                    cleaned.append (float (v))
                else:
                    cleaned.append (float (v))
            vectors[key] = cleaned
            if 'bin_freqs_hz' in entry:
                bins[key] = [float (f) for f in entry['bin_freqs_hz']]
        else:
            scalars[key] = float (val)

    meta = {
        'preset_name': j.get ('preset_name'),
        'anchor_source': j.get ('anchor_source'),
        'fxp_path': j.get ('fxp_path'),
        'vst2_plugin': j.get ('vst2_plugin'),
        'render_settings': j.get ('render_settings'),
        'captured_iso': j.get ('captured_iso'),
    }

    return Anchor (
        name = j.get ('preset_name', p.stem),
        schema_version = sv,
        scalars = scalars,
        vectors = vectors,
        jnd = jnd_map,
        units = units,
        bin_freqs_hz = bins,
        meta = meta,
    )
