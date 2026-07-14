#!/usr/bin/env python3
"""wf_spectrum_probe.py — Wow&Flutter *spectral character* A/B: TapeMachine 2 vs UAD.

wf_probe.py matched W&F DEPTH (a single robust FMdev number). It never looked at
WHERE the modulation energy sits. Two tapes with identical FMdev can sound utterly
different ("watery" vs "smooth") if their modulation SPECTRA differ — wow peak
placement, flutter/scrape bandwidth, and the pitch-(FM)-vs-amplitude-(AM) balance.

Method (SHIPPED preset state — W&F ON at the preset knobs, hiss forced OFF so the
envelope isolates modulation, ~24 s 1 kHz tone for 0.1 Hz resolution):
  1. Bandpass the carrier, Hilbert -> instantaneous frequency trace (Hz).
     FFT that trace -> the FM modulation spectrum 0.1-200 Hz.
  2. Hilbert envelope -> FFT -> the AM modulation spectrum (same band).
  3. Report band power (wow 0.1-4, flutter 4-30, scrape 30-200 Hz), the dominant
     wow/flutter peak frequency, and the AM/FM power balance, mine vs UAD.

UAD Studer A800 has NO W&F param (its baseline ~0.01 Hz is demod noise) -> for Swiss
presets mine's wobble is compared against a ~flat deck (a finding in itself). UAD ATR
has a real W&F toggle -> the meaningful spectral match is on Classic 102 presets.

  python3 wf_spectrum_probe.py                 # 5 audition presets (modulation spectra)
  python3 wf_spectrum_probe.py Sunbaked        # substring subset
  python3 wf_spectrum_probe.py ref             # ATR & Studer reference configs (W&F fwd on)
  python3 wf_spectrum_probe.py texture [subs]  # DIM3 carrier skirt + DIM4 80 Hz pitch bend
"""
import os, sys, tempfile, shutil, subprocess, itertools
import numpy as np
import soundfile as sf
from scipy.signal import butter, sosfiltfilt, hilbert, get_window
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from preset_validate import (parse_presets, decode_uad, UAD_JSON, mine_params,
                             ATR, STUDER, MINE, BIN, STIM)  # noqa: E402

CARRIER = 1000.0
DUR = 24.0
OUT = os.path.join(HERE, "renders", "sto_wfspec")   # unique dest (concurrent agent safe)
_rc = itertools.count()
STIM_TONE = "sto_tone1k_24s.wav"

WOW_BAND = (0.1, 4.0)
FLUT_BAND = (4.0, 30.0)
SCRAPE_BAND = (30.0, 200.0)


def ensure_tone():
    path = os.path.join(STIM, STIM_TONE)
    if os.path.exists(path):
        return
    sr = 48000
    t = np.arange(int(sr * DUR)) / sr
    x = 0.2512 * np.sin(2.0 * np.pi * CARRIER * t)      # -12 dBFS
    fade = int(0.02 * sr)
    x[:fade] *= np.linspace(0, 1, fade)
    x[-fade:] *= np.linspace(1, 0, fade)
    os.makedirs(STIM, exist_ok=True)
    sf.write(path, np.column_stack([x, x]).astype(np.float32), sr)


def check_pace():
    return os.path.isdir("/var/tmp/com.paceap.eden.licensed")


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


def loud_slice(path, thresh=0.1, lo_frac=0.08, hi_frac=0.92):
    """mono slice of the largest loud run (skips the render's silent pad); wide
    inner window so we keep enough samples for 0.1 Hz modulation resolution."""
    x, sr = sf.read(path)
    if x.ndim > 1:
        x = x.mean(axis=1)
    x = x.astype(np.float64)
    win = max(1, int(0.02 * sr))
    env = np.convolve(np.abs(x), np.ones(win) / win, mode="same")
    peak = np.max(env)
    if peak <= 0:
        raise ValueError(f"{path}: silent render")
    loud = env > thresh * peak
    idx = np.flatnonzero(loud)
    splits = np.flatnonzero(np.diff(idx) > 1)
    starts = np.r_[idx[0], idx[splits + 1]]
    ends = np.r_[idx[splits], idx[-1]]
    k = int(np.argmax(ends - starts))
    s, e = starts[k], ends[k]
    L = e - s
    a, b = s + int(lo_frac * L), s + int(hi_frac * L)
    return x[a:b], sr


def if_trace(seg, sr, carrier=CARRIER):
    """Instantaneous-frequency trace (Hz, carrier removed) via bandpass+Hilbert."""
    # Symmetric half-width of max(20% of carrier, 50 Hz): for the 1 kHz carrier this is the
    # original ±200 Hz (unchanged); for the 80 Hz carrier ±16 Hz would clip the documented
    # 4-30 Hz flutter sidebands (80±30), so it widens to ±50 Hz. Carrier stays centred and
    # the band respects DC/Nyquist.
    hw = max(carrier * 0.20, 50.0)
    lo = max(carrier - hw, 20.0)
    hi = min(carrier + hw, sr / 2.0 * 0.99)
    sos = butter(4, [lo, hi], btype="band", fs=sr, output="sos")
    car = sosfiltfilt(sos, seg)
    an = hilbert(car)
    phase = np.unwrap(np.angle(an))
    inst = np.diff(phase) * sr / (2.0 * np.pi)     # Hz
    inst = inst - np.median(inst)                  # remove carrier (robust)
    # clip demod glitches (the ~27% spurious-sample trap) at a robust bound
    mad = np.median(np.abs(inst - np.median(inst))) * 1.4826 + 1e-9
    inst = np.clip(inst, -8 * mad, 8 * mad)
    return inst


def am_trace(seg, sr, carrier=CARRIER):
    # Same widened band as if_trace (±max(20% carrier, 50 Hz)): keeps the 1 kHz band at
    # ±200 Hz while giving a low (80 Hz) carrier room for its flutter sidebands.
    hw = max(carrier * 0.20, 50.0)
    lo = max(carrier - hw, 20.0)
    hi = min(carrier + hw, sr / 2.0 * 0.99)
    sos = butter(4, [lo, hi], btype="band", fs=sr, output="sos")
    car = sosfiltfilt(sos, seg)
    env = np.abs(hilbert(car))
    m = np.mean(env)
    if m < 1e-12:
        return np.zeros_like(env)
    return env / m - 1.0                           # fractional AM


def mod_spectrum(trace, sr, fmax=200.0):
    """One-sided amplitude spectrum of a modulation trace, decimated view to fmax."""
    n = len(trace)
    w = get_window("hann", n)
    tr = trace * w
    sp = np.abs(np.fft.rfft(tr)) / (np.sum(w) / 2.0)
    fr = np.fft.rfftfreq(n, 1.0 / sr)
    keep = fr <= fmax
    return fr[keep], sp[keep]


def band_power(fr, sp, band):
    m = (fr >= band[0]) & (fr < band[1])
    if not np.any(m):
        return 0.0
    return float(np.sqrt(np.sum(sp[m] ** 2)))      # RMS-in-band


def peak_freq(fr, sp, band):
    m = (fr >= band[0]) & (fr < band[1])
    if not np.any(m):
        return float("nan")
    idx = np.flatnonzero(m)
    return float(fr[idx][np.argmax(sp[idx])])


def analyse(path):
    seg, sr = loud_slice(path)
    ift = if_trace(seg, sr)
    amt = am_trace(seg[1:], sr)   # align length with ift
    ffr, fsp = mod_spectrum(ift, sr)
    afr, asp = mod_spectrum(amt, sr)
    return dict(
        fm_wow=band_power(ffr, fsp, WOW_BAND),
        fm_flut=band_power(ffr, fsp, FLUT_BAND),
        fm_scrape=band_power(ffr, fsp, SCRAPE_BAND),
        fm_wowpk=peak_freq(ffr, fsp, WOW_BAND),
        fm_flutpk=peak_freq(ffr, fsp, FLUT_BAND),
        am_wow=band_power(afr, asp, WOW_BAND),
        am_flut=band_power(afr, asp, FLUT_BAND),
        ffr=ffr, fsp=fsp,
    )


def mine_wf_params(p):
    base = [kv for kv in mine_params(p) if kv[0] not in ("Wow", "Flutter", "Noise Amount")]
    base += [("Wow", p["wow"] / 100.0), ("Flutter", p["flutter"] / 100.0),
             ("Noise Amount", 0.0)]
    return base


def uad_wf_nparams(machine, vec):
    v = dict(vec)
    if machine == 1:
        v["Hiss & Hum"] = 0.0
    else:
        v["Noise"] = 0.0
        v["Hum Noise"] = 0.0
        v["Hiss Noise"] = 0.0
    return [(k, val) for k, val in v.items()]


def ascii_spectrum(ffr, fsp, label, fmax=60.0, width=48):
    """Compact ASCII plot of the FM modulation spectrum 0-fmax Hz (log-ish bins)."""
    edges = np.array([0.1, 0.3, 0.5, 0.8, 1.2, 1.8, 2.6, 4, 6, 9, 13, 18, 25, 35, 50, fmax])
    lines = [f"    {label} FM mod-spectrum (Hz -> level):"]
    vals = []
    for i in range(len(edges) - 1):
        m = (ffr >= edges[i]) & (ffr < edges[i + 1])
        vals.append(np.sqrt(np.sum(fsp[m] ** 2)) if np.any(m) else 0.0)
    vmax = max(vals) or 1.0
    for i, v in enumerate(vals):
        bar = "#" * int(round(width * v / vmax))
        lines.append(f"    {edges[i]:5.1f}-{edges[i+1]:5.1f}Hz |{bar}")
    return "\n".join(lines)


def probe(p, show_plot=False):
    machine = p["machine"]
    uad_bin = ATR if machine == 1 else STUDER
    vec, uname = decode_uad(machine, UAD_JSON[p["name"]])
    mp = mine_wf_params(p)
    unp = uad_wf_nparams(machine, vec)
    m_out = render(MINE, mp, STIM_TONE, "wfs_m")
    u_out = render(uad_bin, [], STIM_TONE, "wfs_u", nparams=unp)
    ma, ua = analyse(m_out), analyse(u_out)
    return dict(name=p["name"], machine=machine, wow=p["wow"], flutter=p["flutter"],
                m=ma, u=ua)


def print_row(r):
    m, u = r["m"], r["u"]
    deck = "ATR" if r["machine"] == 1 else "Studer(noWF)"
    print(f"\n### {r['name']:20s} [{deck}]  wow/flut knob {r['wow']:.0f}/{r['flutter']:.0f}")
    print(f"  {'metric':16s} {'MINE':>10} {'UAD':>10}   ratio(m/u)")
    def line(lbl, a, b, unit=""):
        rr = a / b if b > 1e-12 else float("inf")
        print(f"  {lbl:16s} {a:9.4f}{unit:1s} {b:9.4f}{unit:1s}   {rr:8.2f}x")
    line("FM wow(0.1-4)", m["fm_wow"], u["fm_wow"], "H")
    line("FM flut(4-30)", m["fm_flut"], u["fm_flut"], "H")
    line("FM scrape(30+)", m["fm_scrape"], u["fm_scrape"], "H")
    print(f"  {'FM wow peak':16s} {m['fm_wowpk']:9.2f}H {u['fm_wowpk']:9.2f}H")
    print(f"  {'FM flut peak':16s} {m['fm_flutpk']:9.2f}H {u['fm_flutpk']:9.2f}H")
    line("AM wow", m["am_wow"], u["am_wow"])
    line("AM flut", m["am_flut"], u["am_flut"])
    # AM/FM balance: fraction of total modulation that is amplitude vs pitch
    m_bal = (m["am_wow"] + m["am_flut"]) / max(1e-9, m["fm_wow"] + m["fm_flut"])
    u_bal = (u["am_wow"] + u["am_flut"]) / max(1e-9, u["fm_wow"] + u["fm_flut"])
    print(f"  {'AM/FM ratio':16s} {m_bal:9.4f}  {u_bal:9.4f}   (pitch<->ampl balance)")
    print(ascii_spectrum(m["ffr"], m["fsp"], "MINE"))
    print(ascii_spectrum(u["ffr"], u["fsp"], "UAD "))


DEFAULT = ["Sunbaked", "Old Tape", "Analog Warmth", "Modern Rock", "Nice 456"]


def main():
    ensure_tone()
    if not check_pace():
        print("!! PACE down — UAD renders bypass; aborting."); return
    args = sys.argv[1:]
    if args and args[0] == "ref":
        ref_mode(); return
    if args and args[0] == "texture":
        rest = args[1:] or DEFAULT
        texture_mode([s.strip().lower() for s in rest]); return
    subs = [s.strip().lower() for s in (args or DEFAULT)]
    presets = [p for p in parse_presets()
               if any(s in p["name"].lower() for s in subs)]
    print(f"WF spectral probe: {len(presets)} preset(s), {DUR:.0f}s 1kHz, W&F on / hiss off\n")
    with ThreadPoolExecutor(max_workers=4) as ex:
        rows = list(ex.map(lambda p: _safe(probe, p), presets))
    for r in [r for r in rows if r]:
        print_row(r)


def ref_mode():
    """ATR & Studer at 456/NAB/+6/15IPS, W&F forced ON, vs mine at wow7/flut3."""
    ensure_tone()
    print("Reference configs (456/NAB/+6/15IPS, UAD W&F forced ON, mine wow7/flut3)\n")
    # ATR
    atr_np = [("IPS", 2/3), ("Tape Type", 1/3), ("Cal Level", 1/3), ("Emphasis EQ", 0.0),
              ("Path Select", 1/3), ("Auto Cal", 1.0), ("Wow & Flutter", 1.0), ("Hiss & Hum", 0.0)]
    stu_np = [("IPS", 0.5), ("Tape Type", 1/3), ("Cal Level", 1/3), ("Emphasis EQ", 0.0),
              ("Path Select", 1.0), ("Auto Cal", 1.0), ("Noise", 0.0), ("Hum Noise", 0.0), ("Hiss Noise", 0.0)]
    mine_c = [("Tape Machine", 1), ("Tape Speed", 1), ("Tape Type", 0), ("EQ Standard", 0),
              ("Calibration", 1), ("Signal Path", 0), ("Auto Calibration", 1), ("Auto Compensation", 1),
              ("Input Gain", 0.5), ("Oversampling", 1), ("Noise Amount", 0.0),
              ("Wow", 0.07), ("Flutter", 0.03)]
    mine_s = [("Tape Machine", 0), ("Tape Speed", 1), ("Tape Type", 0), ("EQ Standard", 0),
              ("Calibration", 1), ("Signal Path", 0), ("Auto Calibration", 1), ("Auto Compensation", 1),
              ("Input Gain", 0.5), ("Oversampling", 1), ("Noise Amount", 0.0),
              ("Wow", 0.07), ("Flutter", 0.03)]
    jobs = {
        "ATR (W&F on)": lambda: analyse(render(ATR, [], STIM_TONE, "ref_atr", nparams=atr_np)),
        "mine Classic102 wow7/3": lambda: analyse(render(MINE, mine_c, STIM_TONE, "ref_mc")),
        "Studer (W&F n/a)": lambda: analyse(render(STUDER, [], STIM_TONE, "ref_stu", nparams=stu_np)),
        "mine Swiss800 wow7/3": lambda: analyse(render(MINE, mine_s, STIM_TONE, "ref_ms")),
    }
    for lbl, fn in jobs.items():
        a = fn()
        print(f"{lbl:26s} FMwow {a['fm_wow']:.4f} FMflut {a['fm_flut']:.4f} "
              f"FMscrape {a['fm_scrape']:.4f}  wowPk {a['fm_wowpk']:.2f}Hz flutPk {a['fm_flutpk']:.2f}Hz")


def carrier_skirt(path, carrier=CARRIER):
    """DIM3: -40 dB spectral-skirt width + near-carrier (2-40 Hz) sideband energy of
    the W&F-blurred tone. More close-in sideband energy = more audible FR smear."""
    seg, sr = loud_slice(path)
    n = len(seg); w = get_window("hann", n)
    sp = np.abs(np.fft.rfft(seg * w)); fr = np.fft.rfftfreq(n, 1.0 / sr)
    pk = np.max(sp); pkf = fr[np.argmax(sp)]
    spdb = 20 * np.log10(sp / pk + 1e-20)
    band = (fr > pkf - 80) & (fr < pkf + 80)
    m40 = band & (spdb > -40)
    bw40 = float(fr[m40].max() - fr[m40].min()) if m40.any() else 0.0
    side = (np.abs(fr - pkf) > 2) & (np.abs(fr - pkf) < 40)
    return bw40, float(np.sqrt(np.sum(sp[side] ** 2)) / pk)


def low_carrier(path, f0=80.0):
    """DIM4: pitch-modulation on an 80 Hz fundamental (kick/bass tail). Returns cents
    deviation + FM band powers. W&F is ratio-based so cents should track the 1 kHz test;
    a 60 Hz-band scrape can't sideband an 80 Hz carrier, so this isolates wow/flutter."""
    seg, sr = loud_slice(path)
    ift = if_trace(seg, sr, carrier=f0)
    ffr, fsp = mod_spectrum(ift, sr)
    dev = np.std(ift)
    cents = 1200 * np.log2(1 + dev / f0) if dev < f0 else float("nan")
    return dict(cents=cents, fm_wow=band_power(ffr, fsp, WOW_BAND),
                fm_flut=band_power(ffr, fsp, FLUT_BAND),
                wowpk=peak_freq(ffr, fsp, WOW_BAND))


def ensure_low_tone():
    path = os.path.join(STIM, "sto_tone80_24s.wav")
    if os.path.exists(path):
        return
    sr = 48000
    t = np.arange(int(sr * DUR)) / sr
    x = 0.2512 * np.sin(2 * np.pi * 80.0 * t)
    fade = int(0.02 * sr)
    x[:fade] *= np.linspace(0, 1, fade); x[-fade:] *= np.linspace(1, 0, fade)
    os.makedirs(STIM, exist_ok=True)
    sf.write(path, np.column_stack([x, x]).astype(np.float32), sr)


def texture_mode(subs):
    """DIM3 (carrier skirt) + DIM4 (80 Hz pitch bend) for the given presets."""
    ensure_tone(); ensure_low_tone()
    presets = [p for p in parse_presets() if any(s in p["name"].lower() for s in subs)]
    print("DIM3 carrier skirt (1 kHz) + DIM4 pitch bend (80 Hz), W&F on / hiss off\n")
    for p in presets:
        machine = p["machine"]
        uad_bin = ATR if machine == 1 else STUDER
        vec, _ = decode_uad(machine, UAD_JSON[p["name"]])
        mp = mine_wf_params(p); unp = uad_wf_nparams(machine, vec)
        m1 = render(MINE, mp, STIM_TONE, "tx_m"); u1 = render(uad_bin, [], STIM_TONE, "tx_u", nparams=unp)
        ml = render(MINE, mp, "sto_tone80_24s.wav", "tl_m")
        ul = render(uad_bin, [], "sto_tone80_24s.wav", "tl_u", nparams=unp)
        bm, sm = carrier_skirt(m1); bu, su = carrier_skirt(u1)
        lm, lu = low_carrier(ml), low_carrier(ul)
        deck = "ATR" if machine == 1 else "Studer(noWF)"
        print(f"### {p['name']:20s} [{deck}] wow/flut {p['wow']:.0f}/{p['flutter']:.0f}")
        print(f"  DIM3 skirt -40dB: MINE {bm:5.2f}Hz side {sm:.4f}   UAD {bu:5.2f}Hz side {su:.4f}"
              f"   (mine/uad {sm/max(su,1e-9):.2f}x)")
        print(f"  DIM4 80Hz pitch : MINE {lm['cents']:.3f}c wowPk {lm['wowpk']:.2f}Hz   "
              f"UAD {lu['cents']:.3f}c wowPk {lu['wowpk']:.2f}Hz\n")


def _safe(fn, p):
    try:
        return fn(p)
    except Exception as e:
        print(f"!! {p['name']}: {e}")
        return None


if __name__ == "__main__":
    main()
