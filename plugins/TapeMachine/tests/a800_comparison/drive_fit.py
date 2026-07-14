#!/usr/bin/env python3
"""drive_fit.py — for each TapeMachine preset, binary-search Input Gain (dB) so
mine's THD@-6 matches the source UAD factory preset's THD@-6. Reports the fitted
inGain, achieved THD, and the resulting maxFR, and flags presets that hit the
topology wall (can't reach the UAD THD even at +12 dB — the HF-under-drive limit).

  python3 drive_fit.py            # all
  python3 drive_fit.py "Drum"     # substring filter
"""
import sys, numpy as np
import preset_validate as pv

TOL = 0.15          # THD% match tolerance
LO, HI = -12.0, 12.0


def mine_thd_fr(p, ingain_db):
    mp = [(k, (ingain_db + 12.0) / 24.0) if k == "Input Gain" else (k, v)
          for k, v in pv.mine_params(p)]
    sw = pv.render(pv.MINE, mp, "sweep.wav", "m_sw")
    th = pv.render(pv.MINE, mp, "thd_steps.wav", "m_th")
    thd = dict(pv.thd_curve(th)).get(-6, float("nan"))
    mfr = pv.fr_at(sw)
    ufr_ref = None
    return thd, mfr


def uad_thd(p):
    machine = p["machine"]
    uad_bin = pv.ATR if machine == 1 else pv.STUDER
    vec, _ = pv.decode_uad(machine, pv.UAD_JSON[p["name"]])
    unp = pv.uad_nparams(machine, vec)
    th = pv.render(uad_bin, [], "thd_steps.wav", "u_th", nparams=unp)
    sw = pv.render(uad_bin, [], "sweep.wav", "u_sw", nparams=unp)
    return dict(pv.thd_curve(th)).get(-6, float("nan")), pv.fr_at(sw)


def fit(p):
    target, ufr = uad_thd(p)
    lo, hi = LO, HI
    # THD monotonic-increasing in drive; bisect.
    tlo, _ = mine_thd_fr(p, lo)
    thi, _ = mine_thd_fr(p, hi)
    if thi < target - TOL:
        thd, mfr = mine_thd_fr(p, hi)
        return dict(target=target, ingain=hi, thd=thd, wall=True,
                    mfr=mfr, ufr=ufr, orig=p["inGain"])
    if tlo > target + TOL:
        thd, mfr = mine_thd_fr(p, lo)
        return dict(target=target, ingain=lo, thd=thd, wall=True,
                    mfr=mfr, ufr=ufr, orig=p["inGain"])
    for _ in range(9):
        mid = 0.5 * (lo + hi)
        t, _ = mine_thd_fr(p, mid)
        if abs(t - target) < TOL:
            lo = hi = mid
            break
        if t < target:
            lo = mid
        else:
            hi = mid
    g = 0.5 * (lo + hi)
    thd, mfr = mine_thd_fr(p, g)
    return dict(target=target, ingain=g, thd=thd, wall=False,
                mfr=mfr, ufr=ufr, orig=p["inGain"])


def main():
    filt = sys.argv[1] if len(sys.argv) > 1 else ""
    presets = [p for p in pv.parse_presets() if filt.lower() in p["name"].lower()]
    print(f"drive-fitting {len(presets)} preset(s)\n")
    print(f"{'preset':22} {'orig':>6} {'fit':>6} {'THD':>7} {'UAD':>7} {'wall':>5} {'maxFR':>6}")
    for p in presets:
        r = fit(p)
        frdiff = max(abs(m - u) for m, u in zip(r["mfr"], r["ufr"]))
        print(f"{p['name']:22} {r['orig']:+6.1f} {r['ingain']:+6.1f} "
              f"{r['thd']:6.2f}% {r['target']:6.2f}% {'WALL' if r['wall'] else '':>5} {frdiff:6.1f}")


if __name__ == "__main__":
    main()
