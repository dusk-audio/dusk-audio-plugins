#!/usr/bin/env python3
"""
Multi-preset DuskVerb vs ValhallaRoom comparison.

Tests DuskVerb Room mode against multiple VRoom presets at different sizes/decays,
printing a summary table of ringing, RT60 match, and level match.
"""

import numpy as np
from pedalboard import load_plugin

from config import (
    SAMPLE_RATE, DUSKVERB_PATHS, VALHALLAROOM_PATHS,
    find_plugin, apply_duskverb_params, apply_valhallaroom_params,
)
from generate_test_signals import make_impulse, make_tone_burst
import reverb_metrics as metrics


# ---------------------------------------------------------------------------
# Preset definitions: matched DuskVerb <-> ValhallaRoom configurations
# ---------------------------------------------------------------------------
PRESETS = [
    {
        "name": "SmallWoodRoom",
        "duskverb": {
            "algorithm": "Room",
            "decay_time": 0.3,
            "size": 0.09,
            "diffusion": 1.0,
            "treble_multiply": 0.60,
            "bass_multiply": 0.80,
            "crossover": 1000,
            "mod_depth": 0.12,
            "mod_rate": 0.5,
            "early_ref_level": 0.25,
            "early_ref_size": 0.03,
            "pre_delay": 0.0,
            "lo_cut": 20,
            "hi_cut": 20000,
            "width": 1.0,
        },
        "valhallaroom": {
            "_type": 0.0833333358,
            "_decay": 0.00500500482,
            "_predelay": 0.0,
            "_latesize": 0.0900000036,
            "_latecross": 0.879999995,
            "_latemodrate": 0.074747473,
            "_latemoddepth": 0.119999997,
            "_diffusion": 1.0,
            "_earlylatemix": 0.741999984,
            "_earlysend": 0.0,
            "_earlysize": 0.0285285283,
            "_earlycross": 0.0,
            "_earlymodrate": 0.206060603,
            "_earlymoddepth": 0.0,
            "_rtbassmultiply": 0.266666681,
            "_rtxover": 0.0979797989,
            "_rthighmultiply": 0.74444443,
            "_rthighxover": 0.493288577,
            "_hicut": 0.617449641,
            "_locut": 0.0,
        },
    },
    {
        "name": "MediumRoom",
        "duskverb": {
            "algorithm": "Room",
            "decay_time": 0.6,
            "size": 0.30,
            "diffusion": 0.85,
            "treble_multiply": 0.55,
            "bass_multiply": 0.90,
            "crossover": 1000,
            "mod_depth": 0.20,
            "mod_rate": 0.4,
            "early_ref_level": 0.35,
            "early_ref_size": 0.10,
            "pre_delay": 5.0,
            "lo_cut": 20,
            "hi_cut": 20000,
            "width": 1.0,
        },
        "valhallaroom": {
            "_type": 0.0833333358,      # Medium Room
            "_decay": 0.05,
            "_predelay": 0.02,
            "_latesize": 0.30,
            "_latecross": 0.70,
            "_latemodrate": 0.10,
            "_latemoddepth": 0.15,
            "_diffusion": 0.85,
            "_earlylatemix": 0.65,
            "_earlysend": 0.10,
            "_earlysize": 0.10,
            "_earlycross": 0.05,
            "_earlymodrate": 0.15,
            "_earlymoddepth": 0.05,
            "_rtbassmultiply": 0.40,
            "_rtxover": 0.12,
            "_rthighmultiply": 0.65,
            "_rthighxover": 0.50,
            "_hicut": 0.70,
            "_locut": 0.0,
        },
    },
    {
        "name": "LargeRoom",
        "duskverb": {
            "algorithm": "Room",
            "decay_time": 1.2,
            "size": 0.60,
            "diffusion": 0.80,
            "treble_multiply": 0.50,
            "bass_multiply": 1.0,
            "crossover": 900,
            "mod_depth": 0.30,
            "mod_rate": 0.35,
            "early_ref_level": 0.40,
            "early_ref_size": 0.25,
            "pre_delay": 12.0,
            "lo_cut": 20,
            "hi_cut": 20000,
            "width": 1.0,
        },
        "valhallaroom": {
            "_type": 0.0,               # Large Room
            "_decay": 0.12,
            "_predelay": 0.05,
            "_latesize": 0.55,
            "_latecross": 0.60,
            "_latemodrate": 0.08,
            "_latemoddepth": 0.20,
            "_diffusion": 0.80,
            "_earlylatemix": 0.55,
            "_earlysend": 0.15,
            "_earlysize": 0.20,
            "_earlycross": 0.10,
            "_earlymodrate": 0.12,
            "_earlymoddepth": 0.05,
            "_rtbassmultiply": 0.50,
            "_rtxover": 0.15,
            "_rthighmultiply": 0.60,
            "_rthighxover": 0.45,
            "_hicut": 0.75,
            "_locut": 0.0,
        },
    },
    {
        "name": "BrightRoom",
        "duskverb": {
            "algorithm": "Room",
            "decay_time": 0.5,
            "size": 0.20,
            "diffusion": 0.90,
            "treble_multiply": 0.80,
            "bass_multiply": 0.70,
            "crossover": 1200,
            "mod_depth": 0.15,
            "mod_rate": 0.5,
            "early_ref_level": 0.30,
            "early_ref_size": 0.06,
            "pre_delay": 2.0,
            "lo_cut": 20,
            "hi_cut": 20000,
            "width": 1.0,
        },
        "valhallaroom": {
            "_type": 0.1666666716,      # Bright Room
            "_decay": 0.03,
            "_predelay": 0.01,
            "_latesize": 0.20,
            "_latecross": 0.75,
            "_latemodrate": 0.12,
            "_latemoddepth": 0.10,
            "_diffusion": 0.90,
            "_earlylatemix": 0.70,
            "_earlysend": 0.05,
            "_earlysize": 0.06,
            "_earlycross": 0.0,
            "_earlymodrate": 0.18,
            "_earlymoddepth": 0.03,
            "_rtbassmultiply": 0.30,
            "_rtxover": 0.10,
            "_rthighmultiply": 0.85,
            "_rthighxover": 0.55,
            "_hicut": 0.85,
            "_locut": 0.0,
        },
    },
]


def process_stereo(plugin, mono_signal, sr):
    stereo_in = np.stack([mono_signal, mono_signal], axis=0).astype(np.float32)
    output = plugin(stereo_in, sr)
    return output[0], output[1]


def flush_plugin(plugin, sr, duration_sec=2.0):
    silence = np.zeros(int(sr * duration_sec), dtype=np.float32)
    process_stereo(plugin, silence, sr)


def main():
    sr = SAMPLE_RATE

    # Load plugins
    dv_path = find_plugin(DUSKVERB_PATHS)
    vr_path = find_plugin(VALHALLAROOM_PATHS)

    if not dv_path:
        print("ERROR: DuskVerb not found. Build it first.")
        return
    if not vr_path:
        print("ERROR: ValhallaRoom not found.")
        return

    print(f"Loading DuskVerb: {dv_path}")
    duskverb = load_plugin(dv_path)
    print(f"Loading ValhallaRoom: {vr_path}")
    valhallaroom = load_plugin(vr_path)

    # Test signals
    impulse = make_impulse()
    tone = make_tone_burst(1500)

    results = []

    for preset in PRESETS:
        name = preset["name"]
        print(f"\n--- Testing: {name} ---")

        # Configure and flush DuskVerb
        apply_duskverb_params(duskverb, preset["duskverb"])
        flush_plugin(duskverb, sr)

        # Configure and flush ValhallaRoom
        apply_valhallaroom_params(valhallaroom, preset["valhallaroom"])
        flush_plugin(valhallaroom, sr)

        # Process impulse
        dv_imp_l, dv_imp_r = process_stereo(duskverb, impulse, sr)
        flush_plugin(duskverb, sr)
        vr_imp_l, vr_imp_r = process_stereo(valhallaroom, impulse, sr)
        flush_plugin(valhallaroom, sr)

        # Process tone burst
        dv_tone_l, _ = process_stereo(duskverb, tone, sr)
        flush_plugin(duskverb, sr)
        vr_tone_l, _ = process_stereo(valhallaroom, tone, sr)
        flush_plugin(valhallaroom, sr)

        # Analyze impulse ringing
        dv_imp_ring = metrics.detect_modal_resonances(dv_imp_l, sr)
        vr_imp_ring = metrics.detect_modal_resonances(vr_imp_l, sr)

        # Analyze tone burst ringing
        dv_tone_ring = metrics.detect_modal_resonances(
            dv_tone_l, sr, time_windows=[(0.05, 0.15), (0.15, 0.4), (0.4, 1.0)])
        vr_tone_ring = metrics.detect_modal_resonances(
            vr_tone_l, sr, time_windows=[(0.05, 0.15), (0.15, 0.4), (0.4, 1.0)])

        # RT60
        dv_rt60 = metrics.measure_rt60_per_band(dv_imp_l, sr)
        vr_rt60 = metrics.measure_rt60_per_band(vr_imp_l, sr)

        # Level comparison
        dv_rms = float(np.sqrt(np.mean(dv_imp_l[:int(sr * 0.5)] ** 2)))
        vr_rms = float(np.sqrt(np.mean(vr_imp_l[:int(sr * 0.5)] ** 2)))
        level_diff = 20 * np.log10(dv_rms / max(vr_rms, 1e-10))

        # Stereo decorrelation
        dv_corr = float(np.corrcoef(dv_imp_l[:int(sr * 2)], dv_imp_r[:int(sr * 2)])[0, 1])
        vr_corr = float(np.corrcoef(vr_imp_l[:int(sr * 2)], vr_imp_r[:int(sr * 2)])[0, 1])

        # RT60 at key bands
        rt60_500_dv = dv_rt60.get("500 Hz")
        rt60_500_vr = vr_rt60.get("500 Hz")
        rt60_2k_dv = dv_rt60.get("2 kHz")
        rt60_2k_vr = vr_rt60.get("2 kHz")
        rt60_4k_dv = dv_rt60.get("4 kHz")
        rt60_4k_vr = vr_rt60.get("4 kHz")

        row = {
            "name": name,
            "dv_imp_ring": dv_imp_ring["max_peak_prominence_db"],
            "dv_imp_freq": dv_imp_ring["worst_freq_hz"],
            "vr_imp_ring": vr_imp_ring["max_peak_prominence_db"],
            "vr_imp_freq": vr_imp_ring["worst_freq_hz"],
            "dv_tone_ring": dv_tone_ring["max_peak_prominence_db"],
            "vr_tone_ring": vr_tone_ring["max_peak_prominence_db"],
            "level_diff": level_diff,
            "dv_corr": dv_corr,
            "vr_corr": vr_corr,
            "rt60_500_dv": rt60_500_dv,
            "rt60_500_vr": rt60_500_vr,
            "rt60_2k_dv": rt60_2k_dv,
            "rt60_2k_vr": rt60_2k_vr,
            "rt60_4k_dv": rt60_4k_dv,
            "rt60_4k_vr": rt60_4k_vr,
            "dv_persistent": len(dv_imp_ring.get("persistent_peaks", [])),
            "vr_persistent": len(vr_imp_ring.get("persistent_peaks", [])),
        }
        results.append(row)

        # Print per-preset detail
        print(f"  Impulse ring: DV {row['dv_imp_ring']:.1f}dB @ {row['dv_imp_freq']:.0f}Hz | "
              f"VR {row['vr_imp_ring']:.1f}dB @ {row['vr_imp_freq']:.0f}Hz")
        print(f"  Tone ring:    DV {row['dv_tone_ring']:.1f}dB | VR {row['vr_tone_ring']:.1f}dB")
        print(f"  Level diff:   {row['level_diff']:+.1f} dB (DV vs VR)")
        print(f"  Stereo corr:  DV {row['dv_corr']:.3f} | VR {row['vr_corr']:.3f}")

    # Summary table
    print("\n" + "=" * 110)
    print("  MULTI-PRESET SUMMARY: DuskVerb (Dattorro Tank) vs ValhallaRoom")
    print("=" * 110)

    header = (f"  {'Preset':<16s} "
              f"{'DV Imp':>7s} {'VR Imp':>7s} "
              f"{'DV Tone':>8s} {'VR Tone':>8s} "
              f"{'Level':>7s} "
              f"{'RT60@500':>9s} {'RT60@2k':>9s} {'RT60@4k':>9s} "
              f"{'DV Corr':>8s} {'VR Corr':>8s}")
    print(header)
    print("  " + "-" * (len(header) - 2))

    for r in results:
        rt60_500 = ""
        if r["rt60_500_dv"] and r["rt60_500_vr"]:
            ratio = r["rt60_500_dv"] / r["rt60_500_vr"] if r["rt60_500_vr"] > 0 else 0
            rt60_500 = f"{ratio:.2f}x"
        rt60_2k = ""
        if r["rt60_2k_dv"] and r["rt60_2k_vr"]:
            ratio = r["rt60_2k_dv"] / r["rt60_2k_vr"] if r["rt60_2k_vr"] > 0 else 0
            rt60_2k = f"{ratio:.2f}x"
        rt60_4k = ""
        if r["rt60_4k_dv"] and r["rt60_4k_vr"]:
            ratio = r["rt60_4k_dv"] / r["rt60_4k_vr"] if r["rt60_4k_vr"] > 0 else 0
            rt60_4k = f"{ratio:.2f}x"

        line = (f"  {r['name']:<16s} "
                f"{r['dv_imp_ring']:>6.1f}dB {r['vr_imp_ring']:>6.1f}dB "
                f"{r['dv_tone_ring']:>7.1f}dB {r['vr_tone_ring']:>7.1f}dB "
                f"{r['level_diff']:>+6.1f}dB "
                f"{rt60_500:>9s} {rt60_2k:>9s} {rt60_4k:>9s} "
                f"{r['dv_corr']:>8.3f} {r['vr_corr']:>8.3f}")
        print(line)

    # Averages
    print("  " + "-" * (len(header) - 2))
    avg_dv_imp = np.mean([r["dv_imp_ring"] for r in results])
    avg_vr_imp = np.mean([r["vr_imp_ring"] for r in results])
    avg_dv_tone = np.mean([r["dv_tone_ring"] for r in results])
    avg_vr_tone = np.mean([r["vr_tone_ring"] for r in results])
    avg_level = np.mean([r["level_diff"] for r in results])
    print(f"  {'AVERAGE':<16s} "
          f"{avg_dv_imp:>6.1f}dB {avg_vr_imp:>6.1f}dB "
          f"{avg_dv_tone:>7.1f}dB {avg_vr_tone:>7.1f}dB "
          f"{avg_level:>+6.1f}dB")

    print(f"\n  Key:")
    print(f"    Imp = Impulse ringing (lower is better, target <12 dB)")
    print(f"    Tone = 1500Hz tone burst ringing (lower is better)")
    print(f"    Level = DV output level relative to VR (0 = matched)")
    print(f"    RT60@X = DV/VR decay ratio at frequency X (1.0 = matched)")
    print(f"    Corr = Stereo correlation (closer to 0 = better decorrelation)")

    print("\nDone.")


if __name__ == "__main__":
    main()
