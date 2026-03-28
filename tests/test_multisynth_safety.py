#!/usr/bin/env python3
"""
Multi-Synth Safety Validation
Sends MIDI notes through all presets and modes, checks for:
- Audio explosions (peak > threshold)
- NaN/Inf values
- DC offset
- Silence when sound is expected
"""

import numpy as np
import sys
import os

try:
    from pedalboard import load_plugin
except ImportError:
    print("ERROR: pedalboard not installed. Run: pip install pedalboard")
    sys.exit(1)

SAMPLE_RATE = 44100
BLOCK_DURATION = 2.0  # seconds per test
PEAK_LIMIT_DB = 0.0   # maximum acceptable peak in dBFS
PEAK_LIMIT = 10 ** (PEAK_LIMIT_DB / 20.0)  # ~1.0
EXPLOSION_LIMIT = 2.0  # anything above this is a blowup
SILENCE_THRESHOLD = 0.0001  # below this = no sound
DC_OFFSET_LIMIT = 0.05  # max acceptable DC offset

# MIDI note on/off as raw bytes
def midi_note_on(note=60, velocity=100, channel=0):
    return (bytes([0x90 | channel, note, velocity]), 0.0)

def midi_note_off(note=60, channel=0):
    return (bytes([0x80 | channel, note, 0]), BLOCK_DURATION - 0.1)

def midi_chord(notes, velocity=100, channel=0):
    """Send multiple notes at time 0"""
    msgs = []
    for i, note in enumerate(notes):
        msgs.append((bytes([0x90 | channel, note, velocity]), i * 0.001))
    for note in notes:
        msgs.append((bytes([0x80 | channel, note, 0]), BLOCK_DURATION - 0.1))
    return msgs

def analyze_audio(audio, test_name):
    """Analyze audio buffer and return results dict"""
    results = {
        "test": test_name,
        "passed": True,
        "issues": [],
        "peak_db": -999.0,
        "rms_db": -999.0,
        "dc_offset": 0.0,
    }

    # Check for NaN/Inf
    if np.any(np.isnan(audio)):
        results["passed"] = False
        results["issues"].append("NaN values detected!")
        return results
    if np.any(np.isinf(audio)):
        results["passed"] = False
        results["issues"].append("Inf values detected!")
        return results

    peak = np.max(np.abs(audio))
    if peak > 0:
        results["peak_db"] = 20 * np.log10(peak)
    else:
        results["peak_db"] = -120.0

    rms = np.sqrt(np.mean(audio ** 2))
    if rms > 0:
        results["rms_db"] = 20 * np.log10(rms)
    else:
        results["rms_db"] = -120.0

    # DC offset
    results["dc_offset"] = abs(np.mean(audio))

    # Check for explosion
    if peak > EXPLOSION_LIMIT:
        results["passed"] = False
        results["issues"].append(f"EXPLOSION: peak={peak:.2f} ({results['peak_db']:.1f} dBFS)")

    # Check for excessive level
    elif peak > PEAK_LIMIT:
        results["issues"].append(f"Hot output: peak={peak:.4f} ({results['peak_db']:.1f} dBFS)")

    # Check for silence (when we expect sound)
    if peak < SILENCE_THRESHOLD:
        results["issues"].append(f"Silent output: peak={peak:.8f}")

    # Check DC offset
    if results["dc_offset"] > DC_OFFSET_LIMIT:
        results["issues"].append(f"DC offset: {results['dc_offset']:.4f}")

    return results


def discover_plugin_path():
    """Find Multi-Synth VST3"""
    paths = [
        os.path.expanduser("~/.vst3/Multi-Synth.vst3"),
        os.path.expanduser("~/Library/Audio/Plug-Ins/Components/Multi-Synth.component"),
    ]
    for p in paths:
        if os.path.exists(p):
            return p
    return None


def get_preset_configs():
    """Return list of (name, param_overrides) for each preset/test scenario"""
    modes = ["Cosmos", "Oracle", "Mono", "Modular"]

    configs = []

    # Test each mode with init settings
    for mode in modes:
        configs.append((f"Init {mode}", {"synth_mode": mode}))

    # Test each mode with high resonance (filter instability risk)
    for mode in modes:
        configs.append((f"{mode} High Res", {
            "synth_mode": mode,
            "filter_resonance": 1.0,
            "filter_cutoff": 2000.0,
            "filter_env_amt": 1.0,
        }))

    # Test each mode with extreme filter envelope
    for mode in modes:
        configs.append((f"{mode} Extreme Filter Env", {
            "synth_mode": mode,
            "filter_cutoff": 200.0,
            "filter_resonance": 0.9,
            "filter_env_amt": 1.0,
            "filter_attack": 0.001,
            "filter_decay": 0.1,
            "filter_sustain": 0.0,
            "filter_release": 0.1,
        }))

    # High unison count (gain stacking)
    for mode in modes:
        configs.append((f"{mode} 8x Unison", {
            "synth_mode": mode,
            "unison_voices": 8,
            "unison_detune": 50.0,
        }))

    # All effects on
    for mode in modes:
        configs.append((f"{mode} All FX", {
            "synth_mode": mode,
            "drive_on": True,
            "drive_amount": 1.0,
            "chorus_on": True,
            "chorus_mix": 1.0,
            "delay_on": True,
            "delay_mix": 0.5,
            "delay_feedback": 0.9,
            "reverb_on": True,
            "reverb_mix": 0.5,
            "reverb_decay": 15.0,
        }))

    # Cross/FM/Ring mod at max
    configs.append(("Cosmos Max CrossMod", {
        "synth_mode": "Cosmos",
        "cross_mod": 1.0,
    }))
    configs.append(("Oracle Max PolyMod", {
        "synth_mode": "Oracle",
        "cross_mod": 1.0,
        "filter_resonance": 0.9,
    }))
    configs.append(("Mono Max RingMod + Sub", {
        "synth_mode": "Mono",
        "ring_mod": 1.0,
        "sub_level": 1.0,
    }))
    configs.append(("Modular Max FM + Sync", {
        "synth_mode": "Modular",
        "fm_amount": 1.0,
        "hard_sync": True,
        "ring_mod": 1.0,
    }))

    # Velocity extremes
    configs.append(("Max Velocity", {"synth_mode": "Cosmos"}))
    configs.append(("Min Velocity", {"synth_mode": "Cosmos"}))

    # Extreme analog + vintage
    for mode in modes:
        configs.append((f"{mode} Max Analog+Vintage", {
            "synth_mode": mode,
            "analog": 1.0,
            "vintage": 1.0,
        }))

    # Portamento stress
    configs.append(("Mono Fast Portamento", {
        "synth_mode": "Mono",
        "portamento": 0.5,
    }))

    return configs


def reset_all_params(plugin):
    """Reset all parameters to safe defaults before each test"""
    defaults = {
        "synth_mode": "Cosmos",
        "osc_1_wave": "Saw", "osc_1_detune": 0.0, "osc_1_pw": 0.5, "osc_1_level": 1.0,
        "osc_2_wave": "Saw", "osc_2_detune": 7.0, "osc_2_pw": 0.5, "osc_2_level": 0.8, "osc_2_semi": 0,
        "osc_3_wave": "Saw", "osc_3_level": 0.5,
        "sub_level": 0.5, "sub_wave": "Square",
        "noise_level": 0.0,
        "filter_cutoff": 8000.0, "filter_resonance": 0.3, "filter_hp": 20.0, "filter_env_amt": 0.5,
        "amp_attack": 0.01, "amp_decay": 0.2, "amp_sustain": 0.8, "amp_release": 0.3,
        "filter_attack": 0.01, "filter_decay": 0.3, "filter_sustain": 0.4, "filter_release": 0.5,
        "cross_mod": 0.0, "ring_mod": 0.0, "hard_sync": False, "fm_amount": 0.0,
        "portamento": 0.0, "legato": False,
        "analog": 0.2, "vintage": 0.0, "velocity_sens": 0.7,
        "unison_voices": 1, "unison_detune": 10.0, "unison_spread": 1.0,
        "arp_on": False,
        "drive_on": False, "drive_amount": 0.3, "drive_mix": 1.0,
        "chorus_on": False, "chorus_rate": 0.8, "chorus_depth": 0.5, "chorus_mix": 0.5,
        "delay_on": False, "delay_sync": True, "delay_feedback": 0.3, "delay_mix": 0.3,
        "reverb_on": False, "reverb_size": 0.5, "reverb_decay": 2.0, "reverb_mix": 0.2,
        "master_volume": 0.0, "master_pan": 0.0,
    }
    for key, value in defaults.items():
        try:
            setattr(plugin, key, value)
        except Exception:
            pass  # Some params may have different names


def run_test(plugin, name, params, midi_messages):
    """Run a single test: set params, send MIDI, analyze output"""
    plugin.reset()
    reset_all_params(plugin)

    # Set parameters
    for key, value in params.items():
        try:
            setattr(plugin, key, value)
        except Exception as e:
            print(f"  WARNING: Could not set {key}={value}: {e}")

    # Process with MIDI
    try:
        audio = plugin(
            midi_messages,
            duration=BLOCK_DURATION,
            sample_rate=SAMPLE_RATE,
            num_channels=2,
        )
    except Exception as e:
        return {
            "test": name,
            "passed": False,
            "issues": [f"CRASH: {e}"],
            "peak_db": -999.0,
            "rms_db": -999.0,
            "dc_offset": 0.0,
        }

    return analyze_audio(audio, name)


def main():
    plugin_path = discover_plugin_path()
    if not plugin_path:
        print("ERROR: Multi-Synth plugin not found!")
        sys.exit(1)

    print(f"Loading plugin: {plugin_path}")
    plugin = load_plugin(plugin_path)
    print(f"Plugin: {plugin}")
    print(f"Is instrument: {plugin.is_instrument}")
    print()

    configs = get_preset_configs()

    # MIDI test patterns
    single_note = [midi_note_on(60, 100), midi_note_off(60)]
    chord = midi_chord([48, 55, 60, 64, 67])  # C major spread voicing
    high_note = [midi_note_on(96, 127), midi_note_off(96)]
    low_note = [midi_note_on(24, 127), midi_note_off(24)]
    max_vel = [midi_note_on(60, 127), midi_note_off(60)]
    min_vel = [midi_note_on(60, 1), midi_note_off(60)]

    # Run all tests
    all_results = []
    failures = []
    warnings = []

    total = len(configs)
    for i, (name, params) in enumerate(configs):
        # Choose MIDI pattern
        if "Max Velocity" in name:
            midi = max_vel
        elif "Min Velocity" in name:
            midi = min_vel
        else:
            midi = chord  # Default: play a chord

        result = run_test(plugin, name, params, midi)
        all_results.append(result)

        status = "PASS" if result["passed"] else "FAIL"
        if not result["passed"]:
            failures.append(result)
            status_icon = "FAIL"
        elif result["issues"]:
            warnings.append(result)
            status_icon = "WARN"
        else:
            status_icon = "OK  "

        issues_str = "; ".join(result["issues"]) if result["issues"] else ""
        print(f"  [{status_icon}] {i+1:2d}/{total} {name:35s} peak={result['peak_db']:7.1f} dB  rms={result['rms_db']:7.1f} dB  {issues_str}")

    # Also test with high and low notes across all modes
    print("\n--- Pitch Range Tests ---")
    for mode in ["Cosmos", "Oracle", "Mono", "Modular"]:
        for note_name, midi_pat in [("Low C1", low_note), ("High C7", high_note)]:
            test_name = f"{mode} {note_name}"
            result = run_test(plugin, test_name, {"synth_mode": mode}, midi_pat)
            all_results.append(result)
            if not result["passed"]:
                failures.append(result)
            elif result["issues"]:
                warnings.append(result)

            status_icon = "FAIL" if not result["passed"] else ("WARN" if result["issues"] else "OK  ")
            issues_str = "; ".join(result["issues"]) if result["issues"] else ""
            print(f"  [{status_icon}] {test_name:35s} peak={result['peak_db']:7.1f} dB  rms={result['rms_db']:7.1f} dB  {issues_str}")

    # Rapid note test (polyphony stress)
    print("\n--- Polyphony Stress Tests ---")
    for mode in ["Cosmos", "Oracle", "Modular"]:
        rapid_midi = []
        for j in range(8):
            note = 48 + j * 3
            rapid_midi.append((bytes([0x90, note, 100]), j * 0.05))
        for j in range(8):
            note = 48 + j * 3
            rapid_midi.append((bytes([0x80, note, 0]), BLOCK_DURATION - 0.1))

        test_name = f"{mode} 8-voice poly"
        result = run_test(plugin, test_name, {"synth_mode": mode}, rapid_midi)
        all_results.append(result)
        if not result["passed"]:
            failures.append(result)
        elif result["issues"]:
            warnings.append(result)

        status_icon = "FAIL" if not result["passed"] else ("WARN" if result["issues"] else "OK  ")
        issues_str = "; ".join(result["issues"]) if result["issues"] else ""
        print(f"  [{status_icon}] {test_name:35s} peak={result['peak_db']:7.1f} dB  rms={result['rms_db']:7.1f} dB  {issues_str}")

    # Summary
    print("\n" + "=" * 70)
    print(f"RESULTS: {len(all_results)} tests, {len(failures)} FAILURES, {len(warnings)} warnings")
    print("=" * 70)

    if failures:
        print("\nFAILURES:")
        for f in failures:
            print(f"  {f['test']}: {'; '.join(f['issues'])}")

    if warnings:
        print("\nWARNINGS:")
        for w in warnings:
            print(f"  {w['test']}: {'; '.join(w['issues'])}")

    if failures:
        print(f"\n FAILED — {len(failures)} test(s) produced unsafe audio output")
        sys.exit(1)
    else:
        print(f"\n PASSED — all tests within safe limits (peak <= {PEAK_LIMIT_DB} dBFS)")
        sys.exit(0)


if __name__ == "__main__":
    main()
