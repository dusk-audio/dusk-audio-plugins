#!/usr/bin/env python3
"""wf_probe.py — Wow & Flutter A/B probe: TapeMachine 2 vs the UAD source preset.

For each factory preset, render a 1 kHz sine (-12 dBFS, 12 s) through:
  * MINE  at the preset's full settings, NOISE OFF, W&F ON at the preset wow/flutter.
  * UAD   at the decoded factory chunk (like preset_validate.py) but W&F LEFT ON
          (only hiss/hum forced off so the envelope isolates W&F).

Two metrics per render (the pulsing calibration this probe exists for):
  FMdev Hz   — robust (MAD-based) instantaneous-frequency deviation of the tone.
               Reuses wow_flutter.measure_wow_flutter (validated demod that rejects
               the ~27% spurious glitch samples plain RMS is wrecked by). FM dev in Hz
               = robust_pct/100 * detected_carrier. This is the DEPTH knob target
               (mine must ~= UAD per preset).
  AMper      — autocorrelation peak of the amplitude envelope (the "pulsing" metric).
               Pure-sine modulation -> high (~0.8); random tape wobble -> low (~0.15).
               Mine must stay ~= UAD (<= ~0.2 on heavy-W&F presets).

CRITICAL: the render binary pads the output ~3x with SILENCE, so analyse the loud
sustained region (largest run above 0.1*peak, then its 30-70% slice) — never a fixed
middle slice (that reads the silent tail). PACE must be up for the UAD renders; if a
UAD render comes back a pristine sine (FMdev ~0 on a W&F-on preset) PACE is down and
the row is flagged, not faked.

  python3 wf_probe.py            # all presets with nonzero wow/flutter
  python3 wf_probe.py Sunbaked   # subset by name substring (comma-separated ok)
"""
import os, sys, tempfile, shutil
import numpy as np
import soundfile as sf
from scipy.signal import butter, sosfiltfilt, hilbert
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
# reuse the validated harness plumbing (preset parse, UAD chunk decode, renderer)
from preset_validate import (parse_presets, decode_uad, UAD_JSON, render, mine_params,
                             ATR, STUDER, MINE)  # noqa: E402
from wow_flutter import measure_wow_flutter  # noqa: E402

STIM_TONE = "tone1k_long.wav"   # 12 s, 1 kHz, -12 dBFS (auto-generated; stimuli/ is git-ignored)
CARRIER = 1000.0


def ensure_tone():
    """(Re)create the 1 kHz probe tone if missing — stimuli/ is git-ignored."""
    path = os.path.join(HERE, "stimuli", STIM_TONE)
    if os.path.exists(path):
        return
    sr, dur = 48000, 12.0
    t = np.arange(int(sr * dur)) / sr
    x = 0.2512 * np.sin(2.0 * np.pi * CARRIER * t)   # -12 dBFS
    fade = int(0.02 * sr)
    x[:fade] *= np.linspace(0, 1, fade)
    x[-fade:] *= np.linspace(1, 0, fade)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    sf.write(path, np.column_stack([x, x]).astype(np.float32), sr)

# pass/fail gate: mine FMdev within tol of UAD, and periodicity close to UAD.
# The UAD reference is TINY (ATR max W&F ~0.2 Hz std at 1 kHz; Studer has no W&F param
# -> ~0.01 Hz baseline), so tolerances are in tenths of a Hz.
FM_TOL_HZ = 0.10       # abs Hz tolerance on FM deviation
FM_TOL_REL = 0.40      # OR within 40% (whichever is looser)
PER_TOL = 0.20         # |mine - UAD| periodicity tolerance (the "pulsing" gate)
PER_HEAVY_ABS = 0.30   # heavy presets must also be under this absolute periodicity


def check_pace():
    return os.path.isdir("/var/tmp/com.paceap.eden.licensed")


def loud_slice(path, thresh=0.1):
    """Return (mono slice, sr) of the 30-70% inner part of the largest loud run
    (samples above thresh*peak) — skips the render binary's silent padding."""
    x, sr = sf.read(path)
    if x.ndim > 1:
        x = x.mean(axis=1)
    x = x.astype(np.float64)
    peak = np.max(np.abs(x))
    if peak <= 0:
        raise ValueError(f"{path}: silent render")
    # smoothed envelope (moving-average of |x| over ~20 ms) so the threshold tracks
    # the tone vs the silent tail, NOT every sine zero-crossing (a raw |x|>thresh run
    # is only a half-cycle long).
    win = max(1, int(0.02 * sr))
    env = np.convolve(np.abs(x), np.ones(win) / win, mode="same")
    loud = env > thresh * np.max(env)
    idx = np.flatnonzero(loud)
    if idx.size == 0:
        raise ValueError(f"{path}: no loud region")
    splits = np.flatnonzero(np.diff(idx) > 1)
    starts = np.r_[idx[0], idx[splits + 1]]
    ends = np.r_[idx[splits], idx[-1]]
    k = int(np.argmax(ends - starts))
    s, e = starts[k], ends[k]
    L = e - s
    a, b = s + int(0.30 * L), s + int(0.70 * L)
    return x[a:b], sr


def am_periodicity(seg, sr, carrier=CARRIER):
    """Autocorrelation peak of the tone's amplitude envelope (pulsing metric).
    Bandpass the carrier, take the analytic envelope, remove DC, normalise, then the
    max normalised autocorrelation over modulation lags (0.05-4 s) is the periodicity."""
    lo = max(carrier * 0.90, 20.0)
    hi = min(carrier * 1.10, sr / 2.0 * 0.99)
    sos = butter(4, [lo, hi], btype="band", fs=sr, output="sos")
    car = sosfiltfilt(sos, seg)
    env = np.abs(hilbert(car))
    env = env - np.mean(env)
    std = np.std(env)
    if std < 1e-12:
        return 0.0
    env = env / std
    n = len(env)
    ac = np.correlate(env, env, mode="full")[n - 1:]
    ac = ac / ac[0]
    lo_lag = int(0.05 * sr)
    hi_lag = min(int(4.0 * sr), n // 2)
    if hi_lag <= lo_lag:
        return 0.0
    return float(np.max(ac[lo_lag:hi_lag]))


def wf_metrics(path):
    seg, sr = loud_slice(path)
    per = am_periodicity(seg, sr)
    tmp = tempfile.mkdtemp()
    try:
        wav = os.path.join(tmp, "slice.wav")
        sf.write(wav, seg.astype(np.float32), sr)
        m = measure_wow_flutter(wav, carrier_hz=CARRIER, trim_sec=0.05)
        fmdev = m["pitch_dev_robust_pct"] / 100.0 * m["carrier_detected_hz"]
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return fmdev, per


def mine_wf_params(p):
    """Preset render params with NOISE off but W&F ON at the preset wow/flutter.
    Start from preset_validate.mine_params (W&F off) and flip Wow/Flutter back on
    (0..100 float params -> normalised /100)."""
    base = [kv for kv in mine_params(p) if kv[0] not in ("Wow", "Flutter")]
    base += [("Wow", p["wow"] / 100.0), ("Flutter", p["flutter"] / 100.0)]
    return base


def uad_wf_nparams(machine, vec):
    """UAD factory vector with hiss/hum forced OFF but W&F LEFT ON (factory value)."""
    v = dict(vec)
    if machine == 1:
        v["Hiss & Hum"] = 0.0
    else:
        v["Noise"] = 0.0
    return [(k, val) for k, val in v.items()]


def probe(p):
    machine = p["machine"]
    uad_bin = ATR if machine == 1 else STUDER
    vec, uname = decode_uad(machine, UAD_JSON[p["name"]])
    uad_wf = vec.get("Wow & Flutter" if machine == 1 else "Noise")  # info only

    mp = mine_wf_params(p)
    unp = uad_wf_nparams(machine, vec)

    m_out = render(MINE, mp, STIM_TONE, "wf_m")
    u_out = render(uad_bin, [], STIM_TONE, "wf_u", nparams=unp)

    m_fm, m_per = wf_metrics(m_out)
    u_fm, u_per = wf_metrics(u_out)
    # UAD W&F state: ATR has a Wow&Flutter TOGGLE (0/1); Studer A800 has NO W&F param.
    uwf = ("off" if uad_wf is None else ("on" if uad_wf > 0.5 else "OFF")) if machine == 1 else "n/a"
    return dict(name=p["name"], machine=machine, wow=p["wow"], flutter=p["flutter"],
                m_fm=m_fm, u_fm=u_fm, m_per=m_per, u_per=u_per, uwf=uwf)


def verdict(r):
    heavy = r["wow"] >= 8 or r["flutter"] >= 6
    fm_ok = (abs(r["m_fm"] - r["u_fm"]) <= FM_TOL_HZ or
             abs(r["m_fm"] - r["u_fm"]) <= FM_TOL_REL * max(r["u_fm"], 1e-6))
    per_ok = abs(r["m_per"] - r["u_per"]) <= PER_TOL
    if heavy:
        per_ok = per_ok and (r["m_per"] <= PER_HEAVY_ABS)
    return "PASS" if (fm_ok and per_ok) else "fail"


# ---------------------------------------------------------------------------
# SPEED SWEEP — measure the UAD W&F depth-vs-tape-speed curve (the target for the
# per-speed depth scaling). Fixed config 456/NAB/+6/Repro, W&F ON, hiss/hum off;
# only IPS varies. ATR has a real Wow&Flutter toggle (measure all 4 speeds); the
# Studer A800 has NO W&F param (measure whether its baked-in wobble varies — likely
# ~0). Mine is rendered at a fixed wow/flutter (7/3, the shipped default) so its
# CURRENT flat-vs-speed behaviour is visible next to the UAD's speed curve.
#   python3 wf_probe.py speed
SPEED_IPS = ["3.75", "7.5", "15", "30"]
# ATR IPS as a normalised --nparam: 0=3.75, .333=7.5, .667=15, 1=30 (avoids the
# "3.75 IPS" --param label misparse). Tape 456=.333, cal +6=.333, NAB=0, Repro=.333.
ATR_IPS_N = {"3.75": 0.0, "7.5": 1.0 / 3.0, "15": 2.0 / 3.0, "30": 1.0}
# Studer A800 has 3 speeds only; IPS nparam 0=7.5, .5=15, 1=30. Repro=1.0.
STU_IPS_N = {"7.5": 0.0, "15": 0.5, "30": 1.0}
# Mine's tape-speed choice index: 0=7.5, 1=15, 2=30, 3=3.75.
MINE_SPEED_IDX = {"7.5": 0, "15": 1, "30": 2, "3.75": 3}
MINE_WOW, MINE_FLUT = 7.0, 3.0    # shipped default knob values


def _atr_speed_nparams(ips):
    return [("IPS", ATR_IPS_N[ips]), ("Tape Type", 1.0 / 3.0), ("Cal Level", 1.0 / 3.0),
            ("Emphasis EQ", 0.0), ("Path Select", 1.0 / 3.0), ("Auto Cal", 1.0),
            ("Wow & Flutter", 1.0), ("Hiss & Hum", 0.0)]


def _stu_speed_nparams(ips):
    return [("IPS", STU_IPS_N[ips]), ("Tape Type", 1.0 / 3.0), ("Cal Level", 1.0 / 3.0),
            ("Emphasis EQ", 0.0), ("Path Select", 1.0), ("Auto Cal", 1.0),
            ("Noise", 0.0), ("Hum Noise", 0.0), ("Hiss Noise", 0.0)]


def _mine_speed_params(ips, machine):
    """Mine at fixed 456/NAB/+6/Repro, given machine+speed, W&F at the default knobs."""
    return [("Tape Machine", machine), ("Tape Speed", MINE_SPEED_IDX[ips]),
            ("Tape Type", 0), ("EQ Standard", 0), ("Calibration", 1),
            ("Signal Path", 0), ("Auto Calibration", 1), ("Auto Compensation", 1),
            ("Input Gain", 0.5), ("Oversampling", 1), ("Noise Amount", 0.0),
            ("Noise Enabled", 0), ("Wow", MINE_WOW / 100.0), ("Flutter", MINE_FLUT / 100.0)]


def _speed_row(machine, ips):
    if machine == 1:
        u_out = render(ATR, [], STIM_TONE, "sp_u", nparams=_atr_speed_nparams(ips))
    else:
        u_out = render(STUDER, [], STIM_TONE, "sp_u", nparams=_stu_speed_nparams(ips))
    m_out = render(MINE, _mine_speed_params(ips, machine), STIM_TONE, "sp_m")
    u_fm, u_per = wf_metrics(u_out)
    m_fm, m_per = wf_metrics(m_out)
    return dict(machine=machine, ips=ips, u_fm=u_fm, u_per=u_per, m_fm=m_fm, m_per=m_per)


def speed_sweep():
    ensure_tone()
    if not check_pace():
        print("!! PACE down — UAD renders will bypass; aborting.\n"); return
    jobs = [(1, ips) for ips in SPEED_IPS] + [(0, ips) for ips in ["7.5", "15", "30"]]
    with ThreadPoolExecutor(max_workers=6) as ex:
        rows = [r for r in ex.map(lambda j: _safe_sp(_speed_row, *j), jobs) if r]

    for machine, deck in [(1, "ATR-102 (Wow&Flutter ON)"), (0, "Studer A800 (no W&F param)")]:
        mrows = sorted([r for r in rows if r["machine"] == machine],
                       key=lambda x: float(x["ips"]))
        print(f"\n=== {deck} — 1 kHz, 456/NAB/+6/Repro, IPS sweep ===")
        print(f"{'IPS':>6}  {'UAD FMdev':>10} {'UAD per':>8}   "
              f"{'mine FMdev':>10} {'mine per':>8}   {'norm(15=1)':>10}")
        ref = next((r["u_fm"] for r in mrows if r["ips"] == "15"), None)
        for r in mrows:
            norm = (r["u_fm"] / ref) if ref else float("nan")
            print(f"{r['ips']:>6}  {r['u_fm']:9.4f}H {r['u_per']:8.2f}   "
                  f"{r['m_fm']:9.4f}H {r['m_per']:8.2f}   {norm:10.3f}")


def _safe_sp(fn, *a):
    try:
        return fn(*a)
    except Exception as e:
        print(f"!! speed {a}: {e}")
        return None


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "speed":
        speed_sweep(); return
    ensure_tone()
    filt = sys.argv[1] if len(sys.argv) > 1 else ""
    subs = [s.strip().lower() for s in filt.split(",") if s.strip()]
    presets = [p for p in parse_presets()
               if (p["wow"] > 0 or p["flutter"] > 0)
               and (not subs or any(s in p["name"].lower() for s in subs))]
    if not check_pace():
        print("!! PACE socket /var/tmp/com.paceap.eden.licensed MISSING — UAD renders "
              "will BYPASS (pristine sine). Start PACE before trusting UAD columns.\n")
    print(f"probing {len(presets)} preset(s)  (1 kHz tone, W&F on, noise off)\n")

    with ThreadPoolExecutor(max_workers=6) as ex:
        rows = list(ex.map(lambda p: _safe(probe, p), presets))
    rows = [r for r in rows if r]

    hdr = (f"{'preset':22} {'wow/flut':>8} {'UADwf':>6}  {'mFMdev':>7} {'uFMdev':>7}"
           f"   {'mPer':>5} {'uPer':>5}  {'ok':>5}")
    print(hdr)
    print("-" * len(hdr))
    npass = 0
    for r in sorted(rows, key=lambda x: (x["machine"], x["name"])):
        v = verdict(r)
        # PACE-down tell: an ATR preset with the W&F toggle ON must wobble ABOVE the
        # ~0.014 Hz demod noise floor; a flat read = the UAD bypassed. (At 15/30 IPS the
        # ATR's W&F is genuinely subtle ~0.02-0.04 Hz, so only a truly flat read flags.)
        # Studer (n/a) and W&F-OFF presets are flat by design and never flag. Apply this
        # override BEFORE counting the pass so a PACE-bypassed row is never tallied as PASS.
        if r["machine"] == 1 and r["uwf"] == "on" and r["u_fm"] < 0.018:
            v = "PACE?"
        npass += (v == "PASS")
        print(f"{r['name']:22} {r['wow']:3.0f}/{r['flutter']:<4.0f} {r['uwf']:>6}  "
              f"{r['m_fm']:6.3f}H {r['u_fm']:6.3f}H   "
              f"{r['m_per']:5.2f} {r['u_per']:5.2f}  {v:>5}")
    print(f"\nPASS {npass}/{len(rows)}   (FMdev tol +-{FM_TOL_HZ}Hz or {FM_TOL_REL:.0%}; "
          f"periodicity tol +-{PER_TOL}, heavy<= {PER_HEAVY_ABS})")


def _safe(fn, p):
    try:
        return fn(p)
    except Exception as e:
        print(f"!! {p['name']}: {e}")
        return None


if __name__ == "__main__":
    main()
