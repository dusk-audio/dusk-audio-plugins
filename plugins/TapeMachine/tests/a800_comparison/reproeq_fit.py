#!/usr/bin/env python3
"""reproeq_fit.py — fit each preset's Repro HF / Repro LF trim (the advanced params)
so mine's FR residual vs its UAD factory preset is nulled. HF shelf (3 kHz) targets
the 8-15 kHz band; LF shelf (180 Hz) targets 30-80 Hz. Iterates (the shelves are
~linear in dB near their plateau). Prints dB values to paste into the preset table.

  python3 reproeq_fit.py            # all
  python3 reproeq_fit.py "Jazz"     # filter
"""
import sys, numpy as np
import preset_validate as pv

HFB = [5000, 10000, 15000]             # HF-shelf target band (pv.FRq grid)
LFB = [30, 50]                         # LF-shelf target band
ITERS = 5


def band_idx(freqs):
    return [pv.FRq.index(f) for f in freqs]


HFi, LFi = band_idx(HFB), band_idx(LFB)


def mine_with_repro(p, hf, lf):
    mp = pv.mine_params(p) + [("Repro HF", (hf + 12.0) / 24.0),
                              ("Repro LF", (lf + 12.0) / 24.0)]
    return mp


def fit(p):
    machine = p["machine"]
    uad_bin = pv.ATR if machine == 1 else pv.STUDER
    vec, _ = pv.decode_uad(machine, pv.UAD_JSON[p["name"]])
    unp = pv.uad_nparams(machine, vec)
    u_sw = pv.render(uad_bin, [], "sweep.wav", "u_sw", nparams=unp)
    ufr = pv.fr_at(u_sw)
    def resid(hf, lf):
        m_sw = pv.render(pv.MINE, mine_with_repro(p, hf, lf), "sweep.wav", "m_sw")
        mfr = pv.fr_at(m_sw)
        d = [m - u for m, u in zip(mfr, ufr)]
        return d, max(abs(x) for x in d)

    base_d, base_max = resid(0.0, 0.0)                     # no-EQ baseline
    hf, lf = 0.0, 0.0
    for _ in range(ITERS):
        d, _ = resid(hf, lf)
        dHF = np.mean([d[i] for i in HFi])
        dLF = np.mean([d[i] for i in LFi])
        hf = max(-12.0, min(12.0, hf - 0.7 * dHF))         # damped; boost where mine is low
        lf = max(-12.0, min(12.0, lf - 0.7 * dLF))
    fit_d, fit_max = resid(hf, lf)
    if fit_max >= base_max:                                # never worse than no EQ
        return 0.0, 0.0, base_max, base_d
    return hf, lf, fit_max, fit_d


def main():
    filt = sys.argv[1] if len(sys.argv) > 1 else ""
    presets = [p for p in pv.parse_presets() if filt.lower() in p["name"].lower()]
    print(f"{'preset':22} {'reproHF':>8} {'reproLF':>8} {'maxFR':>6}   (residual @ "
          + " ".join(f"{f}" for f in pv.FRq) + ")")
    out = {}
    for p in presets:
        hf, lf, maxfr, diff = fit(p)
        out[p["name"]] = (hf, lf)
        print(f"{p['name']:22} {hf:+8.1f} {lf:+8.1f} {maxfr:6.1f}   "
              + " ".join(f"{x:+4.1f}" for x in diff))
    print("\n# paste (name: reproHF, reproLF):")
    for k, (hf, lf) in out.items():
        print(f'  "{k}": ({hf:.1f}, {lf:.1f}),')


if __name__ == "__main__":
    main()
