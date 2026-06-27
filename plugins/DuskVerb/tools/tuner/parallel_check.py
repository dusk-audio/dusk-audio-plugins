#!/usr/bin/env python3
"""Parallel gain-matched full_check across presets (6 workers).

Each preset renders into its OWN --output-dir (no shared-glob collisions),
gain-matches to its anchor noiseburst RMS, runs full_check, and reports
n_fail. Renders + checks run concurrently in a 6-worker pool (12-core box;
leaves headroom for the OS — memory: duskverb_tuner_workers OOM ceiling).

Usage: parallel_check.py [preset ...]      (default: all anchored presets)
       parallel_check.py --fails NAME      (print the failing-gate list too)
"""
import os, sys, glob, json, shutil, subprocess
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import soundfile as sf, numpy as np

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))
REND = os.path.join(REPO, "build/tests/duskverb_render/duskverb_render")
VVV  = os.path.join(REPO, "tests/duskverb_render/output/vvv")
ANCH = os.path.expanduser("~/projects/dusk-audio-tools/tuner_runs/anchors")
FC   = os.path.join(os.path.dirname(__file__), "full_check.py")
# --sustained-pink-seconds: the tail-mod-ripple + mod-freq gates measure on
# the SUSTAINED render (full_check falls back to noiseburst otherwise, which
# its own authors flag as an unreliable noise-vs-noise comparison). Render it
# so presets whose anchors have a sustained capture get the intended gate.
WET  = ["--param", "Dry/Wet=1.0", "--param", "Bus Mode=1", "--param", "Freeze=0",
        "--sustained-pink-seconds", "4.0"]
STIM = ["impulse", "noiseburst", "snare", "sine1k", "sustained"]

# preset -> (anchor_dir, anchor_prefix)
PRESETS = {
    "Vocal Plate":          (VVV,                       "vvv_vocal_plate"),
    "Drum Plate":           (VVV,                       "vvv_Drum_Plate"),
    "Vocal Hall":           (VVV,                       "vvv_Vocal_Hall"),
    "Cathedral Large Hall": (f"{ANCH}/vvv-cathedral",   "vvv-cathedral"),
    "Blade Runner 224":     (f"{ANCH}/vvv-blade-runner","vvv-blade-runner"),
    "Tiled Room":           (f"{ANCH}/vvv-tiled-room",  "vvv-tiled-room"),
    "79 Vocal Chamber":     (f"{ANCH}/vvv-79vc",        "vvv-79vc"),
    "Ambience":             (f"{ANCH}/vvv-ambience",    "vvv-ambience"),
    "Bright Hall":          (f"{ANCH}/vvv-bright-hall", "vvv-bright-hall"),
    "Medium Drum Room":     (f"{ANCH}/vvv-fat-snare-room", "vvv-fat-snare-room"),
}


def rms(p):
    x, _ = sf.read(p)
    m = x.mean(axis=1) if x.ndim > 1 else x
    return float(np.sqrt(np.mean(m * m)))


def check_one(name):
    # Contain ALL worker exceptions: one bad render/read must not kill the
    # whole sweep (ThreadPoolExecutor.map re-raises at iteration otherwise).
    try:
        return _check_one(name)
    except Exception as e:  # noqa: BLE001 — report-and-continue tool
        return name, None, f"exception: {e}"


def _check_one(name):
    adir, apref = PRESETS[name]
    slug = name.lower().replace(" ", "_")
    dv, lex = f"/tmp/pcheck_{slug}", f"/tmp/pcheck_{slug}_lex"
    shutil.rmtree(dv, ignore_errors=True); shutil.rmtree(lex, ignore_errors=True)
    os.makedirs(dv); os.makedirs(lex)
    r = subprocess.run([REND, "--program", name, "--output-dir", dv, *WET],
                       cwd=REPO, capture_output=True, text=True, timeout=420)
    if r.returncode != 0:
        return name, None, f"render rc={r.returncode}: {r.stderr[-300:]}"
    nb = glob.glob(f"{dv}/*_noiseburst.wav")
    if not nb:
        return name, None, "no noiseburst rendered"
    for s in STIM:
        src = f"{adir}/{apref}_{s}.wav"
        if os.path.exists(src):
            shutil.copy(src, f"{lex}/anchor_{s}.wav")
    if not os.path.exists(f"{lex}/anchor_noiseburst.wav"):
        return (name, None,
                f"missing anchor noiseburst capture for {name} "
                f"(no {apref}_noiseburst.wav in {adir})")
    a = rms(f"{lex}/anchor_noiseburst.wav")
    d = rms(nb[0])
    if d < 1e-12:
        return name, None, "silent render"
    g = a / d
    for f in glob.glob(f"{dv}/*.wav"):
        # subtype='FLOAT': the default WAV subtype is PCM_16, which silently
        # REQUANTIZES the float render — the added -96 dBFS dither floor
        # dominates quiet late windows (cent_500 read 3253 Hz vs the true
        # 1374 on MDR) and penalized every preset's late-window gates.
        x, sr = sf.read(f); sf.write(f, x * g, sr, subtype="FLOAT")
    r = subprocess.run([sys.executable, FC, dv, lex, "--name", name, "--json"],
                       capture_output=True, text=True, timeout=200)
    for line in r.stdout.splitlines():
        if line.startswith("JSON_RESULT:"):
            res = json.loads(line.split("JSON_RESULT: ")[1])
            return name, res, None
    return name, None, f"no JSON_RESULT (rc={r.returncode})"


def main():
    args = [a for a in sys.argv[1:] if a != "--fails"]
    show_fails = "--fails" in sys.argv
    # Dedupe (order-preserving): each preset maps to a fixed /tmp/pcheck_<slug>
    # dir, so a duplicated name would have two workers racing the same workspace
    # (one rmtree's the other's render mid-run).
    names = list(dict.fromkeys(args)) if args else list(PRESETS)
    bad = [n for n in names if n not in PRESETS]
    if bad:
        sys.exit(f"unknown preset(s): {bad}; known: {list(PRESETS)}")
    with ThreadPoolExecutor(max_workers=6) as ex:
        for name, res, err in ex.map(check_one, names):
            if err:
                print(f"{name:22s} ERROR  {err}")
            else:
                print(f"{name:22s} n_fail={res['n_fail']}")
                if show_fails:
                    for f in res["fails"]:
                        print("   ", f)


if __name__ == "__main__":
    main()
