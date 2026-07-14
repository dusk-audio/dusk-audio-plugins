#!/usr/bin/env python3
"""ladder_probe.py — RELIABLE multi-level harmonic ladder (2..7 vs level) for the
brightness/harmonic-distribution campaign (2026-07-12).

curve_probe.py's per-level extraction MISALIGNS on 'mine' (concatenated-render
alignment bug -> false -53 dB 3rd). This script instead reuses deep_probe's
VALIDATED fixed-segment windowing on thd_steps.wav (seg4=-6 matched the trusted
deep_probe/UAD gate exactly), extracting every segment. Each segment gets a peak
sanity check vs its nominal level to catch any misalignment (rule: a suspiciously
clean/wrong number is a bug, not data).

Levels in thd_steps.wav: seg0..5 = -30/-24/-18/-12/-6/-3 dBFS, 1.2 s each.

Usage:
  python3 ladder_probe.py            # A800 + ATR, NAB reference config
  CFG=CCIR python3 ladder_probe.py   # CCIR ("CRC") config
"""
import os, sys, shutil, tempfile, subprocess
import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
BIN  = os.path.join(REPO, "build/tests/duskverb_render/duskverb_render")
MINE = os.path.expanduser("~/Library/Audio/Plug-Ins/Components/tape_machine_2.component")
STIM = os.path.join(HERE, "stimuli")
SR = 48000
LEVELS = [-30, -24, -18, -12, -6, -3]
FUND = 1000.0

CFG = os.environ.get("CFG", "NAB")           # NAB | CCIR
UAD_A800 = "/Library/Audio/Plug-Ins/Components/uaudio_studer_a800.component"
UAD_ATR  = "/Library/Audio/Plug-Ins/Components/uaudio_ampex_atr-102_tape.component"

def mine_base(mi):
    eq = "0" if CFG == "NAB" else "1"
    return [("Tape Machine", mi), ("Tape Speed", "1"), ("Tape Type", "0"),
            ("EQ Standard", eq), ("Signal Path", "0"), ("Calibration", "1"),
            ("Noise Amount", "0"), ("Oversampling", "2"), ("Wow", "0"), ("Flutter", "0")]
def uad_base():
    return [("IPS", "15 IPS"), ("Tape Type", "456"), ("Emphasis EQ", CFG)]

DECKS = [("A800", "0", UAD_A800), ("ATR", "1", UAD_ATR)]

def render(plugin, params, dest):
    tmp = tempfile.mkdtemp()
    cmd = [BIN, "--au", plugin, "--input-wav", os.path.join(STIM, "thd_steps.wav"),
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

def spec(x):
    w = np.hanning(len(x)); X = np.abs(np.fft.rfft(x * w))
    f = np.fft.rfftfreq(len(x), 1 / SR); return f, X
def lv(f, X, hz, bw=8):
    k = np.argmin(np.abs(f - hz)); return float(np.max(X[max(0, k-bw):k+bw+1]))

def ladder(path):
    """Return {level: {peakdBFS, {h: dB rel fund for h in 2..7}}} using
    deep_probe's fixed-segment windowing (validated at -6)."""
    x, _ = sf.read(path); x = x.mean(1) if x.ndim > 1 else x
    seg = int(1.2 * SR); out = {}
    for i, db in enumerate(LEVELS):
        a, b = i*seg + int(0.3*SR), (i+1)*seg - int(0.3*SR)
        if b > len(x):
            out[db] = None; continue
        s = x[a:b]
        pk = 20*np.log10(np.max(np.abs(s)) + 1e-12)
        # Peak sanity guard: each segment's peak must sit near its nominal level.
        # Measured plugin gain/compression residuals span -1.2..+0.5 dB across all
        # decks/configs; a one-segment misread lands at least a neighbor-gap away
        # (3 dB at the -6/-3 step, 6 dB elsewhere). 1.5 dB = half the smallest gap
        # cleanly separates the two — the old >6 dB check missed every adjacent-
        # segment misread (the misalignment class behind curve_probe's false 3rd).
        misaligned = abs(pk - db) > 1.5
        f, X = spec(s); f0 = lv(f, X, FUND)
        hs = {h: 20*np.log10(lv(f, X, FUND*h)/f0 + 1e-12) for h in range(2, 8)}
        out[db] = {"pk": pk, "h": hs, "bad": misaligned}
        if misaligned:
            print(f"  !! segment {db:+d} dBFS peak reads {pk:+.1f} dBFS "
                  f"(>1.5 dB off nominal) — MISALIGNED, row untrustworthy")
    return out

def main():
    outdir = os.path.join(HERE, "renders", "ladder")
    skip = "--skip-render" in sys.argv
    print(f"\n############ HARMONIC LADDER  (config={CFG}, 456/15IPS/Repro/+6cal) ############")
    for deck, mi, uad in DECKS:
        mp = os.path.join(outdir, f"{deck}_{CFG}_mine.wav")
        up = os.path.join(outdir, f"{deck}_{CFG}_uad.wav")
        if not skip:
            if not render(MINE, mine_base(mi), mp):
                sys.exit(f"render failed: {deck} mine")
            if not render(uad, uad_base(), up):
                sys.exit(f"render failed: {deck} UAD")
        Lm, Lu = ladder(mp), ladder(up)
        print(f"\n===== {deck}  (mine vs UAD) =====")
        print("  lvl |        2f            3f            4f            5f            6f            7f     | pk(m/u)")
        for db in LEVELS:
            m, u = Lm[db], Lu[db]
            if m is None or u is None:
                continue
            cells = []
            for h in range(2, 8):
                cells.append(f"{m['h'][h]:+5.0f}/{u['h'][h]:+5.0f}")
            print(f"  {db:+3d} | " + "  ".join(cells) + f" | {m['pk']:+.0f}/{u['pk']:+.0f}")
        # delta table (mine - uad) for the key harmonics
        print("  Δ(mine-uad) dB:")
        for db in LEVELS:
            m, u = Lm[db], Lu[db]
            if m is None or u is None: continue
            d = {h: m['h'][h]-u['h'][h] for h in range(2, 8)}
            print(f"    {db:+3d}: " + "  ".join(f"{h}f={d[h]:+5.1f}" for h in range(2, 8)))

if __name__ == "__main__":
    main()
