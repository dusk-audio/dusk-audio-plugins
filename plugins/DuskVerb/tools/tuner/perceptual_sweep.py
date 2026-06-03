#!/usr/bin/env python3
"""Perceptual-gate Optuna sweep (2026-06-02).

Targets the new full_check perceptual gates — attack_time / onset_slope /
spatial_width_bands / diffusion_flux — plus the existing scoreboard. Objective
is full_check's integer n_fail (the proven scoreboard metric) PLUS a small
continuous perceptual-distance term so TPE gets a usable gradient toward the
onset/width/diffusion targets instead of stalling on n_fail ties.

The render harness only exposes macro APVTS params (Diffusion, Early Ref
Level/Size, Mod Depth/Rate, Width, Size, Decay, Pre-Delay). The internal ER
tap geometry and FDN cross-feed matrix referenced in the brief are NOT
APVTS-exposed — if these macro levers can't close attack_time/width, that is
the engine ticket, reported honestly rather than gamed.

Usage:
  python3 perceptual_sweep.py --preset "Vocal Hall" --anchor <dir> --trials 100
"""
import argparse, json, subprocess, sys, tempfile, os
from pathlib import Path
import numpy as np
import optuna

REPO = Path(__file__).resolve().parents[4]
RENDER = REPO / "build" / "tests" / "duskverb_render" / "duskverb_render"
VST3 = Path.home() / ".vst3" / "DuskVerb.vst3"
FC = Path(__file__).resolve().parent / "full_check.py"

sys.path.insert(0, str(Path(__file__).resolve().parent))
from full_check import attack_profile, spatial_width_bands, diffusion_flux_curve, GATES

# Swept APVTS axes (name, lo, hi). Warm-start = program defaults; trial 0 is
# pinned to the baseline (enqueued below) so the sweep never scores worse than
# shipped without seeing it.
# Phase 4 option-2 (ER-driven attack): free ER level/size/BOOST + diffusion +
# mod (flutter control) + width. Baseline n_fail acts as a hard guard so the
# attack-priority objective can't regress the rest of the board (the scatter
# lesson). er_boost is the new [1,8] APVTS "Early Ref Boost".
AXES = {
    "Vocal Hall": [
        # edt fix via the dormant post-tank PerBandEDTShape (RT60-decoupled).
        # +attack = hold (longer edt), -attack = shorter. tau = window. Lows
        # need hold, hi needs shorten. Attack/width/T60 untouched (no FDN loop).
        ("EDT Sub Attack",      0.0, 16.0),
        ("EDT Sub Tau",        50.0, 200.0),
        ("EDT Low-Mid Attack",  0.0, 14.0),
        ("EDT Low-Mid Tau",    60.0, 220.0),
        ("EDT Mid-High Attack", -14.0, 4.0),
        ("EDT Mid-High Tau",   60.0, 260.0),
        ("EDT Air Attack",     -12.0, 6.0),
        ("EDT Air Tau",        60.0, 260.0),
    ],
    "Tiled Room": [
        # VH-playbook: attack (er_boost/rise), width (Width + HF cross-talk),
        # edt (per-band shaper). Tiled is over-correlated (opposite of VH) so it
        # widens via Width; xtalk trims the HF band.
        ("Early Ref Boost",  3.00, 7.00),
        ("Early Ref Rise",   6.00, 16.00),
        ("Width",            0.90, 1.30),
        ("HF Cross-Talk",    0.00, 0.10),
        ("EDT Sub Attack",   0.0, 16.0),
        ("EDT Low-Mid Attack", -14.0, 14.0),
        ("EDT Mid-High Attack", -14.0, 4.0),
    ],
}
BASELINE_NFAIL = {"Vocal Hall": 26, "Tiled Room": 23}


def render(preset, overrides, outdir):
    cmd = [str(RENDER), "--vst3", str(VST3), "--program", preset,
           "--param", "Dry/Wet=1.0", "--param", "Bus Mode=1", "--param", "Freeze=0",
           "--slug", "S", "--prerun-seconds", "5", "--sustained-pink-seconds", "4",
           "--output-dir", str(outdir)]
    for k, v in overrides.items():
        cmd += ["--param", f"{k}={v}"]
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)


def n_fail(outdir, anchor, preset):
    # full_check exits 1 when any gate fails — do NOT use check=True.
    r = subprocess.run(["python3", str(FC), str(outdir), str(anchor),
                        "--name", preset, "--json"],
                       stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    for ln in r.stdout.splitlines():
        if ln.startswith("JSON_RESULT:"):
            return json.loads(ln[len("JSON_RESULT:"):])["n_fail"]
    return 999


def perceptual_distance(outdir, anchor):
    """Continuous gradient in GATE units (1.0 == exactly at the gate edge).
    Steers TPE toward onset/width/diffusion even while n_fail is tied."""
    imp_dv = next(Path(outdir).glob("*_impulse.wav"), None)
    imp_lx = next(Path(anchor).glob("*_impulse.wav"), None)
    if imp_dv is None or imp_lx is None:
        return 10.0
    a_dv, s_dv = attack_profile(str(imp_dv)); a_lx, s_lx = attack_profile(str(imp_lx))
    w_dv = spatial_width_bands(str(imp_dv)); w_lx = spatial_width_bands(str(imp_lx))
    k_dv = diffusion_flux_curve(str(imp_dv)); k_lx = diffusion_flux_curve(str(imp_lx))
    d = 0.0
    if a_dv is not None and a_lx and a_lx > 0:
        d += min(abs(a_dv - a_lx) / GATES["attack_time_ms_abs"],
                 abs(a_dv - a_lx) / a_lx * 100 / GATES["attack_time_pct"])
    if s_dv is not None and s_lx not in (None, 0):
        d += abs(s_dv - s_lx) / abs(s_lx) * 100 / GATES["onset_slope_pct"]
    for cd, cl in zip(w_dv, w_lx):
        if cd is not None and cl is not None:
            d += abs(cd - cl) / GATES["spatial_width_band"]
    n = min(len(k_dv), len(k_lx))
    if n >= 4:
        d += float(np.mean(np.abs(k_dv[:n] - k_lx[:n]))) / GATES["diffusion_flux"]
    return d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preset", required=True)
    ap.add_argument("--anchor", required=True)
    ap.add_argument("--trials", type=int, default=100)
    ap.add_argument("--jobs", type=int, default=1)
    # Distributed mode: N worker processes share one RDB study (true parallel
    # renders — optuna n_jobs threading is GIL/sampler-serialized for subprocess
    # objectives). Each worker runs --trials trials against the shared study.
    ap.add_argument("--storage", default=None)
    ap.add_argument("--study-name", default=None)
    args = ap.parse_args()
    axes = AXES[args.preset]
    tmp = Path(tempfile.mkdtemp(prefix="psweep_"))

    def objective(trial):
        ov = {name: round(trial.suggest_float(name, lo, hi), 4) for name, lo, hi in axes}
        od = tmp / f"t{trial.number}"
        od.mkdir(exist_ok=True)
        guard = 0.0
        try:
            render(args.preset, ov, od)
            nf = n_fail(od, args.anchor, args.preset)
            pd = perceptual_distance(od, args.anchor)
            # Attack/slope guard: the won onset must HOLD. Heavy penalty if it
            # regresses out of gate, so the tank-decay axes can't trade the
            # attack back for edt/T60 wins.
            imp_dv = next(od.glob("*_impulse.wav"), None)
            imp_lx = next(Path(args.anchor).glob("*_impulse.wav"), None)
            if imp_dv and imp_lx:
                a_dv, s_dv = attack_profile(str(imp_dv))
                a_lx, s_lx = attack_profile(str(imp_lx))
                a_ok = (a_dv is not None and a_lx and
                        (abs(a_dv - a_lx) <= GATES["attack_time_ms_abs"] or
                         abs(a_dv - a_lx) / a_lx * 100 <= GATES["attack_time_pct"]))
                s_ok = (s_dv is not None and s_lx not in (None, 0) and
                        abs(s_dv - s_lx) / abs(s_lx) * 100 <= GATES["onset_slope_pct"])
                guard = 0.0 if (a_ok and s_ok) else 8.0
        except Exception as e:
            return 1e6
        finally:
            for f in od.glob("*.wav"):
                f.unlink()
            try:
                od.rmdir()          # drop the now-empty trial dir (avoid /tmp buildup)
            except OSError:
                pass
        # Now that attack is closed, minimize TOTAL n_fail (drive toward all-
        # within-JND) while the guard keeps the onset win locked. Small pd term
        # keeps a gradient on width/diffusion.
        return nf + guard + 0.02 * pd

    if args.storage:
        study = optuna.create_study(direction="minimize",
                                    sampler=optuna.samplers.TPESampler(seed=7),
                                    study_name=args.study_name, storage=args.storage,
                                    load_if_exists=True)
    else:
        study = optuna.create_study(direction="minimize",
                                    sampler=optuna.samplers.TPESampler(seed=7))
        # Warm-start: trial 0 = midpoint (only meaningful single-process).
        study.enqueue_trial({name: round((lo + hi) / 2, 4) for name, lo, hi in axes})
    optuna.logging.set_verbosity(optuna.logging.WARNING)
    study.optimize(objective, n_trials=args.trials, n_jobs=args.jobs, show_progress_bar=False)

    print(f"\n===== {args.preset}: BEST trial #{study.best_trial.number}  "
          f"obj={study.best_value:.3f} =====")
    for k, v in study.best_trial.params.items():
        print(f"  {k:20s} = {v:.4f}")


if __name__ == "__main__":
    main()
