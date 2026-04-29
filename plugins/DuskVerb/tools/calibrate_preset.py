#!/usr/bin/env python3
"""
DuskVerb per-preset calibration loop.

Iteratively tunes the kBakedRt60 and kBakedEq constants in a per-preset .cpp
file until the rendered plugin output measures within tolerance of the target
(measured-from-IR) values. Closes the gap between "values from analyze_ir.py
on the reference IR" and "what those values produce when fed through the
plugin's actual engine + shaper."

Why: the engine has natural per-band shape and per-band RT60 floors that no
single set of baked values can hit on the first try. Iterating render →
measure → adjust → re-render converges on values that PRODUCE matching
output measurements, even though the values themselves end up offset from
the IR's raw measurement.

Usage:
    python3 calibrate_preset.py \\
        --preset-cpp plugins/DuskVerb/src/dsp/presets/VocalPlatePreset.cpp \\
        --algorithm-index 0 \\
        --target-ir /path/to/reference_ir.wav \\
        [--max-iterations 20] [--tolerance-rt60 0.05] [--tolerance-eq 1.0]

Convergence criteria (per band):
    |measured_rt60 - target_rt60| / target_rt60 < tolerance_rt60   (5 % default)
    |measured_eq   - target_eq|                  < tolerance_eq    (1 dB default)
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

import numpy as np
from scipy import signal as sig
from scipy.io import wavfile


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PLUGIN_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", "..", ".."))
DEFAULT_BUILD_DIR = os.path.join(PLUGIN_ROOT, "build")
SR = 48000

BAND_EDGES_HZ = [63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0]
BAND_LABELS   = ["63", "125", "250", "500", "1k", "2k", "4k", "8k"]
NUM_BANDS     = 8
MID_BAND      = 3   # 500-1000 Hz reference for relative EQ

# Regexes that locate the baked-constant blocks in a per-preset .cpp.
RT60_PATTERN     = re.compile(r"(constexpr\s+float\s+kBakedRt60\s*\[8\]\s*=\s*\{)([^}]*)(\};)")
EQ_PATTERN       = re.compile(r"(constexpr\s+float\s+kBakedEq\s*\[8\]\s*=\s*\{)([^}]*)(\};)")
MOD_RATE_PATTERN = re.compile(r"(constexpr\s+float\s+kBakedModRateHz\s*=\s*)([^;]+)(;)")
MOD_DEPTH_PATTERN= re.compile(r"(constexpr\s+float\s+kBakedModDepth01\s*=\s*)([^;]+)(;)")
GRAIN_PATTERN    = re.compile(r"(constexpr\s+float\s+kBakedGrain01\s*=\s*)([^;]+)(;)")
DENSITY_PATTERN  = re.compile(r"(constexpr\s+float\s+kBakedDensityXps\s*=\s*)([^;]+)(;)")
STEREO_PATTERN   = re.compile(r"(constexpr\s+float\s+kBakedStereoCorr\s*\[6\]\s*=\s*\{)([^}]*)(\};)")
ER_PATTERN       = re.compile(r"(constexpr\s+ERTap\s+kBakedERTaps\s*\[8\]\s*=\s*\{)([^}]*)(\};)", re.DOTALL)


# ---------------------------------------------------------------------------
# Measurement helpers (kept compatible with analyze_ir.py 1:1)
# ---------------------------------------------------------------------------

def load_mono(path, max_samples=None):
    sr, d = wavfile.read(path)
    if d.dtype == np.int16:   d = d.astype(np.float64) / 32768.0
    elif d.dtype == np.int32: d = d.astype(np.float64) / 2147483648.0
    else:                     d = d.astype(np.float64)
    m = d.mean(axis=1) if d.ndim > 1 else d
    if max_samples: m = m[:max_samples]
    return sr, m


def per_band_rt60(mono, sr):
    out = []
    for i in range(NUM_BANDS):
        lo, hi = BAND_EDGES_HZ[i], BAND_EDGES_HZ[i + 1]
        nyq = sr / 2.0
        sos = sig.butter(4, [lo / nyq, min(hi / nyq, 0.999)], btype="band", output="sos")
        f = sig.sosfiltfilt(sos, mono)
        sq = f * f
        edc = np.flip(np.cumsum(np.flip(sq)))
        if edc[0] <= 0: out.append(None); continue
        edc_db = 10.0 * np.log10(edc / edc[0] + 1e-30)
        below_lo = np.where(edc_db <= -5.0)[0]
        below_hi = np.where(edc_db <= -25.0)[0]
        if len(below_lo) == 0 or len(below_hi) == 0: out.append(None); continue
        idx_lo, idx_hi = below_lo[0], below_hi[0]
        if idx_hi <= idx_lo + 4: out.append(None); continue
        t = np.arange(idx_lo, idx_hi) / sr
        slope, _ = np.polyfit(t, edc_db[idx_lo:idx_hi], 1)
        out.append(float(-60.0 / slope) if slope < 0 else None)
    return out


def per_band_eq_db(mono, sr):
    spec = np.abs(np.fft.rfft(mono))
    freqs = np.fft.rfftfreq(len(mono), 1.0 / sr)
    db_per_band = []
    for i in range(NUM_BANDS):
        lo, hi = BAND_EDGES_HZ[i], BAND_EDGES_HZ[i + 1]
        mask = (freqs >= lo) & (freqs < hi)
        if not np.any(mask):
            db_per_band.append(-120.0)
            continue
        power = (spec[mask] ** 2).sum() / mask.sum()
        db_per_band.append(10.0 * np.log10(power + 1e-20))
    mid = db_per_band[MID_BAND]
    return [d - mid for d in db_per_band]


# ---------------------------------------------------------------------------
# Source-file mutation: read / patch the kBakedRt60 + kBakedEq arrays
# ---------------------------------------------------------------------------

def parse_array(match):
    """Extracts 8 floats from a constexpr float kBaked...[8] = { ... }; match."""
    body = match.group(2)
    nums = re.findall(r"-?\d+(?:\.\d+)?(?:e[-+]?\d+)?", body)
    return [float(n) for n in nums[:8]]


def format_array(values, width=6, precision=2):
    """Formats 8 floats as ' 1.74f, 1.96f, ..., 0.73f '."""
    formatted = ", ".join(f"{v:.{precision}f}f" for v in values)
    return " " + formatted + " "


def read_baked_values(cpp_path):
    with open(cpp_path, "r") as f:
        src = f.read()
    rt60_m = RT60_PATTERN.search(src)
    eq_m   = EQ_PATTERN.search(src)
    if not rt60_m or not eq_m:
        raise RuntimeError(f"Could not find kBakedRt60/kBakedEq arrays in {cpp_path}")
    return parse_array(rt60_m), parse_array(eq_m)


def write_baked_values(cpp_path, rt60_values, eq_values,
                       mod_rate=None, mod_depth=None, grain=None, density=None,
                       stereo_corr=None, er_taps=None):
    """Patches whichever baked constants are provided. Each can be None to leave
    the existing value untouched. RT60/EQ are 8-element arrays; mod/grain/density
    are scalars; stereo_corr is a 6-element array; er_taps is a list of 8
    (time_ms, level_dB) tuples."""
    with open(cpp_path, "r") as f:
        src = f.read()
    src = RT60_PATTERN.sub(
        lambda m: m.group(1) + format_array(rt60_values, precision=2) + m.group(3), src)
    src = EQ_PATTERN.sub(
        lambda m: m.group(1) + format_array(eq_values, precision=2) + m.group(3), src)
    if mod_rate is not None:
        src = MOD_RATE_PATTERN.sub(lambda m: m.group(1) + f"{mod_rate:.2f}f" + m.group(3), src)
    if mod_depth is not None:
        src = MOD_DEPTH_PATTERN.sub(lambda m: m.group(1) + f"{mod_depth:.3f}f" + m.group(3), src)
    if grain is not None:
        src = GRAIN_PATTERN.sub(lambda m: m.group(1) + f"{grain:.3f}f" + m.group(3), src)
    if density is not None:
        src = DENSITY_PATTERN.sub(lambda m: m.group(1) + f"{density:.2f}f" + m.group(3), src)
    if stereo_corr is not None:
        src = STEREO_PATTERN.sub(
            lambda m: m.group(1) + format_array(stereo_corr, precision=2) + m.group(3), src)
    if er_taps is not None:
        body = "\n        " + ", ".join(
            f"{{ {t:.1f}f, {lv:.1f}f }}" for t, lv in er_taps) + "\n    "
        src = ER_PATTERN.sub(lambda m: m.group(1) + body + m.group(3), src)
    with open(cpp_path, "w") as f:
        f.write(src)
    # Force a future-mtime so cmake's coarse second-resolution timestamp check
    # always sees the source as newer than any built object.
    now = os.path.getmtime(cpp_path)
    os.utime(cpp_path, (now + 2, now + 2))


# ---------------------------------------------------------------------------
# Build + render
# ---------------------------------------------------------------------------

def rebuild(build_dir):
    raise RuntimeError(
        "DuskVerb_IRTest was removed during the v2 soft reset along with the "
        "rest of the SDIR-matching infrastructure (see plugins/DuskVerb/"
        "CMakeLists.txt). This calibration script renders through that "
        "binary and cannot run until the target is re-added against the "
        "current engine. Until then, tune presets by hand.")
    # Original implementation (kept for reference when DuskVerb_IRTest is
    # restored):
    #   res = subprocess.run(
    #       ["cmake", "--build", build_dir, "--config", "Release",
    #        "--target", "DuskVerb_IRTest", "-j8"],
    #       capture_output=True, text=True)
    #   if res.returncode != 0:
    #       raise RuntimeError(f"Build failed:\n{res.stdout}\n{res.stderr}")


def render(algorithm_index, output_wav, build_dir=DEFAULT_BUILD_DIR):
    irtest_bin = os.path.join(build_dir, "plugins", "DuskVerb", "DuskVerb_IRTest")
    res = subprocess.run(
        [irtest_bin, str(algorithm_index), "0", output_wav],
        capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"Render failed:\n{res.stdout}\n{res.stderr}")


# ---------------------------------------------------------------------------
# Calibration loop
# ---------------------------------------------------------------------------

def fmt_band_row(values, fmt="{:6.2f}"):
    return " ".join(fmt.format(v) if v is not None else "    --" for v in values)


def render_and_measure(algorithm_index, ir_len_samples, sr, build_dir):
    rebuild(build_dir)
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
        wav_path = tmp.name
    try:
        render(algorithm_index, wav_path, build_dir=build_dir)
        rendered_sr, dv = load_mono(wav_path, max_samples=ir_len_samples)
    finally:
        if os.path.exists(wav_path):
            os.unlink(wav_path)
    return per_band_rt60(dv, rendered_sr), per_band_eq_db(dv, rendered_sr)


def calibrate(cpp_path, algorithm_index, target_ir,
              max_iterations=20, tol_rt60=0.05, tol_eq=1.0,
              build_dir=DEFAULT_BUILD_DIR):
    """Three-phase calibration:
      Phase 0 — measure engine's natural per-band shape (one render at flat
                RT60 = 4 s, EQ = 0 dB). Captures the engine's intrinsic
                spectrum + per-band RT60 floors that must be compensated.
      Phase 1 — iterate RT60 to converge per-band measured RT60 to target.
      Phase 2 — analytically solve EQ from target_eq, target_rt60, and the
                engine's natural shape (one shot, no iteration).
      Phase 3 — final measurement to verify.

    Why analytic EQ: per-band measured energy = engine_natural[i] + 20*log10(eq[i])
    + 10*log10(rt60[i]/rt60[mid]). Solving for eq[i] given target_eq[i] is
    closed-form once we know engine_natural[i]. Iteration only needed for
    RT60 because the engine's response to per-band gain inside the loop
    isn't a clean function we can invert.
    """
    sr, ir = load_mono(target_ir)
    target_rt60 = per_band_rt60(ir, sr)
    target_eq   = per_band_eq_db(ir, sr)

    print("=" * 76)
    print(f"Calibrating: {os.path.basename(cpp_path)}")
    print(f"Target IR:   {target_ir}")
    print("=" * 76)
    print(f"Target RT60:  {fmt_band_row(target_rt60)}")
    print(f"Target EQ:    {fmt_band_row(target_eq, '{:6.1f}')}")
    print()

    backup_path = cpp_path + ".calibration_backup"
    shutil.copy2(cpp_path, backup_path)

    ir_len_samples = len(ir)

    def converged_rt60(meas):
        return all(
            (m is not None and t is not None and t > 0 and abs(m - t) / t <= tol_rt60)
            for m, t in zip(meas, target_rt60))

    def converged_eq(meas):
        return all(abs(m - t) <= tol_eq for m, t in zip(meas, target_eq))

    try:
        # ----- Phase 0: characterise engine's natural per-band shape -----
        print("--- PHASE 0: characterising engine natural shape ---")
        original_rt60, original_eq = read_baked_values(cpp_path)
        flat_rt60 = [4.0] * 8
        flat_eq   = [0.0] * 8
        write_baked_values(cpp_path, flat_rt60, flat_eq)
        natural_rt60, natural_eq = render_and_measure(
            algorithm_index, ir_len_samples, sr, build_dir)
        print(f"  natural RT60: {fmt_band_row(natural_rt60)}")
        print(f"  natural EQ:   {fmt_band_row(natural_eq, '{:6.1f}')}")
        # Restore original values before Phase 1
        write_baked_values(cpp_path, original_rt60, original_eq)

        # ----- Phase 1: RT60 only (EQ kept at original values) -----
        # Adaptive learning rate: per-band lr is halved whenever the error
        # sign flips between iterations (= oscillation around the optimum).
        # This lets fast-converging bands finish quickly while slow/oscillating
        # bands settle into a smaller step.
        print("\n--- PHASE 1: RT60 calibration ---")
        per_band_lr  = [0.3] * 8
        prev_errs    = [None] * 8
        rt60_converged = False
        for it in range(1, max_iterations + 1):
            current_rt60, current_eq = read_baked_values(cpp_path)
            measured_rt60, _ = render_and_measure(
                algorithm_index, ir_len_samples, sr, build_dir)
            errs = [(m - t) / t if (m is not None and t and t > 0) else 0.0
                    for m, t in zip(measured_rt60, target_rt60)]
            lr_str = ' '.join(f'{l:.2f}' for l in per_band_lr)
            print(f"  iter {it:2d}: meas={fmt_band_row(measured_rt60)}  "
                  f"err={' '.join(f'{e*100:+5.0f}%' for e in errs)}  lr={lr_str}")
            if converged_rt60(measured_rt60):
                print(f"  Phase 1 converged after {it} iteration(s).")
                rt60_converged = True
                break
            # Adapt per-band learning rate: halve on sign flip OR when |err|
            # grew (monotonic divergence — gain > 1 in this band, lr is too high).
            for i in range(8):
                if prev_errs[i] is not None:
                    if errs[i] * prev_errs[i] < 0 or abs(errs[i]) > abs(prev_errs[i]):
                        per_band_lr[i] = max(0.02, per_band_lr[i] * 0.5)
                prev_errs[i] = errs[i]
            new_rt60 = []
            for i, (cur, meas, tgt) in enumerate(zip(current_rt60, measured_rt60, target_rt60)):
                if meas is None or meas <= 0 or tgt is None:
                    new_rt60.append(cur); continue
                ratio_lr = 1.0 + per_band_lr[i] * (tgt / meas - 1.0)
                new_rt60.append(max(0.05, min(30.0, cur * ratio_lr)))
            write_baked_values(cpp_path, new_rt60, current_eq)

        # ----- Phase 2: analytic EQ seed from natural shape -----
        print("\n--- PHASE 2: analytic EQ seed from natural shape ---")
        current_rt60, _ = read_baked_values(cpp_path)
        rt60_mid = current_rt60[MID_BAND]
        seed_eq = []
        for i in range(8):
            rt60_term = 10.0 * np.log10(max(current_rt60[i] / max(rt60_mid, 1e-3), 1e-6))
            shaper_eq_db = target_eq[i] - natural_eq[i] - rt60_term
            seed_eq.append(max(-30.0, min(12.0, shaper_eq_db)))
        print(f"  seed EQ: {fmt_band_row(seed_eq, '{:6.1f}')}")
        write_baked_values(cpp_path, current_rt60, seed_eq)

        # ----- Phase 3: iterative EQ refinement -----
        # Same adaptive-lr logic as Phase 1 but on EQ instead of RT60.
        print("\n--- PHASE 3: EQ refinement (adaptive lr, RT60 frozen) ---")
        per_band_lr_eq = [0.6] * 8
        prev_eq_errs   = [None] * 8
        for it in range(1, max_iterations + 1):
            current_rt60, current_eq = read_baked_values(cpp_path)
            _, measured_eq = render_and_measure(
                algorithm_index, ir_len_samples, sr, build_dir)
            errs = [m - t for m, t in zip(measured_eq, target_eq)]
            lr_str = ' '.join(f'{l:.2f}' for l in per_band_lr_eq)
            print(f"  iter {it:2d}: meas={fmt_band_row(measured_eq, '{:6.1f}')}  "
                  f"err={' '.join(f'{e:+5.1f}' for e in errs)}  lr={lr_str}")
            if converged_eq(measured_eq):
                print(f"  Phase 3 converged after {it} iteration(s).")
                break
            for i in range(8):
                if prev_eq_errs[i] is not None:
                    if errs[i] * prev_eq_errs[i] < 0 or abs(errs[i]) > abs(prev_eq_errs[i]):
                        per_band_lr_eq[i] = max(0.05, per_band_lr_eq[i] * 0.5)
                prev_eq_errs[i] = errs[i]
            new_eq = [max(-30.0, min(12.0, cur - per_band_lr_eq[i] * errs[i]))
                      for i, cur in enumerate(current_eq)]
            write_baked_values(cpp_path, current_rt60, new_eq)

        # ----- Final measurement -----
        print("\n--- Final measurement ---")
        final_rt60, final_eq = render_and_measure(
            algorithm_index, ir_len_samples, sr, build_dir)
        baked_rt60, baked_eq = read_baked_values(cpp_path)
        rt60_errs = [(m - t) / t * 100 if (m is not None and t and t > 0) else float("nan")
                     for m, t in zip(final_rt60, target_rt60)]
        eq_errs   = [m - t for m, t in zip(final_eq, target_eq)]
        print(f"baked RT60:   {fmt_band_row(baked_rt60)}")
        print(f"meas  RT60:   {fmt_band_row(final_rt60)}")
        print(f"target RT60:  {fmt_band_row(target_rt60)}")
        print(f"err   RT60:   {' '.join(f'{e:+5.0f}%' for e in rt60_errs)}")
        print(f"baked EQ:     {fmt_band_row(baked_eq, '{:6.1f}')}")
        print(f"meas  EQ:     {fmt_band_row(final_eq, '{:6.1f}')}")
        print(f"target EQ:    {fmt_band_row(target_eq, '{:6.1f}')}")
        print(f"err   EQ:     {' '.join(f'{e:+5.1f}' for e in eq_errs)}")
        ok = converged_rt60(final_rt60) and converged_eq(final_eq)
        if ok:
            print("\n*** Converged. ***")
        else:
            print("\n!! Did not fully converge. Best-effort values left in source.")
        return ok
    finally:
        print(f"\n(Original file backed up to: {backup_path})")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--preset-cpp",       required=True)
    parser.add_argument("--algorithm-index",  required=True, type=int)
    parser.add_argument("--target-ir",        required=True)
    parser.add_argument("--max-iterations",   type=int,   default=20)
    parser.add_argument("--tolerance-rt60",   type=float, default=0.05,
                        help="Per-band RT60 fractional error to consider converged (0.05 = 5%%).")
    parser.add_argument("--tolerance-eq",     type=float, default=1.0,
                        help="Per-band EQ absolute dB error to consider converged.")
    parser.add_argument("--build-dir",        default=DEFAULT_BUILD_DIR)
    parser.add_argument("--learning-rate",    type=float, default=0.6,
                        help="0..1 update rate; lower = more iterations but smoother.")
    args = parser.parse_args()

    if not os.path.isfile(args.preset_cpp):
        print(f"!! preset .cpp not found: {args.preset_cpp}", file=sys.stderr)
        return 1
    if not os.path.isfile(args.target_ir):
        print(f"!! target IR not found: {args.target_ir}", file=sys.stderr)
        return 1

    ok = calibrate(args.preset_cpp, args.algorithm_index, args.target_ir,
                   max_iterations=args.max_iterations,
                   tol_rt60=args.tolerance_rt60, tol_eq=args.tolerance_eq,
                   build_dir=args.build_dir)
    return 0 if ok else 2


if __name__ == "__main__":
    sys.exit(main())
