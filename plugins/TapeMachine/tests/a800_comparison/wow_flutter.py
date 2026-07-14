#!/usr/bin/env python3
"""wow_flutter.py — measure wow & flutter from a rendered steady tone.

Method (FM demodulation):
  1. Band-pass the carrier around its nominal frequency to isolate it.
  2. Analytic signal (Hilbert) -> unwrapped phase -> instantaneous frequency.
  3. Dimensionless pitch deviation d(t) = f_inst(t) / median(f_inst) - 1.
  4. Headline metric is a ROBUST (MAD-based) deviation, because the demod of a
     heavily-processed (oversampled, saturated) tape tone throws frequent
     single-sample glitches (spurious zero crossings / phase noise) that wreck
     an ordinary RMS or a Welch band-split. MAD ignores those outliers; it was
     validated to track a plugin's Wow knob monotonically where RMS did not.

Reported:
  pitch_dev_robust_pct   1.4826 * MAD(dev) * 100  — robust "RMS-equivalent" %.
                         THE trustworthy headline for mine-vs-reference.
  p1_99_span_pct         (p99 - p1) of dev, %      — a glitch/confidence gauge:
                         if this is >> the robust metric, the demod is glitchy
                         and any band-split below is INDICATIVE ONLY.
  glitch_pct             % of samples with |dev| > 2 % (physically impossible
                         for tape pitch — pure demod error).
  wow_pct / flutter_pct  INDICATIVE band-split (0.5-6 / 6-100 Hz) computed on the
                         glitch-rejected, decimated signal. Do not over-trust.

Usage:      wow_flutter.py render.wav [--carrier 3150] [--json]
Importable: from wow_flutter import measure_wow_flutter  -> dict (+ _spectrum_*)
"""
import sys
import json
import argparse
import numpy as np
import soundfile as sf
from scipy.signal import butter, sosfiltfilt, hilbert, welch, decimate

FS_LO = 960.0          # decimated analysis rate (keeps sub-Hz filters stable)
GLITCH_CLIP = 0.02     # |dev| beyond 2 % is a demod glitch, not tape pitch


def _load_mono(path):
    x, sr = sf.read(path)
    if x.ndim > 1:
        x = x.mean(axis=1)
    return x.astype(np.float64), sr


def _factor_stages(factor):
    """Split a decimation factor into stages of <=13 (scipy IIR guidance)."""
    stages = []
    while factor > 13:
        for f in (5, 4, 3, 2):
            if factor % f == 0:
                stages.append(f); factor //= f; break
        else:
            stages.append(2); factor //= 2
    if factor > 1:
        stages.append(factor)
    return stages or [1]


def _band_rms(dev, fs, lo, hi, order=2):
    hi = min(hi, fs / 2.0 * 0.98)
    if lo >= hi:
        return 0.0
    sos = butter(order, [lo, hi], btype="band", fs=fs, output="sos")
    return float(np.sqrt(np.mean(sosfiltfilt(sos, dev) ** 2)) * 100.0)


def measure_wow_flutter(path, carrier_hz=3150.0, trim_sec=1.5):
    """Return a dict of wow/flutter metrics for a rendered steady-tone WAV."""
    x, sr = _load_mono(path)
    t0 = int(trim_sec * sr)                       # drop fades + transport settle
    x = x[t0:len(x) - int(0.5 * sr)]
    if len(x) < sr:
        raise ValueError(f"{path}: too short after trim ({len(x)} samples)")

    # isolate the carrier (±10 %) so harmonics/noise don't corrupt the phase
    lo = max(carrier_hz * 0.90, 20.0)
    hi = min(carrier_hz * 1.10, sr / 2.0 * 0.99)
    sos = butter(4, [lo, hi], btype="band", fs=sr, output="sos")
    car = sosfiltfilt(sos, x)

    inst_phase = np.unwrap(np.angle(hilbert(car)))
    inst_freq = np.diff(inst_phase) / (2.0 * np.pi) * sr
    edge = int(0.3 * sr)
    inst_freq = inst_freq[edge:len(inst_freq) - edge]

    f_car = float(np.median(inst_freq))
    dev = inst_freq / f_car - 1.0

    # --- robust headline metric (outlier-immune) ---
    med = np.median(dev)
    mad = np.median(np.abs(dev - med))
    robust = 1.4826 * mad * 100.0
    p1, p99 = np.percentile(dev, [1, 99])
    span = float((p99 - p1) * 100.0)
    glitch = float(np.mean(np.abs(dev) > GLITCH_CLIP) * 100.0)

    # --- indicative band-split: reject glitches, interpolate, decimate ---
    d = dev.copy()
    bad = np.abs(d) > GLITCH_CLIP
    if bad.all():
        wow = flutter = 0.0
        fmod = np.array([1.0]); mag_pct = np.array([0.0])
    else:
        d[bad] = np.nan
        good = ~np.isnan(d)
        d = np.interp(np.arange(len(d)), np.where(good)[0], d[good])
        factor = max(1, int(round(sr / FS_LO)))
        stages = _factor_stages(factor)
        for f in stages:
            d = decimate(d, f, ftype="iir", zero_phase=True)
        # Derive fs_lo from the ACTUAL product of stages: _factor_stages truncates for
        # factors with a prime part > 13 (e.g. 17 -> [2, 8] = 16), so sr/factor would be
        # wrong and mis-place the wow/flutter band edges + _spectrum_fmod.
        actual_factor = int(np.prod(stages))
        fs_lo = sr / actual_factor
        d = d - np.mean(d)
        wow = _band_rms(d, fs_lo, 0.5, 6.0)
        flutter = _band_rms(d, fs_lo, 6.0, 100.0)
        nper = min(len(d), int(fs_lo * 8))
        fmod, psd = welch(d, fs=fs_lo, nperseg=nper, scaling="spectrum")
        mag_pct = np.sqrt(psd) * 100.0
        keep = (fmod > 0.3) & (fmod <= 100.0)
        fmod, mag_pct = fmod[keep], mag_pct[keep]

    return {
        "file": path,
        "carrier_detected_hz": round(f_car, 2),
        "pitch_dev_robust_pct": round(robust, 4),
        "p1_99_span_pct": round(span, 3),
        "glitch_pct": round(glitch, 2),
        "wow_pct_indicative": round(wow, 4),
        "flutter_pct_indicative": round(flutter, 4),
        "_spectrum_fmod": fmod.tolist(),
        "_spectrum_pct": mag_pct.tolist(),
    }


def _print_human(m):
    print(f"  carrier              {m['carrier_detected_hz']} Hz")
    print(f"  pitch dev (robust)   {m['pitch_dev_robust_pct']:.3f} %   <- headline")
    print(f"  p1-99 span           {m['p1_99_span_pct']:.2f} %   (glitch gauge)")
    print(f"  glitch fraction      {m['glitch_pct']:.2f} %")
    print(f"  wow  0.5-6  (indic.) {m['wow_pct_indicative']:.3f} %")
    print(f"  flut 6-100  (indic.) {m['flutter_pct_indicative']:.3f} %")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("wav")
    ap.add_argument("--carrier", type=float, default=3150.0)
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()
    m = measure_wow_flutter(a.wav, a.carrier)
    if a.json:
        m.pop("_spectrum_fmod", None); m.pop("_spectrum_pct", None)
        print(json.dumps(m, indent=2))
    else:
        print(m["file"]); _print_human(m)
