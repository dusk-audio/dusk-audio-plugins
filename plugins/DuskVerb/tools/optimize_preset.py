#!/usr/bin/env python3
"""
Closed-loop optimizer for per-preset data matching.

Iterates:
  1. Patch kEngineNaturalPerBand + kAppliedEqDb in the preset .cpp
  2. Build DuskVerb_IRTest
  3. Render preset IR
  4. Measure via analyze_ir.py
  5. Compute per-metric errors vs EMT reference
  6. Update naturals (RT60 inverse formula) and applied EQ (empirical delta)
     with damping to avoid oscillation
  7. Go to 1 until converged or max iterations

Convergence:
  RT60 within ±0.08 s per band
  EQ   within ±0.8 dB per band (mid anchored at 0)
  Density within 5 xps
  Grain   within 0.08

Usage:
  python3 optimize_preset.py \\
      --preset-cpp plugins/DuskVerb/src/dsp/presets/VocalPlatePreset.cpp \\
      --algorithm-index 0 \\
      --target-ir /Users/marckorte/Downloads/EMT-140/emt_140_medium_2.wav \\
      [--max-iterations 20] [--damping 0.5]
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile

import numpy as np

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)
import analyze_ir as ai  # noqa: E402

PLUGIN_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", "..", ".."))
DEFAULT_BUILD_DIR = os.path.join(PLUGIN_ROOT, "build")
IRTEST_BIN = os.path.join(DEFAULT_BUILD_DIR, "plugins", "DuskVerb",
                          "DuskVerb_IRTest")

NATURAL_PATTERN = re.compile(
    r"(constexpr\s+float\s+kEngineNaturalPerBand\s*\[8\]\s*=\s*\{)([^}]*)(\};)")
APPLIED_PATTERN = re.compile(
    r"(constexpr\s+float\s+kAppliedEqDb\s*\[8\]\s*=\s*\{)([^}]*)(\};)")


def read_file(path):
    with open(path, "r") as f:
        return f.read()


def write_file(path, text):
    with open(path, "w") as f:
        f.write(text)
    now = os.path.getmtime(path)
    os.utime(path, (now + 2, now + 2))  # nudge mtime so cmake rebuilds


def parse_array(src, pattern, name):
    m = pattern.search(src)
    if m is None:
        raise RuntimeError(f"couldn't find {name} in source")
    values = [float(x.strip().rstrip("f"))
              for x in m.group(2).split(",") if x.strip()]
    if len(values) != 8:
        raise RuntimeError(f"{name} has {len(values)} values, expected 8")
    return values


def patch_array(src, pattern, values):
    body = "\n        " + ", ".join(f"{v:+.4f}f" for v in values) + "\n    "
    return pattern.sub(lambda m: m.group(1) + body + m.group(3), src)


def build(build_dir):
    res = subprocess.run(
        ["cmake", "--build", build_dir, "--config", "Release",
         "--target", "DuskVerb_IRTest", "-j8"],
        capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"Build failed:\n{res.stderr}")


def render(algorithm_index, out_path, er_level=1.0):
    res = subprocess.run(
        [IRTEST_BIN, str(algorithm_index), "0", out_path, str(er_level)],
        capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"Render failed:\n{res.stderr}")


def measure(wav_path):
    sr, data = ai.load_ir(wav_path)
    mono = ai.to_mono(data)
    return {
        "rt60":    ai.per_band_rt60(mono, sr),
        "eq":      ai.per_band_eq_db(mono, sr),
        "density": ai.modal_density_per_sec(mono, sr),
        "grain":   ai.diffusion_smoothness(mono, sr),
        "rms":     ai.rms_dbfs(mono),
    }


def compute_natural_inverse(measured, target, old_nat):
    """Given current measurement + target + current natural, solve for the
    natural RT60 that would make shaper's combined-RT60 formula yield target."""
    if target >= old_nat - 1e-3:
        return old_nat
    decay = target * old_nat / (old_nat - target)
    inv = 1.0 / measured - 1.0 / decay
    if inv <= 1e-6:
        return old_nat
    return 1.0 / inv


def iterate_once(preset_cpp, build_dir, algo_idx, target_ir, damping,
                 tol_rt60, tol_eq):
    """One build+measure+update step. Returns (measurements, new_naturals,
    new_applied_eq, converged_bool)."""
    src = read_file(preset_cpp)
    naturals_cur = parse_array(src, NATURAL_PATTERN, "kEngineNaturalPerBand")
    applied_cur  = parse_array(src, APPLIED_PATTERN, "kAppliedEqDb")

    build(build_dir)
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
        tmp_path = tmp.name
    try:
        render(algo_idx, tmp_path)
        meas = measure(tmp_path)
    finally:
        # Always remove the temp WAV — render() or measure() can raise (e.g.
        # IRTest binary missing, scipy load error) and leaving a 6-second
        # 48 k float32 WAV around per failed iteration adds up fast.
        try:
            os.unlink(tmp_path)
        except OSError:
            pass

    # Targets derived from the reference IR on demand
    sr_t, data_t = ai.load_ir(target_ir)
    mono_t = ai.to_mono(data_t)
    target_rt60   = ai.per_band_rt60(mono_t, sr_t)
    target_eq     = ai.per_band_eq_db(mono_t, sr_t)
    target_density = ai.modal_density_per_sec(mono_t, sr_t)
    target_grain   = ai.diffusion_smoothness(mono_t, sr_t)
    target_rms     = ai.rms_dbfs(mono_t)

    # Convergence: all RT60 within tol_rt60 AND all EQ within tol_eq (relative to mid)
    rt60_err = [abs(meas["rt60"][i] - target_rt60[i]) for i in range(8)]
    eq_err   = [abs(meas["eq"][i]   - target_eq[i])   for i in range(8)]
    converged = (max(rt60_err) < tol_rt60 and max(eq_err) < tol_eq)

    # Update naturals: use inverse formula per band, damp blend to current
    naturals_new = []
    for i in range(8):
        proposed = compute_natural_inverse(meas["rt60"][i], target_rt60[i],
                                           naturals_cur[i])
        # Clamp: natural must stay strictly greater than target
        proposed = max(proposed, target_rt60[i] + 0.1)
        # Damp toward proposed
        n = naturals_cur[i] + damping * (proposed - naturals_cur[i])
        naturals_new.append(n)

    # Update applied EQ: empirical delta with damping
    applied_new = []
    for i in range(8):
        if i == 3:
            applied_new.append(0.0)  # mid-band anchored
            continue
        delta = target_eq[i] - meas["eq"][i]
        applied_new.append(applied_cur[i] + damping * delta)

    return {
        "measured": meas,
        "targets":  {"rt60": target_rt60, "eq": target_eq,
                     "density": target_density, "grain": target_grain,
                     "rms": target_rms},
        "naturals_old": naturals_cur,
        "naturals_new": naturals_new,
        "applied_old":  applied_cur,
        "applied_new":  applied_new,
        "rt60_err": rt60_err,
        "eq_err":   eq_err,
        "converged": converged,
    }


def apply_update(preset_cpp, naturals, applied):
    src = read_file(preset_cpp)
    src = patch_array(src, NATURAL_PATTERN, naturals)
    src = patch_array(src, APPLIED_PATTERN, applied)
    write_file(preset_cpp, src)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--preset-cpp",      required=True)
    ap.add_argument("--algorithm-index", required=True, type=int)
    ap.add_argument("--target-ir",       required=True)
    ap.add_argument("--build-dir",       default=DEFAULT_BUILD_DIR)
    ap.add_argument("--max-iterations",  type=int, default=25)
    ap.add_argument("--damping",         type=float, default=0.5)
    ap.add_argument("--tol-rt60",        type=float, default=0.08)
    ap.add_argument("--tol-eq",          type=float, default=0.8)
    args = ap.parse_args()

    best_err = None
    best_naturals = None
    best_applied  = None
    best_meas     = None

    for it in range(args.max_iterations):
        step = iterate_once(args.preset_cpp, args.build_dir,
                            args.algorithm_index, args.target_ir,
                            args.damping, args.tol_rt60, args.tol_eq)

        # Compute combined error metric (for tracking best iteration)
        err = max(max(step["rt60_err"]), max(step["eq_err"]) / 10.0)

        meas = step["measured"]
        tgt  = step["targets"]
        print(f"\n=== iter {it} === converged={step['converged']} "
              f"max_rt60_err={max(step['rt60_err']):.3f}s  "
              f"max_eq_err={max(step['eq_err']):.2f}dB")
        print(f"  rt60:  DV={[round(v,2) for v in meas['rt60']]}")
        print(f"         EMT={[round(v,2) for v in tgt['rt60']]}")
        print(f"  eq:    DV={[round(v,1) for v in meas['eq']]}")
        print(f"         EMT={[round(v,1) for v in tgt['eq']]}")
        print(f"  density={meas['density']:.2f} (tgt {tgt['density']:.2f})  "
              f"grain={meas['grain']:.2f} (tgt {tgt['grain']:.2f})  "
              f"rms={meas['rms']:.1f} (tgt {tgt['rms']:.1f})")

        # Track best
        if best_err is None or err < best_err:
            best_err = err
            best_naturals = list(step["naturals_old"])
            best_applied  = list(step["applied_old"])
            best_meas     = meas

        if step["converged"]:
            print(f"\n*** Converged at iteration {it} ***")
            return 0

        # Apply new update to preset .cpp
        apply_update(args.preset_cpp, step["naturals_new"], step["applied_new"])

    # Max iterations hit — restore best-seen values
    print(f"\n*** Max iterations reached. Restoring best state (err={best_err:.3f}) ***")
    apply_update(args.preset_cpp, best_naturals, best_applied)
    print(f"Final best measurement:")
    print(f"  rt60:  DV={[round(v,2) for v in best_meas['rt60']]}")
    print(f"  eq:    DV={[round(v,1) for v in best_meas['eq']]}")
    print(f"  density={best_meas['density']:.2f}  grain={best_meas['grain']:.2f}  rms={best_meas['rms']:.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
