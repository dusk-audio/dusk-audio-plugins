#!/usr/bin/env python3
"""level_probe.py — measure the LEVEL-DEPENDENT frequency response of TapeMachine 2
vs the UAD deck it is tuned against, and print the FR(freq, level) surfaces plus
the mine-minus-UAD DELTA surface.

WHY THIS EXISTS
---------------
The fixed-level FR gates (matrix_probe / preset_validate render a -12 dBFS sweep)
all pass, yet presets sound tonally different on DRUMS. Hypothesis H1: mine's FR
changes with SIGNAL LEVEL differently than the UAD's. `driveHfComp` keys on the
KNOB drive, not the instantaneous signal level, so a high-crest drum hit — which
sweeps the whole level range on every transient — sees a program-dependent tonal
tilt that no single-level sine can catch. This probe renders a grid of pure tones
at 15 frequencies x 6 input levels (-30..0 dBFS) and reads the FUNDAMENTAL bin
only (harmonics land in other bins and are ignored), so it isolates the linear
gain of each frequency as a function of drive.

METHOD
------
  * One stimulus: 6 level rows x 15 freq columns, each a 0.6 s tone (0.15 s guard
    each side, analyse the middle 0.3 s). Latency is ~2 ms so fixed-time slicing
    is safe (see the harness memory: output is start-aligned + silence-padded).
  * FR(f, L) = 20*log10( out_fundamental / in_fundamental )  [dB]
  * Each level row is normalised to its own 1 kHz value (rel-1k convention, same
    as the rest of the harness) so broadband compression is removed and only the
    tonal SHAPE at that drive remains.
  * DELTA(f, L) = mine_rel1k - UAD_rel1k  — this is the exact surface a
    signal-level-keyed EQ would have to cancel (the Phase-B fit target).

CASES: reference config (both machines) + presets Drum Bus, Nice 456 Master,
Old Tape, Fat 456 Master (mine at its preset, UAD at the decoded factory chunk —
same pairing as preset_validate).

  python3 level_probe.py                # all cases
  python3 level_probe.py "Drum"         # only cases whose name matches substr
"""
import os, sys, json, tempfile, shutil, subprocess, itertools
from concurrent.futures import ThreadPoolExecutor
import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from preset_validate import (parse_presets, decode_uad, uad_nparams, mine_params,
                             UAD_JSON, ATR, STUDER, MINE, BIN)  # noqa: E402

SR = 48000
OUT = os.path.join(HERE, "renders", "level_probe")
_rc = itertools.count()

# analysis grid — same 15-pt FR grid as preset_validate (rel-1k)
FRq = [30, 50, 80, 100, 160, 250, 400, 630, 1000, 2000, 3000, 5000, 8000, 10000, 15000]
LEVELS = [-30, -24, -18, -12, -6, 0]        # input dBFS
SEG = 0.6                                    # seconds per tone
GUARD = 0.15                                 # skip each side (latency + ramp)
RAMP = 0.02

TARGET_PRESETS = ["Drum Bus", "Nice 456 Master", "Old Tape", "Fat 456 Master"]


def build_stimulus():
    """6 levels x 15 freqs of 0.6 s tones; return (path, seg_samples)."""
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, "level_grid.wav")
    seg = int(SEG * SR)
    ramp = int(RAMP * SR)
    env = np.ones(seg)
    env[:ramp] = np.sin(np.linspace(0, np.pi / 2, ramp)) ** 2
    env[-ramp:] = np.sin(np.linspace(np.pi / 2, 0, ramp)) ** 2
    chunks = []
    for L in LEVELS:
        amp = 10.0 ** (L / 20.0)
        for f in FRq:
            t = np.arange(seg) / SR
            chunks.append(amp * np.sin(2 * np.pi * f * t) * env)
    mono = np.concatenate(chunks).astype(np.float32)
    sf.write(path, np.column_stack([mono, mono]), SR, subtype="FLOAT")
    return path, seg


def render(plugin, params, inp, mode, tag):
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
    dest = os.path.join(OUT, f"{tag}_{os.getpid()}_{next(_rc)}.wav")
    shutil.move(stem, dest)
    shutil.rmtree(tmp, ignore_errors=True)
    return dest


def seg_fundamental(x, seg, li, fi, f0):
    """Fundamental-bin magnitude of segment (li,fi) in signal x (mono)."""
    idx = li * len(FRq) + fi
    a = idx * seg + int(GUARD * SR)
    b = (idx + 1) * seg - int(GUARD * SR)
    if b > len(x):
        return np.nan
    w = x[a:b] * np.hanning(b - a)
    X = np.abs(np.fft.rfft(w))
    f = np.fft.rfftfreq(len(w), 1 / SR)
    k = np.argmin(np.abs(f - f0))
    return float(np.max(X[max(0, k - 3):k + 4]))


def surface(out_path, in_path):
    """FR(freq,level) rel-1k dB surface, shape (len(LEVELS), len(FRq))."""
    o, _ = sf.read(out_path); o = o.mean(1) if o.ndim > 1 else o
    inp, _ = sf.read(in_path); inp = inp.mean(1) if inp.ndim > 1 else inp
    seg = int(SEG * SR)
    S = np.full((len(LEVELS), len(FRq)), np.nan)
    for li in range(len(LEVELS)):
        for fi, f0 in enumerate(FRq):
            om = seg_fundamental(o, seg, li, fi, f0)
            im = seg_fundamental(inp, seg, li, fi, f0)
            if om > 0 and im > 0:
                S[li, fi] = 20 * np.log10(om / im)
        k1 = FRq.index(1000)
        if np.isfinite(S[li, k1]):
            S[li] = S[li] - S[li, k1]        # rel-1k per row
    return S


def ref_mine(mac):
    return [("Tape Machine", mac), ("Tape Speed", 1), ("Tape Type", 0),
            ("EQ Standard", 0), ("Signal Path", 0), ("Calibration", 1),
            ("Input Gain", 0.5), ("Auto Compensation", 0), ("Auto Calibration", 1),
            ("Bias", 0.5), ("Oversampling", 1), ("Wow", 0.0), ("Flutter", 0.0),
            ("Noise Amount", 0.0), ("Noise Enabled", 0)]


def ref_uad(mac):
    return [("IPS", "15 IPS"), ("Tape Type", "456"), ("Emphasis EQ", "NAB"),
            ("Path Select", "Repro" if mac == 0 else "REPRO"), ("Cal Level", "+6 dB"),
            ("Noise", "Off")] if mac == 0 else \
           [("IPS", "15 IPS"), ("Tape Type", "456"), ("Emphasis EQ", "NAB"),
            ("Path Select", "REPRO"), ("Cal Level", "+6 dB"),
            ("Wow & Flutter", "Off"), ("Hiss & Hum", "Off")]


def build_cases(filt):
    ps = {p["name"]: p for p in parse_presets()}
    cases = []
    for mac, ubin, deck in [(0, STUDER, "A800"), (1, ATR, "ATR")]:
        cases.append((f"REF-{deck}", deck, ref_mine(mac), "param", ref_uad(mac), ubin))
    for name in TARGET_PRESETS:
        p = ps[name]
        vec, _ = decode_uad(p["machine"], UAD_JSON[name])
        ubin = ATR if p["machine"] == 1 else STUDER
        deck = "ATR" if p["machine"] == 1 else "A800"
        cases.append((name, deck, mine_params(p), "nparam",
                      uad_nparams(p["machine"], vec), ubin))
    return [c for c in cases if filt.lower() in c[0].lower()]


def fmt_surface(S, title):
    lines = [f"  {title}"]
    lines.append("  level\\Hz " + " ".join(f"{f:>6}" for f in FRq))
    for li, L in enumerate(LEVELS):
        lines.append(f"  {L:>4} dB " + " ".join(
            (f"{S[li,fi]:+6.1f}" if np.isfinite(S[li, fi]) else "     .")
            for fi in range(len(FRq))))
    return "\n".join(lines)


def run_case(c, stim, seg):
    name, deck, mp, mode, up, ubin = c
    m_out = render(MINE, mp, stim, "param", f"m_{name.replace(' ','_')}")
    u_out = render(ubin, up, stim, mode, f"u_{name.replace(' ','_')}")
    Sm = surface(m_out, stim)
    Su = surface(u_out, stim)
    return name, deck, Sm, Su, Sm - Su


def main():
    filt = sys.argv[1] if len(sys.argv) > 1 else ""
    stim, seg = build_stimulus()
    cases = build_cases(filt)
    print(f"level_probe: {len(cases)} case(s), levels {LEVELS} dBFS, rel-1k\n")
    results = {}
    with ThreadPoolExecutor(max_workers=6) as ex:
        futs = [ex.submit(run_case, c, stim, seg) for c in cases]
        for f in futs:
            name, deck, Sm, Su, D = f.result()
            results[name] = dict(deck=deck, mine=Sm.tolist(), uad=Su.tolist(),
                                 delta=D.tolist())
            print("=" * 96)
            print(f"### {name}  ({deck})")
            print(fmt_surface(Sm, "mine  FR(freq,level) rel-1k [dB]"))
            print(fmt_surface(Su, "UAD   FR(freq,level) rel-1k [dB]"))
            print(fmt_surface(D,  "DELTA mine-UAD [dB]  (Phase-B fit target)"))
            # summary: worst delta cell, and level-spread at HF (10k)
            k10 = FRq.index(10000); k1 = FRq.index(1000)
            hf_lo = D[LEVELS.index(-30), k10]; hf_hi = D[LEVELS.index(0), k10]
            worst = np.nanmax(np.abs(D))
            print(f"  -> 10k delta: {hf_lo:+.1f} dB @-30 -> {hf_hi:+.1f} dB @0  "
                  f"(level-drift {hf_hi-hf_lo:+.1f} dB) | worst|delta| {worst:.1f} dB\n")
    dump = os.path.join(OUT, "level_surfaces.json")
    json.dump(dict(freqs=FRq, levels=LEVELS, cases=results), open(dump, "w"), indent=1)
    print(f"surfaces written: {dump}")


if __name__ == "__main__":
    main()
