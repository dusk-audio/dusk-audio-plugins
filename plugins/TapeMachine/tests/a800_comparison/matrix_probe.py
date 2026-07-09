#!/usr/bin/env python3
"""matrix_probe.py — Classic102 vs UAD ATR-102 across a SETTINGS MATRIX (not just
the 456/NAB/15/Repro reference). Renders both plugins at matched settings + same
signal (W&F off), prints FR at key freqs + THD@-6 per config with the delta, so we
can see WHERE our single-config tuning fails to generalize.

  python3 matrix_probe.py speed   # sweep the 3 tape speeds (456/NAB/Repro/+6)
  python3 matrix_probe.py tape    # sweep the 3 tapes  (NAB/15/Repro/+6)
  python3 matrix_probe.py eq      # NAB vs AES         (456/15/Repro/+6)
  python3 matrix_probe.py cal     # cal sweep          (456/NAB/15/Repro)
  python3 matrix_probe.py full    # the 3x3x2 speed x tape x eq matrix
"""
import os
import sys
import tempfile
import shutil
import subprocess
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from compare_a800 import freq_response, thd_curve  # noqa: E402
REPO = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
BIN = os.path.join(REPO, "build/tests/duskverb_render/duskverb_render")
MINE = os.path.expanduser("~/Library/Audio/Plug-Ins/Components/tape_machine_2.component")
STIM = os.path.join(HERE, "stimuli")
SR = 48000
FRq = [30, 50, 100, 1000, 5000, 10000, 15000]

# MATRIX_MACHINE=ATR102 (Classic102 idx 1 vs UAD Ampex ATR-102) or A800
# (Swiss800 idx 0 vs UAD Studer A800). Selects UAD, our machine idx, EQ set,
# and the UAD param names (studer has no Wow&Flutter toggle; Path "Repro").
MACHINE = os.environ.get("MATRIX_MACHINE", "ATR102")
SPEED = {"7.5": ("0", "7.5 IPS"), "15": ("1", "15 IPS"), "30": ("2", "30 IPS")}
CAL = {"+3": ("1", "+3 dB"), "+6": ("2", "+6 dB"), "+9": ("3", "+9 dB")}
if MACHINE == "A800":
    UAD = "/Library/Audio/Plug-Ins/Components/uaudio_studer_a800.component"
    OUT = os.path.join(HERE, "renders", "matrix_a800")
    MINE_MACHINE = "0"
    TAPE = {"456": ("0", "456"), "GP9": ("1", "GP9"), "250": ("3", "250")}
    EQ = {"NAB": ("0", "NAB"), "CCIR": ("1", "CCIR")}
    UAD_PATH = "Repro"
    UAD_WF = []                                  # studer has no W&F / Hiss&Hum toggle
else:
    UAD = "/Library/Audio/Plug-Ins/Components/uaudio_ampex_atr-102_tape.component"
    OUT = os.path.join(HERE, "renders", "matrix_atr")
    MINE_MACHINE = "1"
    TAPE = {"456": ("0", "456"), "GP9": ("1", "900"), "250": ("3", "250")}
    EQ = {"NAB": ("0", "NAB"), "AES": ("2", "AES")}
    UAD_PATH = "REPRO"
    UAD_WF = [("Wow & Flutter", "Off"), ("Hiss & Hum", "Off")]


def render(plugin, params, inp, tag, nparams=None):
    tmp = tempfile.mkdtemp()
    cmd = [BIN, "--au", plugin, "--input-wav", os.path.join(STIM, inp),
           "--slug", "s", "--output-dir", tmp, "--prerun-seconds", "2"]
    for n, v in params:
        cmd += ["--param", f"{n}={v}"]
    for n, v in (nparams or []):
        cmd += ["--nparam", f"{n}={v}"]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    stem = os.path.join(tmp, "s_stem.wav")
    if proc.returncode != 0 or not os.path.exists(stem):
        shutil.rmtree(tmp, ignore_errors=True)
        raise RuntimeError(
            f"render failed: plugin={plugin} stim={inp} tag={tag} "
            f"rc={proc.returncode}\nstderr:\n{proc.stderr}")
    os.makedirs(OUT, exist_ok=True)
    dest = os.path.join(OUT, f"{tag}.wav")   # UNIQUE per plugin+stim (was a shared path bug)
    shutil.move(stem, dest)
    shutil.rmtree(tmp, ignore_errors=True)
    return dest


def mine_params(sp, tp, eq, cal, path="0"):
    return [("Tape Machine", MINE_MACHINE), ("Tape Speed", SPEED[sp][0]), ("Tape Type", TAPE[tp][0]),
            ("EQ Standard", EQ[eq][0]), ("Signal Path", path), ("Calibration", CAL[cal][0]),
            ("Noise Amount", "0"), ("Oversampling", "2"), ("Wow", "0"), ("Flutter", "0")]


def uad_params(sp, tp, eq, cal):
    return [("IPS", SPEED[sp][1]), ("Tape Type", TAPE[tp][1]), ("Emphasis EQ", EQ[eq][1]),
            ("Cal Level", CAL[cal][1]), ("Path Select", UAD_PATH)] + UAD_WF


def fr_at(sweep_out):
    g, mag = freq_response(os.path.join(STIM, "sweep.wav"), sweep_out)
    return [mag[np.argmin(np.abs(g - f))] for f in FRq]


def measure(sp, tp, eq, cal):
    ms = render(MINE, mine_params(sp, tp, eq, cal), "sweep.wav", "m_sw")
    us = render(UAD, uad_params(sp, tp, eq, cal), "sweep.wav", "u_sw")
    mfr, ufr = fr_at(ms), fr_at(us)
    mt = render(MINE, mine_params(sp, tp, eq, cal), "thd_steps.wav", "m_thd")
    ut = render(UAD, uad_params(sp, tp, eq, cal), "thd_steps.wav", "u_thd")
    mthd = dict(thd_curve(mt)).get(-6, float("nan"))
    uthd = dict(thd_curve(ut)).get(-6, float("nan"))
    return mfr, ufr, mthd, uthd


def show(label, sp, tp, eq, cal):
    mfr, ufr, mthd, uthd = measure(sp, tp, eq, cal)
    print(f"\n=== {label}  (mine vs UAD {MACHINE}, rel 1k, W&F off) ===")
    print("  freq " + " ".join(f"{f:>6}" for f in FRq) + "   THD@-6")
    print("  mine " + " ".join(f"{v:+6.1f}" for v in mfr) + f"   {mthd:.2f}%")
    print("  UAD  " + " ".join(f"{v:+6.1f}" for v in ufr) + f"   {uthd:.2f}%")
    print("  diff " + " ".join(f"{m-u:+6.1f}" for m, u in zip(mfr, ufr)) +
          f"   {mthd-uthd:+.2f}")


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "speed"
    if mode == "speed":
        for sp in ("7.5", "15", "30"):
            show(f"{sp} IPS / 456 / NAB / +6", sp, "456", "NAB", "+6")
    elif mode == "tape":
        for tp in ("456", "GP9", "250"):
            show(f"15 / {tp} / NAB / +6", "15", tp, "NAB", "+6")
    elif mode == "eq":
        for eq in EQ:
            show(f"15 / 456 / {eq} / +6", "15", "456", eq, "+6")
    elif mode == "cal":
        for cal in ("+3", "+6", "+9"):
            show(f"15 / 456 / NAB / {cal}", "15", "456", "NAB", cal)
    elif mode == "full":
        for sp in ("7.5", "15", "30"):
            for tp in ("456", "GP9", "250"):
                for eq in EQ:
                    show(f"{sp}/{tp}/{eq}/+6", sp, tp, eq, "+6")


if __name__ == "__main__":
    main()
