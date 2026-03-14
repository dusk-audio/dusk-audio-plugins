#!/usr/bin/env python3
"""
Extract ER settings from VintageVerb factory presets and generate DuskVerb C++ factory presets.

For each qualifying VV preset:
1. Loads VintageVerb via pedalboard
2. Sends impulse through VV, captures stereo IR
3. Measures ER characteristics (RMS energy, time centroid) in 0-80ms window
4. Translates all VV params → DV equivalents
5. Outputs complete C++ FactoryPreset code for FactoryPresets.h

Usage:
    python3 extract_er_settings.py              # Run all 53 presets
    python3 extract_er_settings.py --dry-run     # Use formula-based ER (no plugin needed)
"""

import argparse
import json
import math
import os
import sys
import numpy as np

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------
SAMPLE_RATE = 48000
IR_DURATION = 1.0  # seconds of IR to capture
FLUSH_DURATION = 2.0  # seconds of silence before impulse

ER_WINDOW_MS = 80    # Early reflections window (ms)
LATE_START_MS = 80    # Late reverb starts here (ms)
LATE_END_MS = 500     # Late reverb analysis window end (ms)

# VV mode → DV algorithm mapping
VV_MODE_TO_DV = {
    0.0417: ("Hall", 1),
    0.0833: ("Plate", 0),
    0.1250: ("Room", 3),
    0.1667: ("Chamber", 2),
    0.2917: ("Ambient", 4),
}

# DV algorithms that use ERs
DV_ER_ALGORITHMS = {"Hall", "Chamber", "Room"}

# VV mode names
VV_MODE_NAMES = {
    0.0417: "Concert Hall",
    0.0833: "Plate",
    0.1250: "Room",
    0.1667: "Chamber",
    0.2917: "Ambience",
}

# Per-VV-mode desired trebleMultScale (calibrated from preset_suite).
# Compensation = desired / global → adjusts treble_multiply so effective = desired.
# These values are tuned to make DV's HF character match VV when combined with
# DV's internal trebleMultScale. They do NOT correspond to VV display values.
MODE_TREBLE_MULT_SCALE = {
    "Concert Hall": 0.75,   # Hall algo global=0.75 → ratio 1.0
    "Plate":        1.30,   # Plate algo global=1.30 → ratio 1.0
    "Room":         0.45,   # Room algo global=0.45 → ratio 1.0
    "Chamber":      1.55,   # Chamber algo global=1.20 → ratio 1.292
    "Ambience":     0.78,   # Ambient algo global=0.60 → ratio 1.30
}

# Calibrated trebleMultScale values that produce correct HF behavior when used
# in the compensation formula above. These are NOT the raw AlgorithmConfig.h values;
# they are tuned as a system with MODE_TREBLE_MULT_SCALE to match VV's HF decay.
DV_GLOBAL_TREBLE_MULT_SCALE = {
    "Plate":   1.30,
    "Hall":    0.75,
    "Chamber": 1.20,
    "Room":    0.45,
    "Ambient": 0.60,
}

# VV ColorMode → treble offset
COLOR_TREBLE_OFFSET = {
    0.000: -0.15,
    0.333: 0.00,
    0.667: 0.10,
    1.000: 0.15,
}

# DV category for presets
VV_MODE_TO_CATEGORY = {
    0.0417: "Halls",
    0.0833: "Plates",
    0.1250: "Rooms",
    0.1667: "Chambers",
    0.2917: "Ambience",
}

# Mix defaults based on preset name/type
def default_mix(name, mode_float):
    name_lower = name.lower()
    if "gate" in name_lower:
        return 0.50
    if "ambient" in name_lower or "ambience" in name_lower or "pad" in name_lower:
        return 0.45
    if "huge" in name_lower or "large" in name_lower or "long" in name_lower:
        return 0.35
    if "vocal" in name_lower or "vox" in name_lower:
        return 0.25
    if "snare" in name_lower or "drum" in name_lower:
        return 0.25
    if mode_float == 0.0833:  # Plate
        return 0.30
    return 0.30


# ---------------------------------------------------------------------------
# VV → DV parameter conversion (from preset_suite.py)
# ---------------------------------------------------------------------------
def vv_predelay_to_ms(vv_val):
    return 300.0 * vv_val * vv_val  # Quadratic: matches VV's actual timing (raw=0.193 → 11ms, not 58ms)

def vv_bassmult_to_mult(vv_val):
    return 2.0 ** (4.0 * (vv_val - 0.5))

def vv_modrate_to_hz(vv_val):
    return 0.1 * (100.0 ** vv_val)  # Log scale (matches preset_suite.py)

def vv_highcut_to_hz(vv_val):
    if vv_val >= 0.99:
        return 20000
    return 200.0 * (100.0 ** vv_val)  # Log scale (matches preset_suite.py)

def vv_lowcut_to_hz(vv_val):
    if vv_val <= 0.01:
        return 20
    return 20.0 * (100.0 ** vv_val)

def vv_bassxover_to_hz(vv_val):
    return 50.0 * (100.0 ** vv_val)

def vv_highfreq_to_hz(vv_val):
    return 500.0 * (32.0 ** vv_val)

def color_treble_offset(colormode):
    known = sorted(COLOR_TREBLE_OFFSET.keys())
    closest = min(known, key=lambda k: abs(k - colormode))
    return COLOR_TREBLE_OFFSET[closest]


def translate_preset(vv_params, dv_algorithm, mode_name, name=""):
    """Translate VV preset parameters to DV equivalents."""
    dv = {
        "size": vv_params.get("Size", 0.5),
        "diffusion": max(
            vv_params.get("EarlyDiffusion", 0.7),
            vv_params.get("LateDiffusion", 0.7),
        ),
        "mod_depth": vv_params.get("ModDepth", 0.3),
        "mod_rate": vv_modrate_to_hz(vv_params.get("ModRate", 0.3)),
        "predelay": vv_predelay_to_ms(vv_params.get("PreDelay", 0.0)),
        "lo_cut": max(20, min(500, vv_lowcut_to_hz(vv_params.get("LowCut", 0.0)))),
        "hi_cut": max(1000, min(20000, vv_highcut_to_hz(vv_params.get("HighCut", 1.0)))),
        "width": 1.0,
    }

    # Bass multiply: clamped 0.5-2.0
    dv["bass_mult"] = max(0.5, min(2.0, vv_bassmult_to_mult(vv_params.get("BassMult", 0.5))))

    # Crossover: clamped 200-4000
    dv["crossover"] = max(200, min(4000, vv_bassxover_to_hz(vv_params.get("BassXover", 0.4))))

    # Treble multiply (damping)
    highshelf = vv_params.get("HighShelf", 0.0)
    colormode = vv_params.get("ColorMode", 0.333)
    highfreq_hz = vv_highfreq_to_hz(vv_params.get("HighFreq", 0.5))

    # VV HighShelf raw=0 means "no additional damping beyond DV's built-in HF structure".
    # DV's trebleMultScale (e.g. Hall=0.50) already provides base HF damping.
    # Higher VV HighShelf → more additional damping → lower DV treble_multiply.
    treble = 1.0 - (highshelf * 0.6)  # raw=0→1.0(no extra), raw=1→0.4(extra damping)
    treble += color_treble_offset(colormode)

    ref_hz = 2000.0
    if highfreq_hz < ref_hz:
        freq_scale = (highfreq_hz / ref_hz) ** (-0.5)
        treble *= freq_scale

    treble = max(0.1, min(1.0, treble))

    # Per-mode trebleMultScale compensation
    desired_tms = MODE_TREBLE_MULT_SCALE[mode_name]
    global_tms = DV_GLOBAL_TREBLE_MULT_SCALE[dv_algorithm]
    if abs(desired_tms - global_tms) > 0.001:
        compensation = desired_tms / global_tms
        treble = max(0.1, min(1.0, treble * compensation))

    dv["damping"] = treble

    # Decay time: rough initial estimate
    vv_decay = vv_params.get("Decay", 0.3)
    dv["decay"] = 0.5 + vv_decay * 15.0

    # Gate presets
    dv["gate_hold"] = 0.0
    dv["gate_release"] = 50.0
    if "gate" in name.lower() and vv_decay < 0.15:
        vv_size = vv_params.get("Size", 0.5)
        vv_attack = vv_params.get("Attack", 0.0)
        dv["gate_hold"] = 50.0 + vv_size * 400.0
        dv["gate_release"] = 5.0 + vv_attack * 30.0
        dv["decay"] = 2.0

    return dv


# ---------------------------------------------------------------------------
# ER extraction from IR
# ---------------------------------------------------------------------------
def extract_er_from_ir(ir_l, ir_r, sr):
    """Extract ER level and size from a stereo IR.

    Returns (er_level, er_size, debug_info).
    er_level: 0-1, relative ER energy
    er_size: 0-1, time centroid of ER window
    """
    er_end = int(ER_WINDOW_MS * sr / 1000)
    late_start = int(LATE_START_MS * sr / 1000)
    late_end = int(LATE_END_MS * sr / 1000)

    # Use mono sum for analysis
    ir = (ir_l[:late_end] + ir_r[:late_end]) / 2.0

    # Skip first 1ms (direct sound / click)
    skip = int(1 * sr / 1000)

    # ER energy (skip..er_end)
    er_segment = ir[skip:er_end]
    er_rms = float(np.sqrt(np.mean(er_segment ** 2))) if len(er_segment) > 0 else 1e-10

    # Late energy (late_start..late_end)
    late_segment = ir[late_start:late_end]
    late_rms = float(np.sqrt(np.mean(late_segment ** 2))) if len(late_segment) > 0 else 1e-10

    er_rms = max(er_rms, 1e-10)
    late_rms = max(late_rms, 1e-10)

    # ER level: ratio of ER to late energy in dB
    er_late_ratio_db = 20 * math.log10(er_rms / late_rms)

    # Map to 0-1: ER/late ratio of +10dB → er_level=0.65, -5dB → er_level=0.10
    # Linear mapping: er_level = 0.10 + (ratio_db + 5) * (0.55 / 15)
    er_level = 0.10 + (er_late_ratio_db + 5.0) * (0.55 / 15.0)
    er_level = max(0.0, min(1.0, er_level))

    # ER size: time centroid of the ER window
    er_abs = np.abs(er_segment)
    total_energy = float(np.sum(er_abs))
    if total_energy > 1e-10:
        times = np.arange(len(er_segment)) / sr * 1000  # in ms
        centroid_ms = float(np.sum(times * er_abs) / total_energy)
    else:
        centroid_ms = 40.0  # default

    # Map centroid to 0-1: 10ms → 0.2, 40ms → 0.5, 80ms → 0.8
    # Linear: er_size = 0.2 + (centroid_ms - 10) * (0.6 / 70)
    er_size = 0.2 + (centroid_ms - 10.0) * (0.6 / 70.0)
    er_size = max(0.0, min(1.0, er_size))

    debug = {
        "er_rms_db": 20 * math.log10(er_rms),
        "late_rms_db": 20 * math.log10(late_rms),
        "er_late_ratio_db": er_late_ratio_db,
        "centroid_ms": centroid_ms,
    }

    return er_level, er_size, debug


def formula_er(vv_params, dv_algorithm):
    """Simple formula-based ER estimation (fallback when VV not available)."""
    if dv_algorithm not in DV_ER_ALGORITHMS:
        return 0.0, 0.0
    attack = vv_params.get("Attack", 0.5)
    size = vv_params.get("Size", 0.5)
    er_level = max(0.0, 0.6 - attack * 0.5)
    er_size = size * 0.8
    return er_level, er_size


# ---------------------------------------------------------------------------
# VV plugin helpers
# ---------------------------------------------------------------------------
VV_PARAM_KEY_MAP = {
    "ReverbMode": "reverbmode",
    "ColorMode": "colormode",
    "Decay": "decay",
    "Size": "size",
    "PreDelay": "predelay",
    "EarlyDiffusion": "earlydiffusion",
    "LateDiffusion": "latediffusion",
    "ModRate": "modrate",
    "ModDepth": "moddepth",
    "HighCut": "highcut",
    "LowCut": "lowcut",
    "BassMult": "bassmult",
    "BassXover": "bassxover",
    "HighShelf": "highshelf",
    "HighFreq": "highfreq",
    "Attack": "attack",
}


# ---------------------------------------------------------------------------
# Simple RT60 measurement (bandpass at 500Hz, energy decay curve)
# ---------------------------------------------------------------------------
def measure_rt60_500hz(ir, sr):
    """Measure RT60 at 500Hz from a mono IR using energy decay curve."""
    from scipy.signal import butter, sosfilt

    # Bandpass 400-630Hz (one octave centered on 500Hz)
    sos = butter(4, [400, 630], btype='band', fs=sr, output='sos')
    filtered = sosfilt(sos, ir.astype(np.float64))

    # Energy decay curve (Schroeder integration, backwards)
    energy = filtered ** 2
    edc = np.cumsum(energy[::-1])[::-1]
    edc_max = edc[0]
    if edc_max <= 0:
        return None

    edc_db = 10 * np.log10(np.maximum(edc / edc_max, 1e-12))

    # Find -60dB point (or extrapolate from -5dB to -25dB)
    idx_5 = np.argmax(edc_db < -5)
    idx_25 = np.argmax(edc_db < -25)
    if idx_5 == 0 or idx_25 == 0 or idx_25 <= idx_5:
        return None

    # Linear regression on -5 to -25dB region, extrapolate to -60dB
    slope = (-25 - (-5)) / (idx_25 - idx_5)  # dB per sample
    if slope >= 0:
        return None
    samples_to_60 = -60.0 / slope  # samples from start of decay
    rt60 = samples_to_60 / sr
    return rt60


# ---------------------------------------------------------------------------
# DV plugin helpers
# ---------------------------------------------------------------------------
DV_PARAM_KEY_MAP = {
    "algorithm": "algorithm",
    "decay_time": "decay_time",
    "pre_delay": "pre_delay",
    "size": "size",
    "treble_multiply": "treble_multiply",
    "bass_multiply": "bass_multiply",
    "crossover": "crossover",
    "diffusion": "diffusion",
    "mod_depth": "mod_depth",
    "mod_rate": "mod_rate",
    "early_ref_level": "early_ref_level",
    "early_ref_size": "early_ref_size",
    "mix": "mix",
    "lo_cut": "lo_cut",
    "hi_cut": "hi_cut",
    "width": "width",
    "gate_hold": "gate_hold",
    "gate_release": "gate_release",
}

DV_ALGORITHM_MAP = {
    "Plate": 0, "Hall": 1, "Chamber": 2, "Room": 3, "Ambient": 4,
}


def apply_dv_params(plugin, dv_params, dv_algorithm):
    """Apply DV preset parameters to DuskVerb plugin instance."""
    # Set algorithm (pedalboard uses string values for choice parameters)
    try:
        plugin.algorithm = dv_algorithm
    except Exception as e:
        print(f"  WARNING: Failed to set algorithm: {e}")

    key_map = {
        "decay": "decay_time",
        "predelay": "pre_delay",
        "size": "size",
        "damping": "treble_multiply",
        "bass_mult": "bass_multiply",
        "crossover": "crossover",
        "diffusion": "diffusion",
        "mod_depth": "mod_depth",
        "mod_rate": "mod_rate",
        "lo_cut": "lo_cut",
        "hi_cut": "hi_cut",
        "width": "width",
        "gate_hold": "gate_hold",
        "gate_release": "gate_release",
    }

    for our_key, dv_attr in key_map.items():
        if our_key in dv_params:
            try:
                setattr(plugin, dv_attr, dv_params[our_key])
            except Exception as e:
                print(f"  WARNING: Failed to set {dv_attr}={dv_params[our_key]}: {e}")

    # ER params
    if "er_level" in dv_params:
        try:
            plugin.early_ref_level = dv_params["er_level"]
        except Exception:
            pass
    if "er_size" in dv_params:
        try:
            plugin.early_ref_size = dv_params["er_size"]
        except Exception:
            pass

    # Always 100% wet for calibration
    try:
        plugin.mix = 1.0
    except Exception:
        pass


def calibrate_decay(dv_plugin, target_rt60, dv_params, dv_algorithm, sr,
                    iterations=12):
    """Binary search DV decay_time to match target RT60 at 500Hz.

    Returns (best_decay_time, measured_rt60).
    """
    sig_dur = max(target_rt60 * 3, 4.0)
    n_samples = int(sr * sig_dur)

    lo, hi = 0.2, 30.0
    best_decay, best_rt60, best_error = None, None, float('inf')

    for _ in range(iterations):
        mid = (lo + hi) / 2.0
        dv_params_trial = dict(dv_params)
        dv_params_trial["decay"] = mid

        apply_dv_params(dv_plugin, dv_params_trial, dv_algorithm)

        # Flush
        silence = np.zeros(int(sr * 2.0), dtype=np.float32)
        stereo_silence = np.stack([silence, silence], axis=0)
        dv_plugin(stereo_silence, sr)

        # Impulse
        impulse = np.zeros(n_samples, dtype=np.float32)
        impulse[0] = 1.0
        stereo_in = np.stack([impulse, impulse], axis=0)
        output = dv_plugin(stereo_in, sr)
        ir_mono = output[0]

        rt60 = measure_rt60_500hz(ir_mono, sr)
        if rt60 is None or rt60 <= 0:
            hi = mid
            continue

        error = abs(rt60 / target_rt60 - 1.0)
        if error < best_error:
            best_error = error
            best_decay = mid
            best_rt60 = rt60

        if error < 0.08:
            return mid, rt60

        if rt60 > target_rt60:
            hi = mid
        else:
            lo = mid

    return best_decay, best_rt60


def apply_vv_params(plugin, vv_params, mode_float):
    """Apply VV preset parameters to plugin instance."""
    # Set mode first
    try:
        plugin.reverbmode = mode_float
    except Exception as e:
        print(f"  WARNING: Failed to set reverbmode: {e}")

    for vv_key, attr_name in VV_PARAM_KEY_MAP.items():
        if vv_key == "ReverbMode":
            continue  # Already set
        if vv_key in vv_params:
            try:
                setattr(plugin, attr_name, vv_params[vv_key])
            except Exception as e:
                print(f"  WARNING: Failed to set {attr_name}={vv_params[vv_key]}: {e}")

    # Always 100% wet for IR capture
    try:
        plugin.mix = 1.0
    except Exception:
        pass


def capture_ir(plugin, sr, duration=1.0, flush_dur=2.0):
    """Capture stereo IR from plugin by sending a unit impulse."""
    # Flush with silence
    silence = np.zeros(int(sr * flush_dur), dtype=np.float32)
    stereo_silence = np.stack([silence, silence], axis=0)
    plugin(stereo_silence, sr)

    # Send impulse
    n_samples = int(sr * duration)
    impulse = np.zeros(n_samples, dtype=np.float32)
    impulse[0] = 1.0
    stereo_in = np.stack([impulse, impulse], axis=0)
    output = plugin(stereo_in, sr)
    return output[0], output[1]


# ---------------------------------------------------------------------------
# C++ code generation
# ---------------------------------------------------------------------------
def format_float(v, decimals=2):
    """Format float for C++ with trailing f."""
    if decimals == 0:
        return f"{v:.1f}f"  # Always include .0 for valid C++ literal
    s = f"{v:.{decimals}f}"
    # Remove trailing zeros but keep at least one decimal
    if '.' in s:
        s = s.rstrip('0')
        if s.endswith('.'):
            s += '0'
    return s + "f"


def generate_preset_line(name, category, algo_idx, dv, er_level, er_size, mix_val):
    """Generate a single C++ FactoryPreset initializer line."""
    # Format values
    vals = [
        f"{algo_idx}",
        format_float(dv["decay"]),
        format_float(dv["predelay"], 1),
        format_float(dv["size"], 3),
        format_float(dv["damping"]),
        format_float(dv["bass_mult"]),
        format_float(dv["crossover"], 0),
        format_float(dv["diffusion"]),
        format_float(dv["mod_depth"], 3),
        format_float(dv["mod_rate"]),
        format_float(er_level),
        format_float(er_size),
        format_float(mix_val),
        format_float(dv["lo_cut"], 0),
        format_float(dv["hi_cut"], 0),
        format_float(dv["width"]),
        format_float(dv["gate_hold"], 0),
        format_float(dv["gate_release"], 0),
    ]
    val_str = ", ".join(vals)
    return f'        {{ "{name}",{" " * max(1, 30 - len(name))}"{category}",{" " * max(1, 12 - len(category))}{val_str} }},'


def generate_cpp_output(presets_data):
    """Generate the complete FactoryPresets.h content."""
    lines = []
    lines.append("#pragma once")
    lines.append("")
    lines.append("#include <juce_audio_processors/juce_audio_processors.h>")
    lines.append("#include <vector>")
    lines.append("")
    lines.append("// Minimal factory preset data. Each preset stores parameter values in a flat struct.")
    lines.append("struct FactoryPreset")
    lines.append("{")
    lines.append("    const char* name;")
    lines.append("    const char* category;")
    lines.append("")
    lines.append("    int   algorithm;       // 0-4")
    lines.append("    float decay;           // 0.2-30")
    lines.append("    float predelay;        // 0-250 ms")
    lines.append("    float size;            // 0-1")
    lines.append("    float damping;         // 0.1-1 (treble multiply)")
    lines.append("    float bassMult;        // 0.5-2")
    lines.append("    float crossover;       // 200-4000 Hz")
    lines.append("    float diffusion;       // 0-1")
    lines.append("    float modDepth;        // 0-1")
    lines.append("    float modRate;         // 0.1-10 Hz")
    lines.append("    float erLevel;         // 0-1")
    lines.append("    float erSize;          // 0-1")
    lines.append("    float mix;             // 0-1")
    lines.append("    float loCut;           // 20-500 Hz")
    lines.append("    float hiCut;           // 1000-20000 Hz")
    lines.append("    float width;           // 0-2")
    lines.append("    float gateHold;        // 0-500 ms (0 = disabled)")
    lines.append("    float gateRelease;     // 5-500 ms")
    lines.append("")
    lines.append("    void applyTo (juce::AudioProcessorValueTreeState& apvts) const")
    lines.append("    {")
    lines.append('        if (auto* p = apvts.getParameter ("algorithm"))   p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (algorithm)));')
    lines.append('        if (auto* p = apvts.getParameter ("decay"))        p->setValueNotifyingHost (p->convertTo0to1 (decay));')
    lines.append('        if (auto* p = apvts.getParameter ("predelay"))     p->setValueNotifyingHost (p->convertTo0to1 (predelay));')
    lines.append('        if (auto* p = apvts.getParameter ("size"))         p->setValueNotifyingHost (p->convertTo0to1 (size));')
    lines.append('        if (auto* p = apvts.getParameter ("damping"))      p->setValueNotifyingHost (p->convertTo0to1 (damping));')
    lines.append('        if (auto* p = apvts.getParameter ("bass_mult"))    p->setValueNotifyingHost (p->convertTo0to1 (bassMult));')
    lines.append('        if (auto* p = apvts.getParameter ("crossover"))    p->setValueNotifyingHost (p->convertTo0to1 (crossover));')
    lines.append('        if (auto* p = apvts.getParameter ("diffusion"))    p->setValueNotifyingHost (p->convertTo0to1 (diffusion));')
    lines.append('        if (auto* p = apvts.getParameter ("mod_depth"))    p->setValueNotifyingHost (p->convertTo0to1 (modDepth));')
    lines.append('        if (auto* p = apvts.getParameter ("mod_rate"))     p->setValueNotifyingHost (p->convertTo0to1 (modRate));')
    lines.append('        if (auto* p = apvts.getParameter ("er_level"))     p->setValueNotifyingHost (p->convertTo0to1 (erLevel));')
    lines.append('        if (auto* p = apvts.getParameter ("er_size"))      p->setValueNotifyingHost (p->convertTo0to1 (erSize));')
    lines.append('        if (auto* p = apvts.getParameter ("mix"))          p->setValueNotifyingHost (p->convertTo0to1 (mix));')
    lines.append('        if (auto* p = apvts.getParameter ("lo_cut"))       p->setValueNotifyingHost (p->convertTo0to1 (loCut));')
    lines.append('        if (auto* p = apvts.getParameter ("hi_cut"))       p->setValueNotifyingHost (p->convertTo0to1 (hiCut));')
    lines.append('        if (auto* p = apvts.getParameter ("width"))        p->setValueNotifyingHost (p->convertTo0to1 (width));')
    lines.append('        if (auto* p = apvts.getParameter ("gate_hold"))    p->setValueNotifyingHost (p->convertTo0to1 (gateHold));')
    lines.append('        if (auto* p = apvts.getParameter ("gate_release")) p->setValueNotifyingHost (p->convertTo0to1 (gateRelease));')
    lines.append('        if (auto* p = apvts.getParameter ("freeze"))        p->setValueNotifyingHost (0.0f);')
    lines.append('        if (auto* p = apvts.getParameter ("predelay_sync")) p->setValueNotifyingHost (0.0f);')
    lines.append('        if (auto* p = apvts.getParameter ("bus_mode"))      p->setValueNotifyingHost (0.0f);')
    lines.append("    }")
    lines.append("};")
    lines.append("")
    lines.append("// clang-format off")
    lines.append("//")
    lines.append("// Translated from VintageVerb factory presets.")
    lines.append("// Effective value = raw param * algorithm scale factor")
    lines.append("// Algorithm trebleMultScale: Plate=1.30, Hall=0.75, Chamber=1.20, Room=0.45, Ambient=0.60")
    lines.append("// Algorithm bassMultScale:   Plate=1.0, Hall=1.0,  Chamber=1.0, Room=0.85, Ambient=1.0")
    lines.append("// Algorithm erLevelScale:    Plate=0.0, Hall=1.0,  Chamber=0.8, Room=0.5,  Ambient=0.0")
    lines.append("// Algorithm lateGainScale:   Plate=0.47, Hall=0.65, Chamber=0.45, Room=1.10, Ambient=0.60")
    lines.append("// Algorithm decayTimeScale:  Plate=1.0, Hall=1.0,  Chamber=1.0, Room=1.4,  Ambient=1.0")
    lines.append("//")
    lines.append("inline const std::vector<FactoryPreset>& getFactoryPresets(){")
    lines.append("    static const std::vector<FactoryPreset> presets = {")
    lines.append("        //                                              algo  decay  pre    size   damp  bass   xover   diff  modD   modR  erLv  erSz  mix   loCut  hiCut   width  gHold  gRel")

    current_category = None
    for pd in presets_data:
        if pd["category"] != current_category:
            current_category = pd["category"]
            lines.append(f"        // -- {current_category} --")
        lines.append(pd["cpp_line"])

    lines.append("    };")
    lines.append("    return presets;")
    lines.append("}")
    lines.append("// clang-format on")
    lines.append("")

    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="Extract ER settings and generate DuskVerb factory presets")
    parser.add_argument("--dry-run", action="store_true",
                        help="Use formula-based ER (no VV plugin needed)")
    parser.add_argument("--output", type=str, default=None,
                        help="Output file path (default: stdout)")
    parser.add_argument("--write", action="store_true",
                        help="Write directly to FactoryPresets.h")
    args = parser.parse_args()

    # Load qualifying presets
    json_path = os.path.join(os.path.dirname(__file__),
                             "..", "tests", "reference_comparison", "qualifying_presets.json")
    with open(json_path) as f:
        all_presets = json.load(f)

    # Filter to our 5 modes (exclude Hall1984)
    qualifying = [p for p in all_presets if p["mode_float"] in VV_MODE_TO_DV]
    print(f"Found {len(qualifying)} qualifying presets", file=sys.stderr)

    # Load plugins if not dry-run
    vv_plugin = None
    dv_plugin = None
    if not args.dry_run:
        try:
            from pedalboard import load_plugin
            vv_paths = [
                "/Library/Audio/Plug-Ins/Components/ValhallaVintageVerbAU64.component",
                os.path.expanduser("~/Library/Audio/Plug-Ins/Components/ValhallaVintageVerbAU64.component"),
                "/Library/Audio/Plug-Ins/VST3/ValhallaVintageVerb.vst3",
            ]
            for p in vv_paths:
                if os.path.exists(p):
                    print(f"Loading VintageVerb: {p}", file=sys.stderr)
                    vv_plugin = load_plugin(p)
                    break
            if vv_plugin is None:
                print("WARNING: VintageVerb not found, falling back to formula-based ER", file=sys.stderr)
                args.dry_run = True

            # Load DuskVerb for decay calibration
            dv_paths = [
                os.path.expanduser("~/Library/Audio/Plug-Ins/Components/DuskVerb.component"),
                "/Library/Audio/Plug-Ins/Components/DuskVerb.component",
                os.path.expanduser("~/.vst3/DuskVerb.vst3"),
            ]
            for p in dv_paths:
                if os.path.exists(p):
                    print(f"Loading DuskVerb: {p}", file=sys.stderr)
                    dv_plugin = load_plugin(p)
                    break
            if dv_plugin is None:
                print("WARNING: DuskVerb not found, decay will use rough formula", file=sys.stderr)
        except ImportError:
            print("WARNING: pedalboard not installed, falling back to formula-based ER", file=sys.stderr)
            args.dry_run = True

    # Process each preset
    presets_data = []
    for preset in qualifying:
        name = preset["name"]
        vv_params = preset["params"]
        mode_float = preset["mode_float"]
        dv_algorithm, algo_idx = VV_MODE_TO_DV[mode_float]
        mode_name = VV_MODE_NAMES[mode_float]
        category = VV_MODE_TO_CATEGORY[mode_float]

        # Override: plate-like presets in Chamber/Ambience use Plate algorithm
        if "plate" in name.lower() and dv_algorithm in ("Chamber", "Ambient"):
            dv_algorithm = "Plate"
            algo_idx = 0
            category = "Plates"  # Re-categorize

        # Translate VV params → DV params
        dv = translate_preset(vv_params, dv_algorithm, mode_name, name)

        # ER extraction
        vv_ir_l = None
        if dv_algorithm not in DV_ER_ALGORITHMS:
            er_level, er_size = 0.0, 0.0
            er_debug = "no ERs (Plate/Ambient)"
        elif args.dry_run:
            er_level, er_size = formula_er(vv_params, dv_algorithm)
            er_debug = "formula"
        else:
            # Capture IR from VV
            apply_vv_params(vv_plugin, vv_params, mode_float)
            ir_l, ir_r = capture_ir(vv_plugin, SAMPLE_RATE, IR_DURATION, FLUSH_DURATION)
            vv_ir_l = ir_l
            er_level, er_size, debug = extract_er_from_ir(ir_l, ir_r, SAMPLE_RATE)
            er_debug = f"IR: ratio={debug['er_late_ratio_db']:.1f}dB, centroid={debug['centroid_ms']:.1f}ms"

        # Decay calibration: measure VV's RT60 and binary search DV's decay_time
        # Skip calibration for gate presets (gate controls tail length, not decay)
        is_gate = dv.get("gate_hold", 0) > 0
        cal_debug = ""
        if dv_plugin is not None and not args.dry_run and not is_gate:
            # Get VV IR for RT60 measurement (capture longer IR if needed)
            if vv_ir_l is None:
                apply_vv_params(vv_plugin, vv_params, mode_float)
                vv_ir_l, _ = capture_ir(vv_plugin, SAMPLE_RATE, max(IR_DURATION, 4.0), FLUSH_DURATION)

            # Capture longer VV IR for RT60 (need at least 3x RT60 worth of samples)
            vv_decay_raw = vv_params.get("Decay", 0.3)
            rt60_est = max(0.5, vv_decay_raw * 20)  # rough upper bound
            if rt60_est > IR_DURATION:
                apply_vv_params(vv_plugin, vv_params, mode_float)
                vv_ir_l, _ = capture_ir(vv_plugin, SAMPLE_RATE, max(rt60_est * 3, 6.0), FLUSH_DURATION)

            vv_rt60 = measure_rt60_500hz(vv_ir_l, SAMPLE_RATE)
            if vv_rt60 and vv_rt60 > 0:
                cal_decay, cal_rt60 = calibrate_decay(
                    dv_plugin, vv_rt60, dv, dv_algorithm, SAMPLE_RATE)
                if cal_decay is not None:
                    dv["decay"] = cal_decay
                    cal_debug = f"decay: VV RT60={vv_rt60:.2f}s → DV decay={cal_decay:.2f}s (RT60={cal_rt60:.2f}s)"
                else:
                    cal_debug = f"decay: VV RT60={vv_rt60:.2f}s (calibration failed, using formula)"
            else:
                cal_debug = "decay: VV RT60 unmeasurable (using formula)"

        # Mix
        mix_val = default_mix(name, mode_float)

        # Generate C++ line
        cpp_line = generate_preset_line(name, category, algo_idx, dv, er_level, er_size, mix_val)

        presets_data.append({
            "name": name,
            "category": category,
            "algorithm": dv_algorithm,
            "cpp_line": cpp_line,
            "er_debug": er_debug,
        })

        debug_line = f"  {name:<35s} → {dv_algorithm:<8s} ER: level={er_level:.2f}, size={er_size:.2f} ({er_debug})"
        if cal_debug:
            debug_line += f"\n    {cal_debug}"
        print(debug_line, file=sys.stderr)

    # Sort by category order, then alphabetically within category
    category_order = ["Halls", "Plates", "Rooms", "Chambers", "Ambience"]
    presets_data.sort(key=lambda p: (category_order.index(p["category"]) if p["category"] in category_order else 99, p["name"]))

    # Generate output
    output = generate_cpp_output(presets_data)

    if args.write:
        out_path = os.path.join(os.path.dirname(__file__), "..", "src", "FactoryPresets.h")
        with open(out_path, 'w') as f:
            f.write(output)
        print(f"\nWrote {len(presets_data)} presets to {out_path}", file=sys.stderr)
    elif args.output:
        with open(args.output, 'w') as f:
            f.write(output)
        print(f"\nWrote {len(presets_data)} presets to {args.output}", file=sys.stderr)
    else:
        print(output)

    print(f"\nSummary: {len(presets_data)} presets across {len(set(p['category'] for p in presets_data))} categories",
          file=sys.stderr)
    for cat in category_order:
        count = sum(1 for p in presets_data if p["category"] == cat)
        if count > 0:
            print(f"  {cat}: {count}", file=sys.stderr)


if __name__ == "__main__":
    main()
