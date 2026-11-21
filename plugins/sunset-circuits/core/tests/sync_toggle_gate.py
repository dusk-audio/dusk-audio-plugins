#!/usr/bin/env python3
"""Hard sync can be switched on and off without a glitch (#303, review round 1).

The synced render puts both halves of each band-limited step down itself (the
second in a carry for the next sample); the free render puts the half before an
edge on the sample before it and the half after it on the sample after. Switching
from one to the other used to add the half after an edge twice (sync off) or
drop it (sync on): a step of up to a quarter of full scale at 1x.

Every row renders an Oracle saw, square or pulse on osc 1 with osc 2 on the same
pitch and phase, so a sync reset coincides with osc 1's own wrap and syncing it
changes nothing; then the same note with Sync switched 120 times (every 7.31 ms,
16-sample blocks). The two renders must agree to 1e-3 of their peak.

    worst |toggled - never synced| / peak     1x        2x        4x
    before (saw / square, pulse)              4e-2 / 2.5e-1   7e-6 / 6.9e-2   1e-5 / 1e-5
    now                                       9e-6 / 1e-5     7e-6 / 9e-6     1e-5 / 1e-5

The triangle is not in this gate: its synced integrator restarts every cycle,
so it sits on a different DC from the free one by design. Both now read the
integrator before its update, so switching neither repeats nor skips a sample
(the free triangle used to read after it, one sample ahead of its phase).
"""
import sys

import numpy as np

from _harness import render

BAR = 1e-3
BASE = dict(osc2Level=0, osc2Semi=0, osc2Detune=0, osc1Level=1, filterCutoff=20000, filterRes=0,
            filterEnvAmt=0, analogAmt=0, unisonVoices=1, ampA=0.005, ampS=1, masterVol=-12,
            block=16, sr=48000, hardSync=0)
TOGGLES = ["%.5f:hardSync:%d" % (0.2 + 0.00731 * k, (k + 1) % 2) for k in range(120)]


def main():
    fails = 0
    for wave, name in ((0, "saw"), (1, "square"), (4, "pulse")):
        for os in (1, 2, 4):
            p = dict(BASE, osc1Wave=wave, osc2Wave=wave)
            _, free = render(1, 84, 1.2, os, f"st_free_{wave}_{os}", **p)
            _, tog = render(1, 84, 1.2, os, f"st_tog_{wave}_{os}", setat=TOGGLES, **p)
            err = float(np.max(np.abs(tog[:, 0] - free[:, 0])) / np.max(np.abs(free[:, 0])))
            ok = err <= BAR
            fails += not ok
            print(f"  {name:<7} {os}x  worst difference {err:.1e} of peak   {'PASS' if ok else 'FAIL'}")
    print(f"sync_toggle_gate: {'PASS' if not fails else f'FAIL ({fails} rows)'}")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
