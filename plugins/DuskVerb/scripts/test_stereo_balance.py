#!/usr/bin/env python3
"""Compare L/R stereo balance between DuskVerb and VintageVerb for Fat Snare Hall."""

import numpy as np
import os
from pedalboard import load_plugin

SAMPLE_RATE = 48000

# Fat Snare Hall VV raw params
VV_PARAMS = {
    "ReverbMode": 0.0417,
    "Size": 0.568,
    "Decay": 0.18,
    "PreDelay": 0.193,
    "EarlyDiffusion": 1.0,
    "LateDiffusion": 1.0,
    "ModRate": 0.03,
    "ModDepth": 1.0,
    "HighCut": 0.35,
    "LowCut": 0.0,
    "BassMult": 0.55,
    "BassXover": 0.4,
    "HighShelf": 0.0,
    "HighFreq": 0.592,
    "ColorMode": 0.333,
    "Attack": 0.0,
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


def analyze_stereo(ir_l, ir_r, sr, label, windows_ms=None):
    if windows_ms is None:
        windows_ms = [(0, 50), (0, 100), (0, 200), (0, 500), (50, 200), (200, 1000)]

    print(f"\n{'='*60}")
    print(f"  {label}")
    print(f"{'='*60}")

    # Overall RMS
    rms_l = float(np.sqrt(np.mean(ir_l ** 2)))
    rms_r = float(np.sqrt(np.mean(ir_r ** 2)))
    if rms_l > 1e-10 and rms_r > 1e-10:
        lr_db = 20 * np.log10(rms_l / rms_r)
        print(f"  Overall:  L={rms_l:.6f}  R={rms_r:.6f}  L/R={lr_db:+.2f} dB")

    # Per-window analysis
    for start_ms, end_ms in windows_ms:
        s = int(start_ms * sr / 1000)
        e = min(int(end_ms * sr / 1000), len(ir_l))
        if s >= e:
            continue
        seg_l = ir_l[s:e]
        seg_r = ir_r[s:e]
        rms_l_w = float(np.sqrt(np.mean(seg_l ** 2)))
        rms_r_w = float(np.sqrt(np.mean(seg_r ** 2)))
        if rms_l_w > 1e-10 and rms_r_w > 1e-10:
            lr_db = 20 * np.log10(rms_l_w / rms_r_w)
            print(f"  {start_ms:4d}-{end_ms:4d}ms: L={rms_l_w:.6f}  R={rms_r_w:.6f}  L/R={lr_db:+.2f} dB")

    # Cross-correlation for L/R timing offset
    max_lag = int(5 * sr / 1000)  # ±5ms
    window = min(int(0.2 * sr), len(ir_l))
    seg_l = ir_l[:window].astype(np.float64)
    seg_r = ir_r[:window].astype(np.float64)
    best_lag = 0
    best_corr = -1
    for lag in range(-max_lag, max_lag + 1):
        if lag >= 0:
            corr = np.sum(seg_l[lag:] * seg_r[:len(seg_l) - lag])
        else:
            corr = np.sum(seg_l[:len(seg_l) + lag] * seg_r[-lag:])
        if corr > best_corr:
            best_corr = corr
            best_lag = lag
    lag_ms = best_lag / sr * 1000
    print(f"  L/R cross-corr peak lag: {lag_ms:+.2f} ms (positive = R leads)")

    # Energy onset: first sample above -40dB of peak
    peak = max(np.max(np.abs(ir_l)), np.max(np.abs(ir_r)))
    threshold = peak * 0.01  # -40dB
    above_l = np.nonzero(np.abs(ir_l) > threshold)[0]
    above_r = np.nonzero(np.abs(ir_r) > threshold)[0]
    onset_l = above_l[0] if len(above_l) > 0 else len(ir_l)
    onset_r = above_r[0] if len(above_r) > 0 else len(ir_r)
    onset_diff_ms = (onset_r - onset_l) / sr * 1000
    print(f"  Energy onset: L={onset_l/sr*1000:.2f}ms  R={onset_r/sr*1000:.2f}ms  diff={onset_diff_ms:+.2f}ms")


def main():
    # Load VV
    vv_paths = [
        "/Library/Audio/Plug-Ins/Components/ValhallaVintageVerbAU64.component",
        os.path.expanduser("~/Library/Audio/Plug-Ins/Components/ValhallaVintageVerbAU64.component"),
    ]
    vv = None
    for p in vv_paths:
        if os.path.exists(p):
            print(f"Loading VV: {p}")
            vv = load_plugin(p)
            break

    # Load DV
    dv_paths = [
        os.path.expanduser("~/Library/Audio/Plug-Ins/Components/DuskVerb.component"),
        "/Library/Audio/Plug-Ins/Components/DuskVerb.component",
    ]
    dv = None
    for p in dv_paths:
        if os.path.exists(p):
            print(f"Loading DV: {p}")
            dv = load_plugin(p)
            break

    if not vv or not dv:
        print("ERROR: Need both plugins")
        return

    # Configure VV
    vv.reverbmode = VV_PARAMS["ReverbMode"]
    for key, val in VV_PARAMS.items():
        if key == "ReverbMode":
            continue
        try:
            setattr(vv, key.lower(), val)
        except Exception as e:
            print(f"  VV: skip {key}: {e}")
    vv.mix = 1.0

    # Capture VV IR
    print("\nCapturing VV IR...")
    vv_l, vv_r = capture_stereo_ir(vv, SAMPLE_RATE)
    analyze_stereo(vv_l, vv_r, SAMPLE_RATE, "VintageVerb - Fat Snare Hall")

    # Configure DV with same preset values
    dv.algorithm = "Hall"
    dv.size = 0.568
    dv.decay_time = 0.55  # calibrated
    dv.pre_delay = 11.2
    dv.treble_multiply = 1.0
    dv.bass_multiply = 1.2
    dv.crossover = 348.9
    dv.diffusion = 1.0
    dv.mod_depth = 1.0
    dv.mod_rate = 0.11
    dv.early_ref_level = 0.13
    dv.early_ref_size = 0.56
    dv.lo_cut = 20.0
    dv.hi_cut = 1000.0
    dv.width = 1.0
    dv.mix = 1.0

    # Capture DV IR
    print("\nCapturing DV IR...")
    dv_l, dv_r = capture_stereo_ir(dv, SAMPLE_RATE)
    analyze_stereo(dv_l, dv_r, SAMPLE_RATE, "DuskVerb - Fat Snare Hall")

    # Also test with ER disabled to isolate FDN
    dv.early_ref_level = 0.0
    print("\nCapturing DV IR (ERs disabled)...")
    dv_l2, dv_r2 = capture_stereo_ir(dv, SAMPLE_RATE)
    analyze_stereo(dv_l2, dv_r2, SAMPLE_RATE, "DuskVerb - Fat Snare Hall (no ERs)")


if __name__ == "__main__":
    main()
