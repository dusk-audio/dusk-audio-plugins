#!/usr/bin/env python3
"""
Cold-start convenience wrapper around `preset_vs_external_optuna.py`.

Read this first — it is NOT the primary tuning path anymore:

  Per the current tuning protocol (see HANDOFF_2026-07-06_opus48.md §5),
  the primary method is MANUAL per-gate tuning: read the failing-gate list
  from `full_check.py --json`, map each gate to its lever, edit the baked
  table (FactoryPresets.h / kPmbByName / octave-T60 maps / kPteqByName) and
  rebuild. Optuna is for COLD-START ONLY — re-sweeping an already-tuned
  preset just floors at its baseline and wastes hours.

What this wrapper does when a cold start IS warranted:

  1. Launches `preset_vs_external_optuna.py` on the pre-rendered anchor.
     Objective = `full_check.py --json` n_fail (+ margin), i.e. the exact
     validation scoreboard, not a proxy loss.
  2. Optionally warm-starts the study from a param dict (--enqueue-json,
     usually `duskverb_render --dump-params` of the current preset) so
     trial 0 reproduces the shipping baseline and the study can only improve.
  3. Parses the winning param dict, re-renders it, and runs full_check for a
     final report.
  4. Prints the winning params for you to bake by hand. It does NOT edit any
     source file.

Lock-in is manual and goes into baked tables, NOT render.cpp positional
values: FactoryPresets.h (positional preset row), kPmbByName / octave-T60 /
kPteqByName maps in PluginProcessor.cpp/FactoryPresets.h. Runtime `--param`
edits of those baked tables are desync-BLOCKED by design — a swept value
only takes effect once baked into the map + rebuilt.

Resource limits (a 1500-trial / 4-worker cold sweep OOM'd the 32 GB box):
  workers <= 6, trials <= 300. This wrapper clamps to those ceilings.

Usage:
    python3 tune_preset.py "Vintage Vocal Plate" \\
        --anchor-rendered /tmp/anchor_v2/LexAnchor_noiseburst.wav \\
        --enqueue-json /tmp/vvp_baseline.json \\
        --trials 300 --workers 6

    # Prove parse_best still tracks the optuna output format:
    python3 tune_preset.py --self-test
"""
from __future__ import annotations
import argparse, json, re, subprocess, sys, tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]
RENDER_BIN = REPO / "build" / "tests" / "duskverb_render" / "duskverb_render"
DEFAULT_VST3 = Path.home() / ".vst3" / "DuskVerb.vst3"
TUNER_DIR = Path(__file__).resolve().parent

# Resource ceilings — a 1500-trial / 4-worker cold sweep OOM'd the 32 GB box.
MAX_TRIALS = 300
MAX_WORKERS = 6


def run_sweep(preset, anchor, vst3, trials, workers, prerun, log_path,
              enqueue_json=None):
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
    # Warm-start so trial 0 reproduces the shipping baseline (cold TPE over
    # ~20 dims otherwise floors at baseline; see HANDOFF §5).
    if enqueue_json:
        cmd += ["--enqueue-json", str(enqueue_json)]
    print(f"\n>>> launching sweep: {' '.join(cmd)}")
    with open(log_path, "w") as f:
        rc = subprocess.call(cmd, stdout=f, stderr=subprocess.STDOUT)
    if rc != 0:
        sys.exit(f"sweep failed (exit {rc}); see {log_path}")
    print(f">>> sweep complete; log: {log_path}")


def parse_best(log_path):
    """Extract 'Best trial JSON: <path>' from the sweep log and load it.

    `preset_vs_external_optuna.py` writes best.json as
        {preset, target_ir, best_loss, best_params: {...}, best_metrics: {...}}
    where best_params is the APVTS-name-keyed override dict (the value of the
    winning trial's 'params' user-attr). Return that inner dict — passing the
    outer wrapper to render_with() would forward 'preset=' / 'best_loss=' as
    bogus --param overrides the harness ignores, so the swept parameters would
    never reach the plugin.
    """
    txt = Path(log_path).read_text()
    m = re.search(r"Best trial JSON:\s*(\S+)", txt)
    if not m:
        sys.exit(f"could not find 'Best trial JSON:' in {log_path}")
    j = Path(m.group(1))
    if not j.exists():
        sys.exit(f"best.json not found: {j}")
    raw = json.loads(j.read_text())
    if isinstance(raw, dict) and 'best_params' in raw:
        params = raw['best_params']
    else:
        params = raw   # tolerate a bare param dict (older/hand-written files)
    if not isinstance(params, dict) or not params:
        sys.exit(f"best.json holds no usable params: {j}")
    return params


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
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
    except subprocess.TimeoutExpired:
        sys.exit("render timed out (>180s) — aborting tune")
    if r.returncode != 0:
        sys.exit(f"render failed: {r.stderr[-400:]}")


def slug(name):
    return "".join(c for c in name if c.isalnum() or c in "+-_'")


def run_full_check(dv_dir, anchor_dir, preset):
    cmd = [sys.executable, str(TUNER_DIR / "full_check.py"),
           str(dv_dir), str(anchor_dir), "--name", preset]
    r = subprocess.call(cmd)
    return r == 0


def emit_lockin_block(preset, params):
    """Print the winning params for the user to BAKE BY HAND.

    Lock-in target is a baked table, never render.cpp positional values:
      - FactoryPresets.h — the preset's positional parameter row.
      - PluginProcessor.cpp / FactoryPresets.h keyed maps — kPmbByName,
        octave-T60 (BEGIN/END_OCTAVE_T60_MAP), kPteqByName, kFiveBandByName.
    Runtime --param edits of the keyed maps are desync-BLOCKED by design, so a
    swept value only takes effect once baked into the map and rebuilt. After
    baking, run `fleet_audit.py --verify-tables` (a migration can strand the
    old engine's name-keyed rows) and re-audit the preset.
    """
    print("\n" + "=" * 64)
    print("  WINNING PARAMS — bake by hand into the baked table (see docstring)")
    print("=" * 64)
    print("\n# Best params (JSON):")
    print(json.dumps(params, indent=2))
    print("\n# Sorted for inspection:")
    for k in sorted(params.keys()):
        print(f"  {k:28s} = {params[k]}")


def self_test():
    """Prove parse_best still tracks the optuna best.json format.

    Fabricates a best.json wrapper + a sweep log exactly as
    preset_vs_external_optuna.py emits them, then asserts parse_best returns
    the UNWRAPPED APVTS param dict. Run: `tune_preset.py --self-test`.
    """
    ok = True
    with tempfile.TemporaryDirectory(prefix="tune_selftest_") as d:
        d = Path(d)
        # (1) current format: wrapper with best_params
        bj = d / "best.json"
        bj.write_text(json.dumps({
            "preset": "Vocal Hall",
            "target_ir": "/tmp/anchor/vh_noiseburst.wav",
            "best_loss": 13012.5,
            "best_params": {"Decay Time": 2.0, "Width": 0.97, "Bass Multiply": 1.1},
            "best_metrics": {"n_fail": 13, "margin_sum": 12.5},
        }))
        log = d / "sweep.log"
        log.write_text(
            "Loading target IR: /tmp/anchor/vh_noiseburst.wav\n"
            "======================================================================\n"
            "BEST TRIAL #42   loss = 13012.500000\n"
            "Best params:\n  Decay Time         = 2.0000\n"
            f"Best trial JSON: {bj}\n"
            f"Scratch dir kept at: {d}\n")
        got = parse_best(log)
        exp = {"Decay Time": 2.0, "Width": 0.97, "Bass Multiply": 1.1}
        if got == exp:
            print(f"PASS wrapper unwrap -> {got}")
        else:
            print(f"FAIL wrapper unwrap: got {got}, expected {exp}"); ok = False

        # (2) tolerate a bare param dict (no wrapper)
        bj.write_text(json.dumps({"Decay Time": 5.0}))
        got = parse_best(log)
        if got == {"Decay Time": 5.0}:
            print(f"PASS bare-dict fallback -> {got}")
        else:
            print(f"FAIL bare-dict fallback: got {got}"); ok = False
    print("\nself-test: " + ("OK" if ok else "FAILED"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(
        description="Cold-start Optuna wrapper (NOT the primary tuning path — "
                    "see the module docstring / HANDOFF §5).")
    ap.add_argument("preset", nargs="?", help="DuskVerb preset name (--program).")
    ap.add_argument("--self-test", action="store_true",
                    help="Prove parse_best tracks the optuna best.json format, then exit.")
    ap.add_argument("--anchor-rendered",
                    help="Path to pre-rendered anchor noiseburst WAV.")
    ap.add_argument("--anchor-dir",
                    help="Directory holding anchor renders (snare + noiseburst + impulse). "
                         "Default: parent of --anchor-rendered.")
    ap.add_argument("--enqueue-json",
                    help="Warm-start JSON (param dict or list of dicts), e.g. from "
                         "`duskverb_render --dump-params`. Strongly recommended — cold "
                         "TPE floors at baseline without it.")
    ap.add_argument("--vst3", default=str(DEFAULT_VST3))
    ap.add_argument("--trials", type=int, default=MAX_TRIALS,
                    help=f"Optuna trial budget (clamped to <= {MAX_TRIALS}).")
    ap.add_argument("--workers", type=int, default=MAX_WORKERS,
                    help=f"Parallel workers (clamped to <= {MAX_WORKERS}).")
    ap.add_argument("--prerun", type=float, default=5.0)
    ap.add_argument("--work-dir", default="/tmp/tune_preset")
    ap.add_argument("--skip-sweep", action="store_true",
                    help="Reuse the last sweep's best.json (re-run render + full_check only).")
    args = ap.parse_args()

    if args.self_test:
        sys.exit(self_test())
    if not args.preset:
        ap.error("preset is required (or pass --self-test)")
    if not args.anchor_rendered:
        ap.error("--anchor-rendered is required")

    if args.trials > MAX_TRIALS:
        print(f"!! clamping --trials {args.trials} -> {MAX_TRIALS} (OOM ceiling)")
        args.trials = MAX_TRIALS
    if args.workers > MAX_WORKERS:
        print(f"!! clamping --workers {args.workers} -> {MAX_WORKERS} (OOM ceiling)")
        args.workers = MAX_WORKERS
    if not args.enqueue_json:
        print("!! no --enqueue-json warm-start: cold TPE usually floors at "
              "baseline. Pass `duskverb_render --dump-params` output unless this "
              "is a genuine cold start.")

    anchor_rendered = Path(args.anchor_rendered)
    anchor_dir = Path(args.anchor_dir) if args.anchor_dir else anchor_rendered.parent
    work_dir = Path(args.work_dir) / slug(args.preset)
    work_dir.mkdir(parents=True, exist_ok=True)
    log_path = work_dir / "sweep.log"
    dv_dir = work_dir / "dv"

    if not args.skip_sweep:
        run_sweep(args.preset, anchor_rendered, args.vst3,
                  args.trials, args.workers, args.prerun, log_path,
                  enqueue_json=args.enqueue_json)
    if not log_path.exists():
        sys.exit("no sweep log found; remove --skip-sweep or run a sweep first")

    best = parse_best(log_path)
    print("\n>>> best params from sweep:")
    for k, v in sorted(best.items()):
        print(f"   {k:24s} = {v}")

    # Gain is a free Optuna axis (Gain Trim), so loudness is optimized jointly;
    # no post-sweep trim pass (it re-introduced band-energy mismatch). Render
    # the winner once and validate.
    render_with(args.preset, best, args.vst3, dv_dir, prerun=args.prerun)

    print("\n>>> running full_check...")
    passed = run_full_check(dv_dir, anchor_dir, args.preset)

    emit_lockin_block(args.preset, best)

    if passed:
        print("\nOK all gates pass. Bake the winning params by hand (see above), "
              "run fleet_audit.py --verify-tables, then re-audit + listen.")
        sys.exit(0)
    print("\nX gates still failing. Do NOT bake yet. Options:")
    print("  1. Switch to manual per-gate tuning (HANDOFF §5) — map each failing "
          "gate to its lever; Optuna floors once a preset is roughly tuned.")
    print("  2. If a listening issue has no gate at all, ADD the gate + loss term "
          "first (full_check.py + preset_vs_external_optuna.py), then re-sweep.")
    sys.exit(1)


if __name__ == "__main__":
    main()
