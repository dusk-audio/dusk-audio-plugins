#!/usr/bin/env python3
"""
Automated Multi-Comp Opto vs LA-2A comparison.

Loads the VST3 via pedalboard, auto-calibrates PR/Gain to match the LA-2A
reference bounce, processes all dry test signals, and runs the comparison.

Calibration method:
  1. Measure LA-2A flat-region gain from its bounce (lowest 5 gain-curve tones)
  2. Binary search MC Gain to match the same flat-region gain
  3. Binary search MC PR to match LA-2A compression at -3 dBFS

Usage:
    python3 auto_compare.py                    # Build + compare
    python3 auto_compare.py --skip-build       # Skip cmake build
    python3 auto_compare.py --pr 33.1 --gain 59.8  # Use fixed settings
"""

import argparse
import subprocess
import sys
import os
import numpy as np
import soundfile as sf

PLUGIN_PATH = os.environ.get(
    "PLUGIN_PATH",
    os.path.expanduser("~/Library/Audio/Plug-Ins/VST3/Multi-Comp.vst3"))
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.abspath(os.path.join(_SCRIPT_DIR, "..", "..", "..", ".."))
BUILD_DIR = os.environ.get(
    "BUILD_DIR",
    os.path.join(_REPO_ROOT, "build"))
DRY_DIR = os.path.join(_SCRIPT_DIR, "test_signals")
CAPTURED_DIR = os.environ.get(
    "CAPTURED_DIR",
    os.path.expanduser("~/Downloads/Plugin-Testing"))
LA2A_PATH = os.path.join(CAPTURED_DIR, "LA2A_1.wav")
MC_OUT_PATH = os.path.join(CAPTURED_DIR, "Multi Comp_1.wav")

SAMPLE_RATE = 48000


def db_to_gain(db):
    return 10 ** (db / 20.0)


def rms_db(signal):
    if len(signal) == 0:
        return -200.0
    return 10 * np.log10(max(np.mean(signal ** 2), 1e-20))


def load_mono(path):
    data, sr = sf.read(path)
    if data.ndim == 2:
        data = data[:, 0]
    return data, sr


def setup_plugin(plugin, pr=0.0, gain=50.0):
    """Set all Opto mode parameters."""
    plugin.mode = "Vintage Opto"
    plugin.oversampling = "2x"
    plugin.analog_noise = True
    plugin.mix = 100.0
    plugin.stereo_link = 100.0
    plugin.bypass = False
    plugin.limit_mode = False
    plugin.sc_listen = False
    plugin.external_sidechain = False
    plugin.auto_makeup = False
    plugin.distortion = "Off"
    plugin.input = 0.0
    plugin.output = 0.0
    plugin.peak_reduction = pr
    plugin.gain = gain


def process_mono(plugin, audio, sr):
    """Process mono float32 audio, return mono output."""
    stereo_in = np.stack([audio, audio]).astype(np.float32)
    stereo_out = plugin(stereo_in, sr)
    return stereo_out[0]


def generate_tone(sr, duration_s, level_db, freq=1000.0):
    t = np.arange(int(sr * duration_s)) / sr
    return (db_to_gain(level_db) * np.sin(2 * np.pi * freq * t)).astype(np.float32)


def measure_steady_state(plugin, pr, gain, level_db=-3.0, sr=SAMPLE_RATE):
    """Reset, configure, process 5s tone, measure last 2s."""
    plugin.reset()
    setup_plugin(plugin, pr=pr, gain=gain)
    tone = generate_tone(sr, 5.0, level_db)
    output = process_mono(plugin, tone, sr)
    n = int(sr * 2.0)
    return rms_db(output[-n:])


def measure_la2a_reference(sr=SAMPLE_RATE):
    """Measure the LA-2A reference bounce to extract calibration targets."""
    la2a, la2a_sr = load_mono(LA2A_PATH)
    if la2a_sr != sr:
        raise ValueError(f"LA-2A sample rate mismatch: expected {sr}, got {la2a_sr}")

    # Load gain curve dry signal to find its position
    test_names = [
        "01_step_response", "02_program_dependency", "03_release_curve",
        "04_attack_transients", "05_frequency_response", "06_thd",
        "07_pink_noise", "08_gain_curve"
    ]
    offset = 0
    gc_start = 0
    for name in test_names:
        path = os.path.join(DRY_DIR, f"{name}.wav")
        data, file_sr = load_mono(path)
        if file_sr != sr:
            raise ValueError(
                f"Sample rate mismatch in {path}: expected {sr}, got {file_sr}")
        if name == "08_gain_curve":
            gc_start = offset
        offset += len(data)

    dry_gc, _ = load_mono(os.path.join(DRY_DIR, "08_gain_curve.wav"))

    # Gain curve: 2s silence then 1s tones from -40 to 0 dB in 2dB steps
    tone_start = gc_start + int(2 * sr)
    levels = list(range(-40, 1, 2))

    gains = []
    for i, level_db in enumerate(levels):
        seg_start = tone_start + i * sr
        meas_start = seg_start + sr // 4
        meas_end = seg_start + 3 * sr // 4
        if meas_end > len(la2a):
            break

        dry_start = int(2 * sr) + i * sr + sr // 4
        dry_end = dry_start + sr // 2
        if dry_end > len(dry_gc):
            break

        dry_level = rms_db(dry_gc[dry_start:dry_end])
        la2a_level = rms_db(la2a[meas_start:meas_end])
        gains.append((level_db, dry_level, la2a_level, la2a_level - dry_level))

    if len(gains) < 5:
        raise ValueError(f"Need at least 5 gain measurements for flat-region estimate, got {len(gains)}")

    # Flat-region gain (lowest 5 tones where no compression)
    flat_gain = np.mean([g[3] for g in gains[:5]])

    # GR at loudest tone (0 dBFS — last entry in the gain curve)
    last = gains[-1]
    gr_at_max = last[3] - flat_gain  # Actual compression (negative = compressed)

    # Output level at loudest tone
    output_at_max = last[2]

    return {
        'flat_gain': flat_gain,
        'gr_at_max': gr_at_max,
        'output_at_max': output_at_max,
        'gains': gains,
    }


def calibrate(plugin, la2a_ref, sr=SAMPLE_RATE):
    """Auto-calibrate MC to match the LA-2A reference bounce."""
    print(f"\n  LA-2A reference:")
    print(f"    Flat-region gain: {la2a_ref['flat_gain']:+.1f} dB")
    print(f"    GR at 0 dBFS: {la2a_ref['gr_at_max']:+.1f} dB")
    print(f"    Output at 0 dBFS: {la2a_ref['output_at_max']:.1f} dBFS")

    # Step 1: Find Gain that matches flat-region gain (use -40 dB tone, no compression)
    # Gain mapping: (gainParam - 50) * 0.8 = dB of makeup
    print(f"\n  Step 1: Matching flat-region gain...")
    gain_low, gain_high = 0.0, 100.0
    for i in range(25):
        gain_mid = (gain_low + gain_high) / 2.0
        out = measure_steady_state(plugin, pr=0.0, gain=gain_mid, level_db=-40.0)
        # At PR=0, no compression, so output = input + gain
        mc_flat_gain = out - rms_db(generate_tone(sr, 1.0, -40.0))
        if abs(mc_flat_gain - la2a_ref['flat_gain']) < 0.1:
            break
        if mc_flat_gain < la2a_ref['flat_gain']:
            gain_low = gain_mid
        else:
            gain_high = gain_mid
    best_gain = (gain_low + gain_high) / 2.0
    out = measure_steady_state(plugin, pr=0.0, gain=best_gain, level_db=-40.0)
    mc_flat = out - rms_db(generate_tone(sr, 1.0, -40.0))
    print(f"    Gain={best_gain:.1f} -> flat gain={mc_flat:+.1f} dB (target={la2a_ref['flat_gain']:+.1f})")

    # Step 2: Find PR that matches compression at 0 dBFS (matching reference level)
    print(f"\n  Step 2: Matching compression at 0 dBFS...")
    target_output = la2a_ref['output_at_max']
    pr_low, pr_high = 0.0, 100.0
    for i in range(25):
        pr_mid = (pr_low + pr_high) / 2.0
        out = measure_steady_state(plugin, pr=pr_mid, gain=best_gain, level_db=0.0)
        if abs(out - target_output) < 0.1:
            break
        if out > target_output:
            pr_low = pr_mid  # Need more compression
        else:
            pr_high = pr_mid  # Need less
    best_pr = (pr_low + pr_high) / 2.0
    out = measure_steady_state(plugin, pr=best_pr, gain=best_gain, level_db=0.0)
    mc_gr = out - rms_db(generate_tone(sr, 1.0, 0.0)) - mc_flat
    print(f"    PR={best_pr:.1f} -> output={out:.1f} dBFS (target={target_output:.1f})")
    print(f"    MC GR at 0dB: {mc_gr:+.1f} dB (LA-2A: {la2a_ref['gr_at_max']:+.1f})")

    print(f"\n  Calibrated: PR={best_pr:.1f}, Gain={best_gain:.1f}")
    return best_pr, best_gain


def process_test_signals(plugin, pr, gain, sr=SAMPLE_RATE):
    """Process all dry test signals as one continuous stream."""
    test_names = [
        "01_step_response", "02_program_dependency", "03_release_curve",
        "04_attack_transients", "05_frequency_response", "06_thd",
        "07_pink_noise", "08_gain_curve"
    ]

    parts = []
    for name in test_names:
        path = os.path.join(DRY_DIR, f"{name}.wav")
        if not os.path.exists(path):
            print(f"  WARNING: Test signal missing, skipping: {path}")
            continue
        data, file_sr = sf.read(path)
        if data.ndim == 2:
            data = data[:, 0]
        if file_sr != sr:
            raise ValueError(f"{name}: sample rate mismatch, expected {sr}, got {file_sr}")
        parts.append((name, data))

    if not parts:
        raise ValueError("No test signal files found")
    all_dry = np.concatenate([p[1] for p in parts]).astype(np.float32)
    print(f"  Total: {len(all_dry)/sr:.1f}s")

    plugin.reset()
    setup_plugin(plugin, pr=pr, gain=gain)
    all_output = process_mono(plugin, all_dry, sr)

    offset = 0
    for name, data in parts:
        seg_out = all_output[offset:offset + len(data)]
        in_rms = rms_db(data)
        out_rms = rms_db(seg_out)
        print(f"    {name}: in={in_rms:.1f}, out={out_rms:.1f}, diff={out_rms-in_rms:+.1f} dB")
        offset += len(data)

    return all_output


def build_plugins():
    print("\n  Building Multi-Comp...")
    for target in ["MultiComp_VST3", "MultiComp_AU"]:
        try:
            result = subprocess.run(
                ["cmake", "--build", ".", "--target", target, "-j8"],
                cwd=BUILD_DIR, capture_output=True, text=True, timeout=120
            )
        except subprocess.TimeoutExpired as e:
            print(f"  {target} TIMED OUT after 120s in {BUILD_DIR}")
            if e.stdout:
                out = e.stdout.decode(errors='replace') if isinstance(e.stdout, bytes) else e.stdout
                print(f"  stdout: {out}")
            if e.stderr:
                err = e.stderr.decode(errors='replace') if isinstance(e.stderr, bytes) else e.stderr
                print(f"  stderr: {err}")
            sys.exit(1)
        if result.returncode != 0:
            print(f"  {target} FAILED! (exit code {result.returncode})")
            if result.stdout:
                print(result.stdout)
            if result.stderr:
                print(result.stderr)
            sys.exit(1)
    print("  VST3 + AU built and installed.")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--pr", type=float, default=None)
    parser.add_argument("--gain", type=float, default=None)
    args = parser.parse_args()

    print("=" * 70)
    print("  Multi-Comp Opto — Automated Comparison")
    print("=" * 70)

    if not args.skip_build:
        build_plugins()

    from pedalboard import load_plugin
    print("\n  Loading VST3...")
    plugin = load_plugin(PLUGIN_PATH)

    if (args.pr is None) != (args.gain is None):
        print("\n  ERROR: --pr and --gain must be provided together")
        sys.exit(1)

    if args.pr is not None and args.gain is not None:
        pr, gain = args.pr, args.gain
        print(f"\n  Using fixed settings: PR={pr:.1f}, Gain={gain:.1f}")
    else:
        if not os.path.exists(LA2A_PATH):
            print(f"\n  ERROR: LA-2A reference file not found: {LA2A_PATH}")
            print("  Provide --pr and --gain to skip calibration, or capture the LA-2A reference first.")
            sys.exit(1)
        la2a_ref = measure_la2a_reference()
        pr, gain = calibrate(plugin, la2a_ref)

    print(f"\n  Processing test signals (PR={pr:.1f}, Gain={gain:.1f})...")
    mc_output = process_test_signals(plugin, pr, gain)

    # Write as stereo (dual-mono) to match LA-2A reference format
    out_dir = os.path.dirname(MC_OUT_PATH)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    mc_stereo = np.column_stack([mc_output, mc_output])
    sf.write(MC_OUT_PATH, mc_stereo, SAMPLE_RATE)
    print(f"  Saved: {MC_OUT_PATH} ({len(mc_output)/SAMPLE_RATE:.1f}s)")

    if os.path.exists(LA2A_PATH):
        print()
        compare_script = os.path.join(_SCRIPT_DIR, "quick_compare.py")
        try:
            subprocess.run(
                [sys.executable, compare_script],
                check=True
            )
        except (subprocess.CalledProcessError, FileNotFoundError) as e:
            print(f"  WARNING: quick_compare failed or not found: {e}")


if __name__ == "__main__":
    main()
