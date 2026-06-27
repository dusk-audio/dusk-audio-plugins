#!/usr/bin/env python3
"""
Round-4 BH mini-sweep, parallel 8-worker.
Lock from round-3: m=1.06, t=0.85, hc=7000, gt=0.5, lc=30.
Adds PostBandTrim 4-band post-tank EQ axes.
"""
import json
import shutil
import subprocess, sys
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed

REPO   = Path(__file__).resolve().parents[4]
RENDER = REPO / "build/tests/duskverb_render/duskverb_render"
VST3   = Path.home() / ".vst3/DuskVerb.vst3"
ANCHOR = Path("/tmp/anchor_bh")


def trial(idx, sub, lm, mh, air):
    out_dir = Path(f"/tmp/bh_m4_{idx:04d}")
    # Wipe the whole dir so no stale state of any kind leaks into this trial.
    shutil.rmtree(out_dir, ignore_errors=True)
    out_dir.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(RENDER), "--vst3", str(VST3),
        "--program", "Bright Hall",
        "--param", "Dry/Wet=1.0",
        "--param", "Bus Mode=1",
        # Round-3 best locked:
        "--param", "Mid Multiply=1.06",
        "--param", "Damping=0.85",
        "--param", "Hi Cut=7000",
        "--param", "Gain Trim=0.5",
        "--param", "Lo Cut=30",
        # Round-4 sweep axes (PostBandTrim):
        "--param", f"Post Band Sub Gain={sub}",
        "--param", f"Post Band Low-Mid Gain={lm}",
        "--param", f"Post Band Mid-High Gain={mh}",
        "--param", f"Post Band Air Gain={air}",
        "--prerun-seconds", "5",
        "--sustained-pink-seconds", "4",
        "--output-dir", str(out_dir),
    ]
    out_wav = out_dir / "BrightHall_noiseburst.wav"
    try:
        cp = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
    except subprocess.TimeoutExpired:
        return (999, idx, sub, lm, mh, air, out_dir)
    # Treat a non-zero render exit as failure, and only then trust the file.
    if cp.returncode != 0 or not out_wav.exists():
        return (999, idx, sub, lm, mh, air, out_dir)
    try:
        r = subprocess.run(
            [sys.executable, str(REPO / "plugins/DuskVerb/tools/tuner/full_check.py"),
             str(out_dir), str(ANCHOR), "--name", "BH", "--category", "Halls", "--json"],
            capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        return (999, idx, sub, lm, mh, air, out_dir)
    # Parse the structured JSON_RESULT line (robust vs the human-readable sheet).
    fails = 999
    for line in r.stdout.splitlines():
        if line.startswith("JSON_RESULT:"):
            try:
                fails = json.loads(line[len("JSON_RESULT:"):])["n_fail"]
            except Exception:
                pass
            break
    return (fails, idx, sub, lm, mh, air, out_dir)


def main():
    sub_grid = [-4.0, -2.0, 0.0, +2.0]
    lm_grid  = [-3.0, -1.5, 0.0]
    mh_grid  = [-3.0, -1.5, 0.0, +1.5]
    air_grid = [-3.0, 0.0, +3.0]
    jobs = [(i, s, l, mh, a)
            for i, (s, l, mh, a) in enumerate(
                [(s, l, mh, a) for s in sub_grid for l in lm_grid
                 for mh in mh_grid for a in air_grid], start=1)]
    print(f"Round-4 total trials: {len(jobs)}, workers: 8\n")

    results = []
    with ThreadPoolExecutor(max_workers=8) as ex:
        futs = [ex.submit(trial, *job) for job in jobs]
        for i, fut in enumerate(as_completed(futs), start=1):
            res = fut.result()
            results.append(res)
            if i % 20 == 0 or res[0] < 19:
                fails, idx, s, l, mh, a, _ = res
                print(f"[{i:3d}/{len(jobs)}] idx={idx:3d} sub={s} lm={l} mh={mh} air={a}  fails={fails}")

    results.sort(key=lambda r: r[0])
    print("\n=== TOP 10 ===")
    for fails, idx, s, l, mh, a, _ in results[:10]:
        print(f"  fails={fails:3d}  sub={s} lm={l} mh={mh} air={a}")
    if results:
        bf, bidx, bs, bl, bmh, ba, btmp = results[0]
        print(f"\nBEST: fails={bf}  sub={bs} lm={bl} mh={bmh} air={ba}")
        print(f"Render: {btmp}")


if __name__ == "__main__":
    main()
