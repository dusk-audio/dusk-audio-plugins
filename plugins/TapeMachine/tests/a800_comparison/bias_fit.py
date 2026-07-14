#!/usr/bin/env python3
"""bias_fit.py — for manual-bias presets whose inGain hit the fit wall (drive can't
move), bisect the preset's BIAS to match the UAD factory THD@-6, then re-solve the
4-band repro EQ. Usage: python3 bias_fit.py "Analog Warmth" "Vocal Presence"
"""
import sys, numpy as np
import preset_validate as pv
import joint4_fit as j4

BANDS = j4.BANDS


def thd_at(p, bias, bands):
    q = dict(p); q["bias"] = bias
    for b in BANDS: q[b] = bands[b]
    th = pv.render(pv.MINE, pv.mine_params(q), "thd_steps.wav", "m_bth")
    return dict(pv.thd_curve(th)).get(-6, float("nan"))


def fr_at(p, bias, bands):
    q = dict(p); q["bias"] = bias
    for b in BANDS: q[b] = bands[b]
    return pv.fr_at(pv.render(pv.MINE, pv.mine_params(q), "sweep.wav", "m_bsw"))


def fit(pname, A):
    ps = {p["name"]: p for p in pv.parse_presets()}
    p = ps[pname]
    ufr, uthd = j4.uad_targets(p)
    bands = {b: p.get(b, 0.0) for b in BANDS}
    # bias direction: lower bias => hotter drive => more THD
    lo, hi = 20.0, 80.0
    t_lo, t_hi = thd_at(p, lo, bands), thd_at(p, hi, bands)
    print(f"{pname}: UAD THD {uthd:.2f}%  bias20={t_lo:.2f}% bias80={t_hi:.2f}%  (cur bias {p['bias']:.0f} -> {thd_at(p, p['bias'], bands):.2f}%)")
    for _ in range(9):
        mid = 0.5 * (lo + hi)
        if thd_at(p, mid, bands) > uthd: lo = mid
        else: hi = mid
    bias = 0.5 * (lo + hi)
    # re-solve bands against FR residual (2 rounds)
    for _ in range(2):
        cur = fr_at(p, bias, bands)
        resid = np.array([ufr[i] - cur[i] for i in range(len(j4.G))])
        dg, *_ = np.linalg.lstsq(A, resid, rcond=None)
        for j, b in enumerate(BANDS):
            bands[b] = float(np.clip(bands[b] + 0.9 * dg[j], -12, 12))
    th = thd_at(p, bias, bands)
    frf = fr_at(p, bias, bands)
    maxfr = max(abs(frf[i] - ufr[i]) for i in range(len(j4.G)))
    print(f"  -> bias {bias:.1f}  THD {th:.2f}% (UAD {uthd:.2f})  maxFR {maxfr:.1f}")
    print(f"  -> repro ({bands['reproLf']:.1f}, {bands['reproLmf']:.1f}, {bands['reproHmf']:.1f}, {bands['reproHf']:.1f})")
    return bias, bands


def main():
    A = j4.design_matrix()
    for name in sys.argv[1:]:
        fit(name, A)


if __name__ == "__main__":
    main()
