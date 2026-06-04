#!/usr/bin/env python3
"""
Phase β: 16-int FDN base-delay sweep for matching VVV per-band Hilbert peaks.

For each trial:
  1) sample 16 ints in [kMin, kMax]
  2) export DUSKVERB_FDN_DELAYS env, render through harness
  3) measure per-band Hilbert peak frequency on noiseburst tail
  4) loss = sum of relative errors vs VVV anchor targets

Run with:
  python3 baseDelay_sweep.py --preset "Vocal Hall" --trials 300 --workers 6 \
                              --anchor /tmp/anchor_vh --out /tmp/vh_sweep

Best trial's delays are printed and dumped to <out>/best_delays.csv.
"""

import argparse
import json
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

K_MIN = 700           # ~16ms @ 48kHz lower bound
K_MAX = 6700          # FDNReverb::kMaxBaseDelay
N_LINES = 16

# Per-band Hilbert targets in Hz. Bands match full_check.py band edges.
PRESET_TARGETS = {
    "Vocal Hall":  {"bass": (40, 250, 1.92),
                    "lowmid": (250, 1000, 1.56),
                    "high": (4000, 12000, 2.47)},
    "Bright Hall": {"bass": (40, 250, 3.02),
                    "lowmid": (250, 1000, 2.01),
                    "mid":   (1000, 4000, 4.76),
                    "high":  (4000, 12000, 7.23)},
}


sys.path.insert(0, str(Path(__file__).resolve().parent))
from full_check import _tail_mod_peak_freq  # noqa: E402


def render_and_measure(delays, preset, anchor_targets, out_dir):
    """Run the harness with override; return per-band Hilbert peaks dict."""
    env = os.environ.copy()
    env["DUSKVERB_FDN_DELAYS"] = ",".join(str(d) for d in delays)
    cmd = [
        str(RENDER), "--vst3", str(VST3),
        "--program", preset,
        "--param", "Dry/Wet=1.0",
        "--param", "Bus Mode=On",
        "--prerun-seconds", "5",
        "--output-dir", str(out_dir),
    ]
    try:
        res = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        print(f"render timeout (>120s) for {preset}", file=sys.stderr)
        return None
    if res.returncode != 0:
        return None
    slug = preset.replace(" ", "")
    wav = out_dir / f"{slug}_noiseburst.wav"
    if not wav.exists():
        return None
    measured = {}
    for band, (lo, hi, _target) in anchor_targets.items():
        peak = _tail_mod_peak_freq(str(wav), 1.0, 3.0, lo, hi)
        if peak is not None:
            measured[band] = peak
    return measured


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preset", required=True, choices=list(PRESET_TARGETS.keys()))
    ap.add_argument("--trials", type=int, default=300)
    ap.add_argument("--workers", type=int, default=6)
    ap.add_argument("--out", required=True, help="Output dir for best WAVs + log")
    ap.add_argument("--study-name", default=None)
    ap.add_argument("--storage", default=None,
                    help="Optional sqlite URL for resumable study")
    args = ap.parse_args()

    targets = PRESET_TARGETS[args.preset]
    out_root = Path(args.out)
    out_root.mkdir(parents=True, exist_ok=True)
    print(f"Preset: {args.preset}")
    print("Per-band Hilbert targets (Hz):")
    for band, (lo, hi, t) in targets.items():
        print(f"  {band:8s} {lo:5d}-{hi:5d} Hz  target {t:.2f} Hz")

    def objective(trial):
        delays = []
        for i in range(N_LINES):
            delays.append(trial.suggest_int(f"d{i:02d}", K_MIN, K_MAX))
        delays_sorted = sorted(delays)
        tmp = Path(tempfile.mkdtemp(prefix=f"sweep_{trial.number}_"))
        try:
            measured = render_and_measure(delays_sorted, args.preset, targets, tmp)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
        if measured is None:
            return 10.0
        loss = 0.0
        weights = {"bass": 1.0, "lowmid": 1.0, "mid": 1.0, "high": 2.0}
        for band, (lo, hi, target) in targets.items():
            w = weights.get(band, 1.0)
            if band not in measured:
                loss += 1.0 * w
                continue
            err = abs(measured[band] - target) / target
            loss += err * w
            trial.set_user_attr(f"{band}_peak", measured[band])
        trial.set_user_attr("delays", delays_sorted)
        return loss

    storage = args.storage
    study_name = args.study_name or f"baseDelay_{args.preset.replace(' ', '_')}"
    if storage:
        study = optuna.create_study(study_name=study_name, storage=storage,
                                     load_if_exists=True,
                                     direction="minimize",
                                     sampler=optuna.samplers.TPESampler(seed=42))
    else:
        study = optuna.create_study(direction="minimize",
                                     sampler=optuna.samplers.TPESampler(seed=42))

    study.optimize(objective, n_trials=args.trials, n_jobs=args.workers)

    best = study.best_trial
    print("\n========== BEST ==========")
    print(f"Trial {best.number}  loss={best.value:.4f}")
    for band, (lo, hi, target) in targets.items():
        m = best.user_attrs.get(f"{band}_peak")
        if m is not None:
            err = abs(m - target) / target * 100.0
            print(f"  {band:8s} measured={m:5.2f} Hz  target={target:5.2f}  err={err:5.1f}%")
    delays = best.user_attrs["delays"]
    print(f"\n  delays: {delays}")
    csv = ",".join(str(d) for d in delays)
    print(f"  csv: {csv}")
    (out_root / "best_delays.csv").write_text(csv + "\n")
    (out_root / "best.json").write_text(json.dumps({
        "trial": best.number,
        "loss": best.value,
        "delays": delays,
        "measured": {b: best.user_attrs.get(f"{b}_peak") for b in targets},
        "targets": {b: t for b, (_lo, _hi, t) in targets.items()},
    }, indent=2))

    # Render the best one to out_root for visual inspection
    env = os.environ.copy()
    env["DUSKVERB_FDN_DELAYS"] = csv
    subprocess.run([
        str(RENDER), "--vst3", str(VST3),
        "--program", args.preset,
        "--param", "Dry/Wet=1.0",
        "--param", "Bus Mode=On",
        "--prerun-seconds", "5",
        "--sustained-pink-seconds", "4",
        "--output-dir", str(out_root),
    ], env=env, check=False, capture_output=True)
    print(f"\nBest render in {out_root}")


if __name__ == "__main__":
    main()
