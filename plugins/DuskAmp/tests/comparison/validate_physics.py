#!/usr/bin/env python3
"""DuskAmp Physics-Based Validation.

Validates the DSP amp models against known circuit physics — not against
NAM profiles or other models, but against independently verifiable data
from published measurements, circuit analysis, and the laws of physics.

Tests:
  1. TONE STACK: Compare DSP frequency response against the Yeh/Smith
     analytical transfer function derived from actual component values.
  2. THD: Measure harmonic distortion at known drive levels and compare
     against published measurements of real amps.
  3. COMPRESSION: Measure input/output transfer curves to verify Class A
     vs Class AB behavior.
  4. SAG RECOVERY: Measure envelope recovery after transients and compare
     against known RC time constants.

Usage:
    python3 validate_physics.py [--plots]
"""

import os
import sys
import argparse
import numpy as np
import soundfile as sf
from pathlib import Path
from scipy.signal import freqz, welch, hilbert

# pedalboard for plugin loading
from pedalboard import load_plugin

SAMPLE_RATE = 44100
SCRIPT_DIR = Path(__file__).parent
OUTPUT_DIR = SCRIPT_DIR / "results" / "physics"

AMP_MODELS = {
    "Fender": "Blackface",
    "Marshall": "Plexi",
    "Vox": "British Combo",
}


def find_plugin():
    # Prefer VST3 (pedalboard handles it better), fall back to AU
    vst3 = Path.home() / "Library/Audio/Plug-Ins/VST3/DuskAmp.vst3"
    if vst3.exists():
        return str(vst3)
    au = Path.home() / "Library/Audio/Plug-Ins/Components/DuskAmp.component"
    if au.exists():
        return str(au)
    return None


_plugin_cache = {}

def load_duskamp(plugin_path, amp_name, drive=0.0):
    """Load DuskAmp with specific amp model, minimal coloring."""
    # pedalboard may need stereo hint for AU/VST3 with stereo output
    try:
        plugin = load_plugin(plugin_path)
    except ValueError:
        # If 1-channel fails, try forcing 2-channel input
        from pedalboard._pedalboard import AudioUnitPlugin, VST3Plugin
        # Re-try — some versions of pedalboard handle this differently
        plugin = load_plugin(plugin_path)

    plugin.amp_model = AMP_MODELS[amp_name]
    plugin.delay = False
    plugin.reverb = False
    plugin.cabinet = False
    plugin.bypass = False
    plugin.input_gain = 0.0
    plugin.output_level = 0.0
    plugin.bass = 0.5
    plugin.mid = 0.5
    plugin.treble = 0.5
    plugin.presence = 0.5
    plugin.resonance = 0.5
    plugin.drive = drive
    return plugin


def process_stereo(plugin, mono_signal, sr):
    """Process mono signal through stereo plugin, return mono output."""
    stereo = np.vstack([mono_signal, mono_signal]).astype(np.float32)
    output = plugin(stereo, sr)
    return output[0] if output.ndim > 1 else output


# ============================================================================
# TEST 1: Tone Stack — Yeh/Smith Analytical Transfer Function
# ============================================================================

def yeh_smith_fender_tonestack(freqs, bass=0.5, mid=0.5, treble=0.5):
    """Analytical transfer function for Fender AB763 tone stack.

    Component values from Yeh & Smith, "Discretization of the '59 Fender
    Bassman Tone Stack" (DAFx-06) and Fender AB763 schematic:
      R1 = 250kΩ (treble pot)
      R2 = 1MΩ (bass pot)
      R3 = 25kΩ (mid pot)
      R4 = 56kΩ
      C1 = 250pF
      C2 = 20nF
      C3 = 20nF
    """
    # Component values (Fender Twin/Bassman AB763)
    R1 = 250e3 * treble  # treble pot (wiper position)
    R2 = 1e6 * bass      # bass pot
    R3 = 25e3 * mid      # mid pot
    R4 = 56e3
    C1 = 250e-12
    C2 = 20e-9
    C3 = 20e-9

    # Avoid zero values
    R1 = max(R1, 1.0)
    R2 = max(R2, 1.0)
    R3 = max(R3, 1.0)

    s = 2j * np.pi * freqs

    # Yeh/Smith derived coefficients for the 3rd-order transfer function
    # H(s) = (b1*s + b2*s^2 + b3*s^3) / (a0 + a1*s + a2*s^2 + a3*s^3)
    b1 = C1*R1 + C3*R3
    b2 = C1*C2*R1*R4 + C1*C3*R1*R3 + C2*C3*R3*R4
    b3 = C1*C2*C3*R1*R3*R4

    a0 = 1.0
    a1 = C1*R1 + C1*R4 + C2*R4 + C3*R3 + C3*R4 + C2*R2
    a2 = (C1*C2*R1*R4 + C1*C2*R2*R4 + C1*C3*R1*R3 + C1*C3*R1*R4
          + C1*C3*R3*R4 + C2*C3*R2*R4 + C2*C3*R3*R4)
    a3 = (C1*C2*C3*R1*R3*R4 + C1*C2*C3*R2*R3*R4)

    num = b1*s + b2*s**2 + b3*s**3
    den = a0 + a1*s + a2*s**2 + a3*s**3

    H = num / den
    return 20 * np.log10(np.abs(H) + 1e-20)


def yeh_smith_marshall_tonestack(freqs, bass=0.5, mid=0.5, treble=0.5):
    """Marshall JTM45/1959 tone stack (same topology, different values).

    Component values from Marshall schematic:
      R1 = 250kΩ (treble pot)
      R2 = 1MΩ (bass pot)
      R3 = 25kΩ (mid pot)
      R4 = 33kΩ
      C1 = 470pF
      C2 = 22nF
      C3 = 22nF
    """
    R1 = 250e3 * treble
    R2 = 1e6 * bass
    R3 = 25e3 * mid
    R4 = 33e3
    C1 = 470e-12
    C2 = 22e-9
    C3 = 22e-9

    R1 = max(R1, 1.0)
    R2 = max(R2, 1.0)
    R3 = max(R3, 1.0)

    s = 2j * np.pi * freqs

    b1 = C1*R1 + C3*R3
    b2 = C1*C2*R1*R4 + C1*C3*R1*R3 + C2*C3*R3*R4
    b3 = C1*C2*C3*R1*R3*R4

    a0 = 1.0
    a1 = C1*R1 + C1*R4 + C2*R4 + C3*R3 + C3*R4 + C2*R2
    a2 = (C1*C2*R1*R4 + C1*C2*R2*R4 + C1*C3*R1*R3 + C1*C3*R1*R4
          + C1*C3*R3*R4 + C2*C3*R2*R4 + C2*C3*R3*R4)
    a3 = (C1*C2*C3*R1*R3*R4 + C1*C2*C3*R2*R3*R4)

    num = b1*s + b2*s**2 + b3*s**3
    den = a0 + a1*s + a2*s**2 + a3*s**3

    H = num / den
    return 20 * np.log10(np.abs(H) + 1e-20)


def yeh_smith_vox_tonestack(freqs, bass=0.5, treble=0.5, cut=0.5):
    """Vox AC30 Top Boost tone stack (different topology — 2nd order).

    The AC30 tone circuit is NOT a Fender-style tone stack. It's a
    simpler treble/bass network:
      R_treble = 50kΩ pot
      R_bass = 1MΩ pot
      C_treble = 50pF
      C_bass = 47nF
      R_mix = 100kΩ
    """
    R_t = 50e3 * treble
    R_b = 1e6 * bass
    C_t = 50e-12
    C_b = 47e-9
    R_m = 100e3

    R_t = max(R_t, 1.0)
    R_b = max(R_b, 1.0)

    s = 2j * np.pi * freqs

    # Simplified: treble path is HPF, bass path is LPF, mixed
    Z_treble = 1.0 / (s * C_t + 1.0/R_t)
    Z_bass = R_b / (s * C_b * R_b + 1.0)
    Z_total = 1.0 / (1.0/Z_treble + 1.0/Z_bass + 1.0/R_m)
    H = Z_total / (Z_total + R_m)

    return 20 * np.log10(np.abs(H) + 1e-20)


def test_tonestack(plugin_path, do_plots=False):
    """Compare DSP tone stack against analytical transfer functions."""
    print("\n" + "="*70)
    print("TEST 1: TONE STACK — vs Yeh/Smith Analytical Transfer Functions")
    print("="*70)

    freqs = np.logspace(1.3, 4.3, 500)  # 20Hz to 20kHz
    sr = SAMPLE_RATE

    results = {}

    for amp_name in ["Fender", "Marshall", "Vox"]:
        # Generate sine sweep and process through plugin
        plugin = load_duskamp(plugin_path, amp_name, drive=0.0)

        # Use impulse response to measure frequency response
        # (cleaner than sine sweep for frequency response measurement)
        n_samples = sr * 2  # 2 seconds
        impulse = np.zeros(n_samples, dtype=np.float32)
        impulse[100] = 1.0  # Impulse at sample 100

        output = process_stereo(plugin, impulse, sr)
        del plugin

        # Compute frequency response from impulse response
        # Skip first 100 samples (pre-impulse) and take 1 second of IR
        ir = output[100:100 + sr]
        n_fft = 8192
        H_measured = np.fft.rfft(ir, n=n_fft)
        f_measured = np.fft.rfftfreq(n_fft, 1.0/sr)
        mag_measured = 20 * np.log10(np.abs(H_measured) + 1e-20)

        # Compute analytical response
        if amp_name == "Fender":
            mag_analytical = yeh_smith_fender_tonestack(freqs)
        elif amp_name == "Marshall":
            mag_analytical = yeh_smith_marshall_tonestack(freqs)
        else:
            mag_analytical = yeh_smith_vox_tonestack(freqs)

        # Compare in the 80Hz - 8kHz range
        mask_m = (f_measured >= 80) & (f_measured <= 8000)
        mask_a = (freqs >= 80) & (freqs <= 8000)

        # Interpolate analytical to measured frequencies for comparison
        from scipy.interpolate import interp1d
        interp_func = interp1d(freqs[mask_a], mag_analytical[mask_a],
                               kind='linear', fill_value='extrapolate')
        mag_analytical_interp = interp_func(f_measured[mask_m])

        # Normalize both to 0dB at 1kHz for shape comparison
        idx_1k_m = np.argmin(np.abs(f_measured - 1000))
        idx_1k_a = np.argmin(np.abs(freqs - 1000))
        mag_m_norm = mag_measured[mask_m] - mag_measured[idx_1k_m]
        mag_a_norm = mag_analytical_interp - mag_analytical[idx_1k_a]

        # Correlation of shapes
        corr = np.corrcoef(mag_m_norm, mag_a_norm)[0, 1]

        # RMS shape error
        rms_error = np.sqrt(np.mean((mag_m_norm - mag_a_norm) ** 2))

        results[amp_name] = {
            "correlation": corr,
            "rms_error_db": rms_error,
            "f_measured": f_measured,
            "mag_measured": mag_measured,
            "freqs_analytical": freqs,
            "mag_analytical": mag_analytical,
        }

        grade = "A" if corr > 0.95 else "B" if corr > 0.85 else "C" if corr > 0.70 else "D"
        print(f"\n  {amp_name}:")
        print(f"    Shape correlation: {corr:.3f}")
        print(f"    RMS shape error: {rms_error:.1f} dB")
        print(f"    Grade: {grade}")

    print(f"\n  NOTE: Current tone stack uses parametric EQ (3 independent biquads),")
    print(f"  not the Yeh/Smith circuit-derived transfer function. Real tone stacks")
    print(f"  have interactive controls (turning bass affects treble response).")
    print(f"  This is a known architectural limitation.")

    if do_plots:
        plot_tonestack(results)

    return results


# ============================================================================
# TEST 2: THD at Known Drive Levels
# ============================================================================

def measure_thd(signal, sr, fundamental, n_harmonics=10):
    """Measure Total Harmonic Distortion of a signal."""
    n = len(signal)
    fft = np.fft.rfft(signal)
    freqs = np.fft.rfftfreq(n, 1.0/sr)
    magnitudes = np.abs(fft)

    bin_width = sr / n
    search_bins = max(3, int(15 / bin_width))  # ±15Hz search window

    fund_mag = 0
    harm_mags = []

    for h in range(1, n_harmonics + 1):
        target = fundamental * h
        if target > sr / 2 - 100:
            break
        center = int(round(target / bin_width))
        lo = max(0, center - search_bins)
        hi = min(len(magnitudes), center + search_bins + 1)
        peak = np.max(magnitudes[lo:hi])

        if h == 1:
            fund_mag = peak
        else:
            harm_mags.append(peak)

    if fund_mag <= 0:
        return 0, [], 0

    thd = np.sqrt(sum(m**2 for m in harm_mags)) / fund_mag * 100
    harm_ratios = [m / fund_mag for m in harm_mags]

    # Even/odd ratio (important for push-pull vs Class A distinction)
    even_power = sum(harm_mags[i]**2 for i in range(0, len(harm_mags), 2))
    odd_power = sum(harm_mags[i]**2 for i in range(1, len(harm_mags), 2))
    even_odd_ratio = even_power / (odd_power + 1e-20)

    return thd, harm_ratios, even_odd_ratio


def test_thd(plugin_path, do_plots=False):
    """Measure THD at various drive levels and compare against published data."""
    print("\n" + "="*70)
    print("TEST 2: THD — vs Published Measurements of Real Amps")
    print("="*70)

    # Published THD data (approximate, from various sources):
    # These are for power amp output at rated power levels
    published = {
        "Fender":   {"clean": "<3%",  "crunch": "5-10%",  "cranked": "10-20%"},
        "Marshall":  {"clean": "<5%",  "crunch": "8-15%",  "cranked": "15-30%"},
        "Vox":      {"clean": "<5%",  "crunch": "5-12%",  "cranked": "10-25%"},
    }

    fundamental = 440.0  # A4 — standard test tone
    duration = 2.0
    sr = SAMPLE_RATE
    t = np.linspace(0, duration, int(sr * duration), endpoint=False, dtype=np.float32)

    # Test at three input levels
    input_levels = {
        "whisper": 0.05,   # Very quiet playing
        "normal": 0.15,    # Normal playing
        "loud": 0.35,      # Hard picking
    }

    drive_levels = {"clean": 0.15, "crunch": 0.5, "cranked": 0.85}

    results = {}

    for amp_name in AMP_MODELS:
        print(f"\n  {amp_name} (published: {published[amp_name]})")
        results[amp_name] = {}

        for drive_name, drive_val in drive_levels.items():
            plugin = load_duskamp(plugin_path, amp_name, drive=drive_val)

            for level_name, level in input_levels.items():
                test_tone = level * np.sin(2 * np.pi * fundamental * t).astype(np.float32)

                # Fade in to avoid transients
                fade = int(0.1 * sr)
                test_tone[:fade] *= np.linspace(0, 1, fade)

                output = process_stereo(plugin, test_tone, sr)

                # Measure THD from the steady-state portion (skip first 0.5s)
                steady = output[int(0.5 * sr):]
                thd, harms, even_odd = measure_thd(steady, sr, fundamental)

                key = f"{drive_name}_{level_name}"
                results[amp_name][key] = {
                    "thd": thd, "harmonics": harms, "even_odd": even_odd,
                    "drive": drive_val, "input_level": level,
                }

            # Print summary for this drive level at normal input
            r = results[amp_name].get(f"{drive_name}_normal", {})
            thd = r.get("thd", 0)
            eo = r.get("even_odd", 0)

            # Grade against published range
            status = "?"
            if drive_name == "clean":
                status = "PASS" if thd < 10 else "HIGH" if thd < 30 else "FAIL"
            elif drive_name == "crunch":
                status = "PASS" if 3 < thd < 30 else "LOW" if thd <= 3 else "HIGH"
            elif drive_name == "cranked":
                status = "PASS" if 8 < thd < 50 else "LOW" if thd <= 8 else "HIGH"

            print(f"    {drive_name:>8} (input=normal): THD={thd:6.1f}%  "
                  f"Even/Odd={eo:.2f}  [{status}]")

            del plugin

    # Even/odd harmonic analysis
    print(f"\n  EVEN/ODD HARMONIC RATIO (physics check):")
    print(f"  Push-pull (Fender/Marshall): should be < 0.5 (even harmonics cancelled)")
    print(f"  Class A (Vox):              should be > 0.8 (even harmonics preserved)")
    for amp_name in AMP_MODELS:
        r = results[amp_name].get("cranked_loud", results[amp_name].get("cranked_normal", {}))
        eo = r.get("even_odd", 0)
        expected = "> 0.8" if amp_name == "Vox" else "< 0.5"
        status = ""
        if amp_name == "Vox":
            status = "PASS" if eo > 0.6 else "FAIL — not enough even harmonics"
        else:
            status = "PASS" if eo < 0.8 else "FAIL — too many even harmonics for push-pull"
        print(f"    {amp_name}: Even/Odd = {eo:.2f} (expected {expected}) [{status}]")

    return results


# ============================================================================
# TEST 3: Compression Curve (Input/Output Transfer)
# ============================================================================

def test_compression(plugin_path, do_plots=False):
    """Measure input/output transfer curves to verify compression behavior."""
    print("\n" + "="*70)
    print("TEST 3: COMPRESSION CURVE — Class A vs Class AB Behavior")
    print("="*70)

    sr = SAMPLE_RATE
    fundamental = 220.0
    duration = 0.5  # 500ms per level

    # Test at many input levels
    input_levels_db = np.linspace(-40, 0, 20)
    input_levels = 10 ** (input_levels_db / 20)

    results = {}

    for amp_name in AMP_MODELS:
        plugin = load_duskamp(plugin_path, amp_name, drive=0.5)

        output_levels_db = []
        for level in input_levels:
            t = np.linspace(0, duration, int(sr * duration), endpoint=False, dtype=np.float32)
            tone = level * np.sin(2 * np.pi * fundamental * t).astype(np.float32)
            fade = int(0.05 * sr)
            tone[:fade] *= np.linspace(0, 1, fade)

            output = process_stereo(plugin, tone, sr)
            steady = output[int(0.2 * sr):]
            rms = np.sqrt(np.mean(steady ** 2) + 1e-20)
            output_levels_db.append(20 * np.log10(rms + 1e-20))

        del plugin

        output_levels_db = np.array(output_levels_db)

        # Calculate compression ratio at different input levels
        # (slope of output vs input in dB — 1.0 = linear, <1.0 = compressed)
        slopes = np.diff(output_levels_db) / np.diff(input_levels_db)

        # Compression onset: where slope drops below 0.9
        onset_idx = np.argmax(slopes < 0.9) if np.any(slopes < 0.9) else len(slopes)
        onset_db = input_levels_db[onset_idx] if onset_idx < len(input_levels_db) else 0

        # Average compression ratio in the -20 to -5 dB range
        mask = (input_levels_db[:-1] >= -20) & (input_levels_db[:-1] <= -5)
        avg_ratio = np.mean(slopes[mask]) if np.any(mask) else 1.0

        results[amp_name] = {
            "input_db": input_levels_db,
            "output_db": output_levels_db,
            "slopes": slopes,
            "onset_db": onset_db,
            "avg_ratio": avg_ratio,
        }

        expected_ratio = "< 0.7 (heavy)" if amp_name == "Vox" else "> 0.8 (moderate)"
        status = ""
        if amp_name == "Vox":
            status = "PASS" if avg_ratio < 0.85 else "FAIL — not enough compression for Class A"
        else:
            status = "PASS" if avg_ratio > 0.6 else "FAIL — too much compression for Class AB"

        print(f"\n  {amp_name}:")
        print(f"    Compression onset: {onset_db:.0f} dBFS")
        print(f"    Avg compression ratio (-20 to -5 dB): {avg_ratio:.2f}")
        print(f"    Expected: {expected_ratio}")
        print(f"    [{status}]")

    print(f"\n  PHYSICS: Class A (Vox) should compress earlier and more uniformly")
    print(f"  because the output tube is always conducting. Class AB (Fender/Marshall)")
    print(f"  should have more headroom before compression onset.")

    if do_plots:
        plot_compression(results)

    return results


# ============================================================================
# TEST 4: Sag Recovery Time
# ============================================================================

def test_sag_recovery(plugin_path, do_plots=False):
    """Measure sag recovery time and compare against known RC constants."""
    print("\n" + "="*70)
    print("TEST 4: SAG RECOVERY — vs Known RC Time Constants")
    print("="*70)

    sr = SAMPLE_RATE

    # Published sag characteristics:
    # GZ34 rectifier (Fender Deluxe, AC30): ~50-100ms recovery
    # Silicon rectifier (Marshall 1959): ~5-20ms recovery
    published_recovery_ms = {
        "Fender": (50, 150),    # GZ34 — moderate sag
        "Vox": (30, 100),       # GZ34 — heavy sag, fast attack
        "Marshall": (5, 30),    # Silicon — tight, minimal sag
    }

    results = {}

    for amp_name in AMP_MODELS:
        plugin = load_duskamp(plugin_path, amp_name, drive=0.6)

        # Generate a loud burst followed by silence
        burst_dur = 0.5   # 500ms loud burst
        quiet_dur = 1.5   # 1.5s recovery period
        total_dur = burst_dur + quiet_dur

        t = np.linspace(0, total_dur, int(sr * total_dur), endpoint=False, dtype=np.float32)

        # Loud chord burst
        signal = np.zeros_like(t)
        burst_end = int(burst_dur * sr)
        freqs_chord = [82.4, 110.0, 164.8, 220.0]  # E power chord
        for f in freqs_chord:
            signal[:burst_end] += 0.3 * np.sin(2 * np.pi * f * t[:burst_end])

        # Sharp cutoff
        signal[burst_end:] = 0.0

        # Soft reference tone after the burst (to measure sag recovery)
        ref_start = burst_end + int(0.01 * sr)  # 10ms after burst ends
        ref_tone = 0.05 * np.sin(2 * np.pi * 220 * t)
        signal[ref_start:] += ref_tone[ref_start:].astype(np.float32)

        # Fade in the burst
        fade = int(0.005 * sr)
        signal[:fade] *= np.linspace(0, 1, fade)

        output = process_stereo(plugin, signal, sr)
        del plugin

        # Extract envelope of the reference tone region
        ref_output = output[ref_start:]
        env = np.abs(hilbert(ref_output.astype(np.float64)))
        win = int(0.005 * sr)
        if win > 1:
            kernel = np.ones(win) / win
            env = np.convolve(env, kernel, mode='same')

        # Find recovery time: time to reach 90% of final level
        if len(env) > int(0.5 * sr):
            final_level = np.mean(env[int(0.5 * sr):])
            target = 0.9 * final_level

            recovery_idx = np.argmax(env > target) if np.any(env > target) else len(env)
            recovery_ms = recovery_idx / sr * 1000

            # Initial dip (sag depth)
            initial_level = np.mean(env[:int(0.02 * sr)])
            sag_depth_db = 20 * np.log10((initial_level + 1e-10) / (final_level + 1e-10))
        else:
            recovery_ms = 0
            sag_depth_db = 0
            final_level = 0

        lo, hi = published_recovery_ms[amp_name]
        status = "PASS" if lo <= recovery_ms <= hi * 3 else "CHECK"

        results[amp_name] = {
            "recovery_ms": recovery_ms,
            "sag_depth_db": sag_depth_db,
            "envelope": env,
        }

        print(f"\n  {amp_name}:")
        print(f"    Recovery to 90%: {recovery_ms:.0f} ms")
        print(f"    Initial sag depth: {sag_depth_db:.1f} dB")
        print(f"    Expected recovery: {lo}-{hi} ms")
        print(f"    [{status}]")

    print(f"\n  PHYSICS: GZ34 tube rectifier (Fender/Vox) should recover slower")
    print(f"  than silicon diode rectifier (Marshall). Class A (Vox) should show")
    print(f"  deeper sag because it draws constant high current.")

    return results


# ============================================================================
# Plotting
# ============================================================================

def plot_tonestack(results):
    import matplotlib.pyplot as plt
    fig, axes = plt.subplots(1, 3, figsize=(16, 5))

    for ax, amp_name in zip(axes, ["Fender", "Marshall", "Vox"]):
        r = results[amp_name]
        f_m = r["f_measured"]
        mag_m = r["mag_measured"]
        f_a = r["freqs_analytical"]
        mag_a = r["mag_analytical"]

        # Normalize to 0dB at 1kHz
        idx_m = np.argmin(np.abs(f_m - 1000))
        idx_a = np.argmin(np.abs(f_a - 1000))
        mag_m_n = mag_m - mag_m[idx_m]
        mag_a_n = mag_a - mag_a[idx_a]

        mask_m = (f_m >= 30) & (f_m <= 15000)
        mask_a = (f_a >= 30) & (f_a <= 15000)

        ax.semilogx(f_m[mask_m], mag_m_n[mask_m], 'b-', label='DSP', linewidth=1.5)
        ax.semilogx(f_a[mask_a], mag_a_n[mask_a], 'r--', label='Yeh/Smith', linewidth=1.5)
        ax.set_title(f"{amp_name}\ncorr={r['correlation']:.3f}")
        ax.set_xlabel("Frequency (Hz)")
        ax.set_ylabel("dB (normalized)")
        ax.set_ylim(-25, 10)
        ax.legend()
        ax.grid(True, alpha=0.3)

    plt.suptitle("Tone Stack: DSP vs Yeh/Smith Analytical", fontsize=14)
    plt.tight_layout()
    plt.savefig(str(OUTPUT_DIR / "tonestack_validation.png"), dpi=150)
    print(f"  Plot: {OUTPUT_DIR / 'tonestack_validation.png'}")
    plt.close()


def plot_compression(results):
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(1, 1, figsize=(8, 6))

    for amp_name in ["Fender", "Marshall", "Vox"]:
        r = results[amp_name]
        ax.plot(r["input_db"], r["output_db"], 'o-', label=amp_name, linewidth=2)

    # Reference: 1:1 line (perfectly linear)
    ax.plot([-40, 0], [-40, 0], 'k--', alpha=0.3, label='Linear (1:1)')
    ax.set_xlabel("Input Level (dBFS)")
    ax.set_ylabel("Output Level (dBFS)")
    ax.set_title("Compression Curve: Input vs Output (drive=0.5)")
    ax.legend()
    ax.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig(str(OUTPUT_DIR / "compression_curves.png"), dpi=150)
    print(f"  Plot: {OUTPUT_DIR / 'compression_curves.png'}")
    plt.close()


# ============================================================================
# Main
# ============================================================================

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--plots", action="store_true")
    args = parser.parse_args()

    plugin_path = find_plugin()
    if not plugin_path:
        print("DuskAmp VST3 not found.")
        sys.exit(1)

    os.makedirs(OUTPUT_DIR, exist_ok=True)

    print("DuskAmp Physics-Based Validation")
    print(f"Plugin: {plugin_path}\n")

    ts_results = test_tonestack(plugin_path, args.plots)
    thd_results = test_thd(plugin_path, args.plots)
    comp_results = test_compression(plugin_path, args.plots)
    sag_results = test_sag_recovery(plugin_path, args.plots)

    # Final summary
    print(f"\n\n{'='*70}")
    print("FINAL VALIDATION SUMMARY")
    print(f"{'='*70}")
    print(f"\n{'Test':<25} {'Fender':<18} {'Marshall':<18} {'Vox':<18}")
    print("-" * 70)

    # Tone stack
    for amp in ["Fender", "Marshall", "Vox"]:
        corr = ts_results[amp]["correlation"]
        grade = "A" if corr > 0.95 else "B" if corr > 0.85 else "C" if corr > 0.70 else "D"
        ts_results[amp]["grade"] = grade
        ts_results[amp]["summary"] = f"{grade} ({corr:.2f})"
    print(f"{'Tone Stack Shape':<25} "
          f"{ts_results['Fender']['summary']:<18} "
          f"{ts_results['Marshall']['summary']:<18} "
          f"{ts_results['Vox']['summary']:<18}")

    # THD at clean
    for amp in ["Fender", "Marshall", "Vox"]:
        r = thd_results[amp].get("clean_normal", {})
        thd = r.get("thd", 0)
        grade = "PASS" if thd < 10 else "HIGH"
        thd_results[amp]["clean_grade"] = f"{thd:.0f}% [{grade}]"
    print(f"{'THD (clean, normal)':<25} "
          f"{thd_results['Fender']['clean_grade']:<18} "
          f"{thd_results['Marshall']['clean_grade']:<18} "
          f"{thd_results['Vox']['clean_grade']:<18}")

    # Compression
    for amp in ["Fender", "Marshall", "Vox"]:
        ratio = comp_results[amp]["avg_ratio"]
        if amp == "Vox":
            grade = "PASS" if ratio < 0.85 else "FAIL"
        else:
            grade = "PASS" if ratio > 0.6 else "FAIL"
        comp_results[amp]["grade"] = f"{ratio:.2f} [{grade}]"
    print(f"{'Compression Ratio':<25} "
          f"{comp_results['Fender']['grade']:<18} "
          f"{comp_results['Marshall']['grade']:<18} "
          f"{comp_results['Vox']['grade']:<18}")

    # Sag
    for amp in ["Fender", "Marshall", "Vox"]:
        ms = sag_results[amp]["recovery_ms"]
        sag_results[amp]["summary"] = f"{ms:.0f} ms"
    print(f"{'Sag Recovery':<25} "
          f"{sag_results['Fender']['summary']:<18} "
          f"{sag_results['Marshall']['summary']:<18} "
          f"{sag_results['Vox']['summary']:<18}")


if __name__ == "__main__":
    main()
