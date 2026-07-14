#!/usr/bin/env python3
"""Scan one preset's HP or LP control while jointly refitting drive + repro EQ.

Used for residual response shapes that the four fixed repro bands cannot express
on their own. UAD targets and the repro design matrix are measured once, then
candidate corner frequencies are fitted in parallel.

  python3 shape_fit.py "Fat 456 Master" hp 20 25 30 35 40 45 50
  python3 shape_fit.py "Sunbaked Cassette" lp 4000 5000 6000 7000 8000 10000
"""
import sys
from concurrent.futures import ThreadPoolExecutor

import joint4_fit as jf
import preset_validate as pv


def main():
    if len(sys.argv) < 4 or sys.argv[2] not in ("hp", "lp"):
        raise SystemExit("usage: shape_fit.py PRESET (hp|lp) VALUE [VALUE ...]")
    name, control = sys.argv[1], sys.argv[2]
    values = [float(v) for v in sys.argv[3:]]
    matches = [p for p in pv.parse_presets() if name.lower() in p["name"].lower()]
    if len(matches) != 1:
        raise SystemExit(f"expected one preset match, got {len(matches)}")

    preset = matches[0]
    matrix = jf.design_matrix()
    targets = jf.uad_targets(preset)

    def fit(value):
        candidate = dict(preset)
        candidate[control] = value
        return value, jf.joint(candidate, matrix, targets)

    with ThreadPoolExecutor(max_workers=min(8, len(values))) as executor:
        results = list(executor.map(fit, values))

    print(f"{preset['name']} — {control} scan")
    print(f"{control:>7} {'inG':>6} {'LF':>6} {'LMF':>6} {'HMF':>6} {'HF':>6} "
          f"{'THD':>7} {'UAD':>7} {'maxFR':>6}")
    for value, (gain, bands, thd, uthd, maxfr) in results:
        print(f"{value:7.1f} {gain:+6.1f} {bands['reproLf']:+6.1f} "
              f"{bands['reproLmf']:+6.1f} {bands['reproHmf']:+6.1f} "
              f"{bands['reproHf']:+6.1f} {thd:6.2f}% {uthd:6.2f}% {maxfr:6.2f}")


if __name__ == "__main__":
    main()
