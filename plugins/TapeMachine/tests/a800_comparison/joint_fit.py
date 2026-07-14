#!/usr/bin/env python3
"""joint_fit.py — jointly fit each preset's Input Gain (THD) AND Repro HF/LF (FR),
which are coupled (the repro HF shelf boosts harmonic HF -> shifts THD; drive shifts
the HF-under-drive darkening -> shifts FR). Alternates a drive step and a repro step
to convergence, so both THD and FR land together. Prints final values to paste.

  python3 joint_fit.py           # all
  python3 joint_fit.py "Thick"   # filter
"""
import sys, numpy as np
import preset_validate as pv

ROUNDS = 3
HFi = [pv.FRq.index(f) for f in (5000, 10000, 15000)]
LFi = [pv.FRq.index(f) for f in (30, 50)]


def uad_targets(p):
    machine = p["machine"]
    ub = pv.ATR if machine == 1 else pv.STUDER
    vec, _ = pv.decode_uad(machine, pv.UAD_JSON[p["name"]])
    unp = pv.uad_nparams(machine, vec)
    u_sw = pv.render(ub, [], "sweep.wav", "u_sw", nparams=unp)
    u_th = pv.render(ub, [], "thd_steps.wav", "u_th", nparams=unp)
    return pv.fr_at(u_sw), dict(pv.thd_curve(u_th)).get(-6, float("nan"))


def mine(p, g, hf, lf):
    q = dict(p); q["inGain"] = g; q["reproHf"] = hf; q["reproLf"] = lf
    return pv.mine_params(q)


def thd_at(p, g, hf, lf):
    th = pv.render(pv.MINE, mine(p, g, hf, lf), "thd_steps.wav", "m_th")
    return dict(pv.thd_curve(th)).get(-6, float("nan"))


def fr_at(p, g, hf, lf):
    sw = pv.render(pv.MINE, mine(p, g, hf, lf), "sweep.wav", "m_sw")
    return pv.fr_at(sw)


def fit_drive(p, hf, lf, target, g):
    # bisect inGain so THD@-6 == target (monotonic in drive)
    lo, hi = -12.0, 12.0
    if thd_at(p, hi, hf, lf) < target - 0.15:
        return hi, True
    if thd_at(p, lo, hf, lf) > target + 0.15:
        return lo, True
    for _ in range(8):
        mid = 0.5 * (lo + hi)
        if thd_at(p, mid, hf, lf) < target:
            lo = mid
        else:
            hi = mid
    return 0.5 * (lo + hi), False


def fit_repro(p, g, ufr, hf, lf):
    for _ in range(4):
        d = [m - u for m, u in zip(fr_at(p, g, hf, lf), ufr)]
        hf = max(-12, min(12, hf - 0.7 * np.mean([d[i] for i in HFi])))
        lf = max(-12, min(12, lf - 0.7 * np.mean([d[i] for i in LFi])))
    return hf, lf


def joint(p):
    ufr, uthd = uad_targets(p)
    g, hf, lf = p["inGain"], p.get("reproHf", 0.0), p.get("reproLf", 0.0)
    wall = False
    for _ in range(ROUNDS):
        g, wall = fit_drive(p, hf, lf, uthd, g)
        hf, lf = fit_repro(p, g, ufr, hf, lf)
    # final metrics
    fr = fr_at(p, g, hf, lf); thd = thd_at(p, g, hf, lf)
    frdiff = [m - u for m, u in zip(fr, ufr)]
    # if the shelves make FR worse than neutral, drop them
    fr0 = fr_at(p, g, 0.0, 0.0)
    if max(abs(x) for x in (a-b for a,b in zip(fr0, ufr))) <= max(abs(x) for x in frdiff):
        hf, lf = 0.0, 0.0; frdiff = [a-b for a,b in zip(fr0, ufr)]; thd = thd_at(p, g, 0, 0)
    return g, hf, lf, thd, uthd, max(abs(x) for x in frdiff), wall


def main():
    filt = sys.argv[1] if len(sys.argv) > 1 else ""
    ps = [p for p in pv.parse_presets() if filt.lower() in p["name"].lower()]
    print(f"{'preset':22} {'inGain':>7} {'rHF':>6} {'rLF':>6} {'THD':>7} {'UAD':>7} {'maxFR':>6} {'wall':>4}")
    drv, rep = {}, {}
    for p in ps:
        g, hf, lf, thd, uthd, maxfr, wall = joint(p)
        drv[p["name"]] = g; rep[p["name"]] = (hf, lf)
        print(f"{p['name']:22} {g:+7.1f} {hf:+6.1f} {lf:+6.1f} {thd:6.2f}% {uthd:6.2f}% "
              f"{maxfr:6.1f} {'WALL' if wall else '':>4}")
    print("\n# drives:")
    for k, g in drv.items(): print(f'  "{k}": {g:.1f},')
    print("# repro (hf,lf):")
    for k, (hf, lf) in rep.items(): print(f'  "{k}": ({hf:.1f}, {lf:.1f}),')


if __name__ == "__main__":
    main()
