#!/usr/bin/env python3
"""compare_a800.py — measure TapeMachine A800 vs UAD Studer A800 and report.

Consumes the WAVs produced by render_ab.sh (renders/{tapemachine,uad}/{speed}/*)
and produces, per speed:
  * frequency response overlay (level-matched)  -> report/fr_<speed>ips.png
  * THD-vs-input-level overlay                  -> report/thd_<speed>ips.png
  * wow/flutter modulation spectrum overlay     -> report/wf_<speed>ips.png
  * noise-floor spectrum overlay                -> report/noise_<speed>ips.png
and a single markdown summary                   -> report/comparison_<date>.md
with a numeric diff table (mine | UAD | delta) for every metric.

Reuses tests/audio_analyzer.py (AudioAnalyzer.calculate_thd) and the local
wow_flutter.measure_wow_flutter. Frequency response is computed here directly
(ratio of output/input spectra of the log sweep) so we control the smoothing
and the level-match normalisation.
"""
import os
import sys
import glob
import datetime
import numpy as np
import soundfile as sf
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tests"))
from audio_analyzer import AudioAnalyzer  # noqa: E402
sys.path.insert(0, HERE)
from wow_flutter import measure_wow_flutter  # noqa: E402

SR = 48000
STIM = os.path.join(HERE, "stimuli")
RENDERS = os.path.join(HERE, "renders")
REPORT = os.path.join(HERE, "report")
PLUGINS = {"tapemachine": "TapeMachine A800", "uad": "UAD A800"}
COLORS = {"tapemachine": "#2a7de1", "uad": "#e0662a"}

# THD segment layout (mirrors gen_stimuli.py)
THD_LEVELS = [-30, -24, -18, -12, -6, -3]
THD_SEG_SEC = 1.2


def load(path):
    x, sr = sf.read(path)
    if x.ndim > 1:
        x = x.mean(axis=1)
    return x.astype(np.float64), sr


# ---- frequency response -----------------------------------------------------
# Fixed log-spaced analysis grid (20 Hz-20 kHz). Every FR curve lands on this
# same grid, so mine and UAD can be diffed bin-for-bin, and 1/6-octave smoothing
# is a cheap per-point band average instead of an O(N^2) sweep over the full FFT.
FR_GRID = np.geomspace(20.0, 20000.0, 256)


def freq_response(in_wav, out_wav, frac=1 / 6):
    x, _ = load(in_wav)
    y, _ = load(out_wav)
    n = min(len(x), len(y))
    X = np.abs(np.fft.rfft(x[:n]))
    Y = np.abs(np.fft.rfft(y[:n]))
    f = np.fft.rfftfreq(n, 1 / SR)
    raw = 20 * np.log10(Y / (X + 1e-12) + 1e-12)
    # 1/6-octave average onto the fixed log grid (vectorised via bin search)
    lo = FR_GRID * 2 ** (-frac / 2)
    hi = FR_GRID * 2 ** (frac / 2)
    il = np.searchsorted(f, lo)
    ih = np.searchsorted(f, hi)
    # a band narrower than the FFT resolution can cover no bins (il == ih),
    # which would read as a fake 0 dB — fall back to the nearest real bin
    empty = ih <= il
    if np.any(empty):
        c = np.clip(np.searchsorted(f, FR_GRID[empty]), 1, len(raw) - 1)
        nearest = np.where(FR_GRID[empty] - f[c - 1] <= f[c] - FR_GRID[empty], c - 1, c)
        il[empty] = nearest
        ih[empty] = nearest + 1
    csum = np.concatenate([[0.0], np.cumsum(raw)])
    cnt = ih - il
    mag = (csum[ih] - csum[il]) / cnt
    ref = np.median(mag[(FR_GRID >= 300) & (FR_GRID <= 3000)])
    return FR_GRID.copy(), mag - ref


def fr_features(f, mag):
    lows = (f >= 25) & (f <= 200)
    bump_i = np.argmax(mag[lows])
    bump_db = float(mag[lows][bump_i])
    bump_f = float(f[lows][bump_i])
    # HF -3 dB point (first crossing above 2 kHz)
    hf = f >= 2000
    ff, mm = f[hf], mag[hf]
    cross = np.where(mm <= -3.0)[0]
    hf3 = float(ff[cross[0]]) if len(cross) else float(ff[-1])
    return {"head_bump_db": round(bump_db, 2), "head_bump_hz": round(bump_f, 1),
            "hf_minus3db_hz": round(hf3, 0)}


# ---- THD vs level -----------------------------------------------------------
def thd_curve(out_wav):
    y, sr = load(out_wav)
    an = AudioAnalyzer(sr)
    seg = int(THD_SEG_SEC * sr)
    pts = []
    for i, lvl in enumerate(THD_LEVELS):
        a = i * seg + int(0.3 * sr)       # skip ramp + settle
        b = (i + 1) * seg - int(0.3 * sr)
        if b > len(y):
            break
        chunk = y[a:b]
        if np.sqrt(np.mean(chunk ** 2)) < 1e-5:
            continue
        pts.append((lvl, float(an.calculate_thd(chunk, 1000.0))))
    return pts


# ---- noise floor ------------------------------------------------------------
def noise_metrics(out_wav):
    y, sr = load(out_wav)
    y = y[int(0.3 * sr):]
    rms = float(np.sqrt(np.mean(y ** 2)))
    rms_db = 20 * np.log10(rms + 1e-12)
    nper = min(len(y), sr)
    from scipy.signal import welch
    f, psd = welch(y, fs=sr, nperseg=nper)
    return rms_db, f, 10 * np.log10(psd + 1e-20)


# ---- overall residual (level+latency aligned) -------------------------------
def residual_db(a_wav, b_wav):
    a, _ = load(a_wav)
    b, _ = load(b_wav)
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]
    # integer-lag align via cross-correlation
    N = 1 << (2 * n - 1).bit_length()
    corr = np.fft.irfft(np.fft.rfft(a, N) * np.conj(np.fft.rfft(b, N)), N)
    ms = 4096
    lags = np.concatenate([np.arange(0, ms + 1), np.arange(-ms, 0)])
    idx = np.concatenate([np.arange(0, ms + 1), N + np.arange(-ms, 0)])
    lag = int(lags[int(np.argmax(corr[idx]))])
    if lag > 0:
        a, b = a[lag:], b[:len(a) - 0][:len(a[lag:])]
        a = a[:len(b)]
    elif lag < 0:
        b = b[-lag:]; a = a[:len(b)]
    m = min(len(a), len(b)); a, b = a[:m], b[:m]
    # level-match b to a (least-squares scalar)
    g = np.dot(a, b) / (np.dot(b, b) + 1e-20)
    resid = a - g * b
    return 20 * np.log10(np.sqrt(np.mean(resid ** 2)) / (np.sqrt(np.mean(a ** 2)) + 1e-20) + 1e-20)


# ---- per-speed driver -------------------------------------------------------
def analyze_speed(speed, md):
    d = {p: os.path.join(RENDERS, p, str(speed)) for p in PLUGINS}
    for p in PLUGINS:
        if not os.path.isdir(d[p]):
            md.append(f"\n> _skipped {speed} IPS — no renders for {p}_\n")
            return
    os.makedirs(REPORT, exist_ok=True)
    md.append(f"\n## {speed} IPS\n")

    # -- frequency response --
    fig, ax = plt.subplots(figsize=(9, 4))
    feats = {}
    for p in PLUGINS:
        f, mag = freq_response(os.path.join(STIM, "sweep.wav"),
                               os.path.join(d[p], "sweep.wav"))
        feats[p] = fr_features(f, mag)
        ax.semilogx(f, mag, label=PLUGINS[p], color=COLORS[p], lw=1.6)
    ax.set(xlim=(20, 20000), ylim=(-12, 8), xlabel="Hz", ylabel="dB (level-matched)",
           title=f"Frequency response — {speed} IPS")
    ax.grid(True, which="both", alpha=0.3); ax.legend()
    fig.tight_layout(); fig.savefig(os.path.join(REPORT, f"fr_{speed}ips.png"), dpi=110); plt.close(fig)
    md.append("### Frequency response\n")
    md.append("| Metric | TapeMachine | UAD A800 | Δ |\n|---|---|---|---|")
    for k, unit in [("head_bump_db", "dB"), ("head_bump_hz", "Hz"), ("hf_minus3db_hz", "Hz")]:
        mv, uv = feats["tapemachine"][k], feats["uad"][k]
        md.append(f"| {k} | {mv} {unit} | {uv} {unit} | {round(mv - uv, 2)} |")
    md.append(f"\n![fr](fr_{speed}ips.png)\n")

    # -- THD vs level --
    fig, ax = plt.subplots(figsize=(9, 4))
    thd = {}
    for p in PLUGINS:
        pts = thd_curve(os.path.join(d[p], "thd_steps.wav"))
        thd[p] = dict(pts)
        if pts:
            xs, ys = zip(*pts)
            ax.plot(xs, ys, "-o", label=PLUGINS[p], color=COLORS[p])
    ax.set(xlabel="input level (dBFS)", ylabel="THD (%)", title=f"THD vs level — {speed} IPS")
    ax.grid(True, alpha=0.3); ax.legend()
    fig.tight_layout(); fig.savefig(os.path.join(REPORT, f"thd_{speed}ips.png"), dpi=110); plt.close(fig)
    md.append("### THD vs input level\n")
    md.append("| input dBFS | TapeMachine % | UAD % | Δ |\n|---|---|---|---|")
    for lvl in THD_LEVELS:
        mv, uv = thd["tapemachine"].get(lvl), thd["uad"].get(lvl)
        if mv is None or uv is None:
            continue
        md.append(f"| {lvl} | {mv:.3f} | {uv:.3f} | {mv - uv:+.3f} |")
    md.append(f"\n![thd](thd_{speed}ips.png)\n")

    # -- wow & flutter --
    fig, ax = plt.subplots(figsize=(9, 4))
    wf = {}
    for p in PLUGINS:
        m = measure_wow_flutter(os.path.join(d[p], "wf_3150.wav"), 3150.0)
        wf[p] = m
        ax.semilogx(m["_spectrum_fmod"][1:], np.array(m["_spectrum_pct"][1:]),
                    label=PLUGINS[p], color=COLORS[p], lw=1.4)
    ax.set(xlim=(0.3, 100), xlabel="modulation freq (Hz)", ylabel="deviation (%)",
           title=f"Wow & flutter spectrum (indicative) — {speed} IPS")
    ax.grid(True, which="both", alpha=0.3); ax.legend()
    fig.tight_layout(); fig.savefig(os.path.join(REPORT, f"wf_{speed}ips.png"), dpi=110); plt.close(fig)
    md.append("### Wow & flutter\n")
    md.append("_TapeMachine at shipped Wow=7 Flutter=3; the UAD A800's transport flutter is "
              "intrinsic (no user control). Headline = robust (MAD-based) pitch deviation, which "
              "was validated to track the Wow knob monotonically. The band-split and spectrum are "
              "INDICATIVE only — the FM-demod of the oversampled/saturated tone throws frequent "
              "glitches (see the p1–99 span), so treat sub-metrics as directional, not exact._\n")
    md.append("| Metric | TapeMachine | UAD A800 | Δ |\n|---|---|---|---|")
    hv_m, hv_u = wf["tapemachine"]["pitch_dev_robust_pct"], wf["uad"]["pitch_dev_robust_pct"]
    md.append(f"| **pitch dev (robust, %)** | **{hv_m:.3f}** | **{hv_u:.3f}** | {hv_m - hv_u:+.3f} |")
    for k, lbl in [("wow_pct_indicative", "wow 0.5–6 Hz (indic.)"),
                   ("flutter_pct_indicative", "flutter 6–100 Hz (indic.)"),
                   ("p1_99_span_pct", "p1–99 span (glitch gauge)"),
                   ("glitch_pct", "glitch fraction (%)")]:
        mv, uv = wf["tapemachine"][k], wf["uad"][k]
        md.append(f"| {lbl} | {mv:.3f} | {uv:.3f} | {mv - uv:+.3f} |")
    md.append(f"\n![wf](wf_{speed}ips.png)\n")

    # -- noise floor -- (hiss = silence in with tape noise enabled)
    fig, ax = plt.subplots(figsize=(9, 4))
    noise = {}
    for p in PLUGINS:
        rms_db, f, psd = noise_metrics(os.path.join(d[p], "hiss.wav"))
        noise[p] = rms_db
        ax.semilogx(f[1:], psd[1:], label=f"{PLUGINS[p]} ({rms_db:.1f} dBFS)", color=COLORS[p], lw=1.2)
    ax.set(xlim=(20, 20000), xlabel="Hz", ylabel="dB", title=f"Noise floor — {speed} IPS")
    ax.grid(True, which="both", alpha=0.3); ax.legend()
    fig.tight_layout(); fig.savefig(os.path.join(REPORT, f"noise_{speed}ips.png"), dpi=110); plt.close(fig)
    md.append("### Noise floor (silence in, tape noise enabled)\n")
    md.append("_Not level-matched: TapeMachine Noise Amount=50, UAD Noise=On — nominal settings, "
              "compared for spectral character._\n")
    md.append("| Metric | TapeMachine | UAD A800 | Δ |\n|---|---|---|---|")
    md.append(f"| broadband RMS | {noise['tapemachine']:.1f} dBFS | {noise['uad']:.1f} dBFS | "
              f"{noise['tapemachine'] - noise['uad']:+.1f} |")
    md.append(f"\n![noise](noise_{speed}ips.png)\n")

    # -- overall residual --
    res = residual_db(os.path.join(d["tapemachine"], "sweep.wav"),
                      os.path.join(d["uad"], "sweep.wav"))
    md.append("### Overall difference\n")
    md.append(f"Level+latency-aligned residual (sweep, mine vs UAD): **{res:.1f} dB** "
              f"(0 dB = totally different, −∞ = identical).\n")


def main():
    speeds = sys.argv[1:] or [15, 30]
    speeds = [int(s) for s in speeds]
    date = datetime.date.today().isoformat()
    md = [f"# TapeMachine A800 vs UAD Studer A800 — {date}\n",
          "Matched settings: Studer A800 / tape 456 / NAB / Repro / +6 dB cal / unity I-O, "
          "48 kHz, TapeMachine at 4× oversampling. Frequency-response curves are level-matched "
          "to 0 dB across 300 Hz–3 kHz. THD from a 1 kHz stepped-level tone. Wow & flutter by "
          "Hilbert FM-demodulation of a 3150 Hz tone.\n"]
    have = False
    for s in speeds:
        if any(os.path.isdir(os.path.join(RENDERS, p, str(s))) for p in PLUGINS):
            analyze_speed(s, md); have = True
    if not have:
        print("No renders found. Run render_ab.sh first.", file=sys.stderr)
        sys.exit(1)
    os.makedirs(REPORT, exist_ok=True)
    out = os.path.join(REPORT, f"comparison_{date}.md")
    with open(out, "w") as f:
        f.write("\n".join(md) + "\n")
    print(f"Wrote {out}")
    print("Plots in", REPORT)


if __name__ == "__main__":
    main()
