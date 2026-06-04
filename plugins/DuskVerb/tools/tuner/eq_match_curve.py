#!/usr/bin/env python3
"""
Auto-generate the EQ-match delta curve between a DV render and a Lex anchor
(equivalent of plotting both in a match-EQ plugin and reading the orange line).

Replaces the manual "open both in match-EQ, screenshot the orange curve"
loop with a programmatic equivalent so future presets don't need user-visual
verification of EQ gaps.

Usage:
    python3 eq_match_curve.py <dv_dir> <lex_dir>
    python3 eq_match_curve.py /tmp/tune_preset/X/dv /tmp/anchor_v2

Outputs:
    - 1/24-octave delta table (DV vs anchor, on sustained-pink steady-state)
    - Identifies the bands with |Δ| > 2 dB (the bands a human would tweak)
    - Prints an ASCII bar chart of the delta curve

Stimulus: prefers sustained-pink (musical-content proxy). Falls back to
noiseburst if sustained isn't rendered.
"""
from __future__ import annotations
import argparse, sys
from pathlib import Path
import numpy as np, soundfile as sf


def find_stim(d, stem):
    c = sorted(Path(d).glob(f"*_{stem}.wav"))
    return str(c[0]) if c else None


def fine_band_power_db(p, t_start, t_end, n_bands_per_oct=6):
    """1/(n_bands_per_oct)-octave band-power table over [t_start, t_end].
    Returns list of (fc, power_db) tuples."""
    x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim>1 else x
    a, b = int(t_start*sr), min(int(t_end*sr), len(m))
    if b-a < 1024:
        return []
    seg = m[a:b] * np.hanning(b-a)
    S = np.abs(np.fft.rfft(seg))**2
    f = np.fft.rfftfreq(b-a, 1.0/sr)
    # Band centers: 1000 * 2^(n / n_bands_per_oct), n integer.
    n_lo = int(np.log2(20/1000) * n_bands_per_oct) - 1
    n_hi = int(np.log2(20000/1000) * n_bands_per_oct) + 1
    centers = [1000.0 * 2.0**(n/n_bands_per_oct) for n in range(n_lo, n_hi+1)]
    centers = [c for c in centers if 20 <= c <= sr*0.45]
    rows = []
    for fc in centers:
        bw = 2.0 ** (1.0 / (2 * n_bands_per_oct))
        lo, hi = fc / bw, fc * bw
        mask = (f >= lo) & (f < hi)
        e = float(S[mask].sum()) + 1e-30
        rows.append((fc, 10.0 * np.log10(e)))
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("dv_dir")
    ap.add_argument("lex_dir")
    ap.add_argument("--stimulus", default="auto",
                    choices=["auto", "sustained", "noiseburst", "snare"])
    ap.add_argument("--t0", type=float, default=2.5,
                    help="Window start (s). sustained-pink steady-state defaults 2.5.")
    ap.add_argument("--t1", type=float, default=4.0)
    ap.add_argument("--bands-per-oct", type=int, default=6,
                    help="Resolution. 6 = 1/6-oct (default). 12 = 1/12-oct.")
    args = ap.parse_args()

    stem = args.stimulus
    if stem == "auto":
        for s in ("sustained", "noiseburst", "snare"):
            if find_stim(args.dv_dir, s) and find_stim(args.lex_dir, s):
                stem = s
                break
        if stem == "auto":
            sys.exit("no matching stimulus pair found in both dirs")
    dv = find_stim(args.dv_dir, stem)
    lx = find_stim(args.lex_dir, stem)
    if not dv or not lx:
        sys.exit(f"missing {stem} render in dv_dir or lex_dir")

    # For non-sustained stimuli, the steady-state window is meaningless;
    # use post-peak windows instead.
    if stem != "sustained":
        # Reset to post-peak windows: use peak-aligned [50ms, 1.0s].
        x_dv, sr_dv = sf.read(dv); m_dv = x_dv.mean(axis=1) if x_dv.ndim>1 else x_dv
        pk = int(np.argmax(np.abs(m_dv)))
        args.t0 = pk / sr_dv + 0.05
        args.t1 = pk / sr_dv + 1.0

    print(f"\n══ EQ-MATCH DELTA CURVE ══")
    print(f"  DV:  {dv}")
    print(f"  Lex: {lx}")
    print(f"  Stimulus: {stem}   Window: [{args.t0:.2f}, {args.t1:.2f}]s")
    print(f"  Resolution: 1/{args.bands_per_oct}-octave")
    print()

    dv_b = fine_band_power_db(dv, args.t0, args.t1, args.bands_per_oct)
    lx_b = fine_band_power_db(lx, args.t0, args.t1, args.bands_per_oct)
    rows = []
    for (fc_d, dv_v), (_, lx_v) in zip(dv_b, lx_b):
        d = lx_v - dv_v   # positive = ADD dB to DV to match Lex
        rows.append((fc_d, dv_v, lx_v, d))

    print(f"  {'fc(Hz)':>8s}  {'DV(dB)':>8s}  {'Lex(dB)':>8s}  {'Δ-need':>8s}  bar")
    for fc, dv_v, lx_v, d in rows:
        n_bars = min(int(abs(d)), 15)
        bar = ('+' if d > 0 else '-')*n_bars if abs(d) > 0.5 else ''
        flag = ' ⚠' if abs(d) > 5 else ''
        print(f"  {fc:8.1f}  {dv_v:+8.2f}  {lx_v:+8.2f}  {d:+8.2f}  {bar}{flag}")

    # Auto-identify peaks (bands where human would put an EQ node)
    print()
    print("── Suggested EQ adjustments (bands where |Δ| > 2 dB) ──")
    suggestions = []
    for fc, _, _, d in rows:
        if abs(d) > 2.0:
            kind = "LIFT" if d > 0 else "CUT "
            suggestions.append((fc, d, kind))
    if not suggestions:
        print("  (no band exceeds ±2 dB — clean match)")
    else:
        # Group adjacent suggestions
        last_fc = None
        for fc, d, kind in suggestions:
            print(f"  {kind} {abs(d):4.1f} dB @ {fc:6.0f} Hz")

    # Identify primary EQ moves (worst gap per 4 broad bands)
    print()
    print("── PRIMARY EQ MOVES (worst Δ per region) ──")
    regions = [('deep sub (20-50)', 20, 50),
               ('sub-low (50-250)', 50, 250),
               ('low-mid (250-1k)', 250, 1000),
               ('mid (1-4k)', 1000, 4000),
               ('hi (4-10k)', 4000, 10000),
               ('air (>10k)', 10000, 30000)]
    for name, lo, hi in regions:
        region = [(fc, d) for fc, _, _, d in rows if lo <= fc < hi]
        if not region: continue
        worst = max(region, key=lambda r: abs(r[1]))
        if abs(worst[1]) < 1.0:
            print(f"  {name:22s}  matched (max Δ {worst[1]:+.1f} dB @ {worst[0]:.0f} Hz)")
        else:
            kind = "LIFT" if worst[1] > 0 else "CUT "
            print(f"  {name:22s}  {kind} {abs(worst[1]):4.1f} dB @ {worst[0]:.0f} Hz")


if __name__ == "__main__":
    main()
