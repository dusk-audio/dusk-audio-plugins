#!/usr/bin/env python3
"""
distortion_check.py — repeatable nonlinear / clarity gate for DuskVerb.

Measures the energy in a reverb output that is NOT explained by linear
time-invariant (LTI) convolution — i.e. saturation, audio-rate modulation,
and aliasing. This is the "trashy / distorted highs" detector.

Method (matched-impulse residual):
    wet      = system(session_dry)              # actual reverb output of the stem
    ir       = system(impulse)                  # the system's impulse response
    lin      = session_dry  (convolve) ir       # LTI prediction
    residual = wet - alpha * lin                # alpha = least-squares gain fit
    metric_band = 20*log10( rms(residual_band) / rms(wet_band) )

For a purely LTI system residual -> -inf. Real reverbs modulate, so the value
is finite; the DISCRIMINATOR is the high-frequency residual relative to the wet
signal. A clean reverb keeps HF residual below the HF signal (negative dB); a
gritty / aliasing reverb pushes it AT or ABOVE the signal (>= 0 dB).

CRITICAL: impulse and session MUST come from the SAME render config (same
preset, same params, same build). Mismatched impulse inflates the residual and
the result is fiction. The standard render emits both <slug>_impulse.wav and
<slug>_session.wav in one pass — always use a pair from one render.

Usage:
    # absolute gate (no reference)
    distortion_check.py --dv <slug-prefix>            # dir/prefix for *_impulse.wav + *_session.wav
                        [--dry test_signals/session.wav]

    # comparative gate vs a reference render (e.g. Valhalla VintageVerb)
    distortion_check.py --dv <dv-prefix> --ref <ref-prefix> [--margin 2.0]

Exit code 0 = PASS, 1 = FAIL (so it drops into a per-round gate loop).
"""
import argparse, os, sys
import numpy as np
import soundfile as sf
from scipy.signal import fftconvolve, butter, sosfiltfilt

BANDS = [
    ("total",  20.0,   20000.0),
    ("mid",    300.0,  3000.0),
    ("HI6-12k", 6000.0, 12000.0),
    ("AIR12-20k", 12000.0, 20000.0),
]

def load_stereo(path):
    x, sr = sf.read(path, always_2d=True)
    if x.shape[1] == 1:
        x = np.repeat(x, 2, axis=1)
    return x[:, :2].astype(np.float64), sr

def bandpass(x, sr, lo, hi):
    nyq = sr * 0.5
    hi = min(hi, nyq * 0.999)
    sos = butter(4, [lo / nyq, hi / nyq], btype="band", output="sos")
    return sosfiltfilt(sos, x)

def rms(x):
    return float(np.sqrt(np.mean(x * x) + 1e-30))

def residual_channel(dry, ir, wet, sr):
    """Return (residual, wet_aligned) for one channel, gain+lag matched."""
    lin = fftconvolve(dry, ir)[: len(wet) + len(ir)]
    n = min(len(lin), len(wet))
    lin, wet = lin[:n], wet[:n]
    # integer-lag align via cross-correlation over a modest search window
    maxlag = int(sr * 0.05)
    seg = slice(0, min(n, int(sr * 1.5)))
    xc = fftconvolve(wet[seg], lin[seg][::-1])
    center = len(lin[seg]) - 1
    lo = max(0, center - maxlag); hi = min(len(xc), center + maxlag + 1)
    lag = int(np.argmax(np.abs(xc[lo:hi]))) + lo - center
    if lag > 0:
        lin = np.r_[np.zeros(lag), lin][:n]
    elif lag < 0:
        lin = np.r_[lin[-lag:], np.zeros(-lag)][:n]
    # least-squares scalar gain so trivial level mismatch is not called distortion
    denom = float(np.dot(lin, lin)) + 1e-30
    alpha = float(np.dot(wet, lin)) / denom
    residual = wet - alpha * lin
    return residual, wet

def measure(prefix, dry_path, sr_expect=None):
    imp_path = prefix + "_impulse.wav"
    ses_path = prefix + "_session.wav"
    for p in (imp_path, ses_path):
        if not os.path.isfile(p):
            sys.exit(f"  ! missing {p} (need a matched impulse+session pair from ONE render)")
    ir, sr = load_stereo(imp_path)
    wet, sr2 = load_stereo(ses_path)
    dry, sr3 = load_stereo(dry_path)
    if not (sr == sr2 == sr3):
        sys.exit(f"  ! sample-rate mismatch ir={sr} session={sr2} dry={sr3}")
    out = {}
    per_ch = []
    for ch in range(2):
        res, wetA = residual_channel(dry[:, ch], ir[:, ch], wet[:, ch], sr)
        per_ch.append((res, wetA))
    for name, lo, hi in BANDS:
        vals = []
        for res, wetA in per_ch:
            rb = bandpass(res, sr, lo, hi)
            wb = bandpass(wetA, sr, lo, hi)
            vals.append(20.0 * np.log10(rms(rb) / rms(wb)))
        out[name] = float(np.mean(vals))
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dv", required=True, help="DV render prefix (dir/slug, no _impulse.wav)")
    ap.add_argument("--ref", help="reference render prefix (e.g. VVV) for comparative gate")
    here = os.path.dirname(os.path.abspath(__file__))
    default_dry = os.path.normpath(os.path.join(
        here, "../../../../tests/duskverb_render/test_signals/session.wav"))
    ap.add_argument("--dry", default=default_dry, help="dry session stem (default: test_signals/session.wav)")
    ap.add_argument("--margin", type=float, default=2.0,
                    help="comparative: FAIL if DV HF residual exceeds ref by > margin dB (default 2.0)")
    ap.add_argument("--abs-hi", type=float, default=1.5, help="absolute gate: max HI6-12k residual dB")
    ap.add_argument("--abs-air", type=float, default=2.0, help="absolute gate: max AIR12-20k residual dB")
    args = ap.parse_args()

    if not os.path.isfile(args.dry):
        sys.exit(f"  ! dry stem not found: {args.dry}")

    dv = measure(args.dv, args.dry)
    ref = measure(args.ref, args.dry) if args.ref else None

    print("=== matched-impulse residual / wet (dB; higher = more non-LTI artifact) ===")
    hdr = f"{'band':<12}{'DV':>9}"
    if ref: hdr += f"{'REF':>9}{'Δ':>8}"
    print(hdr)
    for name, _, _ in BANDS:
        line = f"{name:<12}{dv[name]:>+9.1f}"
        if ref: line += f"{ref[name]:>+9.1f}{dv[name]-ref[name]:>+8.1f}"
        print(line)

    # gate on the two HF bands (the clarity-critical ones)
    fails = []
    if ref:
        for b in ("HI6-12k", "AIR12-20k"):
            if dv[b] - ref[b] > args.margin:
                fails.append(f"{b}: DV {dv[b]:+.1f} > REF {ref[b]:+.1f} +{args.margin} margin")
    else:
        if dv["HI6-12k"] > args.abs_hi:
            fails.append(f"HI6-12k: {dv['HI6-12k']:+.1f} > {args.abs_hi}")
        if dv["AIR12-20k"] > args.abs_air:
            fails.append(f"AIR12-20k: {dv['AIR12-20k']:+.1f} > {args.abs_air}")

    print()
    if fails:
        print("DISTORTION GATE: FAIL")
        for f in fails: print("  ✗ " + f)
        sys.exit(1)
    print("DISTORTION GATE: PASS")
    sys.exit(0)

if __name__ == "__main__":
    main()
