#!/usr/bin/env python3
"""Check L/R stereo balance across all DuskVerb algorithms vs VintageVerb."""

import json
import numpy as np
import os
from pedalboard import load_plugin

SAMPLE_RATE = 48000

VV_MODE_TO_DV = {
    0.0417: "Hall",
    0.0833: "Plate",
    0.1250: "Room",
    0.1667: "Chamber",
    0.2917: "Ambient",
}

# Pick one representative preset per mode
REPRESENTATIVE_PRESETS = {
    "Concert Hall": "Fat Snare Hall",
    "Plate": "Vocal Plate",
    "Room": "Fat Snare Room",
    "Chamber": "Clear Chamber",
    "Ambience": "Ambience",
}


def capture_stereo_ir(plugin, sr, duration=2.0, flush_dur=2.0):
    silence = np.zeros(int(sr * flush_dur), dtype=np.float32)
    stereo_silence = np.stack([silence, silence], axis=0)
    plugin(stereo_silence, sr)
    n = int(sr * duration)
    impulse = np.zeros(n, dtype=np.float32)
    impulse[0] = 1.0
    stereo_in = np.stack([impulse, impulse], axis=0)
    out = plugin(stereo_in, sr)
    return out[0], out[1]


def measure_lr_stats(ir_l, ir_r, sr):
    """Return key L/R balance metrics."""
    # Overall level
    rms_l = float(np.sqrt(np.mean(ir_l ** 2)))
    rms_r = float(np.sqrt(np.mean(ir_r ** 2)))
    level_db = 20 * np.log10(max(rms_l, 1e-10) / max(rms_r, 1e-10))

    # Cross-correlation for timing offset (±10ms search)
    max_lag = int(10 * sr / 1000)
    window = min(int(0.2 * sr), len(ir_l))
    seg_l = ir_l[:window].astype(np.float64)
    seg_r = ir_r[:window].astype(np.float64)
    best_lag = 0
    best_corr = -1e30
    for lag in range(-max_lag, max_lag + 1):
        if lag >= 0:
            corr = np.sum(seg_l[lag:] * seg_r[:len(seg_l) - lag])
        else:
            corr = np.sum(seg_l[:len(seg_l) + lag] * seg_r[-lag:])
        if corr > best_corr:
            best_corr = corr
            best_lag = lag
    lag_ms = best_lag / sr * 1000

    # Per-window level balance
    windows = [(0, 100), (100, 500), (500, 1500)]
    window_dbs = {}
    for s_ms, e_ms in windows:
        s = int(s_ms * sr / 1000)
        e = min(int(e_ms * sr / 1000), len(ir_l))
        if s >= e:
            continue
        rl = float(np.sqrt(np.mean(ir_l[s:e] ** 2)))
        rr = float(np.sqrt(np.mean(ir_r[s:e] ** 2)))
        if rl > 1e-10 and rr > 1e-10:
            window_dbs[f"{s_ms}-{e_ms}ms"] = 20 * np.log10(rl / rr)

    return level_db, lag_ms, window_dbs


def main():
    # Load presets
    json_path = os.path.join(os.path.dirname(__file__),
                             "..", "tests", "reference_comparison", "qualifying_presets.json")
    with open(json_path) as f:
        all_presets = json.load(f)

    # Load plugins
    vv_paths = [
        "/Library/Audio/Plug-Ins/Components/ValhallaVintageVerbAU64.component",
        os.path.expanduser("~/Library/Audio/Plug-Ins/Components/ValhallaVintageVerbAU64.component"),
    ]
    dv_paths = [
        os.path.expanduser("~/Library/Audio/Plug-Ins/Components/DuskVerb.component"),
        "/Library/Audio/Plug-Ins/Components/DuskVerb.component",
    ]

    vv = dv = None
    for p in vv_paths:
        if os.path.exists(p):
            print(f"Loading VV: {p}")
            vv = load_plugin(p)
            break
    for p in dv_paths:
        if os.path.exists(p):
            print(f"Loading DV: {p}")
            dv = load_plugin(p)
            break
    if not vv or not dv:
        print("ERROR: Need both plugins")
        return

    # VV param key map
    vv_param_keys = {
        "ReverbMode": "reverbmode", "ColorMode": "colormode", "Decay": "decay",
        "Size": "size", "PreDelay": "predelay", "EarlyDiffusion": "earlydiffusion",
        "LateDiffusion": "latediffusion", "ModRate": "modrate", "ModDepth": "moddepth",
        "HighCut": "highcut", "LowCut": "lowcut", "BassMult": "bassmult",
        "BassXover": "bassxover", "HighShelf": "highshelf", "HighFreq": "highfreq",
        "Attack": "attack",
    }

    print(f"\n{'Plugin':<6s} {'Mode':<14s} {'Preset':<30s} {'L/R dB':>7s} {'Lag ms':>8s} {'0-100ms':>8s} {'100-500ms':>10s} {'500-1500ms':>11s}")
    print("-" * 105)

    for mode_float, dv_algo in VV_MODE_TO_DV.items():
        mode_name = {0.0417: "Concert Hall", 0.0833: "Plate", 0.1250: "Room",
                     0.1667: "Chamber", 0.2917: "Ambience"}[mode_float]

        # Find the representative preset
        target_name = REPRESENTATIVE_PRESETS[mode_name]
        preset = None
        for p in all_presets:
            if p["name"] == target_name and p["mode_float"] == mode_float:
                preset = p
                break
        if not preset:
            print(f"  Preset '{target_name}' not found for mode {mode_name}")
            continue

        vv_params = preset["params"]

        # Configure and measure VV
        vv.reverbmode = mode_float
        for vv_key, attr in vv_param_keys.items():
            if vv_key == "ReverbMode":
                continue
            if vv_key in vv_params:
                try:
                    setattr(vv, attr, vv_params[vv_key])
                except Exception:
                    pass
        vv.mix = 1.0

        vv_l, vv_r = capture_stereo_ir(vv, SAMPLE_RATE)
        vv_level, vv_lag, vv_wins = measure_lr_stats(vv_l, vv_r, SAMPLE_RATE)

        w1 = vv_wins.get("0-100ms", 0)
        w2 = vv_wins.get("100-500ms", 0)
        w3 = vv_wins.get("500-1500ms", 0)
        print(f"{'VV':<6s} {mode_name:<14s} {target_name:<30s} {vv_level:>+6.1f}dB {vv_lag:>+7.2f}ms {w1:>+7.1f}dB {w2:>+9.1f}dB {w3:>+10.1f}dB")

        # Configure and measure DV
        dv.algorithm = dv_algo
        dv.size = vv_params.get("Size", 0.5)
        dv.decay_time = 1.0 + vv_params.get("Decay", 0.3) * 10
        dv.pre_delay = 300 * vv_params.get("PreDelay", 0.0) ** 2
        dv.treble_multiply = 0.7
        dv.bass_multiply = 1.0
        dv.crossover = 500
        dv.diffusion = max(vv_params.get("EarlyDiffusion", 0.7), vv_params.get("LateDiffusion", 0.7))
        dv.mod_depth = vv_params.get("ModDepth", 0.3)
        dv.mod_rate = 0.1 * 100 ** vv_params.get("ModRate", 0.3)
        dv.early_ref_level = 0.3 if dv_algo in ("Hall", "Room", "Chamber") else 0.0
        dv.early_ref_size = 0.5
        dv.lo_cut = 20
        dv.hi_cut = max(1000, min(20000, 200 * 100 ** vv_params.get("HighCut", 1.0)))
        dv.width = 1.0
        dv.mix = 1.0

        dv_l, dv_r = capture_stereo_ir(dv, SAMPLE_RATE)
        dv_level, dv_lag, dv_wins = measure_lr_stats(dv_l, dv_r, SAMPLE_RATE)

        w1 = dv_wins.get("0-100ms", 0)
        w2 = dv_wins.get("100-500ms", 0)
        w3 = dv_wins.get("500-1500ms", 0)
        print(f"{'DV':<6s} {dv_algo:<14s} {'':<30s} {dv_level:>+6.1f}dB {dv_lag:>+7.2f}ms {w1:>+7.1f}dB {w2:>+9.1f}dB {w3:>+10.1f}dB")
        print()


if __name__ == "__main__":
    main()
