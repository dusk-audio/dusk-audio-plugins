#!/usr/bin/env python3
"""joint4_fit.py — jointly fit Input Gain (THD) + the 4-band Repro EQ (FR) per preset.
The 4 repro bands are post-tape LINEAR filters, so their dB effect on the output FR is
additive and preset-independent: measure each band's unit response ONCE, build a 7x4
design matrix, and least-squares solve the 4 gains against each preset's residual FR.
Interleave with a drive bisection (the HMF/HF bands boost harmonic THD -> shift drive).

  python3 joint4_fit.py                         # all presets
  python3 joint4_fit.py "Fat 456" "Old Tape"  # selected presets
"""
import sys, numpy as np
import preset_validate as pv

ROUNDS = 3
BANDS = ["reproLf", "reproLmf", "reproHmf", "reproHf"]
PNAME = {"reproLf": "Repro LF", "reproLmf": "Repro LMF",
         "reproHmf": "Repro HMF", "reproHf": "Repro HF"}
G = pv.FRq  # [30,50,100,1000,5000,10000,15000]


def mine(p, g, bands):
    q = dict(p); q["inGain"] = g
    for b in BANDS: q[b] = bands[b]
    return pv.mine_params(q)


def fr(p, g, bands):
    return pv.fr_at(pv.render(pv.MINE, mine(p, g, bands), "sweep.wav", "m_sw"))


def thd(p, g, bands):
    th = pv.render(pv.MINE, mine(p, g, bands), "thd_steps.wav", "m_th")
    return dict(pv.thd_curve(th)).get(-6, float("nan"))


def design_matrix():
    """Unit FR response (dB per dB) of each band at the grid freqs — measured once on a
    flat-ish reference config; additive so it transfers to every preset."""
    base = dict(machine=1, speed=1, type=0, eq=0, cal=1, path=0, head=1,
                autoCal=True, bias=50.0, inGain=0.0, hp=20.0, lp=20000.0)
    z = {b: 0.0 for b in BANDS}
    r0 = fr(base, 0.0, z)
    cols = []
    for b in BANDS:
        bb = dict(z); bb[b] = 6.0
        r = fr(base, 0.0, bb)
        cols.append([(r[i] - r0[i]) / 6.0 for i in range(len(G))])
    return np.array(cols).T   # 7 x 4


def uad_targets(p):
    m = p["machine"]; ub = pv.ATR if m == 1 else pv.STUDER
    vec, _ = pv.decode_uad(m, pv.UAD_JSON[p["name"]]); unp = pv.uad_nparams(m, vec)
    ufr = pv.fr_at(pv.render(ub, [], "sweep.wav", "u_sw", nparams=unp))
    uthd = dict(pv.thd_curve(pv.render(ub, [], "thd_steps.wav", "u_th", nparams=unp))).get(-6, float("nan"))
    return ufr, uthd


def fit_drive(p, bands, target, g):
    lo, hi = -12.0, 12.0
    if thd(p, hi, bands) < target - 0.15: return hi
    if thd(p, lo, bands) > target + 0.15: return lo
    for _ in range(8):
        mid = 0.5 * (lo + hi)
        if thd(p, mid, bands) < target: lo = mid
        else: hi = mid
    return 0.5 * (lo + hi)


def joint(p, A, targets=None):
    ufr, uthd = targets if targets is not None else uad_targets(p)
    g = p["inGain"]
    # Refine the preset's current jointly-fitted point instead of restarting the
    # four repro bands at neutral. This keeps targeted release-gate repairs local
    # and lets the dense 15-point grid correct residuals missed by earlier fits.
    bands = {b: p.get(b, 0.0) for b in BANDS}
    for _ in range(ROUNDS):
        g = fit_drive(p, bands, uthd, g)
        cur = fr(p, g, bands)
        resid = np.array([ufr[i] - cur[i] for i in range(len(G))])   # want to add
        dg, *_ = np.linalg.lstsq(A, resid, rcond=None)
        for j, b in enumerate(BANDS):
            bands[b] = float(np.clip(bands[b] + 0.9 * dg[j], -12, 12))
    g = fit_drive(p, bands, uthd, g)
    fr_f = fr(p, g, bands); th = thd(p, g, bands)
    maxfr = max(abs(fr_f[i] - ufr[i]) for i in range(len(G)))
    return g, bands, th, uthd, maxfr


def main():
    from concurrent.futures import ThreadPoolExecutor
    A = design_matrix()
    print("design matrix (dB/dB):\n", np.round(A, 2))
    ps = pv.parse_presets()
    filters = [s.lower() for s in sys.argv[1:]]
    if filters:
        ps = [p for p in ps if any(f in p["name"].lower() for f in filters)]
    if not ps:
        raise SystemExit("no matching presets")
    print(f"\n{'preset':22} {'inG':>6} {'LF':>5} {'LMF':>5} {'HMF':>5} {'HF':>5} {'THD':>7} {'maxFR':>6}")
    drv, rep = {}, {}
    # presets are independent -> fit them in parallel (each joint() is serial renders,
    # but subprocess renders release the GIL so threads run truly concurrently).
    with ThreadPoolExecutor(max_workers=min(8, len(ps))) as ex:
        results = list(ex.map(lambda p: (p["name"], joint(p, A)), ps))
    for name, (g, bands, th, uthd, maxfr) in results:
        drv[name] = g; rep[name] = bands
        print(f"{name:22} {g:+6.1f} {bands['reproLf']:+5.1f} {bands['reproLmf']:+5.1f} "
              f"{bands['reproHmf']:+5.1f} {bands['reproHf']:+5.1f} {th:6.2f}% {maxfr:6.1f}")
    print("\n# drives:")
    for k, g in drv.items(): print(f'  "{k}": {g:.1f},')
    print("# repro (lf,lmf,hmf,hf):")
    for k, b in rep.items():
        print(f'  "{k}": ({b["reproLf"]:.1f}, {b["reproLmf"]:.1f}, {b["reproHmf"]:.1f}, {b["reproHf"]:.1f}),')


if __name__ == "__main__":
    main()
