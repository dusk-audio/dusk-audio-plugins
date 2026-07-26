#!/usr/bin/env python3
"""
Localized mini-sweep on VintageTank new-axes only.

Search:
  Mid Multiply    ∈ [1.04, 1.06, 1.08, 1.10, 1.12]
  Treble Mult     ∈ [0.85, 0.88, 0.91, 0.94, 0.97]      (APVTS "damping")
  Gain Trim (dB)  ∈ [+1.5, +2.0, +2.5]                  (Output Gain proxy)

Reads anchor BH, runs harness per combo, counts full_check gate fails.
Picks min-fail combo, prints params + delta sheet.
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO  = Path(__file__).resolve().parents[4]
RENDER = REPO / "build/tests/duskverb_render/duskverb_render"
VST3   = Path.home() / ".vst3/DuskVerb.vst3"
ANCHOR = Path("/tmp/anchor_bh")

sys.path.insert(0, str(Path(__file__).resolve().parent))
from full_check import (
    _t60_band_schroeder, _full_rms_db, _tail_mod_peak_freq,
)
import scipy.io.wavfile as wf
import scipy.signal as sg
import numpy as np


def run_check(out_dir: Path) -> int:
    """Run full_check.py against anchor BH, return total fail count."""
    try:
        r = subprocess.run(
            [sys.executable, str(REPO / "plugins/DuskVerb/tools/tuner/full_check.py"),
             str(out_dir), str(ANCHOR), "--name", "BH-mini", "--category", "Halls"],
            capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        print(f"full_check timeout (>60s) for {out_dir}", file=sys.stderr)
        return 999
    for line in r.stdout.splitlines():
        if "GATE(S) FAILED" in line:
            try:
                return int(line.split("✗")[1].strip().split()[0])
            except Exception as e:
                print(f"full_check: failed to parse fail count from "
                      f"{line!r}: {e}", file=sys.stderr)
                return 999
    # Sentinel 999 sorts last (worst); a real fail count never approaches it.
    print(f"full_check: no 'GATE(S) FAILED' line in output for {out_dir}",
          file=sys.stderr)
    return 999


def render(out_dir: Path, mid_mult: float, treble_mult: float, gain_trim: float) -> bool:
    # Full clean dir so stale artifacts in a reused trial dir can't contaminate scoring.
    shutil.rmtree(out_dir, ignore_errors=True)
    out_dir.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(RENDER), "--vst3", str(VST3),
        "--program", "Bright Hall",
        "--param", "Dry/Wet=1.0",
        "--param", "Bus Mode=On",
        "--param", f"Mid Multiply={mid_mult}",
        "--param", f"Damping={treble_mult}",
        "--param", f"Gain Trim={gain_trim}",
        "--prerun-seconds", "5",
        "--sustained-pink-seconds", "4",
        "--output-dir", str(out_dir),
    ]
    wav = out_dir / "BrightHall_noiseburst.wav"
    try:
        res = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        print(f"render timeout (>120s) for {out_dir}", file=sys.stderr)
        return False
    if res.returncode != 0:
        print(f"render failed (rc={res.returncode}) for {out_dir}: "
              f"{res.stderr.strip()}", file=sys.stderr)
        return False
    return wav.exists()


def main():
    mid_grid    = [1.04, 1.06, 1.08, 1.10, 1.12]
    treble_grid = [0.85, 0.88, 0.91, 0.94, 0.97]
    trim_grid   = [1.5, 2.0, 2.5]
    print(f"Grid: {len(mid_grid)} mid × {len(treble_grid)} treble × {len(trim_grid)} trim "
          f"= {len(mid_grid)*len(treble_grid)*len(trim_grid)} trials\n")

    results = []
    n = 0
    total = len(mid_grid) * len(treble_grid) * len(trim_grid)
    for m in mid_grid:
        for t in treble_grid:
            for g in trim_grid:
                n += 1
                tmp = Path(f"/tmp/bh_mini_{n:03d}")
                if not render(tmp, m, t, g):
                    print(f"[{n:3d}/{total}] mid={m:.2f} treble={t:.2f} trim={g:+.1f}  RENDER FAIL")
                    continue
                fails = run_check(tmp)
                results.append((fails, m, t, g, tmp))
                print(f"[{n:3d}/{total}] mid={m:.2f} treble={t:.2f} trim={g:+.1f}  fails={fails}")

    results.sort()
    print("\n=== TOP 5 ===")
    for fails, m, t, g, tmp in results[:5]:
        print(f"  fails={fails}  mid={m:.2f} treble={t:.2f} trim={g:+.1f}  ({tmp})")

    if results:
        best_fails, best_m, best_t, best_g, best_tmp = results[0]
        print(f"\nBEST: fails={best_fails}  mid={best_m:.2f}  treble={best_t:.2f}  trim={best_g:+.1f}")
        print(f"Render dir: {best_tmp}")


if __name__ == "__main__":
    main()
