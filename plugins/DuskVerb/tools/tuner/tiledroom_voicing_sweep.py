#!/usr/bin/env python3
"""Voice the TiledRoomEngine (algo 13) on the VVV Tiled Room anchor.

Sweeps the 6 engine voicing params via the DUSKVERB_TILEDROOM env override
(erGain, tailGain, feedback, lpHz, onsetMs, erDecayMs) — no rebuild per trial.
Objective = full_check n_fail (gain-matched, sustained-correct). Robust harness
(exception-contained eval, self-cleaning per-trial dirs, SQLite-persisted study,
no enqueue) — same lessons as joint_dense32_sweep.py.

Run:  python3 -u plugins/DuskVerb/tools/tuner/tiledroom_voicing_sweep.py --trials 120
"""
import argparse, os, sys, glob, json, shutil, subprocess
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import soundfile as sf, numpy as np, optuna

REPO = Path(__file__).resolve().parents[4]
REND = str(REPO / "build/tests/duskverb_render/duskverb_render")
FC   = str(Path(__file__).resolve().parent / "full_check.py")
# Defaults voice Tiled Room; --preset/--anchor/--apref/--baseline retarget any
# algo-13 composite preset (e.g. Medium Drum Room). All composite presets share
# the DUSKVERB_TILEDROOM env hook + the kCompositeERByName config path.
PRESET = "Tiled Room"
ANCH = Path.home() / "projects/dusk-audio-tools/tuner_runs/anchors/vvv-tiled-room"
APREF = "vvv-tiled-room"
WET  = ["--param", "Dry/Wet=1.0", "--param", "Bus Mode=1", "--param", "Freeze=0",
        "--sustained-pink-seconds", "4.0"]
STIM = ["impulse", "noiseburst", "snare", "sine1k", "sustained"]
BASELINE = 26   # FDN algo-10 baseline (sustained-correct); set per-preset via --baseline


def rms(p):
    x, _ = sf.read(p); m = x.mean(axis=1) if x.ndim > 1 else x
    return float(np.sqrt(np.mean(m * m)))


def eval_voicing(cfg, tag, bt=None):
    dv = f"/tmp/trv_{tag}"; lex = f"/tmp/trv_{tag}_lex"
    try:
        shutil.rmtree(dv, ignore_errors=True); shutil.rmtree(lex, ignore_errors=True)
        os.makedirs(dv, exist_ok=True); os.makedirs(lex, exist_ok=True)
        env = dict(os.environ, DUSKVERB_TILEDROOM=",".join(f"{v:.4f}" for v in cfg))
        if bt is not None:   # UltraRoom decouple: post-loop per-band level trim (dB)
            env["DUSKVERB_BANDTRIM"] = ",".join(f"{v:.3f}" for v in bt)
        r = subprocess.run([REND, "--program", PRESET, "--output-dir", dv, *WET],
                           cwd=str(REPO), capture_output=True, text=True, env=env)
        nb = glob.glob(f"{dv}/*_noiseburst.wav")
        if r.returncode != 0 or not nb:
            return None
        for s in STIM:
            src = ANCH / f"{APREF}_{s}.wav"
            if src.exists(): shutil.copy(src, f"{lex}/anchor_{s}.wav")
        a = rms(f"{lex}/anchor_noiseburst.wav"); d = rms(nb[0])
        if d < 1e-12: return None
        g = a / d
        for f in glob.glob(f"{dv}/*.wav"):
            x, sr = sf.read(f); sf.write(f, x * g, sr)
        r = subprocess.run([sys.executable, FC, dv, lex, "--name", PRESET, "--json"],
                           capture_output=True, text=True)
        nf = None
        for line in r.stdout.splitlines():
            if line.startswith("JSON_RESULT:"):
                nf = json.loads(line.split("JSON_RESULT: ")[1])["n_fail"]; break
        if nf is None: return None
        # energy_first50: % of impulse-response energy in the first 50 ms post-peak
        imp = glob.glob(f"{dv}/*_impulse.wav")
        first50 = None
        if imp:
            x, sr = sf.read(imp[0]); m = x.mean(axis=1) if x.ndim > 1 else x
            pk = int(np.argmax(np.abs(m))); seg = m[pk:] ** 2
            tot = float(seg.sum())
            if tot > 1e-20:
                first50 = 100.0 * float(seg[:int(0.05 * sr)].sum()) / tot
        return (nf, first50)
    except Exception:
        return None
    finally:
        shutil.rmtree(dv, ignore_errors=True); shutil.rmtree(lex, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trials", type=int, default=120)
    ap.add_argument("--workers", type=int, default=4)
    ap.add_argument("--preset", default="Tiled Room")
    ap.add_argument("--anchor", default=None, help="anchor dir (default vvv-tiled-room)")
    ap.add_argument("--apref", default=None, help="anchor file prefix")
    ap.add_argument("--baseline", type=int, default=26)
    ap.add_argument("--study", default="trv", help="sqlite study name (use distinct per preset)")
    ap.add_argument("--first50-target", type=float, default=None,
                    help="anchor energy_first50 to this %% (penalize deviation); None = pure n_fail")
    ap.add_argument("--bandtrim", action="store_true",
                    help="also sweep the 4 post-loop band-level trims (UltraRoom decouple)")
    a = ap.parse_args()
    global PRESET, ANCH, APREF, BASELINE
    PRESET = a.preset; BASELINE = a.baseline
    if a.anchor: ANCH = Path(a.anchor)
    if a.apref:  APREF = a.apref

    def obj(trial):
        # COMPOSITE params: erSize,onsetMs,erDecayMs,burst2Ms,sparseTailGain,erGain.
        # erGain backs off the ER level so a less-front-loaded room (MDR, first50
        # 44.8%) doesn't overshoot. Objective = n_fail + a continuous penalty that
        # anchors energy_first50 to --first50-target (None = pure n_fail).
        cfg = [
            trial.suggest_float("erSize",         0.30, 0.70),
            trial.suggest_float("onsetMs",        0.8, 2.5),
            trial.suggest_float("erDecayMs",      16.0, 32.0),
            trial.suggest_float("burst2Ms",       0.0, 45.0),
            trial.suggest_float("sparseTailGain", 0.40, 0.92),
            trial.suggest_float("erGain",         0.25, 1.00),
        ]
        bt = None
        if a.bandtrim:   # UltraRoom decouple: post-loop per-band ss-level trims (dB)
            bt = [trial.suggest_float("bt_sub",   -6.0, 6.0),
                  trial.suggest_float("bt_lowmid", -6.0, 6.0),
                  trial.suggest_float("bt_midhi",  -6.0, 6.0),
                  trial.suggest_float("bt_air",    -6.0, 6.0)]
        res = eval_voicing(cfg, str(trial.number), bt)
        if res is None: return 1e3
        nf, first50 = res
        trial.set_user_attr("cfg", [round(v, 4) for v in cfg])
        trial.set_user_attr("bt", [round(v, 3) for v in bt] if bt else None)
        trial.set_user_attr("nfail", nf)
        trial.set_user_attr("first50", round(first50, 1) if first50 is not None else None)
        loss = float(nf)
        if a.first50_target is not None and first50 is not None:
            loss += 0.12 * abs(first50 - a.first50_target)   # anchor first50 to the target
        return loss

    study = optuna.create_study(direction="minimize", study_name=a.study,
                                storage=f"sqlite:////tmp/{a.study}_study.db", load_if_exists=True,
                                sampler=optuna.samplers.TPESampler(seed=42))
    optuna.logging.set_verbosity(optuna.logging.WARNING)
    study.optimize(obj, n_trials=a.trials, n_jobs=a.workers)

    # Report the lowest-n_fail trial (not just lowest-loss) for the scoreboard.
    cs = [t for t in study.trials if t.value is not None and t.value < 1e2]
    bestnf = min(cs, key=lambda t: (t.user_attrs.get("nfail", 99), t.value))
    print(f"\nBEST n_fail={bestnf.user_attrs['nfail']} first50={bestnf.user_attrs.get('first50')} "
          f"({PRESET}, baseline {BASELINE})")
    print("ER cfg (erSize,onsetMs,erDecayMs,burst2Ms,sparseTailGain,erGain):")
    print("  " + ",".join(str(v) for v in bestnf.user_attrs["cfg"]))
    if bestnf.user_attrs.get("bt"):
        print("bandtrim (sub,lowMid,midHi,air dB): " + ",".join(str(v) for v in bestnf.user_attrs["bt"]))

if __name__ == "__main__":
    main()
