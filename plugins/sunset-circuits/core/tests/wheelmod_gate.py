#!/usr/bin/env python3
"""Oracle Wheel Mod gate (#303): the section, rendered through the whole synth.

core/WheelMod.hpp is checked on its own by wheelmod_test.cpp. This gate renders
notes through render_test and measures what comes out, against the reference
model's measurements of 2026-11-17 (WheelMod.hpp gives each number's origin),
using the same estimators the reference was measured with. Tolerances were set
before the first run.

  [off]      no destination on: changing every other wheel setting and moving the
             wheel renders byte-identically to the defaults. Control: one
             destination on changes the render.
  [modes]    the section is Oracle-only: Cosmos, Mono, Modular, Prism and Acid
             with all five destinations on at full depth render byte-identically
             to the same patch without them.
  [shapes]   wheel -> osc B frequency at full wheel, LFO 0.163 Hz, every shape
             combination: least-squares fit cents = D + S*saw + T*tri + Q*square
             over the LFO phase; every coefficient within 5 cents of the
             reference's fit (zero-crossing pitch track, A4 sine carrier).
  [freqA]    wheel -> osc A frequency, square: +804.1 c (+-5; the reference's own
             osc A read +811, a 0.9 % difference the model does not keep).
  [depth]    half wheel (64/127): the triangle swing scales to 64/127 (+-5 c).
  [rate]     LFO rate 4.8195 Hz (knob 75): period within 1 % of the reference's
             4.795 Hz.
  [pw]       wheel -> PW A, square, from a 10 % pulse at wheel 32: duty moves by
             0.2016 (+-0.005; reference 0.2021). From a 50 % pulse at full wheel
             the pulse CLOSES while the square is up. Read the way the reference
             was (channel mean, raw RMS of 20 ms windows from 0.5 s, the two-state
             split): the reference reads 33.25 .. 39.27 dB over seven realizations
             (prerun 0.5 / 1.5 / 2.5 / 3.5 / 4.5 s at 48 kHz, 0.5 s at 96 and
             44.1 kHz; mean 36.89, SD 2.65). Pass band, set from that spread before
             the model was read with this estimator: the realizations' range
             widened by one SD each side, 30.6 .. 41.9 dB. This reading is mostly
             the DC step of the closing decaying through the output's low-frequency
             coupling, so it follows that coupling as much as the pulse.
  [filter]   wheel -> filter, self-oscillating filter at 300 Hz, square at wheel
             16: the two pitch states are 1.113 octaves apart (+-0.03; reference
             1.1127).
  [noise]    noise -> osc B frequency at full wheel, A7 sine carrier, 160 s at each
             of 48 / 96 / 44.1 kHz (three realizations, as the reference),
             zero-crossing track resampled to 3 kHz (the reference pipeline): RMS
             70.87 c (+-3 %), mean within 2 c, skew -0.956 (+-0.05), excess
             kurtosis 2.07 (+-0.25); deviation spectrum, dB re 4-16 Hz, within
             0.75 dB of the reference in the 2..512 Hz bands and 1.5 dB in the
             0.25..1 Hz bands (still a noisy estimate there). Reference = three
             160 s realizations (48 / 96 / 44.1 kHz) pooled; a single realization
             is not a fixed target (skew -0.82..-1.09, kurtosis 1.91..2.36), and
             160 s is the length of each: one 60 s window of the model spreads
             +-0.021 in skew and +-0.087 in kurtosis (SD over 20 windows).
  [sh]       S&H, LFO knob 75 (4.8 Hz), full wheel, A4 carrier, 90 s, as the
             reference was measured: steps uniform between +-801 c (extremes
             +-15 c, excess kurtosis -1.20 +-0.15; reference 411 steps).
  [shared]   one LFO and one noise for every voice: two voices (A5 and A7, the
             second started 0.37 s later), deviation correlation through
             band_track. The reference, rendered with exactly this design (osc B
             triangle, its nearest to a sine), reads 0.999942 .. 0.999961 for noise
             (five realizations: prerun 0.5 / 1.5 / 2.5 s at 48 kHz, 0.5 s at 96 and
             44.1 kHz) and 0.9999993 for the square LFO (knob 75, wheel 32; two
             realizations). Bound: decorrelation 1 - r at most ten times the
             reference's worst, r >= 0.99942 (noise) and r >= 0.9999933 (LFO); what
             remains at the reference's level is the estimator's own floor (its
             150..300 Hz tracking), not a per-voice source. Carriers this high are
             needed: on an A3 carrier the instantaneous frequency cannot follow the
             noise's 150..300 Hz content (A3 + A6 reads 0.828 / 0.830 on the
             reference and 0.835 on the model).
  [os]       the modulation is the same at 1x / 2x / 4x: noise deviation tracks
             correlate >= 0.999 and their RMS agree within 0.5 %.

Each check prints its measured value; failing ones say so. Exit 1 on any fail.
"""
import sys

import numpy as np
from scipy import signal, stats

from _harness import render

CARRIER_B = dict(osc1Level=0, osc2Level=1, osc2Wave=3, osc2Detune=0, noiseLevel=0,
                 filterCutoff=20000, filterRes=0, filterEnvAmt=0, analogAmt=0, vintage=0,
                 unisonVoices=1, ampA=0.001, ampD=0.2, ampS=1, ampR=0.3)
CARRIER_A = dict(CARRIER_B, osc1Level=1, osc1Wave=3, osc1Detune=0, osc2Level=0)
SLOW = 0.16309            # LFO knob 25 under the 0.03..26.2 Hz law (reference 0.1628)
ALL_ON = dict(wheelFreqA=1, wheelFreqB=1, wheelPwA=1, wheelPwB=1, wheelFilter=1,
              wheelLfoSaw=1, wheelLfoSquare=1, wheelSourceMix=0.5, wheelLowerLimit=1, wheelLfoRate=5)

fails = []


def report(tag, ok, text):
    print(f"[{tag}] {'PASS' if ok else 'FAIL'}: {text}")
    if not ok:
        fails.append(tag)


def zc_track(x, fs, t_from=0.3):
    """Rising zero crossings of a sine carrier -> (t, f) one per cycle."""
    y = x[int(t_from * fs):, 0]
    y = signal.sosfiltfilt(signal.butter(2, 5.0, "high", fs=fs, output="sos"), y)
    i = np.flatnonzero((y[:-1] < 0) & (y[1:] >= 0))
    tz = (i + (-y[i]) / (y[i + 1] - y[i])) / fs
    return 0.5 * (tz[1:] + tz[:-1]) + t_from, 1.0 / np.diff(tz)


def cents_re(x, fs, f0, t_from=0.3):
    t, f = zc_track(x, fs, t_from)
    return t, 1200.0 * np.log2(f / f0)


def f0_of(x, fs):
    return float(np.median(zc_track(x, fs)[1]))


def fit_shapes(t, c, use, f=SLOW):
    """Least squares over the LFO phase (the engine's LFO starts at phase 0 on frame 0)."""
    phi = np.mod(f * t, 1.0)
    saw, sq = phi, (phi < 0.5).astype(float)
    tri = np.where(phi < 0.5, 0.5 - 2 * phi, -1.5 + 2 * phi)
    A = np.stack([np.ones_like(t)] + [b for b, u in zip((saw, tri, sq), use) if u], 1)
    keep = np.ones(len(t), bool)
    for _ in range(3):   # drop the transition cycle at each step
        coef, *_ = np.linalg.lstsq(A[keep], c[keep], rcond=None)
        r = c - A @ coef
        keep = np.abs(r) < max(15.0, 4 * np.std(r[keep]))
    return coef


def two_states(v):
    lo, hi = np.percentile(v, [1, 99])
    s = np.sort(v[(v >= lo) & (v <= hi)])
    k = int(np.argmax(np.diff(s)))
    return float(np.mean(s[:k + 1])), float(np.mean(s[k + 1:]))


def check_off_and_modes():
    fs, ref = render(1, 57, 1.5, 2, "wm_off_ref", **CARRIER_B)
    _, other = render(1, 57, 1.5, 2, "wm_off_other", wheelLfoSaw=1, wheelLfoSquare=1, wheelLfoRate=20,
                      wheelSourceMix=0.6, wheelNoiseSH=1, wheelLowerLimit=0.8, wheelUpperLimit=0.1,
                      modwheel=1, modwheelat=["0.5:0.2", "1.0:0.9"], **CARRIER_B)
    _, ctl = render(1, 57, 1.5, 2, "wm_off_ctl", wheelFreqB=1, modwheel=1, **CARRIER_B)
    report("off", np.array_equal(ref, other) and not np.array_equal(ref, ctl),
           f"no destination: byte-identical to the defaults ({np.array_equal(ref, other)}); "
           f"control, Freq B on changes the render ({not np.array_equal(ref, ctl)})")
    same = []
    for mode in (0, 2, 3, 4, 5):
        _, a = render(mode, 45, 1.0, 2, f"wm_mode{mode}_off", noiseLevel=0.05)
        _, b = render(mode, 45, 1.0, 2, f"wm_mode{mode}_on", noiseLevel=0.05, **ALL_ON)
        same.append(np.array_equal(a, b))
    _, a = render(1, 45, 1.0, 2, "wm_mode1_off", noiseLevel=0.05)
    _, b = render(1, 45, 1.0, 2, "wm_mode1_on", noiseLevel=0.05, **ALL_ON)
    report("modes", all(same) and not np.array_equal(a, b),
           f"all destinations on, byte-identical in Cosmos/Mono/Modular/Prism/Acid: {same}; "
           f"control, Oracle changes: {not np.array_equal(a, b)}")


def check_shapes():
    fs, base = render(1, 69, 2.0, 2, "wm_f0", **CARRIER_B)
    f0 = f0_of(base, fs)
    # Reference fits (cents re the unmodulated note, full wheel); all three from the
    # half-wheel render scaled by 127/64, whose phase fit is the clean one.
    cases = [((1, 0, 0), (2.39, 1175.06)), ((0, 1, 0), (42.92, 1182.80)), ((0, 0, 1), (0.00, 804.09)),
             ((1, 0, 1), (0.24, 1046.40, 627.18)), ((0, 1, 1), (45.23, 1182.84, 1272.96)),
             ((1, 1, 0), (42.84, 1866.94, 1182.94)), ((1, 1, 1), (47.31, 1660.34, 1183.05, 990.59))]
    names = {0: "saw", 1: "tri", 2: "square"}
    for use, want in cases:
        label = "+".join(names[i] for i in range(3) if use[i])
        fs, x = render(1, 69, 7.0, 2, f"wm_shape_{label}", wheelFreqB=1, modwheel=1, wheelLfoRate=SLOW,
                       wheelLfoSaw=use[0], wheelLfoTri=use[1], wheelLfoSquare=use[2], **CARRIER_B)
        t, c = cents_re(x, fs, f0)
        got = fit_shapes(t, c, use)
        err = np.max(np.abs(got - np.array(want)))
        report("shapes", err <= 5.0, f"{label:16s} fit {np.round(got, 1).tolist()} vs reference "
               f"{list(want)} (worst {err:.1f} c)")
    fs, base = render(1, 69, 2.0, 2, "wm_f0a", **CARRIER_A)
    f0a = f0_of(base, fs)
    fs, x = render(1, 69, 7.0, 2, "wm_freqA", wheelFreqA=1, modwheel=1, wheelLfoRate=SLOW,
                   wheelLfoTri=0, wheelLfoSquare=1, **CARRIER_A)
    t, c = cents_re(x, fs, f0a)
    q = fit_shapes(t, c, (0, 0, 1))[1]
    report("freqA", abs(q - 804.09) <= 5.0, f"osc A square {q:+.1f} c (model 804.1; reference osc A +811)")
    fs, x = render(1, 69, 7.0, 2, "wm_half", wheelFreqB=1, modwheel=64 / 127, wheelLfoRate=SLOW, **CARRIER_B)
    t, c = cents_re(x, fs, f0)
    tri = fit_shapes(t, c, (0, 1, 0))[1]
    report("depth", abs(tri - 1182.80 * 64 / 127) <= 5.0, f"triangle at wheel 64: {tri:.1f} c "
           f"(full-wheel reference x 64/127 = {1182.80 * 64 / 127:.1f})")
    fs, x = render(1, 69, 3.0, 2, "wm_rate75", wheelFreqB=1, modwheel=1, wheelLfoRate=4.8195,
                   wheelLfoTri=0, wheelLfoSquare=1, **CARRIER_B)
    t, c = cents_re(x, fs, f0)
    up = t[1:][(c[1:] > 400) & (c[:-1] <= 400)]
    rate = 1.0 / np.median(np.diff(up))
    report("rate", abs(rate / 4.795 - 1) <= 0.01, f"knob-75 rate {rate:.3f} Hz (reference 4.795)")


def duty_track(x, fs, t_from=0.5, win=0.02):
    m = x[int(t_from * fs):, 0]
    W = int(win * fs)
    d = []
    for a in range(0, len(m) - W, W):
        s = m[a:a + W]
        lo, hi = np.percentile(s, [5, 95])
        d.append(np.mean(s > 0.5 * (lo + hi)))
    return np.array(d)


def closed_ratio_db(x, fs, t_from=0.5, win=0.02):
    """The reference's reading of a closing pulse: channel mean, raw RMS (DC included) of 20 ms windows, the
    two-state split, high over low in dB."""
    m = x.mean(axis=1)
    W = int(win * fs)
    rms = np.array([np.sqrt(np.mean(m[a:a + W] ** 2)) for a in range(int(t_from * fs), len(m) - W, W)])
    rl, rh = two_states(rms)
    return 20 * np.log10(rh / max(rl, 1e-12))


def closure_decay_ms(x, fs):
    # At A3 the open pulse has two large edges per cycle. A gap of half a
    # second marks a closed half-cycle. Average in 1 ms bins to read its DC tail.
    width = round(0.001 * fs)
    mono = x.mean(axis=1)
    bins = mono[:len(mono) // width * width].reshape(-1, width)
    spans = np.ptp(bins, axis=1)
    edges = np.flatnonzero(spans > 0.2 * np.percentile(spans, 99))
    if not edges.size:
        return []
    ends = np.append(edges[1:], len(bins))
    result = []
    for start in edges[(ends - edges) * width > 0.5 * fs] + 1:
        if start * width < 0.5 * fs:
            continue
        means = bins[start:start + 100].mean(axis=1)
        if abs(means[0]) < 1e-9:
            continue
        tail = means / means[0]
        below = np.flatnonzero(tail <= np.exp(-1.0))
        if len(below):
            k = below[0]
            frac = (tail[k - 1] - np.exp(-1.0)) / (tail[k - 1] - tail[k])
            result.append((k - 1 + frac) * width * 1000.0 / fs)
    return result


def check_pw_edges():
    # With the matrix already at a width limit, the wheel must start at that
    # audible width. Compare against a pulse dialled to the same limit by hand.
    for osc, number, carrier in (("A", 1, CARRIER_A), ("B", 2, CARRIER_B)):
        for width, velocity_amount in ((0.05, -0.8), (0.95, 0.8)):
            patch = dict(carrier, wheelLfoTri=0, wheelLfoSquare=1, wheelLfoRate=2)
            patch.update({f"osc{number}Wave": 4, f"osc{number}PW": width})
            routed = dict(patch, modSrc0=6, modDst0=2 + number, modAmt0=velocity_amount)
            dest = {"wheelPw" + osc: 1}
            fs, off = render(1, 57, 2, 2, "wm_edge_off", **routed)
            _, neutral = render(1, 57, 2, 2, "wm_edge_zero", **routed, **dest)
            _, moving = render(1, 57, 2, 2, "wm_edge_on", modwheel=0.25, **routed, **dest)
            _, expected = render(1, 57, 2, 2, "wm_edge_dialled", modwheel=0.25, **patch, **dest)
            # Skip the attack. Require an audible baseline and a working wheel
            # so silent or disconnected routes cannot satisfy the comparisons.
            tail = slice(fs, None)
            level = np.sqrt(np.mean(off[tail] ** 2))
            neutral_match = np.array_equal(off, neutral)
            moving_match = np.array_equal(moving[tail], expected[tail])
            movement = np.max(np.abs(moving[tail] - off[tail]))
            report("pw edge", level > 0.01 and neutral_match and moving_match and movement > 0.01,
                   f"PW {osc}, {width:.0%} edge: dry RMS {level:.4f}, zero identical {neutral_match}, "
                   f"wheel up matches dialled width {moving_match}, peak change {movement:.4f}")


def check_pw_filter():
    pulse = dict(CARRIER_A, osc1Wave=4, filterCutoff=20000)
    fs, x = render(1, 57, 14.0, 2, "wm_pw32", osc1PW=0.10, wheelPwA=1, wheelLfoTri=0, wheelLfoSquare=1,
                   wheelLfoRate=SLOW, modwheel=32 / 127, **pulse)
    d = duty_track(x, fs)
    lo, hi = two_states(d)
    report("pw", abs((hi - lo) - 0.2016) <= 0.005, f"square at wheel 32 from 10 %: duty {lo:.4f} -> {hi:.4f}, "
           f"moves {hi - lo:.4f} (reference 0.2021)")
    fs, x = render(1, 57, 14.0, 2, "wm_pwclose", osc1PW=0.50, wheelPwA=1, wheelLfoTri=0, wheelLfoSquare=1,
                   wheelLfoRate=SLOW, modwheel=1, **pulse)
    drop = closed_ratio_db(x, fs)
    report("pw", 30.6 <= drop <= 41.9, f"50 % pulse + square at full wheel: the over-the-top half reads {drop:.2f} dB "
           f"down (reference 33.25 .. 39.27 dB over seven realizations; pass 30.6 .. 41.9)")
    # Fourteen closures in seven reference captures: 31.30..36.16 ms. Allow
    # one 1 ms analysis bin on each side, fixed before reading Oracle.
    times = closure_decay_ms(x, fs)
    report("coupling", len(times) == 2 and all(30.29 <= t <= 37.16 for t in times),
           f"closed pulse, time to 1/e: {[round(float(t), 2) for t in times]} ms "
           "(reference 31.30 .. 36.16 ms)")
    selfosc = dict(osc1Level=0, osc2Level=0, noiseLevel=0.001, filterRes=1, filterCutoff=300,
                   filterEnvAmt=0, analogAmt=0, ampA=0.005, ampS=1, unisonVoices=1)
    fs, x = render(1, 57, 14.0, 2, "wm_filt16", wheelFilter=1, wheelLfoTri=0, wheelLfoSquare=1,
                   wheelLfoRate=SLOW, modwheel=16 / 127, **selfosc)
    t, f = zc_track(x, fs, 0.5)
    a, b = two_states(np.log2(f))
    report("filter", abs((b - a) - 1.1127) <= 0.03, f"square at wheel 16: {2 ** a:.1f} / {2 ** b:.1f} Hz, "
           f"{b - a:.4f} octaves (reference 1.1127)")


def welch_bands(c, fs, bands):
    c = c - np.mean(c)
    nper = min(1 << 15, 1 << int(np.log2(len(c))))
    f, p = signal.welch(c, fs, window="hann", nperseg=nper, noverlap=nper // 2, detrend="constant")
    ref = np.mean(p[(f >= 4) & (f <= 16)])
    return [10 * np.log10(np.mean(p[(f >= b / 2 ** 0.25) & (f < b * 2 ** 0.25)]) / ref) for b in bands]


def resampled_cents(x, fs, f0, fs_out=3000.0, t_from=1.0):
    t, f = zc_track(x, fs, t_from)
    tu = np.arange(t[0], t[-1], 1 / fs_out)
    return 1200.0 * np.log2(np.interp(tu, t, f) / f0)


def check_noise_sh():
    # Measured exactly as the reference was: one 160 s realization per host rate
    # (the noise runs at the host rate, so each rate is its own realization),
    # moments over the pooled samples, band levels averaged over the three.
    bands = [0.25, 0.5, 1, 2, 32, 48, 64, 96, 128, 180, 256, 362, 512]
    pooled, rows = [], []
    for sr in (48000, 96000, 44100):
        fs, base = render(1, 105, 3.0, 2, f"wm_a7_f0_{sr}", sr=sr, **CARRIER_B)
        f0 = f0_of(base, fs)
        fs, x = render(1, 105, 161.0, 2, f"wm_noise_a7_{sr}", sr=sr, wheelFreqB=1, wheelSourceMix=1,
                       modwheel=1, **CARRIER_B)
        c = resampled_cents(x, fs, f0)
        pooled.append(c - np.mean(c))
        rows.append(welch_bands(c, 3000.0, bands))
    c = np.concatenate(pooled)
    rms, mu = float(np.std(c)), float(np.mean([np.mean(v) for v in pooled]))
    sk, ku = float(stats.skew(c)), float(stats.kurtosis(c))
    ok = abs(rms / 70.87 - 1) <= 0.03 and abs(mu) <= 2.0 and abs(sk + 0.956) <= 0.05 and abs(ku - 2.07) <= 0.25
    report("noise", ok, f"RMS {rms:.2f} c (70.87), mean {mu:+.2f}, skew {sk:.3f} (-0.956), "
           f"excess kurtosis {ku:.2f} (2.07)")
    refdb = [2.05, 1.11, -0.41, 0.46, -0.30, -0.83, -1.38, -2.88, -4.59, -7.52, -11.59, -16.51, -22.20]
    got = list(np.mean(rows, axis=0))
    bad = [(b, round(g, 2), r) for b, g, r in zip(bands, got, refdb) if abs(g - r) > (1.5 if b < 2 else 0.75)]
    report("noise", not bad, "spectrum vs reference, dB: " + " ".join(f"{b:g}:{g:+.2f}/{r:+.2f}"
           for b, g, r in zip(bands, got, refdb)) + (f"  out: {bad}" if bad else ""))
    # S&H: A4 carrier, LFO knob 75, as the reference S&H was measured.
    fs, base4 = render(1, 69, 2.0, 2, "wm_a4_f0", **CARRIER_B)
    f4 = f0_of(base4, fs)
    fs, x = render(1, 69, 91.0, 2, "wm_sh", wheelFreqB=1, wheelSourceMix=1, wheelNoiseSH=1,
                   wheelLfoRate=4.8195, modwheel=1, **CARRIER_B)
    t, f = zc_track(x, fs, 1.0)
    cc = 1200.0 * np.log2(f / f4)
    cut = np.flatnonzero(np.abs(np.diff(cc)) > 25) + 1
    seg = np.split(cc, cut)
    lv = np.array([np.median(s[2:-2]) for s in seg if len(s) >= 8])
    ok = abs(lv.max() - 801) <= 15 and abs(lv.min() + 801) <= 15 and abs(stats.kurtosis(lv) + 1.2) <= 0.15
    report("sh", ok, f"{len(lv)} steps, {lv.min():.0f} .. {lv.max():.0f} c (+-801), excess kurtosis "
           f"{stats.kurtosis(lv):.3f} (-1.20)")


def band_track(x, fs, lo, hi, t_from=1.0):
    """Instantaneous frequency (cents) of one voice's sine, isolated by a band-pass;
    0.25 s dropped at each end, where the zero-phase filters ring."""
    y = x[int(t_from * fs):, 0]
    b = signal.sosfiltfilt(signal.butter(4, [lo, hi], "bandpass", fs=fs, output="sos"), y)
    ph = np.unwrap(np.angle(signal.hilbert(b)))
    f = np.diff(ph) * fs / (2 * np.pi)
    f = signal.sosfiltfilt(signal.butter(4, 300.0, "low", fs=fs, output="sos"), f)[::16]
    edge = int(0.25 * fs / 16)
    f = f[edge:-edge]
    return 1200.0 * np.log2(np.maximum(f, 1.0) / np.median(f))


def check_shared_os():
    two = dict(CARRIER_B, filterCutoff=20000)
    fs, x = render(1, 81, 12.0, 2, "wm_two_noise", wheelFreqB=1, wheelSourceMix=1, modwheel=1,
                   noteon="0.37:105", **two)
    a, b = band_track(x, fs, 500, 1500), band_track(x, fs, 2000, 6000)
    n = min(len(a), len(b))
    rn = float(np.corrcoef(a[:n], b[:n])[0, 1])
    fs, x = render(1, 81, 6.0, 2, "wm_two_lfo", wheelFreqB=1, wheelLfoTri=0, wheelLfoSquare=1, wheelLfoRate=5,
                   modwheel=0.25, noteon="0.37:105", **two)
    a, b = band_track(x, fs, 500, 1500), band_track(x, fs, 2000, 6000)
    n = min(len(a), len(b))
    rl = float(np.corrcoef(a[:n], b[:n])[0, 1])
    report("shared", rn >= 0.99942 and rl >= 0.9999933,
           f"two voices: noise correlation {rn:.6f} (reference 0.999942 .. 0.999961; >= 0.99942), "
           f"square LFO {rl:.7f} (reference 0.9999993; >= 0.9999933)")
    tracks = {}
    for osf in (1, 2, 4):
        fs, x = render(1, 93, 20.0, osf, f"wm_os{osf}", wheelFreqB=1, wheelSourceMix=1, modwheel=1, **CARRIER_B)
        tracks[osf] = band_track(x, fs, 1000, 4000)
    n = min(len(v) for v in tracks.values())
    r12 = float(np.corrcoef(tracks[1][:n], tracks[2][:n])[0, 1])
    r24 = float(np.corrcoef(tracks[2][:n], tracks[4][:n])[0, 1])
    s = [float(np.std(tracks[k][:n])) for k in (1, 2, 4)]
    ok = r12 >= 0.999 and r24 >= 0.999 and (max(s) / min(s) - 1) <= 0.005
    report("os", ok, f"noise deviation 1x/2x/4x: correlation {r12:.5f} / {r24:.5f}, RMS "
           f"{s[0]:.2f} / {s[1]:.2f} / {s[2]:.2f} c")


def main():
    check_off_and_modes()
    check_shapes()
    check_pw_edges()
    check_pw_filter()
    check_noise_sh()
    check_shared_os()
    print(f"\nwheelmod_gate: {'PASS' if not fails else 'FAIL'} ({len(fails)} failed checks)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
