#!/usr/bin/env python3
"""
Round-2 BH mini-sweep on more axes. Locked from round-1: trim=+1.5.
Expanding: bass_mult, low_xover, hi_cut. Re-grid mid + treble around best.
"""
import os, subprocess, sys
from pathlib import Path

REPO   = Path(__file__).resolve().parents[4]
RENDER = REPO / "build/tests/duskverb_render/duskverb_render"
VST3   = Path.home() / ".vst3/DuskVerb.vst3"
ANCHOR = Path("/tmp/anchor_bh")


def run_check(out_dir: Path) -> int:
    try:
        r = subprocess.run(
            [sys.executable, str(REPO / "plugins/DuskVerb/tools/tuner/full_check.py"),
             str(out_dir), str(ANCHOR), "--name", "BH", "--category", "Halls"],
            capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        return 999
    for line in r.stdout.splitlines():
        if "GATE(S) FAILED" in line:
            try:
                return int(line.split("✗")[1].strip().split()[0])
            except Exception:
                pass
    return 999


def render(out_dir: Path, mid, treble, bass, lo_xover, hi_cut, trim=1.5) -> bool:
    out_dir.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(RENDER), "--vst3", str(VST3),
        "--program", "Bright Hall",
        "--param", "Dry/Wet=1.0",
        "--param", "Bus Mode=On",
        "--param", f"Mid Multiply={mid}",
        "--param", f"Damping={treble}",
        "--param", f"Bass Multiply={bass}",
        "--param", f"Low Crossover={lo_xover}",
        "--param", f"Hi Cut={hi_cut}",
        "--param", f"Gain Trim={trim}",
        "--prerun-seconds", "5",
        "--sustained-pink-seconds", "4",
        "--output-dir", str(out_dir),
    ]
    try:
        subprocess.run(cmd, capture_output=True, text=True, timeout=120)
        return (out_dir / "BrightHall_noiseburst.wav").exists()
    except subprocess.TimeoutExpired:
        return False


def main():
    mid_grid    = [1.10, 1.12, 1.14]
    treble_grid = [0.82, 0.88, 0.95]
    bass_grid   = [0.85, 0.92, 1.00]
    loxv_grid   = [180, 250, 400]
    hicut_grid  = [4500, 6000, 8000]
    total = (len(mid_grid)*len(treble_grid)*len(bass_grid)
             *len(loxv_grid)*len(hicut_grid))
    print(f"Grid total: {total} trials\n")

    results = []
    n = 0
    for m in mid_grid:
        for t in treble_grid:
            for b in bass_grid:
                for lx in loxv_grid:
                    for hc in hicut_grid:
                        n += 1
                        tmp = Path(f"/tmp/bh_m2_{n:03d}")
                        if not render(tmp, m, t, b, lx, hc):
                            print(f"[{n:3d}/{total}] m={m} t={t} b={b} lx={lx} hc={hc}  RENDER FAIL")
                            continue
                        fails = run_check(tmp)
                        results.append((fails, m, t, b, lx, hc, tmp))
                        if n % 10 == 0 or fails < 19:
                            print(f"[{n:3d}/{total}] m={m} t={t} b={b} lx={lx} hc={hc}  fails={fails}")

    results.sort()
    print("\n=== TOP 10 ===")
    for fails, m, t, b, lx, hc, tmp in results[:10]:
        print(f"  fails={fails}  m={m} t={t} b={b} lx={lx} hc={hc}  ({tmp})")
    if results:
        bf, bm, bt, bb, blx, bhc, btmp = results[0]
        print(f"\nBEST: fails={bf}  m={bm} t={bt} b={bb} lx={blx} hc={bhc}")


if __name__ == "__main__":
    main()
