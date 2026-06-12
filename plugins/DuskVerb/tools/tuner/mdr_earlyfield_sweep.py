#!/usr/bin/env python3
"""Optuna COLD-START sweep: Dattorro (algo 0) voicing on Medium Drum Room.

MDR is baked algo 0; this drives the voicing entirely via --param (NO rebuild
per trial). Objective = full_check n_fail + continuous tiebreak (summed gate
exceedance, mapped into [0,1) so n_fail always dominates → Optuna gets a
gradient through n_fail plateaus). Sustained-correct, gain-matched to the
anchor noiseburst RMS. Self-cleaning per-trial dirs (tmpfs OOM lesson).
SQLite-persisted (resumable). No enqueue_trial (race bug). n_jobs=6.
"""
import os, sys, glob, json, shutil, subprocess, re
from pathlib import Path
import optuna, soundfile as sf, numpy as np

# Repo root = .../plugins/DuskVerb/tools/tuner/<this> -> parents[4]. Override
# with REPO_PATH for non-standard checkouts.
REPO = os.environ.get("REPO_PATH", str(Path(__file__).resolve().parents[4]))
REND = f"{REPO}/build/tests/duskverb_render/duskverb_render"
ANCH = os.path.expanduser("~/projects/dusk-audio-tools/tuner_runs/anchors")
FC   = f"{REPO}/plugins/DuskVerb/tools/tuner/full_check.py"
WET  = ["--param", "Dry/Wet=1.0", "--param", "Bus Mode=1", "--param", "Freeze=0",
        "--sustained-pink-seconds", "4.0"]
STIM = ["impulse", "noiseburst", "snare", "sine1k", "sustained"]
NAME = "Medium Drum Room"
ADIR, APREF = f"{ANCH}/vvv-fat-snare-room", "vvv-fat-snare-room"
WORK = "/tmp/dattorro_sweep"
os.makedirs(WORK, exist_ok=True)

# (display_name, lo, hi). Ranges seeded from this session's probes: Decay 0.65
# gave T60-500 0.35s vs anchor 0.93s -> need ~2x; anchor T60 0.66(16k)..0.99(125)
# so Treble<1 (HF shorter), Bass~1. Size ~0.4 (anchor 38.8%).
PARAMS = [  # round 4: EARLY-FIELD axis on the frozen r3 tank. Target cluster:
            # attack +17ms slow, onset -76%, first50 +19pp, snare-RMS +2.7,
            # osc P2P -5.4, width-low/stereo. Tank micro-dims for rebalance only.
    ("Early Ref Level",  0.10, 0.55),
    ("Early Ref Boost",  1.0,  4.5),
    ("Early Ref Rise",   4.0,  26.0),
    ("Early Ref Size",   0.40, 0.95),
    ("ER Bus Low Gain",  -8.0, 4.0),
    ("ER Bus High Gain", -4.0, 8.0),
    ("Tank Level",       0.45, 1.00),
    ("Pre-Delay",        4.0,  14.0),
    ("Width",            0.85, 1.10),
    ("Decay Time",       0.90, 1.05),
    ("Treble Multiply",  0.60, 0.78),
]
_UNUSED_ENV_PARAMS = [
    ("tf_low_db",  -8.0, 0.0),    # tank-feed low shelf @ 250 Hz (boom/ss-sub)
    ("tf_high_db", -12.0, 0.0),   # tank-feed high shelf (late HF / ss air)
    ("tf_high_fc", 1200.0, 4000.0),
    ("densjit",    0.0, 0.02),    # density-AP wander (pitch-chorus vs ring)
]


def rms(p):
    x, _ = sf.read(p); m = x.mean(axis=1) if x.ndim > 1 else x
    return float(np.sqrt(np.mean(m * m)))


def exceedance(fails):
    """Sum |Δ|/gate over fail lines (generic parse). Tiebreak only."""
    tot = 0.0
    for f in fails:
        dm = re.search(r"Δ=\s*([+-]?[\d.]+)", f)
        gm = re.search(r"gate=[±≤]?\s*([\d.]+)", f)
        if dm and gm:
            g = float(gm.group(1))
            if g > 0:
                tot += abs(float(dm.group(1))) / g
    return tot


def objective(trial):
    tid = trial.number
    pflags = []
    for nm, lo, hi in PARAMS:
        v = trial.suggest_float(nm, lo, hi)
        pflags += ["--param", f"{nm}={v}"]
    renv = dict(os.environ)   # feed-EQ/densjit ride the baked maps
    dv, lex = f"{WORK}/t{tid}", f"{WORK}/t{tid}_lex"
    shutil.rmtree(dv, ignore_errors=True); shutil.rmtree(lex, ignore_errors=True)
    os.makedirs(dv); os.makedirs(lex)
    try:
        r = subprocess.run([REND, "--program", NAME, "--output-dir", dv, *WET, *pflags],
                           cwd=REPO, capture_output=True, text=True, timeout=150, env=renv)
        nb = glob.glob(f"{dv}/*_noiseburst.wav")
        if r.returncode != 0 or not nb:
            return 999.0
        for s in STIM:
            src = f"{ADIR}/{APREF}_{s}.wav"
            if os.path.exists(src):
                shutil.copy(src, f"{lex}/anchor_{s}.wav")
        g = rms(f"{lex}/anchor_noiseburst.wav") / max(rms(nb[0]), 1e-12)
        for f in glob.glob(f"{dv}/*.wav"):
            x, sr = sf.read(f); sf.write(f, x * g, sr, subtype='FLOAT')
        r = subprocess.run([sys.executable, FC, dv, lex, "--name", NAME, "--json"],
                           capture_output=True, text=True, timeout=150)
        res = None
        for line in r.stdout.splitlines():
            if line.startswith("JSON_RESULT:"):
                res = json.loads(line.split("JSON_RESULT: ")[1])
        if res is None:
            return 999.0
        nf = res["n_fail"]
        ex = exceedance(res["fails"])
        trial.set_user_attr("n_fail", nf)
        return nf + ex / (ex + 50.0)
    except Exception:
        return 999.0
    finally:
        shutil.rmtree(dv, ignore_errors=True); shutil.rmtree(lex, ignore_errors=True)


if __name__ == "__main__":
    n_trials = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    n_jobs   = int(sys.argv[2]) if len(sys.argv) > 2 else 6
    optuna.logging.set_verbosity(optuna.logging.WARNING)
    study = optuna.create_study(direction="minimize", study_name="mdr_r4_er",
                                storage=f"sqlite:///{WORK}/study_r4.db", load_if_exists=True)
    done = len([t for t in study.trials if t.state.is_finished()])
    print(f"resuming: {done} trials done; running {n_trials} more, {n_jobs} workers")
    study.optimize(objective, n_trials=n_trials, n_jobs=n_jobs)
    bt = study.best_trial
    print("BEST n_fail:", bt.user_attrs.get("n_fail"), "objective:", round(study.best_value, 4))
    print("BEST params:", json.dumps(study.best_params, indent=2))
