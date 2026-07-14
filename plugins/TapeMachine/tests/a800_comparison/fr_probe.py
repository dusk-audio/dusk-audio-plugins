#!/usr/bin/env python3
"""fr_probe.py — fast Classic102 FR tuning. Renders mine (Classic102, W&F off) for
Repro/Input/Sync and prints the full-band FR vs the clean W&F-off ATR references
(renders/deep_atr/{sw_u_clean,path_Input_u_clean,path_Sync_u_clean}.wav)."""
import os
import sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import deep_probe as dp  # noqa: E402
from compare_a800 import freq_response  # noqa: E402

OUT = os.path.join(HERE, "renders", "deep_atr")
BASE = [("Tape Machine", "1"), ("Tape Speed", "1"), ("Tape Type", "0"), ("EQ Standard", "0"),
        ("Calibration", "1"), ("Noise Amount", "0"), ("Oversampling", "2"), ("Wow", "0"), ("Flutter", "0")]  # idx1 = +6 dB after cal remap
FREQS = [30, 50, 80, 100, 200, 1000, 2000, 5000, 10000, 15000]
SWEEP = os.path.join(HERE, "stimuli", "sweep.wav")


def at(out, f):
    g, mag = freq_response(SWEEP, out)
    return mag[np.argmin(np.abs(g - f))]


def main():
    paths = {"Repro": ("0", "sw_u_clean"), "Input": ("2", "path_Input_u_clean"),
             "Sync": ("1", "path_Sync_u_clean")}
    for pm, (idx, _) in paths.items():
        dp.render(dp.MINE, BASE + [("Signal Path", idx)], "sweep.wav", os.path.join(OUT, f"fr_{pm}_m.wav"))
    for pm, (idx, uc) in paths.items():
        m = os.path.join(OUT, f"fr_{pm}_m.wav"); u = os.path.join(OUT, f"{uc}.wav")
        print(f"\n## {pm}: freq  mine   ATR   diff")
        for f in FREQS:
            mv, uv = at(m, f), at(u, f)
            flag = "  <-- " + ("bright" if mv > uv else "dark") if abs(mv - uv) > 1.0 else ""
            print(f"   {f:6d} {mv:+6.2f} {uv:+6.2f} {mv-uv:+5.2f}{flag}")


if __name__ == "__main__":
    main()
