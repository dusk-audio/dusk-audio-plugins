#!/usr/bin/env python3
"""Mode-accuracy routing regression gate.

This does not claim hardware equivalence; it proves that the newly exposed
mode-defining paths are live, finite, and materially change the rendered signal:
complete Oracle Poly-Mod + sync, Modular audio-rate patch points + filter
revision, and the dedicated Cosmos BBD path.
"""

import sys
import numpy as np
from _harness import render


def rms_db(x):
    return 20.0 * np.log10(float(np.sqrt(np.mean(x * x))) + 1.0e-30)


def centroid(x, sr):
    mono = x[:, 0]
    power = np.abs(np.fft.rfft(mono * np.hanning(len(mono)))) ** 2
    freq = np.fft.rfftfreq(len(mono), 1.0 / sr)
    return float(np.sum(freq * power) / (np.sum(power) + 1.0e-30))


def finite(*signals):
    return all(np.all(np.isfinite(x)) for x in signals)


BASE = dict(
    sr=48000, analogAmt=0, vintage=0, masterVol=-12,
    arpOn=0, reverbOn=0, delayOn=0, chorusOn=0, driveOn=0,
    cosmosChorus=0, ampA=0.001, ampD=0.1, ampS=1.0, ampR=0.1,
    filtA=0.001, filtD=0.3, filtS=0.2,
    filterCutoff=1800, filterRes=0.25, filterEnvAmt=0.7,
    osc1Level=1.0, osc2Level=0.7, subLevel=0, noiseLevel=0,
)


def pair(mode, note, name, param, a, b, **patch):
    sr, x0 = render(mode, note, 1.0, 2, name + "_off",
                    **dict(BASE, **patch, **{param: a}))
    _, x1 = render(mode, note, 1.0, 2, name + "_on",
                   **dict(BASE, **patch, **{param: b}))
    return sr, x0, x1


def main():
    rows = []

    sr, a, b = pair(1, 48, "accuracy_oracle_fenv_pwm",
                    "pmFenvPWM", 0, 1, osc1Wave=4, osc2Wave=0)
    rows.append(("Oracle FEnv -> PW", rms_db(a - b), finite(a, b)))

    _, a, b = pair(1, 48, "accuracy_oracle_oscb_filter",
                   "pmOscBFilt", 0, 1, osc1Wave=0, osc2Wave=3, osc2Semi=12)
    rows.append(("Oracle OscB -> filter", rms_db(a - b), finite(a, b)))

    sync_patch = dict(osc1Wave=0, osc1Level=0, osc2Wave=0,
                      osc2Level=1, osc2Semi=19)
    _, a, b = pair(1, 48, "accuracy_oracle_sync",
                   "hardSync", 0, 1, **sync_patch)
    rows.append(("Oracle hard sync", rms_db(a - b), finite(a, b)))

    filter_patch = dict(filterCutoff=20000, filterRes=0.2, filterEnvAmt=0,
                        osc1Wave=0, osc2Level=0, osc3Level=0)
    _, a, b = pair(3, 60, "accuracy_modular_filter",
                   "modFilterModel", 0, 1, **filter_patch)
    filter_delta = abs(centroid(a, sr) - centroid(b, sr))
    rows.append(("Modular early / late", rms_db(a - b),
                 finite(a, b) and filter_delta >= 20.0))

    route_patch = dict(filterCutoff=2500, filterRes=0.3, filterEnvAmt=0,
                       osc1Wave=3, osc1Level=1, osc2Wave=3, osc2Level=0,
                       osc3Level=0, osc2Semi=7)
    _, a, b = pair(3, 48, "accuracy_modular_2_to_1",
                   "modOsc2Osc1", 0, 1, **route_patch)
    rows.append(("Modular Osc2 -> Osc1", rms_db(a - b), finite(a, b)))

    route_patch = dict(filterCutoff=1200, filterRes=0.3, filterEnvAmt=0,
                       osc1Wave=0, osc1Level=1, osc2Level=0,
                       osc3Wave=3, osc3Level=0)
    _, a, b = pair(3, 48, "accuracy_modular_3_to_filter",
                   "modOsc3Filter", 0, 1, **route_patch)
    rows.append(("Modular Osc3 -> filter", rms_db(a - b), finite(a, b)))

    chorus_patch = dict(filterCutoff=10000, filterRes=0.1, filterEnvAmt=0,
                        osc1Wave=0, osc2Level=0)
    _, a, b = pair(0, 60, "accuracy_cosmos_bbd",
                   "cosmosChorus", 0, 3, **chorus_patch)
    stereo = rms_db(b[:, 0] - b[:, 1])
    rows.append(("Cosmos dual BBD", rms_db(a - b),
                 finite(a, b) and stereo > -45.0))

    fails = []
    print(f"{'path':<27}{'null dBFS':>12}   result")
    for name, delta, extra_ok in rows:
        # A null above -55 dBFS is comfortably clear of numerical/filter drift
        # while allowing subtle paths such as the late revision's bandwidth limit.
        ok = extra_ok and delta > -55.0
        if not ok:
            fails.append(name)
        print(f"{name:<27}{delta:>12.2f}   {'PASS' if ok else 'FAIL'}")

    print(f"accuracy_gate: {'PASS' if not fails else 'FAIL (' + ', '.join(fails) + ')'}")
    sys.exit(0 if not fails else 1)


if __name__ == "__main__":
    main()
