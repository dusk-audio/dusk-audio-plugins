#!/usr/bin/env python3
"""PMB (ParallelMultiband, algo 15) cold-start Optuna driver — sweeps the per-band
DUSKVERB_PMB env dims (t60/level/direct/width x6) + Early Ref Level against the FULL
full_check gate set (n_fail, all stimuli incl. sustained → ripple/ss gates counted).

Written 2026-07-06 for the Small Drum Room PMB migration (second PMB preset). The
preset MUST already be on algo 15 (FactoryPresets.h) and the VST3 built/installed —
this drives the INSTALLED plugin via duskverb_render + DUSKVERB_PMB, exactly the path
sdr_probe.sh uses, so the best config transcribes 1:1 into kPmbByName.

Objective = n_fail + 1e-3 * soft_exceedance (the soft term breaks integer ties so TPE
gets a gradient). Lower is better. Warm-start with --enqueue "t60x6;lvlx6;dirx6;widx6".

Usage:
  python3 pmb_optuna.py --preset "Small Drum Room" --anchor vvv-84-small-room \
     --trials 240 --workers 6 --erlevel \
     --enqueue "0.30,0.32,0.60,0.55,0.52,0.40;1.0,1.15,0.4,0.35,0.9,0.9;0,0,0.2,0.2,0.15,0.1;0.3,0.4,1,1,1,1"
"""
import os, sys, re, glob, json, shutil, argparse, subprocess
import numpy as np, soundfile as sf
import optuna

ROOT = os.path.expanduser("~/projects/plugins")
REND = f"{ROOT}/build/tests/duskverb_render/duskverb_render"
VST3 = os.path.expanduser("~/.vst3/DuskVerb.vst3")
FC   = f"{ROOT}/plugins/DuskVerb/tools/tuner/full_check.py"
ANCHOR_ROOT = os.path.expanduser("~/projects/dusk-audio-tools/tuner_runs/anchors")
STIM = ["noiseburst", "snare", "sine1k", "impulse", "piano", "sustained"]

_num = re.compile(r'[-+]?\d+\.?\d*')

def rms(f):
    x, _ = sf.read(f); m = x.mean(axis=1) if x.ndim > 1 else x  # mono-sum first, like full_check / fleet_audit
    return float(np.sqrt(np.mean(m ** 2)))

def soft_exceedance(fails):
    """Sum of (|Δ| - gate)/gate over parseable failing gates — a smooth severity
    proxy so TPE can descend between integer n_fail steps. Unparseable rows add 1.0."""
    tot = 0.0
    for row in fails:
        try:
            dm = re.search(r'Δ=\s*([-+]?\d+\.?\d*)', row)
            gm = re.search(r'gate[=≤≥]\s*±?\s*([-+]?\d+\.?\d*)', row)
            if dm and gm:
                d = abs(float(dm.group(1))); g = abs(float(gm.group(1)))
                tot += max(0.0, (d - g) / g) if g > 1e-9 else 1.0
            else:
                tot += 1.0
        except Exception:
            tot += 1.0
    return tot

def evaluate(pmb_env, erlevel, adir, apref, name, slot):
    dv = f"/tmp/pmbopt_{slot}"; lex = f"{dv}_a"
    shutil.rmtree(dv, ignore_errors=True); os.makedirs(dv)
    try:
        env = dict(os.environ); env["DUSKVERB_PMB"] = pmb_env
        cmd = [REND, "--vst3", VST3, "--program", name, "--output-dir", dv,
               "--sustained-pink-seconds", "4.0", "--param", "Dry/Wet=1.0", "--param", "Bus Mode=1"]
        if erlevel is not None:
            cmd += ["--param", f"Early Ref Level={erlevel:.4f}"]
        try:
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=420, env=env)
        except subprocess.TimeoutExpired:
            return 999.0
        nb = glob.glob(f"{dv}/*_noiseburst.wav")
        if r.returncode != 0 or not nb:
            return 999.0
        g = rms(f"{adir}/{apref}_noiseburst.wav") / max(rms(nb[0]), 1e-12)
        for f in glob.glob(f"{dv}/*.wav"):
            x, sr = sf.read(f); sf.write(f, x * g, sr, subtype="FLOAT")
        shutil.rmtree(lex, ignore_errors=True); os.makedirs(lex)
        for s in STIM:
            src = f"{adir}/{apref}_{s}.wav"
            if os.path.exists(src):
                shutil.copy(src, f"{lex}/anchor_{s}.wav")
        try:
            fc = subprocess.run([sys.executable, FC, dv, lex, "--name", name, "--json"],
                                capture_output=True, text=True, timeout=200)
        except subprocess.TimeoutExpired:
            return 999.0
        result = 999.0
        for ln in fc.stdout.splitlines():
            if "JSON_RESULT" in ln:
                d = json.loads(ln.split("JSON_RESULT", 1)[1].strip().lstrip(":").strip())
                result = d.get("n_fail", 999) + 1e-3 * soft_exceedance(d.get("fails", []))
                break
        return result
    finally:
        # Always remove BOTH per-trial dirs, even on an early return (timeout /
        # render fail / missing noiseburst / parse miss) — else /tmp/pmbopt_* leak.
        shutil.rmtree(dv, ignore_errors=True); shutil.rmtree(lex, ignore_errors=True)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preset", required=True)
    ap.add_argument("--anchor", required=True, help="anchor dir basename under tuner_runs/anchors/")
    ap.add_argument("--trials", type=int, default=240)
    ap.add_argument("--workers", type=int, default=6)
    ap.add_argument("--erlevel", action="store_true", help="also search Early Ref Level [0,0.9]")
    ap.add_argument("--enqueue", default=None, help='warm-start "t60x6;lvlx6;dirx6;widx6"')
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--out", default=None, help="write best config json here")
    a = ap.parse_args()

    a.trials = min(a.trials, 300); a.workers = min(a.workers, 6)   # box limits (handoff §5)
    adir = f"{ANCHOR_ROOT}/{a.anchor}"; apref = a.anchor
    if not os.path.exists(f"{adir}/{apref}_noiseburst.wav"):
        sys.exit(f"no anchor at {adir}")

    def unpack(trial):
        t60 = [trial.suggest_float(f"t60_{i}", 0.12, 0.90) for i in range(6)]
        lvl = [trial.suggest_float(f"lvl_{i}", 0.30, 1.60) for i in range(6)]
        dr  = [trial.suggest_float(f"dir_{i}", 0.00, 0.50) for i in range(6)]
        wd  = [trial.suggest_float(f"wid_{i}", 0.00, 1.50) for i in range(6)]
        er  = trial.suggest_float("erlevel", 0.0, 0.9) if a.erlevel else None
        env = ";".join(",".join(f"{v:.4f}" for v in grp) for grp in [t60, lvl, dr, wd])
        return env, er

    def objective(trial):
        env, er = unpack(trial)
        return evaluate(env, er, adir, apref, a.preset, trial.number)  # unique slot per trial

    sampler = optuna.samplers.TPESampler(seed=a.seed, multivariate=True, group=True)
    study = optuna.create_study(direction="minimize", sampler=sampler)

    if a.enqueue:
        groups = a.enqueue.split(";")
        vals = [[float(x) for x in g.split(",")] for g in groups]
        seed = {}
        for i in range(6):
            seed[f"t60_{i}"] = vals[0][i]; seed[f"lvl_{i}"] = vals[1][i]
            seed[f"dir_{i}"] = vals[2][i]; seed[f"wid_{i}"] = vals[3][i]
        if a.erlevel:
            seed["erlevel"] = 0.8
        study.enqueue_trial(seed)
        print(f"warm-start enqueued: {a.enqueue}")

    study.optimize(objective, n_trials=a.trials, n_jobs=a.workers, show_progress_bar=False)

    best = study.best_trial
    p = best.params
    t60 = [p[f"t60_{i}"] for i in range(6)]; lvl = [p[f"lvl_{i}"] for i in range(6)]
    dr  = [p[f"dir_{i}"] for i in range(6)]; wd  = [p[f"wid_{i}"] for i in range(6)]
    env = ";".join(",".join(f"{v:.4f}" for v in grp) for grp in [t60, lvl, dr, wd])
    print("\n==== BEST ====")
    print(f"value (n_fail+soft) = {best.value:.4f}  (n_fail ~= {int(best.value)})")
    print(f"DUSKVERB_PMB={env}")
    if a.erlevel:
        print(f"Early Ref Level={p.get('erlevel'):.4f}")
    print("kPmbByName row:")
    fmt = lambda v: "{" + ",".join(f"{x:.3f}f" for x in v) + "}"
    print(f'  {{ "{a.preset}", {{ {fmt(t60)}, {fmt(lvl)}, {fmt(dr)}, {fmt(wd)} }} }},')
    if a.out:
        json.dump({"env": env, "erlevel": p.get("erlevel"), "value": best.value,
                   "t60": t60, "lvl": lvl, "dir": dr, "wid": wd}, open(a.out, "w"), indent=2)
        print(f"wrote {a.out}")

if __name__ == "__main__":
    main()
