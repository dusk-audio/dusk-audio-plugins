#!/usr/bin/env python3
"""
One-command preset tuner — runs the full automated pipeline:

  1. Render reference (Lex / VVV / whatever) at 100% wet, 5s prerun
     (skipped if --anchor-rendered points at a pre-existing WAV).
  2. Optuna 1500-trial sweep with all current loss terms:
       - peak-aligned RMS-normalized 1/3-oct L1
       - peak-aligned tail decay shape (t30/t40/t60)
       - peak-aligned centroid windows
       - absolute band-energy match (sub / low / mid / umid / hi)
       - envelope oscillation match
       - stereo correlation match
  3. Apply best params via render harness, level-match to snare RMS,
     re-render.
  4. Run full_check.py against the rendered output. Exits non-zero on
     any gate failure with a per-gate report.

This script is the ONLY way to tune a preset going forward. NEVER
hand-edit FactoryPresets.h or render.cpp in response to a listening
test. If the listening test reveals an issue full_check did not catch,
ADD that gate + a corresponding loss term to the Optuna script and
re-run THIS tool. Manual edits don't scale to N presets.

Usage:
    python3 tune_preset.py "Vintage Vocal Plate" \\
        --anchor-rendered /tmp/anchor_v2/LexAnchor_noiseburst.wav \\
        --vst3 ~/.vst3/DuskVerb.vst3 \\
        --trials 1500 --workers 4

When the sweep finishes, the best params are written to a JSON file +
the script tells you exactly what to paste into FactoryPresets.h /
render.cpp (so the lock-in step is just a copy-paste, no eyeballing).
"""
from __future__ import annotations
import argparse, json, re, subprocess, sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]
RENDER_BIN = REPO / "build" / "tests" / "duskverb_render" / "duskverb_render"
DEFAULT_VST3 = Path.home() / ".vst3" / "DuskVerb.vst3"
TUNER_DIR = Path(__file__).resolve().parent


def run_sweep(preset, anchor, vst3, trials, workers, prerun, log_path):
    """Launch the Optuna sweep in the foreground (caller can ^C)."""
    log_path.parent.mkdir(parents=True, exist_ok=True)
    cmd = [sys.executable, str(TUNER_DIR / "preset_vs_external_optuna.py"),
           "--target-ir", str(anchor),
           "--dv-preset", preset,
           "--stimulus", "noiseburst",
           "--prerun-seconds", str(prerun),
           "--trials", str(trials),
           "--workers", str(workers),
           "--vst3", str(vst3)]
    print(f"\n>>> launching sweep: {' '.join(cmd)}")
    with open(log_path, "w") as f:
        rc = subprocess.call(cmd, stdout=f, stderr=subprocess.STDOUT)
    if rc != 0:
        sys.exit(f"sweep failed (exit {rc}); see {log_path}")
    print(f">>> sweep complete; log: {log_path}")


def parse_best(log_path):
    """Extract 'Best trial JSON: <path>' and load. The Optuna script writes
    best.json with an outer wrapper {preset, target_ir, best_loss, best_params:
    {...}, best_metrics: {...}}. Return the UNWRAPPED params dict (just the
    APVTS parameter map) — passing the wrapper to render_with would silently
    forward 'preset=...' and 'best_loss=...' as --param overrides that the
    harness ignores, so the actual swept parameters never reach the plugin."""
    txt = Path(log_path).read_text()
    m = re.search(r"Best trial JSON:\s*(\S+)", txt)
    if not m:
        sys.exit(f"could not find 'Best trial JSON:' in {log_path}")
    j = Path(m.group(1))
    if not j.exists():
        sys.exit(f"best.json not found: {j}")
    raw = json.loads(j.read_text())
    if isinstance(raw, dict) and 'best_params' in raw:
        return raw['best_params']
    return raw


def render_with(preset, params, vst3, out_dir, prerun=5.0, extra=None):
    out_dir.mkdir(parents=True, exist_ok=True)
    cmd = [str(RENDER_BIN), "--vst3", str(vst3),
           "--output-dir", str(out_dir),
           "--prerun-seconds", str(prerun),
           # Mirror Optuna's render: include sustained-pink so full_check
           # can verify steady-state per-band energy + per-band decay match.
           # Without this, the lock-in render skips the sustained section
           # and falsely "passes" without the user-perceptual checks.
           "--sustained-pink-seconds", "4.0",
           "--param", "Dry/Wet=1.0",
           "--param", "Bus Mode=1",
           "--param", "Freeze=0"]
    for k, v in params.items():
        if k in {"Dry/Wet", "Bus Mode", "Freeze"}:
            continue
        cmd += ["--param", f"{k}={v}"]
    if extra:
        for k, v in extra.items():
            cmd += ["--param", f"{k}={v}"]
    # Canonical program path (setCurrentProgram/applyFactoryPreset), NOT the
    # positional arg — the positional routes through the harness's legacy
    # duplicate preset table and drifts from what the plugin ships.
    cmd += ["--program", preset]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
    if r.returncode != 0:
        sys.exit(f"render failed: {r.stderr[-400:]}")


def integrated_rms(p):
    import soundfile as sf, numpy as np
    x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim>1 else x
    pk = int(np.argmax(np.abs(m)))
    return float(20*np.log10(np.sqrt(np.mean(m[pk:]**2))+1e-30))


def slug(name):
    return "".join(c for c in name if c.isalnum() or c in "+-_'")


def auto_level_match(preset, params, vst3, anchor_dir, dv_dir):
    """Deprecated 2026-05-27: Gain Trim is now a free Optuna parameter so
    loudness is optimized jointly with everything else. Post-sweep trim
    adjustment re-introduced absolute-band-energy mismatch (bands hot by
    same dB as the trim bump). This function now just returns the params
    unchanged — the single final render is performed by render_lockin()
    immediately after, so rendering here would be a redundant duplicate.
    """
    return params


def render_lockin(preset, params, vst3, dv_dir):
    """Final render at locked params for full_check."""
    render_with(preset, params, vst3, dv_dir)


def run_full_check(dv_dir, anchor_dir, preset):
    cmd = [sys.executable, str(TUNER_DIR / "full_check.py"),
           str(dv_dir), str(anchor_dir), "--name", preset]
    r = subprocess.call(cmd)
    return r == 0


def emit_lockin_block(preset, params):
    """Print copy-pasteable FactoryPresets.h + render.cpp blocks for the user."""
    print("\n" + "═" * 64)
    print("  LOCK-IN SNIPPETS (paste into FactoryPresets.h + render.cpp)")
    print("═" * 64)
    print("\n# Best params (JSON):")
    print(json.dumps(params, indent=2))
    print("\n# Paste FactoryPresets.h positional values:")
    # Print all params for inspection
    for k in sorted(params.keys()):
        print(f"  {k:28s} = {params[k]}")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("preset", help="DuskVerb preset name in render.cpp.")
    ap.add_argument("--anchor-rendered", required=True,
                    help="Path to pre-rendered anchor noiseburst WAV.")
    ap.add_argument("--anchor-dir",
                    help="Directory holding anchor renders (snare + noiseburst + impulse). "
                         "Default: parent of --anchor-rendered.")
    ap.add_argument("--vst3", default=str(DEFAULT_VST3))
    ap.add_argument("--trials", type=int, default=1500)
    ap.add_argument("--workers", type=int, default=4)
    ap.add_argument("--prerun", type=float, default=5.0)
    ap.add_argument("--work-dir", default="/tmp/tune_preset")
    ap.add_argument("--skip-sweep", action="store_true",
                    help="Reuse the last sweep's best.json (for re-running level match + full_check).")
    args = ap.parse_args()

    anchor_rendered = Path(args.anchor_rendered)
    anchor_dir = Path(args.anchor_dir) if args.anchor_dir else anchor_rendered.parent
    work_dir = Path(args.work_dir) / slug(args.preset)
    work_dir.mkdir(parents=True, exist_ok=True)
    log_path = work_dir / "sweep.log"
    dv_dir = work_dir / "dv"

    if not args.skip_sweep:
        run_sweep(args.preset, anchor_rendered, args.vst3,
                  args.trials, args.workers, args.prerun, log_path)
    if not log_path.exists():
        sys.exit("no sweep log found; remove --skip-sweep or run a sweep first")

    best = parse_best(log_path)
    print("\n>>> best params from sweep:")
    for k, v in sorted(best.items()):
        print(f"   {k:24s} = {v}")

    # Auto level-match by adjusting Gain Trim against snare RMS
    best = auto_level_match(args.preset, best, args.vst3, anchor_dir, dv_dir)

    # Final lock-in render at matched gain
    render_lockin(args.preset, best, args.vst3, dv_dir)

    # Run all gates
    print("\n>>> running full_check...")
    passed = run_full_check(dv_dir, anchor_dir, args.preset)

    # Output lock-in snippets regardless (so user can paste even if gates fail)
    emit_lockin_block(args.preset, best)

    if passed:
        print("\n✓ ALL GATES PASS — paste lock-in values + commit.")
        sys.exit(0)
    else:
        print("\n✗ GATES FAILED — DO NOT lock in. Options:")
        print("  1. Tighten loss terms in preset_vs_external_optuna.py for the failed gates and re-run.")
        print("  2. If a category has no loss term, ADD ONE first.")
        print("  3. Never patch values manually in response to a failed gate.")
        sys.exit(1)


if __name__ == "__main__":
    main()
