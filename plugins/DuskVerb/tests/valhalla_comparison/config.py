"""
Configuration for DuskVerb vs VintageVerb reverb comparison.

Plugin paths, mode pairings, and matched parameter sets.
VintageVerb v4.0.5 uses 22 modes at step 1/24 (~0.0417).
"""

import os

SAMPLE_RATE = 48000
SIGNAL_DURATION = 12.0  # seconds of tail capture (VintageVerb tails can exceed 8s)

# ---------------------------------------------------------------------------
# Plugin discovery
# ---------------------------------------------------------------------------
DUSKVERB_PATHS = [
    "~/Library/Audio/Plug-Ins/Components/DuskVerb.component",
    "~/.vst3/DuskVerb.vst3",
]

VINTAGEVERB_PATHS = [
    "/Library/Audio/Plug-Ins/Components/ValhallaVintageVerbAU64.component",
    "~/Library/Audio/Plug-Ins/Components/ValhallaVintageVerbAU64.component",
    "~/Library/Audio/Plug-Ins/Components/ValhallaVintageVerb.component",
    "~/Library/Audio/Plug-Ins/VST3/ValhallaVintageVerb.vst3",
    "~/.vst3/ValhallaVintageVerb.vst3",
]

VALHALLAROOM_PATHS = [
    "/Library/Audio/Plug-Ins/Components/ValhallaRoomAU64.component",
    "~/Library/Audio/Plug-Ins/Components/ValhallaRoomAU64.component",
    "~/Library/Audio/Plug-Ins/Components/ValhallaRoom.component",
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
# Mode pairings: DuskVerb algorithm <-> VintageVerb color/mode
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
            "decay_time": 1.0,          # Medium decay — match VintageVerb Chamber
            "size": 0.50,               # Medium size
            "diffusion": 0.70,          # Moderate diffusion
            "treble_multiply": 0.55,    # HF damping
            "bass_multiply": 1.0,       # Neutral bass
            "crossover": 1000,
            "mod_depth": 0.30,          # Moderate modulation
            "mod_rate": 0.5,
            "early_ref_level": 0.50,    # Moderate ERs
            "early_ref_size": 0.35,
            "pre_delay": 5.0,           # Small pre-delay
            "lo_cut": 20,
            "hi_cut": 20000,
            "width": 1.0,
        },
        "valhalla": {
            "_reverbmode": 0.1250,      # Room (VV v4.0.5: 22 modes, step 1/24)
            "_colormode": 0.333,        # 1980s (neutral)
            "_decay": 0.10,             # Short-medium decay
            "_size": 0.50,
            "_predelay": 0.02,
            "_diffusion_early": 0.70,
            "_diffusion_late": 0.70,
            "_mod_rate": 0.25,
            "_mod_depth": 0.30,
            "_high_cut": 1.0,
            "_low_cut": 0.0,
            "_bassmult": 0.50,
            "_bassxover": 0.40,
            "_highshelf": 0.0,
            "_highfreq": 0.50,
            "_attack": 0.50,
        },
    },
    {
        "name": "Hall",
        "duskverb": {
            "algorithm": "Hall",
            "decay_time": 2.5,
            "size": 0.70,
            "diffusion": 0.70,
            "treble_multiply": 0.58,
            "bass_multiply": 1.15,
            "crossover": 900,
            "mod_depth": 0.40,
            "mod_rate": 0.8,
            "early_ref_level": 0.55,
            "early_ref_size": 0.50,
            "pre_delay": 30.0,
            "lo_cut": 20,
            "hi_cut": 20000,
            "width": 1.0,
        },
        "valhalla": {
            "_reverbmode": 0.0417,      # Concert Hall (VV v4.0.5)
            "_colormode": 0.333,        # 1980s (neutral)
            "_decay": 0.18,             # Medium decay to match ~2.5s
            "_size": 0.70,
            "_predelay": 0.10,          # ~30ms (normalized)
            "_diffusion_early": 0.80,
            "_diffusion_late": 0.80,
            "_mod_rate": 0.30,
            "_mod_depth": 0.35,
            "_high_cut": 1.0,
            "_low_cut": 0.0,
            "_bassmult": 0.60,
            "_bassxover": 0.40,
            "_highshelf": 0.0,
            "_highfreq": 0.50,
            "_attack": 0.50,
        },
    },
    {
        "name": "Plate",
        "duskverb": {
            "algorithm": "Plate",
            "decay_time": 2.0,
            "size": 0.65,
            "diffusion": 0.80,
            "treble_multiply": 0.70,
            "bass_multiply": 1.0,
            "crossover": 1200,
            "mod_depth": 0.25,
            "mod_rate": 0.6,
            "early_ref_level": 0.0,
            "early_ref_size": 0.0,
            "pre_delay": 0.0,
            "lo_cut": 20,
            "hi_cut": 20000,
            "width": 1.0,
        },
        "valhalla": {
            "_reverbmode": 0.0833,      # Plate (VV v4.0.5)
            "_colormode": 0.333,        # 1980s (neutral)
            "_decay": 0.15,             # Medium decay to match ~2.0s
            "_size": 0.65,
            "_predelay": 0.0,           # No pre-delay
            "_diffusion_early": 0.90,
            "_diffusion_late": 0.90,
            "_mod_rate": 0.20,
            "_mod_depth": 0.25,
            "_high_cut": 1.0,
            "_low_cut": 0.0,
            "_bassmult": 0.50,
            "_bassxover": 0.40,
            "_highshelf": 0.0,
            "_highfreq": 0.50,
            "_attack": 0.50,
        },
    },
    {
        "name": "Chamber",
        "duskverb": {
            "algorithm": "Chamber",
            "decay_time": 1.8,
            "size": 0.60,
            "diffusion": 0.70,
            "treble_multiply": 0.55,
            "bass_multiply": 1.10,
            "crossover": 1000,
            "mod_depth": 0.35,
            "mod_rate": 0.7,
            "early_ref_level": 0.60,
            "early_ref_size": 0.50,
            "pre_delay": 20.0,
            "lo_cut": 20,
            "hi_cut": 20000,
            "width": 1.0,
        },
        "valhalla": {
            "_reverbmode": 0.1667,      # Chamber (VV v4.0.5)
            "_colormode": 0.333,        # 1980s (neutral)
            "_decay": 0.14,             # Medium decay to match ~1.8s
            "_size": 0.60,
            "_predelay": 0.07,          # ~20ms (normalized)
            "_diffusion_early": 0.80,
            "_diffusion_late": 0.80,
            "_mod_rate": 0.25,
            "_mod_depth": 0.30,
            "_high_cut": 1.0,
            "_low_cut": 0.0,
            "_bassmult": 0.55,
            "_bassxover": 0.40,
            "_highshelf": 0.0,
            "_highfreq": 0.50,
            "_attack": 0.50,
        },
    },
    {
        "name": "Ambient",
        "duskverb": {
            "algorithm": "Ambient",
            "decay_time": 6.0,
            "size": 0.85,
            "diffusion": 0.90,
            "treble_multiply": 0.48,
            "bass_multiply": 1.20,
            "crossover": 700,
            "mod_depth": 0.60,
            "mod_rate": 0.9,
            "early_ref_level": 0.0,
            "early_ref_size": 0.0,
            "pre_delay": 40.0,
            "lo_cut": 20,
            "hi_cut": 20000,
            "width": 1.6,
        },
        "valhalla": {
            "_reverbmode": 0.2917,      # Ambience (VV v4.0.5)
            "_colormode": 0.333,        # 1980s (neutral)
            "_decay": 0.35,             # Longer decay to match ~6.0s
            "_size": 0.85,
            "_predelay": 0.13,          # ~40ms (normalized)
            "_diffusion_early": 1.0,
            "_diffusion_late": 1.0,
            "_mod_rate": 0.40,
            "_mod_depth": 0.50,
            "_high_cut": 1.0,
            "_low_cut": 0.0,
            "_bassmult": 0.65,
            "_bassxover": 0.40,
            "_highshelf": 0.0,
            "_highfreq": 0.50,
            "_attack": 0.50,
        },
    },
]

# ---------------------------------------------------------------------------
# VintageVerb parameter name mapping
# ---------------------------------------------------------------------------
# Populated after running: python3 compare_reverbs.py --list-params
# Maps our semantic names (_mode, _decay, etc.) to pedalboard attribute names.
# Update these after discovery:

# VintageVerb parameter mapping (discovered via --list-params)
# All VintageVerb params are 0.0-1.0 normalized.
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
VALHALLA_PARAM_MAP = {
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
# ValhallaRoom parameter name mapping
# ---------------------------------------------------------------------------
# All ValhallaRoom params are 0.0-1.0 normalized.
# Discovered via pedalboard parameter inspection.
#
# Type values (12 types, evenly spaced 0-1):
#   0.000 = Large Room, 0.083 = Medium Room, 0.167 = Bright Room, ...
#
VALHALLAROOM_PARAM_MAP = {
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


def apply_valhallaroom_params(plugin, config):
    """Apply ValhallaRoom parameters using the discovered mapping."""
    for semantic_key, value in config.items():
        if not semantic_key.startswith('_'):
            continue
        mapped = VALHALLAROOM_PARAM_MAP.get(semantic_key)
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


def apply_valhalla_params(plugin, valhalla_config):
    """Apply VintageVerb parameters using the discovered mapping.

    valhalla_config keys start with '_' (semantic names).
    VALHALLA_PARAM_MAP maps them to actual pedalboard attribute names.
    """
    if not VALHALLA_PARAM_MAP:
        print("WARNING: VALHALLA_PARAM_MAP is empty. Run --list-params first.")
        return

    for semantic_key, value in valhalla_config.items():
        if not semantic_key.startswith('_'):
            continue
        mapped = VALHALLA_PARAM_MAP.get(semantic_key)
        if mapped is None:
            print(f"  WARNING: No mapping for {semantic_key}")
            continue
        try:
            setattr(plugin, mapped, value)
        except Exception as e:
            print(f"  WARNING: Failed to set {mapped}={value}: {e}")

    # Always set 100% wet
    mix_attr = VALHALLA_PARAM_MAP.get("_mix")
    if mix_attr:
        try:
            setattr(plugin, mix_attr, 1.0)
        except Exception:
            pass


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
