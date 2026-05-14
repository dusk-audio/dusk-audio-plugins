"""Composite-loss adapter on top of analyze_ir + perceptual_diff.

`measure_pair(impulse_wav, noiseburst_wav)` computes every supported metric
in one pass (so cached results survive config changes). `compute_loss`
sums weighted |Δ|/JND only for metric keys the user enabled in YAML.

Adding a new metric: add an entry to the `_measure` function and (if needed)
a special-case in `compute_loss` for vector-typed metrics.
"""
from __future__ import annotations

import os
import sys
from pathlib import Path
from typing import Any

import numpy as np

# Shared libs live in the parent tools/ dir.
_HERE = os.path.dirname(os.path.abspath(__file__))
_TOOLS = os.path.dirname(_HERE)
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)

import analyze_ir as ai
import perceptual_diff as pd


# Octave anchors for c80_per_octave. Order matters — composite_loss zips
# `meas['c80_per_octave']` against `anchor['c80_per_octave']` positionally.
C80_OCTAVES_HZ = (250.0, 500.0, 1000.0, 2000.0)

# Octave anchors for centroid_drift_per_band. Captures how HF content
# evolves over the tail in each octave — the signed dB difference between
# late and early windows. Lex Concert Hall measured ~-47 dB (natural HF
# decay over tail); SixAPTank Smooth Concert Hall measured +39 dB
# (late-tail modal ringing emerging *above* the early content). Opposite
# sign = engine-character mismatch the prior metric set didn't catch.
CENTROID_DRIFT_OCTAVES_HZ = (500.0, 1000.0, 2000.0, 4000.0)

# Vector-typed metric keys — compute_loss / format_report treat these as
# per-band arrays. Add new vector metrics here and to measure_pair only.
_VECTOR_METRIC_KEYS = ("rt60_per_band", "c80_per_octave",
                       "centroid_drift_per_band")


def measure_pair(impulse_path: Path, noiseburst_path: Path) -> dict[str, Any]:
    """Compute every supported metric from one (impulse, noiseburst) pair."""
    sr_i, ir_raw = ai.load_ir(str(impulse_path))
    ir = ai.to_mono(ir_raw).astype(np.float64)
    sr_n, nb = pd.load_mono(str(noiseburst_path))
    peak = int(np.argmax(np.abs(ir)))
    ir_trim = ir[peak:]
    nb_tail = nb[int(0.1 * sr_n):] if len(nb) > int(0.1 * sr_n) else nb

    return {
        # Per-band vectors
        "rt60_per_band":   ai.per_band_rt60(ir, sr_i),     # 8 floats / None
        "c80_per_octave":  [pd.c80_per_octave(ir, sr_i, fc) for fc in C80_OCTAVES_HZ],
        # Centroid drift per octave: signed dB shift (late_window - early_window).
        # Lex Concert Hall: -40 to -55 dB across 500/1k/2k/4k (HF dies naturally).
        # SixAPTank Smooth Concert Hall: +30 to +50 dB (late-tail modal ringing
        # emerges above early signal — engine artifact). Wider JND (5-10 dB).
        "centroid_drift_per_band":
            [pd.centroid_drift_db(ir, sr_i, fc) for fc in CENTROID_DRIFT_OCTAVES_HZ],
        # Broadband / single-value energy decay metrics (impulse)
        "edt":             pd.edt(ir, sr_i),
        "edt_500":         pd.edt(ir, sr_i, 500),
        "c80":             pd.c80(ir, sr_i),
        "d50":             pd.d50(ir_trim, sr_i),
        "bass_ratio":      pd.bass_ratio(ir_trim, sr_i),
        "treble_ratio":    pd.treble_ratio(ir_trim, sr_i),
        # Steady-state / spectral metrics (noise burst)
        "box_ratio_db":    pd.box_ratio_db(nb, sr_n),
        "a_weighted_rms_db": pd.a_weighted_rms_db(nb, sr_n),
        "k_weighted_lufs": pd.k_weighted_lufs(nb, sr_n),
        "spectral_crest_db": pd.spectral_crest_db(nb_tail, sr_n),
        "time_domain_crest": pd.time_domain_crest(nb, sr_n),
        # Stereo metric needs the file path (perceptual_diff loads the stereo
        # version internally — load_mono above would collapse channels).
        "stereo_correlation": pd.stereo_correlation(str(noiseburst_path)),
    }


def measure_anchor(impulse_path: Path, noiseburst_path: Path) -> dict[str, Any]:
    """Same as measure_pair — exposed under a separate name so future
    anchor-specific preprocessing (level matching, etc.) has a hook."""
    return measure_pair(impulse_path, noiseburst_path)


def _safe_pair(m: Any, a: Any) -> tuple[float, float] | None:
    if m is None or a is None:
        return None
    try:
        mf, af = float(m), float(a)
    except (TypeError, ValueError):
        return None
    if np.isnan(mf) or np.isnan(af):
        return None
    return mf, af


def compute_loss(meas: dict, anchor: dict,
                 weights_cfg: dict) -> tuple[float, dict[str, float]]:
    """Weighted L1 (each term divided by its JND). Returns (total, breakdown).
    Missing metric values are skipped (do not contribute), so a partially-
    measured render still produces a finite loss instead of NaN."""
    total = 0.0
    breakdown: dict[str, float] = {}

    for key, w in weights_cfg.items():
        if key in _VECTOR_METRIC_KEYS:
            m_vec = meas.get(key) or []
            a_vec = anchor.get(key) or []
            if len(m_vec) != len(a_vec):
                print(
                    f"warn: {key} length mismatch — m_vec={len(m_vec)} "
                    f"a_vec={len(a_vec)}; zip will truncate to shorter",
                    file=sys.stderr,
                )
            pairs = [p for p in (_safe_pair(mv, av)
                                 for mv, av in zip(m_vec, a_vec)) if p]
            if not pairs:
                continue
            d = sum(abs(mv - av) for mv, av in pairs) / (w.jnd * len(pairs))
        else:
            pair = _safe_pair(meas.get(key), anchor.get(key))
            if pair is None:
                continue
            d = abs(pair[0] - pair[1]) / w.jnd
        breakdown[key] = d
        total += w.weight * d
    return total, breakdown


def format_report(meas: dict, anchor: dict,
                  weights_cfg: dict) -> str:
    """Plain-text before/after table for stdout / report.md.
    Columns: metric, anchor, measured, |Δ|, |Δ|/JND, status."""
    loss, breakdown = compute_loss(meas, anchor, weights_cfg)
    lines = [f"{'metric':<22s} {'anchor':>10s} {'measured':>10s} "
             f"{'|Δ|':>8s} {'|Δ|/JND':>8s} {'w·d':>8s}  status"]
    lines.append("-" * 80)
    for key, w in weights_cfg.items():
        if key in _VECTOR_METRIC_KEYS:
            m_vec = meas.get(key) or []
            a_vec = anchor.get(key) or []
            for i, (mv, av) in enumerate(zip(m_vec, a_vec)):
                pair = _safe_pair(mv, av)
                if pair is None:
                    continue
                delta = abs(pair[0] - pair[1])
                norm = delta / w.jnd
                status = "ok" if norm <= 1.0 else "OFF"
                lines.append(
                    f"  {key}[{i}]{'':<10s} {pair[1]:>10.3f} {pair[0]:>10.3f} "
                    f"{delta:>8.3f} {norm:>8.2f} {'':>8s}  {status}")
            agg = breakdown.get(key, 0.0)
            lines.append(f"{key:<22s} {'(mean)':>10s} {'':>10s} {'':>8s} "
                         f"{agg:>8.2f} {w.weight * agg:>8.2f}  "
                         f"{'ok' if agg <= 1.0 else 'OFF'}")
        else:
            pair = _safe_pair(meas.get(key), anchor.get(key))
            if pair is None:
                lines.append(f"{key:<22s} {'(missing)':>10s}")
                continue
            delta = abs(pair[0] - pair[1])
            norm = delta / w.jnd
            status = "ok" if norm <= 1.0 else "OFF"
            lines.append(
                f"{key:<22s} {pair[1]:>10.3f} {pair[0]:>10.3f} "
                f"{delta:>8.3f} {norm:>8.2f} {w.weight * norm:>8.2f}  {status}")
    lines.append("-" * 80)
    lines.append(f"{'total loss':<22s} {'':>10s} {'':>10s} {'':>8s} {'':>8s} "
                 f"{loss:>8.2f}")
    return "\n".join(lines)
