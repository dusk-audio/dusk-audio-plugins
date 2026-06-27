#!/usr/bin/env python3
"""Comprehensive sweep: decay + spectral + PostTankEQ all axes jointly.
Loss = sum of relative gate distances across T60/RT60/RMS/centroid/spec/sine1k.
Holds baked baseDelays via map lookup. Goal: close as many full_check gates
as possible in one pass."""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
import optuna
import scipy.io.wavfile as wf
import scipy.signal as sg

REPO = Path(__file__).resolve().parents[4]
RENDER = REPO / "build" / "tests" / "duskverb_render" / "duskverb_render"
VST3 = Path.home() / ".vst3" / "DuskVerb.vst3"

sys.path.insert(0, str(Path(__file__).resolve().parent))
from full_check import _t60_band_schroeder, _full_rms_db, _tail_mod_peak_freq  # noqa: E402

MOD_TARGETS = {
    "Vocal Hall":  [(40, 250, 1.92), (250, 1000, 1.56), (4000, 12000, 2.47)],
    "Bright Hall": [(40, 250, 3.02), (250, 1000, 2.01),
                    (1000, 4000, 4.76), (4000, 12000, 7.23)],
}

# (lo, hi, center, target_T60s) per band — for VH from VVV anchor
T60_VH = {
    63: 5.57, 125: 5.23, 250: 5.03, 500: 4.20,
    1000: 3.18, 2000: 2.74, 4000: 2.45, 8000: 2.04, 16000: 1.70,
}
T60_BH = {
    63: 7.30, 125: 7.10, 250: 6.12, 500: 5.59,
    1000: 4.73, 2000: 4.15, 4000: 3.37, 8000: 2.36, 16000: 1.37,
}

T60_BANDS = [(45, 90, 63), (90, 180, 125), (180, 350, 250), (350, 700, 500),
             (700, 1400, 1000), (1400, 2800, 2000), (2800, 5600, 4000),
             (5600, 11200, 8000), (11200, 22000, 16000)]

SS_BANDS = [(20, 50), (50, 100), (100, 250), (250, 1000), (1000, 4000),
            (4000, 12000), (5000, 10000), (10000, 20000)]


def _band_rms_db(path, lo, hi, win_s=2.0):
    sr, x = wf.read(path)
    if x.dtype.kind == "i":
        x = x.astype(np.float64) / np.iinfo(x.dtype).max
    if x.ndim > 1:
        x = x.mean(axis=1)
    peak = int(np.argmax(np.abs(x)))
    seg = x[peak:peak + int(sr * win_s)]
    sos = sg.butter(4, [max(lo, 10.0), min(hi, sr * 0.49)], "band",
                    fs=sr, output="sos")
    f = sg.sosfiltfilt(sos, seg)
    rms = np.sqrt(np.mean(f ** 2))
    return 20.0 * np.log10(max(rms, 1.0e-12))


def _edt_band(path, lo, hi, win_s=2.0):
    """Early decay time (s) via first-crossing. Band-pass filters the
    post-peak tail to [lo, hi], takes its Hilbert envelope, normalizes to a
    0 dB peak, and returns the time of the FIRST envelope sample below -10 dB.
    This is a first-crossing measure, NOT a least-squares slope fit. Returns
    None if the envelope never drops below -10 dB within the window.

    Args:
        path:   WAV file path.
        lo, hi: band-pass edges (Hz), clamped to [10, sr*0.49].
        win_s:  post-peak window length (s) analyzed."""
    sr, x = wf.read(path)
    if x.dtype.kind == "i":
        x = x.astype(np.float64) / np.iinfo(x.dtype).max
    if x.ndim > 1:
        x = x.mean(axis=1)
    peak = int(np.argmax(np.abs(x)))
    seg = x[peak:peak + int(sr * win_s)]
    sos = sg.butter(4, [max(lo, 10.0), min(hi, sr * 0.49)], "band",
                    fs=sr, output="sos")
    band = sg.sosfiltfilt(sos, seg)
    env = np.abs(sg.hilbert(band))
    env_db = 20.0 * np.log10(np.maximum(env, 1.0e-12))
    env_db -= env_db.max()
    idx = np.where(env_db < -10.0)[0]
    if len(idx) == 0:
        return None
    return float(idx[0] / sr)


def measure_anchor(anchor_dir, slug):
    """Pre-compute anchor measurements for fast loss eval."""
    nb = anchor_dir / f"{slug}_noiseburst.wav"
    snare = anchor_dir / f"{slug}_snare.wav"
    s1k = anchor_dir / f"{slug}_sine1k.wav"
    missing = [str(p) for p in (nb, s1k, snare) if not p.exists()]
    if missing:
        raise ValueError(
            "measure_anchor: missing anchor file(s): " + ", ".join(missing))
    out = {
        "ss": {},
        "edt": {},
        "sine1k_rms": _full_rms_db(str(s1k)),
        "snare_rms":  _full_rms_db(str(snare)),
    }
    for lo, hi in SS_BANDS:
        out["ss"][(lo, hi)] = _band_rms_db(str(nb), lo, hi)
    for lo, hi in [(20, 100), (100, 250), (500, 2000), (2000, 8000)]:
        out["edt"][(lo, hi)] = _edt_band(str(nb), lo, hi)
    return out


def render_trial(out_dir, preset, p):
    cmd = [str(RENDER), "--vst3", str(VST3), "--program", preset,
           "--param", "Dry/Wet=1.0", "--param", "Bus Mode=1",
           "--param", f"Decay Time={p['decay']}",
           "--param", f"Bass Multiply={p['bass']}",
           "--param", f"Treble Multiply={p['treble']}",
           "--param", f"Mid Multiply={p['mid']}",
           "--param", f"Hi Cut Shelf={p['hi_cut_shelf']}",
           "--param", f"Low Crossover={p['low_xover']}",
           "--param", f"High Crossover={p['high_xover']}",
           "--param", f"Hi Cut={p['hi_cut']}",
           "--param", f"Lo Cut={p['lo_cut']}",
           "--param", f"Saturation={p['saturation']}",
           "--param", f"Gain Trim={p['gain_trim']}",
           "--param", f"PostTankEQ Band 0 Gain={p['pteq0']}",
           "--param", f"PostTankEQ Band 1 Gain={p['pteq1']}",
           "--param", f"PostTankEQ Band 2 Gain={p['pteq2']}",
           "--param", f"PostTankEQ Band 3 Gain={p['pteq3']}",
           "--param", f"Post Band Sub Gain={p['pb_sub']}",
           "--param", f"Post Band Low-Mid Gain={p['pb_lowmid']}",
           "--param", f"Post Band Mid-High Gain={p['pb_midhi']}",
           "--param", f"Post Band Air Gain={p['pb_air']}",
           # Phase ζ pre-Hadamard peaking (freq + Q fixed; gain swept)
           "--param", f"In-Loop Peak Freq=1000.0",
           "--param", f"In-Loop Peak Q=2.0",
           "--param", f"In-Loop Peak Gain={p['ilp_db']}",
           # Phase η dual-time-constant bass shelf (fcs + transition fixed; gains swept)
           "--param", f"Bass Shelf Fast Fc=400.0",
           "--param", f"Bass Shelf Slow Fc=200.0",
           "--param", f"Bass Shelf Transition=100.0",
           "--param", f"Bass Shelf Fast Gain={p['bs_fast_db']}",
           "--param", f"Bass Shelf Slow Gain={p['bs_slow_db']}",
           "--prerun-seconds", "5",
           "--output-dir", str(out_dir)]
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=120).returncode == 0
    except subprocess.TimeoutExpired:
        print(f"render timeout (>120s) for {preset}", file=sys.stderr)
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preset", required=True)
    ap.add_argument("--anchor-dir", required=True)
    ap.add_argument("--trials", type=int, default=500)
    ap.add_argument("--workers", type=int, default=6)
    ap.add_argument("--out-json", default=None,
                    help="Write best params + measurements to this JSON file.")
    args = ap.parse_args()

    slug = args.preset.replace(" ", "")
    anchor_dir = Path(args.anchor_dir)
    anchor = measure_anchor(anchor_dir, slug)

    if args.preset == "Vocal Hall":
        t60_target = T60_VH
    elif args.preset == "Bright Hall":
        t60_target = T60_BH
    else:
        sys.exit(f"no T60 targets for {args.preset}")

    print(f"Anchor sine1k RMS: {anchor['sine1k_rms']:.2f} dB")
    for (lo, hi), v in anchor["ss"].items():
        print(f"  ss {lo:>5}-{hi:>5} Hz: {v:6.2f} dB")

    def objective(trial):
        p = {
            "decay":      trial.suggest_float("decay", 2.0, 8.0),
            "bass":       trial.suggest_float("bass", 1.0, 2.0),
            "treble":     trial.suggest_float("treble", 1.0, 2.0),
            "mid":        trial.suggest_float("mid", 0.7, 1.3),
            "hi_cut_shelf": trial.suggest_float("hi_cut_shelf", -12.0, 0.0),
            "low_xover":  trial.suggest_float("low_xover", 200.0, 1200.0),
            "high_xover": trial.suggest_float("high_xover", 3000.0, 12000.0),
            "hi_cut":     trial.suggest_float("hi_cut", 4000.0, 15000.0),
            "lo_cut":     trial.suggest_float("lo_cut", 20.0, 100.0),
            "saturation": trial.suggest_float("saturation", 0.0, 0.5),
            "gain_trim":  trial.suggest_float("gain_trim", -6.0, 6.0),
            "pteq0":      trial.suggest_float("pteq0", -6.0, 6.0),
            "pteq1":      trial.suggest_float("pteq1", -6.0, 6.0),
            "pteq2":      trial.suggest_float("pteq2", -6.0, 6.0),
            "pteq3":      trial.suggest_float("pteq3", -6.0, 6.0),
            "pb_sub":     trial.suggest_float("pb_sub",    -4.0, 4.0),
            "pb_lowmid":  trial.suggest_float("pb_lowmid", -4.0, 4.0),
            "pb_midhi":   trial.suggest_float("pb_midhi",  -4.0, 4.0),
            "pb_air":     trial.suggest_float("pb_air",    -4.0, 4.0),
            # Phase ζ pre-Hadamard peaking at 1k — clamped to safe zone.
            # Debug pass (2026-05-29) found +10.88 dB explodes impulse band
            # by +94 dB → breaks 21+ transient gates. Safe range [-3, +3] dB
            # gives modest sine1k benefit without transient blow-up.
            "ilp_db":     trial.suggest_float("ilp_db",    -3.0,  3.0),
            "bs_fast_db": trial.suggest_float("bs_fast_db", -8.0,  4.0),
            "bs_slow_db": trial.suggest_float("bs_slow_db", -4.0,  8.0),
        }

        tmp = Path(tempfile.mkdtemp(prefix=f"comb_{trial.number}_"))
        try:
            if not render_trial(tmp, args.preset, p):
                return 1e6
            nb = tmp / f"{slug}_noiseburst.wav"
            s1k = tmp / f"{slug}_sine1k.wav"
            if not nb.exists() or not s1k.exists():
                return 1e6

            loss = 0.0
            measured = {}

            # T60 per band — Phase η loss boost: lower-band weight 6x (was 3x)
            # to force the optimizer to use the new dual-time-constant bass
            # shelf to close bass T60 instead of overshooting Decay knob.
            for lo, hi, c in T60_BANDS:
                t60 = _t60_band_schroeder(str(nb), lo, hi)
                if t60 is None:
                    loss += 1.0
                    continue
                measured[f"t60_{c}"] = t60
                err = abs(t60 - t60_target[c]) / t60_target[c]
                w = 6.0 if c <= 500 else 3.0
                loss += err * w

            # ss band RMS vs anchor (weight 1.5)
            for lo, hi in SS_BANDS:
                dv = _band_rms_db(str(nb), lo, hi)
                lex = anchor["ss"][(lo, hi)]
                err = abs(dv - lex) / 2.0  # normalize by 2 dB gate
                loss += err * 1.5

            # sine1k full RMS (Phase ζ loss weight 3x — tightened from 6x
            # after debug pass showed +10.88 dB ilp tradeoff broke 21+
            # transient gates via impulse-band explosion).
            dv_s = _full_rms_db(str(s1k))
            measured["sine1k_rms"] = dv_s
            err = abs(dv_s - anchor["sine1k_rms"]) / 2.0
            loss += err * 3.0

            # Phase ζ guard: penalize impulse-band explosion (sweep best
            # +10.88 dB ilp produced +94 dB at 1k impulse band vs bypass).
            # Catches the transient blow-up that whole-file RMS misses.
            try:
                import scipy.io.wavfile as _wf, scipy.signal as _sg, numpy as _np
                ip = tmp / f"{slug}_impulse.wav"
                if ip.exists():
                    sr, x = _wf.read(str(ip))
                    x = x.astype(_np.float64) / _np.iinfo(x.dtype).max if x.dtype.kind == 'i' else x.astype(_np.float64)
                    if x.ndim > 1:
                        x = x.mean(axis=1)
                    sos = _sg.butter(4, [800, 1200], 'band', fs=sr, output='sos')
                    band = _sg.sosfiltfilt(sos, x)
                    imp_band_db = 20 * _np.log10(max(_np.sqrt(_np.mean(band**2)), 1e-12))
                    # Anchor impulse band ≈ -53 dB. Penalize deltas > +10 dB.
                    overshoot = max(0.0, imp_band_db - (-43.0))
                    loss += overshoot * 4.0
                    measured["impulse_1k_band_db"] = imp_band_db
            except Exception:
                pass

            # snare RMS (weight 1.5)
            snare = tmp / f"{slug}_snare.wav"
            if snare.exists():
                dv_sn = _full_rms_db(str(snare))
                measured["snare_rms"] = dv_sn
                err = abs(dv_sn - anchor["snare_rms"]) / 2.0
                loss += err * 1.5

            # Per-band Hilbert mod-freq (weight 3) — penalize deviations
            # from VVV's structural modal-beat pattern. Without this term
            # the optimizer freely changes damping mults that shift mode
            # resonance and re-open the mod gates Phase β closed.
            mod_targets = MOD_TARGETS.get(args.preset, [])
            for lo, hi, target in mod_targets:
                m = _tail_mod_peak_freq(str(nb), 1.0, 3.0, lo, hi)
                if m is None:
                    loss += 0.5 * 3.0
                    continue
                err = abs(m - target) / target
                loss += err * 3.0

            # EDT per band (weight 1.5)
            for lo, hi in [(20, 100), (100, 250), (500, 2000), (2000, 8000)]:
                anchor_edt = anchor["edt"].get((lo, hi))
                if anchor_edt is None or anchor_edt <= 0:
                    continue
                dv_e = _edt_band(str(nb), lo, hi)
                if dv_e is None:
                    loss += 0.5
                    continue
                err = abs(dv_e - anchor_edt) / max(anchor_edt, 0.01)
                loss += err * 1.5

            for k, v in measured.items():
                trial.set_user_attr(k, v)
            return loss
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    study = optuna.create_study(direction="minimize",
                                 sampler=optuna.samplers.TPESampler(seed=42))
    study.optimize(objective, n_trials=args.trials, n_jobs=args.workers)

    best = study.best_trial
    print("\n========== BEST ==========")
    print(f"Trial {best.number}  loss={best.value:.4f}")
    for k, v in best.params.items():
        print(f"  {k:14s} = {v:.4f}")
    print("\nT60 per band:")
    for lo, hi, c in T60_BANDS:
        m = best.user_attrs.get(f"t60_{c}")
        if m is None:
            continue
        t = t60_target[c]
        e = abs(m - t) / t * 100.0
        print(f"  {c:>5} Hz  DV={m:.2f}s VVV={t:.2f}s err={e:5.1f}%")
    print(f"\nsine1k RMS DV={best.user_attrs.get('sine1k_rms', 0):.2f} dB "
          f"VVV={anchor['sine1k_rms']:.2f} dB")
    if args.out_json:
        import json
        Path(args.out_json).write_text(json.dumps({
            "trial": best.number,
            "loss": best.value,
            "params": dict(best.params),
            "measurements": dict(best.user_attrs),
            "anchor": {
                "sine1k_rms": anchor["sine1k_rms"],
                "snare_rms":  anchor["snare_rms"],
            },
        }, indent=2))
        print(f"\nWrote {args.out_json}")


if __name__ == "__main__":
    main()
