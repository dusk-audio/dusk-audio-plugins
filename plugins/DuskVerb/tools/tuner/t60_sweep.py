#!/usr/bin/env python3
"""Focused sweep over Decay / Bass Mult / Treble Mult / Hi Cut Shelf / Mid
Multiply for per-band T60 matching. Holds baked baseDelays via map lookup."""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import optuna

REPO = Path(__file__).resolve().parents[4]
RENDER = REPO / "build" / "tests" / "duskverb_render" / "duskverb_render"
VST3 = Path.home() / ".vst3" / "DuskVerb.vst3"

sys.path.insert(0, str(Path(__file__).resolve().parent))
from full_check import _t60_band_schroeder  # noqa: E402

PRESET_TARGETS = {
    "Vocal Hall": {
        63: 5.57, 125: 5.23, 250: 5.03, 500: 4.20,
        1000: 3.18, 2000: 2.74, 4000: 2.45, 8000: 2.04, 16000: 1.70,
    },
    "Bright Hall": {
        63: 7.30, 125: 7.10, 250: 6.12, 500: 5.59,
        1000: 4.73, 2000: 4.15, 4000: 3.37, 8000: 2.36, 16000: 1.37,
    },
}

BANDS = [(45, 90, 63), (90, 180, 125), (180, 350, 250), (350, 700, 500),
         (700, 1400, 1000), (1400, 2800, 2000), (2800, 5600, 4000),
         (5600, 11200, 8000), (11200, 22000, 16000)]


def render_trial(out_dir, preset, decay, bass, treble, mid, hicut_shelf, low_xover):
    cmd = [
        str(RENDER), "--vst3", str(VST3),
        "--program", preset,
        "--param", "Dry/Wet=1.0",
        "--param", "Bus Mode=On",
        "--param", f"Decay Time={decay}",
        "--param", f"Bass Multiply={bass}",
        "--param", f"Treble Multiply={treble}",
        "--param", f"Mid Multiply={mid}",
        "--param", f"Hi Cut Shelf={hicut_shelf}",
        "--param", f"Low Crossover={low_xover}",
        "--prerun-seconds", "5",
        "--output-dir", str(out_dir),
    ]
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=120).returncode == 0
    except subprocess.TimeoutExpired:
        print(f"render timeout (>120s) for {preset}", file=sys.stderr)
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preset", required=True, choices=list(PRESET_TARGETS.keys()))
    ap.add_argument("--trials", type=int, default=300)
    ap.add_argument("--workers", type=int, default=6)
    args = ap.parse_args()

    targets = PRESET_TARGETS[args.preset]
    slug = args.preset.replace(" ", "")

    def objective(trial):
        decay = trial.suggest_float("decay", 2.0, 8.0)
        bass = trial.suggest_float("bass", 1.0, 2.0)
        treble = trial.suggest_float("treble", 1.0, 2.0)
        mid = trial.suggest_float("mid", 0.7, 1.3)
        hicut_shelf = trial.suggest_float("hicut_shelf", -12.0, 0.0)
        low_xover = trial.suggest_float("low_xover", 200.0, 1000.0)

        tmp = Path(tempfile.mkdtemp(prefix=f"t60_{trial.number}_"))
        try:
            ok = render_trial(tmp, args.preset, decay, bass, treble, mid,
                              hicut_shelf, low_xover)
            if not ok:
                return 100.0
            wav = tmp / f"{slug}_noiseburst.wav"
            if not wav.exists():
                return 100.0
            loss = 0.0
            measured = {}
            for lo, hi, center in BANDS:
                t60 = _t60_band_schroeder(str(wav), lo, hi)
                if t60 is None:
                    loss += 1.0
                    continue
                measured[center] = t60
                target = targets[center]
                err = abs(t60 - target) / target
                loss += err
            for c, m in measured.items():
                trial.set_user_attr(f"t60_{c}", m)
            return loss
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    study = optuna.create_study(direction="minimize",
                                 sampler=optuna.samplers.TPESampler(seed=42))
    study.optimize(objective, n_trials=args.trials, n_jobs=args.workers)

    best = study.best_trial
    # Abort only when NO real per-band T60 was recorded (every trial hit the render-
    # fail sentinel). A high best.value alone is NOT a failure — loss is a sum of
    # relative band errors and can legitimately exceed 100 on a poor-but-measured run.
    if not any(f"t60_{c}" in best.user_attrs for _, _, c in BANDS):
        print(f"\nAll trials failed to render/measure (best loss {best.value:.1f}); "
              "no valid T60 measurement — nothing to report.")
        return
    print("\n========== BEST ==========")
    print(f"Trial {best.number}  loss={best.value:.4f}")
    print("  params:")
    for k, v in best.params.items():
        print(f"    {k:14s} = {v:.4f}")
    print("  T60 per band:")
    for lo, hi, c in BANDS:
        m = best.user_attrs.get(f"t60_{c}")
        if m is None:
            continue
        t = targets[c]
        e = abs(m - t) / t * 100.0
        print(f"    {c:>5d} Hz  DV={m:.2f}s  VVV={t:.2f}s  err={e:5.1f}%")


if __name__ == "__main__":
    main()
