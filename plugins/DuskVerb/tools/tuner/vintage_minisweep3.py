#!/usr/bin/env python3
"""
Round-3 BH mini-sweep, parallel 8-worker.

Aggressive on 1k bloom + mid hot:
  Mid Multiply  ∈ [0.98, 1.02, 1.06, 1.10]
  Treble (Damping) ∈ [0.65, 0.75, 0.85]            ← deeper choke
  Hi Cut         ∈ [2500, 3500, 5000, 7000]        ← lower → more HF damping
  Gain Trim      ∈ [+0.5, +1.0, +1.5, +2.0]
  Lo Cut         ∈ [30, 60, 100]                   ← cut sub buildup
"""
import subprocess, sys
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed

REPO   = Path(__file__).resolve().parents[4]
RENDER = REPO / "build/tests/duskverb_render/duskverb_render"
VST3   = Path.home() / ".vst3/DuskVerb.vst3"
ANCHOR = Path("/tmp/anchor_bh")


def trial(idx, m, t, hc, gt, lc):
    out_dir = Path(f"/tmp/bh_m3_{idx:04d}")
    out_dir.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(RENDER), "--vst3", str(VST3),
        "--program", "Bright Hall",
        "--param", "Dry/Wet=1.0",
        "--param", "Bus Mode=On",
        "--param", f"Mid Multiply={m}",
        "--param", f"Damping={t}",
        "--param", f"Hi Cut={hc}",
        "--param", f"Gain Trim={gt}",
        "--param", f"Lo Cut={lc}",
        "--prerun-seconds", "5",
        "--sustained-pink-seconds", "4",
        "--output-dir", str(out_dir),
    ]
    try:
        res = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
        if res.returncode != 0:
            print(f"render failed (rc={res.returncode}) idx={idx}: "
                  f"{res.stderr.strip()}", file=sys.stderr)
            return (999, idx, m, t, hc, gt, lc, out_dir)
    except (subprocess.TimeoutExpired, FileNotFoundError, OSError) as e:
        print(f"render error idx={idx}: {e}", file=sys.stderr)
        return (999, idx, m, t, hc, gt, lc, out_dir)
    if not (out_dir / "BrightHall_noiseburst.wav").exists():
        return (999, idx, m, t, hc, gt, lc, out_dir)
    try:
        r = subprocess.run(
            [sys.executable, str(REPO / "plugins/DuskVerb/tools/tuner/full_check.py"),
             str(out_dir), str(ANCHOR), "--name", "BH", "--category", "Halls"],
            capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        return (999, idx, m, t, hc, gt, lc, out_dir)
    fails = 999
    for line in r.stdout.splitlines():
        if "GATE(S) FAILED" in line:
            try:
                fails = int(line.split("✗")[1].strip().split()[0])
            except Exception:
                pass
            break
    return (fails, idx, m, t, hc, gt, lc, out_dir)


def main():
    mids    = [0.98, 1.02, 1.06, 1.10]
    trebs   = [0.65, 0.75, 0.85]
    hicuts  = [2500, 3500, 5000, 7000]
    trims   = [0.5, 1.0, 1.5, 2.0]
    locuts  = [30, 60, 100]
    jobs = [(i, m, t, hc, gt, lc)
            for i, (m, t, hc, gt, lc) in enumerate(
                [(m, t, hc, gt, lc)
                 for m in mids for t in trebs for hc in hicuts
                 for gt in trims for lc in locuts], start=1)]
    print(f"Total trials: {len(jobs)}, workers: 8\n")

    results = []
    with ThreadPoolExecutor(max_workers=8) as ex:
        futs = [ex.submit(trial, *job) for job in jobs]
        for i, fut in enumerate(as_completed(futs), start=1):
            res = fut.result()
            results.append(res)
            if i % 20 == 0 or res[0] < 19:
                fails, idx, m, t, hc, gt, lc, _ = res
                print(f"[{i:3d}/{len(jobs)}] idx={idx:3d} m={m} t={t} hc={hc} "
                      f"gt={gt} lc={lc}  fails={fails}")

    results.sort(key=lambda r: r[0])
    print("\n=== TOP 10 ===")
    for fails, idx, m, t, hc, gt, lc, _ in results[:10]:
        print(f"  fails={fails:3d}  m={m} t={t} hc={hc} gt={gt} lc={lc}")
    if results:
        bf, bidx, bm, bt, bhc, bgt, blc, btmp = results[0]
        print(f"\nBEST: fails={bf}  m={bm} t={bt} hc={bhc} gt={bgt} lc={blc}")
        print(f"Render: {btmp}")


if __name__ == "__main__":
    main()
