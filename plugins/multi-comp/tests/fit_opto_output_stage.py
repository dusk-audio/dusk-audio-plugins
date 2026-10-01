#!/usr/bin/env python3
"""Fit the Opto output stage as one static, memoryless transfer curve.

The reference's uncompressed (PR 0) harmonics are the same from 30 Hz to
8 kHz to within ~0.1 dB, and their phases sit at 0/180 degrees apart from the
sub-audio high-passes' 1/f offsets: the stage is memoryless.  A memoryless
curve is fixed by its sine responses over amplitude, so its samples are fitted
by linear least squares to the reference's complex H1-H7 at every PR 0
drive of the R10 fit grid (drive = input level + Gain, all fitting Gains).

The curve is ``y = f(x)``, a uniform Catmull-Rom interpolation of node values
in ``s = asinh(x / X0)``; the C++ evaluates exactly the same interpolation.
The fitter reads only the R10 fit grid's reference file.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[3]
EVIDENCE = REPO / "build-multi-comp-1176/opto-production-harmonics-20260928"
UAD_PATH = EVIDENCE / "r10-fit-base-uad.json"
OUTPUT = REPO / "plugins/multi-comp/core/MultiCompOptoOutputStage.hpp"
X0 = 2.0e-2
S_STEP = 0.05
X_MAX = 64.0
S_MAX = math.ceil(math.asinh(X_MAX / X0) / S_STEP) * S_STEP
NODES = int(round(2 * S_MAX / S_STEP)) + 1
FIT_FREQUENCY = 1000
GAINS = (0.15, 0.25, 0.35, 0.50, 0.65, 0.80)
MAKEUP_DB = {0.15: -8.777571, 0.25: 0.999066, 0.35: 7.824299,
             0.50: 14.5, 0.65: 21.813084, 0.80: 33.268223}
PHASES = 32768
FLOOR_DBFS = -100.0
EVEN_DRIVE_FIRST_DB, EVEN_DRIVE_STEP_DB, EVEN_DRIVE_COUNT = -90.0, 0.5, 261


def catmull_rom_weights(x: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Node indices (n, 4) and weights (n, 4) for f evaluated at x."""
    s = np.arcsinh(np.asarray(x) / X0)
    position = np.clip((s + S_MAX) / S_STEP, 0.0, NODES - 1.0)
    i = np.minimum(np.floor(position).astype(int), NODES - 2)
    t = position - i
    t2, t3 = t * t, t * t * t
    w = np.stack([-0.5 * t3 + t2 - 0.5 * t,
                  1.5 * t3 - 2.5 * t2 + 1.0,
                  -1.5 * t3 + 2.0 * t2 + 0.5 * t,
                  0.5 * t3 - 0.5 * t2], axis=1)
    idx = np.stack([i - 1, i, i + 1, i + 2], axis=1)
    # Clamp the stencil at the ends: the end node is repeated (flat plateau).
    idx = np.clip(idx, 0, NODES - 1)
    return idx, w


def harmonic_matrix(amplitude: float) -> np.ndarray:
    """(7, NODES) complex map from node values to H1..H7 of f(A sin theta)."""
    theta = 2.0 * np.pi * np.arange(PHASES) / PHASES
    idx, w = catmull_rom_weights(amplitude * np.sin(theta))
    samples = np.zeros((PHASES, NODES))
    np.add.at(samples, (np.repeat(np.arange(PHASES), 4), idx.ravel()), w.ravel())
    kernel = np.exp(-1j * np.outer(np.arange(1, 8), theta)) * 2.0 / PHASES
    return kernel @ samples


def load_targets() -> list[dict]:
    rows = json.loads(UAD_PATH.read_text())
    cells = {}
    for r in rows:
        if (r["frequency_hz"] != FIT_FREQUENCY or r["peak_reduction_normalised"] != 0.0
                or r["limit"] or r["gain_normalised"] not in GAINS):
            continue
        key = (r["input_dbfs"], r["gain_normalised"])
        cell = cells.setdefault(key, {"h1_dbfs": r["fundamental_dbfs"], "h": {}})
        if r["representable"]:
            cell["h"][r["harmonic"]] = (r["magnitude_dbfs"], r["relative_phase_degrees"])
    targets = []
    for (level, gain), cell in sorted(cells.items()):
        drive = level + MAKEUP_DB[gain]
        wanted = {1: complex(0.0, -10.0 ** (cell["h1_dbfs"] / 20.0))}
        for k, (mag, phase) in cell["h"].items():
            # Model frame: fundamental at -90 degrees (sine), so harmonic k's
            # absolute phase is its relative phase minus k * 90 degrees.
            angle = math.radians(phase - 90.0 * k)
            wanted[k] = 10.0 ** (mag / 20.0) * complex(math.cos(angle), math.sin(angle))
        targets.append({"level": level, "gain": gain, "drive_db": drive,
                        "amplitude": 10.0 ** (drive / 20.0), "wanted": wanted})
    return targets


def fit(targets: list[dict], smooth: float, iterations: int = 10) -> np.ndarray:
    blocks, rhs, meta = [], [], []
    for t in targets:
        H = harmonic_matrix(t["amplitude"])
        for k, value in t["wanted"].items():
            blocks.append(H[k - 1]); rhs.append(value)
            meta.append((k, max(abs(value), 10.0 ** (FLOOR_DBFS / 20.0)),
                         abs(value) > 10.0 ** (FLOOR_DBFS / 20.0)))
    H = np.asarray(blocks); y = np.asarray(rhs)
    base_w = np.asarray([(30.0 if k == 1 else 1.0) / m for k, m, _ in meta])
    D = np.zeros((NODES - 2, NODES))
    for i in range(NODES - 2):
        D[i, i:i + 3] = (1.0, -2.0, 1.0)
    # f(0) = 0: harmonics never see a constant, so pin it (a free offset
    # would reach the post high-pass as a start-up step).
    centre = np.zeros((1, NODES)); centre[0, NODES // 2] = 1.0e3
    emphasis = np.ones(len(meta))
    f = None
    for _ in range(iterations):
        w = base_w * emphasis
        A = np.vstack([(w[:, None] * H).real, (w[:, None] * H).imag,
                       smooth * D, centre])
        b = np.concatenate([(w * y).real, (w * y).imag, np.zeros(NODES - 2),
                            np.zeros(1)])
        f = np.linalg.lstsq(A, b, rcond=None)[0]
        predicted = H @ f
        relative = np.abs(predicted - y) / np.asarray([m for _, m, _ in meta])
        emphasis = np.maximum(relative / 0.2, 0.3) ** 2.5
    return f


def evaluate(targets: list[dict], f: np.ndarray) -> dict:
    errors, h1 = [], []
    worst = []
    for t in targets:
        H = harmonic_matrix(t["amplitude"])
        predicted = H @ f
        for k, value in t["wanted"].items():
            measured_db = 20.0 * math.log10(abs(value))
            model_db = 20.0 * math.log10(max(abs(predicted[k - 1]), 1e-30))
            if k == 1:
                h1.append(model_db - measured_db)
            elif measured_db > FLOOR_DBFS:
                errors.append(model_db - measured_db)
                worst.append((abs(model_db - measured_db), t["level"], t["gain"], k,
                              round(measured_db, 1), round(model_db, 1)))
    errors = np.asarray(errors)
    worst.sort(reverse=True)
    return {"eligible": int(errors.size),
            "within_2db": int(np.count_nonzero(np.abs(errors) <= 2.0)),
            "worst_abs_db": float(np.max(np.abs(errors))),
            "h1_worst_abs_db": float(np.max(np.abs(h1))), "worst_rows": worst[:8]}


def header(f: np.ndarray, provenance: str) -> str:
    values = [f"{v:.9e}f" for v in f]
    body = "\n".join("    " + ", ".join(values[i:i + 6]) + ","
                     for i in range(0, len(values), 6))
    return f'''// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
// Generated by tests/fit_opto_output_stage.py -- do not edit.
// {provenance}
// Static, memoryless Opto output-stage transfer y = f(x): uniform Catmull-Rom
// interpolation of {NODES} samples in s = asinh(x / {X0:g}), |x| <= {X_MAX:g}; the
// end samples hold beyond that (the plateaus).
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace duskaudio
{{
inline constexpr float kOptoOutputStageX0 = {X0:.9e}f;
inline constexpr float kOptoOutputStageStep = {S_STEP:.9e}f;
inline constexpr float kOptoOutputStageSMax = {S_MAX:.9e}f;
inline constexpr std::array<float, {NODES}> kOptoOutputStageCurve{{{{
{body}
}}}};

inline constexpr int kOptoOutputStageLast =
    static_cast<int>(kOptoOutputStageCurve.size()) - 1;

inline float optoOutputStageCurve(float x) noexcept
{{
    constexpr int last = kOptoOutputStageLast;
    const float s = std::asinh(x / kOptoOutputStageX0);
    // Written so NaN fails the comparison and lands on a finite node.
    const float raw = (s + kOptoOutputStageSMax) / kOptoOutputStageStep;
    const float position = raw > 0.0f
        ? (raw < static_cast<float>(last) ? raw : static_cast<float>(last)) : 0.0f;
    const int i = std::min(static_cast<int>(position), last - 1);
    const float t = position - static_cast<float>(i);
    const auto at = [](int n) noexcept {{
        return kOptoOutputStageCurve[static_cast<size_t>(
            std::clamp(n, 0, kOptoOutputStageLast))];
    }};
    const float t2 = t * t, t3 = t2 * t;
    return at(i - 1) * (-0.5f * t3 + t2 - 0.5f * t)
         + at(i) * (1.5f * t3 - 2.5f * t2 + 1.0f)
         + at(i + 1) * (-1.5f * t3 + 2.0f * t2 + 0.5f * t)
         + at(i + 2) * (0.5f * t3 - 0.5f * t2);
}}

}} // namespace duskaudio
'''


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--smooth", type=float, default=3.0e-2)
    parser.add_argument("--generate", action="store_true")
    args = parser.parse_args()
    targets = load_targets()
    f = fit(targets, args.smooth)
    report = {"firewall": "R10 fit-grid reference, 1 kHz PR 0 only",
              "nodes": NODES, "smooth": args.smooth, "fit": evaluate(targets, f)}
    # Leave-one-Gain-out, inside the fit grid.
    held = []
    for gain in GAINS:
        train = [t for t in targets if t["gain"] != gain]
        test = [t for t in targets if t["gain"] == gain]
        held.append({"gain": gain, **{k: v for k, v in evaluate(
            test, fit(train, args.smooth)).items() if k != "worst_rows"}})
    report["leave_one_gain_out"] = held
    print(json.dumps(report, indent=1))
    if args.generate:
        provenance = (f"Fit: {UAD_PATH.name} (SHA-256 "
                      f"{hashlib.sha256(UAD_PATH.read_bytes()).hexdigest()[:16]}), "
                      f"1 kHz PR 0, Gains {', '.join(f'{g:.2f}' for g in GAINS)}.")
        OUTPUT.write_text(header(f, provenance))
        (EVIDENCE / "r10-output-stage-fit.json").write_text(json.dumps(report, indent=1) + "\n")
        print(f"wrote {OUTPUT}")


if __name__ == "__main__":
    main()
