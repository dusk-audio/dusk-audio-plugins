#!/usr/bin/env python3
"""Fit DuskVerb's final three-band stereo profile to captured anchors.

Run stereo_jnd_audit.py with DUSKVERB_POSTSTEER=0 first.  This fitter then
applies the same causal split, constant-power gains, and 2 ms gain smoothing as
the C++ stage to the saved hard-left/right stems and solves all profile values
without relaunching either plugin.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy.optimize import least_squares
from scipy.signal import lfilter

import stereo_jnd_audit as audit


PREDELAY_MS = {
    "Vocal Plate": 18.8, "Drum Plate": 0.0, "Bright Hall": 25.2,
    "Vocal Hall": 8.0, "Cathedral Large Hall": 20.0,
    "Blade Runner 224": 35.9, "79 Vocal Chamber": 0.0,
    "Small Drum Room": 0.0, "Medium Drum Room": 5.1,
    "Tiled Room": 2.9, "Ambience": 0.0, "Vintage Vocal Plate": 10.0,
    "Vintage Gold Plate": 0.0, "Large Chamber": 6.6, "Live Room": 18.0,
    "Reverse Taps": 94.4, "Black Hole": 0.0, "Deep Blue Day": 25.0,
}


def split_bands(x: np.ndarray, sr: int) -> np.ndarray:
    c1 = math.exp(-2.0 * math.pi * 300.0 / sr)
    c2 = math.exp(-2.0 * math.pi * 2000.0 / sr)
    lp1 = lfilter([1.0 - c1], [1.0, -c1], x, axis=0)
    lp2 = lfilter([1.0 - c2], [1.0, -c2], x, axis=0)
    return np.stack((lp1, lp2 - lp1, x - lp2), axis=0)


def smooth_gain(target: np.ndarray, sr: int) -> np.ndarray:
    a = math.exp(-1.0 / (0.002 * sr))
    out, _ = lfilter([1.0 - a], [1.0, -a], target, zi=[a])
    return out


def time_envelope(n: int, sr: int, predelay_ms: float, hold_ms: float,
                  release_ms: float) -> np.ndarray:
    start = 1000 + int(round(predelay_ms * 0.001 * sr))
    stop = start + int(round((0.005 + 0.001 * hold_ms) * sr))
    env = np.zeros(n, dtype=np.float64)
    env[start:min(stop, n)] = 1.0
    if stop < n:
        if release_ms <= 0.0:
            env[stop:] = 1.0
        else:
            a = math.exp(-1.0 / (0.001 * release_ms * sr))
            env[stop:] = a ** np.arange(1, n - stop + 1, dtype=np.float64)
    return env


def apply_profile(bands: np.ndarray, sign: float, sr: int, predelay_ms: float,
                  params: np.ndarray) -> np.ndarray:
    early = params[:3]
    middle = params[3:6]
    late = params[6:9]
    hold_ms = float(params[9])
    fast_ms = math.exp(float(params[10]))
    slow_ms = fast_ms * math.exp(float(params[11]))
    fast_env = time_envelope(bands.shape[1], sr, predelay_ms, hold_ms, fast_ms)
    slow_env = time_envelope(bands.shape[1], sr, predelay_ms, hold_ms, slow_ms)
    wander = None
    if len(params) >= 18:
        start = 1000 + int(round(predelay_ms * 0.001 * sr))
        stop = start + int(round(0.005 * sr))
        elapsed = np.maximum(np.arange(bands.shape[1], dtype=np.float64) - stop, 0.0) / sr
        rate_hz = math.exp(float(params[15]))
        decay_s = 0.001 * math.exp(float(params[16]))
        wander = np.sin(float(params[17]) + 2.0 * math.pi * rate_hz * elapsed)
        wander *= np.exp(-elapsed / decay_s)
        wander[:start] = 0.0
    out = np.zeros_like(bands[0])
    for band in range(3):
        k = (late[band]
             + (middle[band] - late[band]) * slow_env
             + (early[band] - middle[band]) * fast_env)
        if wander is not None:
            k = np.clip(k + params[12 + band] * wander, -0.9999, 0.9999)
        gl = smooth_gain(np.sqrt(np.maximum(0.0, 1.0 + sign * k)), sr)
        gr = smooth_gain(np.sqrt(np.maximum(0.0, 1.0 - sign * k)), sr)
        out[:, 0] += bands[band, :, 0] * gl
        out[:, 1] += bands[band, :, 1] * gr
    return out


def metrics(left: np.ndarray, right: np.ndarray, center: np.ndarray, sr: int):
    lt, rt, ct = audit._tail(left, sr), audit._tail(right, sr), audit._tail(center, sr)
    n = min(len(lt), len(rt))
    m = audit.Metrics(
        ild_db=audit._ild(lt),
        ild_low_db=audit._band_ild(lt, sr, 80.0, 300.0),
        ild_mid_db=audit._band_ild(lt, sr, 300.0, 2000.0),
        ild_high_db=audit._band_ild(lt, sr, 2000.0, 10000.0),
        pan_swap_db=audit._normalised_difference_db(lt[:n], rt[:n]),
        mirror_residual_db=audit._normalised_difference_db(lt[:n], rt[:n, ::-1]),
        center_corr=audit._corr(ct),
        center_side_mid_db=audit._side_mid(ct),
    )
    return m, audit._trajectory(left, sr)


def quick_values(left: np.ndarray, right: np.ndarray, sr: int):
    """Optimization metrics without repeated long FFTs.

    The causal 300/2k split is the stage being fitted, so its per-band energy is
    also the most direct optimization coordinate. The authoritative FFT gates
    are still evaluated once on the winning profile below.
    """
    lt, rt = audit._tail(left, sr), audit._tail(right, sr)
    lb = split_bands(left, sr)[:, audit.TAIL_START:min(len(left), int(audit.TAIL_END_SECONDS * sr))]
    band_ild = []
    for band in range(3):
        energy = np.sum(lb[band] ** 2, axis=0)
        band_ild.append(10.0 * math.log10((float(energy[0]) + audit.EPS) /
                                           (float(energy[1]) + audit.EPS)))
    n = min(len(lt), len(rt))
    return np.asarray([
        audit._ild(lt), *band_ild,
        audit._normalised_difference_db(lt[:n], rt[:n]),
    ]), audit._trajectory(left, sr)


def load_set(root: Path, prefix: str):
    data = {}
    sr = None
    for side in ("left", "right", "center"):
        data[side], this_sr = sf.read(root / f"{prefix}_{side}_stem.wav",
                                      always_2d=True, dtype="float64")
        sr = this_sr if sr is None else sr
        if this_sr != sr:
            raise ValueError("sample-rate mismatch")
    return data, int(sr)


def fit_preset(preset: str, root: Path, thorough: bool = False) -> dict:
    slug = preset.lower().replace(" ", "_").replace("'", "")
    dv, sr = load_set(root / slug / "dv", "dv")
    ref, ref_sr = load_set(root / slug / "ref", "ref")
    if sr != ref_sr:
        raise ValueError("reference sample-rate mismatch")
    ref_m, ref_traj = metrics(ref["left"], ref["right"], ref["center"], sr)
    ref_quick, ref_quick_traj = quick_values(ref["left"], ref["right"], sr)
    bands_l = split_bands(dv["left"], sr)
    bands_r = split_bands(dv["right"], sr)
    predelay = PREDELAY_MS[preset]

    def evaluate(p: np.ndarray):
        left = apply_profile(bands_l, +1.0, sr, predelay, p)
        right = apply_profile(bands_r, -1.0, sr, predelay, p)
        return (*metrics(left, right, dv["center"], sr), left, right)

    def trajectory_residual(candidate: np.ndarray, reference: np.ndarray) -> list[float]:
        """Keep the residual dimension fixed when a candidate frame is silent."""
        n = min(len(candidate), len(reference))
        keep = np.isfinite(reference[:n])
        if not np.any(keep):
            return []
        values = candidate[:n][keep] - reference[:n][keep]
        # A profile can briefly cancel both channels closely enough for the
        # audit's relative silence test. Treat that as a large miss instead of
        # dropping the frame and changing least_squares' residual dimension.
        values = np.nan_to_num(values, nan=2.0 * audit.TRAJECTORY_ILD_LIMIT_DB)
        return values.tolist()

    def residual(p: np.ndarray) -> np.ndarray:
        left = apply_profile(bands_l, +1.0, sr, predelay, p)
        right = apply_profile(bands_r, -1.0, sr, predelay, p)
        q, traj = quick_values(left, right, sr)
        values = (3.0 * (q - ref_quick)).tolist()
        values.extend(trajectory_residual(traj, ref_quick_traj))
        return np.asarray(values, dtype=np.float64)

    bounds = (np.array([-0.9999] * 9 + [0.0, math.log(15.0), 0.0]),
              np.array([+0.9999] * 9 + [800.0, math.log(2000.0), math.log(30.0)]))
    starts = (
        np.array([0.9, 0.9, 0.9, -0.3, -0.3, -0.3, 0.0, 0.0, 0.0,
                  120.0, math.log(140.0), math.log(5.0)]),
    )
    if thorough:
        starts += (
            np.array([0.0] * 9 + [0.0, math.log(50.0), math.log(4.0)]),
            np.array([0.8, -0.8, 0.8, -0.8, 0.8, -0.8, 0.2, -0.2, 0.2,
                      40.0, math.log(35.0), math.log(8.0)]),
            np.array([-0.8, 0.8, -0.8, 0.8, -0.8, 0.8, -0.2, 0.2, -0.2,
                      250.0, math.log(250.0), math.log(3.0)]),
        )
    best = None
    for start in starts:
        result = least_squares(residual, start, bounds=bounds,
                               max_nfev=140 if thorough else 80,
                               x_scale="jac", ftol=2e-5, xtol=2e-5, gtol=2e-5)
        score = float(np.sqrt(np.mean(residual(result.x) ** 2)))
        if best is None or score < best[0]:
            best = (score, result.x)
    assert best is not None
    _, quick_p = best

    # Short authoritative refinement. The quick stage gets the temporal shape
    # close cheaply; this stage uses the audit's exact FFT bands to land every
    # published 1 dB gate rather than trusting crossover-energy surrogates.
    def full_residual(p: np.ndarray) -> np.ndarray:
        m, traj, _, _ = evaluate(p)
        values = [4.0 * (m.ild_db - ref_m.ild_db),
                  4.0 * (m.ild_low_db - ref_m.ild_low_db),
                  4.0 * (m.ild_mid_db - ref_m.ild_mid_db),
                  4.0 * (m.ild_high_db - ref_m.ild_high_db),
                  4.0 * (m.pan_swap_db - ref_m.pan_swap_db)]
        values.extend(trajectory_residual(traj, ref_traj))
        return np.asarray(values, dtype=np.float64)

    refined = least_squares(full_residual, quick_p, bounds=bounds,
                            max_nfev=120 if thorough else 55,
                            x_scale="jac", ftol=1e-5, xtol=1e-5, gtol=1e-5)
    p = refined.x
    score = float(np.sqrt(np.mean(full_residual(p) ** 2)))
    m, traj, _, _ = evaluate(p)
    failures, actual = audit._compare(m, ref_m, traj, ref_traj)
    fast_ms = math.exp(float(p[10]))
    profile = ([float(v) for v in p[:9]] + [float(p[9]), fast_ms,
               fast_ms * math.exp(float(p[11]))])
    return {
        "preset": preset, "profile": profile, "fit_rms": score,
        "actual_stereo_reference": actual, "metrics": vars(m),
        "reference": vars(ref_m), "failures": failures,
    }


def fit_wander_preset(preset: str, root: Path, base_profile: list[float]) -> dict:
    """Add a damped image wander to a fitted profile.

    Lexicon's Blade Runner image crosses the centre repeatedly during the first
    second. A monotonic early/middle/late envelope cannot reproduce that motion,
    so solve one input-keyed, damped sinusoid shared by the three image bands.
    """
    slug = preset.lower().replace(" ", "_").replace("'", "")
    dv, sr = load_set(root / slug / "dv", "dv")
    ref, ref_sr = load_set(root / slug / "ref", "ref")
    if sr != ref_sr:
        raise ValueError("reference sample-rate mismatch")
    ref_m, ref_traj = metrics(ref["left"], ref["right"], ref["center"], sr)
    bands_l = split_bands(dv["left"], sr)
    bands_r = split_bands(dv["right"], sr)
    predelay = PREDELAY_MS[preset]
    base = np.asarray(base_profile[:10] + [math.log(base_profile[10]),
                                           math.log(base_profile[11] / base_profile[10])],
                      dtype=np.float64)

    def expand(w: np.ndarray) -> np.ndarray:
        return np.concatenate((base, w))

    def evaluate(w: np.ndarray):
        p = expand(w)
        left = apply_profile(bands_l, +1.0, sr, predelay, p)
        right = apply_profile(bands_r, -1.0, sr, predelay, p)
        return (*metrics(left, right, dv["center"], sr), left, right)

    def residual(w: np.ndarray) -> np.ndarray:
        m, traj, _, _ = evaluate(w)
        values = [6.0 * (m.ild_db - ref_m.ild_db),
                  6.0 * (m.ild_low_db - ref_m.ild_low_db),
                  6.0 * (m.ild_mid_db - ref_m.ild_mid_db),
                  6.0 * (m.ild_high_db - ref_m.ild_high_db),
                  6.0 * (m.pan_swap_db - ref_m.pan_swap_db)]
        n = min(len(traj), len(ref_traj))
        keep = np.isfinite(ref_traj[:n])
        delta = traj[:n][keep] - ref_traj[:n][keep]
        values.extend(np.nan_to_num(delta, nan=2.0 * audit.TRAJECTORY_ILD_LIMIT_DB).tolist())
        return np.asarray(values, dtype=np.float64)

    bounds = (np.array([-1.5, -1.5, -1.5, math.log(0.25), math.log(100.0), -math.pi]),
              np.array([+1.5, +1.5, +1.5, math.log(5.0), math.log(5000.0), +math.pi]))
    starts = []
    for rate in (1.0, 1.7, 2.5):
        for phase in (-0.5 * math.pi, 0.0, 0.5 * math.pi):
            starts.append(np.array([0.5, 0.5, 0.5, math.log(rate),
                                    math.log(900.0), phase]))
    best = None
    for start in starts:
        result = least_squares(residual, start, bounds=bounds, max_nfev=180,
                               x_scale="jac", ftol=5e-6, xtol=5e-6, gtol=5e-6)
        score = float(np.sqrt(np.mean(residual(result.x) ** 2)))
        if best is None or score < best[0]:
            best = (score, result.x)
    assert best is not None
    score, w = best
    m, traj, _, _ = evaluate(w)
    failures, actual = audit._compare(m, ref_m, traj, ref_traj)
    profile = list(base_profile) + [float(v) for v in w[:3]] + [
        math.exp(float(w[3])), math.exp(float(w[4])), float(w[5])]
    return {
        "preset": preset, "profile": profile, "fit_rms": score,
        "actual_stereo_reference": actual, "metrics": vars(m),
        "reference": vars(ref_m), "failures": failures,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path("/tmp/duskverb-stereo-jnd"))
    parser.add_argument("--preset", action="append", choices=sorted(audit.PRESETS))
    parser.add_argument("--thorough", action="store_true",
                        help="use multiple starts and a longer authoritative refinement")
    parser.add_argument("--wander-from", type=Path,
                        help="fit damped image wander on top of a prior one-preset fit JSON")
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    selected = args.preset or list(audit.PRESETS)
    if args.wander_from and len(selected) != 1:
        parser.error("--wander-from requires exactly one --preset")
    base_profile = None
    if args.wander_from:
        base_profile = json.loads(args.wander_from.read_text())["results"][0]["profile"]
    results = []
    for i, preset in enumerate(selected, 1):
        print(f"[{i}/{len(selected)}] {preset}", flush=True)
        result = (fit_wander_preset(preset, args.root, base_profile)
                  if base_profile is not None else fit_preset(preset, args.root, args.thorough))
        results.append(result)
        profile = ",".join(f"{v:.6g}" for v in result["profile"])
        print(f"  {profile}  rms={result['fit_rms']:.3f}  "
              f"fail={len(result['failures'])}")
        for failure in result["failures"]:
            print(f"    - {failure}")
    payload = {"results": results, "failure_count": sum(len(r["failures"]) for r in results)}
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(payload, indent=2) + "\n")
    return 1 if payload["failure_count"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
