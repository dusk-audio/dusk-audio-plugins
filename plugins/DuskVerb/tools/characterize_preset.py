#!/usr/bin/env python3
"""
Per-preset offline characterization sweeps. Generates the LUTs / matrices
that let the engine translate "script-units" 1:1 to its internal coefficients.

Subcommands:
  gain      — measure RMS at default settings, print kBakedOutputGainDb
              that compensates to match the reference IR's RMS.
  density   — sweep the preset's density-coefficient via a temporary edit
              to kBakedDensityXps, render at each, measure modal density,
              print (coeff, xps) LUT pairs.
  grain     — sweep kBakedGrain01, render at each, measure diffusion grain,
              print (samples, score) LUT pairs.
  eq-leakage — for each band i in turn, render with kBakedEq[i] = +12 dB
              (others 0), measure per-band EQ. Build 8x8 leakage matrix L,
              invert, print kEqLeakageInverse C++ literal.

Usage:
  python3 characterize_preset.py <subcommand> \\
      --preset-cpp plugins/DuskVerb/src/dsp/presets/VocalPlatePreset.cpp \\
      --algorithm-index 0 \\
      [--target-ir /path/to/ir.wav]   # required for "gain" only

The script reads/edits the preset .cpp directly, rebuilds DuskVerb_IRTest,
renders, measures via analyze_ir.py functions. Restores the original .cpp
contents at the end.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

import numpy as np

# Reuse analyze_ir.py measurement functions
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)
import analyze_ir as ai  # noqa: E402

PLUGIN_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", "..", ".."))
DEFAULT_BUILD_DIR = os.path.join(PLUGIN_ROOT, "build")


def get_irtest_bin(build_dir):
    return os.path.join(build_dir, "plugins", "DuskVerb", "DuskVerb_IRTest")

# ---------------------------------------------------------------------------
# Source-file editing — same patterns as calibrate_preset.py
# ---------------------------------------------------------------------------
EQ_PATTERN       = re.compile(r"(constexpr\s+float\s+kBakedEq\s*\[8\]\s*=\s*\{)([^}]*)(\};)")
GRAIN_PATTERN    = re.compile(r"(constexpr\s+float\s+kBakedGrain01\s*=\s*)([^;]+)(;)")
DENSITY_PATTERN  = re.compile(r"(constexpr\s+float\s+kBakedDensityXps\s*=\s*)([^;]+)(;)")
GAIN_PATTERN     = re.compile(r"(constexpr\s+float\s+kBakedOutputGainDb\s*=\s*)([^;]+)(;)")


def _read(path):
    with open(path, "r") as f:
        return f.read()


def _write(path, txt):
    with open(path, "w") as f:
        f.write(txt)
    now = os.path.getmtime(path)
    os.utime(path, (now + 2, now + 2))  # force cmake rebuild


def patch_eq(src, values):
    body = " " + ", ".join(f"{v:+.2f}f" for v in values) + " "
    return EQ_PATTERN.sub(lambda m: m.group(1) + body + m.group(3), src)


def patch_scalar(src, pattern, value, fmt="{:.3f}f"):
    return pattern.sub(lambda m: m.group(1) + fmt.format(value) + m.group(3), src)


# ---------------------------------------------------------------------------
# Build + render helpers
# ---------------------------------------------------------------------------

def rebuild(build_dir):
    res = subprocess.run(
        ["cmake", "--build", build_dir, "--config", "Release",
         "--target", "DuskVerb_IRTest", "-j8"],
        capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"Build failed:\n{res.stdout}\n{res.stderr}")


def render(algorithm_index, output_wav, build_dir=DEFAULT_BUILD_DIR, er_level=0.0):
    irtest_bin = get_irtest_bin(build_dir)
    res = subprocess.run(
        [irtest_bin, str(algorithm_index), "0", output_wav, str(er_level)],
        capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"Render failed:\n{res.stdout}\n{res.stderr}")


def render_and_load(algorithm_index, build_dir, er_level=0.0):
    rebuild(build_dir)
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
        wav_path = tmp.name
    try:
        render(algorithm_index, wav_path, build_dir=build_dir, er_level=er_level)
        sr, data = ai.load_ir(wav_path)
        return sr, data
    finally:
        if os.path.exists(wav_path):
            os.unlink(wav_path)


# ---------------------------------------------------------------------------
# Subcommands
# ---------------------------------------------------------------------------

def cmd_gain(args):
    """Render at current settings (with kBakedOutputGainDb temporarily set to
    0), measure RMS vs target IR's RMS, compute the compensation, and write
    it back into the preset .cpp. Default behaviour is to APPLY the value;
    pass --dry-run to only print."""
    if not args.target_ir or not os.path.isfile(args.target_ir):
        print("!! gain requires --target-ir", file=sys.stderr)
        return 1
    sr_ir, ir = ai.load_ir(args.target_ir)
    ir_mono = ai.to_mono(ir)
    ir_rms = ai.rms_dbfs(ir_mono)

    print(f"Target IR: {args.target_ir}")
    print(f"  RMS:      {ir_rms:+.1f} dBFS")
    print(f"Rendering plugin (algorithm {args.algorithm_index}) with gain comp = 0 ...")

    src_orig = _read(args.preset_cpp)
    # Temporarily zero kBakedOutputGainDb so we measure the engine's natural level
    src_zeroed = patch_scalar(src_orig, GAIN_PATTERN, 0.0, "{:+.2f}f")
    _write(args.preset_cpp, src_zeroed)
    try:
        sr, data = render_and_load(args.algorithm_index, args.build_dir)
    finally:
        _write(args.preset_cpp, src_orig)  # restore (or overwrite below)

    dv = ai.to_mono(data)[:len(ir_mono)]
    dv_rms = ai.rms_dbfs(dv)
    delta = ir_rms - dv_rms
    print(f"DV (no gain comp):")
    print(f"  RMS:      {dv_rms:+.1f} dBFS")
    print(f"  Delta:    {delta:+.1f} dB → kBakedOutputGainDb")
    print()

    if args.dry_run:
        print(f"--dry-run: not writing. Recommended value: {delta:+.2f}f")
        return 0

    # Write the new value back into the preset .cpp
    src_with_gain = patch_scalar(src_orig, GAIN_PATTERN, delta, "{:+.2f}f")
    _write(args.preset_cpp, src_with_gain)
    print(f"Wrote kBakedOutputGainDb = {delta:+.2f}f to {args.preset_cpp}")
    return 0


def cmd_density(args):
    """Sweep kBakedDensityXps across a range, measure modal density at each,
    output (input_xps, measured_xps) pairs that form the inverse-LUT."""
    sweep_xps = [0.5, 2.0, 5.0, 8.0, 12.0, 16.0, 20.0, 25.0, 30.0, 38.0, 47.0]
    print(f"Sweeping kBakedDensityXps across {len(sweep_xps)} values...")
    src_orig = _read(args.preset_cpp)
    pairs = []
    try:
        for x in sweep_xps:
            src_mod = patch_scalar(src_orig, DENSITY_PATTERN, x, "{:.2f}f")
            _write(args.preset_cpp, src_mod)
            sr, data = render_and_load(args.algorithm_index, args.build_dir)
            mono = ai.to_mono(data)
            measured = ai.modal_density_per_sec(mono, sr)
            print(f"  input {x:6.2f} xps  →  measured {measured:6.2f} xps")
            pairs.append((x, measured))
    finally:
        _write(args.preset_cpp, src_orig)

    print()
    print("--- C++ LUT (paste into preset.cpp) ---")
    print(f"constexpr int   kDensityLUTSize = {len(pairs)};")
    print("constexpr float kDensityCoeffLUT[" + str(len(pairs)) + "] = { "
          + ", ".join(f"{p[0]:.2f}f" for p in pairs) + " };")
    print("constexpr float kDensityXpsLUT["   + str(len(pairs)) + "] = { "
          + ", ".join(f"{p[1]:.2f}f" for p in pairs) + " };")
    return 0


def cmd_grain(args):
    """Same as density, sweep kBakedGrain01 → measure diffusion grain."""
    sweep_grain = [0.0, 0.05, 0.10, 0.15, 0.25, 0.40, 0.55, 0.70, 0.85, 1.00]
    print(f"Sweeping kBakedGrain01 across {len(sweep_grain)} values...")
    src_orig = _read(args.preset_cpp)
    pairs = []
    try:
        for g in sweep_grain:
            src_mod = patch_scalar(src_orig, GRAIN_PATTERN, g, "{:.3f}f")
            _write(args.preset_cpp, src_mod)
            sr, data = render_and_load(args.algorithm_index, args.build_dir)
            mono = ai.to_mono(data)
            measured = ai.diffusion_smoothness(mono, sr)
            print(f"  input {g:5.2f}  →  measured {measured:5.2f}")
            pairs.append((g, measured))
    finally:
        _write(args.preset_cpp, src_orig)

    print()
    print("--- C++ LUT (paste into preset.cpp) ---")
    print(f"constexpr int   kGrainLUTSize = {len(pairs)};")
    print("constexpr float kGrainInputLUT[" + str(len(pairs)) + "] = { "
          + ", ".join(f"{p[0]:.3f}f" for p in pairs) + " };")
    print("constexpr float kGrainScoreLUT[" + str(len(pairs)) + "] = { "
          + ", ".join(f"{p[1]:.3f}f" for p in pairs) + " };")
    return 0


def cmd_eq_leakage(args):
    """Build the linear transfer matrix C where:
        measured_mag[j] = sum_i C[j][i] * eqGain_lin[i]
    (absolute magnitudes — NOT rel-to-mid). At eqGain = {1,...,1},
    C @ {1,...,1} = baseline_mag. Boosting band i from 1 to u gives
        measured_boost_i_mag[j] = baseline_mag[j] + C[j][i] * (u - 1)
    so C[j][i] = (measured_boost_i_mag[j] - baseline_mag[j]) / (u - 1).

    Then solve for applied eqGain_lin such that measured_mag[j] / measured_mag[3]
    matches the preset's kBakedEq[j] rel-to-mid target. Pin eqGain[3] = 1 to
    anchor the mid reference (removes gauge degree of freedom).

    Output: constexpr float kAppliedEqDb[8] that C++ passes directly to
    shaper_.setBandEqDb in place of kBakedEq — no matrix work at runtime.
    """
    # Extract kBakedEq from the preset .cpp so we can solve for the target ratios.
    src_orig = _read(args.preset_cpp)
    eq_match = re.search(
        r"constexpr\s+float\s+kBakedEq\s*\[8\]\s*=\s*\{([^}]*)\};", src_orig)
    if eq_match is None:
        print("!! couldn't locate kBakedEq[8] in preset .cpp", file=sys.stderr)
        return 1
    baked_eq_db = [float(x.strip().rstrip("f"))
                   for x in eq_match.group(1).split(",") if x.strip()]
    if len(baked_eq_db) != 8:
        print(f"!! kBakedEq parse error: got {len(baked_eq_db)} values", file=sys.stderr)
        return 1

    boost_db = 12.0
    u_boost  = 10.0 ** (boost_db / 20.0)   # linear gain applied at the boosted band
    zeros    = [0.0] * 8

    # Render baseline + 8 single-boost sweeps in absolute dB.
    try:
        src_mod = patch_eq(src_orig, zeros)
        _write(args.preset_cpp, src_mod)
        sr, data = render_and_load(args.algorithm_index, args.build_dir)
        baseline_abs = ai.per_band_eq_absolute_db(ai.to_mono(data), sr)
        print("Baseline (all 0 dB) absolute EQ per band:")
        print("  " + " ".join(f"{v:+6.1f}" for v in baseline_abs))

        meas_abs = np.zeros((8, 8), dtype=np.float64)  # meas_abs[i][j] = measured at band j with boost at band i
        for i in range(8):
            target = list(zeros)
            target[i] = boost_db
            src_mod = patch_eq(src_orig, target)
            _write(args.preset_cpp, src_mod)
            sr, data = render_and_load(args.algorithm_index, args.build_dir)
            m = ai.per_band_eq_absolute_db(ai.to_mono(data), sr)
            meas_abs[i] = np.array(m)
            print(f"  boost band {i} +{boost_db:.0f} dB → abs: " +
                  " ".join(f"{v:+6.1f}" for v in m))
    finally:
        _write(args.preset_cpp, src_orig)

    # Build C matrix: C[j][i] = (measured_boost_i_mag[j] - baseline_mag[j]) / (u - 1)
    baseline_mag = np.array([10.0 ** (v / 20.0) for v in baseline_abs])
    C = np.zeros((8, 8), dtype=np.float64)
    for i in range(8):
        meas_mag_boost_i = np.array([10.0 ** (v / 20.0) for v in meas_abs[i]])
        C[:, i] = (meas_mag_boost_i - baseline_mag) / (u_boost - 1.0)

    # Solve for applied eqGain (linear) such that
    #   measured_mag[j] / measured_mag[3] == 10^(baked_eq[j]/20)  (rel-to-mid target)
    # with eqGain[3] = 1 (anchor).
    # That is: (C[j] - r_j * C[3]) @ eqGain = 0 for j != 3, plus eqGain[3] = 1.
    r = np.array([10.0 ** (v / 20.0) for v in baked_eq_db])  # target rel-to-mid in linear
    A = np.zeros((8, 8), dtype=np.float64)
    b = np.zeros(8, dtype=np.float64)
    eq_row = 0
    for j in range(8):
        if j == 3: continue
        A[eq_row] = C[j] - r[j] * C[3]
        b[eq_row] = 0.0
        eq_row += 1
    A[7] = np.zeros(8); A[7, 3] = 1.0
    b[7] = 1.0

    try:
        applied_gain_lin = np.linalg.solve(A, b)
    except np.linalg.LinAlgError as e:
        print(f"!! matrix singular: {e}", file=sys.stderr)
        return 1
    if np.any(applied_gain_lin <= 0):
        print(f"!! solver returned non-positive eqGain at some bands — unstable")
        print(f"   applied_gain_lin = {applied_gain_lin}")
        return 1

    applied_db = 20.0 * np.log10(applied_gain_lin)

    # Sanity: predict measured rel-to-mid after applying.
    predicted_mag = C @ applied_gain_lin
    predicted_rel_mid_db = 20.0 * np.log10(np.maximum(predicted_mag / predicted_mag[3], 1e-10))

    print()
    print("Per-band applied gain that matches target kBakedEq rel-to-mid:")
    print("       band:     63    125    250    500     1k     2k     4k     8k")
    print("     target:  " + "  ".join(f"{v:+6.1f}" for v in baked_eq_db))
    print("   applied:  " + "  ".join(f"{v:+6.1f}" for v in applied_db))
    print("  predicted: " + "  ".join(f"{v:+6.1f}" for v in predicted_rel_mid_db))
    print()
    print("--- Paste into <Preset>Preset.cpp (replaces kBakedEq use in shaper call) ---")
    print("constexpr float kAppliedEqDb[8] = {")
    print("    " + ", ".join(f"{v:+.2f}f" for v in applied_db))
    print("};")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="cmd", required=True)
    for name in ("gain", "density", "grain", "eq-leakage"):
        p = sub.add_parser(name)
        p.add_argument("--preset-cpp",      required=True)
        p.add_argument("--algorithm-index", required=True, type=int)
        p.add_argument("--build-dir",       default=DEFAULT_BUILD_DIR)
        if name == "gain":
            p.add_argument("--target-ir",   required=True)
            p.add_argument("--dry-run", action="store_true",
                           help="Print recommended value without writing it.")
    args = parser.parse_args()

    handler = {
        "gain":       cmd_gain,
        "density":    cmd_density,
        "grain":      cmd_grain,
        "eq-leakage": cmd_eq_leakage,
    }[args.cmd]
    return handler(args)


if __name__ == "__main__":
    sys.exit(main())
