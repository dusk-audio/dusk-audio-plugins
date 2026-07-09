#!/usr/bin/env python3
"""tune_score.py — compact numeric scoreboard for tuning TapeMachine's A800
mode toward the frozen UAD A800 reference.

Compares the current renders/tapemachine/* against renders/uad/* (which is the
UAD-at-factory-defaults target, rendered once and left untouched) and prints:
  * FR full-curve error (RMS dB, 25 Hz-18 kHz, level-matched)   <- primary knob
  * head-bump freq/gain, HF -3 dB point (mine vs target)
  * THD-vs-level deltas
  * wow/flutter robust pitch deviation
plus a single scalar FR score so a tuning iteration is one number to watch.

Run after (re)rendering mine. No plots — see compare_a800.py for the full report.
"""
import os
import sys
import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from compare_a800 import (freq_response, fr_features, thd_curve, THD_LEVELS,  # noqa: E402
                          RENDERS, STIM)
from wow_flutter import measure_wow_flutter  # noqa: E402


def fr_error_db(speed):
    """RMS dB difference of the two level-matched FR curves, 25 Hz-18 kHz."""
    fm, mm = freq_response(os.path.join(STIM, "sweep.wav"),
                           os.path.join(RENDERS, "tapemachine", str(speed), "sweep.wav"))
    fu, mu = freq_response(os.path.join(STIM, "sweep.wav"),
                           os.path.join(RENDERS, "uad", str(speed), "sweep.wav"))
    # both share the same freq grid (same sweep, same length) -> direct diff
    band = (fm >= 25) & (fm <= 18000)
    diff = mm[band] - mu[band]
    return float(np.sqrt(np.mean(diff ** 2))), fm[band], diff


def score_speed(speed):
    print(f"\n=== {speed} IPS ===")
    err, f, diff = fr_error_db(speed)
    fe_m = fr_features(*freq_response(os.path.join(STIM, "sweep.wav"),
                       os.path.join(RENDERS, "tapemachine", str(speed), "sweep.wav")))
    fe_u = fr_features(*freq_response(os.path.join(STIM, "sweep.wav"),
                       os.path.join(RENDERS, "uad", str(speed), "sweep.wav")))
    print(f"  FR error (RMS dB, 25-18k):  {err:.3f}   <== minimise")
    # where is the curve worst?
    worst = np.argsort(np.abs(diff))[-3:][::-1]
    print("  worst bands:  " + "  ".join(f"{f[i]:.0f}Hz {diff[i]:+.1f}dB" for i in worst))
    print(f"  head bump:  mine {fe_m['head_bump_db']:+.2f}dB@{fe_m['head_bump_hz']:.0f}Hz"
          f"   target {fe_u['head_bump_db']:+.2f}dB@{fe_u['head_bump_hz']:.0f}Hz")
    print(f"  HF -3dB:    mine {fe_m['hf_minus3db_hz']:.0f}Hz   target {fe_u['hf_minus3db_hz']:.0f}Hz")
    # THD
    tm = dict(thd_curve(os.path.join(RENDERS, "tapemachine", str(speed), "thd_steps.wav")))
    tu = dict(thd_curve(os.path.join(RENDERS, "uad", str(speed), "thd_steps.wav")))
    thd_str = "  ".join(f"{lv}:{tm.get(lv,float('nan')):.2f}/{tu.get(lv,float('nan')):.2f}"
                        for lv in THD_LEVELS)
    print(f"  THD% mine/target:  {thd_str}")
    # W&F robust
    wm = measure_wow_flutter(os.path.join(RENDERS, "tapemachine", str(speed), "wf_3150.wav"))
    wu = measure_wow_flutter(os.path.join(RENDERS, "uad", str(speed), "wf_3150.wav"))
    print(f"  W&F robust dev:  mine {wm['pitch_dev_robust_pct']:.3f}%"
          f"   target {wu['pitch_dev_robust_pct']:.3f}%")
    return err


if __name__ == "__main__":
    speeds = [int(s) for s in sys.argv[1:]] or [15, 30]
    errs = [score_speed(s) for s in speeds]
    print(f"\nFR score (mean RMS dB): {np.mean(errs):.3f}")
