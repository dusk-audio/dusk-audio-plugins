#!/usr/bin/env python3
"""Joint re-sweep of the AccurateHall32 (algo 12) Bright Hall 32-delay set.

The kurtosis-only dense32 sweep cut the metallic ring (tail 2-14k kurtosis
19.3 -> 14.3) but, by ignoring every gate, destroyed the 16-line mod-beat
ripple alignment and shifted modal energy -> full_check n_fail 9 -> 20. This
sweep optimizes the SAME 32 delays against full_check n_fail directly (the real
gate suite: ripple, bloom, ss-level, decay, boom, stereo/width), holding the
metal win as a soft ceiling so the recovered config stays dense.

  PRIMARY  minimize full_check n_fail (gain-matched, sustained-correct)
  HOLD     keep tail 2-14k kurtosis <= 16.0 (penalty above) — preserve the metal win
  GUARD    keep mean(delays) ~ 3121 (the 16-line baseline mean -> T60)

Warm-started from the kurtosis-optimal 32 set (its offsets=0 is enqueued as the
first trial), tight +/-90-sample prime windows so it recovers the gates without
collapsing the spectral spread. Renders BH on its native algo (12) via the
DUSKVERB_FDN_DELAYS env override — no rebuild per trial.

Run from repo root:
  python3 plugins/DuskVerb/tools/tuner/joint_dense32_sweep.py --trials 90
"""
import argparse, os, sys, glob, json, shutil, subprocess
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import soundfile as sf, numpy as np, optuna
from dense32_kurtosis_sweep import tail_kurt, PRIMES, _nearest_unused, BASE_MEAN

REPO = Path(__file__).resolve().parents[4]
REND = str(REPO / "build/tests/duskverb_render/duskverb_render")
FC   = str(Path(__file__).resolve().parent / "full_check.py")
ANCH = Path.home() / "projects/dusk-audio-tools/tuner_runs/anchors/vvv-bright-hall"
APREF = "vvv-bright-hall"
WET  = ["--param", "Dry/Wet=1.0", "--param", "Bus Mode=1", "--param", "Freeze=0",
        "--sustained-pink-seconds", "4.0"]
STIM = ["impulse", "noiseburst", "snare", "sine1k", "sustained"]

# Warm-start center = the kurtosis-optimal 32 delays (baked into BH on 2026-06-10).
CENTER = [1171, 1237, 1301, 1381, 1471, 1511, 1607, 1699,
          1811, 1931, 2069, 2129, 2267, 2371, 2539, 2647,
          2789, 3037, 3089, 3307, 3571, 3767, 3967, 4153,
          4391, 4657, 4937, 5227, 5471, 5779, 6131, 6577]
_N = len(CENTER)
WIN = 90                       # +/- sample window around each center, prime-snapped
KURT_CEIL = 16.0               # hold the metal win (kurtosis-optimal set is 14.3)
BASELINE_NFAIL = 9             # BH algo-10 AccurateHall — the never-worse bar


def build_delays(offsets):
    used = set(); out = []
    for i, off in enumerate(offsets):
        p = _nearest_unused(CENTER[i] + off, used)
        used.add(p); out.append(p)
    return sorted(out)


def rms(p):
    x, _ = sf.read(p); m = x.mean(axis=1) if x.ndim > 1 else x
    return float(np.sqrt(np.mean(m * m)))


def eval_delays(delays, tag):
    """Render BH (algo 12) with these delays, gain-match, full_check -> (n_fail, kurt).
    Fully exception-contained: ANY failure (bad render, unreadable WAV, dir race)
    returns (None, None) so one bad trial can't kill the whole study.optimize."""
    dv = f"/tmp/j32_{tag}"; lex = f"/tmp/j32_{tag}_lex"
    try:
        shutil.rmtree(dv, ignore_errors=True); shutil.rmtree(lex, ignore_errors=True)
        os.makedirs(dv, exist_ok=True); os.makedirs(lex, exist_ok=True)
        env = dict(os.environ, DUSKVERB_FDN_DELAYS=",".join(str(x) for x in delays))
        r = subprocess.run([REND, "--program", "Bright Hall", "--output-dir", dv, *WET],
                           cwd=str(REPO), capture_output=True, text=True, env=env, timeout=180)
        nb = glob.glob(f"{dv}/*_noiseburst.wav")
        if r.returncode != 0 or not nb:
            return None, None
        for s in STIM:
            src = ANCH / f"{APREF}_{s}.wav"
            if src.exists(): shutil.copy(src, f"{lex}/anchor_{s}.wav")
        a = rms(f"{lex}/anchor_noiseburst.wav"); d = rms(nb[0])
        if d < 1e-12: return None, None
        g = a / d
        for f in glob.glob(f"{dv}/*.wav"):
            x, sr = sf.read(f); sf.write(f, x * g, sr)
        r = subprocess.run([sys.executable, FC, dv, lex, "--name", "Bright Hall", "--json"],
                           capture_output=True, text=True, timeout=200)
        nfail = None
        for line in r.stdout.splitlines():
            if line.startswith("JSON_RESULT:"):
                nfail = json.loads(line.split("JSON_RESULT: ")[1])["n_fail"]; break
        kurt = tail_kurt(nb[0], 2000, 14000)
        return nfail, kurt
    except Exception:                              # noqa: BLE001 — report-and-continue
        return None, None
    finally:
        # Free the per-trial renders immediately — 90 trials x (dv+lex) sustained
        # WAVs otherwise fill the tmpfs (ENOSPC mid-sweep). Keep nothing.
        shutil.rmtree(dv, ignore_errors=True); shutil.rmtree(lex, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trials", type=int, default=90)
    ap.add_argument("--workers", type=int, default=5)
    a = ap.parse_args()

    nf0, k0 = eval_delays(CENTER, "warm")
    k0s = f"{k0:.1f}" if k0 is not None else "n/a"
    print(f"warm-start (kurtosis-optimal set): n_fail={nf0}  kurt2-14k={k0s}  "
          f"(baseline algo-10 = {BASELINE_NFAIL})\n")

    def obj(trial):
        offs = [trial.suggest_int(f"o{i}", -WIN, WIN) for i in range(_N)]
        delays = build_delays(offs)
        nf, kurt = eval_delays(delays, str(trial.number))
        if nf is None: return 1e3
        loss = float(nf)
        if kurt is not None:
            loss += 0.5 * max(0.0, kurt - KURT_CEIL)         # hold the metal win
        loss += 0.01 * abs(float(np.mean(delays)) - BASE_MEAN)  # hold T60
        trial.set_user_attr("delays", delays)
        trial.set_user_attr("nfail", nf)
        trial.set_user_attr("kurt", round(kurt, 1) if kurt is not None else None)
        return loss

    # SQLite storage: in-memory studies vanish on crash (an ENOSPC mid-run lost a
    # whole pass once). Persist so best survives interruption and is queryable live.
    study = optuna.create_study(direction="minimize", study_name="j32",
                                storage="sqlite:////tmp/j32_study.db",
                                load_if_exists=True,
                                sampler=optuna.samplers.TPESampler(seed=42))
    # NB: do NOT enqueue_trial(CENTER) — enqueue + n_jobs>1 + SQLite storage
    # double-tells the seeded trial ("Cannot tell a COMPLETE trial" crash). The
    # warm-start is already measured+printed above; TPE explores the CENTER
    # neighbourhood anyway since every offset window is centred on 0.
    optuna.logging.set_verbosity(optuna.logging.WARNING)
    study.optimize(obj, n_trials=a.trials, n_jobs=a.workers)

    bt = study.best_trial
    if "delays" not in bt.user_attrs:
        # Every trial hit the 1e3 sentinel (no successful render) → best_trial carries
        # no user_attrs. Report cleanly instead of crashing on the KeyError.
        print(f"\nAll {a.trials} trials failed (no successful render); nothing to report.")
        return
    best = bt.user_attrs["delays"]
    print(f"\nBEST  loss={study.best_value:.2f}  n_fail={bt.user_attrs['nfail']}  "
          f"kurt2-14k={bt.user_attrs['kurt']}  mean={np.mean(best):.0f}")
    print(f"  vs baseline algo-10 n_fail {BASELINE_NFAIL} | warm-start n_fail {nf0} kurt {k0s}")
    print("\nstatic constexpr int kBrightHall32Delays[32] = {")
    for r in range(0, 32, 8):
        print("    " + ", ".join(f"{v:4d}" for v in best[r:r + 8]) + ",")
    print("};")

if __name__ == "__main__":
    main()
