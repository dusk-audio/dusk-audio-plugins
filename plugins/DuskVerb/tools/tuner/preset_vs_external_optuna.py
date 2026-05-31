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
import re
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

# Free parameter search space. Ranges align with the plugin's APVTS
# RangedAudioParameter clamps (see PluginProcessor.cpp). Sampling outside
# the APVTS range wastes trial budget because the setter clamps and many
# trials produce identical effective values.
FREE_PARAMS = {
    "Decay Time":      (0.20,   12.00),    # APVTS [0.2, 30.0] — 12s ceiling matches longest current preset
    "Size":            (0.10,    1.00),    # APVTS [0.0, 1.0]
    "Mod Depth":       (0.00,    0.60),    # APVTS [0.0, 1.0] — 0.6 keeps tail from over-modulating
    "Mod Rate":        (0.10,    3.00),    # APVTS [0.10, 10.0]. (NB: Drum Plate ripple/mud is Mod-DEPTH driven, not rate — a 0.5 Hz floor did NOT fix it, reverted 2026-05-30.)
    # Decay multipliers — clamped to gentle contouring range. Wider ranges
    # let Optuna abuse them as massive spectral gap-fillers (e.g. Bass Mult
    # 1.97 + Low Crossover 3740 forced the bass band to cover most of the
    # spectrum, mangling decay trajectories). Forcing EQ work onto bassLift_
    # / hfLift_ shelves keeps decay physics intact.
    "Treble Multiply": (0.50,    1.50),    # APVTS [0.10, 1.50] — clamped
    "Bass Multiply":   (0.50,    1.50),    # APVTS [0.30, 2.50] — clamped
    "Mid Multiply":    (0.50,    1.50),    # APVTS [0.30, 2.50] — clamped
    # Crossovers — clamped to physically realistic bass/treble band edges.
    # Below ~80 Hz the bass band stops separating from sub; above ~600 Hz it
    # collapses the mid band. High crossover above 10 kHz collapses HF into mid.
    "Low Crossover":   (80.0,   600.0),    # APVTS [200, 4000] — clamped
    "High Crossover":  (3000.0, 10000.0),  # APVTS [1000, 12000] — clamped
    "Diffusion":       (0.00,    1.00),    # APVTS [0.0, 1.0]
    "Lo Cut":          (20.0,    60.0),    # ceiling 60 (was 500): a >60 Hz HPF guts the kick fundamental + sub → audibly weak low end (Drum Plate listening 2026-05-31). APVTS [5, 500]
    "Hi Cut":          (3000.0, 20000.0),  # APVTS [1000, 20000]. Widened down to 3 kHz 2026-05-30 for dark presets (Cathedral HiCut=4261). Safe under the direct-scoreboard objective — the cent_50/bloom gates now police over-darkening, so the old "Optuna kills bright bloom" clamp is obsolete.
    "Width":           (0.50,    1.05),    # APVTS [0.0, 2.0] — clamped to mono-compatible range; >1.05 with sparse Dattorro taps produces anti-correlated L/R
    "Saturation":      (0.00,    0.40),    # APVTS [0.0, 1.0] — 0.4 cap; above is destructive
    # FiveBandDamping (Phase 3, FDN only) — the 4 new T60-shaping axes. Sub
    # decouples <120 Hz decay/energy from the low-mids; Hi-Mid bends the
    # 2k-8k decay slope that the old single mid-plateau forced flat. Transparent
    # when Sub Mult==bassMult & Hi-Mid Mult==trebleMult (warm-start seeds set
    # exactly that → trial 0 reproduces the locked floor).
    "Sub Multiply":    (0.10,    2.00),    # APVTS [0.1, 2.0]
    "Hi-Mid Multiply": (0.10,    2.00),    # APVTS [0.1, 2.0]
    "Sub Crossover":   (20.0,  200.0),     # APVTS [20, 200] Hz
    "Air Crossover":   (4000.0, 20000.0),  # APVTS [4000, 20000] Hz
    # Block 2 feed-forward input energy makeup (2026-05-31). Feed-forward shelves
    # on the input vector B → outside the loop → BIBO-safe; lifts the static
    # 1 kHz / sub energy scoops decay multipliers can't reach.
    "Input Sub Gain":  (-6.0,    6.0),     # APVTS [-6, 6] dB
    "Input Mid Gain":  (-6.0,    6.0),     # APVTS [-6, 6] dB
    # Block 2b: in-loop narrow peak GAIN at 1 kHz (freq/Q locked above). Fills
    # the modal null. Capped at +3.5 dB (engine also hard-clamps for stability).
    "In-Loop Peak Gain": (0.0,   2.0),     # APVTS [-12, 12] dB — swept [0, 2.0] (engine hard-caps +2.0: >+2 rings the 1kHz mode away on long-decay loops)
    # DattorroPlateVintage corrective EQ + brightness (algo=1 only). Optimizer
    # samples these unconditionally; on non-DPV engines the setters are no-ops
    # via DuskVerbEngine glue, so the values are wasted but harmless.
    "DPV HF Shelf Gain":   (-12.0,   24.0),    # APVTS [-12, 24] dB
    "DPV HF Shelf Freq":   (2000.0, 20000.0),  # APVTS [2000, 20000] Hz — extended to reach >12 kHz air band
    "DPV Struct HF Damp":  (2000.0, 18000.0),  # APVTS [2000, 18000] Hz
    "DPV Box Cut Gain":    (-12.0,    6.0),    # APVTS [-12, 6] dB (negative = corrective cut)
    "DPV Box Cut Freq":    (100.0,  800.0),    # APVTS [100, 800] Hz
    "DPV Bass Shelf Gain": (-6.0,   18.0),     # APVTS [-6, 18] dB
    "DPV Bass Shelf Freq": (60.0,   500.0),    # APVTS [60, 500] Hz
    # Gain Trim is now a FREE parameter — Optuna jointly optimizes loudness
    # match with every other axis. Previously locked at 0 + level-matched
    # post-sweep, which pushed absolute band energies hot (because the trim
    # adjustment happened AFTER the band loss was evaluated). Free range
    # [-12, +24] covers all observed snare-match offsets.
    "Gain Trim":            (-12.0, 24.0),     # APVTS [-48, 48] dB — clamped
}

# DPV (DattorroPlateVintage, algo=1) corrective-EQ params. Inert on every
# other engine — the setters are no-ops via DuskVerbEngine glue — so sampling
# them on an FDN/SixAP/etc. preset wastes 7 trial dimensions on dead axes and
# slows convergence. Gated OUT of the sampling loop unless --has-dpv is passed.
DPV_PARAMS = {
    "DPV HF Shelf Gain", "DPV HF Shelf Freq", "DPV Struct HF Damp",
    "DPV Box Cut Gain", "DPV Box Cut Freq", "DPV Bass Shelf Gain",
    "DPV Bass Shelf Freq",
}

# Locked overrides applied on top of the preset's factory baseline.
# Forces 100% wet bus rendering with neutral gain trim so the optimizer
# cannot game volume via Gain Trim or mix.
LOCKED_OVERRIDES = {
    "Dry/Wet": 1.0,
    "Bus Mode": 1,
    "Freeze": 0,
    # Block 2b (Drum Plate 1 kHz modal null): fix the EXISTING in-loop peaking
    # filter at 1 kHz / Q 2.5 and sweep only its gain (below) — a narrow boost
    # fills the notch without disturbing the broadband mid (which a wide input
    # bell could not). NB: preset-targeted; revisit if sweeping other presets.
    "In-Loop Peak Freq": 1000.0,
    "In-Loop Peak Q":    2.5,
    # Gain Trim was previously locked at 0 (post-sweep level-match handled it).
    # Now it's in FREE_PARAMS so Optuna optimizes loudness jointly with spectrum.
    # Post-sweep auto-trim is no longer needed and would re-introduce the
    # band-energy shift bug.
}


# ---------------------------------------------------------------------------
# Subprocess render
# ---------------------------------------------------------------------------

def render_trial(
    preset_name: str,
    overrides: dict,
    vst3_path: Path,
    out_dir: Path,
    stimulus: str = "noiseburst",
    prerun_seconds: float = 5.0,
    timeout_s: float = 120.0,
) -> Path | None:
    """
    Run the harness once with the given param overrides. Returns the path
    to the resulting <preset>_{stimulus}.wav, or None on failure/timeout.

    `stimulus` selects which harness-rendered stimulus file the optimizer
    measures against:
      'noiseburst' (default) — 50 ms pink noise burst then silence. The
        broadband + temporally-bounded stimulus that best matches what
        a listener actually hears (snare/vocal-chop response). Avoids the
        Dirac-impulse trap where time-variant reverbs respond differently
        to a 1-sample click than to real-world transients.
      'impulse' — legacy LTI fingerprint. Use only for static engines.
      'snare' — real percussive transient.

    `prerun_seconds` controls the warm-up silence before each stimulus
    fires. 5 s is the convergence-tested standard; <3 s misses steady-
    state modulator drift and produces measurements that don't match
    DAW perception.
    """
    out_dir.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(RENDER_BIN),
        "--vst3", str(vst3_path),
        "--output-dir", str(out_dir),
        "--prerun-seconds", str(prerun_seconds),
        # Render sustained-pink stimulus alongside noiseburst/snare/impulse.
        # Sustained-pink = 4s continuous input + 4s tail; captures the
        # engine's steady-state response under musical-content excitation.
        # The 100 ms noiseburst doesn't reach modal steady-state, so it
        # missed Lex's sustained low-end and HF damping. Cost: ~8s extra
        # render time per trial (acceptable on 1500-trial sweeps).
        "--sustained-pink-seconds", "4.0",
    ]
    for name, value in overrides.items():
        cmd += ["--param", f"{name}={value}"]
    # Canonical render path (2026-05-30): select the plugin's OWN factory
    # program via --program (applies the shipped FactoryPresets row as the
    # base), NOT the positional name — which routed through render.cpp's
    # hand-duplicated getPresetByName/makePreset table and drifted ~2 gates
    # from what ships (Cathedral: positional 25 vs --program 27). The sweep
    # now scores EXACTLY the canonical render, retiring the duplicate-table
    # class of bug. NB: preset_name MUST be an exact plugin PROGRAM name
    # (e.g. "Cathedral Large Hall"), not a render.cpp alias like "Cathedral".
    preset_token = "".join(c for c in preset_name if c.isalnum() or c in "+-_'")
    cmd += ["--slug", preset_token, "--program", preset_name]

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

    # Harness writes "<PresetTokenName>_<stimulus>.wav" — strip spaces and quotes.
    preset_token = "".join(c for c in preset_name if c.isalnum() or c in "+-_'")
    impulse = out_dir / f"{preset_token}_{stimulus}.wav"
    if not impulse.exists():
        # Try the slug form (lowercased, hyphens). Some harness branches do this.
        candidates = sorted(out_dir.glob(f"*_{stimulus}.wav"))
        if candidates:
            impulse = candidates[0]
        else:
            return None
    return impulse


# ---------------------------------------------------------------------------
# Optuna objective
# ---------------------------------------------------------------------------

def _parse_full_check_json(stdout: str):
    """Parse full_check.py --json output. Returns (n_fail, margin_sum), or
    (None, None) if the JSON_RESULT line is missing/malformed.

    margin_sum = Σ over failing gates of how far each sits OUTSIDE its gate,
    parsed from the gate strings. Three gate-string shapes are handled:
      • percentage   '... Δ= -44.2% gate=±5.0% ✗'   → |Δ|/tol − 1
      • symmetric dB '... Δ= -3.59 gate=±2.0 ✗'      → |Δ|/tol − 1
      • one-sided    '... Δ=-8.74 gate≤+1.5'/'gate≥-1.5' → signed excess
    Unparseable fails contribute a small constant so they still register. The
    fail-count term (×1000 in the objective) dominates; this is the smooth
    tiebreaker, so approximate margins are fine."""
    line = None
    for ln in stdout.splitlines():
        if ln.startswith("JSON_RESULT:"):
            line = ln[len("JSON_RESULT:"):].strip()
            break
    if line is None:
        return None, None
    try:
        payload = json.loads(line)
    except Exception:
        return None, None
    # ── Perceptual Weighting Overrule (2026-05-30, Drum Plate listening) ──
    # The count-minimizer farms easy statistical gates while sacrificing the
    # ones the ear actually flags (kick-masking low decay, low-mid mod mud,
    # dull HF tail). Multiply those specific gates' margin contribution so a
    # config that closes/approaches them is strongly preferred. NB: this
    # biases the tiebreaker — at equal n_fail the audible-correct config wins,
    # and closing a weighted gate (−1 fail AND a big margin term removed) is
    # the strongest loss drop available.
    PERCEPTUAL_WEIGHTS = (
        ("edt low 100-250",      5.0),   # transient recovery → kick clarity
        ("ripple lowmid 250-1k", 4.0),   # low-mid mod mud → drive Mod Depth down
        ("T60 16",               3.0),   # HF air extension → brighter tail
        # Low-end BODY (2026-05-31): the clarity weights above let the optimizer
        # "clean up" by high-passing the whole low end (Lo Cut→144) → audibly
        # weak sub. Weight the sub-energy gates so body is protected alongside
        # clarity (paired with the Lo Cut≤60 clamp).
        ("sub-bass <100",        4.0),   # broadband sub energy
        ("ss deep sub 20-50",    3.0),   # steady-state deep sub
        ("ss sub 50-100",        3.0),   # steady-state sub
    )

    def _weight (s):
        for key, w in PERCEPTUAL_WEIGHTS:
            if key in s:
                return w
        return 1.0

    n_fail = int(payload.get("n_fail", 0))
    margin = 0.0
    for s in payload.get("fails", []):
        contrib = 0.5   # fallback for unparseable fails
        m = re.search(r"Δ=\s*([-+]?[0-9.]+)\s*%.*gate=±\s*([0-9.]+)\s*%", s)
        if m:
            d = abs(float(m.group(1))); tol = float(m.group(2))
            contrib = max(0.0, d / tol - 1.0) if tol > 0 else 0.0
        elif (m := re.search(r"Δ=\s*([-+]?[0-9.]+).*gate=±\s*([0-9.]+)", s)):
            d = abs(float(m.group(1))); tol = float(m.group(2))
            contrib = max(0.0, d / tol - 1.0) if tol > 0 else 0.0
        elif (m := re.search(r"Δ=\s*([-+]?[0-9.]+).*gate([≤≥])\s*([-+]?[0-9.]+)", s)):
            d = float(m.group(1)); op = m.group(2); bound = float(m.group(3))
            contrib = max(0.0, (d - bound) if op == "≤" else (bound - d))
        margin += contrib * _weight (s)

        # Sub-bass OVERSHOOT guard (2026-05-31): the input makeup loves to bloom
        # the sub hot (+5.3 dB). Punish sub-energy gates that climb >+0.5 dB
        # above the anchor, squared × 2.0, so the optimizer pins sub ~flat
        # rather than farming the deep-sub gates by over-boosting.
        if any (k in s for k in ("sub-bass <100", "ss deep sub", "ss sub 50-100")):
            mo = re.search(r"Δ=\s*([-+]?[0-9.]+).*gate=±\s*([0-9.]+)", s)
            if mo:
                dv_delta = float(mo.group(1)); tol = float(mo.group(2))
                if dv_delta > 0.5 and tol > 0:
                    margin += 2.0 * (max(0.0, (dv_delta - 0.5) / tol)) ** 2
    return n_fail, margin


def make_objective(
    target_ir: Path,
    preset_name: str,
    vst3_path: Path,
    trial_root: Path,
    fail_loss: float = 1.0e9,   # must exceed any real loss (n_fail*1000+margin,
                                # ~26k–60k) so an errored/timed-out trial is
                                # ranked WORST, not best. Was 1000 (a latent bug
                                # under the direct-scoreboard objective: a failed
                                # render scored below every real config and won).
    stimulus: str = "noiseburst",
    prerun_seconds: float = 5.0,
    has_dpv: bool = False,
):
    """
    Returns an Optuna objective closure that renders one trial via
    the harness and returns the multi-metric loss vs the target IR.

    `stimulus` and `prerun_seconds` control what kind of rendered audio
    the optimizer measures — see render_trial() docstring.

    `has_dpv` gates the DPV_PARAMS axes into the search space (only useful
    for algo=1 DattorroPlateVintage presets; left False everywhere else).
    """
    target_ir = str(target_ir)
    anchor_dir = str(Path(target_ir).parent)   # full_check compares dir↔dir

    def objective(trial: optuna.Trial) -> float:
        # Sample free params.
        overrides = dict(LOCKED_OVERRIDES)
        for name, (lo, hi) in FREE_PARAMS.items():
            if name in DPV_PARAMS and not has_dpv:
                continue   # inert axis on non-DPV engines — don't waste a dim
            overrides[name] = trial.suggest_float(name, lo, hi)

        # Per-trial output dir keeps parallel workers from colliding.
        out_dir = trial_root / f"trial_{trial.number:05d}"
        impulse = render_trial(
            preset_name=preset_name,
            overrides=overrides,
            vst3_path=vst3_path,
            out_dir=out_dir,
            stimulus=stimulus,
            prerun_seconds=prerun_seconds,
        )
        if impulse is None:
            return fail_loss

        # ── DIRECT SCOREBOARD OPTIMIZATION (Option A, 2026-05-30) ──────────
        # The weighted-sum proxy was blind to ~half the gate families
        # (ripple/boom/body/env/stereo/per-stim RMS), so the optimizer dumped
        # fails there \u2014 3 proxy sweeps all regressed vs the 34-fail
        # baseline. Minimize the validation scoreboard DIRECTLY: run
        # full_check.py (the exact gate harness, incl. its ceiling exemptions)
        # as a blocking subprocess on the trial render and return
        #     Loss = n_fail*1000 + sum_failing max(0, |delta|/tol - 1)
        # The x1000 makes any broken gate outrank all margins; the margin sum
        # is a smooth gradient pulling failing gates toward their boundary.
        # Objective and scoreboard can no longer diverge.
        try:
            fc = subprocess.run(
                [sys.executable,
                 str(Path(__file__).resolve().parent / "full_check.py"),
                 str(out_dir), str(anchor_dir),
                 "--name", preset_name, "--json"],
                capture_output=True, text=True, timeout=180,
            )
        except Exception as exc:
            sys.stderr.write(f"full_check subprocess failed (trial {trial.number}): {exc}\n")
            shutil.rmtree(out_dir, ignore_errors=True)
            return fail_loss
        n_fail, margin_sum = _parse_full_check_json(fc.stdout)
        shutil.rmtree(out_dir, ignore_errors=True)
        if n_fail is None:
            sys.stderr.write(
                f"full_check parse fail (trial {trial.number}): "
                f"{fc.stdout[-200:]!r} {fc.stderr[-200:]!r}\n")
            return fail_loss
        loss = n_fail * 1000.0 + margin_sum
        trial.set_user_attr('n_fail', int(n_fail))
        trial.set_user_attr('margin_sum', float(margin_sum))
        trial.set_user_attr('params', json.dumps(overrides))
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
    ap.add_argument("--stimulus", default="noiseburst",
                    choices=["impulse", "noiseburst", "snare"],
                    help="Which rendered stimulus to compare (default noiseburst).")
    ap.add_argument("--prerun-seconds", type=float, default=5.0,
                    help="Warm-up silence before each stimulus (default 5.0).")
    ap.add_argument("--has-dpv", action="store_true",
                    help="Sample the 7 DPV (DattorroPlateVintage) corrective-EQ "
                         "params. Only meaningful for algo=1 presets; on every "
                         "other engine they are no-ops, so leave OFF to shrink "
                         "the search-space dimensionality.")
    ap.add_argument("--enqueue-json", default=None,
                    help="Warm-start: a JSON file path (or inline JSON string) "
                         "holding a param dict, or a list of param dicts, to "
                         "enqueue as the first trial(s). Use the shipped "
                         "FactoryPresets values so the study starts at the "
                         "baseline and can only improve.")
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

    # Warm-start: seed the study with known-good configs (e.g. the current
    # shipped FactoryPresets row) so trial 0 already scores the baseline and
    # study.best can only improve from there. Cold TPE over 15 dims needs many
    # trials just to RECOVER a baseline that came from a long prior sweep;
    # enqueueing it bounds the result at ≤ baseline and gives TPE a good basin.
    if args.enqueue_json:
        ej = Path(args.enqueue_json)
        seeds = json.loads(ej.read_text() if ej.is_file() else args.enqueue_json)
        if isinstance(seeds, dict):
            seeds = [seeds]
        for seed in seeds:
            study.enqueue_trial({k: v for k, v in seed.items()
                                 if k in FREE_PARAMS}, skip_if_exists=True)
        print(f"Warm-start: enqueued {len(seeds)} seed config(s).")

    objective = make_objective(
        target_ir=target_ir,
        preset_name=args.dv_preset,
        vst3_path=vst3,
        trial_root=trial_root,
        stimulus=args.stimulus,
        prerun_seconds=args.prerun_seconds,
        has_dpv=args.has_dpv,
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
