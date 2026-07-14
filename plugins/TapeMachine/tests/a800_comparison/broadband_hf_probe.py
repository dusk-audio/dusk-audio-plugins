#!/usr/bin/env python3
"""broadband_hf_probe.py — the TRUE broadband HF-loss curve mine-vs-UAD as a
function of program level, both machines (brightness campaign, 2026-07-12).

WHY: level_probe measures SINGLE-TONE fundamental-bin HF loss. A lone loud HF tone
SELF-COMPRESSES in the shaper (real, level-dependent) so mine droops ~5 dB @HF at
0 dBFS and the levelHfShelf restores it -> good single-tone match. But on BROADBAND
program the HF partials ride LOW on dominant LF/mid energy, are NOT self-compressed,
yet the shelf (keyed on the loud broadband peak) still boosts them -> a rising HF
tilt the user hears. This probe measures the ACTUAL broadband HF loss the shelf must
match: a sustained pink-weighted multitone (HF probe partials riding on louder LF/mid)
rendered at a sweep of PEAK levels, mine-vs-UAD, rel-1k LTAS tilt over the HF bands.

  + = mine brighter than UAD.  The shelf should be sized so mine-UAD ~ 0 here.

  python3 broadband_hf_probe.py           # A800 + ATR, current build
"""
import os, tempfile, shutil, subprocess
import numpy as np, soundfile as sf
HERE = os.path.dirname(os.path.abspath(__file__))
from ladder_probe import BIN, MINE, mine_base, uad_base, UAD_A800, UAD_ATR, SR
STIM = os.path.join(HERE, "stimuli"); os.makedirs(STIM, exist_ok=True)

PEAKS = [-18, -12, -9, -6, -3, 0]     # stimulus peak dBFS (sets flux / levelFactor)
THIRD = [100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000,
         2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000]


def synth(seconds=6.0, seed=5):
    """Sustained pink-weighted multitone: a dense LF/mid body carrying the energy
    plus steady HF probe partials at ~ -18 dB below the body (as in real program,
    where cymbals/air sit well below the fundamental bulk). Deterministic."""
    n = int(seconds * SR); t = np.arange(n) / SR
    x = np.zeros(n)
    # LF/mid body (carries the peak level) — pink-ish falling amplitude
    body = [55, 82.4, 110, 146.8, 196, 261.6, 349.2, 440, 587.3, 784, 1046.5, 1318.5]
    for k, f in enumerate(body):
        a = 1.0 / np.sqrt(f / 55.0)           # ~pink (-3 dB/oct amplitude)
        x += a * np.sin(2 * np.pi * f * t + 0.7 * k)
    # HF probe partials, aggregate HF RMS ~18 dB below the LF/mid body RMS (program-like).
    # Size hfscale from the incoherent RMS of the two sums (different freqs => sum of
    # a_i^2/2), not a bare 1/sqrt(N), so the 18 dB offset holds regardless of partial count.
    hfp = [2093, 2637, 3520, 4699, 6272, 8372, 11175, 14080]
    body_rms = np.sqrt(np.sum([(1.0 / np.sqrt(f / 55.0)) ** 2 for f in body]) / 2.0)
    hf_rms = body_rms * 10 ** (-18.0 / 20.0)
    hfscale = hf_rms / np.sqrt(len(hfp) / 2.0)
    for k, f in enumerate(hfp):
        x += hfscale * np.sin(2 * np.pi * f * t + 1.3 * k)
    x /= (np.max(np.abs(x)) + 1e-9)
    return x.astype(np.float32)


def render(plugin, params, src, mode="param"):
    tmp = tempfile.mkdtemp()
    cmd = [BIN, "--au", plugin, "--input-wav", src, "--slug", "s",
           "--output-dir", tmp, "--prerun-seconds", "2"]
    flag = "--param" if mode == "param" else "--nparam"
    for k, v in params:
        cmd += [flag, f"{k}={v}"]
    subprocess.run(cmd, capture_output=True, text=True)
    stem = os.path.join(tmp, "s_stem.wav")
    y, _ = sf.read(stem) if os.path.exists(stem) else (None, None)
    shutil.rmtree(tmp, ignore_errors=True)
    return y.mean(1) if (y is not None and y.ndim > 1) else y


def ltas(x):
    a = np.abs(x); thr = 0.02 * np.max(a)
    idx = np.where(a > thr)[0]; x = x[idx[0]:idx[-1]] if len(idx) else x
    f = np.fft.rfftfreq(len(x), 1 / SR)
    X = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    out = []
    for c in THIRD:
        lo, hi = c / 2 ** (1 / 6), c * 2 ** (1 / 6); m = (f >= lo) & (f < hi)
        out.append(10 * np.log10(np.sum(X[m]) + 1e-30))
    out = np.array(out); return out - out[THIRD.index(1000)]


def main():
    base = synth()
    hf = [i for i, c in enumerate(THIRD) if c >= 6300]
    for name, mi, uad in [("A800", "0", UAD_A800), ("ATR", "1", UAD_ATR)]:
        print(f"\n### {name}  broadband HF-loss (mine-UAD, rel-1k tilt; + = mine brighter)")
        print(f"  {'peak':>5} | " + " ".join(f"{c/1000:>4g}k" for c in THIRD if c >= 6300)
              + " | HFmean signed")
        for pk in PEAKS:
            src = os.path.join(STIM, "bb_hf.wav")
            sf.write(src, base * 10 ** (pk / 20.0), SR)
            ym = render(MINE, mine_base(mi), src)
            yu = render(uad, uad_base(), src)
            if ym is None or yu is None:
                print(f"  {pk:>5} | render fail"); continue
            L = min(len(ym), len(yu)); d = ltas(ym[:L]) - ltas(yu[:L])
            hfvals = " ".join(f"{d[i]:+5.1f}" for i in hf)
            print(f"  {pk:>4}d | {hfvals} | {np.mean([d[i] for i in hf]):+.2f}")


if __name__ == "__main__":
    main()
