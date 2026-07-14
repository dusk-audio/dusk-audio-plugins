#!/usr/bin/env python3
"""dynamic_probe.py — characterize the DYNAMIC / program-dependent behaviour of
the UAD decks (ATR-102, Studer A800) vs mine (Classic102, Swiss800), which use a
MEMORYLESS waveshaper. Two experiments:

(a) two-tone IMD level series: 1 kHz + 60 Hz (4:1 low:high), at -12/-6/-3 dBFS
    input, each a separate segment (silence-gapped). Measures how the IMD
    sidebands around 1 kHz grow with input level.
(b) level-step test: 1 kHz tone stepping -20 dBFS (2 s) -> -6 dBFS (2 s), and the
    reverse -6 -> -20, continuous (no gap). Measures the 1 kHz fundamental GAIN in
    each segment and its time-trajectory within the segment (attack/release
    settling = program-dependent gain; a memoryless shaper shows a flat step).

Saves renders to scratchpad/dyn_renders/ and results to scratchpad/dynamic_results.json.
Run:  python3 dynamic_probe.py
"""
import os, sys, json, shutil, tempfile, subprocess
import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
SCRATCH = os.environ.get("DYN_OUT", os.path.join(HERE, "renders", "dyn"))
BIN = os.path.join(HERE, "..", "..", "..", "..", "build/tests/duskverb_render/duskverb_render")
MINE = os.path.expanduser("~/Library/Audio/Plug-Ins/Components/tape_machine_2.component")
UAD_ATR = "/Library/Audio/Plug-Ins/Components/uaudio_ampex_atr-102_tape.component"
UAD_STUDER = "/Library/Audio/Plug-Ins/Components/uaudio_studer_a800.component"
STIMDIR = os.path.join(SCRATCH, "dyn_stim")
RENDDIR = os.path.join(SCRATCH, "dyn_renders")
SR = 48000
os.makedirs(STIMDIR, exist_ok=True)
os.makedirs(RENDDIR, exist_ok=True)

# reference config 456/NAB/15/Repro/+6 (matches deep_probe MINE_BASE)
def mine_base(machine):  # machine 1=Classic102/ATR, 0=Swiss800/A800
    return [("Tape Machine", str(machine)), ("Tape Speed", "1"), ("Tape Type", "0"),
            ("EQ Standard", "0"), ("Signal Path", "0"), ("Calibration", "1"),
            ("Noise Amount", "0"), ("Oversampling", "2"), ("Wow", "0"), ("Flutter", "0")]
UAD_BASE = [("IPS", "15 IPS"), ("Tape Type", "456"), ("Emphasis EQ", "NAB")]
UAD_WF_ATR = [("Wow & Flutter", "Off"), ("Hiss & Hum", "Off")]  # kill dynamics we don't test
UAD_WF_STUDER = [("Noise", "Off")]

DECKS = {
    "ATR":     dict(uad=UAD_ATR,    mine=mine_base(1), wf=UAD_WF_ATR),
    "Studer":  dict(uad=UAD_STUDER, mine=mine_base(0), wf=UAD_WF_STUDER),
}

def render(plugin, params, inp_path, dest):
    tmp = tempfile.mkdtemp()
    cmd = [os.path.abspath(BIN), "--au", plugin, "--input-wav", inp_path,
           "--slug", "s", "--output-dir", tmp, "--prerun-seconds", "2"]
    for n, v in params:
        cmd += ["--param", f"{n}={v}"]
    p = subprocess.run(cmd, capture_output=True, text=True)
    stem = os.path.join(tmp, "s_stem.wav")
    if not os.path.exists(stem):
        shutil.rmtree(tmp, ignore_errors=True)
        raise RuntimeError(f"render failed {plugin}: {p.stderr[-400:]}")
    shutil.move(stem, dest)
    shutil.rmtree(tmp, ignore_errors=True)
    return dest

def dbfs(a): return 10 ** (a / 20.0)

# ---------- stimuli ----------
def gen_imd_levels():
    """3 segments 1k+60Hz (4:1 low:high), -12/-6/-3 dBFS peak, 1.5s + 0.4s gap."""
    seg, gap = int(1.5 * SR), int(0.4 * SR)
    t = np.arange(seg) / SR
    out = []
    levels = [-12, -6, -3]
    for lv in levels:
        amp = dbfs(lv)
        lo = 0.8 * np.sin(2 * np.pi * 60 * t)   # 4:1 -> low is 4x, normalize below
        hi = 0.2 * np.sin(2 * np.pi * 1000 * t)
        s = lo + hi
        s = s / np.max(np.abs(s)) * amp
        out.append(s)
        out.append(np.zeros(gap))
    x = np.concatenate(out).astype(np.float32)
    sf.write(os.path.join(STIMDIR, "imd_levels.wav"), np.column_stack([x, x]), SR)
    return levels, seg, gap

def gen_step(lo_db, hi_db, name):
    """continuous 1 kHz tone, 2s at lo_db then 2s at hi_db (no gap)."""
    d = int(2.0 * SR)
    t = np.arange(d) / SR
    a = np.sin(2 * np.pi * 1000 * t)
    x = np.concatenate([a * dbfs(lo_db), a * dbfs(hi_db)]).astype(np.float32)
    sf.write(os.path.join(STIMDIR, name), np.column_stack([x, x]), SR)
    return d

# ---------- analysis ----------
def load(path):
    x, sr = sf.read(path)
    return (x.mean(1) if x.ndim > 1 else x).astype(np.float64), sr

def find_onset(x, thr_frac=0.05):
    thr = np.max(np.abs(x)) * thr_frac
    nz = np.where(np.abs(x) > thr)[0]
    return nz[0] if len(nz) else 0

def imd_sidebands(x, f_hi=1000.0, f_lo=60.0):
    """IMD products around 1 kHz carrier: sum(1k +/- n*60) / carrier, %."""
    w = np.hanning(len(x)); X = np.abs(np.fft.rfft(x * w)); f = np.fft.rfftfreq(len(x), 1 / SR)
    def L(hz):
        k = np.argmin(np.abs(f - hz)); return np.max(X[max(0, k - 6):k + 7])
    carrier = L(f_hi)
    side = np.sqrt(sum(L(f_hi + s * f_lo) ** 2 + L(f_hi - s * f_lo) ** 2 for s in (1, 2, 3, 4, 5)))
    return float(side / (carrier + 1e-12) * 100.0), float(20*np.log10(carrier+1e-12))

def seg_fundamental_gain(y, seg_start, seg_len, in_amp_db, f0=1000.0):
    """RMS level of the 1 kHz fundamental (bandpass via goertzel-ish FFT) in a
    segment, minus the input amplitude -> gain dB. Also windowed trajectory."""
    s = y[seg_start:seg_start + seg_len]
    # overall fundamental level via FFT peak
    w = np.hanning(len(s)); X = np.abs(np.fft.rfft(s * w)); f = np.fft.rfftfreq(len(s), 1 / SR)
    k = np.argmin(np.abs(f - f0))
    # coherent amplitude estimate: peak bin magnitude normalized by window sum
    amp = np.max(X[k-4:k+5]) * 2 / np.sum(w)
    lvl_db = 20 * np.log10(amp + 1e-12)
    gain = lvl_db - in_amp_db
    # trajectory: 50ms hann windows, fundamental amplitude over time
    wlen = int(0.05 * SR); hop = int(0.025 * SR)
    traj = []
    ww = np.hanning(wlen)
    for st in range(0, len(s) - wlen, hop):
        sw = s[st:st + wlen]
        Xw = np.abs(np.fft.rfft(sw * ww)); fw = np.fft.rfftfreq(wlen, 1 / SR)
        kk = np.argmin(np.abs(fw - f0))
        aw = np.max(Xw[kk-2:kk+3]) * 2 / np.sum(ww)
        traj.append(20 * np.log10(aw + 1e-12) - in_amp_db)
    return float(gain), [round(v, 3) for v in traj]

def main():
    results = {}
    # ===== (a) IMD level series =====
    levels, seg, gap = gen_imd_levels()
    imd_stim = os.path.join(STIMDIR, "imd_levels.wav")
    results["imd_levels"] = {"levels_db": levels}
    for deck, cfg in DECKS.items():
        for who, plug, params in (("mine", cfg["mine"], cfg["mine"]),
                                  ("uad", cfg["uad"], UAD_BASE + cfg["wf"])):
            plugp = MINE if who == "mine" else cfg["uad"]
            dest = os.path.join(RENDDIR, f"imd_{deck}_{who}.wav")
            render(plugp, params, imd_stim, dest)
            y, _ = load(dest)
            on = find_onset(y)
            per = []
            for i, lv in enumerate(levels):
                a = on + i * (seg + gap) + int(0.3 * SR)
                b = on + i * (seg + gap) + seg - int(0.2 * SR)
                imd, carr = imd_sidebands(y[a:b])
                per.append({"in_db": lv, "imd_pct": round(imd, 4), "carrier_db": round(carr, 2)})
            results["imd_levels"].setdefault(deck, {})[who] = per

    # ===== (b) level-step tests =====
    results["step"] = {}
    steps = {"up_-20_to_-6": (-20, -6), "down_-6_to_-20": (-6, -20)}
    for name, (lo_db, hi_db) in steps.items():
        d = gen_step(lo_db, hi_db, f"step_{name}.wav")
        stim = os.path.join(STIMDIR, f"step_{name}.wav")
        results["step"][name] = {"seg1_in_db": lo_db, "seg2_in_db": hi_db}
        for deck, cfg in DECKS.items():
            for who in ("mine", "uad"):
                plugp = MINE if who == "mine" else cfg["uad"]
                params = cfg["mine"] if who == "mine" else UAD_BASE + cfg["wf"]
                dest = os.path.join(RENDDIR, f"step_{name}_{deck}_{who}.wav")
                render(plugp, params, stim, dest)
                y, _ = load(dest)
                on = find_onset(y)
                g1, tr1 = seg_fundamental_gain(y, on + int(0.1*SR), d - int(0.2*SR), lo_db)
                g2, tr2 = seg_fundamental_gain(y, on + d + int(0.1*SR), d - int(0.2*SR), hi_db)
                results["step"][name].setdefault(deck, {})[who] = {
                    "seg1_gain_db": round(g1, 3), "seg2_gain_db": round(g2, 3),
                    "seg1_traj": tr1, "seg2_traj": tr2}

    with open(os.path.join(SCRATCH, "dynamic_results.json"), "w") as f:
        json.dump(results, f, indent=2)

    # ---- print summary ----
    print("=== (a) Two-tone IMD level series (1k+60Hz 4:1), IMD sidebands around 1k / carrier ===")
    for deck in DECKS:
        print(f"\n  {deck}:  in_dB   mine_IMD%   uad_IMD%   (growth with level)")
        md = results["imd_levels"][deck]["mine"]; ud = results["imd_levels"][deck]["uad"]
        for m, u in zip(md, ud):
            print(f"     {m['in_db']:>4}     {m['imd_pct']:>7.3f}    {u['imd_pct']:>7.3f}")
    print("\n=== (b) Level-step fundamental gain (dB rel input amplitude) ===")
    for name in steps:
        print(f"\n  step {name}:")
        for deck in DECKS:
            for who in ("mine", "uad"):
                r = results["step"][name][deck][who]
                # settling within seg2 (attack): first vs last window
                t2 = r["seg2_traj"]
                drift = (t2[-1] - t2[0]) if t2 else 0.0
                print(f"    {deck:7s} {who:4s}: seg1 {r['seg1_gain_db']:+.2f} dB | "
                      f"seg2 {r['seg2_gain_db']:+.2f} dB | seg2 drift(last-first) {drift:+.2f} dB")
    print("\nJSON -> scratchpad/dynamic_results.json ; renders -> scratchpad/dyn_renders/")

if __name__ == "__main__":
    main()
