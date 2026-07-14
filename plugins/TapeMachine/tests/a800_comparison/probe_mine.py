#!/usr/bin/env python3
"""probe_mine.py — fast Classic102-only tuning probe. Renders ONLY my AU
(Classic102) for the key stimuli and prints harmonics / THD / IMD / crest / path
HF / aliasing next to the ATR-102 targets already rendered in renders/deep_atr/
(the *_u.wav from a prior `DEEP_MACHINE=ATR102 deep_probe.py`, which don't change).
Much faster than re-rendering the UAD side every iteration.

  python3 probe_mine.py            # render mine + report vs cached ATR
"""
import os
import sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import deep_probe as dp  # noqa: E402

OUT = os.path.join(HERE, "renders", "deep_atr")
MINE = dp.MINE
# Classic102 reference config (== deep_probe MINE_BASE with machine idx 1)
BASE = [("Tape Machine", "1"), ("Tape Speed", "1"), ("Tape Type", "0"),
        ("EQ Standard", "0"), ("Signal Path", "0"), ("Calibration", "1"),  # idx1 = +6 dB after cal remap
        ("Noise Amount", "0"), ("Oversampling", "2"), ("Wow", "0"), ("Flutter", "0")]


def r(tag, inp, extra=None):
    d = os.path.join(OUT, f"{tag}_m.wav")
    base = BASE if not extra else [p for p in BASE if p[0] not in {e[0] for e in extra}] + extra
    dp.render(MINE, base, inp, d)
    return d


def main():
    from compare_a800 import thd_curve
    print("Rendering Classic102 (mine) only...")
    harm = r("harm", "thd_steps.wav")
    imd = r("imd", "imd.wav")
    tr = r("tr", "transient.wav")
    sw = r("sw", "sweep.wav")
    hotsw = r("hotsw", "hot_sweep.wav")
    alias = r("alias", "alias.wav")
    paths = {pm: r(f"path_{pm}", "sweep.wav", [("Signal Path", idx)])
             for pm, idx in (("Input", "2"), ("Sync", "1"), ("Repro", "0"))}
    U = lambda t: os.path.join(OUT, f"{t}_u.wav")  # noqa: E731  cached ATR targets

    print("\n# Classic102 (mine) vs ATR-102 target — 456/NAB/15/Repro\n")
    hm, om, em = dp.harmonics(harm)
    hu, ou, eu = dp.harmonics(U("harm"))
    print("## 2. Harmonics @-6 dBFS (rel fundamental)")
    print("  mine: " + " ".join(f"{h}f={hm[h]:+.0f}" for h in range(2, 7)) +
          f"   odd(3,5)={om:+.1f}  even(2,4,6)={em:+.1f}")
    print("  ATR : " + " ".join(f"{h}f={hu[h]:+.0f}" for h in range(2, 7)) +
          f"   odd(3,5)={ou:+.1f}  even(2,4,6)={eu:+.1f}")
    tm = dict(thd_curve(harm)); tu = dict(thd_curve(U("harm")))
    print(f"  THD@-6: mine {tm.get(-6, float('nan')):.2f}%   ATR {tu.get(-6, float('nan')):.2f}%")
    print(f"\n## 2. IMD: mine {dp.imd_smpte(imd):.3f}%   ATR {dp.imd_smpte(U('imd')):.3f}%")
    print(f"\n## 3. Crest: mine {dp.crest(tr):.2f}dB   ATR {dp.crest(U('tr')):.2f}dB")
    print("\n## 4. Path HF@10k rel 1k")
    for pm in ("Input", "Sync", "Repro"):
        print(f"  {pm:6s}: mine {dp.hf_ratio('sweep.wav', paths[pm]):+.2f}   ATR {dp.hf_ratio('sweep.wav', U('path_'+pm)):+.2f}")
    print("\n## 1b. HF@10k nominal vs hot")
    print(f"  nom: mine {dp.hf_ratio('sweep.wav', sw):+.2f}   ATR {dp.hf_ratio('sweep.wav', U('sw')):+.2f}")
    print(f"  hot: mine {dp.hf_ratio('hot_sweep.wav', hotsw):+.2f}   ATR {dp.hf_ratio('hot_sweep.wav', U('hotsw')):+.2f}")
    print(f"\n## 7. Aliasing: mine {dp.aliasing(alias):.1f}dB   ATR {dp.aliasing(U('alias')):.1f}dB  (<=-60 target)")


if __name__ == "__main__":
    main()
