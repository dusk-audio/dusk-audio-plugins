#!/usr/bin/env python3
"""noise_probe.py — Noise-floor anatomy A/B: TapeMachine 2 vs UAD, SHIPPED state.

Every prior gate forced noise OFF. This measures the noise the user actually hears
when a preset ships with Noise Amount > 0 (Sunbaked 10, Analog Warmth 6, Old Tape 8)
and compares it to the UAD deck at its factory hiss/hum settings.

Three measurements (>=20 s renders so the low bands resolve):
  IDLE   — silence in. Overall level (dBFS), third-octave hiss spectrum, and the
           mains-hum line levels (50/60/100/120/180/240 Hz). UAD models a constant
           idle hiss+hum (~-82 dBFS); mine adds a800/classic idleNoise (pink+60 Hz hum).
  MODN   — 1 kHz tone at -24/-12/-6 dBFS in. Noise level BETWEEN the harmonics
           (median PSD at non-harmonic bins) vs signal level -> modulation-noise law.
           Mine's tape noise is partly signal-dependent; UAD's idle hiss is constant.
  Reports mine vs UAD side by side; the hum-frequency mismatch (60 Hz US vs the deck's
  NAB/CCIR-selected hum) and any hiss-tilt / level gap are the audible tells.

  python3 noise_probe.py                # 3 noisy audition presets (idle + modn)
  python3 noise_probe.py Sunbaked       # substring subset
  python3 noise_probe.py ref            # max-noise reference (shape, level-independent)
"""
import os, sys, tempfile, shutil, subprocess, itertools
import numpy as np
import soundfile as sf
from scipy.signal import welch
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from preset_validate import (parse_presets, decode_uad, UAD_JSON, mine_params,
                             ATR, STUDER, MINE, BIN, STIM)  # noqa: E402

OUT = os.path.join(HERE, "renders", "sto_noise")
_rc = itertools.count()
SR = 48000
DUR = 22.0

# ISO third-octave centres 20 Hz .. 20 kHz
THIRD_OCT = [25, 31.5, 40, 63, 100, 160, 250, 400, 630, 1000,
             1600, 2500, 4000, 6300, 10000, 16000]
HUM_LINES = [50, 60, 100, 120, 180, 240]
TONE_LEVELS_DB = [-24, -12, -6]


def check_pace():
    return os.path.isdir("/var/tmp/com.paceap.eden.licensed")


def _write(path, x):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if x.ndim == 1:
        x = np.column_stack([x, x])
    sf.write(path, x.astype(np.float32), SR)


def ensure_stimuli():
    sil = os.path.join(STIM, "sto_silence_22s.wav")
    if not os.path.exists(sil):
        _write(sil, np.zeros(int(SR * DUR)))
    for db in TONE_LEVELS_DB:
        p = os.path.join(STIM, f"sto_tone1k_{db}dB_22s.wav")
        if not os.path.exists(p):
            t = np.arange(int(SR * DUR)) / SR
            amp = 10 ** (db / 20.0)
            x = amp * np.sin(2 * np.pi * 1000.0 * t)
            fade = int(0.02 * SR)
            x[:fade] *= np.linspace(0, 1, fade)
            x[-fade:] *= np.linspace(1, 0, fade)
            _write(p, x)


def render(plugin, params, inp, tag, nparams=None):
    tmp = tempfile.mkdtemp()
    cmd = [BIN, "--au", plugin, "--input-wav", os.path.join(STIM, inp),
           "--slug", "s", "--output-dir", tmp, "--prerun-seconds", "2"]
    for k, v in params:
        cmd += ["--param", f"{k}={v}"]
    for k, v in (nparams or []):
        cmd += ["--nparam", f"{k}={v}"]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    stem = os.path.join(tmp, "s_stem.wav")
    if proc.returncode != 0 or not os.path.exists(stem):
        shutil.rmtree(tmp, ignore_errors=True)
        raise RuntimeError(f"render failed tag={tag} rc={proc.returncode}\n{proc.stderr[-600:]}")
    os.makedirs(OUT, exist_ok=True)
    dest = os.path.join(OUT, f"{tag}_{os.getpid()}_{next(_rc)}.wav")
    shutil.move(stem, dest)
    shutil.rmtree(tmp, ignore_errors=True)
    return dest


def load_mono(path):
    x, sr = sf.read(path)
    if x.ndim > 1:
        x = x.mean(axis=1)
    return x.astype(np.float64), sr


def steady_region(x, sr):
    """For a noise/tone render, drop the leading/trailing 0.5 s and any silent pad.
    Idle noise is stationary so we keep the largest nonzero run's inner 90%."""
    a = np.abs(x)
    nz = a > (a.max() * 1e-4 if a.max() > 0 else 0)
    idx = np.flatnonzero(nz)
    if idx.size < sr:
        return x  # very low level; use whole file
    s, e = idx[0], idx[-1]
    L = e - s
    return x[s + int(0.05 * L): s + int(0.95 * L)]


def overall_dbfs(x):
    r = np.sqrt(np.mean(x ** 2))
    return 20 * np.log10(r) if r > 0 else -200.0


def third_oct(x, sr):
    f, pxx = welch(x, fs=sr, nperseg=16384, noverlap=8192)
    out = {}
    for fc in THIRD_OCT:
        lo, hi = fc / 2 ** (1 / 6), fc * 2 ** (1 / 6)
        m = (f >= lo) & (f < hi)
        p = np.sum(pxx[m]) if np.any(m) else 0.0
        out[fc] = 10 * np.log10(p) if p > 0 else -200.0
    return out


def hum_lines(x, sr):
    f, pxx = welch(x, fs=sr, nperseg=32768, noverlap=16384)
    out = {}
    for fl in HUM_LINES:
        m = np.abs(f - fl) < 3.0
        p = np.max(pxx[m]) if np.any(m) else 0.0
        out[fl] = 10 * np.log10(p) if p > 0 else -200.0
    return out


def modn_noise(x, sr, carrier=1000.0):
    """Median PSD at NON-harmonic bins (the modulation-noise skirt) in dB, plus the
    carrier level, so noise-below-signal can be reported."""
    f, pxx = welch(x, fs=sr, nperseg=16384, noverlap=8192)
    harm = np.zeros_like(f, dtype=bool)
    for k in range(1, 25):
        harm |= np.abs(f - k * carrier) < 25.0
    band = (f > 200) & (f < 8000) & ~harm
    noise = np.median(pxx[band]) if np.any(band) else 0.0
    cm = np.abs(f - carrier) < 15.0
    car = np.max(pxx[cm]) if np.any(cm) else 0.0
    ndb = 10 * np.log10(noise) if noise > 0 else -200.0
    cdb = 10 * np.log10(car) if car > 0 else -200.0
    return ndb, cdb


def mine_noise_params(p):
    base = [kv for kv in mine_params(p)
            if kv[0] not in ("Noise Amount", "Noise Enabled", "Wow", "Flutter")]
    base += [("Noise Amount", p["noise"] / 100.0), ("Noise Enabled", 1),
             ("Wow", 0.0), ("Flutter", 0.0)]
    return base


def uad_noise_nparams(machine, vec):
    """Factory vector with W&F forced OFF but hiss/hum LEFT at factory (shipped noise)."""
    v = dict(vec)
    if machine == 1:
        v["Wow & Flutter"] = 0.0
    else:
        pass  # Studer has no W&F param
    return [(k, val) for k, val in v.items()]


def probe_idle(p):
    machine = p["machine"]
    uad_bin = ATR if machine == 1 else STUDER
    vec, _ = decode_uad(machine, UAD_JSON[p["name"]])
    mp = mine_noise_params(p)
    unp = uad_noise_nparams(machine, vec)
    m = render(MINE, mp, "sto_silence_22s.wav", "nz_m")
    u = render(uad_bin, [], "sto_silence_22s.wav", "nz_u", nparams=unp)
    mx, _ = load_mono(m); ux, _ = load_mono(u)
    mx, ux = steady_region(mx, SR), steady_region(ux, SR)
    if machine == 1:
        uad_noise = vec.get("Hiss & Hum")
    else:
        uad_noise = vec.get("Noise")
    return dict(name=p["name"], machine=machine, noise=p["noise"], uad_noise=uad_noise,
                m_dbfs=overall_dbfs(mx), u_dbfs=overall_dbfs(ux),
                m_to=third_oct(mx, SR), u_to=third_oct(ux, SR),
                m_hum=hum_lines(mx, SR), u_hum=hum_lines(ux, SR))


def probe_modn(p):
    machine = p["machine"]
    uad_bin = ATR if machine == 1 else STUDER
    vec, _ = decode_uad(machine, UAD_JSON[p["name"]])
    mp = mine_noise_params(p)
    unp = uad_noise_nparams(machine, vec)
    rows = []
    for db in TONE_LEVELS_DB:
        stim = f"sto_tone1k_{db}dB_22s.wav"
        m = render(MINE, mp, stim, "mn_m")
        u = render(uad_bin, [], stim, "mn_u", nparams=unp)
        mx, _ = load_mono(m); ux, _ = load_mono(u)
        mx, ux = steady_region(mx, SR), steady_region(ux, SR)
        mn, mc = modn_noise(mx, SR)
        un, uc = modn_noise(ux, SR)
        rows.append(dict(db=db, m_noise=mn, m_car=mc, u_noise=un, u_car=uc))
    return dict(name=p["name"], machine=machine, rows=rows)


def print_idle(r):
    deck = "ATR" if r["machine"] == 1 else "Studer"
    print(f"\n### {r['name']:20s} [{deck}]  mine NoiseAmt {r['noise']:.0f}%  "
          f"UAD noise param {r['uad_noise']}")
    print(f"  overall idle level:  MINE {r['m_dbfs']:7.1f} dBFS   UAD {r['u_dbfs']:7.1f} dBFS "
          f"  (delta {r['m_dbfs']-r['u_dbfs']:+.1f})")
    print(f"  {'band(Hz)':>9} {'MINE dB':>9} {'UAD dB':>9}  {'delta':>6}")
    for fc in THIRD_OCT:
        md, ud = r["m_to"][fc], r["u_to"][fc]
        print(f"  {fc:>9} {md:9.1f} {ud:9.1f}  {md-ud:+6.1f}")
    print(f"  hum lines (dB):")
    for fl in HUM_LINES:
        print(f"    {fl:4d}Hz  MINE {r['m_hum'][fl]:7.1f}   UAD {r['u_hum'][fl]:7.1f}   "
              f"delta {r['m_hum'][fl]-r['u_hum'][fl]:+.1f}")


def print_modn(r):
    deck = "ATR" if r["machine"] == 1 else "Studer"
    print(f"\n### {r['name']:20s} [{deck}]  modulation noise (noise-between-harmonics)")
    print(f"  {'tone in':>8} {'MINE noise':>11} {'MINE N/C':>9} {'UAD noise':>11} {'UAD N/C':>9}")
    for row in r["rows"]:
        m_nc = row["m_noise"] - row["m_car"]
        u_nc = row["u_noise"] - row["u_car"]
        print(f"  {row['db']:6d}dB {row['m_noise']:10.1f}dB {m_nc:8.1f}dB "
              f"{row['u_noise']:10.1f}dB {u_nc:8.1f}dB")


DEFAULT = ["Sunbaked", "Analog Warmth", "Old Tape"]


def main():
    ensure_stimuli()
    if not check_pace():
        print("!! PACE down — UAD renders bypass; aborting."); return
    args = sys.argv[1:]
    if args and args[0] == "ref":
        ref_mode(); return
    subs = [s.strip().lower() for s in (args or DEFAULT)]
    presets = [p for p in parse_presets()
               if any(s in p["name"].lower() for s in subs) and p["noise"] > 0]
    print(f"noise probe: {len(presets)} preset(s), {DUR:.0f}s renders, W&F off\n")
    print("=" * 60 + "\nIDLE (silence in)\n" + "=" * 60)
    with ThreadPoolExecutor(max_workers=3) as ex:
        idle = list(ex.map(lambda p: _safe(probe_idle, p), presets))
    for r in [r for r in idle if r]:
        print_idle(r)
    print("\n" + "=" * 60 + "\nMODULATION NOISE (tone in)\n" + "=" * 60)
    with ThreadPoolExecutor(max_workers=3) as ex:
        modn = list(ex.map(lambda p: _safe(probe_modn, p), presets))
    for r in [r for r in modn if r]:
        print_modn(r)


def ref_mode():
    """Max-noise shape: mine NoiseAmount=100 vs UAD hiss/hum max, 456/NAB/+6/15IPS."""
    ensure_stimuli()
    atr_np = [("IPS", 2/3), ("Tape Type", 1/3), ("Cal Level", 1/3), ("Emphasis EQ", 0.0),
              ("Path Select", 1/3), ("Auto Cal", 1.0), ("Wow & Flutter", 0.0), ("Hiss & Hum", 1.0)]
    stu_np = [("IPS", 0.5), ("Tape Type", 1/3), ("Cal Level", 1/3), ("Emphasis EQ", 0.0),
              ("Path Select", 1.0), ("Auto Cal", 1.0), ("Noise", 1.0), ("Hum Noise", 1.0), ("Hiss Noise", 1.0)]
    mine_c = [("Tape Machine", 1), ("Tape Speed", 1), ("Tape Type", 0), ("EQ Standard", 0),
              ("Calibration", 1), ("Signal Path", 0), ("Auto Calibration", 1), ("Auto Compensation", 1),
              ("Input Gain", 0.5), ("Oversampling", 1), ("Noise Amount", 1.0), ("Wow", 0.0), ("Flutter", 0.0)]
    mine_s = [(k, v) for k, v in mine_c]; mine_s[0] = ("Tape Machine", 0)
    for lbl, plugin, params, npar in [
        ("mine Classic102 N100", MINE, mine_c, None),
        ("UAD ATR H&H max", ATR, [], atr_np),
        ("mine Swiss800 N100", MINE, mine_s, None),
        ("UAD Studer noise max", STUDER, [], stu_np)]:
        o = render(plugin, params, "sto_silence_22s.wav", "ref", nparams=npar)
        x, _ = load_mono(o); x = steady_region(x, SR)
        to = third_oct(x, SR)
        hz = hum_lines(x, SR)
        tilt = to[10000] - to[100]
        print(f"{lbl:24s} level {overall_dbfs(x):7.1f}dBFS  100Hz {to[100]:6.1f} "
              f"1k {to[1000]:6.1f} 10k {to[10000]:6.1f} tilt(10k-100) {tilt:+.1f}  "
              f"60Hz {hz[60]:.0f} 120 {hz[120]:.0f}")


def _safe(fn, p):
    try:
        return fn(p)
    except Exception as e:
        print(f"!! {p['name']}: {e}")
        return None


if __name__ == "__main__":
    main()
