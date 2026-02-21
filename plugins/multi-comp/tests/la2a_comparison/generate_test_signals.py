#!/usr/bin/env python3
"""
Generate test signals for LA-2A vs Multi-Comp Opto comparison.

Generates WAV files in ./test_signals/ for processing through both plugins
in Logic Pro. Each test targets a specific compressor characteristic.

Usage:
    python3 generate_test_signals.py [--sr 48000] [--output-dir ./test_signals]
"""

import argparse
import os
import sys
import numpy as np
import soundfile as sf


def db_to_lin(db):
    return 10 ** (db / 20.0)


def generate_silence(duration, sr):
    return np.zeros(int(duration * sr))


def generate_sine(freq, duration, sr, amplitude_db=-20.0):
    t = np.arange(int(duration * sr)) / sr
    return db_to_lin(amplitude_db) * np.sin(2 * np.pi * freq * t)


def generate_pink_noise(duration, sr, amplitude_db=-20.0):
    """Pink noise via Voss-McCartney algorithm."""
    n_samples = int(duration * sr)
    n_rows = 16
    out = np.zeros(n_samples)
    rows = np.zeros(n_rows)
    running_sum = 0.0
    rng = np.random.default_rng(42)

    for i in range(n_samples):
        # Find lowest set bit
        idx = 0
        n = i
        while n > 0 and (n & 1) == 0 and idx < n_rows - 1:
            n >>= 1
            idx += 1
        running_sum -= rows[idx]
        rows[idx] = rng.standard_normal()
        running_sum += rows[idx]
        out[i] = running_sum + rng.standard_normal()

    # Normalize to target level
    out = out / (np.max(np.abs(out)) + 1e-10)
    out *= db_to_lin(amplitude_db)
    return out


def test_step_response(sr):
    """
    Test 1: Step response — reveals attack and release shape.

    2s silence → 2s at -20dB → step to -6dB for 2s → back to -20dB for 4s → silence

    Look for: attack time (10-90%), release time, overshoot
    """
    silence = generate_silence(2.0, sr)
    low = generate_sine(1000, 2.0, sr, amplitude_db=-20.0)
    high = generate_sine(1000, 2.0, sr, amplitude_db=-6.0)
    tail = generate_sine(1000, 4.0, sr, amplitude_db=-20.0)
    tail_silence = generate_silence(1.0, sr)
    return np.concatenate([silence, low, high, tail, tail_silence])


def test_release_short_vs_long(sr):
    """
    Test 2: Program dependency — short vs long burst.

    Shows how compression history affects release time.
    Section A: 100ms burst at -3dB → 3s release observation
    Section B: 5s sustained at -3dB → 3s release observation

    The LA-2A's release should be noticeably slower after the long burst
    due to phosphor charge accumulation.
    """
    silence_gap = generate_silence(2.0, sr)
    level = -3.0

    # Short burst (100ms)
    short_burst = generate_sine(1000, 0.1, sr, amplitude_db=level)
    short_release = generate_sine(1000, 3.0, sr, amplitude_db=-30.0)

    # Long sustained (5s)
    long_burst = generate_sine(1000, 5.0, sr, amplitude_db=level)
    long_release = generate_sine(1000, 3.0, sr, amplitude_db=-30.0)

    return np.concatenate([
        silence_gap, short_burst, short_release,
        silence_gap, long_burst, long_release,
        generate_silence(1.0, sr)
    ])


def test_release_curve(sr):
    """
    Test 3: Detailed release curve at multiple drive levels.

    3 bursts at increasing levels (-10, -6, -3 dB) each held for 1s,
    followed by 4s of quiet signal for release observation.

    Shows: two-stage release character, level-dependent release times
    """
    parts = [generate_silence(2.0, sr)]

    for drive_db in [-10, -6, -3]:
        burst = generate_sine(1000, 1.0, sr, amplitude_db=drive_db)
        observe = generate_sine(1000, 4.0, sr, amplitude_db=-40.0)
        gap = generate_silence(1.5, sr)
        parts.extend([burst, observe, gap])

    return np.concatenate(parts)


def test_attack_transients(sr):
    """
    Test 4: Transient attack timing.

    Repeating pattern: silence → sharp transient at -3dB for 50ms → silence
    Repeat 5 times with different gap lengths.

    Shows: attack speed, transient preservation
    """
    parts = [generate_silence(2.0, sr)]

    for gap_sec in [0.5, 1.0, 2.0, 3.0, 5.0]:
        transient = generate_sine(1000, 0.05, sr, amplitude_db=-3.0)
        gap = generate_silence(gap_sec, sr)
        parts.extend([transient, gap])

    parts.append(generate_silence(1.0, sr))
    return np.concatenate(parts)


def test_frequency_response(sr):
    """
    Test 5: Frequency-dependent compression.

    Sustained tones at different frequencies, all at same level (-6dB).
    100Hz, 300Hz, 1kHz, 3kHz, 10kHz — each for 2s with 1s gap.

    The LA-2A in Compress mode has HF-emphasized sidechain, so high
    frequencies should compress slightly more. Limit mode should be flat.
    """
    parts = [generate_silence(2.0, sr)]

    for freq in [100, 300, 1000, 3000, 10000]:
        if freq >= sr / 2:
            continue  # Skip frequencies at or above Nyquist
        tone = generate_sine(freq, 2.0, sr, amplitude_db=-6.0)
        gap = generate_silence(1.0, sr)
        parts.extend([tone, gap])

    return np.concatenate(parts)

def test_thd(sr):
    """
    Test 6: Total Harmonic Distortion measurement.

    Low-level 1kHz sine at -20dB for 10s (below threshold — just hardware path).
    Then driven 1kHz sine at -3dB for 10s (with compression engaged).

    Compare harmonic content between the two plugins.
    """
    silence = generate_silence(2.0, sr)
    low_sine = generate_sine(1000, 10.0, sr, amplitude_db=-20.0)
    gap = generate_silence(2.0, sr)
    hot_sine = generate_sine(1000, 10.0, sr, amplitude_db=-3.0)
    tail = generate_silence(1.0, sr)
    return np.concatenate([silence, low_sine, gap, hot_sine, tail])


def test_pink_noise_program(sr):
    """
    Test 7: Pink noise program material.

    Closest to real-world use. Pink noise at -12dB for 20s.
    Good for overall character comparison, RMS level tracking.
    """
    silence = generate_silence(2.0, sr)
    noise = generate_pink_noise(20.0, sr, amplitude_db=-12.0)
    tail = generate_silence(2.0, sr)
    return np.concatenate([silence, noise, tail])


def test_gain_reduction_curve(sr):
    """
    Test 8: Static gain reduction curve (input/output mapping).

    Stepped sine tones from -40dB to 0dB in 2dB steps, each held for 1s.
    This maps the compression curve (ratio, knee).
    """
    parts = [generate_silence(2.0, sr)]

    for level_db in range(-40, 1, 2):
        tone = generate_sine(1000, 1.0, sr, amplitude_db=level_db)
        parts.append(tone)

    parts.append(generate_silence(1.0, sr))
    return np.concatenate(parts)


TESTS = {
    "01_step_response": (test_step_response,
        "Step response: -20dB → -6dB → -20dB. Shows attack/release shape."),
    "02_program_dependency": (test_release_short_vs_long,
        "Short vs long burst. Shows program-dependent release (phosphor memory)."),
    "03_release_curve": (test_release_curve,
        "Release at 3 drive levels. Shows two-stage release and level dependency."),
    "04_attack_transients": (test_attack_transients,
        "Repeated transients. Shows attack speed and transient preservation."),
    "05_frequency_response": (test_frequency_response,
        "Tones at 5 frequencies. Shows sidechain frequency emphasis (Compress vs Limit)."),
    "06_thd": (test_thd,
        "THD measurement. Low-level + driven sine. Shows harmonic distortion character."),
    "07_pink_noise": (test_pink_noise_program,
        "Pink noise program material. Overall character and RMS tracking."),
    "08_gain_curve": (test_gain_reduction_curve,
        "Stepped levels -40 to 0dB. Maps the compression curve (ratio/knee)."),
}


def main():
    parser = argparse.ArgumentParser(description="Generate LA-2A comparison test signals")
    parser.add_argument("--sr", type=int, default=48000, help="Sample rate (default: 48000)")
    parser.add_argument("--output-dir", default="./test_signals", help="Output directory")
    args = parser.parse_args()

    if args.sr <= 0:
        print(f"ERROR: Invalid sample rate: {args.sr}")
        sys.exit(1)

    if args.sr < 22050:
        print(f"ERROR: Sample rate {args.sr} too low for test signals (minimum: 22050)")
        sys.exit(1)

    os.makedirs(args.output_dir, exist_ok=True)

    print(f"Generating test signals at {args.sr} Hz → {args.output_dir}/\n")

    for name, (gen_func, description) in TESTS.items():
        signal = gen_func(args.sr)

        # Stereo (dual mono) for plugin compatibility
        stereo = np.column_stack([signal, signal])

        path = os.path.join(args.output_dir, f"{name}.wav")
        sf.write(path, stereo, args.sr, subtype="FLOAT")

        duration = len(signal) / args.sr
        print(f"  {name}.wav  ({duration:.1f}s)")
        print(f"    → {description}")

    print(f"\n✓ Generated {len(TESTS)} test signals")
    print(f"\n{'='*70}")
    print("LOGIC PRO WORKFLOW:")
    print(f"{'='*70}")
    print(f"""
1. Create a new Logic Pro project at {args.sr / 1000:.4g}kHz / 32-bit float
2. Import ALL test signal WAVs onto a single track

3. For each plugin (Multi-Comp Opto, then UA LA-2A):
   a. Insert the plugin on the track
   b. Set these matching settings:

      UA LA-2A:
        - Peak Reduction: ~40 (moderate compression, ~6-8dB GR on peaks)
        - Gain: Unity (adjust so uncompressed signal = same level)
        - Mode: COMPRESS
        - Meter: GR

      Multi-Comp (Opto mode):
        - Peak Reduction: match to get same ~6-8dB GR on the step response
        - Gain: 50% (unity)
        - Mode: Compress (limit OFF)

   c. Bounce in place (or File → Bounce → offline)
   d. Export as WAV (32-bit float, {args.sr}Hz)
   e. Name the output files:
        multicomp_01_step_response.wav
        multicomp_02_program_dependency.wav
        ...
        la2a_01_step_response.wav
        la2a_02_program_dependency.wav
        ...

4. Place all output WAVs in ./captured/

5. Run: python3 analyze_comparison.py
""")


if __name__ == "__main__":
    main()
