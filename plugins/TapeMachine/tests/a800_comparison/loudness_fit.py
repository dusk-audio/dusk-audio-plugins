#!/usr/bin/env python3
"""loudness_fit.py — match each preset's OUTPUT LOUDNESS to the UAD deck (fix ④).

All 20 factory presets ship with gain link (Auto Compensation) ON, so the tape
core holds output at unity (-12 dBFS in -> -12 dBFS out) regardless of drive. The
UAD factory presets, however, dial their own record/repro (Studer) or rec/repro
(ATR) levels, so their net output is NOT unity — a preset may be deliberately
louder or quieter. Mine forcing unity leaves a per-preset loudness offset vs UAD
(audible as A/B level jumps).

LEVER: a per-preset flat OUTPUT trim. The DSP now computes, under gain link,
`outputGain = -inputGain + OutputGainDb`, so the OUTPUT knob becomes an additive
makeup trim on top of the inverse link (default 0 -> byte-identical / unity). The
trim is a POST-TAPE LINEAR gain: it shifts loud-region RMS and LUFS by exactly its
dB value and is transparent to THD / FR-shape / aliasing (it does not change the
drive into the tape). Because it is exactly linear, the fit is closed form:
    trim = -deltaRMS(trim=0)
We still render to VERIFY (mirror drive_fit's measure-after-set discipline).

Target = loud-region RMS on pink -18 dBFS (the fix's spec metric). A flat trim
CANNOT correct a spectral-tilt difference, so where LUFS (HF-weighted) diverges
from RMS (LF-weighted) that residual is the documented HF-brightness item (walls:
shaper / emphasis / level-comp), not this fix's job — we report it, don't chase it.

  python3 loudness_fit.py measure            # current deltas, all presets (pink+drum)
  python3 loudness_fit.py measure "Sunbaked" # substring filter
  python3 loudness_fit.py fit                 # closed-form trim + verify, print table
"""
import os, sys, json, tempfile, shutil, subprocess, itertools
from concurrent.futures import ThreadPoolExecutor
import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from preset_validate import (parse_presets, decode_uad, uad_nparams, mine_params,
                             UAD_JSON, ATR, STUDER, MINE, BIN)  # noqa: E402
from det_probe import make_pink, kweight_lufs, loud_region  # noqa: E402
from program_probe import synth_drums  # noqa: E402

SR = 48000
OUT = os.path.join(HERE, "renders", "loud_fit")
_rc = itertools.count()


def render(plugin, params, inp, mode, tag):
    tmp = tempfile.mkdtemp()
    cmd = [BIN, "--au", plugin, "--input-wav", inp, "--slug", "s",
           "--output-dir", tmp, "--prerun-seconds", "2"]
    flag = "--param" if mode == "param" else "--nparam"
    for k, v in params:
        cmd += [flag, f"{k}={v}"]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    stem = os.path.join(tmp, "s_stem.wav")
    if proc.returncode != 0 or not os.path.exists(stem):
        shutil.rmtree(tmp, ignore_errors=True)
        raise RuntimeError(f"render {tag} rc={proc.returncode}\n{proc.stderr[-600:]}")
    os.makedirs(OUT, exist_ok=True)
    dest = os.path.join(OUT, f"{tag}_{os.getpid()}_{next(_rc)}.wav")
    shutil.move(stem, dest)
    shutil.rmtree(tmp, ignore_errors=True)
    return dest


def load(p):
    x, _ = sf.read(p)
    return (x.mean(1) if x.ndim > 1 else x).astype(np.float64)


def loud_rms_lufs(path):
    x = load(path)
    a, b = loud_region(x)
    x = x[a:b]
    rms = 20 * np.log10(np.sqrt(np.mean(x ** 2)) + 1e-20)
    return rms, kweight_lufs(x)


def mine_params_trim(p, trim_db):
    """mine_params + an OUTPUT-GAIN trim (dB -> normalised -12..12)."""
    mp = [(k, v) for k, v in mine_params(p) if k != "Output Gain"]
    mp += [("Output Gain", (trim_db + 12.0) / 24.0)]
    return mp


# ------------------------------------------------------------------ stimuli
def build_stimuli():
    os.makedirs(OUT, exist_ok=True)
    pink = make_pink()                                   # -18 dBFS pink
    ppath = os.path.join(OUT, "pink.wav")
    sf.write(ppath, np.column_stack([pink, pink]), SR, subtype="FLOAT")
    drum, _ = synth_drums()                              # peak ~ -6 dBFS drum loop
    dpath = os.path.join(OUT, "drum.wav")
    sf.write(dpath, np.column_stack([drum, drum]), SR, subtype="FLOAT")
    return ppath, dpath


def measure_one(p, ppath, dpath, trim_db=0.0):
    vec, _ = decode_uad(p["machine"], UAD_JSON[p["name"]])
    ubin = ATR if p["machine"] == 1 else STUDER
    deck = "ATR" if p["machine"] == 1 else "A800"
    mp = mine_params_trim(p, trim_db)
    up = uad_nparams(p["machine"], vec)
    tag = p["name"].replace(" ", "_")
    mpk = render(MINE, mp, ppath, "param", f"mp_{tag}")
    upk = render(ubin, up, ppath, "nparam", f"up_{tag}")
    mdr = render(MINE, mp, dpath, "param", f"md_{tag}")
    udr = render(ubin, up, dpath, "nparam", f"ud_{tag}")
    prm, plm = loud_rms_lufs(mpk); pru, plu = loud_rms_lufs(upk)
    drm, dlm = loud_rms_lufs(mdr); dru, dlu = loud_rms_lufs(udr)
    return dict(name=p["name"], deck=deck, trim=trim_db,
                pink_drms=prm - pru, pink_dlufs=plm - plu,
                drum_drms=drm - dru, drum_dlufs=dlm - dlu)


def run(presets, ppath, dpath, trims):
    rows = []
    with ThreadPoolExecutor(max_workers=5) as ex:
        futs = [ex.submit(measure_one, p, ppath, dpath, trims.get(p["name"], 0.0))
                for p in presets]
        for f in futs:
            rows.append(f.result())
    rows.sort(key=lambda r: -abs(r["pink_drms"]))
    return rows


def print_table(rows, header):
    print(f"\n{header}  [Δ = mine-UAD dB; + = mine louder]\n")
    print(f"  {'preset':22}{'deck':5}{'trim':>6}"
          f"{'pinkRMS':>9}{'pinkLUF':>9}{'drumRMS':>9}{'drumLUF':>9}")
    for r in rows:
        flag = "  <<" if abs(r["pink_drms"]) > 0.5 else ""
        print(f"  {r['name']:22}{r['deck']:5}{r['trim']:+6.2f}"
              f"{r['pink_drms']:+9.2f}{r['pink_dlufs']:+9.2f}"
              f"{r['drum_drms']:+9.2f}{r['drum_dlufs']:+9.2f}{flag}")
    over = [r for r in rows if abs(r["pink_drms"]) > 0.5]
    print(f"\n  pink RMS |Δ|>0.5: {len(over)}/{len(rows)};  "
          f"mean |pinkRMS| {np.mean([abs(r['pink_drms']) for r in rows]):.2f}  "
          f"max {max(abs(r['pink_drms']) for r in rows):.2f}  |  "
          f"mean |drumRMS| {np.mean([abs(r['drum_drms']) for r in rows]):.2f}  "
          f"max {max(abs(r['drum_drms']) for r in rows):.2f}")


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "measure"
    filt = sys.argv[2] if len(sys.argv) > 2 else ""
    presets = [p for p in parse_presets() if filt.lower() in p["name"].lower()]
    ppath, dpath = build_stimuli()

    if mode == "measure":
        # reflect the SHIPPED presets: use each preset's baked outTrim as the trim.
        shipped = {p["name"]: p.get("outTrim", 0.0) for p in presets}
        rows = run(presets, ppath, dpath, shipped)
        print_table(rows, "LOUDNESS (shipped outTrim)")
        json.dump(rows, open(os.path.join(OUT, "measure.json"), "w"), indent=1)
        return

    if mode == "fit":
        # round 0: measure current delta (trim 0)
        base = {r["name"]: r for r in run(presets, ppath, dpath, {})}
        # closed form: flat output gain shifts RMS by exactly its dB -> trim = -deltaRMS.
        # Round to 0.1 dB; leave |Δ|<0.25 at 0 (already inside the target band, no churn).
        def pick(d):
            return 0.0 if abs(d) < 0.25 else round(-d, 1)
        trims = {n: pick(base[n]["pink_drms"]) for n in base}
        # verify (re-render with the trims applied)
        rows = run(presets, ppath, dpath, trims)
        for r in rows:
            r["trim"] = trims[r["name"]]
        print_table(rows, "LOUDNESS after closed-form trim (VERIFY re-render)")
        # emit trims sorted by preset order for pasting
        order = [p["name"] for p in parse_presets()]
        rows.sort(key=lambda r: order.index(r["name"]))
        print("\n  outTrim per preset (paste into TapeMachinePresets.hpp):")
        for r in rows:
            print(f"    {r['name']:22} {trims[r['name']]:+.2f}")
        json.dump(dict(trims=trims, rows=rows),
                  open(os.path.join(OUT, "fit.json"), "w"), indent=1)
        return


if __name__ == "__main__":
    main()
