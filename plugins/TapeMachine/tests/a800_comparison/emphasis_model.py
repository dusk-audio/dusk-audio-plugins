#!/usr/bin/env python3
"""
emphasis_model.py — quantitative model of the order-11 shaper with a G / 1-G
shelf pair wrapped tightly around it, to predict whether such a pair can move
the ATR/A800 5th harmonic + two-tone IMD toward the UAD WITHOUT regressing
THD@-6 or the 3rd harmonic.  (Campaign: emphasis-rebalance, 2026-07-11.)

Falsifying experiment (CLAUDE.md rule 6): if NO (fc,gain,Q) pair moves 5th
and IMD toward UAD while holding THD@-6 within +-0.1% and 3rd within ~1 dB,
the gates genuinely conflict -> STOP and report the trade curve.

Models the reference operating point of the real DSP chain:
  input * 0.95/dbToGain(6)  ->  preEmphasis  ->  biasFilter
    ->  [G]  ->  shaper(x*drive)*driveInv + even*sh^2  ->  [1/G]
    ->  deEmphasis
Post-shaper tape filters (gap/hf/repro) are ~0 dB at 1-5 kHz at the reference
and are omitted; deEmphasis is the dominant post-NL HF attenuator (the ~7 dB
that pulls the born-at-shaper -47 dB 5th down to the measured -53).
"""
import numpy as np

SR = 192000.0
N  = 192000            # 1 s -> 1 Hz bins, coherent for 1000 & 60 Hz
WARM = 9600            # 50 ms filter warm-up (50 cycles @1 kHz, 3 @60 Hz), discarded

# ---------------- shaper coefficients (from TapeMachineDSP.hpp) --------------
# ATR / Classic102
Cc = [1.025164, -0.008723668, 0.7539777, 0.1042891, -5.338438, -0.3726203,
      15.2137, 0.5746054, -21.00829, -0.3211741, 11.08413]
Cx0, CP0, CS0, Cknee = 0.78, 0.765386, 0.765175, 0.5
kClassicEven = 0.012
# A800 / Swiss800
Sc = [0.9793873, -0.004525098, -0.6470817, 0.05350971, 3.027688, -0.189431,
      -8.856463, 0.2903663, 12.42955, -0.1614584, -6.648037]
Sx0, SP0, SS0, Sknee = 0.8, 0.684929, 0.602271, 0.5
kA800Even = 0.008

def shaper(x, C, x0, P0, S0, knee):
    ax = np.abs(x)
    poly = np.zeros_like(x)
    for c in reversed(C):
        poly = poly * x + c
    poly = poly * x                      # horner gave sum c_k x^k with x factored: c1 x + ...
    kneev = np.sign(x) * (P0 + S0 * knee * np.tanh((ax - x0) / knee))
    return np.where(ax <= x0, poly, kneev)

# --- reference drives (updateFilters, 456 tape, bias 0.5, 15 IPS, cal +6) ----
tapeFormScale = 2.0 * (1.0 - 0.88) + 0.6      # 456 saturationPoint 0.88 -> 0.84
CLASSIC_DRIVE = 2.5 * tapeFormScale * 1.0     # kClassicDriveBase 2.5
A800_DRIVE    = 2.8 * tapeFormScale * 1.0
PRESCALE = 0.95 / (10 ** (6.0 / 20.0))        # cal +6 flux attenuation

# ---------------- first-order tape pre/de-emphasis (TapeEQFilter) -----------
def tapeeq(x, tau_num_us, tau_den_us):
    T = 1.0 / SR
    wN = 2.0 * (tau_num_us * 1e-6) / T
    wD = 2.0 * (tau_den_us * 1e-6) / T
    b0 = (1.0 + wN) / (1.0 + wD)
    b1 = (1.0 - wN) / (1.0 + wD)
    a1 = (1.0 - wD) / (1.0 + wD)
    y = np.empty_like(x); z1 = 0.0
    for n in range(len(x)):
        o = b0 * x[n] + z1
        z1 = b1 * x[n] - a1 * o
        y[n] = o
    return y

# NAB 15 IPS: preEmphasis (125,50), deEmphasis (50,125)
PRE = (125.0, 50.0)
DEE = (50.0, 125.0)

# ---------------- RBJ shelves (DBiquad::shelf) & bias high-shelf ------------
def rbj_shelf(x, fs, freq, gainDb, Q, high):
    A  = 10.0 ** (gainDb / 40.0)
    w0 = 2 * np.pi * freq / fs
    cw, sw = np.cos(w0), np.sin(w0)
    alpha = sw / (2 * Q)
    sqA2a = 2 * np.sqrt(A) * alpha
    if high:
        b0 =  A * ((A + 1) + (A - 1) * cw + sqA2a)
        b1 = -2 * A * ((A - 1) + (A + 1) * cw)
        b2 =  A * ((A + 1) + (A - 1) * cw - sqA2a)
        a0 =      (A + 1) - (A - 1) * cw + sqA2a
        a1 =  2 * ((A - 1) - (A + 1) * cw)
        a2 =      (A + 1) - (A - 1) * cw - sqA2a
    else:
        b0 =  A * ((A + 1) - (A - 1) * cw + sqA2a)
        b1 =  2 * A * ((A - 1) - (A + 1) * cw)
        b2 =  A * ((A + 1) - (A - 1) * cw - sqA2a)
        a0 =      (A + 1) + (A - 1) * cw + sqA2a
        a1 = -2 * ((A - 1) + (A + 1) * cw)
        a2 =      (A + 1) + (A - 1) * cw - sqA2a
    b0, b1, b2, a1, a2 = b0/a0, b1/a0, b2/a0, a1/a0, a2/a0
    y = np.empty_like(x); z1 = z2 = 0.0
    for n in range(len(x)):
        o = b0 * x[n] + z1
        z1 = b1 * x[n] - a1 * o + z2
        z2 = b2 * x[n] - a2 * o
        y[n] = o
    return y

# ---------------- G / 1-G pair: independent low-shelf + high-shelf ----------
# G  = pre-shaper:  low-shelf(+lfDb @ lfFc)  high-shelf(hfDb @ hfFc)
# 1/G = post-shaper: exact inverse (negate both gains)
def apply_G(x, lfDb, lfFc, lfQ, hfDb, hfFc, hfQ):
    if lfDb != 0.0: x = rbj_shelf(x, SR, lfFc, lfDb, lfQ, high=False)
    if hfDb != 0.0: x = rbj_shelf(x, SR, hfFc, hfDb, hfQ, high=True)
    return x
def apply_Ginv(x, lfDb, lfFc, lfQ, hfDb, hfFc, hfQ):
    if hfDb != 0.0: x = rbj_shelf(x, SR, hfFc, -hfDb, hfQ, high=True)
    if lfDb != 0.0: x = rbj_shelf(x, SR, lfFc, -lfDb, lfQ, high=False)
    return x

# ---------------- full reference chain around the shaper --------------------
def chain(x, machine, G=None):
    C, x0, P0, S0, knee, drive, even = (
        (Cc, Cx0, CP0, CS0, Cknee, CLASSIC_DRIVE, kClassicEven) if machine == "ATR"
        else (Sc, Sx0, SP0, SS0, Sknee, A800_DRIVE, kA800Even))
    biasFc = 7000.0 if machine == "ATR" else 8000.0
    biasDb = 1.5 if machine == "ATR" else 1.0
    sig = x * PRESCALE
    sig = tapeeq(sig, *PRE)
    sig = rbj_shelf(sig, SR, biasFc, biasDb, 0.707, high=True)   # bias shelf
    if G: sig = apply_G(sig, **G)
    driveInv = 1.0 / drive
    sh = shaper(sig * drive, C, x0, P0, S0, knee) * driveInv
    sh = sh + even * (sh * sh)
    if G: sh = apply_Ginv(sh, **G)
    out = tapeeq(sh, *DEE)
    return out

# ---------------- measurement -----------------------------------------------
def _spec(x):
    w = np.hanning(len(x))
    X = np.abs(np.fft.rfft(x * w))
    f = np.fft.rfftfreq(len(x), 1 / SR)
    return f, X
def _lvl(f, X, hz, bw=4):
    k = int(np.argmin(np.abs(f - hz)))
    return float(np.max(X[max(0, k - bw):k + bw + 1]))

def harmonics(machine, level_db, G=None):
    A = 10 ** (level_db / 20.0)
    t = np.arange(N + WARM) / SR
    x = A * np.sin(2 * np.pi * 1000 * t)
    out = chain(x, machine, G)[WARM:]   # drop filter warm-up before analysis
    f, X = _spec(out)
    f0 = _lvl(f, X, 1000)
    h = {n: 20 * np.log10(_lvl(f, X, 1000 * n) / f0 + 1e-15) for n in range(2, 8)}
    thd = np.sqrt(sum((10 ** (h[n] / 20)) ** 2 for n in range(2, 8))) * 100
    return h, thd

def imd(machine, level_db, G=None):
    A = 10 ** (level_db / 20.0)
    t = np.arange(N) / SR
    lo = 0.8 * np.sin(2 * np.pi * 60 * t)
    hi = 0.2 * np.sin(2 * np.pi * 1000 * t)
    s = lo + hi
    s = s / np.max(np.abs(s)) * A
    out = chain(s, machine, G)
    f, X = _spec(out)
    carr = _lvl(f, X, 1000)
    side = np.sqrt(sum(_lvl(f, X, 1000 + k * 60) ** 2 + _lvl(f, X, 1000 - k * 60) ** 2
                       for k in range(1, 6)))
    return float(side / (carr + 1e-15) * 100.0)


if __name__ == "__main__":
    import sys, itertools
    UAD = {"ATR": {"5th": -47.0, "thd6": 0.88, "imd3": 11.47, "imd6": 4.20, "imd12": 2.25},
           "A800": {"5th": -50.0, "thd6": 0.88, "imd3": 10.29, "imd6": 5.61, "imd12": 3.09}}

    def report(machine, G, tag):
        h6, thd6 = harmonics(machine, -6, G)
        h3, thd3 = harmonics(machine, -3, G)
        i12 = imd(machine, -12, G); i6 = imd(machine, -6, G); i3 = imd(machine, -3, G)
        print(f"[{machine} {tag}] "
              f"3rd@-6 {h6[3]:+6.1f}  5th@-6 {h6[5]:+6.1f}  7th@-6 {h6[7]:+6.1f}  "
              f"THD@-6 {thd6:5.2f}%  | IMD -12/-6/-3 {i12:5.2f}/{i6:5.2f}/{i3:5.2f}%")
        return dict(t3=h6[3], t5=h6[5], t7=h6[7], thd6=thd6, i12=i12, i6=i6, i3=i3)

    for m in ("ATR", "A800"):
        base = report(m, None, "baseline")
        u = UAD[m]
        print(f"      UAD target: 5th {u['5th']:+.0f}  THD@-6 {u['thd6']}%  "
              f"IMD -12/-6/-3 {u['imd12']}/{u['imd6']}/{u['imd3']}%\n")

    if "--solve" in sys.argv:
        # Combined per-machine G: LF pair (IMD lever) + HF pair (5th lever, NEGATIVE gain).
        CAND = {
            "ATR":  [dict(lfDb=lf, lfFc=150, lfQ=0.5, hfDb=hf, hfFc=3600, hfQ=0.5)
                     for lf in (2.5, 3.0, 3.5) for hf in (-7, -9, -11)],
            "A800": [dict(lfDb=lf, lfFc=150, lfQ=0.5, hfDb=hf, hfFc=3600, hfQ=0.5)
                     for lf in (2.0, 2.5, 3.0) for hf in (-6, -8, -10)],
        }
        for m in ("ATR", "A800"):
            u = UAD[m]
            b6 = harmonics(m, -6); base3 = b6[0][3]
            print(f"\n=== {m} combined-G solve  (targets: 5th {u['5th']} IMD "
                  f"{u['imd12']}/{u['imd6']}/{u['imd3']} THD@-6 {b6[1]:.2f} 3rd {base3:.1f}) ===")
            for G in CAND[m]:
                h6, thd6 = harmonics(m, -6, G)
                i12, i6, i3 = imd(m, -12, G), imd(m, -6, G), imd(m, -3, G)
                print(f"  lf+{G['lfDb']:.1f}@150 hf{G['hfDb']:+d}@3600: "
                      f"3rd {h6[3]:+6.1f}(d{h6[3]-base3:+.1f}) 5th {h6[5]:+6.1f} 7th {h6[7]:+6.1f} "
                      f"THD {thd6:5.2f}%(d{thd6-b6[1]:+.2f}) IMD {i12:5.2f}/{i6:5.2f}/{i3:5.2f}")
        sys.exit(0)

    if "--sweep" in sys.argv:
        machine = "ATR"
        base = harmonics(machine, -6)[0]
        base_thd = harmonics(machine, -6)[1]
        base3, base5 = base[3], base[5]
        print(f"\n=== {machine} G-sweep (HF pair only: 5th lever) baseline 3rd {base3:.1f} 5th {base5:.1f} THD {base_thd:.2f}% ===")
        for hfFc in (3200, 3600, 4000, 4500):
            for hfDb in (2, 4, 6, 8):
                G = dict(lfDb=0, lfFc=120, lfQ=0.5, hfDb=hfDb, hfFc=hfFc, hfQ=0.5)
                h, thd = harmonics(machine, -6, G)
                d3, d5 = h[3] - base3, h[5] - base5
                print(f"  hfFc {hfFc} hfDb +{hfDb}: 3rd {h[3]:+6.1f}(d{d3:+.1f}) "
                      f"5th {h[5]:+6.1f}(d{d5:+.1f}) 7th {h[7]:+6.1f} THD {thd:5.2f}% (dTHD {thd-base_thd:+.2f})")
        print(f"\n=== {machine} G-sweep (LF pair only: IMD lever) baseline IMD-3 {imd(machine,-3):.2f}% ===")
        b_i3 = imd(machine, -3); b_thd = harmonics(machine, -6)[1]
        for lfFc in (100, 150, 220):
            for lfDb in (2, 4, 6):
                G = dict(lfDb=lfDb, lfFc=lfFc, lfQ=0.5, hfDb=0, hfFc=4000, hfQ=0.5)
                i3 = imd(machine, -3, G); i6 = imd(machine, -6, G)
                thd = harmonics(machine, -6, G)[1]
                print(f"  lfFc {lfFc} lfDb +{lfDb}: IMD-6 {i6:5.2f} IMD-3 {i3:5.2f}%(d{i3-b_i3:+.2f}) "
                      f"THD@-6 {thd:5.2f}% (dTHD {thd-b_thd:+.2f})")
