#!/usr/bin/env python3
"""
Diagnostic multi-stimulus comparator — figures out *which* stimulus
reveals the perceptual gap between a DuskVerb preset and a reference
plugin (Lex, VVV, etc.).

Why this exists:
  The 1-sample Dirac impulse is a textbook DSP trap for time-variant
  reverbs. Heavily modulated algorithmic plates respond very differently
  to a single transient than to sustained or repeated-onset signal.
  Tuning against the Dirac alone matches the LTI fingerprint while
  missing the modulation-beating, modal-density-on-sparse-onsets, and
  HF-transient differences a listener actually hears.

What it does:
  For both a DV preset (rendered through the in-tree harness, post-
  warm-up) and a reference plugin preset (rendered through the same
  harness via --vst2/--vst3 + --load-state), render four stimuli:

    1. Dirac impulse                  — LTI fingerprint (legacy)
    2. Pink-noise burst, 50ms         — broadband + modulation-relevant
    3. Sine sweep 20 Hz -> 20 kHz    — coverage of every modal region
    4. Snare hit (real recording)     — sparse onset + transient HF

  Compute the new noise-floor-aware decay times + spec_L1 +
  centroid_50/500 on each stimulus pair. Print a matrix.

  Caller can then audit which stimulus shows the largest gap and
  re-tune against THAT signal.

Usage:
  python3 diagnostic_compare.py \
      --dv-preset "Vintage Vocal Plate" \
      --ref-plugin /home/marc/.vst/yabridge/LexVintagePlate.so \
      --ref-state /home/marc/projects/dusk-audio-tools/anchors/lex/fxp/vocal_plate/Lpl0/lex-vintage-vocal-plate.fxp \
      --prerun-seconds 3.0 \
      --output-dir /tmp/diagnostic_vvp

Output:
  Per-stimulus WAVs for both DV and ref (so you can A/B in the DAW)
  + console table of metrics + a flagging summary at the bottom.
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Optional

import numpy as np
import soundfile as sf

REPO_ROOT = Path(__file__).resolve().parents[4]
RENDER_BIN = REPO_ROOT / "build" / "tests" / "duskverb_render" / "duskverb_render"
DEFAULT_DV_VST3 = Path.home() / ".vst3" / "DuskVerb.vst3"

sys.path.insert(0, str(Path(__file__).resolve().parent))
from metrics_external import compute_metrics


# Stimulus kinds the harness already produces. We use the harness's own
# preroll-warmed render path; passing --prerun-seconds N applies to every
# stimulus produced.
STIMULI = ["impulse", "noiseburst", "snare"]


def render_dv(preset: str, vst3: Path, out_dir: Path, prerun: float, slug: str) -> None:
    """Render the DV preset through the harness. Output WAVs land in
    out_dir/{slug}_{stim}.wav for each stim in STIMULI."""
    out_dir.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(RENDER_BIN),
        "--vst3", str(vst3),
        "--output-dir", str(out_dir),
        "--slug", slug,
        "--prerun-seconds", str(prerun),
        # Force 100% wet in case the preset's mix isn't 1.0
        "--param", "Dry/Wet=1.0",
        "--param", "Bus Mode=1",
        preset,
    ]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    if result.returncode != 0:
        sys.stderr.write(f"DV render failed (rc={result.returncode}):\n{result.stderr[-800:]}\n")
        raise RuntimeError(f"DV render failed for '{preset}'")


def render_ref(ref_plugin: Path, ref_state: Path, out_dir: Path, prerun: float, slug: str,
               ref_mix_param: Optional[str] = None) -> None:
    """Render the reference plugin through the harness. Uses --load-state
    to apply the fxp/vstpreset/state-dump, then forces wet=100%."""
    out_dir.mkdir(parents=True, exist_ok=True)
    # Reference plugin auto-detect by extension via --vst2/--vst3
    ext = ref_plugin.suffix.lower()
    if ext in (".so", ".dll"):
        flag = "--vst2"
    elif ext == ".vst3":
        flag = "--vst3"
    else:
        # Try VST2 default for unknown extensions (yabridge .so).
        flag = "--vst2"
    cmd = [
        str(RENDER_BIN),
        flag, str(ref_plugin),
        "--load-state", str(ref_state),
        "--output-dir", str(out_dir),
        "--slug", slug,
        "--prerun-seconds", str(prerun),
        # Most third-party reverbs expose "Mix" / "Dry/Wet" — try a few names
        # so the wet override survives across plugins.
        "--param", f"{ref_mix_param or 'Mix'}=1.0",
        # Pass a placeholder preset name (harness wants one positional arg).
        # Reference plugin ignores DuskVerb preset names; the --load-state
        # path is what configures it.
        "RefRender",
    ]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
    if result.returncode != 0:
        sys.stderr.write(f"REF render failed (rc={result.returncode}):\n{result.stderr[-800:]}\n")
        raise RuntimeError("REF render failed")


def safe_metrics(path: Path) -> Optional[dict]:
    if not path.exists():
        return None
    try:
        return compute_metrics(str(path))
    except Exception as e:
        sys.stderr.write(f"compute_metrics failed for {path}: {e}\n")
        return None


def _fmt_t(v):
    if v is None: return "  none"
    return f"{v:6.3f}"


def _fmt_pct(dv, vv):
    if dv is None or vv is None or vv == 0:
        return "    -"
    return f"{(dv-vv)/vv*100:+6.1f}%"


def stimulus_centroid(wav_path: Path, t_start: float, t_end: float) -> Optional[float]:
    """Compute spectral centroid over [t_start, t_end] of the rendered file
    DIRECTLY (bypasses the metrics_external normalization shenanigans)."""
    try:
        x, sr = sf.read(str(wav_path))
    except Exception:
        return None
    if x.ndim > 1:
        x = x.mean(axis=1)
    a = int(t_start * sr); b = min(int(t_end * sr), len(x))
    if b - a < 256:
        return None
    seg = x[a:b] * np.hanning(b - a)
    S = np.abs(np.fft.rfft(seg)); f = np.fft.rfftfreq(b - a, 1.0 / sr)
    den = float(S.sum()) + 1e-30
    return float(np.sum(f * S) / den)


def stimulus_spec_l1(dv_path: Path, ref_path: Path, t_start=0.05, t_end=2.0) -> Optional[float]:
    """RMS-normalized 1/3-oct dB L1 across [t_start, t_end] of the rendered
    files DIRECTLY (so we measure the same windowed segment for both)."""
    try:
        x_dv, sr_dv = sf.read(str(dv_path))
        x_rf, sr_rf = sf.read(str(ref_path))
    except Exception:
        return None
    if sr_dv != sr_rf:
        return None
    if x_dv.ndim > 1: x_dv = x_dv.mean(axis=1)
    if x_rf.ndim > 1: x_rf = x_rf.mean(axis=1)
    a = int(t_start * sr_dv); b = min(int(t_end * sr_dv), len(x_dv), len(x_rf))
    if b - a < 1024: return None
    sd = x_dv[a:b] * np.hanning(b - a); sr_sig = x_rf[a:b] * np.hanning(b - a)
    Sd = np.abs(np.fft.rfft(sd)) ** 2
    Sr = np.abs(np.fft.rfft(sr_sig)) ** 2
    f = np.fft.rfftfreq(b - a, 1.0 / sr_dv)
    centers = 1000.0 * 2.0 ** (np.arange(-18, 13) / 3.0)
    centers = centers[(centers > 25) & (centers < sr_dv * 0.4)]
    def band_db(S):
        out = []
        for fc in centers:
            lo, hi = fc / (2 ** (1/6)), fc * (2 ** (1/6))
            mask = (f >= lo) & (f < hi)
            e = float(S[mask].sum()) + 1e-30
            out.append(10 * np.log10(e))
        a = np.array(out)
        return a - np.mean(a)  # RMS-normalize (subtract mean dB ~ ratio-only)
    return float(np.mean(np.abs(band_db(Sd) - band_db(Sr))))


def main():
    ap = argparse.ArgumentParser(description="Multi-stimulus DV-vs-reference diagnostic.")
    ap.add_argument("--dv-preset", required=True, help="DuskVerb preset name in render.cpp.")
    ap.add_argument("--dv-vst3", default=str(DEFAULT_DV_VST3))
    ap.add_argument("--ref-plugin", required=True, help="Reference plugin .so / .dll / .vst3 path.")
    ap.add_argument("--ref-state", required=True, help="Reference plugin state file (fxp / vstpreset / state dump).")
    ap.add_argument("--ref-mix-param", default="Mix",
                    help="Name of the reference plugin's wet/dry parameter (default 'Mix').")
    ap.add_argument("--prerun-seconds", type=float, default=3.0,
                    help="Warm-up silence per stimulus (default 3.0 s).")
    ap.add_argument("--output-dir", default="/tmp/diagnostic_compare",
                    help="Where the rendered WAVs land for DAW A/B.")
    args = ap.parse_args()

    out_root = Path(args.output_dir)
    dv_dir = out_root / "dv"
    ref_dir = out_root / "ref"
    if out_root.exists():
        shutil.rmtree(out_root)

    print(f"=== Rendering DV ({args.dv_preset}) with prerun {args.prerun_seconds}s ===")
    render_dv(args.dv_preset, Path(args.dv_vst3), dv_dir, args.prerun_seconds, "DV")

    print(f"=== Rendering REF ({Path(args.ref_plugin).name}) with prerun {args.prerun_seconds}s ===")
    render_ref(Path(args.ref_plugin), Path(args.ref_state), ref_dir, args.prerun_seconds, "REF",
               ref_mix_param=args.ref_mix_param)

    print()
    header = (f"{'stimulus':12s} | "
              f"{'DV t30':>7s} {'REF t30':>7s} {'Δ%':>7s} | "
              f"{'DV t60':>7s} {'REF t60':>7s} {'Δ%':>7s} | "
              f"{'sL1(0-2s)':>9s} | "
              f"{'cent50DV':>9s} {'cent50RF':>9s} {'Δ%':>7s} | "
              f"{'cent500DV':>10s} {'cent500RF':>10s} {'Δ%':>7s}")
    print(header)
    print("-" * len(header))
    rows = []
    for stim in STIMULI:
        dv_path = dv_dir / f"DV_{stim}.wav"
        rf_path = ref_dir / f"REF_{stim}.wav"
        dv_m = safe_metrics(dv_path) or {}
        rf_m = safe_metrics(rf_path) or {}

        dv_t30 = dv_m.get('tail_t30'); rf_t30 = rf_m.get('tail_t30')
        dv_t60 = dv_m.get('tail_t60'); rf_t60 = rf_m.get('tail_t60')

        sL1 = stimulus_spec_l1(dv_path, rf_path)

        dv_c50  = stimulus_centroid(dv_path, 0.05, 0.50)
        rf_c50  = stimulus_centroid(rf_path, 0.05, 0.50)
        dv_c500 = stimulus_centroid(dv_path, 0.50, 1.50)
        rf_c500 = stimulus_centroid(rf_path, 0.50, 1.50)

        def _f(x, d=1): return f"{x:9.{d}f}" if isinstance(x, (int, float)) else "     none"
        def _f2(x, d=1): return f"{x:10.{d}f}" if isinstance(x, (int, float)) else "      none"
        print(f"{stim:12s} | "
              f"{_fmt_t(dv_t30)} {_fmt_t(rf_t30)} {_fmt_pct(dv_t30, rf_t30)} | "
              f"{_fmt_t(dv_t60)} {_fmt_t(rf_t60)} {_fmt_pct(dv_t60, rf_t60)} | "
              f"{(sL1 if isinstance(sL1,(int,float)) else float('nan')):9.2f} | "
              f"{_f(dv_c50, 0)} {_f(rf_c50, 0)} {_fmt_pct(dv_c50, rf_c50)} | "
              f"{_f2(dv_c500, 0)} {_f2(rf_c500, 0)} {_fmt_pct(dv_c500, rf_c500)}")
        rows.append({
            'stim': stim,
            'dv_t30': dv_t30, 'rf_t30': rf_t30,
            'dv_t60': dv_t60, 'rf_t60': rf_t60,
            'sL1': sL1,
            'dv_c50': dv_c50, 'rf_c50': rf_c50,
            'dv_c500': dv_c500, 'rf_c500': rf_c500,
        })

    # Flagging summary: identify the stimulus that produces the largest
    # gap, since that's the perceptual target the tuner is missing.
    print()
    print("=== Flagging summary ===")
    print("(stimulus with largest spec_L1 -> most likely the listener-perceived gap)")
    best = None
    for r in rows:
        s = r['sL1']
        if s is None: continue
        if best is None or s > best['sL1']:
            best = r
    if best:
        print(f"  worst spec_L1: {best['stim']} ({best['sL1']:.2f} dB)")
    print()
    print(f"Rendered WAVs for DAW A/B in:")
    print(f"  DV:  {dv_dir}")
    print(f"  REF: {ref_dir}")
    print(f"Open both in DAW, A/B each {{impulse, noiseburst, snare}} pair.")


if __name__ == "__main__":
    main()
