#!/usr/bin/env python3
"""Factory-preset audit for the Multi-Synth Phase 5 re-voice sweep.

Renders every factory preset through preset_render (mode-appropriate performance)
and reports, per preset:
    peak dBFS, RMS dBFS, silence flag, spectral centroid (Hz), reference-segment
    f0 (pitch sanity), inter-sample clipping count (|x| >= 1.0), DC offset (dBFS
    below RMS), and a PASS/FAIL against the Phase 5 rules:
        * audibly non-silent (RMS > -60 dBFS)
        * no clipping (peak <= -1.0 dBFS)
        * finite (no NaN/Inf)

Usage:
    preset_audit.py                 # audit all presets
    preset_audit.py 15 23 41        # audit specific indices
    preset_audit.py --json          # machine-readable
"""
import json
import os
import subprocess
import sys

import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
BIN = os.path.join(HERE, "..", "build", "preset_render")
OUT = "/tmp/msynth_presets"
os.makedirs(OUT, exist_ok=True)

PEAK_CLIP_DBFS = -1.0     # peak must be at or below this
SILENCE_DBFS = -60.0      # RMS above this = audible


def preset_count():
    """Number of factory presets = highest index preset_render accepts + 1."""
    # Cheap probe: binary reports out-of-range on a huge index; parse the range.
    r = subprocess.run([BIN, "99999", "/dev/null"], capture_output=True, text=True)
    # message: "preset index 99999 out of range [0,NN)"
    msg = r.stderr.strip()
    try:
        return int(msg.split("[0,")[1].split(")")[0])
    except Exception:
        return 40


def render(index, **kw):
    path = os.path.join(OUT, f"preset_{index:02d}.wav")
    args = [BIN, str(index), path]
    for k, v in kw.items():
        args.append(f"{k}={v}")
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"render failed for {index}: {r.stderr}")
    info = r.stderr.strip()
    x, sr = sf.read(path, always_2d=True)
    return sr, x, info, path


def db(x):
    return 20.0 * np.log10(max(x, 1e-12))


def spectral_centroid(mono, sr):
    # Use the loudest 1-second window for a stable spectrum.
    win = min(len(mono), int(sr))
    if win < 256:
        seg = mono
    else:
        # sliding RMS to find the loudest window start (coarse, hop = win/4)
        hop = max(1, win // 4)
        best_i, best_e = 0, -1.0
        for i in range(0, len(mono) - win + 1, hop):
            e = float(np.mean(mono[i:i + win] ** 2))
            if e > best_e:
                best_e, best_i = e, i
        seg = mono[best_i:best_i + win]
    w = np.hanning(len(seg))
    X = np.abs(np.fft.rfft(seg * w))
    f = np.fft.rfftfreq(len(seg), 1.0 / sr)
    denom = np.sum(X) + 1e-12
    return float(np.sum(f * X) / denom)


def ref_f0(mono, sr):
    """f0 of a stable sustained reference segment (loudest 0.3 s), parabolic FFT."""
    win = min(len(mono), int(0.3 * sr))
    if win < 256:
        return 0.0
    hop = max(1, win // 4)
    best_i, best_e = 0, -1.0
    for i in range(0, len(mono) - win + 1, hop):
        e = float(np.mean(mono[i:i + win] ** 2))
        if e > best_e:
            best_e, best_i = e, i
    seg = mono[best_i:best_i + win]
    w = np.hanning(len(seg))
    X = np.abs(np.fft.rfft(seg * w))
    f = np.fft.rfftfreq(len(seg), 1.0 / sr)
    lo = np.searchsorted(f, 20.0)
    X[:lo] = 0.0
    k = int(np.argmax(X))
    if 1 <= k < len(X) - 1:
        a, b, c = np.log(X[k - 1] + 1e-20), np.log(X[k] + 1e-20), np.log(X[k + 1] + 1e-20)
        delta = 0.5 * (a - c) / (a - 2 * b + c + 1e-20)
    else:
        delta = 0.0
    return float((k + delta) * sr / len(seg))


def audit_one(index):
    sr, x, info, path = render(index)
    name = info.split('"')[1] if '"' in info else f"#{index}"
    mono = x.mean(axis=1)
    finite = bool(np.all(np.isfinite(x)))
    peak = float(np.max(np.abs(x))) if finite else float("inf")
    rms = float(np.sqrt(np.mean(mono ** 2))) if finite else 0.0
    peak_db = db(peak)
    rms_db = db(rms)
    clip_count = int(np.sum(np.abs(x) >= 1.0))
    dc = float(np.mean(mono))
    dc_db = db(abs(dc) / (rms + 1e-12))
    centroid = spectral_centroid(mono, sr) if finite else 0.0
    f0 = ref_f0(mono, sr) if finite else 0.0
    silent = rms_db <= SILENCE_DBFS

    ok = finite and (not silent) and (peak_db <= PEAK_CLIP_DBFS) and clip_count == 0
    return {
        "index": index, "name": name, "finite": finite,
        "peak_db": peak_db, "rms_db": rms_db, "silent": silent,
        "clip": clip_count, "dc_db": dc_db, "centroid": centroid,
        "f0": f0, "pass": ok, "path": path, "info": info,
    }


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    as_json = "--json" in sys.argv
    n = preset_count()
    indices = [int(a) for a in args] if args else list(range(n))

    rows = [audit_one(i) for i in indices]

    if as_json:
        print(json.dumps(rows, indent=2))
        return

    print(f"{'#':>3} {'name':<20}{'peak':>8}{'rms':>8}{'cent':>8}{'f0':>9}"
          f"{'clip':>5}{'dc':>7}  result")
    fails = 0
    for r in rows:
        flag = "PASS" if r["pass"] else "FAIL"
        why = ""
        if not r["pass"]:
            fails += 1
            reasons = []
            if not r["finite"]:
                reasons.append("NONFINITE")
            if r["silent"]:
                reasons.append("SILENT")
            if r["peak_db"] > PEAK_CLIP_DBFS:
                reasons.append("HOT")
            if r["clip"]:
                reasons.append("CLIP")
            why = " " + ",".join(reasons)
        print(f"{r['index']:>3} {r['name']:<20}{r['peak_db']:>7.1f} {r['rms_db']:>7.1f}"
              f"{r['centroid']:>8.0f}{r['f0']:>9.1f}{r['clip']:>5}{r['dc_db']:>7.1f}"
              f"  {flag}{why}")
    print(f"\npreset_audit: {len(rows) - fails}/{len(rows)} pass "
          f"(rule: non-silent, peak<={PEAK_CLIP_DBFS:.0f} dBFS, no clip, finite)")
    sys.exit(0 if fails == 0 else 1)


if __name__ == "__main__":
    main()
