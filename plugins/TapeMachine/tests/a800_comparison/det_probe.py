#!/usr/bin/env python3
"""det_probe.py — DETERMINISTIC (non-magnitude-FR) differences: TapeMachine 2 vs UAD.

All prior gates compared MAGNITUDE spectra. This probe covers everything
deterministic that is NOT magnitude-FR, with W&F + hiss/hum OFF on both sides:

  mode phase     group-delay / excess-phase vs freq (low-level sweep deconvolution,
                 relative to 1 kHz so plugin latency cancels). Allpass-like wiggles
                 audible as transient smear.
  mode transient kick + snare bursts at -18/-12/-6/-3 dBFS; per-hit amplitude
                 envelope (attack slope, peak ratio, 5/20/50 ms decay) + intra-hit
                 spectral-tilt trajectory (0-5 / 5-20 / 20-80 ms) — the level-comp's
                 dynamic-EQ "pumping" signature vs UAD's intrinsically flat hit.
  mode loudness  pink noise -18 dBFS through all 20 presets; integrated loud-region
                 RMS + BS.1770 K-weighted LUFS, mine vs UAD; >0.5 dB = audible.
  mode stereo    L/R identity (mine mono-linked core?), crosstalk bleed (L-only in),
                 mid/side spectral diff on a stereo drum loop.

Renders go to renders/det_<mode>/ (unique prefix — another agent renders concurrently).

  python3 det_probe.py phase [substr]
  python3 det_probe.py transient
  python3 det_probe.py loudness [substr]
  python3 det_probe.py stereo
  python3 det_probe.py all
"""
import os, sys, json, tempfile, shutil, subprocess, itertools
from concurrent.futures import ThreadPoolExecutor
import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from preset_validate import (parse_presets, decode_uad, uad_nparams, mine_params,
                             UAD_JSON, ATR, STUDER, MINE, BIN)  # noqa: E402
from level_probe import ref_mine, ref_uad  # noqa: E402

SR = 48000
_rc = itertools.count()


# ---------------------------------------------------------------- render (stereo)
def render(plugin, params, inp, mode, tag, outdir):
    """Render, preserving stereo. params: list of (name,value). mode param|nparam."""
    tmp = tempfile.mkdtemp()
    cmd = [BIN, "--au", plugin, "--input-wav", inp, "--slug", "s",
           "--output-dir", tmp, "--prerun-seconds", "2"]
    flag = "--param" if mode == "param" else "--nparam"
    for k, v in params:
        cmd += [flag, f"{k}={v}"]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    stem = os.path.join(tmp, "s_stem.wav")
    if proc.returncode != 0 or not os.path.exists(stem):
        shutil.rmtree(tmp, ignore_errors=True)
        raise RuntimeError(f"render {tag} rc={proc.returncode}\n{proc.stderr[-600:]}")
    os.makedirs(outdir, exist_ok=True)
    dest = os.path.join(outdir, f"{tag}_{os.getpid()}_{next(_rc)}.wav")
    shutil.move(stem, dest)
    shutil.rmtree(tmp, ignore_errors=True)
    return dest


def load(p, mono=True):
    x, _ = sf.read(p)
    if mono:
        return (x.mean(1) if x.ndim > 1 else x).astype(np.float64)
    if x.ndim == 1:
        x = np.column_stack([x, x])
    return x.astype(np.float64)


def align_lag(a, b):
    """integer lag to roll b onto a (np.roll(b,lag))."""
    n = min(len(a), len(b))
    s0, s1 = int(n * 0.1), int(n * 0.9)
    aw, bw = a[s0:s1], b[s0:s1]
    L = len(aw)
    nf = 1 << int(np.ceil(np.log2(2 * L)))
    xc = np.fft.irfft(np.fft.rfft(aw, nf) * np.conj(np.fft.rfft(bw, nf)), nf)
    lag = int(np.argmax(xc))
    return lag - nf if lag > nf // 2 else lag


def loud_region(x):
    thr = np.max(np.abs(x)) * 1e-3
    nz = np.where(np.abs(x) > thr)[0]
    return (nz[0], nz[-1] + 1) if len(nz) else (0, len(x))


# ============================================================ MODE: phase / GD
def make_sweep(seconds=6.0, f0=20.0, f1=20000.0, level_db=-24.0):
    n = int(seconds * SR)
    t = np.arange(n) / SR
    k = np.log(f1 / f0)
    ph = 2 * np.pi * f0 * seconds / k * (np.exp(t / seconds * k) - 1.0)
    x = np.sin(ph)
    ramp = int(0.05 * SR)
    x[:ramp] *= np.sin(np.linspace(0, np.pi / 2, ramp)) ** 2
    x[-ramp:] *= np.sin(np.linspace(np.pi / 2, 0, ramp)) ** 2
    x *= 10 ** (level_db / 20.0)
    return x.astype(np.float32)


def group_delay_curve(out_m, in_m, fgrid):
    """Relative group delay (ms, ref 1 kHz) via sweep deconvolution.
    Latency-align first (integer), then GD = -dphi/domega, then subtract GD@1k so
    the pure-delay (linear-phase) term cancels -> only dispersion remains."""
    lag = align_lag(in_m, out_m)
    o = np.roll(out_m, lag)
    n = min(len(o), len(in_m))
    o, i = o[:n], in_m[:n]
    win = np.hanning(n)
    O = np.fft.rfft(o * win)
    I = np.fft.rfft(i * win)
    f = np.fft.rfftfreq(n, 1 / SR)
    H = O / (I + 1e-12 * np.max(np.abs(I)))
    phase = np.unwrap(np.angle(H))
    # group delay = -dphase/domega ; domega = 2pi df
    gd = -np.gradient(phase, 2 * np.pi * f)  # seconds
    # fractional-octave smoothing onto fgrid
    out = []
    for fc in fgrid:
        lo, hi = fc / 2 ** (1 / 6), fc * 2 ** (1 / 6)
        band = (f >= lo) & (f < hi)
        out.append(np.median(gd[band]) if band.any() else np.nan)
    out = np.array(out) * 1000.0  # ms
    k1 = int(np.argmin(np.abs(np.array(fgrid) - 1000)))
    return out - out[k1]  # relative to 1 kHz


def mode_phase(filt):
    outdir = os.path.join(HERE, "renders", "det_phase")
    fgrid = [30, 50, 80, 125, 200, 315, 500, 800, 1000, 1600, 2500,
             4000, 6300, 10000, 15000]
    sweep = make_sweep()
    spath = os.path.join(outdir, "sweep_lo.wav")
    os.makedirs(outdir, exist_ok=True)
    sf.write(spath, np.column_stack([sweep, sweep]), SR, subtype="FLOAT")
    in_m = sweep.astype(np.float64)

    cases = []
    for mac, ubin, deck in [(0, STUDER, "A800"), (1, ATR, "ATR")]:
        cases.append((f"REF-{deck}", deck, ref_mine(mac), ref_uad(mac), ubin))
    ps = {p["name"]: p for p in parse_presets()}
    for name in ["Nice 456 Master", "Fat 456 Master", "Drum Bus"]:
        p = ps[name]
        vec, _ = decode_uad(p["machine"], UAD_JSON[name])
        ubin = ATR if p["machine"] == 1 else STUDER
        deck = "ATR" if p["machine"] == 1 else "A800"
        cases.append((name, deck, mine_params(p), uad_nparams(p["machine"], vec), ubin))
    cases = [c for c in cases if filt.lower() in c[0].lower()]

    print(f"phase/GD: {len(cases)} case(s); GD rel 1 kHz [ms]  (+ = mine later)\n")
    results = {}
    for name, deck, mp, up, ubin in cases:
        m = render(MINE, mp, spath, "param", f"m_{name.replace(' ','_')}", outdir)
        u = render(ubin, up, spath, "nparam", f"u_{name.replace(' ','_')}", outdir)
        gm = group_delay_curve(load(m), in_m, fgrid)
        gu = group_delay_curve(load(u), in_m, fgrid)
        d = gm - gu
        results[name] = dict(deck=deck, mine=gm.tolist(), uad=gu.tolist(), diff=d.tolist())
        print(f"### {name} ({deck})")
        print("  Hz   " + " ".join(f"{f:>6}" for f in fgrid))
        print("  mine " + " ".join(f"{v:+6.2f}" for v in gm))
        print("  UAD  " + " ".join(f"{v:+6.2f}" for v in gu))
        print("  diff " + " ".join(f"{v:+6.2f}" for v in d))
        lf = [d[i] for i, f in enumerate(fgrid) if f < 500]
        print(f"  LF(<500Hz) |diff| max {max(abs(x) for x in lf):.2f} ms  "
              f"worst|diff| {np.nanmax(np.abs(d)):.2f} ms\n")
    json.dump(dict(fgrid=fgrid, cases=results),
              open(os.path.join(outdir, "gd.json"), "w"), indent=1)


# ============================================================ MODE: transient
def synth_hits():
    """Train of kick + snare hits, each repeated at 4 peak levels. Returns
    (mono float32, list of (onset_sample, kind, level_db))."""
    levels = [-18, -12, -6, -3]
    gap = int(0.55 * SR)
    hits = []
    segs = []
    pos = int(0.1 * SR)

    def kick(m):
        tt = np.arange(m) / SR
        f = 55 + 75 * np.exp(-tt / 0.025)
        ph = 2 * np.pi * np.cumsum(f) / SR
        e = np.exp(-tt / 0.09)
        click = np.exp(-tt / 0.0015) * 0.4
        return (np.sin(ph) + click) * e

    def snare(m, rng):
        tt = np.arange(m) / SR
        e = np.exp(-tt / 0.05)
        body = np.sin(2 * np.pi * 190 * tt) * 0.4
        noise = rng.standard_normal(m) * 0.9
        return (noise + body) * e

    rng = np.random.default_rng(3)
    dur = int(0.32 * SR)
    total = pos + len(levels) * 2 * gap + dur
    x = np.zeros(total)
    for kind in ("kick", "snare"):
        for L in levels:
            w = kick(dur) if kind == "kick" else snare(dur, rng)
            w = w / (np.max(np.abs(w)) + 1e-12) * 10 ** (L / 20.0)
            x[pos:pos + dur] += w
            hits.append((pos, kind, L))
            pos += gap
    x[:64] *= np.linspace(0, 1, 64)
    return x.astype(np.float32), hits


def envelope(x, tau_ms=1.0):
    a = np.exp(-1.0 / (tau_ms * 1e-3 * SR))
    r = np.abs(x)
    y = np.empty_like(r)
    acc = 0.0
    for i in range(len(r)):
        acc = r[i] if r[i] > acc else a * acc + (1 - a) * r[i]
        y[i] = acc
    return y


def tilt(x):
    """HF/LF spectral tilt (dB): mean power 2-8 kHz minus mean power 100-800 Hz."""
    if len(x) < 64:
        return np.nan
    w = x * np.hanning(len(x))
    P = np.abs(np.fft.rfft(w)) ** 2
    f = np.fft.rfftfreq(len(w), 1 / SR)
    hi = (f >= 2000) & (f < 8000)
    lo = (f >= 100) & (f < 800)
    if not hi.any() or not lo.any():
        return np.nan
    return 10 * np.log10(np.mean(P[hi]) + 1e-20) - 10 * np.log10(np.mean(P[lo]) + 1e-20)


def mode_transient(filt):
    outdir = os.path.join(HERE, "renders", "det_transient")
    x, hits = synth_hits()
    spath = os.path.join(outdir, "hits.wav")
    os.makedirs(outdir, exist_ok=True)
    sf.write(spath, np.column_stack([x, x]), SR, subtype="FLOAT")
    in_m = x.astype(np.float64)

    cases = []
    for mac, ubin, deck in [(0, STUDER, "A800"), (1, ATR, "ATR")]:
        cases.append((f"REF-{deck}", deck, ref_mine(mac), ref_uad(mac), ubin))
    ps = {p["name"]: p for p in parse_presets()}
    for name in ["Drum Bus", "Fat 456 Master"]:
        p = ps[name]
        vec, _ = decode_uad(p["machine"], UAD_JSON[name])
        ubin = ATR if p["machine"] == 1 else STUDER
        deck = "ATR" if p["machine"] == 1 else "A800"
        cases.append((name, deck, mine_params(p), uad_nparams(p["machine"], vec), ubin))
    cases = [c for c in cases if filt.lower() in c[0].lower()]

    print(f"transient: {len(cases)} case(s), {len(hits)} hits\n")
    results = {}
    for name, deck, mp, up, ubin in cases:
        m = render(MINE, mp, spath, "param", f"m_{name.replace(' ','_')}", outdir)
        u = render(ubin, up, spath, "nparam", f"u_{name.replace(' ','_')}", outdir)
        mo, uo = load(m), load(u)
        mo = np.roll(mo, align_lag(in_m, mo))
        uo = np.roll(uo, align_lag(in_m, uo))
        # loudness match over loud region
        lo, hi = loud_region(in_m)
        km = np.sqrt(np.mean(mo[lo:hi] ** 2)); ku = np.sqrt(np.mean(uo[lo:hi] ** 2))
        if ku > 1e-9:
            uo = uo * (km / ku)
        em = envelope(mo); eu = envelope(uo)
        rows = []
        for onset, kind, L in hits:
            w = int(0.12 * SR)
            a, b = onset, onset + w
            pm = np.max(em[a:b]); pu = np.max(eu[a:b])
            peak_ratio = 20 * np.log10((pm + 1e-12) / (pu + 1e-12))
            # attack 10-90% time on mine vs uad (ms)
            def atk(e):
                seg = e[a:b]; pk = np.max(seg); i0 = np.argmax(seg)
                lo10 = np.where(seg[:i0 + 1] >= 0.1 * pk)[0]
                lo90 = np.where(seg[:i0 + 1] >= 0.9 * pk)[0]
                if len(lo10) and len(lo90):
                    return (lo90[0] - lo10[0]) / SR * 1000
                return np.nan
            atk_m, atk_u = atk(em), atk(eu)
            # decay: env at 5/20/50 ms after peak, dB rel peak
            def dec(e, ms):
                pk_i = a + np.argmax(e[a:b]); j = pk_i + int(ms * 1e-3 * SR)
                if j >= len(e):
                    return np.nan
                return 20 * np.log10((e[j] + 1e-12) / (np.max(e[a:b]) + 1e-12))
            dm = [dec(em, t) for t in (5, 20, 50)]
            du = [dec(eu, t) for t in (5, 20, 50)]
            # intra-hit spectral tilt trajectory (0-5/5-20/20-80 ms)
            def tilt_traj(sig):
                wins = [(0, 5), (5, 20), (20, 80)]
                return [tilt(sig[onset + int(w0 * 1e-3 * SR):onset + int(w1 * 1e-3 * SR)])
                        for w0, w1 in wins]
            tm = tilt_traj(mo); tu = tilt_traj(uo)
            rows.append(dict(kind=kind, L=L, peak_ratio=peak_ratio,
                             atk_m=atk_m, atk_u=atk_u, dec_m=dm, dec_u=du,
                             tilt_m=tm, tilt_u=tu))
        results[name] = dict(deck=deck, hits=rows)
        print(f"### {name} ({deck})   [peakΔ = mine-UAD dB after loudness-match]")
        print(f"  {'hit':12} {'pkΔdB':>6} {'atkM':>5} {'atkU':>5} "
              f"{'decM(5/20/50)':>18} {'decU(5/20/50)':>18}")
        for r in rows:
            dm = "/".join(f"{v:+.0f}" for v in r["dec_m"])
            du = "/".join(f"{v:+.0f}" for v in r["dec_u"])
            print(f"  {r['kind']+str(r['L']):12} {r['peak_ratio']:+6.2f} "
                  f"{r['atk_m']:5.2f} {r['atk_u']:5.2f} {dm:>18} {du:>18}")
        # intra-hit tilt movement: does mine's tilt swing MORE across the hit than UAD's?
        def swing(rows, key):
            sw = []
            for r in rows:
                v = [t for t in r[key] if np.isfinite(t)]
                if len(v) >= 2:
                    sw.append(max(v) - min(v))
            return float(np.mean(sw)) if sw else np.nan
        print(f"  intra-hit tilt SWING (0-5/5-20/20-80ms): mine {swing(rows,'tilt_m'):.2f} dB "
              f"vs UAD {swing(rows,'tilt_u'):.2f} dB  "
              f"(mine-UAD {swing(rows,'tilt_m')-swing(rows,'tilt_u'):+.2f})\n")
    json.dump(results, open(os.path.join(outdir, "transient.json"), "w"), indent=1)


# ============================================================ MODE: loudness
def kweight_lufs(x):
    """BS.1770 K-weighted loudness (LUFS-ish, mono). Two biquads then -0.691+10log10(ms)."""
    from scipy.signal import lfilter
    # stage 1: high-shelf (48k coeffs, BS.1770)
    b1 = [1.53512485958697, -2.69169618940638, 1.19839281085285]
    a1 = [1.0, -1.69065929318241, 0.73248077421585]
    # stage 2: highpass
    b2 = [1.0, -2.0, 1.0]
    a2 = [1.0, -1.99004745483398, 0.99007225036621]
    y = lfilter(b2, a2, lfilter(b1, a1, x))
    ms = np.mean(y ** 2)
    return -0.691 + 10 * np.log10(ms + 1e-20)


def make_pink(seconds=8.0, level_db=-18.0, seed=11):
    n = int(seconds * SR)
    rng = np.random.default_rng(seed)
    w = rng.standard_normal(n)
    W = np.fft.rfft(w)
    f = np.fft.rfftfreq(n, 1 / SR)
    f[0] = f[1]
    W = W / np.sqrt(f)
    p = np.fft.irfft(W, n)
    p = p / (np.sqrt(np.mean(p ** 2)) + 1e-12)   # unit RMS
    p = p * 10 ** (level_db / 20.0)
    # guard against clipping the file
    pk = np.max(np.abs(p))
    if pk > 0.99:
        p = p * 0.99 / pk
    return p.astype(np.float32)


def mode_loudness(filt):
    outdir = os.path.join(HERE, "renders", "det_loud")
    pink = make_pink()
    spath = os.path.join(outdir, "pink.wav")
    os.makedirs(outdir, exist_ok=True)
    sf.write(spath, np.column_stack([pink, pink]), SR, subtype="FLOAT")

    presets = [p for p in parse_presets() if filt.lower() in p["name"].lower()]

    def one(p):
        vec, _ = decode_uad(p["machine"], UAD_JSON[p["name"]])
        ubin = ATR if p["machine"] == 1 else STUDER
        deck = "ATR" if p["machine"] == 1 else "A800"
        mp = mine_params(p); up = uad_nparams(p["machine"], vec)
        tag = p["name"].replace(" ", "_")
        m = render(MINE, mp, spath, "param", f"m_{tag}", outdir)
        u = render(ubin, up, spath, "nparam", f"u_{tag}", outdir)
        mo, uo = load(m), load(u)
        # loud region on each (pad is silence)
        lm0, lm1 = loud_region(mo); lu0, lu1 = loud_region(uo)
        mo, uo = mo[lm0:lm1], uo[lu0:lu1]
        rms_m = 20 * np.log10(np.sqrt(np.mean(mo ** 2)) + 1e-20)
        rms_u = 20 * np.log10(np.sqrt(np.mean(uo ** 2)) + 1e-20)
        lufs_m = kweight_lufs(mo); lufs_u = kweight_lufs(uo)
        gl = "on" if any(k == "Auto Compensation" and v == 1 for k, v in mp) else "off"
        return dict(name=p["name"], deck=deck, gl=gl, rms_m=rms_m, rms_u=rms_u,
                    lufs_m=lufs_m, lufs_u=lufs_u,
                    d_rms=rms_m - rms_u, d_lufs=lufs_m - lufs_u)

    print(f"loudness: {len(presets)} preset(s), pink -18 dBFS in  "
          f"[Δ = mine-UAD; >0.5 dB audible]\n")
    rows = []
    with ThreadPoolExecutor(max_workers=5) as ex:
        for r in ex.map(one, presets):
            rows.append(r)
    rows.sort(key=lambda r: -abs(r["d_lufs"]))
    print(f"  {'preset':22}{'deck':5}{'GL':4}{'RMSm':>7}{'RMSu':>7}{'ΔRMS':>7}"
          f"{'LUFSm':>8}{'LUFSu':>8}{'ΔLUFS':>7}")
    for r in rows:
        flag = "  <<" if abs(r["d_lufs"]) > 0.5 else ""
        print(f"  {r['name']:22}{r['deck']:5}{r['gl']:4}{r['rms_m']:7.1f}{r['rms_u']:7.1f}"
              f"{r['d_rms']:+7.2f}{r['lufs_m']:8.1f}{r['lufs_u']:8.1f}{r['d_lufs']:+7.2f}{flag}")
    over = [r for r in rows if abs(r["d_lufs"]) > 0.5]
    print(f"\n  {len(over)}/{len(rows)} presets exceed 0.5 dB LUFS delta; "
          f"mean |ΔLUFS| {np.mean([abs(r['d_lufs']) for r in rows]):.2f} dB, "
          f"max {max(abs(r['d_lufs']) for r in rows):.2f} dB")
    json.dump(rows, open(os.path.join(outdir, "loudness.json"), "w"), indent=1)


# ============================================================ MODE: stereo
def make_stereo_drums(seconds=6.0, seed=5):
    """L/R-decorrelated drum-ish loop for M/S analysis + a mono-linked check part."""
    rng = np.random.default_rng(seed)
    n = int(seconds * SR)
    L = np.zeros(n); R = np.zeros(n)
    step = int(0.13 * SR)
    for s in range(n // step):
        t0 = s * step
        dur = int(0.2 * SR)
        m = min(dur, n - t0)
        tt = np.arange(m) / SR
        e = np.exp(-tt / 0.06)
        if s % 4 == 0:      # centred kick
            f = 55 + 70 * np.exp(-tt / 0.03)
            ph = 2 * np.pi * np.cumsum(f) / SR
            k = np.sin(ph) * e * 0.8
            L[t0:t0 + m] += k; R[t0:t0 + m] += k
        else:               # decorrelated hats/snare (different noise L vs R)
            nl = rng.standard_normal(m) * e * 0.3
            nr = rng.standard_normal(m) * e * 0.3
            L[t0:t0 + m] += nl; R[t0:t0 + m] += nr
    pk = max(np.max(np.abs(L)), np.max(np.abs(R))) + 1e-12
    g = 10 ** (-6.0 / 20.0) / pk
    return (np.column_stack([L, R]) * g).astype(np.float32)


def spec_db(x):
    w = x * np.hanning(len(x))
    P = np.abs(np.fft.rfft(w)) ** 2
    f = np.fft.rfftfreq(len(w), 1 / SR)
    bands = [(30, 60), (60, 125), (125, 250), (250, 500), (500, 1000),
             (1000, 2000), (2000, 4000), (4000, 8000), (8000, 16000)]
    out = []
    for lo, hi in bands:
        b = (f >= lo) & (f < hi)
        out.append(10 * np.log10(np.sum(P[b]) + 1e-20))
    return np.array(out), bands


def mode_stereo(filt):
    outdir = os.path.join(HERE, "renders", "det_stereo")
    os.makedirs(outdir, exist_ok=True)
    # (A) L/R identity + crosstalk: mono drum in L, silence in R
    x, hits = synth_hits()
    lonly = np.column_stack([x, np.zeros_like(x)]).astype(np.float32)
    lpath = os.path.join(outdir, "lonly.wav")
    sf.write(lpath, lonly, SR, subtype="FLOAT")
    # (B) stereo decorrelated program for M/S diff + L/R-identity when fed identical
    st = make_stereo_drums()
    stpath = os.path.join(outdir, "stereo.wav")
    sf.write(stpath, st, SR, subtype="FLOAT")

    cases = []
    for mac, ubin, deck in [(0, STUDER, "A800"), (1, ATR, "ATR")]:
        cases.append((f"REF-{deck}", deck, ref_mine(mac), ref_uad(mac), ubin))
    cases = [c for c in cases if filt.lower() in c[0].lower()]

    print("stereo integrity:\n")
    _, bands = spec_db(np.zeros(4096))
    results = {}
    for name, deck, mp, up, ubin in cases:
        # crosstalk / channel test
        mL = render(MINE, mp, lpath, "param", f"mL_{name}", outdir)
        uL = render(ubin, up, lpath, "nparam", f"uL_{name}", outdir)
        ms = load(mL, mono=False); us = load(uL, mono=False)
        lo, hi = loud_region(ms[:, 0])
        def xtalk(sig):
            l = sig[lo:hi, 0]; r = sig[lo:hi, 1]
            pl = np.sqrt(np.mean(l ** 2)); pr = np.sqrt(np.mean(r ** 2))
            return 20 * np.log10((pr + 1e-20) / (pl + 1e-20))
        xt_m, xt_u = xtalk(ms), xtalk(us)
        # stereo program: L/R identity when fed identical? (feed stereo, check M/S)
        mS = render(MINE, mp, stpath, "param", f"mS_{name}", outdir)
        uS = render(ubin, up, stpath, "nparam", f"uS_{name}", outdir)
        msS = load(mS, mono=False); usS = load(uS, mono=False)
        ls, le = loud_region(msS[:, 0])
        def side_ltas(sig):
            side = (sig[ls:le, 0] - sig[ls:le, 1]) / 2
            mid = (sig[ls:le, 0] + sig[ls:le, 1]) / 2
            sm, _ = spec_db(side); md, _ = spec_db(mid)
            return sm - md   # side-minus-mid per band
        sm_m = side_ltas(msS); sm_u = side_ltas(usS)
        # loudness-match side/mid comparison is scale-free (side-minus-mid ratio)
        sdiff = sm_m - sm_u
        results[name] = dict(deck=deck, xtalk_mine=xt_m, xtalk_uad=xt_u,
                             side_diff=sdiff.tolist(), bands=bands)
        print(f"### {name} ({deck})")
        print(f"  crosstalk (R bleed, L-only in):  mine {xt_m:6.1f} dB   UAD {xt_u:6.1f} dB   "
              f"(Δ {xt_m-xt_u:+.1f})")
        print("  band(Hz) " + " ".join(f"{lo:>5}" for lo, _ in bands))
        print("  S-M mine " + " ".join(f"{v:+5.1f}" for v in sm_m))
        print("  S-M UAD  " + " ".join(f"{v:+5.1f}" for v in sm_u))
        print("  S-M diff " + " ".join(f"{v:+5.1f}" for v in sdiff) +
              f"   worst {np.max(np.abs(sdiff)):.1f} dB\n")
    json.dump(results, open(os.path.join(outdir, "stereo.json"), "w"), indent=1)


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "all"
    filt = sys.argv[2] if len(sys.argv) > 2 else ""
    if mode in ("phase", "all"):
        print("\n" + "#" * 40 + " PHASE / GROUP DELAY\n"); mode_phase(filt)
    if mode in ("transient", "all"):
        print("\n" + "#" * 40 + " TRANSIENT ENVELOPE\n"); mode_transient(filt)
    if mode in ("loudness", "all"):
        print("\n" + "#" * 40 + " OUTPUT LOUDNESS\n"); mode_loudness(filt)
    if mode in ("stereo", "all"):
        print("\n" + "#" * 40 + " STEREO INTEGRITY\n"); mode_stereo(filt)


if __name__ == "__main__":
    main()
