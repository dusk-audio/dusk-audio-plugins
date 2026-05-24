#!/usr/bin/env python3
"""
Optuna optimizer: tune a DuskVerb factory preset to match an external
reference impulse (e.g. a Valhalla Vintage Verb vpreset render).

Per-trial flow:
  1. Optuna TPE samples values for the 14 free APVTS params.
  2. Subprocess runs the render harness with the parent factory preset
     selected + the 14 --param overrides, into a per-trial output dir
     so parallel workers do not collide.
  3. metrics_external.compare() scores the resulting impulse WAV
     against the target reference IR.
  4. The combined multi-metric loss is returned to Optuna.

Locked params (forced for fair A/B):
  Dry/Wet = 1.0, Bus Mode = 1, Pre-Delay = factory, Early Ref Level/Size
  = factory, Freeze = 0, Mono Below = factory, Gain Trim = 0.

Free params (14):
  Decay Time, Size, Mod Depth, Mod Rate, Treble Multiply, Bass Multiply,
  Mid Multiply, Low Crossover, High Crossover, Diffusion, Lo Cut,
  Hi Cut, Width, Saturation.

CLI:
  --target-ir   path to reference WAV (e.g. VVV vpreset render)
  --dv-preset   DV preset name (must exist in render.cpp's getPresetByName)
  --trials      Optuna budget (default 1500)
  --workers     parallel workers via Optuna n_jobs (default 4)
  --study-name  optional name for sqlite study persistence
  --storage     optional sqlite URL ('sqlite:///tuner.db') for resuming
  --vst3        path to DuskVerb VST3 (default ~/.vst3/DuskVerb.vst3)
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import optuna

# Make sibling module importable when invoked as a script.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from metrics_external import compare, compute_metrics  # noqa: E402


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

REPO_ROOT = Path(__file__).resolve().parents[4]
RENDER_BIN = REPO_ROOT / "build" / "tests" / "duskverb_render" / "duskverb_render"
DEFAULT_VST3 = Path.home() / ".vst3" / "DuskVerb.vst3"

# Free parameter search space. Ranges chosen to cover the full sensible
# APVTS range for each axis (matches plugin's RangedAudioParameter).
FREE_PARAMS = {
    "Decay Time":      (0.20,   12.00),    # seconds
    "Size":            (0.10,    1.00),
    "Mod Depth":       (0.00,    0.60),
    "Mod Rate":        (0.05,    3.00),    # Hz
    "Treble Multiply": (0.05,    4.00),
    "Bass Multiply":   (0.10,    4.00),
    "Mid Multiply":    (0.10,    4.00),
    "Low Crossover":   (100.0, 2000.0),    # Hz
    "High Crossover":  (1000.0, 12000.0),  # Hz
    "Diffusion":       (0.00,    1.00),
    "Lo Cut":          (20.0,   500.0),    # Hz
    "Hi Cut":          (4000.0, 20000.0),  # Hz
    "Width":           (0.50,    1.50),
    "Saturation":      (0.00,    0.40),
}

# Locked overrides applied on top of the preset's factory baseline.
# Forces 100% wet bus rendering with neutral gain trim so the optimizer
# cannot game volume via Gain Trim or mix.
LOCKED_OVERRIDES = {
    "Dry/Wet": 1.0,
    "Bus Mode": 1,
    "Freeze": 0,
    "Gain Trim": 0.0,
}


# ---------------------------------------------------------------------------
# Subprocess render
# ---------------------------------------------------------------------------

def render_trial(
    preset_name: str,
    overrides: dict,
    vst3_path: Path,
    out_dir: Path,
    timeout_s: float = 60.0,
) -> Path | None:
    """
    Run the harness once with the given param overrides. Returns the path
    to the resulting <preset>_impulse.wav, or None on failure/timeout.
    """
    out_dir.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(RENDER_BIN),
        "--vst3", str(vst3_path),
        "--output-dir", str(out_dir),
    ]
    for name, value in overrides.items():
        cmd += ["--param", f"{name}={value}"]
    cmd.append(preset_name)

    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=timeout_s,
        )
    except subprocess.TimeoutExpired:
        return None

    if result.returncode != 0:
        # First failure of a worker should be visible; subsequent failures
        # are suppressed by Optuna's trial-failure handling.
        sys.stderr.write(f"render failed (rc={result.returncode}):\n")
        sys.stderr.write(result.stderr[-500:] + "\n")
        return None

    # Harness writes "<PresetTokenName>_impulse.wav" — strip spaces and quotes.
    preset_token = "".join(c for c in preset_name if c.isalnum() or c in "+-_'")
    impulse = out_dir / f"{preset_token}_impulse.wav"
    if not impulse.exists():
        # Try the slug form (lowercased, hyphens). Some harness branches do this.
        candidates = sorted(out_dir.glob("*_impulse.wav"))
        if candidates:
            impulse = candidates[0]
        else:
            return None
    return impulse


# ---------------------------------------------------------------------------
# Optuna objective
# ---------------------------------------------------------------------------

def make_objective(
    target_ir: Path,
    preset_name: str,
    vst3_path: Path,
    trial_root: Path,
    fail_loss: float = 1000.0,
):
    """
    Returns an Optuna objective closure that renders one trial via
    the harness and returns the multi-metric loss vs the target IR.
    """
    target_ir = str(target_ir)

    def objective(trial: optuna.Trial) -> float:
        # Sample free params.
        overrides = dict(LOCKED_OVERRIDES)
        for name, (lo, hi) in FREE_PARAMS.items():
            overrides[name] = trial.suggest_float(name, lo, hi)

        # Per-trial output dir keeps parallel workers from colliding.
        out_dir = trial_root / f"trial_{trial.number:05d}"
        impulse = render_trial(
            preset_name=preset_name,
            overrides=overrides,
            vst3_path=vst3_path,
            out_dir=out_dir,
        )
        if impulse is None:
            return fail_loss

        try:
            loss, breakdown = compare(str(impulse), target_ir)
        except Exception as exc:
            sys.stderr.write(f"compare() raised: {exc}\n")
            return fail_loss

        # Tag the trial with breakdown for inspection.
        for k, v in breakdown.items():
            if isinstance(v, (int, float)):
                trial.set_user_attr(k, float(v))

        # Best-effort cleanup so the trial root doesn't balloon.
        # Keep impulse for the best trial — we'll re-load it from
        # trial.user_attrs path. For other trials, drop the whole dir.
        # Optuna keeps the params, so we can re-render the winner later.
        try:
            shutil.rmtree(out_dir, ignore_errors=True)
        except Exception:
            pass

        return loss

    return objective


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target-ir", required=True,
                    help="Reference IR WAV (e.g. VVV vpreset render).")
    ap.add_argument("--dv-preset", required=True,
                    help="DuskVerb preset name as known to the render harness.")
    ap.add_argument("--trials", type=int, default=1500,
                    help="Optuna trial budget (default 1500).")
    ap.add_argument("--workers", type=int, default=4,
                    help="Parallel worker count via Optuna n_jobs (default 4).")
    ap.add_argument("--vst3", default=str(DEFAULT_VST3),
                    help=f"DuskVerb VST3 path (default {DEFAULT_VST3}).")
    ap.add_argument("--study-name", default=None,
                    help="Study name for sqlite persistence.")
    ap.add_argument("--storage", default=None,
                    help="Optuna storage URL (sqlite:///path.db). Default: in-memory.")
    ap.add_argument("--seed", type=int, default=42,
                    help="TPE sampler seed (default 42).")
    args = ap.parse_args()

    target_ir = Path(args.target_ir)
    if not target_ir.is_file():
        sys.exit(f"target IR not found: {target_ir}")
    vst3 = Path(args.vst3)
    if not vst3.exists():
        sys.exit(f"VST3 not found: {vst3}")
    if not RENDER_BIN.exists():
        sys.exit(f"render binary not found: {RENDER_BIN}  (build duskverb_render first)")

    # Compute target metrics once so we can print them up front.
    print(f"Loading target IR: {target_ir}")
    target = compute_metrics(str(target_ir))
    print(f"  Target RT60       = {target['rt60']:.3f} s")
    print(f"  Target Cent  50ms = {target['cent_50']:.0f} Hz")
    print(f"  Target Cent 500ms = {target['cent_500']:.0f} Hz")
    print(f"  Target Stereo r   = {target['stereo_corr']:+.3f}")
    print(f"  Target Env P2P    = {target['env_res_p2p']:.2f} dB")
    print()

    trial_root = Path(tempfile.mkdtemp(prefix="dv_optuna_"))
    print(f"Trial scratch dir: {trial_root}")

    sampler = optuna.samplers.TPESampler(seed=args.seed)
    study_kwargs = {"direction": "minimize", "sampler": sampler}
    if args.study_name:
        study_kwargs["study_name"] = args.study_name
    if args.storage:
        study_kwargs["storage"] = args.storage
        study_kwargs["load_if_exists"] = True
    study = optuna.create_study(**study_kwargs)

    objective = make_objective(
        target_ir=target_ir,
        preset_name=args.dv_preset,
        vst3_path=vst3,
        trial_root=trial_root,
    )

    print(f"Starting {args.trials} trials with {args.workers} parallel workers...")
    t0 = time.time()
    study.optimize(
        objective,
        n_trials=args.trials,
        n_jobs=args.workers,
        show_progress_bar=False,
    )
    elapsed = time.time() - t0
    print(f"\nFinished in {elapsed/60:.1f} min ({args.trials} trials, {args.workers} workers)")

    # Report
    best = study.best_trial
    print()
    print("=" * 70)
    print(f"BEST TRIAL #{best.number}   loss = {best.value:.6f}")
    print("=" * 70)
    print("\nBest params:")
    for k, v in sorted(best.params.items()):
        print(f"  {k:18s} = {v:.4f}")

    ua = best.user_attrs
    if ua:
        print("\nBest metrics vs target:")
        print(f"  RT60         DV={ua.get('rt60_dv', 0):.3f}s   VVV={ua.get('rt60_vvv', 0):.3f}s    Δ={((ua.get('rt60_dv', 0) - ua.get('rt60_vvv', 0)) / max(ua.get('rt60_vvv', 1), 1e-6) * 100):+.1f}%")
        print(f"  Cent 50ms    DV={ua.get('cent50_dv', 0):.0f}Hz  VVV={ua.get('cent50_vvv', 0):.0f}Hz   Δ={((ua.get('cent50_dv', 0) - ua.get('cent50_vvv', 0)) / max(ua.get('cent50_vvv', 1), 1e-6) * 100):+.1f}%")
        print(f"  Cent 500ms   DV={ua.get('cent500_dv', 0):.0f}Hz  VVV={ua.get('cent500_vvv', 0):.0f}Hz   Δ={((ua.get('cent500_dv', 0) - ua.get('cent500_vvv', 0)) / max(ua.get('cent500_vvv', 1), 1e-6) * 100):+.1f}%")
        print(f"  Stereo r     DV={ua.get('stereo_dv', 0):+.3f}    VVV={ua.get('stereo_vvv', 0):+.3f}    Δ={(ua.get('stereo_dv', 0) - ua.get('stereo_vvv', 0)):+.3f}")
        print(f"  Env P2P      DV={ua.get('envP2P_dv', 0):.2f}dB  VVV={ua.get('envP2P_vvv', 0):.2f}dB  Δ={(ua.get('envP2P_dv', 0) - ua.get('envP2P_vvv', 0)):+.2f}dB")
        print(f"  Spec L1      = {ua.get('spec_l1_db', 0):.3f} dB")

    # Dump the best params as JSON for downstream tooling.
    best_json = trial_root / "best.json"
    payload = {
        "preset": args.dv_preset,
        "target_ir": str(target_ir),
        "best_loss": best.value,
        "best_params": best.params,
        "best_metrics": {k: v for k, v in best.user_attrs.items()},
        "n_trials": args.trials,
        "elapsed_sec": elapsed,
    }
    best_json.write_text(json.dumps(payload, indent=2))
    print(f"\nBest trial JSON: {best_json}")

    # Done — leave trial_root in /tmp so caller can re-render the winner.
    print(f"Scratch dir kept at: {trial_root}")


if __name__ == "__main__":
    main()
