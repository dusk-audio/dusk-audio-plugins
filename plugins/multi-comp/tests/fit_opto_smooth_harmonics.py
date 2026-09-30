#!/usr/bin/env python3
"""Fit one smooth log-frequency Opto harmonic calibration.

The generated runtime table contains Chebyshev polynomial coefficients, never
per-frequency values.  Model order and ridge strength are selected using only
leave-one-fit-frequency-out predictions.  The separate R9 holdout capture is
deliberately not read here.
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
UAD_PATH = EVIDENCE / "r9-fit-base-uad.json"
MC2_PATH = EVIDENCE / "r9-fit-r9-linear-base-mc2.json"
OUTPUT = REPO / "plugins/multi-comp/core/MultiCompOptoSmoothHarmonics.hpp"
FREQUENCIES = (50, 100, 200, 500, 1000, 2000, 5000, 8000)
LEVELS = tuple(range(-40, 1, 4))
GAINS = (0.15, 0.25, 0.35)
PRS = (0.0, 0.35, 0.70, 1.0)
HARMONICS = tuple(range(2, 8))
LOG_MIN = math.log(FREQUENCIES[0])
LOG_MAX = math.log(FREQUENCIES[-1])


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def key(row: dict) -> tuple:
    return (row["frequency_hz"], row["input_dbfs"],
            row["peak_reduction_normalised"], row["gain_normalised"],
            row["limit"], row["harmonic"])


def relative_complex(row: dict) -> complex:
    magnitude = 10.0 ** (row["magnitude_dbfs"] / 20.0)
    phase = math.radians(row["relative_phase_degrees"])
    return magnitude * complex(math.cos(phase), math.sin(phase))


def axes(reference: dict, candidate: dict) -> tuple[float, float]:
    residual = relative_complex(reference) - relative_complex(candidate)
    harmonic = candidate["harmonic"]
    fundamental_phase = candidate["fundamental_phase_degrees"]
    sin_phase = math.radians(-90.0 - harmonic * fundamental_phase)
    cos_phase = math.radians(-harmonic * fundamental_phase)
    sin_axis = complex(math.cos(sin_phase), math.sin(sin_phase))
    cos_axis = complex(math.cos(cos_phase), math.sin(cos_phase))
    return ((residual * sin_axis.conjugate()).real,
            (residual * cos_axis.conjugate()).real)


def normalised_log_frequency(frequency: float) -> float:
    return 2.0 * (math.log(frequency) - LOG_MIN) / (LOG_MAX - LOG_MIN) - 1.0


def design(frequencies: list[float], degree: int) -> np.ndarray:
    x = np.asarray([normalised_log_frequency(f) for f in frequencies])
    return np.polynomial.chebyshev.chebvander(x, degree)


def fit_values(frequencies: list[float], values: list[float], degree: int,
               ridge: float) -> np.ndarray:
    matrix = design(frequencies, degree)
    penalty = np.diag([0.0] + [ridge * i * i for i in range(1, degree + 1)])
    return np.linalg.solve(matrix.T @ matrix + penalty,
                           matrix.T @ np.asarray(values))


def evaluate(coefficients: np.ndarray, frequency: float) -> float:
    return float(np.polynomial.chebyshev.chebval(
        normalised_log_frequency(frequency), coefficients))


def load() -> tuple[dict, dict]:
    if not UAD_PATH.exists() or not MC2_PATH.exists():
        raise SystemExit(f"missing actual-AU fit maps: {UAD_PATH} / {MC2_PATH}")
    return ({key(row): row for row in json.loads(UAD_PATH.read_text())},
            {key(row): row for row in json.loads(MC2_PATH.read_text())})


def condition_keys() -> list[tuple]:
    return [(limit, pr, gain, level, harmonic)
            for limit in (False, True) for pr in PRS for gain in GAINS
            for level in LEVELS for harmonic in HARMONICS]


def rows_for(uad: dict, mc2: dict, condition: tuple) -> list[tuple]:
    limit, pr, gain, level, harmonic = condition
    source_limit = False if pr == 0.0 else limit
    result = []
    for frequency in FREQUENCIES:
        row_key = (frequency, level, pr, gain, source_limit, harmonic)
        reference, candidate = uad[row_key], mc2[row_key]
        if reference["representable"]:
            result.append((frequency, reference, candidate, axes(reference, candidate)))
    return result


def score(uad: dict, mc2: dict, degree: int, ridge: float,
          leave_frequency_out: bool) -> dict:
    errors = []
    eligible_errors = []
    for condition in condition_keys():
        rows = rows_for(uad, mc2, condition)
        for held_index, (frequency, reference, candidate, _) in enumerate(rows):
            training = [row for index, row in enumerate(rows)
                        if not leave_frequency_out or index != held_index]
            frequencies = [row[0] for row in training]
            sin_values = [row[3][0] for row in training]
            cos_values = [row[3][1] for row in training]
            sin_coefficient = fit_values(frequencies, sin_values, degree, ridge)
            cos_coefficient = fit_values(frequencies, cos_values, degree, ridge)
            sin_value = evaluate(sin_coefficient, frequency)
            cos_value = evaluate(cos_coefficient, frequency)
            harmonic = candidate["harmonic"]
            fundamental_phase = candidate["fundamental_phase_degrees"]
            sin_phase = math.radians(-90.0 - harmonic * fundamental_phase)
            cos_phase = math.radians(-harmonic * fundamental_phase)
            predicted = relative_complex(candidate) \
                + sin_value * complex(math.cos(sin_phase), math.sin(sin_phase)) \
                + cos_value * complex(math.cos(cos_phase), math.sin(cos_phase))
            predicted_dbfs = 20.0 * math.log10(max(abs(predicted), 1.0e-30))
            error = predicted_dbfs - reference["magnitude_dbfs"]
            errors.append(error)
            if reference["magnitude_dbfs"] > -100.0:
                eligible_errors.append(error)
    values = np.asarray(eligible_errors)
    return {
        "degree": degree, "ridge": ridge,
        "leave_one_frequency_out": leave_frequency_out,
        "eligible_cells": len(eligible_errors),
        "passing_cells": int(np.count_nonzero(np.abs(values) <= 2.0)),
        "worst_abs_error_db": float(np.max(np.abs(values))),
        "rms_error_db": float(np.sqrt(np.mean(np.square(values)))),
    }


def select(uad: dict, mc2: dict) -> tuple[int, float, list[dict]]:
    trials = []
    for degree in range(1, 6):
        for ridge in (1.0e-12, 1.0e-10, 1.0e-8, 1.0e-6, 1.0e-4):
            cv = score(uad, mc2, degree, ridge, True)
            fitted = score(uad, mc2, degree, ridge, False)
            trials.append({"cross_validation": cv, "fitted": fitted})
    best = min(trials, key=lambda trial: (
        -trial["cross_validation"]["passing_cells"],
        trial["cross_validation"]["rms_error_db"],
        trial["cross_validation"]["worst_abs_error_db"],
        trial["cross_validation"]["degree"],
        -trial["cross_validation"]["ridge"]))
    return (best["cross_validation"]["degree"],
            best["cross_validation"]["ridge"], trials)


def generate(uad: dict, mc2: dict, degree: int, ridge: float) -> None:
    values = []
    for condition in condition_keys():
        rows = rows_for(uad, mc2, condition)
        frequencies = [row[0] for row in rows]
        for axis_index in range(2):
            coefficients = fit_values(
                frequencies, [row[3][axis_index] for row in rows], degree, ridge)
            values.extend(float(value) for value in coefficients)
    lines = ["    " + ", ".join(f"{value:.10e}f" for value in values[i:i + 6]) + ","
             for i in range(0, len(values), 6)]
    count = degree + 1
    header = f'''// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
// Generated from actual AU renders by tests/fit_opto_smooth_harmonics.py.
// Frequency law: degree-{degree} Chebyshev polynomial in log frequency.
// Fit frequencies: 50, 100, 200, 500, 1000, 2000, 5000 and 8000 Hz.
// There are no stored per-frequency calibration values.
// Ridge: {ridge:.10e}; UAD SHA-256: {sha256(UAD_PATH)}; MC-2 SHA-256: {sha256(MC2_PATH)}.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace duskaudio
{{
inline constexpr int kOptoSmoothDegree = {degree};
inline constexpr int kOptoSmoothCoefficientCount = {count};
inline constexpr std::array<float, {len(values)}> kOptoSmoothHarmonicTable{{{{
{chr(10).join(lines)}
}}}};

inline std::array<float, 12> optoSmoothHarmonicResiduals(
    float inputLevelDb, float frequencyHz, float peakReduction,
    float gainKnob, bool limit) noexcept
{{
    struct Span {{ int lo, hi; float t; }};
    const auto uniformSpan = [](float value, float first, float step, int count) noexcept {{
        const float position = std::clamp((value - first) / step, 0.0f,
                                          static_cast<float>(count - 1));
        const int lo = std::min(static_cast<int>(position), count - 1);
        const int hi = std::min(lo + 1, count - 1);
        return Span{{lo, hi, position - static_cast<float>(lo)}};
    }};
    constexpr float prPoints[4] = {{0.0f, 0.35f, 0.70f, 1.0f}};
    const float pr = std::clamp(peakReduction * 0.01f, 0.0f, 1.0f);
    const float gain = std::clamp(gainKnob * 0.01f, 0.0f, 1.0f);
    const auto prSpan = [&]() noexcept {{
        if (pr <= prPoints[0]) return Span{{0, 0, 0.0f}};
        if (pr >= prPoints[3]) return Span{{3, 3, 0.0f}};
        int hi = 1;
        while (hi < 4 && pr > prPoints[hi]) ++hi;
        const int lo = hi - 1;
        return Span{{lo, hi, (pr - prPoints[lo]) / (prPoints[hi] - prPoints[lo])}};
    }}();
    const Span gs = uniformSpan(gain, 0.15f, 0.10f, 3);
    const Span ls = uniformSpan(inputLevelDb, -40.0f, 4.0f, 11);
    const int mode = limit ? 1 : 0;
    const auto at = [mode](int pi, int gi, int li, int harmonic,
                           int axis, int coefficient) noexcept {{
        const size_t index = static_cast<size_t>((((((mode * 4 + pi) * 3 + gi)
            * 11 + li) * 6 + harmonic) * 2 + axis)
            * kOptoSmoothCoefficientCount + coefficient);
        return kOptoSmoothHarmonicTable[index];
    }};
    const float x = 2.0f * (std::log(std::max(frequencyHz, 1.0f))
        - {LOG_MIN:.10e}f) / ({LOG_MAX:.10e}f - {LOG_MIN:.10e}f) - 1.0f;
    std::array<float, 12> result{{}};
    for (int harmonic = 0; harmonic < 6; ++harmonic)
        for (int axis = 0; axis < 2; ++axis)
        {{
            std::array<float, {count}> coefficients{{}};
            for (int ci = 0; ci < kOptoSmoothCoefficientCount; ++ci)
            {{
                const auto levelMix = [&](int pi, int gi) noexcept {{
                    const float a = at(pi, gi, ls.lo, harmonic, axis, ci);
                    return a + (at(pi, gi, ls.hi, harmonic, axis, ci) - a) * ls.t;
                }};
                const auto gainMix = [&](int pi) noexcept {{
                    const float a = levelMix(pi, gs.lo);
                    return a + (levelMix(pi, gs.hi) - a) * gs.t;
                }};
                const float a = gainMix(prSpan.lo);
                coefficients[static_cast<size_t>(ci)] =
                    a + (gainMix(prSpan.hi) - a) * prSpan.t;
            }}
            float b1 = 0.0f, b2 = 0.0f;
            for (int ci = kOptoSmoothDegree; ci >= 1; --ci)
            {{
                const float next = 2.0f * x * b1 - b2
                    + coefficients[static_cast<size_t>(ci)];
                b2 = b1; b1 = next;
            }}
            result[static_cast<size_t>(2 * harmonic + axis)] =
                x * b1 - b2 + coefficients[0];
        }}
    return result;
}}
}} // namespace duskaudio
'''
    OUTPUT.write_text(header)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--generate", action="store_true")
    args = parser.parse_args()
    uad, mc2 = load()
    degree, ridge, trials = select(uad, mc2)
    chosen = next(t for t in trials
                  if t["cross_validation"]["degree"] == degree
                  and t["cross_validation"]["ridge"] == ridge)
    report = {"selection_firewall": "fit grid only; holdout file never read",
              "fit_frequencies_hz": FREQUENCIES, "degree": degree,
              "ridge": ridge, "chosen": chosen, "trials": trials}
    if args.generate:
        generate(uad, mc2, degree, ridge)
        report["output"] = str(OUTPUT)
        report["output_sha256"] = sha256(OUTPUT)
    (EVIDENCE / "r9-smooth-fit.json").write_text(
        json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: v for k, v in report.items() if k != "trials"}, indent=2))


if __name__ == "__main__":
    main()
