#!/usr/bin/env python3
"""
Post-sweep helper. Reads Optuna best.json, applies params to the live
plugin via the harness for both noiseburst and snare stimuli (peak-aligned
metrics + raw 1/3-oct delta), and prints the residual table.

Does NOT edit FactoryPresets.h or render.cpp — that's the user's call after
inspecting the residual.

Usage:
    python3 apply_best_and_evaluate.py /tmp/optuna_vvp_v8/run.log
    python3 apply_best_and_evaluate.py --best /tmp/dv_optuna_xxx/best.json \\
                                       --anchor /tmp/anchor_v2/LexAnchor_noiseburst.wav \\
                                       --preset "Vintage Vocal Plate"
"""
from __future__ import annotations
import argparse, json, re, shutil, subprocess, sys, tempfile
from pathlib import Path
import numpy as np, soundfile as sf

REPO = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from metrics_external import compute_metrics


def parse_best_from_runlog(log_path):
    """Find the 'Best trial JSON: ...' line in Optuna stdout."""
    txt = Path(log_path).read_text()
    m = re.search(r"Best trial JSON:\s*(\S+)", txt)
    return Path(m.group(1)) if m else None


def parse_best_json(p):
    raw = json.loads(Path(p).read_text())
    if not isinstance(raw, dict):
        return {}
    # preset_vs_external_optuna.py wraps the params: {preset, target_ir,
    # best_params, best_metrics, ...}. Return ONLY the tuned param map, else
    # the Optuna metadata keys would be passed as bogus --param overrides.
    if "best_params" in raw:
        return raw["best_params"]
    return raw   # staged_tuner.py already writes a flat param dict


def render(preset, params, vst3, out_dir, prerun=5.0):
    out_dir.mkdir(parents=True, exist_ok=True)
    cmd = [str(REPO / "build/tests/duskverb_render/duskverb_render"),
           "--vst3", str(vst3),
           "--output-dir", str(out_dir),
           "--prerun-seconds", str(prerun)]
    for k, v in params.items():
        cmd += ["--param", f"{k}={v}"]
    cmd += ["--program", preset]   # canonical path, not the legacy positional table
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
    if r.returncode != 0:
        sys.exit(f"render failed: {r.stderr[-400:]}")


def integrated_rms_db(p):
    x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim > 1 else x
    pk = int(np.argmax(np.abs(m)))
    return float(20 * np.log10(np.sqrt(np.mean(m[pk:]**2) + 1e-30) + 1e-30))


def peak_aligned_thirdoct(p, t0=0.05, t1=1.0):
    x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim > 1 else x
    pk = int(np.argmax(np.abs(m)))
    a, b = pk + int(t0 * sr), min(pk + int(t1 * sr), len(m))
    seg = m[a:b] * np.hanning(b - a)
    S = np.abs(np.fft.rfft(seg)) ** 2
    f = np.fft.rfftfreq(len(seg), 1.0 / sr)
    centers = 1000.0 * 2.0 ** (np.arange(-15, 11) / 3.0)
    centers = centers[(centers > 25) & (centers < sr * 0.4)]
    out = []
    for fc in centers:
        lo, hi = fc / (2 ** (1/6)), fc * (2 ** (1/6))
        msk = (f >= lo) & (f < hi)
        e = float(S[msk].sum())
        out.append((float(fc), 10 * np.log10(e + 1e-30)))
    return out


def report(preset, params, vst3, anchor):
    # Unique per-invocation dir so overlapping runs can't delete each other's
    # render mid-flight (was a shared /tmp/eval_best). mkdtemp = its own scratch.
    dv_dir = Path(tempfile.mkdtemp(prefix="eval_best_"))
    render(preset, params, vst3, dv_dir)
    # Derive the rendered files from the dir (the harness slug follows the
    # preset name), not a hardcoded "VintageVocalPlate_*" — that mislabelled
    # every other preset's evaluation.
    dv_p = next(iter(sorted(dv_dir.glob("*_noiseburst.wav"))), None)
    dv_s = next(iter(sorted(dv_dir.glob("*_snare.wav"))), None)
    if dv_p is None or dv_s is None:
        sys.exit(f"report: no rendered wavs in {dv_dir}")
    lx_p = Path(anchor)
    lx_s = lx_p.with_name(lx_p.name.replace("_noiseburst", "_snare"))

    print("══════════════════════════════════════════════════════════════════")
    print(f"  POST-SWEEP EVAL — {preset}")
    print(f"  DV render: {dv_p}")
    print(f"  Anchor:    {lx_p}")
    print("══════════════════════════════════════════════════════════════════")

    # Loudness
    print("\n── Loudness (post-peak integrated RMS) ──")
    print(f"  {'stim':10s}  {'DV':>8s}  {'Lex':>8s}  {'Δ':>7s}")
    for label, dvf, lxf in [("noiseburst", dv_p, lx_p), ("snare", dv_s, lx_s if lx_s.exists() else None)]:
        if lxf is None:
            continue
        dv = integrated_rms_db(str(dvf))
        lx = integrated_rms_db(str(lxf))
        flag = " ✓" if abs(dv - lx) < 1.0 else (" ⚠" if abs(dv - lx) < 3 else " ✗")
        print(f"  {label:10s}  {dv:+8.2f}  {lx:+8.2f}  {dv-lx:+7.2f}{flag}")

    # Peak-aligned metrics
    print("\n── Peak-aligned metrics (noiseburst) ──")
    dv_m = compute_metrics(str(dv_p))
    lx_m = compute_metrics(str(lx_p))
    rows = [
        ("tail_t30 (s)", dv_m.get("tail_t30"), lx_m.get("tail_t30"), "pct"),
        ("tail_t60 (s)", dv_m.get("tail_t60"), lx_m.get("tail_t60"), "pct"),
        ("cent_50 (Hz)", dv_m.get("cent_50"), lx_m.get("cent_50"), "pct"),
        ("cent_500 (Hz)", dv_m.get("cent_500"), lx_m.get("cent_500"), "pct"),
        ("stereo_corr", dv_m.get("stereo_corr"), lx_m.get("stereo_corr"), "abs"),
        ("env_p2p (dB)", dv_m.get("env_res_p2p"), lx_m.get("env_res_p2p"), "abs"),
    ]
    print(f"  {'metric':20s}  {'DV':>10s}  {'Lex':>10s}  {'Δ':>8s}")
    for lab, dv, lx, kind in rows:
        if dv is None or lx is None or (isinstance(dv, float) and dv != dv):
            print(f"  {lab:20s}  {'nan':>10s}  {'nan':>10s}  {'---':>8s}")
            continue
        if kind == "pct" and lx:
            d = f"{(dv-lx)/lx*100:+5.1f}%"
        else:
            d = f"{dv-lx:+6.3f}"
        print(f"  {lab:20s}  {dv:10.3f}  {lx:10.3f}  {d:>8s}")

    # 1/3-oct delta (peak-aligned, raw absolute)
    print("\n── 1/3-oct band Δ (peak-aligned, RAW absolute dB) ──")
    print("  Positive = ADD dB to DV to match Lex")
    dv_b = peak_aligned_thirdoct(str(dv_p))
    lx_b = peak_aligned_thirdoct(str(lx_p))
    print(f"  {'fc(Hz)':>8s}  {'DV(dB)':>8s}  {'Lex(dB)':>8s}  {'Δ-needed':>9s}  bar")
    rows = []
    for (fc, dv), (_, lx) in zip(dv_b, lx_b):
        d = lx - dv
        n_bars = min(int(abs(d)), 15)
        bar = ("+" if d > 0 else "-") * n_bars if abs(d) > 0.5 else ""
        flag = " ⚠" if abs(d) > 5 else ""
        print(f"  {fc:8.1f}  {dv:+8.2f}  {lx:+8.2f}  {d:+9.2f}  {bar}{flag}")
        rows.append((fc, d))
    abs_d = [abs(d) for _, d in rows]
    if not abs_d:
        print("\n  (no overlapping 1/3-oct bands — skipping mean/max |Δ|)")
    else:
        max_abs = max(abs_d)
        print(f"\n  mean |Δ|: {sum(abs_d)/len(abs_d):.2f} dB")
        peak_fc = next((fc for fc, d in rows if abs(d) == max_abs), None)
        print(f"  max  |Δ|: {max_abs:.2f} dB @ {peak_fc:.0f} Hz")
    # Sub-bass region focus (user's gate)
    sub = [(fc, d) for fc, d in rows if 80 <= fc <= 250]
    if sub:
        max_sub = max(abs(d) for _, d in sub)
        print(f"  max  |Δ| in 80-250 Hz: {max_sub:.2f} dB  ({'PASS' if max_sub < 5 else 'FAIL — sub-bass peaking biquad needed'})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run_log", nargs="?", help="Optuna run.log (auto-extracts best.json path).")
    ap.add_argument("--best", help="Explicit best.json path.")
    ap.add_argument("--anchor", default="/tmp/anchor_v2/LexAnchor_noiseburst.wav")
    ap.add_argument("--preset", default="Vintage Vocal Plate")
    ap.add_argument("--vst3", default=str(Path.home() / ".vst3/DuskVerb.vst3"))
    args = ap.parse_args()

    if args.best:
        best_json = Path(args.best)
    elif args.run_log:
        best_json = parse_best_from_runlog(args.run_log)
        if best_json is None or not best_json.exists():
            sys.exit(f"could not locate best.json from {args.run_log}")
    else:
        sys.exit("provide run_log or --best")

    params = parse_best_json(best_json)
    if not params:
        sys.exit(f"empty best.json: {best_json}")

    # Locked overrides applied for fair render conditions. Gain Trim is no
    # longer forced here so the tuned value (now present in params) is heard.
    params = {**params, "Dry/Wet": 1.0, "Bus Mode": 1, "Freeze": 0}

    print(f"Best params (from {best_json}):")
    for k, v in sorted(params.items()):
        print(f"  {k:24s} = {v}")

    report(args.preset, params, args.vst3, args.anchor)


if __name__ == "__main__":
    main()
