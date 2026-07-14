#!/usr/bin/env python3
"""deep_probe.py — the extended A800 benchmark set (categories 2-7 + hot-FR)
that the FR/THD/W&F matrix doesn't cover. Runs on a reference config
(456 / NAB / 15 IPS / Repro) plus path and bias sweeps, comparing TapeMachine
against the UAD A800 and printing a structured report.

Covers:
  2  harmonic-order profile (odd vs even) + SMPTE IMD (60 Hz + 7 kHz)
  3  transient rounding (crest-factor reduction)
  4  signal path: Input / Sync / Repro
  5  bias behaviour sweep (HF roll + THD vs bias)
  6  crosstalk (L->R bleed) + hum (50/60 Hz) in the noise floor
  7  aliasing (hot HF foldback) + phase response
  1b saturation-dependent HF (hot-level sweep vs nominal)

  python3 deep_probe.py          # render + analyse everything
  python3 deep_probe.py --skip-render   # re-analyse existing renders
"""
import os
import sys
import shutil
import tempfile
import subprocess
import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from compare_a800 import freq_response  # noqa: E402  (proper output/input FR ratio)
REPO = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
BIN = os.path.join(REPO, "build/tests/duskverb_render/duskverb_render")
MINE = os.path.expanduser("~/Library/Audio/Plug-Ins/Components/tape_machine_2.component")
STIM = os.path.join(HERE, "stimuli")
SR = 48000

# Target machine: A800 (Swiss800, TapeMachine idx 0) vs UAD Studer A800, or
# ATR102 (Classic102, idx 1) vs UAD Ampex ATR-102. Select with DEEP_MACHINE=ATR102.
MACHINE = os.environ.get("DEEP_MACHINE", "A800")
if MACHINE == "ATR102":
    UAD = "/Library/Audio/Plug-Ins/Components/uaudio_ampex_atr-102_tape.component"
    OUT = os.path.join(HERE, "renders", "deep_atr")
    _mine_machine = "1"                          # Classic102
    UAD_NOISE = ("Hiss & Hum", "On")             # ATR idle-noise param
    UAD_BIAS_PARAMS = ["L Bias", "R Bias"]       # ATR bias is per-channel (Stereo Link on)
    UAD_BIAS_EXTRA = [("Auto Cal", "Off")]       # free the manual bias knobs
else:
    UAD = "/Library/Audio/Plug-Ins/Components/uaudio_studer_a800.component"
    OUT = os.path.join(HERE, "renders", "deep")
    _mine_machine = "0"                          # Swiss800
    UAD_NOISE = ("Noise", "On")
    UAD_BIAS_PARAMS = ["Bias"]
    UAD_BIAS_EXTRA = []

# 456 / NAB / 15 IPS / Repro / +6 cal, on each plugin
MINE_BASE = [("Tape Machine", _mine_machine), ("Tape Speed", "1"), ("Tape Type", "0"),
             ("EQ Standard", "0"), ("Signal Path", "0"), ("Calibration", "1"),  # idx1 = +6 dB after cal remap
             ("Noise Amount", "0"), ("Oversampling", "2"), ("Wow", "0"), ("Flutter", "0")]
UAD_BASE = [("IPS", "15 IPS"), ("Tape Type", "456"), ("Emphasis EQ", "NAB")]


def render(plugin, params, inp, dest, nparams=None):
    tmp = tempfile.mkdtemp()
    cmd = [BIN, "--au", plugin, "--input-wav", os.path.join(STIM, inp),
           "--slug", "s", "--output-dir", tmp, "--prerun-seconds", "2"]
    for n, v in params:
        cmd += ["--param", f"{n}={v}"]
    for n, v in (nparams or []):
        cmd += ["--nparam", f"{n}={v}"]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    stem = os.path.join(tmp, "s_stem.wav")
    # Fail loudly on a bad exit / missing stem instead of returning False and leaving a
    # STALE destination file for the analysis to silently pick up.
    if proc.returncode != 0 or not os.path.exists(stem):
        shutil.rmtree(tmp, ignore_errors=True)
        raise RuntimeError(f"render failed -> {dest} rc={proc.returncode}\n{proc.stderr[-600:]}")
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    shutil.move(stem, dest)
    shutil.rmtree(tmp, ignore_errors=True)
    return dest


def load(path, stereo=False):
    x, sr = sf.read(path)
    if stereo:
        if x.ndim == 1:
            x = np.column_stack([x, x])
        return x.astype(np.float64), sr
    return (x.mean(axis=1) if x.ndim > 1 else x).astype(np.float64), sr


def spec(x):
    w = np.hanning(len(x))
    X = np.abs(np.fft.rfft(x * w))
    f = np.fft.rfftfreq(len(x), 1 / SR)
    return f, X


def level_at(f, X, hz, bw=8):
    k = np.argmin(np.abs(f - hz))
    return float(np.max(X[max(0, k - bw):k + bw + 1]))


# ---- category analyses ------------------------------------------------------
def harmonics(path, fund=1000.0):
    x, _ = load(path)
    # thd_steps segments are 1.2 s each; index 4 == -6 dBFS. Analyse its centre.
    seg = int(1.2 * SR)
    a, b = 4 * seg + int(0.3 * SR), 5 * seg - int(0.3 * SR)
    x = x[a:b] if len(x) >= b else x[int(0.5 * SR):int(2.0 * SR)]
    f, X = spec(x)
    f0 = level_at(f, X, fund)
    hs = {h: 20 * np.log10(level_at(f, X, fund * h) / f0 + 1e-12) for h in range(2, 7)}
    odd = np.sqrt(sum((10 ** (hs[h] / 20)) ** 2 for h in (3, 5)))
    even = np.sqrt(sum((10 ** (hs[h] / 20)) ** 2 for h in (2, 4, 6)))
    return hs, 20 * np.log10(odd + 1e-12), 20 * np.log10(even + 1e-12)


def imd_smpte(path, f_hi=7000.0, f_lo=60.0):
    x, _ = load(path)
    x = x[int(0.5 * SR):]
    f, X = spec(x)
    carrier = level_at(f, X, f_hi)
    side = np.sqrt(sum(level_at(f, X, f_hi + s * f_lo) ** 2 + level_at(f, X, f_hi - s * f_lo) ** 2
                       for s in (1, 2, 3)))
    return float(side / (carrier + 1e-12) * 100.0)


def crest(path, active_only=True):
    x, _ = load(path)
    if active_only:  # trim trailing silence pad so RMS isn't diluted
        thr = np.max(np.abs(x)) * 1e-3
        nz = np.where(np.abs(x) > thr)[0]
        if len(nz):
            x = x[nz[0]:nz[-1] + 1]
    pk = np.max(np.abs(x)); rms = np.sqrt(np.mean(x ** 2))
    return float(20 * np.log10(pk / (rms + 1e-12)))


def hf_ratio(sweep_wav, out_wav, hz=10000.0):
    """True FR level at hz relative to 1 kHz (dB) — output/input sweep ratio,
    level-matched at the midband. Immune to the log-sweep's own spectral tilt."""
    g, mag = freq_response(os.path.join(STIM, sweep_wav), out_wav)
    return float(mag[np.argmin(np.abs(g - hz))])


def crosstalk_db(path):
    x, _ = load(path, stereo=True)
    x = x[int(0.5 * SR):]
    lr = np.sqrt(np.mean(x[:, 0] ** 2)); rr = np.sqrt(np.mean(x[:, 1] ** 2))
    return float(20 * np.log10(rr / (lr + 1e-12) + 1e-12))


def hum_and_hiss(path):
    x, _ = load(path)
    x = x[int(0.3 * SR):]
    f, X = spec(x)
    rms = np.sqrt(np.mean(x ** 2))
    rms_db = 20 * np.log10(rms + 1e-12)
    hum = np.sqrt(sum(level_at(f, X, h) ** 2 for h in (50, 60, 100, 120)))
    tot = np.sqrt(np.sum(X ** 2))
    hum_frac = 20 * np.log10(hum / (tot + 1e-12) + 1e-12)
    return rms_db, hum_frac


def aliasing(path, freqs=(5000, 8000, 11000, 15000, 19000), seg=1.0):
    """Per hot HF tone, in-band spurious (non-harmonic) energy rel fundamental."""
    x, _ = load(path)
    worst = -999.0
    for i, f0 in enumerate(freqs):
        a = int((i * seg + 0.2) * SR); b = int(((i + 1) * seg - 0.2) * SR)
        if b > len(x):
            break
        f, X = spec(x[a:b])
        fund = level_at(f, X, f0)
        harm = {int(round(f0 * k)) for k in range(1, 6)}
        band = (f > 100) & (f < 20000)
        spur = 0.0
        for k in np.where(band)[0]:
            if all(abs(f[k] - h) > 60 for h in harm):
                spur = max(spur, X[k])
        worst = max(worst, 20 * np.log10(spur / (fund + 1e-12) + 1e-12))
    return worst


def phase_response(in_wav, out_wav):
    x, _ = load(os.path.join(STIM, in_wav)); y, _ = load(out_wav)
    n = min(len(x), len(y))
    x, y = x[:n], y[:n]
    # Remove the plugin's latency (integer lag) before deriving phase, then report phase
    # RELATIVE to 1 kHz — the pure-delay term otherwise dominates the raw angle (mirrors
    # det_probe.py's latency-align + 1 kHz-relative group-delay approach).
    s0, s1 = int(n * 0.1), int(n * 0.9)
    xw, yw = x[s0:s1], y[s0:s1]
    L = len(xw)
    nf = 1 << int(np.ceil(np.log2(2 * L)))
    xc = np.fft.irfft(np.fft.rfft(xw, nf) * np.conj(np.fft.rfft(yw, nf)), nf)
    lag = int(np.argmax(xc)); lag = lag - nf if lag > nf // 2 else lag
    y = np.roll(y, lag)
    H = np.fft.rfft(y) / (np.fft.rfft(x) + 1e-12)
    f = np.fft.rfftfreq(n, 1 / SR)
    ph = np.unwrap(np.angle(H))
    ref = ph[np.argmin(np.abs(f - 1000.0))]      # 1 kHz reference (residual dispersion only)
    return {hz: float(np.degrees(ph[np.argmin(np.abs(f - hz))] - ref)) for hz in (100, 1000, 10000)}


# ---- driver -----------------------------------------------------------------
def rr(sub, plug_path, params, inp):
    dest = os.path.join(OUT, sub)
    render(plug_path, params, inp, dest)
    return dest


def main():
    skip = "--skip-render" in sys.argv
    P = {}

    def do(tag, plug, base, extra, inp):
        d = os.path.join(OUT, f"{tag}.wav")
        if not skip:
            render(plug, base + extra, inp, d)
        return d

    print("Rendering deep-probe set (may take a minute)..." if not skip else "Analysing...")
    # reference-config renders
    jobs = {
        "harm_m": (MINE, MINE_BASE, [], "thd_steps.wav"),
        "harm_u": (UAD, UAD_BASE, [], "thd_steps.wav"),
        "imd_m": (MINE, MINE_BASE, [], "imd.wav"),
        "imd_u": (UAD, UAD_BASE, [], "imd.wav"),
        "tr_m": (MINE, MINE_BASE, [], "transient.wav"),
        "tr_u": (UAD, UAD_BASE, [], "transient.wav"),
        "xt_m": (MINE, MINE_BASE, [], "xtalk.wav"),
        "xt_u": (UAD, UAD_BASE, [], "xtalk.wav"),
        "alias_m": (MINE, MINE_BASE, [], "alias.wav"),
        "alias_u": (UAD, UAD_BASE, [], "alias.wav"),
        "hotsw_m": (MINE, MINE_BASE, [], "hot_sweep.wav"),
        "hotsw_u": (UAD, UAD_BASE, [], "hot_sweep.wav"),
        "sw_m": (MINE, MINE_BASE, [], "sweep.wav"),
        "sw_u": (UAD, UAD_BASE, [], "sweep.wav"),
    }
    for tag, (plug, base, extra, inp) in jobs.items():
        if not skip:
            render(plug, base + extra, inp, os.path.join(OUT, f"{tag}.wav"))
        P[tag] = os.path.join(OUT, f"{tag}.wav")

    # hiss (noise on)
    if not skip:
        render(MINE, [p for p in MINE_BASE if p[0] != "Noise Amount"] + [("Noise Amount", "50")],
               "silence.wav", os.path.join(OUT, "hiss_m.wav"))
        render(UAD, UAD_BASE + [UAD_NOISE], "silence.wav", os.path.join(OUT, "hiss_u.wav"))
    P["hiss_m"] = os.path.join(OUT, "hiss_m.wav"); P["hiss_u"] = os.path.join(OUT, "hiss_u.wav")

    # path modes
    paths = {"Input": ("2", "Input"), "Sync": ("1", "Sync"), "Repro": ("0", "Repro")}
    for pm, (midx, ulabel) in paths.items():
        if not skip:
            mb = [p for p in MINE_BASE if p[0] != "Signal Path"] + [("Signal Path", midx)]
            render(MINE, mb, "sweep.wav", os.path.join(OUT, f"path_{pm}_m.wav"))
            render(UAD, UAD_BASE + [("Path Select", ulabel)], "sweep.wav", os.path.join(OUT, f"path_{pm}_u.wav"))
        P[f"path_{pm}_m"] = os.path.join(OUT, f"path_{pm}_m.wav")
        P[f"path_{pm}_u"] = os.path.join(OUT, f"path_{pm}_u.wav")

    # bias sweep (auto-cal OFF on mine so Bias knob is live; UAD Bias in volts).
    # sweep -> HF roll-off vs bias; thd_steps -> distortion vs bias.
    bias_pts = [("low", "0.2", "0.15"), ("nom", "0.5", "0.5"), ("high", "0.8", "0.85")]
    for name, mnorm, unorm in bias_pts:
        for stim, sfx in (("sweep.wav", "sw"), ("thd_steps.wav", "thd")):
            if not skip:
                mb = [p for p in MINE_BASE if p[0] != "Noise Amount"] + \
                     [("Auto Calibration", "0")]
                render(MINE, mb, stim, os.path.join(OUT, f"bias_{name}_{sfx}_m.wav"),
                       nparams=[("Bias", mnorm)])
                # Route the UAD bias render through render() too so it gets the same
                # checked (raise-on-failure) behaviour instead of a bare subprocess.run.
                render(UAD, UAD_BASE + UAD_BIAS_EXTRA, stim,
                       os.path.join(OUT, f"bias_{name}_{sfx}_u.wav"),
                       nparams=[(bp, unorm) for bp in UAD_BIAS_PARAMS])
            P[f"bias_{name}_{sfx}_m"] = os.path.join(OUT, f"bias_{name}_{sfx}_m.wav")
            P[f"bias_{name}_{sfx}_u"] = os.path.join(OUT, f"bias_{name}_{sfx}_u.wav")

    # ---- report ----
    print("\n# Deep-probe report — 456 / NAB / 15 IPS / Repro (unless noted)\n")

    print("## 2. Harmonic profile (1 kHz @ -6 dBFS, rel fundamental)")
    for who, p in (("mine", P["harm_m"]), ("UAD", P["harm_u"])):
        hs, odd, even = harmonics(p)
        hstr = " ".join(f"{h}f={hs[h]:+.0f}" for h in range(2, 7))
        print(f"  {who:4s}: {hstr}   odd(3,5)={odd:+.1f}dB  even(2,4,6)={even:+.1f}dB")

    print("\n## 2. SMPTE IMD (60 Hz + 7 kHz, 4:1)")
    print(f"  mine {imd_smpte(P['imd_m']):.3f}%   UAD {imd_smpte(P['imd_u']):.3f}%")

    print("\n## 3. Transient crest factor (higher = less rounding)")
    ci = crest(os.path.join(STIM, "transient.wav"))
    print(f"  input {ci:.2f}dB | mine {crest(P['tr_m']):.2f}dB  UAD {crest(P['tr_u']):.2f}dB")

    print("\n## 4. Signal path — HF@10k rel 1k (true FR ratio)")
    for pm in ("Input", "Sync", "Repro"):
        print(f"  {pm:6s}: mine {hf_ratio('sweep.wav', P[f'path_{pm}_m']):+.2f}dB"
              f"   UAD {hf_ratio('sweep.wav', P[f'path_{pm}_u']):+.2f}dB")

    print("\n## 5. Bias sweep — HF@10k roll (FR) & THD@-6dBFS")
    from compare_a800 import thd_curve  # noqa
    for name, _, _ in bias_pts:
        hm = hf_ratio("sweep.wav", P[f"bias_{name}_sw_m"]); hu = hf_ratio("sweep.wav", P[f"bias_{name}_sw_u"])
        tm = dict(thd_curve(P[f"bias_{name}_thd_m"])).get(-6, float("nan"))
        tu = dict(thd_curve(P[f"bias_{name}_thd_u"])).get(-6, float("nan"))
        print(f"  {name:4s}: HF mine {hm:+.2f}/UAD {hu:+.2f}dB   THD mine {tm:.2f}/UAD {tu:.2f}%")

    print("\n## 6. Crosstalk (L->R bleed) & hum")
    print(f"  crosstalk: mine {crosstalk_db(P['xt_m']):.1f}dB   UAD {crosstalk_db(P['xt_u']):.1f}dB")
    rm, hm = hum_and_hiss(P["hiss_m"]); ru, hu = hum_and_hiss(P["hiss_u"])
    print(f"  hiss RMS:  mine {rm:.1f}dBFS  UAD {ru:.1f}dBFS")
    print(f"  hum frac:  mine {hm:.1f}dB    UAD {hu:.1f}dB  (energy at 50/60/100/120 Hz rel total)")

    print("\n## 7. Aliasing (worst in-band spur, hot HF tones) & phase")
    print(f"  aliasing:  mine {aliasing(P['alias_m']):.1f}dB   UAD {aliasing(P['alias_u']):.1f}dB  (lower=cleaner)")
    pm = phase_response("sweep.wav", P["sw_m"]); pu = phase_response("sweep.wav", P["sw_u"])
    print(f"  phase deg: mine {pm}   UAD {pu}")

    print("\n## 1b. Saturation-dependent HF (hot vs nominal sweep, HF@10k rel 1k)")
    print(f"  nominal:   mine {hf_ratio('sweep.wav', P['sw_m']):+.2f}dB   UAD {hf_ratio('sweep.wav', P['sw_u']):+.2f}dB")
    print(f"  hot:       mine {hf_ratio('hot_sweep.wav', P['hotsw_m']):+.2f}dB   UAD {hf_ratio('hot_sweep.wav', P['hotsw_u']):+.2f}dB")


if __name__ == "__main__":
    main()
