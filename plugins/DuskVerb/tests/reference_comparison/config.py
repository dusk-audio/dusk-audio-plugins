"""
Configuration for DuskVerb vs ReferenceReverb reverb comparison.

Plugin paths, mode pairings, and matched parameter sets.
ReferenceReverb v4.0.5 uses 22 modes at step 1/24 (~0.0417).
"""

import os

SAMPLE_RATE = 48000
SIGNAL_DURATION = 40.0  # seconds of tail capture (Hall1984 1970s RT60 can exceed 12s)

# ---------------------------------------------------------------------------
# Plugin discovery
# ---------------------------------------------------------------------------
DUSKVERB_PATHS = [
    "~/Library/Audio/Plug-Ins/Components/DuskVerb.component",
    "~/.vst3/DuskVerb.vst3",
]

REFERENCE_REVERB_PATHS = [
    "/Library/Audio/Plug-Ins/Components/ValhallaVintageVerbAU64.component",  # AU preferred (raw values verified in Logic Pro)
    "~/Library/Audio/Plug-Ins/Components/ValhallaVintageVerbAU64.component",
    "/Library/Audio/Plug-Ins/VST3/ValhallaVintageVerb.vst3",
]

REFERENCE_ROOM_PATHS = [
    "/Library/Audio/Plug-Ins/Components/ValhallaRoomAU64.component",
    "~/Library/Audio/Plug-Ins/Components/ValhallaRoomAU64.component",
]


def find_plugin(paths):
    """Return the first existing plugin path, or None."""
    for p in paths:
        expanded = os.path.expanduser(p)
        if os.path.exists(expanded):
            return expanded
    return None


def discover_params(plugin):
    """Print all pedalboard-accessible parameters and their current values."""
    params = []
    for attr in sorted(dir(plugin)):
        if attr.startswith('_'):
            continue
        try:
            val = getattr(plugin, attr)
            if callable(val):
                continue
            params.append((attr, val))
        except Exception:
            pass
    return params


# ---------------------------------------------------------------------------
# Mode pairings: DuskVerb algorithm <-> ReferenceReverb color/mode
# ---------------------------------------------------------------------------
# Each pairing has matched parameters for fair A/B comparison:
#   - 100% wet (isolate reverb character)
#   - Matched decay time, size, pre-delay
#   - Neutral EQ (lo/hi cut fully open)
#   - Moderate diffusion and modulation

MODE_PAIRINGS = [
    {
        "name": "Room",
        "auto_calibrate": True,
        "duskverb": {
            "algorithm": "Room",
            "decay_time": 0.76,
            "size": 0.388,
            "diffusion": 0.50,
            "treble_multiply": 1.00,
            "bass_multiply": 1.26,
            "crossover": 633,
            "mod_depth": 0.309,
            "mod_rate": 1.50,
            "early_ref_level": 0.55,
            "early_ref_size": 0.45,
            "pre_delay": 9.0,
            "lo_cut": 20,
            "hi_cut": 12000,
            "width": 1.0,
        },
        "reference": {
            # Exact values from VV factory preset "Fat Snare Room"
            "_reverbmode": 0.125,           # Room
            "_colormode": 0.67,             # Now (from Fat Snare Room screenshot)
            "_decay": 0.229916,             # Factory value (auto_calibrate overrides)
            "_size": 0.388,
            "_predelay": 0.17725,
            "_attack": 0.124,
            "_bassmult": 0.56595,
            "_bassxover": 0.484,
            "_highshelf": 0.0,
            "_highfreq": 0.528385,
            "_diffusion_early": 0.68,
            "_diffusion_late": 1.0,
            "_mod_rate": 0.141414,
            "_mod_depth": 0.308,
            "_high_cut": 0.422995,
            "_low_cut": 0.05,               # ~25Hz (from Fat Snare Room screenshot)
        },
    },
    {
        "name": "Hall",
        "auto_calibrate": True,
        "duskverb": {
            "algorithm": "Hall",
            "decay_time": 1.15,
            "size": 0.504,
            "diffusion": 1.0,
            "treble_multiply": 0.50,
            "bass_multiply": 1.40,
            "crossover": 430,
            "mod_depth": 1.0,
            "mod_rate": 0.46,
            "early_ref_level": 0.51,
            "early_ref_size": 0.40,
            "pre_delay": 23.0,
            "lo_cut": 90,
            "hi_cut": 4000,
            "width": 1.0,
        },
        "reference": {
            # VV "Small Vocal Hall" preset — exact AU raw values from Logic Pro
            # AU uses different 0-1 normalization than VST3; these are verified correct
            "_reverbmode": 0.04,            # Concert Hall
            "_colormode": 0.67,             # 1980s
            "_decay": 0.28,                 # 1.15s display (auto_calibrate overrides)
            "_size": 0.50,                  # 50.4%
            "_predelay": 0.27,              # 23ms
            "_attack": 0.18,                # 18.4%
            "_bassmult": 0.60,              # 1.40x
            "_bassxover": 0.43,             # 430Hz
            "_highshelf": 0.0,              # -24.00dB (maximum HF damping)
            "_highfreq": 0.39,              # 4000Hz
            "_diffusion_early": 0.49,       # 48.8%
            "_diffusion_late": 1.0,         # 100%
            "_mod_rate": 0.04,              # 0.46Hz
            "_mod_depth": 1.0,              # 100%
            "_high_cut": 0.39,              # 4000Hz
            "_low_cut": 0.05,               # 90Hz
        },
    },
    {
        "name": "Plate",
        "auto_calibrate": True,
        "duskverb": {
            "algorithm": "Plate",
            "decay_time": 1.2,
            "size": 0.672,
            "diffusion": 1.0,
            "treble_multiply": 1.0,
            "bass_multiply": 1.16,
            "crossover": 357,
            "mod_depth": 0.192,
            "mod_rate": 0.15,
            "early_ref_level": 0.70,
            "early_ref_size": 0.54,
            "pre_delay": 0.0,
            "lo_cut": 20,
            "hi_cut": 6000,
            "width": 1.0,
        },
        "reference": {
            # Exact values from VV factory preset "Vox Plate"
            "_reverbmode": 0.083333,        # Plate
            "_colormode": 1.0,              # Now+ (factory value)
            "_decay": 0.281755,             # Factory value (auto_calibrate overrides)
            "_size": 0.672,
            "_predelay": 0.0,
            "_attack": 0.144,
            "_bassmult": 0.553585,
            "_bassxover": 0.426918,
            "_highshelf": 0.0,
            "_highfreq": 0.439205,
            "_diffusion_early": 0.58,
            "_diffusion_late": 1.0,
            "_mod_rate": 0.092,
            "_mod_depth": 0.192,
            "_high_cut": 0.74591,
            "_low_cut": 0.05,               # 90Hz (from Vox Plate screenshot)
        },
    },
    {
        "name": "Chamber",
        "auto_calibrate": True,
        "duskverb": {
            "algorithm": "Chamber",
            "decay_time": 2.5,
            "size": 0.70,
            "diffusion": 1.0,
            "treble_multiply": 1.0,
            "bass_multiply": 1.21,
            "crossover": 218,
            "mod_depth": 0.16,
            "mod_rate": 0.17,
            "early_ref_level": 0.48,
            "early_ref_size": 0.56,
            "pre_delay": 30.0,
            "lo_cut": 25,
            "hi_cut": 14000,
            "width": 1.0,
        },
        "reference": {
            # Exact values from VV factory preset "Rich Chamber"
            "_reverbmode": 0.1667,          # Chamber
            "_colormode": 0.67,             # Now (bright)
            "_decay": 0.34,                 # Factory value (auto_calibrate overrides)
            "_size": 0.70,
            "_predelay": 0.28,              # ~84ms
            "_attack": 0.25,
            "_bassmult": 0.57,
            "_bassxover": 0.32,
            "_highshelf": 0.0,
            "_highfreq": 0.63,
            "_diffusion_early": 1.0,
            "_diffusion_late": 1.0,
            "_mod_rate": 0.12,
            "_mod_depth": 0.16,
            "_high_cut": 0.49,              # ~1910Hz
            "_low_cut": 0.05,               # ~25Hz
        },
    },
    {
        "name": "Ambient",
        "auto_calibrate": True,
        "duskverb": {
            "algorithm": "Ambient",
            "decay_time": 0.90,
            "size": 0.20,
            "diffusion": 1.0,
            "treble_multiply": 1.0,
            "bass_multiply": 1.84,
            "crossover": 200,
            "mod_depth": 0.36,
            "mod_rate": 0.11,
            "early_ref_level": 0.0,
            "early_ref_size": 0.0,
            "pre_delay": 0.0,
            "lo_cut": 25,
            "hi_cut": 20000,
            "width": 1.0,
        },
        "reference": {
            # Exact values from VV factory preset "Ambience"
            "_reverbmode": 0.2917,          # Ambience
            "_colormode": 0.67,             # Now (bright)
            "_decay": 0.24,                 # Factory value (auto_calibrate overrides)
            "_size": 0.20,
            "_predelay": 0.0,
            "_attack": 0.36,
            "_bassmult": 0.72,
            "_bassxover": 0.28,
            "_highshelf": 0.0,
            "_highfreq": 0.67,
            "_diffusion_early": 1.0,
            "_diffusion_late": 1.0,
            "_mod_rate": 0.02,
            "_mod_depth": 0.36,
            "_high_cut": 0.85,              # ~10kHz
            "_low_cut": 0.05,               # ~25Hz
        },
    },
]

# ---------------------------------------------------------------------------
# ReferenceReverb parameter name mapping
# ---------------------------------------------------------------------------
# Populated after running: python3 compare_reverbs.py --list-params
# Maps our semantic names (_mode, _decay, etc.) to pedalboard attribute names.
# Update these after discovery:

# ReferenceReverb parameter mapping (discovered via --list-params)
# All ReferenceReverb params are 0.0-1.0 normalized.
#
# ReverbMode values (22 modes, step = 1/24 ≈ 0.0417):
#   0.0417 = Concert Hall    0.0833 = Plate           0.1250 = Room
#   0.1667 = Chamber         0.2083 = Random Space    0.2500 = Chorus Space
#   0.2917 = Ambience        0.3333 = (unnamed)       0.3750 = Sanctuary
#   0.4167 = Dirty Hall      0.4583 = Dirty Plate     0.5000 = Smooth Plate
#   0.5417 = Smooth Room     0.5833 = Smooth Random   0.6250 = Nonlin
#   0.6667 = Chaotic Chamber 0.7083 = Chaotic Hall    0.7500 = Chaotic Neutral
#   0.7917 = Cathedral       0.8333 = Palace          0.8750 = Chamber1979
#   0.9167 = Hall1984
#
# ColorMode values (3 color options):
#   0.000 = 1970s (dark), 0.333 = 1980s (neutral), 0.667 = Now (bright)
#
REFERENCE_PARAM_MAP = {
    "_reverbmode": "reverbmode",    # Algorithm selector (0-1, 22 modes)
    "_colormode": "colormode",      # Color/era selector (0-1, 3 modes)
    "_decay": "decay",              # Decay time (0-1 normalized)
    "_size": "size",                # Room size (0-1)
    "_predelay": "predelay",        # Pre-delay (0-1 normalized, ~0-300ms)
    "_diffusion_early": "earlydiffusion",  # Early diffusion (0-1)
    "_diffusion_late": "latediffusion",    # Late diffusion (0-1)
    "_mod_rate": "modrate",         # Modulation rate (0-1)
    "_mod_depth": "moddepth",       # Modulation depth (0-1)
    "_high_cut": "highcut",         # High cut filter (0-1 normalized freq)
    "_low_cut": "lowcut",           # Low cut filter (0-1 normalized freq)
    "_bassmult": "bassmult",        # Bass multiply (0-1)
    "_bassxover": "bassxover",      # Bass crossover (0-1)
    "_highshelf": "highshelf",      # High shelf (0-1)
    "_highfreq": "highfreq",        # High freq damping (0-1)
    "_attack": "attack",            # Attack/transient shape (0-1)
    "_mix": "mix",                  # Dry/wet mix (0-1)
}


# ---------------------------------------------------------------------------
# ReferenceRoom parameter name mapping
# ---------------------------------------------------------------------------
# All ReferenceRoom params are 0.0-1.0 normalized.
# Discovered via pedalboard parameter inspection.
#
# Type values (12 types, evenly spaced 0-1):
#   0.000 = Large Room, 0.083 = Medium Room, 0.167 = Bright Room, ...
#
REFERENCE_ROOM_PARAM_MAP = {
    "_type": "type",                    # Algorithm type selector (0-1, 12 types)
    "_space": "space",                  # Space modifier (0-1)
    "_decay": "decay",                  # Decay time (0-1 normalized)
    "_predelay": "predelay",            # Pre-delay (0-1 normalized)
    "_latesize": "latesize",            # Late reverb size (0-1)
    "_diffusion": "diffusion",          # Diffusion (0-1)
    "_latemoddepth": "latemoddepth",    # Late modulation depth (0-1)
    "_latemodrate": "latemodrate",      # Late modulation rate (0-1)
    "_earlylatemix": "earlylatemix",    # Early/late balance (0=early, 1=late)
    "_earlysend": "earlysend",          # Early → late send (0-1)
    "_earlysize": "earlysize",          # Early reflection size (0-1)
    "_earlycross": "earlycross",        # Early cross-feed (0-1)
    "_latecross": "latecross",          # Late cross-feed (0-1)
    "_rtbassmultiply": "rtbassmultiply",  # Bass RT60 multiplier (0-1)
    "_rtxover": "rtxover",              # Bass crossover freq (0-1)
    "_rthighmultiply": "rthighmultiply",  # High RT60 multiplier (0-1)
    "_rthighxover": "rthighxover",      # High crossover freq (0-1)
    "_hicut": "hicut",                  # High cut filter (0-1)
    "_locut": "locut",                  # Low cut filter (0-1)
    "_mix": "mix",                      # Dry/wet mix (0-1)
    "_earlymodrate": "earlymodrate",    # Early modulation rate (0-1)
    "_earlymoddepth": "earlymoddepth",  # Early modulation depth (0-1)
}


def apply_reference_room_params(plugin, config):
    """Apply ReferenceRoom parameters using the discovered mapping."""
    for semantic_key, value in config.items():
        if not semantic_key.startswith('_'):
            continue
        mapped = REFERENCE_ROOM_PARAM_MAP.get(semantic_key)
        if mapped is None:
            print(f"  WARNING: No VRoom mapping for {semantic_key}")
            continue
        try:
            setattr(plugin, mapped, value)
        except Exception as e:
            print(f"  WARNING: Failed to set VRoom {mapped}={value}: {e}")

    # Always set 100% wet
    try:
        plugin.mix = 1.0
    except Exception:
        pass


def apply_reference_params(plugin, reference_config):
    """Apply ReferenceReverb parameters using the discovered mapping.

    reference_config keys start with '_' (semantic names).
    REFERENCE_PARAM_MAP maps them to actual pedalboard attribute names.

    Config values are AU 0-1 normalized (verified from Logic Pro parameter inspector).
    Uses setattr for AU plugins. For VST3, uses raw_value (but AU is preferred).
    """
    if not REFERENCE_PARAM_MAP:
        print("WARNING: REFERENCE_PARAM_MAP is empty. Run --list-params first.")
        return

    params_dict = plugin.parameters
    is_vst3 = hasattr(plugin, '_path') and '.vst3' in str(getattr(plugin, '_path', ''))
    # Heuristic: if 'mix' param has range > 1, it's VST3-style
    mix_param = params_dict.get("mix")
    if mix_param is not None:
        try:
            # VST3 mix range is 0-100, AU is 0-1
            is_vst3 = mix_param.max_value > 1.5
        except Exception:
            pass

    for semantic_key, value in reference_config.items():
        if not semantic_key.startswith('_'):
            continue
        mapped = REFERENCE_PARAM_MAP.get(semantic_key)
        if mapped is None:
            print(f"  WARNING: No mapping for {semantic_key}")
            continue

        if is_vst3:
            # VST3: use raw_value (0-1 normalized bypasses unit conversion)
            param = params_dict.get(mapped)
            if param is not None:
                try:
                    param.raw_value = value
                except Exception as e:
                    print(f"  WARNING: Failed to set {mapped}={value} via raw_value: {e}")
        else:
            # AU: setattr with 0-1 values works directly
            try:
                setattr(plugin, mapped, value)
            except Exception as e:
                print(f"  WARNING: Failed to set AU {mapped}={value}: {e}")

    # Always set 100% wet
    if is_vst3:
        if mix_param is not None:
            mix_param.raw_value = 1.0
    else:
        try:
            plugin.mix = 1.0
        except Exception:
            pass

    # Verify critical VV parameters were applied
    print("  Verifying VV parameters:")
    for key in ["mix", "reverbmode", "colormode", "decay", "highcut", "lowcut"]:
        param = params_dict.get(key)
        if param:
            print(f"    {key} = {getattr(plugin, key, '?')} (raw={param.raw_value:.4f})")
        else:
            print(f"    {key} = NOT FOUND")


def apply_duskverb_params(plugin, duskverb_config):
    """Apply DuskVerb parameters (matching analyze_reverb.py pattern)."""
    plugin.algorithm = duskverb_config["algorithm"]
    plugin.dry_wet = 1.0
    plugin.freeze = False
    plugin.bus_mode = False
    plugin.pre_delay_sync = "Free"

    for key, value in duskverb_config.items():
        if key == "algorithm":
            continue
        try:
            setattr(plugin, key, value)
        except Exception as e:
            print(f"  WARNING: Failed to set DuskVerb {key}={value}: {e}")

    # Verify critical parameters were applied
    for key in ["dry_wet", "algorithm", "hi_cut", "lo_cut"]:
        try:
            actual = getattr(plugin, key)
            print(f"    Verify: {key} = {actual}")
        except Exception:
            print(f"    Verify: {key} = CANNOT READ")


# ---------------------------------------------------------------------------
# Tuning parameter mapping: metric deltas -> AlgorithmConfig.h changes
# ---------------------------------------------------------------------------
TUNING_MAP = {
    "rt60_hf_too_short": [
        "Increase trebleMultScale (current Room: 0.65, try 0.80-0.90)",
        "Increase feedbackLPHz (current Room: 5000, try 7000-8000)",
        "Increase bandwidthHz (current Room: 5000, try 8000-10000)",
    ],
    "rt60_lf_too_long": [
        "Decrease bassMultScale (current Room: 0.80, try 0.70)",
    ],
    "modal_ringing": [
        "Perturb delayLengths[] to break harmonic convergence",
        "Increase modDepthScale (current Room: 0.90, try 1.1-1.3)",
        "Increase modDepthFloor (current Room: 0.50, try 0.65+)",
    ],
    "low_echo_density": [
        "Increase inlineDiffusionCoeff (current Room: 0.70, try 0.75)",
        "Increase inputDiffMaxCoeff12 (current Room: 0.70)",
    ],
    "tail_roughness": [
        "Increase modulation (blurs discrete echoes)",
        "Review delay length ratios for better modal distribution",
    ],
}
