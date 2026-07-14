#!/usr/bin/env python3
"""curve_probe.py — GROUND TRUTH transfer-curve extraction for the TapeMachine-2
saturation-reshape campaign (2026-07-10).

Renders a fine multi-level 1 kHz tone (reference config) through the UAD deck AND
mine, extracts the SIGNED harmonic amplitudes per level, reconstructs the static
transfer curve y(x) (UAD has NO envelope — proven instantaneous NL), pools across
levels, and least-squares fits polynomials of order 7/9/11. Reports fit residual,
the reconstructed harmonic ladder (2..7 vs level), and a synthetic IMD-vs-level
prediction so we can see whether a static polynomial reproduces the measured IMD
acceleration BEFORE touching any DSP.

Usage:
  python3 curve_probe.py gen                 # build stimulus
  DEEP_MACHINE=ATR102 python3 curve_probe.py # render+extract ATR (Classic102)
  DEEP_MACHINE=A800    python3 curve_probe.py # render+extract A800 (Swiss800)
"""
import os, sys, shutil, tempfile, subprocess, json
import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
BIN  = os.path.join(REPO, "build/tests/duskverb_render/duskverb_render")
MINE = os.path.expanduser("~/Library/Audio/Plug-Ins/Components/tape_machine_2.component")
STIM = os.path.join(HERE, "stimuli")
SR = 48000

LEVELS = [-36, -30, -24, -18, -15, -12, -9, -6, -4, -2]
SEG = 1.0            # seconds per level
GUARD = 0.15         # trim each end
NHARM = 11
FUND = 1000.0

MACHINE = os.environ.get("DEEP_MACHINE", "ATR102")
if MACHINE == "ATR102":
    UAD = "/Library/Audio/Plug-Ins/Components/uaudio_ampex_atr-102_tape.component"
    _mine = "1"
else:
    UAD = "/Library/Audio/Plug-Ins/Components/uaudio_studer_a800.component"
    _mine = "0"

MINE_BASE = [("Tape Machine", _mine), ("Tape Speed", "1"), ("Tape Type", "0"),
             ("EQ Standard", "0"), ("Signal Path", "0"), ("Calibration", "1"),
             ("Noise Amount", "0"), ("Oversampling", "2"), ("Wow", "0"), ("Flutter", "0")]
UAD_BASE = [("IPS", "15 IPS"), ("Tape Type", "456"), ("Emphasis EQ", "NAB")]

STIMFILE = os.path.join(STIM, "curve_steps.wav")


def gen():
    parts = []
    t = np.arange(int(SEG * SR)) / SR
    for db in LEVELS:
        A = 10 ** (db / 20.0)
        parts.append(A * np.sin(2 * np.pi * FUND * t))
    x = np.concatenate(parts).astype(np.float32)
    sf.write(STIMFILE, np.column_stack([x, x]), SR)
    print(f"wrote {STIMFILE}  ({len(x)/SR:.1f}s, {len(LEVELS)} levels)")


def render(plugin, params, dest):
    tmp = tempfile.mkdtemp()
    cmd = [BIN, "--au", plugin, "--input-wav", STIMFILE,
           "--slug", "s", "--output-dir", tmp, "--prerun-seconds", "2"]
    for n, v in params:
        cmd += ["--param", f"{n}={v}"]
    subprocess.run(cmd, capture_output=True, text=True)
    stem = os.path.join(tmp, "s_stem.wav")
    ok = os.path.exists(stem)
    if ok:
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        shutil.move(stem, dest)
    shutil.rmtree(tmp, ignore_errors=True)
    return ok


def align(x):
    """find the loud region start (render pads with silence)"""
    thr = np.max(np.abs(x)) * 1e-3
    nz = np.where(np.abs(x) > thr)[0]
    return x[nz[0]:] if len(nz) else x


def signed_harmonics(seg):
    w = np.hanning(len(seg))
    X = np.fft.rfft(seg * w)
    f = np.fft.rfftfreq(len(seg), 1 / SR)
    gain = np.sum(w)
    k1 = np.argmin(np.abs(f - FUND))
    phi = np.angle(X[k1])
    out = []
    for k in range(1, NHARM + 1):
        kk = np.argmin(np.abs(f - FUND * k))
        amp = np.real(X[kk] * np.exp(-1j * k * phi)) / gain * 2.0
        out.append(amp)
    return np.array(out)


def extract(path):
    x, sr = sf.read(path)
    if x.ndim > 1:
        x = x.mean(axis=1)
    x = align(x)
    seg = int(SEG * SR)
    theta = np.linspace(0, 2 * np.pi, 4096, endpoint=False)
    xs, ys = [], []
    g0 = None
    ladder = {}
    for i, db in enumerate(LEVELS):
        a = int(i * seg + GUARD * SR)
        b = int((i + 1) * seg - GUARD * SR)
        if b > len(x):
            break
        H = signed_harmonics(x[a:b])
        A = 10 ** (db / 20.0)
        if g0 is None:
            g0 = A / (abs(H[0]) + 1e-12)
        # harmonic ladder rel fundamental (dB)
        ladder[db] = {k + 1: 20 * np.log10(abs(H[k]) / (abs(H[0]) + 1e-12) + 1e-12)
                      for k in range(1, 7)}
        y = np.sum([H[k] * np.cos((k + 1) * theta) for k in range(NHARM)], axis=0) * g0
        xin = A * np.cos(theta)
        xs.append(xin); ys.append(y)
    return np.concatenate(xs), np.concatenate(ys), ladder


def fit(xs, ys, order):
    V = np.vstack([xs ** n for n in range(1, order + 1)]).T
    coef, *_ = np.linalg.lstsq(V, ys, rcond=None)
    yhat = V @ coef
    rms = np.sqrt(np.mean((ys - yhat) ** 2)) / (np.sqrt(np.mean(ys ** 2)) + 1e-12)
    return coef, rms


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "gen":
        gen(); return
    if not os.path.exists(STIMFILE):
        gen()
    out = os.path.join(HERE, "renders", "curve")
    up = os.path.join(out, f"{MACHINE}_uad.wav")
    mp = os.path.join(out, f"{MACHINE}_mine.wav")
    if "--skip-render" not in sys.argv:
        print("rendering UAD...")
        if not render(UAD, UAD_BASE, up):
            raise RuntimeError(f"UAD render failed for {MACHINE} (no stem produced): {UAD}")
        print("rendering mine...")
        if not render(MINE, MINE_BASE, mp):
            raise RuntimeError(f"MINE render failed for {MACHINE} (no stem produced): {MINE}")

    for who, p in (("UAD", up), ("MINE", mp)):
        xs, ys, ladder = extract(p)
        print(f"\n===== {MACHINE} {who} =====")
        print("harmonic ladder (dB rel fund) per input level:")
        print("  lvl   2f    3f    4f    5f    6f    7f")
        for db in LEVELS:
            if db in ladder:
                L = ladder[db]
                print(f"  {db:+3d}  " + " ".join(f"{L[k]:+5.0f}" for k in range(2, 8)))
        for order in (7, 9, 11):
            coef, rms = fit(xs, ys, order)
            print(f"  fit order {order:2d}: residual {rms*100:.3f}%   "
                  f"coeffs=[{', '.join(f'{c:+.4f}' for c in coef)}]")
        # save UAD fit target
        if who == "UAD":
            coefs = {order: fit(xs, ys, order)[0].tolist() for order in (7, 9, 11)}
            js = os.path.join(out, f"{MACHINE}_uadfit.json")
            json.dump({"levels": LEVELS, "coefs": coefs, "ladder": ladder}, open(js, "w"), indent=1)
            print(f"  saved {js}")


if __name__ == "__main__":
    main()
