#!/usr/bin/env python3
"""
Polish-only sweep — Stage 3 (Spectral + EQ) ONLY.

Use after a preset's Stage 1 + Stage 2 params are already locked and you
want CMA-ES to re-tune the post-tank polish axes (Lo Cut, Hi Cut, Saturation,
Gain Trim, Hi Cut Shelf) against the anchor without disturbing the upstream
spatial / decay structure.

Usage:
    python3 polish_only.py "Tight Drum Room" \
        --anchor-rendered /tmp/anchor_tdr/TightDrumRoom_noiseburst.wav \
        --category Rooms \
        --locked-params /tmp/tdr_locked.json

Where locked-params JSON is a flat dict of Stage 1+2 param name -> value.
"""
from __future__ import annotations
import argparse, json, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from staged_tuner import (
    ALL_PARAMS, HARNESS_OVERRIDES, CATEGORY_RULES,
    render, run_stage, stage3_loss, RENDER_BIN, DEFAULT_VST3,
)
import shutil, subprocess


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("preset")
    ap.add_argument("--anchor-rendered", required=True)
    ap.add_argument("--vst3", default=str(DEFAULT_VST3))
    ap.add_argument("--workers", type=int, default=6)
    ap.add_argument("--trials", type=int, default=500)
    ap.add_argument("--category", default="")
    ap.add_argument("--locked-params", required=True,
                    help="JSON dict of Stage 1+2 param -> value to lock.")
    ap.add_argument("--work-dir", default="/tmp/staged_tuner")
    args = ap.parse_args()

    anchor_n = Path(args.anchor_rendered)
    anchor_s = anchor_n.with_name(anchor_n.name.replace("_noiseburst", "_sustained"))
    anchor_snare = anchor_n.with_name(anchor_n.name.replace("_noiseburst", "_snare"))
    anchor_files = {"noiseburst": str(anchor_n), "sustained": str(anchor_s),
                    "snare": str(anchor_snare)}

    with open(args.locked_params) as f:
        locked = json.load(f)

    slug = "".join(c for c in args.preset if c.isalnum() or c in "+-_'")
    work = Path(args.work_dir) / (slug + "_polish")
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)

    profile = CATEGORY_RULES.get(args.category, CATEGORY_RULES[""])
    print(f"\n══════════ POLISH-ONLY DIAGNOSTIC ══════════")
    print(f"  Preset:     {args.preset}")
    print(f"  Category:   {args.category or '(default)'}")
    print(f"  Locked Stage 1+2 params: {len(locked)} values")
    print(f"  Trials:     {args.trials}   Workers: {args.workers}")
    print(f"  has_dpv:    {profile['has_dpv']}")

    s3_active = ["Lo Cut", "Hi Cut", "Saturation", "Gain Trim", "Hi Cut Shelf"]
    s3_range_overrides = dict(profile.get("stage3_ranges", {}))
    if profile["has_dpv"]:
        s3_active += ["DPV HF Shelf Gain", "DPV HF Shelf Freq",
                      "DPV Box Cut Gain",  "DPV Box Cut Freq",
                      "DPV Bass Shelf Gain", "DPV Bass Shelf Freq"]
        s3_range_overrides.update({
            "DPV HF Shelf Gain":   (-6.0, 6.0),
            "DPV Box Cut Gain":    (-6.0, 6.0),
            "DPV Bass Shelf Gain": (-6.0, 6.0),
        })

    s3_locked = {k: v for k, v in locked.items() if k not in s3_active}

    print(f"  Stage 3 active params: {sorted(s3_active)}")
    print(f"  Stage 3 range overrides: {s3_range_overrides}")
    print()

    s3_best, _ = run_stage("Stage 3 — Polish ONLY (locked s1+s2)",
                            s3_active, s3_locked, stage3_loss,
                            args.preset, anchor_files, args.vst3,
                            args.trials, args.workers, work / "s3",
                            range_overrides=s3_range_overrides)

    final = {**locked, **s3_best}
    final_json = work / "final.json"
    final_json.write_text(json.dumps(final, indent=2))
    print(f"\n══════════ POLISH FINAL ══════════")
    print(f"  Combined locked + Stage 3 best → {final_json}")
    for k in sorted(final.keys()):
        print(f"    {k:24s} = {final[k]:.4f}")

    final_dir = work / "final"
    files = render(args.preset, final, args.vst3, final_dir)
    if not files:
        sys.exit("final render failed")
    print(f"\nFinal render: {final_dir}")
    cmd = [sys.executable, str(Path(__file__).resolve().parent / "full_check.py"),
           str(final_dir), str(anchor_n.parent), "--name", args.preset]
    if args.category:
        cmd += ["--category", args.category]
    # NB: full_check exits 1 whenever ANY gate fails (the normal state for these
    # presets), so we intentionally DO NOT use check=True here — we want the
    # report printed regardless; a non-zero exit means "gates failed", not "crash".
    subprocess.call(cmd)


if __name__ == "__main__":
    main()
