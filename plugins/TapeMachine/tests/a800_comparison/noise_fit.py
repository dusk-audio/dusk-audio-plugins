#!/usr/bin/env python3
"""noise_fit.py — per-preset Noise-knob fit (fix ①).

Every factory preset ships with gain link ON and the tape core level-neutral, so the
idle noise the user hears is (idle hiss+hum) * (NoiseAmount/100), scaled by the
preset's cal/speed/LP. The UAD decks add their own constant idle floor whose LEVEL
depends on the preset's config (cal/tape/speed) and its Hiss&Hum / Noise-Hum-Hiss
factory settings. This bisects each preset's NoiseAmount so mine's idle OUTPUT dBFS
matches the UAD factory preset's measured idle within +-1 dB.

Mine's idle noise is EXACTLY linear in NoiseAmount (idleNoise returns (hiss+hum)*frac,
frac = NoiseAmount*0.01), so the fit is closed form:
    NoiseAmount = 100 * 10^((targetDBFS - mine@100DBFS)/20)
clamped to [0,100]. Presets whose UAD idle sits at the silence floor (Hiss&Hum=0 or
Noise/Hum/Hiss all 0) fit to ~0. We still VERIFY-render at the fitted value (mirror
drive_fit / loudness_fit measure-after-set discipline).

Noise renders use each preset's REAL hp/lp (the UAD idle passes the preset LP too) and
W&F OFF (idle spectrum only). Output idle level is entangled with inGain + gain-link +
outTrim, so we fit the OUTPUT dBFS, not a raw noise gain.

  python3 noise_fit.py            # all 20 presets
  python3 noise_fit.py "Old Tape" # substring filter
"""
import os, sys
import numpy as np
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from preset_validate import (parse_presets, decode_uad, mine_params, UAD_JSON,
                             ATR, STUDER, MINE)  # noqa: E402
import noise_probe as NP  # noqa: E402

# below this UAD idle level the noise is inaudible / at the render floor -> ship 0.
SILENCE_DBFS = -100.0


def mine_idle_params(p, noise_amt):
    """mine_params base with an explicit NoiseAmount (%) and W&F off."""
    base = [kv for kv in mine_params(p)
            if kv[0] not in ("Noise Amount", "Noise Enabled", "Wow", "Flutter")]
    base += [("Noise Amount", noise_amt / 100.0), ("Noise Enabled", 1),
             ("Wow", 0.0), ("Flutter", 0.0)]
    return base


def mine_idle_dbfs(p, noise_amt):
    o = NP.render(MINE, mine_idle_params(p, noise_amt), "sto_silence_22s.wav", "nf_m")
    x, _ = NP.load_mono(o)
    return NP.overall_dbfs(NP.steady_region(x, NP.SR))


def uad_idle_dbfs(p):
    machine = p["machine"]
    uad_bin = ATR if machine == 1 else STUDER
    vec, _ = decode_uad(machine, UAD_JSON[p["name"]])
    unp = NP.uad_noise_nparams(machine, vec)   # factory hiss/hum LEFT on, W&F off
    o = NP.render(uad_bin, [], "sto_silence_22s.wav", "nf_u", nparams=unp)
    x, _ = NP.load_mono(o)
    return NP.overall_dbfs(NP.steady_region(x, NP.SR))


def fit(p):
    target = uad_idle_dbfs(p)
    m100 = mine_idle_dbfs(p, 100.0)
    if target <= SILENCE_DBFS:
        amt = 0.0
    else:
        amt = 100.0 * 10 ** ((target - m100) / 20.0)
        amt = float(np.clip(amt, 0.0, 100.0))
    # round to 1 decimal (matches the preset-table precision) and verify-render. Render even
    # at amt_r == 0.0 (through mine_idle_dbfs) rather than substituting -200.0, so the residual
    # output floor is measured and reported as a real delta for every fit.
    amt_r = round(amt, 1)
    achieved = mine_idle_dbfs(p, amt_r)
    wall = amt >= 99.9 and (target - achieved) > 1.0
    return dict(name=p["name"], machine=p["machine"], target=target, m100=m100,
                orig=p["noise"], amt=amt_r, achieved=achieved,
                delta=achieved - target, wall=wall)


def main():
    NP.ensure_stimuli()
    if not NP.check_pace():
        print("!! PACE down — aborting."); return
    args = [s.lower() for s in sys.argv[1:]]
    presets = [p for p in parse_presets()
               if not args or any(s in p["name"].lower() for s in args)]
    with ThreadPoolExecutor(max_workers=4) as ex:
        rows = list(ex.map(fit, presets))
    print(f"{'preset':22s} {'deck':6s} {'UADidle':>8s} {'m@100':>8s} "
          f"{'orig':>5s} {'->fit':>6s} {'got':>8s} {'d':>5s} wall")
    for r in rows:
        deck = "ATR" if r["machine"] == 1 else "Studer"
        print(f"{r['name']:22s} {deck:6s} {r['target']:8.1f} {r['m100']:8.1f} "
              f"{r['orig']:5.0f} {r['amt']:6.1f} {r['achieved']:8.1f} "
              f"{r['delta']:+5.1f} {'WALL' if r['wall'] else ''}")
    print("\nnoise column (paste order = table order):")
    print("  " + ", ".join(f"{r['amt']:.1f}f" for r in rows))


if __name__ == "__main__":
    main()
