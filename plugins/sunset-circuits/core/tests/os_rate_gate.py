#!/usr/bin/env python3
"""Rate-aware Oversampling gate (#303, owner ruling 2026-11-17).

The Oversampling setting names an internal rate: 1x, 2x and 4x mean about 48,
96 and 192 kHz inside (MultiSynthDSP::factorForSetting). So the internal factor
depends on the session rate: 44.1 / 48 kHz sessions run 1, 2, 4; 88.2 / 96 kHz
run 1, 1, 2; 176.4 / 192 kHz run 1, 1, 1. This is the CPU side of the rule that
the Oracle at its default setting may cost no more than the instrument it is
modeled on at 44.1, 48 and 96 kHz: before it, 2x in a 96 kHz session ran the
voices at 192 kHz and cost 15.9 to 20.7 % of a core against the reference's
15.1 to 17.6 %; at 1x it costs 8.3 to 10.8 % for the same aliasing level as 2x
in a 48 kHz session.

Each row renders one Oracle note at a session rate and setting with the
oversampling log on, and reads the factor the engine committed.
"""
import os
import sys

from _harness import OUT, render

EXPECTED = {44100: (1, 2, 4), 48000: (1, 2, 4), 88200: (1, 1, 2), 96000: (1, 1, 2),
            176400: (1, 1, 1), 192000: (1, 1, 1)}


def main():
    fails = 0
    for rate, factors in EXPECTED.items():
        got = []
        for setting, want in zip((1, 2, 4), factors):
            log = os.path.join(OUT, f"osrate_{rate}_{setting}.log")
            render(1, 69, 0.2, setting, f"osrate_{rate}_{setting}", sr=rate, oslog=log)
            with open(log) as f:
                committed = {int(line.split()[1]) for line in f if line.strip()}
            ok = committed == {want}
            fails += not ok
            got.append(f"{setting}x -> {sorted(committed)} (want {want}) {'PASS' if ok else 'FAIL'}")
        print(f"  {rate:>6} Hz  " + "   ".join(got))
    print(f"os_rate_gate: {'PASS' if not fails else f'FAIL ({fails} rows)'}")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
