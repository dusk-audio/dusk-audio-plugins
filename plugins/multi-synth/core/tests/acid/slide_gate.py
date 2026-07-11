#!/usr/bin/env python3
"""Slide gate — exponential glide time and no-retrigger tie.

Two-step slide C2 (MIDI 36) -> C3 (MIDI 48) with slideTime 60 ms. The measured
f0 glide 10..90 % time must be within +-30 % of 60 ms, and the amplitude must NOT
dip at the slide (the slid note ties: no envelope retrigger).
"""
import sys
import numpy as np
from _acid import render, f0_zerocross

SLIDE_MS = 60.0
TOL = 0.30
F_C2 = 65.406
F_C3 = 130.813
T_SLIDE = 0.5   # second note fires here


def main():
    # Sustained so both notes are clearly audible; slide on the second note.
    # Low cutoff -> near-sine output so zero-crossing f0 is clean at low pitch.
    sr, x = render("voice", "slide",
                   events="0:36:0:0;0.5:48:0:1", off=1.1,
                   wave=0, cutoff=180, res=0.1, envMod=0.0,
                   decay=2.0, sustain=1.0, slideTime=SLIDE_MS, seconds=1.2)

    t, f = f0_zerocross(x, sr)
    # Look at the glide window right after the second note.
    m = (t >= T_SLIDE - 0.02) & (t <= T_SLIDE + 0.4)
    tg, fg = t[m], f[m]
    if len(fg) < 5:
        print("slide_gate: FAIL (insufficient f0 track)")
        sys.exit(1)

    f_start, f_end = F_C2, F_C3
    lo = f_start + 0.10 * (f_end - f_start)
    hi = f_start + 0.90 * (f_end - f_start)

    def cross(freqs, times, level):
        for i in range(1, len(freqs)):
            if (freqs[i - 1] < level) != (freqs[i] < level):
                # linear interp of the crossing time
                a, b = freqs[i - 1], freqs[i]
                frac = (level - a) / (b - a + 1e-20)
                return times[i - 1] + frac * (times[i] - times[i - 1])
        return None

    t10 = cross(fg, tg, lo)
    t90 = cross(fg, tg, hi)
    if t10 is None or t90 is None:
        print(f"slide_gate: FAIL (could not find 10/90 crossings; f0 {fg.min():.1f}..{fg.max():.1f} Hz)")
        sys.exit(1)

    measured = t90 - t10
    target = SLIDE_MS / 1000.0
    err = abs(measured - target) / target
    time_ok = err <= TOL

    # No-retrigger check: envelope amplitude must not dip at the slide instant.
    a = np.abs(x)
    win = int(0.005 * sr)
    pre = np.sqrt(np.mean(a[int((T_SLIDE - 0.02) * sr):int((T_SLIDE - 0.02) * sr) + win] ** 2))
    post_lo = np.min([np.sqrt(np.mean(a[i:i + win] ** 2))
                      for i in range(int(T_SLIDE * sr), int((T_SLIDE + 0.06) * sr), win)])
    dip_db = 20.0 * np.log10((post_lo + 1e-20) / (pre + 1e-20))
    no_dip = dip_db > -3.0   # allow small ripple; a retrigger would drop to ~-inf briefly

    print(f"glide 10-90%: measured {measured*1000:.1f} ms  target {SLIDE_MS:.0f} ms  err {err*100:.0f}% (tol {TOL*100:.0f}%)")
    print(f"amp at slide: {dip_db:+.2f} dB vs pre-slide  (retrigger dip guard > -3 dB)")
    passed = time_ok and no_dip
    print(f"slide_gate: {'PASS' if passed else 'FAIL'}")
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()
