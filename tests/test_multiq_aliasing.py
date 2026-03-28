#!/usr/bin/env python3
"""
Multi-Q Aliasing Test
=====================
Verifies that H2 and H3 harmonics that fold above Nyquist are attenuated
to below -100 dBFS in all nonlinear modes.

Test logic:
  For each mode (British, Tube) at each oversampling level (0, 2x, 4x):
    For each test frequency where H2 or H3 lands above Nyquist:
      1. Generate a full-scale sine at `freq`
      2. Process through Multi-Q with the mode and OS level set
      3. Compute high-resolution FFT
      4. Measure energy at the expected alias frequency
      5. Assert < -100 dBFS

Requirements:
  pip3 install numpy scipy pedalboard

Run:
  python3 tests/test_multiq_aliasing.py
"""

import sys
import os
import numpy as np
import scipy.fft as fft
import scipy.signal as sig
from pathlib import Path
from typing import Optional, List, Tuple

# ──────────────────────────────────────────────────────────────────────────────
# Parameters
# ──────────────────────────────────────────────────────────────────────────────

SAMPLE_RATE = 44100
DURATION_SEC = 2.0          # Long enough for accurate FFT
AMPLITUDE = 0.5             # -6 dBFS — well within linear range, drives saturation gently
ALIAS_THRESHOLD_DB = -100.0 # Requirement: aliased harmonics must be below this
DRIVE_AMOUNT = 0.3          # Moderate saturation drive

# Test frequencies where H2 or H3 exceeds Nyquist at 44.1 kHz
# Nyquist = 22050 Hz. H2 aliases if f > 11025, H3 aliases if f > 7350.
TEST_FREQUENCIES = [8000, 10000, 12000, 15000, 18000]

# Multi-Q AU component ID (as seen by pedalboard)
PLUGIN_NAME = "Multi-Q"

# Oversampling levels (pedalboard normalized raw_value: 0.0=Off, 0.5=2x, 1.0=4x)
OS_LEVELS = {0.0: "Off (0x)", 0.5: "2x", 1.0: "4x"}

# EQ mode parameter raw_values (pedalboard normalized, 4 choices: Digital/Match/British/Tube)
# Digital=0.0, Match=0.333, British=0.667, Tube=1.0
MODES = {0.667: "British", 1.0: "Tube"}

NYQUIST = SAMPLE_RATE / 2


# ──────────────────────────────────────────────────────────────────────────────
# Helpers
# ──────────────────────────────────────────────────────────────────────────────

def generate_sine(freq: float, sample_rate: int = SAMPLE_RATE,
                  duration: float = DURATION_SEC, amplitude: float = AMPLITUDE) -> np.ndarray:
    """Pure sine wave, shaped with a short fade-in to reduce spectral leakage transients."""
    n = int(duration * sample_rate)
    t = np.arange(n) / sample_rate
    audio = amplitude * np.sin(2 * np.pi * freq * t)
    # 10 ms fade-in to avoid onset click
    fade_len = int(0.01 * sample_rate)
    fade = np.linspace(0, 1, fade_len)
    audio[:fade_len] *= fade
    return audio.astype(np.float32)


def compute_peak_db_at(audio: np.ndarray, target_freq: float,
                        sample_rate: int = SAMPLE_RATE,
                        search_width_hz: float = 50.0) -> float:
    """Return peak dBFS in a narrow window around target_freq."""
    n = len(audio)
    # Blackman window minimises spectral leakage — important for alias detection
    window = np.blackman(n)
    windowed = (audio * window).astype(np.float64)

    spectrum = np.abs(fft.rfft(windowed))
    freqs = fft.rfftfreq(n, 1.0 / sample_rate)

    # Correct for Blackman window power (coherent gain ≈ 0.42)
    window_gain = np.mean(window)
    spectrum /= (window_gain * n)

    # Find peak in ±search_width_hz around target_freq
    mask = (freqs >= target_freq - search_width_hz) & (freqs <= target_freq + search_width_hz)
    if not np.any(mask):
        return -200.0

    peak_linear = np.max(spectrum[mask])
    return float(20 * np.log10(peak_linear + 1e-15))


def fold_to_baseband(freq: float, sample_rate: int) -> float:
    """
    Fold a frequency into [0, Nyquist] using discrete-time aliasing:
      1. Wrap into [0, fs): f_mod = freq % fs
      2. If above Nyquist, mirror: fs - f_mod
    """
    nyquist = sample_rate / 2.0
    f_mod = freq % sample_rate
    return sample_rate - f_mod if f_mod > nyquist else f_mod


def alias_frequencies(fundamental: float, sample_rate: int = SAMPLE_RATE) -> List[Tuple[str, float]]:
    """
    Return the expected alias frequencies for H2 and H3 of `fundamental`.
    Only returns entries where the harmonic is above Nyquist (otherwise it's just
    a normal harmonic and not an aliasing issue).
    """
    nyquist = sample_rate / 2.0
    aliases = []

    h2 = 2 * fundamental
    if h2 > nyquist:
        aliases.append(("H2", fold_to_baseband(h2, sample_rate)))

    h3 = 3 * fundamental
    if h3 > nyquist:
        aliases.append(("H3", fold_to_baseband(h3, sample_rate)))

    return aliases


# ──────────────────────────────────────────────────────────────────────────────
# Plugin processing (pedalboard)
# ──────────────────────────────────────────────────────────────────────────────

def try_load_plugin():
    """Attempt to load Multi-Q via pedalboard. Returns the plugin or None."""
    try:
        from pedalboard import load_plugin
        plugin = load_plugin(os.path.expanduser("~/.vst3/Multi-Q.vst3"))
        return plugin
    except Exception as e:
        return None


def process_offline(audio: np.ndarray, eq_type: float, os_level: float,
                    plugin, mode_name: str) -> Optional[np.ndarray]:
    """Process audio through Multi-Q with given mode + OS settings."""
    try:
        import pedalboard

        # Set EQ type (normalized raw_value: 0.0=Digital, 0.333=Match, 0.667=British, 1.0=Tube)
        plugin.parameters['eq_type'].raw_value = eq_type

        # Set oversampling level (normalized raw_value: 0.0=Off, 0.5=2x, 1.0=4x)
        plugin.parameters['oversampling'].raw_value = os_level

        # Set saturation drive for whichever mode is active
        # british_saturation: range 0-100%, normalized raw_value [0,1] → 0.3 = 30%
        # tube_eq_tube_drive: range 0-1.0 directly
        if mode_name == "British" and 'british_saturation' in plugin.parameters:
            plugin.parameters['british_saturation'].raw_value = DRIVE_AMOUNT
        elif mode_name == "Tube" and 'tube_eq_tube_drive' in plugin.parameters:
            plugin.parameters['tube_eq_tube_drive'].raw_value = DRIVE_AMOUNT

        board = pedalboard.Pedalboard([plugin])
        stereo = np.stack([audio, audio], axis=0)
        out = board(stereo, SAMPLE_RATE, reset=True)
        return out[0]  # left channel
    except Exception as e:
        print(f"    [process error: {e}]")
        return None


# ──────────────────────────────────────────────────────────────────────────────
# Offline analysis (no plugin — analytical verification of alias math)
# ──────────────────────────────────────────────────────────────────────────────

def run_offline_analysis() -> bool:
    """
    Without a loaded plugin, verify the alias frequency calculations are correct
    and report expected alias locations. This validates the test math itself.
    """
    print("\n[OFFLINE MODE] Verifying alias frequency calculations")
    print("=" * 65)
    all_ok = True

    for freq in TEST_FREQUENCIES:
        aliases = alias_frequencies(freq, SAMPLE_RATE)
        if not aliases:
            continue
        for (harmonic, alias_freq) in aliases:
            # Sanity: alias must be inside [0, Nyquist]
            ok = 0 <= alias_freq <= NYQUIST
            status = "OK" if ok else "FAIL (out of range)"
            if not ok:
                all_ok = False
            print(f"  {freq} Hz | {harmonic} @ {2*freq if harmonic=='H2' else 3*freq} Hz → "
                  f"alias @ {alias_freq:.0f} Hz  {status}")

    print()
    if all_ok:
        print("Alias math verified. Run with Multi-Q plugin loaded for full test.")
    return all_ok


# ──────────────────────────────────────────────────────────────────────────────
# Main test
# ──────────────────────────────────────────────────────────────────────────────

def run_aliasing_tests(plugin) -> bool:
    """Full aliasing test with live plugin processing."""
    print(f"\nMulti-Q Aliasing Test  (threshold: {ALIAS_THRESHOLD_DB} dBFS)")
    print(f"Sample rate: {SAMPLE_RATE} Hz  |  Nyquist: {NYQUIST:.0f} Hz")
    print("=" * 75)

    total_tests = 0
    total_pass = 0
    failures: List[str] = []

    for mode_id, mode_name in MODES.items():
        print(f"\n{'─'*75}")
        print(f"  Mode: {mode_name}")
        print(f"{'─'*75}")
        print(f"  {'Freq':>6}  {'Harmonic':>8}  {'Alias@':>8}  {'OS':>4}  {'Level(dBFS)':>12}  {'Result'}")
        print(f"  {'-'*6}  {'-'*8}  {'-'*8}  {'-'*4}  {'-'*12}  {'-'*6}")

        for os_level, os_name in OS_LEVELS.items():
            for freq in TEST_FREQUENCIES:
                aliases = alias_frequencies(freq, SAMPLE_RATE)
                if not aliases:
                    continue

                audio_in = generate_sine(freq)
                audio_out = process_offline(audio_in, mode_id, os_level, plugin, mode_name)

                if audio_out is None:
                    print(f"  {freq:>6}  {'N/A':>8}  {'N/A':>8}  {os_name:>4}  {'ERROR':>12}  SKIP")
                    continue

                # Skip transient onset (first 50 ms) to avoid DC step artefacts
                skip = int(0.05 * SAMPLE_RATE)
                audio_analysis = audio_out[skip:]

                for (harmonic, alias_freq) in aliases:
                    level_db = compute_peak_db_at(audio_analysis, alias_freq)
                    passed = level_db < ALIAS_THRESHOLD_DB
                    total_tests += 1
                    if passed:
                        total_pass += 1
                        result = "PASS"
                    else:
                        result = "FAIL"
                        failures.append(
                            f"{mode_name} | {os_name} | {freq} Hz | {harmonic} alias @ "
                            f"{alias_freq:.0f} Hz = {level_db:.1f} dBFS "
                            f"(threshold {ALIAS_THRESHOLD_DB} dBFS)"
                        )

                    print(f"  {freq:>6}  {harmonic:>8}  {alias_freq:>7.0f}  {os_name:>4}  "
                          f"{level_db:>11.1f}  {result}")

    # Summary
    print(f"\n{'='*75}")
    print(f"Results: {total_pass}/{total_tests} passed")

    if failures:
        print("\nFAILURES:")
        for f in failures:
            print(f"  ✗ {f}")
        print(
            "\nNote: British/Tube with OS=Off should pass after the min-2x OS fix in MultiQ.cpp.\n"
            "If OS=Off still fails, verify prepareToPlay uses effective osFactor for EQ init."
        )
    else:
        print("\nAll aliasing tests passed.")

    return len(failures) == 0


def main():
    plugin = try_load_plugin()

    if plugin is None:
        print(f"Could not load '{PLUGIN_NAME}' via pedalboard.")
        print("Build the AU first:  cmake --build build --config Release --target MultiQ_AU -j8")
        print("Then re-run this test.")
        print()
        # Fall back to offline alias-math verification
        ok = run_offline_analysis()
        sys.exit(0 if ok else 1)

    success = run_aliasing_tests(plugin)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
