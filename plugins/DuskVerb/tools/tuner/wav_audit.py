#!/usr/bin/env python3
"""
INDEPENDENT WAV audit. Built from first principles — NO shared code with
metrics_external.py. Used to cross-validate that our tuner is measuring
what we think it's measuring.

If wav_audit.py disagrees with metrics_external.py on the same file, ONE
of them is wrong. The script prints both raw observations (per-window
RMS, FFT bins, etc.) and derived metrics (centroid, tail times) using
explicit step-by-step computation so any discrepancy is easy to trace.

Usage:
    python3 wav_audit.py <wav_path>
    python3 wav_audit.py <wav_dv> --compare <wav_lex>
"""
from __future__ import annotations
import argparse
import sys
import numpy as np
import soundfile as sf


def load_stereo_float(path):
    """Read a WAV file. Returns (sr, L, R) as float64 arrays in [-1,1]."""
    data, sr = sf.read(path, dtype='float64', always_2d=True)
    if data.shape[1] == 1:
        L = R = data[:, 0]
    else:
        L = data[:, 0]
        R = data[:, 1]
    return sr, L, R


def rms_db(x):
    """RMS of mono signal x in dBFS (1.0 peak = 0 dBFS)."""
    r = float(np.sqrt(np.mean(x ** 2)))
    if r < 1e-30:
        return float('-inf')
    return float(20.0 * np.log10(r))


def peak_db(x):
    p = float(np.max(np.abs(x)))
    if p < 1e-30:
        return float('-inf')
    return float(20.0 * np.log10(p))


def find_peak_idx(mono):
    """Index of |mono| maximum."""
    return int(np.argmax(np.abs(mono)))


def windowed_rms(mono, sr, t0_s, t1_s):
    """RMS in dB of mono[t0:t1] window."""
    a = max(0, int(t0_s * sr))
    b = min(len(mono), int(t1_s * sr))
    if b - a < 16:
        return None
    return rms_db(mono[a:b])


def fft_bin_power(x, sr):
    """Return (freqs_hz, power_per_bin). Uses Hanning window. No normalization."""
    w = np.hanning(len(x))
    X = np.fft.rfft(x * w)
    P = np.abs(X) ** 2
    f = np.fft.rfftfreq(len(x), d=1.0 / sr)
    return f, P


def spectral_centroid_hz(x, sr):
    """Magnitude-weighted spectral centroid in Hz over the segment x."""
    f, P = fft_bin_power(x, sr)
    M = np.sqrt(P)  # magnitude (not power) for centroid
    denom = float(M.sum())
    if denom < 1e-30:
        return None
    return float((f * M).sum() / denom)


def third_octave_band_db(x, sr, fc_centers):
    """Return list of (fc, total_power_db) for each band center. Edge bands
    use (fc * 2^(±1/6)) as bounds. dB is 10*log10(sum of bin powers in band)."""
    f, P = fft_bin_power(x, sr)
    out = []
    for fc in fc_centers:
        lo = fc / (2 ** (1.0 / 6.0))
        hi = fc * (2 ** (1.0 / 6.0))
        mask = (f >= lo) & (f < hi)
        energy = float(P[mask].sum())
        if energy < 1e-30:
            out.append((fc, float('-inf')))
        else:
            out.append((fc, 10.0 * np.log10(energy)))
    return out


def standard_third_oct_centers(sr):
    """Standard ISO 1/3-oct band centers from 25 Hz to Nyquist/2.5."""
    # Mathematical series: fc = 1000 * 2^(n/3), n integer
    n_lo = -16  # 25 Hz
    n_hi = 12   # ~16 kHz
    centers = [1000.0 * 2.0 ** (n / 3.0) for n in range(n_lo, n_hi + 1)]
    return [c for c in centers if 25 <= c <= sr * 0.4]


def tail_time_to_minus_db(mono, sr, target_db):
    """Time (seconds, post-peak) at which the 10 ms smoothed envelope drops
    to peak - target_db, clamped to 6 dB above measured noise floor. Returns
    None if the signal never drops that far before file ends."""
    win = max(1, int(0.010 * sr))
    pwr = mono ** 2
    sm = np.convolve(pwr, np.ones(win) / win, mode='same')
    peak = float(np.max(sm))
    if peak < 1e-30:
        return None
    pidx = int(np.argmax(sm))
    # Noise floor: median of last 500 ms.
    tail = sm[-min(int(0.5 * sr), len(sm)):]
    noise = float(np.median(tail)) if len(tail) > 0 else peak * 1e-9
    # Threshold in linear power.
    thr_signal = peak * 10.0 ** (-target_db / 10.0)
    thr_noise = noise * 4.0  # 6 dB above floor
    thr = max(thr_signal, thr_noise)
    after_peak = np.where((np.arange(len(sm)) > pidx) & (sm < thr))[0]
    if len(after_peak) == 0:
        return None
    return (int(after_peak[0]) - pidx) / sr


def stereo_corr(L, R, t0=0.0, t1=None):
    """Pearson correlation L vs R over time window."""
    sr = None  # we don't need it
    if t1 is None:
        a, b = 0, len(L)
    else:
        # caller is expected to pass sample indices; we don't have sr here
        # so treat as already in samples
        a, b = int(t0), int(t1)
    if b - a < 16:
        return None
    return float(np.corrcoef(L[a:b], R[a:b])[0, 1])


def audit_one(path):
    """Full audit of a single WAV file. Prints everything."""
    sr, L, R = load_stereo_float(path)
    mono = 0.5 * (L + R)
    n = len(mono)

    print(f"\n══════ AUDIT: {path} ══════")
    print(f"sample rate: {sr} Hz")
    print(f"length:      {n} samples ({n/sr:.3f} s)")
    print(f"channels:    {'stereo' if not np.allclose(L, R) else 'mono(L==R)'}")
    print(f"L peak:      {peak_db(L):+.2f} dBFS")
    print(f"R peak:      {peak_db(R):+.2f} dBFS")
    print(f"mono peak:   {peak_db(mono):+.2f} dBFS")

    # Loudness across windows
    print(f"\n── RMS by window (dBFS) ──")
    for t0, t1 in [(0.00, n/sr), (0.05, 0.55), (0.05, 1.00), (0.10, 0.50),
                   (0.50, 1.50), (1.00, 2.00)]:
        if t1 > n/sr:
            continue
        r = windowed_rms(mono, sr, t0, t1)
        if r is None:
            print(f"  [{t0:.2f}, {t1:.2f}]s  too short")
        else:
            print(f"  [{t0:.2f}, {t1:.2f}]s  {r:+7.2f}")

    # Post-peak integrated RMS (matches user-perceptual loudness on transients)
    pk = find_peak_idx(mono)
    post = mono[pk:]
    print(f"\n── Post-peak integrated RMS ──")
    print(f"  peak at:  {pk/sr:.4f} s")
    print(f"  post-peak RMS: {rms_db(post):+.2f} dBFS  (over {len(post)/sr:.2f} s)")

    # Tail times
    print(f"\n── Tail decay times (post-peak, 10ms smoothed envelope) ──")
    for db in (10, 20, 30, 40, 60):
        t = tail_time_to_minus_db(mono, sr, db)
        print(f"  -{db:2d} dB:  {f'{t:.3f} s' if t else '   none (below noise floor or signal floor)'}")

    # Spectral centroid (multiple windows)
    print(f"\n── Spectral centroid (Hanning + |FFT|, weighted by magnitude) ──")
    for t0, t1 in [(0.05, 0.50), (0.05, 0.30), (0.10, 0.50), (0.50, 1.50)]:
        a, b = int(t0*sr), min(int(t1*sr), n)
        if b-a < 256:
            continue
        seg = mono[a:b]
        rmsdb = rms_db(seg)
        c = spectral_centroid_hz(seg, sr)
        c_str = f"{c:7.1f} Hz" if c is not None else "    none"
        print(f"  [{t0:.2f}, {t1:.2f}]s  centroid = {c_str}   seg RMS = {rmsdb:+.2f} dBFS")

    # Stereo correlation
    if not np.allclose(L, R):
        print(f"\n── Stereo correlation ──")
        for t0, t1 in [(0.0, n/sr), (pk/sr, n/sr), (pk/sr, (pk + int(0.5*sr))/sr)]:
            if t1 > n/sr:
                continue
            r = stereo_corr(L, R, int(t0*sr), int(t1*sr))
            if r is not None:
                print(f"  [{t0:.3f}, {t1:.3f}]s  r = {r:+.4f}")

    # 1/3-octave band energies on the segment after peak (50 ms - 1.0 s)
    a, b = pk + int(0.05*sr), min(pk + int(1.0*sr), n)
    seg = mono[a:b]
    centers = standard_third_oct_centers(sr)
    bands = third_octave_band_db(seg, sr, centers)
    print(f"\n── 1/3-octave band power on segment peak+[50ms, 1.0s] ──")
    print(f"  (raw band power dB; NOT RMS-normalized — kept absolute so direct comparison is meaningful)")
    print(f"  {'fc(Hz)':>8s}  {'power(dB)':>10s}")
    for fc, p in bands:
        if p == float('-inf'):
            ps = '   -inf'
        else:
            ps = f"{p:+10.2f}"
        print(f"  {fc:8.1f}  {ps}")


def audit_compare(dv_path, lex_path):
    """Audit both files individually, then compare key metrics side by side."""
    audit_one(dv_path)
    audit_one(lex_path)

    sr_dv, L_dv, R_dv = load_stereo_float(dv_path)
    sr_lx, L_lx, R_lx = load_stereo_float(lex_path)
    if sr_dv != sr_lx:
        print(f"\n!!! sample rate mismatch: DV {sr_dv} vs Lex {sr_lx}")
        return
    sr = sr_dv
    mono_dv = 0.5 * (L_dv + R_dv)
    mono_lx = 0.5 * (L_lx + R_lx)

    pk_dv = find_peak_idx(mono_dv)
    pk_lx = find_peak_idx(mono_lx)

    print(f"\n\n════════════════════════════════════════════════════════════════")
    print(f"  COMPARISON DV vs Lex")
    print(f"════════════════════════════════════════════════════════════════")

    # Loudness comparison
    print(f"\n── Loudness (multiple definitions) ──")
    print(f"  {'window/def':28s}  {'DV':>10s}  {'Lex':>10s}  {'Δ(DV-Lex)':>10s}")
    pd = rms_db(mono_dv[pk_dv:])
    pl = rms_db(mono_lx[pk_lx:])
    print(f"  {'post-peak integrated RMS':28s}  {pd:+10.2f}  {pl:+10.2f}  {pd-pl:+10.2f}")
    pd = peak_db(mono_dv); pl = peak_db(mono_lx)
    print(f"  {'mono peak':28s}  {pd:+10.2f}  {pl:+10.2f}  {pd-pl:+10.2f}")
    for t0, t1 in [(0.05, 0.55), (0.05, 1.00)]:
        rd = windowed_rms(mono_dv, sr, t0, t1)
        rl = windowed_rms(mono_lx, sr, t0, t1)
        if rd is not None and rl is not None:
            print(f"  RMS [{t0:.2f},{t1:.2f}]s            {rd:+10.2f}  {rl:+10.2f}  {rd-rl:+10.2f}")

    # Tail-time comparison
    print(f"\n── Tail times (post-peak) ──")
    print(f"  {'-dB':>5s}  {'DV(s)':>8s}  {'Lex(s)':>8s}  {'Δ(%)':>8s}")
    for db in (10, 20, 30, 40, 60):
        td = tail_time_to_minus_db(mono_dv, sr, db)
        tl = tail_time_to_minus_db(mono_lx, sr, db)
        if td and tl:
            dpct = (td - tl) / tl * 100
            print(f"  -{db:2d}    {td:8.3f}  {tl:8.3f}  {dpct:+7.1f}%")
        else:
            tdv = f"{td:.3f}" if td else "   none"
            tlv = f"{tl:.3f}" if tl else "   none"
            print(f"  -{db:2d}    {tdv:>8s}  {tlv:>8s}  {'   --':>8s}")

    # Centroid comparison
    print(f"\n── Spectral centroid ──")
    print(f"  {'window':18s}  {'DV(Hz)':>8s}  {'Lex(Hz)':>8s}  {'Δ(%)':>8s}")
    for t0, t1 in [(0.05, 0.30), (0.05, 0.50), (0.10, 0.50), (0.50, 1.50)]:
        ad, bd = int(t0*sr)+pk_dv, min(int(t1*sr)+pk_dv, len(mono_dv))
        al, bl = int(t0*sr)+pk_lx, min(int(t1*sr)+pk_lx, len(mono_lx))
        if bd-ad < 256 or bl-al < 256:
            continue
        cd = spectral_centroid_hz(mono_dv[ad:bd], sr)
        cl = spectral_centroid_hz(mono_lx[al:bl], sr)
        if cd and cl:
            print(f"  [{t0:.2f},{t1:.2f}]s post-pk  {cd:8.0f}  {cl:8.0f}  {(cd-cl)/cl*100:+7.1f}%")

    # 1/3-octave band-energy comparison (PEAK-ALIGNED, ABSOLUTE)
    centers = standard_third_oct_centers(sr)
    ad, bd = pk_dv + int(0.05*sr), min(pk_dv + int(1.0*sr), len(mono_dv))
    al, bl = pk_lx + int(0.05*sr), min(pk_lx + int(1.0*sr), len(mono_lx))
    seg_dv = mono_dv[ad:bd]
    seg_lx = mono_lx[al:bl]
    # Match segment length (longer one truncated)
    n = min(len(seg_dv), len(seg_lx))
    seg_dv = seg_dv[:n]; seg_lx = seg_lx[:n]
    bands_dv = third_octave_band_db(seg_dv, sr, centers)
    bands_lx = third_octave_band_db(seg_lx, sr, centers)

    print(f"\n── 1/3-oct band energy DELTA, ABSOLUTE (no normalization) ──")
    print(f"  positive Δ = Lex has more energy in this band (DV needs +Δ dB lift)")
    print(f"  {'fc(Hz)':>8s}  {'DV(dB)':>10s}  {'Lex(dB)':>10s}  {'Δ-needed':>9s}  bar")
    rows = []
    for (fc1, dv), (fc2, lx) in zip(bands_dv, bands_lx):
        if dv == float('-inf') or lx == float('-inf'):
            continue
        delta = lx - dv
        n_bars = min(int(abs(delta)), 15)
        bar = ('+' if delta > 0 else '-')*n_bars if abs(delta) > 0.5 else ''
        flag = ' ⚠' if abs(delta) > 5 else ''
        print(f"  {fc1:8.1f}  {dv:+10.2f}  {lx:+10.2f}  {delta:+9.2f}  {bar}{flag}")
        rows.append((fc1, dv, lx, delta))
    abs_d = [abs(r[3]) for r in rows]
    if not rows or not abs_d:
        print("\n  (no valid bands — both renders below floor in every band)")
    else:
        max_abs = max(abs_d)
        print(f"\n  mean |Δ|:  {sum(abs_d)/len(abs_d):.2f} dB")
        print(f"  max  |Δ|:  {max_abs:.2f} dB @ {[r[0] for r in rows if abs(r[3])==max_abs][0]:.1f} Hz")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("wav", help="WAV file to audit.")
    ap.add_argument("--compare", help="Optional second WAV to compare against (DV vs reference).")
    args = ap.parse_args()
    if args.compare:
        audit_compare(args.wav, args.compare)
    else:
        audit_one(args.wav)


if __name__ == "__main__":
    main()
