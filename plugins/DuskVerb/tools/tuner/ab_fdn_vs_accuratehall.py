#!/usr/bin/env python3
"""Gain-matched full_check A/B: FDN (native algo) vs AccurateHall (algo 10) for
each FDN preset that has a VVV anchor. Renders both, level-matches each to the
anchor noiseburst RMS, runs full_check, reports n_fail FDN vs AH + the gates
each closes/opens. Run from repo root."""
import os, sys, glob, shutil, subprocess, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import soundfile as sf, numpy as np

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))
REND = os.path.join(REPO, "build/tests/duskverb_render/duskverb_render")
OUTD = os.path.join(REPO, "tests/duskverb_render/output")
VVV  = os.path.join(REPO, "tests/duskverb_render/output/vvv")
ANCH = os.path.expanduser("~/projects/dusk-audio-tools/tuner_runs/anchors")
FC   = os.path.join(os.path.dirname(__file__), "full_check.py")
STIM = ["impulse", "noiseburst", "snare", "sine1k"]
WET  = ["--param","Dry/Wet=1.0","--param","Bus Mode=1","--param","Freeze=0"]

# preset -> (anchor_dir, anchor_prefix)
PRESETS = {
    "Vocal Plate":          (VVV,                      "vvv_vocal_plate"),
    "Drum Plate":           (VVV,                      "vvv_Drum_Plate"),
    "Vocal Hall":           (VVV,                      "vvv_Vocal_Hall"),
    "Cathedral Large Hall": (f"{ANCH}/vvv-cathedral",  "vvv-cathedral"),
    "Blade Runner 224":     (f"{ANCH}/vvv-blade-runner","vvv-blade-runner"),
    "Tiled Room":           (f"{ANCH}/vvv-tiled-room", "vvv-tiled-room"),
    "79 Vocal Chamber":     (f"{ANCH}/vvv-79vc",       "vvv-79vc"),
    "Ambience":             (f"{ANCH}/vvv-ambience",   "vvv-ambience"),
    "Bright Hall":          (f"{ANCH}/vvv-bright-hall","vvv-bright-hall"),
}


def full_rms(p):
    # Mono-mix BEFORE RMS (matches the whole tuner suite + full_check's mono-domain
    # gates). Raw whole-array RMS diverges ~3 dB for decorrelated stereo, mis-leveling
    # the gain-match that full_check then reads.
    x, _ = sf.read(p)
    m = x.mean(axis=1) if x.ndim > 1 else x
    return float(np.sqrt(np.mean(m**2)))


def render(name, algo10, dst):
    for f in glob.glob(f"{OUTD}/*.wav"):
        try:
            os.remove(f)
        except OSError:
            pass
    args = [REND, "--program", name, *WET]
    if algo10:
        args += ["--param", "Algorithm=10"]
    r = subprocess.run(args, cwd=REPO, capture_output=True, text=True, timeout=420)
    if r.returncode != 0:
        raise RuntimeError(f"render failed for {name!r} (rc={r.returncode})\n{r.stderr[-2000:]}")
    # Recreate dst empty so a partial/short render can't leave stale WAVs behind.
    shutil.rmtree(dst, ignore_errors=True)
    os.makedirs(dst, exist_ok=True)
    out = glob.glob(f"{OUTD}/*.wav")
    if not out:
        raise RuntimeError(f"render produced no WAVs for {name!r}")
    for f in out: shutil.copy(f, dst)


def gain_match(dvdir, anchor_nb):
    a = full_rms(anchor_nb)
    cur = glob.glob(f"{dvdir}/*_noiseburst.wav")[0]
    r = full_rms(cur)
    if r < 1e-12:
        raise RuntimeError(f"silent noiseburst render in {dvdir} — cannot gain-match")
    g = a / r
    for f in glob.glob(f"{dvdir}/*.wav"):
        x, sr = sf.read(f); sf.write(f, x * g, sr, subtype="FLOAT")


def lexdir(adir, prefix, dst):
    shutil.rmtree(dst, ignore_errors=True); os.makedirs(dst, exist_ok=True)
    for s in STIM:
        src = f"{adir}/{prefix}_{s}.wav"
        # Fail fast: a missing stimulus would silently shrink the comparison set,
        # so full_check would A/B on an incomplete anchor and report misleading deltas.
        if not os.path.exists(src):
            raise RuntimeError(f"missing anchor stimulus {src!r} — refusing to run full_check on a partial set")
        shutil.copy(src, f"{dst}/anchor_{s}.wav")


def run_fc(dvdir, lex, name):
    r = subprocess.run([sys.executable, FC, dvdir, lex, "--name", name, "--json"],
                       capture_output=True, text=True, timeout=200)
    for line in r.stdout.splitlines():
        if line.startswith("JSON_RESULT:"):
            return json.loads(line.split("JSON_RESULT: ")[1])
    # No JSON line = full_check errored. Bubble up rather than returning a
    # synthetic n_fail=-1 that silently corrupts the A/B slate.
    raise RuntimeError(
        f"full_check produced no JSON_RESULT (rc={r.returncode})\n"
        f"  FC={FC} dv={dvdir} lex={lex}\n  stdout(tail)={r.stdout[-1500:]}\n  stderr(tail)={r.stderr[-1500:]}")


def gset(d): return {f.split("  ")[0].strip() for f in d["fails"]}


def main():
    only = set(sys.argv[1:])
    print(f"{'preset':22s} {'base':>4} {'AH':>4}  delta   notes")
    for name, (adir, prefix) in PRESETS.items():
        if only and name not in only:
            continue
        anb = f"{adir}/{prefix}_noiseburst.wav"
        fdir, adir2 = f"/tmp/ab_{prefix}_fdn", f"/tmp/ab_{prefix}_ah"
        ldir = f"/tmp/ab_{prefix}_lex"
        render(name, False, fdir); gain_match(fdir, anb)
        render(name, True,  adir2); gain_match(adir2, anb)
        lexdir(adir, prefix, ldir)
        f = run_fc(fdir, ldir, name); a = run_fc(adir2, ldir, name)
        fs, as_ = gset(f), gset(a)
        closed = sorted(fs - as_); opened = sorted(as_ - fs)
        d = a["n_fail"] - f["n_fail"]
        print(f"{name:22s} {f['n_fail']:4d} {a['n_fail']:4d}  {d:+4d}")
        if closed: print(f"    closed ({len(closed)}): " + ", ".join(closed))
        if opened: print(f"    opened ({len(opened)}): " + ", ".join(opened))


if __name__ == "__main__":
    main()
