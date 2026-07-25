#!/usr/bin/env python3
"""Poly aftertouch gate: MIDI 0xA0 must move ONE voice, not the whole patch.

Polyphonic key pressure is routed per voice (SynthVoice::polyPressure) and the
mod matrix reads max(channel pressure, that voice's key pressure) on its single
Aftertouch source. Two things have to hold for that to be poly aftertouch rather
than a second channel-pressure input:

  * pressure for a note that no voice is playing is DROPPED (a global fallback
    would open the filter on every sounding voice instead), and
  * channel pressure (0xD0) still works unchanged through the same source.

Probe patch: one note, mod slot 0 = Aftertouch -> Filter Cutoff at full amount,
cutoff parked low (400 Hz) so pressure is visible as a large spectral-centroid
move. Measured on the sustained window, which is well past the attack.

Scenarios (48k, 2x OS):
  a. baseline  : no pressure                       -> reference centroid
  b. poly on   : 0xA0 for the played note          -> centroid opens (>1.5x)
  c. poly other: 0xA0 for a note NOT being played  -> centroid unchanged
  d. channel   : 0xD0                              -> centroid opens like (b)
  e. retrigger : pressed note released and played again -> pressure cleared
"""
import sys
import numpy as np
from _harness import render

# Aftertouch is ModSource index 5, Filter Cutoff is ModDest index 5 (ModMatrix.hpp).
PATCH = dict(modSrc0=5, modDst0=5, modAmt0=1.0,
             filterCutoff=400, filterRes=0.2,
             ampS=1.0, ampR=0.3, reverbOn=0, delayOn=0, cosmosChorus=0)

OPEN_RATIO = 1.5    # pressure must move the centroid at least this much
SAME_TOL   = 0.05   # "unchanged" band, fraction of the baseline centroid


def centroid(x, sr, t0, t1):
    seg = x[int(t0 * sr):int(t1 * sr), 0]
    mag = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
    freq = np.fft.rfftfreq(len(seg), 1.0 / sr)
    return float(np.sum(freq * mag) / (np.sum(mag) + 1e-20))


def run(name, **extra):
    sr, x = render(0, 60, 2.0, 2, name, **PATCH, **extra)
    return centroid(x, sr, 1.0, 1.8)


def check(tag, ok, detail):
    print(f"({tag}) {detail}  {'PASS' if ok else 'FAIL'}")
    return [] if ok else [tag]


def main():
    fails = []

    base = run("pat_base")
    print(f"     baseline centroid {base:7.1f} Hz")

    c_poly = run("pat_poly", polyat="0.5:60:1.0")
    fails += check("b", c_poly > OPEN_RATIO * base,
                   f"poly on   : {c_poly:7.1f} Hz (> {OPEN_RATIO:.1f}x baseline "
                   f"= {OPEN_RATIO * base:.1f})")

    c_other = run("pat_other", polyat="0.5:72:1.0")
    fails += check("c", abs(c_other - base) <= SAME_TOL * base,
                   f"poly other: {c_other:7.1f} Hz (unplayed note dropped, "
                   f"want {base:.1f} +-{SAME_TOL * 100:.0f}%)")

    c_chan = run("pat_chan", chanat="0.5:1.0")
    fails += check("d", abs(c_chan - c_poly) <= SAME_TOL * c_poly,
                   f"channel   : {c_chan:7.1f} Hz (0xD0 unchanged, "
                   f"want {c_poly:.1f} +-{SAME_TOL * 100:.0f}%)")

    # Pressure belongs to the note that was pressed: releasing and re-pressing the
    # key must start the new note unpressed, not inherit the old voice's pressure.
    c_retrig = run("pat_retrig", polyat="0.3:60:1.0",
                   noteoff="0.6:60", noteon="0.8:60")
    fails += check("e", abs(c_retrig - base) <= SAME_TOL * base,
                   f"retrigger : {c_retrig:7.1f} Hz (pressure cleared, "
                   f"want {base:.1f} +-{SAME_TOL * 100:.0f}%)")

    print(f"polyat_gate: {'PASS' if not fails else 'FAIL (' + ','.join(fails) + ')'}")
    sys.exit(0 if not fails else 1)


if __name__ == "__main__":
    main()
