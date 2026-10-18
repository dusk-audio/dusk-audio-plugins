#!/usr/bin/env python3
"""Fit and generate the R10 structural Opto harmonic calibration.

Structure (MultiCompModes.hpp, processOpto), all on the audio branch; the
detector and the gain computer are untouched:

* cell ripple: the audio sees ``g + K * (g - LP(g))``, where ``g`` is the
  cell gain and ``LP`` two one-pole low-passes at half the tone frequency.  The frequency law
  is the cell's own.  K = RIPPLE_SCALE is one physical constant: MC-2's fast
  loop is ~1.5x slower than the reference's (AM test, tau_eq), so its ripple,
  which goes as 1/(w tau) above the corner, is ~1.5x too shallow;
* cell distortion, injected at the cell output (before Gain and the static
  output stage, as in the hardware) and scaled with the cell's fundamental:
  - a memoryless term per harmonic behind a fixed first-order pre-shelf
    (harmonic k scales as S(f)^(k-1));
  - light-ripple lines n = 1..8: a gain modulation at n*f through the cell's
    fast response L(n f) (a fixed single pole, consistent with the AM test's
    falling tau_eq), which lands on harmonics n - 1 and n + 1.

Every coefficient is indexed by input level, PR and mode; frequency enters
only through S, L and the cell ripple.  Gain enters only through the static
output stage, whose response to the injected harmonics is its exact
linearisation around the measured drive.

Fit data: actual-AU UAD renders of the R10 fit grid, and MC-2 renders of the
same grid (base: every term off; probe: ripple scale PROBE, other terms off).
The fitter never opens a held-out file.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path

import numpy as np

import fit_opto_output_stage as stage

REPO = Path(__file__).resolve().parents[3]
EVIDENCE = REPO / "build-multi-comp-1176/opto-production-harmonics-20260928"
OUTPUT = REPO / "plugins/multi-comp/core/MultiCompOptoStructuralHarmonics.hpp"
FREQUENCIES = (30, 50, 100, 200, 500, 1000, 2000, 5000, 8000)
GAINS = (0.15, 0.25, 0.35, 0.50, 0.65, 0.80)
LEVELS = tuple(range(-40, 1, 4))
PRS = (0.0, 0.35, 0.70, 1.0)
HARMONICS = tuple(range(2, 8))
LINES = tuple(range(1, 9))
RIPPLE_SPLIT_RATIO = 0.5
MAKEUP_DB = {0.15: -8.777571, 0.25: 0.999066, 0.35: 7.824299, 0.45: 12.505607,
             0.50: 14.5, 0.65: 21.813084, 0.80: 33.268223}
ELIGIBLE_FLOOR_DBFS = -100.0
PER_CONDITION = 12 + 2 * len(LINES)          # flat, lines
RIPPLE_SCALE = 0.5
CONDITIONS = 2 * 3 * len(LEVELS)             # mode x active PR x level


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(tag: str, plugin: str) -> dict:
    path = EVIDENCE / f"r10-fit-{tag}-{plugin}.json"
    if "holdout" in path.name:
        raise SystemExit("the fitter never reads a held-out grid")
    return {(r["frequency_hz"], r["input_dbfs"], r["peak_reduction_normalised"],
             r["gain_normalised"], r["limit"], r["harmonic"]): r
            for r in json.loads(path.read_text())}


def relative_complex(row: dict) -> complex:
    """Harmonic in its own unit's fundamental frame (fundamental = sin)."""
    angle = math.radians(row["relative_phase_degrees"] - 90.0 * row["harmonic"])
    return 10.0 ** (row["magnitude_dbfs"] / 20.0) * complex(math.cos(angle),
                                                              math.sin(angle))


def as_ab(c: complex) -> np.ndarray:
    """(a, b) of a sin + b cos from its window coefficient c = b - j a."""
    return np.asarray([-c.imag, c.real])


def shelf(f: float, f1: float, f2: float) -> complex:
    return (1.0 + 1j * f / f1) / (1.0 + 1j * f / f2)


def fast(f: float, fc: float) -> complex:
    return 1.0 / (1.0 + 1j * f / fc)


# ------------------------------------------------ output-stage linearisation
_curve = None
_jacobians: dict = {}


def curve(x: np.ndarray) -> np.ndarray:
    global _curve
    if _curve is None:
        text = stage.OUTPUT.read_text()
        body = text.split("kOptoOutputStageCurve{{", 1)[1].split("}};", 1)[0]
        _curve = np.asarray([float(v.strip().rstrip("f")) for v in body.split(",")
                             if v.strip()])
    idx, w = stage.catmull_rom_weights(x)
    return np.sum(_curve[idx] * w, axis=1)


def jacobian(amplitude: float) -> np.ndarray:
    """Real 12x12 map: injected (sin, cos) of H2..H7 -> output (a, b) of H2..H7."""
    key = round(20.0 * math.log10(amplitude), 3)
    if key not in _jacobians:
        n = 4096
        theta = 2.0 * np.pi * np.arange(n) / n
        x = amplitude * np.sin(theta)
        step = max(1e-6, 1e-4 * amplitude)
        slope = (curve(x + step) - curve(x - step)) / (2.0 * step)
        J = np.zeros((12, 12))
        for k in HARMONICS:
            for axis, basis in ((0, np.sin(k * theta)), (1, np.cos(k * theta))):
                y = slope * basis
                for m in HARMONICS:
                    J[2 * (m - 2):2 * (m - 2) + 2, 2 * (k - 2) + axis] = as_ab(
                        2.0 / n * np.dot(y, np.exp(-1j * m * theta)))
        _jacobians[key] = J
    return _jacobians[key]


# ------------------------------------------------------------------ data
def settings() -> list[tuple[float, bool]]:
    return [(pr, limit) for limit in (False, True) for pr in PRS[1:]]


def condition_index(level: int, pr: float, limit: bool) -> int:
    return ((1 if limit else 0) * 3 + PRS.index(pr) - 1) * len(LEVELS) + LEVELS.index(level)


def cells(uad: dict, base: dict, probe: dict, probe_kappa: float,
          frequencies=FREQUENCIES, gains=GAINS) -> list[dict]:
    """One record per compressed (frequency, level, PR, mode, Gain) cell."""
    out = []
    for frequency in frequencies:
        for level in LEVELS:
            for pr, limit in settings():
                low = base[(frequency, level, pr, 0.15, limit, 2)]["fundamental_dbfs"]
                cell_db = low - MAKEUP_DB[0.15]          # fundamental at the cell
                for gain in gains:
                    record = {"frequency": frequency, "level": level, "pr": pr,
                              "limit": limit, "gain": gain,
                              "cell_amplitude": 10.0 ** (cell_db / 20.0),
                              "drive": 10.0 ** ((cell_db + MAKEUP_DB[gain]) / 20.0),
                              "makeup": 10.0 ** (MAKEUP_DB[gain] / 20.0),
                              "harmonics": []}
                    for h in HARMONICS:
                        key = (frequency, level, pr, gain, limit, h)
                        u, b, q = uad[key], base[key], probe[key]
                        if not u["representable"]:
                            continue
                        bc, qc = relative_complex(b), relative_complex(q)
                        ripple = (qc - bc) / probe_kappa
                        record["harmonics"].append({
                            # The candidate's own ripple (base + K * D) is the
                            # starting point every other term corrects.
                            "h": h, "uad": relative_complex(u),
                            "base": bc + RIPPLE_SCALE * ripple,
                            "uad_dbfs": u["magnitude_dbfs"],
                            "eligible": u["magnitude_dbfs"] > ELIGIBLE_FLOOR_DBFS})
                    out.append(record)
    return out


def injection_basis(record: dict, g: dict) -> np.ndarray:
    """(12, PER_CONDITION): flat + line parameters -> injected (a, b)."""
    f = record["frequency"]
    M = np.zeros((12, PER_CONDITION))
    S = shelf(f, g["f1"], g["f2"])
    scale = record["cell_amplitude"]
    for h in HARMONICS:
        r = 2 * (h - 2)
        terms = [(2 * (h - 2), scale * S ** (h - 1))]
        terms += [(12 + 2 * (n - 1), scale * fast(n * f, g["fc"]))
                  for n in (h - 1, h + 1) if n in LINES]
        for col, weight in terms:
            for axis, unit in ((0, -1j), (1, 1.0)):   # parameter a (sin), b (cos)
                M[r:r + 2, col + axis] += as_ab(weight * unit)
    return M


def design(records: list[dict], g: dict):
    rows, targets, weights, meta = [], [], [], []
    for record in records:
        if not record["harmonics"]:
            continue
        inject = (jacobian(record["drive"]) * record["makeup"]) @ injection_basis(record, g)
        for x in record["harmonics"]:
            r = 2 * (x["h"] - 2)
            target = as_ab(x["uad"] - x["base"])
            w = 1.0 / max(abs(x["uad"]), 1e-5)
            for part in (0, 1):
                row = inject[r + part].copy()
                rows.append(row); targets.append(target[part]); weights.append(w)
            meta.append((record, x))
    return np.asarray(rows), np.asarray(targets), np.asarray(weights), meta


def fit_all(recs: list[dict], g: dict, smooth: float, iterations: int = 1) -> np.ndarray:
    """Joint weighted fit, Jacobi-scaled, smooth across level; IRLS emphasis."""
    groups: dict = {}
    for record in recs:
        groups.setdefault(condition_index(record["level"], record["pr"],
                                          record["limit"]), []).append(record)
    total = CONDITIONS * PER_CONDITION
    blocks = {c: design(rs, g) for c, rs in groups.items()}
    emphasis = {c: np.ones(len(b[1])) for c, b in blocks.items()}
    # Second differences across level of every parameter, in scaled units.
    diff = []
    for mode in range(2):
        for p in range(3):
            for li in range(1, len(LEVELS) - 1):
                for j in range(PER_CONDITION):
                    diff.append([((mode * 3 + p) * len(LEVELS) + li + o) * PER_CONDITION + j
                                 for o in (-1, 0, 1)])
    x = np.zeros(total)
    for _ in range(iterations):
        normal = np.zeros((total, total)); rhs = np.zeros(total)
        for c, (A, y, w, meta) in blocks.items():
            if A.size == 0:
                continue
            ww = w * emphasis[c]
            WA = A * ww[:, None]
            sl = slice(c * PER_CONDITION, (c + 1) * PER_CONDITION)
            normal[sl, sl] += WA.T @ WA
            rhs[sl] += WA.T @ (ww * y)
        diagonal = np.diag(normal).copy()
        d = np.where(diagonal > 1e-30, np.sqrt(np.maximum(diagonal, 1e-30)), 1.0)
        scaled = normal / np.outer(d, d)
        scaled[np.diag_indices(total)] += 1e-6 + (diagonal <= 1e-30)
        # Smooth the physical coefficients (per unit cell fundamental, so
        # comparable across level); strength relative to the median data
        # information, expressed in the scaled system.
        strength = smooth * float(np.median(diagonal[diagonal > 1e-30]))
        stencil = np.outer([1.0, -2.0, 1.0], [1.0, -2.0, 1.0])
        for idx in diff:
            scaled[np.ix_(idx, idx)] += strength * stencil / np.outer(d[idx], d[idx])
        x = np.linalg.solve(scaled, rhs / d) / d
        for c, (A, y, w, meta) in blocks.items():
            if A.size == 0:
                continue
            residual = (A @ x[c * PER_CONDITION:(c + 1) * PER_CONDITION] - y) * w
            per_harmonic = np.hypot(residual[0::2], residual[1::2])
            emphasis[c] = np.repeat(np.maximum(per_harmonic / 0.2, 1.0), 2)
    return x


def evaluate(recs: list[dict], x: np.ndarray, g: dict) -> dict:
    errors, rows = [], []
    for record in recs:
        c = condition_index(record["level"], record["pr"], record["limit"])
        A, y, w, meta = design([record], g)
        if A.size == 0:
            continue
        pred = A @ x[c * PER_CONDITION:(c + 1) * PER_CONDITION]
        for i, (rec, h) in enumerate(meta):
            value = h["base"] + complex(pred[2 * i + 1], -pred[2 * i])
            e = 20.0 * math.log10(max(abs(value), 1e-30)) - h["uad_dbfs"]
            if h["eligible"]:
                errors.append(e)
                rows.append((rec["frequency"], rec["level"], rec["pr"], rec["gain"],
                             rec["limit"], h["h"], h["uad_dbfs"], e))
    errors = np.asarray(errors)
    return {"eligible": int(errors.size),
            "within_2db": int(np.count_nonzero(np.abs(errors) <= 2.0)),
            "worst_abs_db": float(np.max(np.abs(errors))),
            "rms_db": float(np.sqrt(np.mean(errors ** 2))), "rows": rows}


# ------------------------------------------------------------ generation
def header(x: np.ndarray, g: dict, provenance: str) -> str:
    full = np.zeros((2, 4, len(LEVELS), PER_CONDITION))
    full[:, 1:] = x.reshape(2, 3, len(LEVELS), PER_CONDITION)
    values = [f"{v:.9e}f" for v in full.ravel()]
    body = "\n".join("    " + ", ".join(values[i:i + 6]) + ","
                     for i in range(0, len(values), 6))
    return f'''// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
// Generated by tests/fit_opto_structural_harmonics.py -- do not edit.
// {provenance}
// Per mode, PR (0/.35/.70/1; PR 0 is all zero) and input level (-40..0 dBFS,
// 4 dB steps): 6 memoryless cell harmonics (H2..H7) and 8 light-ripple lines
// (n f, n = 1..8), each stored as (a, b) of a sin + b cos.  Frequency enters
// only through the fixed pre-shelf S(f), the fast response L(n f) and the
// cell's own ripple (scaled by the constant below); no coefficient depends on
// frequency.
#pragma once
#include <algorithm>
#include <array>
#include <complex>
#include <cstddef>

namespace duskaudio
{{
// Ripple/envelope split of the cell gain, as a fraction of the tone frequency.
inline constexpr double kOptoRippleSplitRatio = {RIPPLE_SPLIT_RATIO:.3f};
// MC-2's fast loop is ~1.5x slower than the reference's (AM test tau_eq), so
// its ripple is ~1.5x too shallow: scale the ripple part of the cell gain.
inline constexpr float kOptoCellRippleScale = {RIPPLE_SCALE:.6f}f;
inline constexpr float kOptoCellShelfZeroHz = {g["f1"]:.6f}f;
inline constexpr float kOptoCellShelfPoleHz = {g["f2"]:.6f}f;
inline constexpr float kOptoCellFastResponseHz = {g["fc"]:.6f}f;
inline constexpr int kOptoStructuralStride = {PER_CONDITION};
inline constexpr std::array<float, {full.size}> kOptoStructuralTable{{{{
{body}
}}}};

namespace optoStructural
{{
struct Span {{ int lo, hi; float t; }};

inline Span levelSpan(float levelDb) noexcept
{{
    const float position = std::clamp((levelDb + 40.0f) / 4.0f, 0.0f, 10.0f);
    const int lo = std::min(static_cast<int>(position), 10);
    return {{lo, std::min(lo + 1, 10), position - static_cast<float>(lo)}};
}}

// PR at or below 10 is the inactive (PR 0) slice, as in the settled law.
inline Span prSpan(float peakReduction) noexcept
{{
    constexpr float points[4] = {{0.0f, 0.35f, 0.70f, 1.0f}};
    const float pr = peakReduction > 10.0f
        ? std::clamp(peakReduction * 0.01f, 0.0f, 1.0f) : 0.0f;
    if (pr <= 0.0f) return {{0, 0, 0.0f}};
    if (pr >= 1.0f) return {{3, 3, 0.0f}};
    int hi = 1;
    while (pr > points[hi]) ++hi;
    return {{hi - 1, hi, (pr - points[hi - 1]) / (points[hi] - points[hi - 1])}};
}}

inline float coefficient(int mode, const Span& ps, const Span& ls, int j) noexcept
{{
    const auto at = [mode, j](int pi, int li) noexcept {{
        return kOptoStructuralTable[static_cast<size_t>(
            ((mode * 4 + pi) * 11 + li) * kOptoStructuralStride + j)];
    }};
    const float lo = at(ps.lo, ls.lo) + (at(ps.lo, ls.hi) - at(ps.lo, ls.lo)) * ls.t;
    const float hi = at(ps.hi, ls.lo) + (at(ps.hi, ls.hi) - at(ps.hi, ls.lo)) * ls.t;
    return lo + (hi - lo) * ps.t;
}}
}} // namespace optoStructural

// Sine/cosine amplitudes of H2..H7 per unit cell fundamental, on the cell
// fundamental's phase, to be injected before Gain and the output stage.
inline std::array<float, 12> optoStructuralHarmonics(
    float frequencyHz, float inputLevelDb, float peakReduction, bool limit) noexcept
{{
    using namespace optoStructural;
    using C = std::complex<float>;
    const int mode = limit ? 1 : 0;
    const Span ls = levelSpan(inputLevelDb);
    const Span ps = prSpan(peakReduction);
    std::array<float, 12> result{{}};
    if (ps.hi == 0) return result;
    // Window coefficient of a sin + b cos is c = b - j a.
    const auto stored = [&](int j) noexcept {{
        return C(coefficient(mode, ps, ls, j + 1), -coefficient(mode, ps, ls, j));
    }};
    const C shelf = C(1.0f, frequencyHz / kOptoCellShelfZeroHz)
        / C(1.0f, frequencyHz / kOptoCellShelfPoleHz);
    std::array<C, 10> line{{}};
    for (int n = 1; n <= 8; ++n)
        line[static_cast<size_t>(n)] = stored(12 + 2 * (n - 1))
            / C(1.0f, static_cast<float>(n) * frequencyHz / kOptoCellFastResponseHz);
    C shelfPower = shelf;
    for (int h = 2; h <= 7; ++h)
    {{
        const C c = stored(2 * (h - 2)) * shelfPower
            + line[static_cast<size_t>(h - 1)] + line[static_cast<size_t>(h + 1)];
        result[static_cast<size_t>(2 * (h - 2))] = -c.imag();
        result[static_cast<size_t>(2 * (h - 2) + 1)] = c.real();
        shelfPower *= shelf;
    }}
    return result;
}}
}} // namespace duskaudio
'''


def write(x: np.ndarray, g: dict, provenance: str) -> None:
    OUTPUT.write_text(header(x, g, provenance))
    print(f"wrote {OUTPUT} ({sha256(OUTPUT)[:16]})")


def main() -> None:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("zero")
    p = sub.add_parser("probe"); p.add_argument("kappa", type=float)
    p = sub.add_parser("fit"); p.add_argument("base_tag"); p.add_argument("probe_tag")
    p.add_argument("probe_kappa", type=float)
    p.add_argument("--generate", action="store_true")
    p.add_argument("--f1", type=float, default=200.0)
    p.add_argument("--ratio", type=float, default=1.4)
    p.add_argument("--fc", type=float, default=100.0)
    p.add_argument("--smooth", type=float, default=1e-4)
    p.add_argument("--cv", action="store_true")
    args = parser.parse_args()
    if args.command in ("zero", "probe"):
        x = np.zeros(CONDITIONS * PER_CONDITION)
        global RIPPLE_SCALE
        RIPPLE_SCALE = args.kappa if args.command == "probe" else 0.0
        write(x, {"f1": 200.0, "f2": 200.0, "fc": 64.0},
              "Base build: every term zero." if args.command == "zero" else
              f"Ripple probe build: ripple scale {args.kappa:g}, other terms zero.")
        return
    g = {"f1": args.f1, "f2": args.f1 * args.ratio, "fc": args.fc}
    recs = cells(load("base", "uad"), load(args.base_tag, "mc2"),
                 load(args.probe_tag, "mc2"), args.probe_kappa)
    x = fit_all(recs, g, args.smooth)
    result = evaluate(recs, x, g)
    report = {"firewall": "R10 fit grid only; no held-out file is read",
              "base_tag": args.base_tag, "probe_tag": args.probe_tag,
              "globals": g, "smooth": args.smooth,
              "fitted": {k: v for k, v in result.items() if k != "rows"}}
    print(json.dumps(report["fitted"]), flush=True)
    if args.cv:
        held = []
        for f in FREQUENCIES:
            xv = fit_all([r for r in recs if r["frequency"] != f], g, args.smooth)
            held += evaluate([r for r in recs if r["frequency"] == f], xv, g)["rows"]
        e = np.asarray([r[-1] for r in held])
        report["leave_one_frequency_out"] = {
            "eligible": int(e.size), "within_2db": int(np.count_nonzero(np.abs(e) <= 2)),
            "worst_abs_db": float(np.max(np.abs(e)))}
        print("LOFO", report["leave_one_frequency_out"], flush=True)
    (EVIDENCE / "r10-structural-fit.json").write_text(json.dumps(report, indent=1) + "\n")
    if args.generate:
        write(x, g, f"Fit: UAD base + MC-2 {args.base_tag}/{args.probe_tag}; R10 fit grid; "
                    f"shelf {g['f1']:g}/{g['f2']:g} Hz, fast response {g['fc']:g} Hz.")


if __name__ == "__main__":
    main()
