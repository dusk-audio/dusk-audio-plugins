#!/usr/bin/env python3
"""preset_validate.py — validate TapeMachine 2's factory presets against the UAD
factory presets they were curated from.

Each TapeMachine preset (kTmPresets in dpf-plugin/TapeMachinePresets.hpp) was
decoded from a named UAD factory preset (Studer A800 for Swiss 800, Ampex ATR-102
for Classic 102). This renders BOTH plugins at their OWN preset and compares:

  * frequency response at key freqs (rel 1 kHz)   — tone match
  * THD @ -6 dB (thd_steps stimulus)              — saturation/drive match
  * broadband spectral null RMS on the sweep      — overall audio match

W&F + hiss/hum are forced OFF on both sides (they are decorrelated noise/FM that
cannot null and pollute FR/THD — see the a800 harness memory). The report also
prints the decoded UAD settings next to the TapeMachine preset so setting-level
mismatches (drive, cal, auto-cal) are visible.

  python3 preset_validate.py            # all presets
  python3 preset_validate.py "Modern"   # only presets whose name matches substr
  python3 preset_validate.py "Fat 456" "Old Tape"  # selected presets
"""
import os, sys, re, json, base64, struct, tempfile, shutil, subprocess, itertools
import numpy as np
import soundfile as sf

_rc = itertools.count()   # thread-safe unique render-file counter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from compare_a800 import freq_response, thd_curve  # noqa: E402
from deep_probe import aliasing as _aliasing  # noqa: E402
REPO = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
BIN = os.path.join(REPO, "build/tests/duskverb_render/duskverb_render")
MINE = os.path.expanduser("~/Library/Audio/Plug-Ins/Components/tape_machine_2.component")
STIM = os.path.join(HERE, "stimuli")
OUT = os.path.join(HERE, "renders", "preset_val")
PRESETS_HPP = os.path.join(REPO, "plugins/TapeMachine/dpf-plugin/TapeMachinePresets.hpp")
UASUP = "/Library/Application Support/Universal Audio/Plug-Ins"

ATR = "/Library/Audio/Plug-Ins/Components/uaudio_ampex_atr-102_tape.component"
STUDER = "/Library/Audio/Plug-Ins/Components/uaudio_studer_a800.component"
ATR_PRE = os.path.join(UASUP, "uaudio_ampex_atr-102_tape.lunacomponent/algo.bundle/Contents/Resources/presets")
STU_PRE = os.path.join(UASUP, "uaudio_studer_a800.lunacomponent/algo.bundle/Contents/Resources/presets")

# Dense FR grid. The old 7-point grid [30,50,100,1000,5000,10000,15000] had a
# 100->1000 Hz HOLE: the joint fit's LO-MID repro band (160 Hz peak) was
# unconstrained inside it and could bloat 150-500 Hz while still nailing 100 & 1000.
# The added 80/160/250/400/630/2000/3000/8000 points close every gap so the fit
# (and validation) see the low-mid region. joint4_fit's design matrix + FR error and
# preset_validate's FR check both key off this single grid.
FRq = [30, 50, 80, 100, 160, 250, 400, 630, 1000, 2000, 3000, 5000, 8000, 10000, 15000]

# UAD AU parameter name order (from --list-params). chunk layout = (nfloats-nparams)
# leading floats then the AU param array in this order.
ATR_PARAMS = ['Auto Gain','L Rec Level','R Rec Level','L Repro Level','R Repro Level',
    'Path Select','IPS','Tape Type','Cal Level','Head Width','Emphasis EQ','Auto Cal',
    'L HF EQ','R HF EQ','L Shelf EQ','R Shelf EQ','L Repro HF EQ','R Repro HF EQ',
    'L Repro LF EQ','R Repro LF EQ','L Bias','R Bias','Crosstalk','Wow & Flutter',
    'Hiss & Hum','Transformer','Stereo Link','Meter','Power']
STU_PARAMS = ['Input Level','Output Level','Path Select','IPS','Tape Type','Cal Level',
    'Emphasis EQ','HF Record EQ','Bias','Sync HF EQ','Sync LF EQ','Repro HF EQ',
    'Repro LF EQ','Noise','Hum Noise','Hiss Noise','Auto Cal','Power']

# TapeMachine preset name -> UAD factory preset json basename (both share names; a
# few TapeMachine names were shortened). machine picks the deck/dir.
UAD_JSON = {
    "Big 456 Master": "Classic_Big_456_Master", "Nice 456 Master": "Classic_Nice_456_Master",
    "Jazz Vision Master": "Classic_Jazz_Vision_Master", "Clean 900 Master": "Clean_Type_900_Master",
    "Fat 456 Master": "Pushed_456_Fat_Master", "GP9 Drum Bus": "Pushed_Drum_Bus",
    "Massive Bass": "Pushed_Massive_Bass", "Bright & Sizzly": "Pushed_Bright_and_Sizzly",
    "Sunbaked Cassette": "Vintage_Sunbaked_Cassette", "Analog Warmth": "Vintage_Analog_Warmth",
    "Classic Rock Crisp": "Classic_Rock_Crisp", "Modern Rock": "Modern_Rock",
    "Drum Bus": "Drum_Bus", "Hi-Fi Shine": "Hi-Fi_Shine", "Lush Film": "Lush_Film",
    "Jazz Warmth": "Jazz_Warmth", "Thick Saturation": "Thick_Saturation",
    "Hip-Hop Punch": "Hip-Hop_Punch", "Vocal Presence": "Vocal_Presence", "Old Tape": "Old_Tape",
}

CAL_LBL = {0: "+3", 1: "+6", 2: "+7.5", 3: "+9"}
SPEED_LBL = {0: "7.5", 1: "15", 2: "30", 3: "3.75"}
TYPE_LBL = {0: "456", 1: "GP9", 2: "900", 3: "250"}


def parse_presets():
    """Extract kTmPresets rows from the hpp as dicts (single source of truth)."""
    txt = open(PRESETS_HPP).read()
    body = txt[txt.index("kTmPresets[]"):]
    rows = re.findall(r'\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,([^}]+)\}', body)
    keys = ["machine","speed","type","eq","cal","path","head","inGain",
            "autoCal","bias","hp","lp","wow","flutter","noise"]
    # optional trailing repro-EQ fields (reproLf,reproLmf,reproHmf,reproHf); rows may omit
    out = []
    for name, cat, rest in rows:
        vals = [v.strip().rstrip("f") for v in rest.split(",")]
        vals = [v for v in vals if v != ""]
        if len(vals) < len(keys):
            continue
        d = {"name": name, "category": cat}
        for k, v in zip(keys, vals):
            if k == "autoCal":
                d[k] = (v == "true")
            elif k in ("machine","speed","type","eq","cal","path","head"):
                d[k] = int(float(v))
            else:
                d[k] = float(v)
        # optional trailing repro-EQ (reproLf,reproLmf,reproHmf,reproHf) + outTrim
        for k, v in zip(["reproLf","reproLmf","reproHmf","reproHf","outTrim"], vals[len(keys):]):
            d[k] = float(v)
        out.append(d)
    return out


def decode_uad(machine, basename):
    """Return dict{param_name: normalised value} from the UAD factory preset json."""
    pdir, plist = (ATR_PRE, ATR_PARAMS) if machine == 1 else (STU_PRE, STU_PARAMS)
    d = json.load(open(os.path.join(pdir, basename + ".json")))
    raw = base64.b64decode(d["chunk"])
    n = len(raw) // 4
    f = struct.unpack("<%df" % n, raw[:n * 4])
    off = n - len(plist)
    if off < 0:
        raise ValueError(f"{basename}: decoded {n} floats < {len(plist)} params")
    return {nm: f[off + i] for i, nm in enumerate(plist)}, d.get("name", basename)


def _pace_up():
    """UAD plugins bypass (pristine passthrough) when the PACE licensing daemon is down."""
    return os.path.isdir("/var/tmp/com.paceap.eden.licensed")


def render(plugin, params, inp, tag, nparams=None):
    # A bypassed UAD render is a pristine passthrough that would silently pass FR/THD
    # validation. Detect it up front (ATR/Studer + PACE down) and fail instead of
    # validating garbage.
    if plugin in (ATR, STUDER) and not _pace_up():
        raise RuntimeError(f"render failed tag={tag}: PACE down — UAD {os.path.basename(plugin)} "
                           f"would BYPASS (pristine passthrough); start PACE before validating")
    tmp = tempfile.mkdtemp()
    cmd = [BIN, "--au", plugin, "--input-wav", os.path.join(STIM, inp),
           "--slug", "s", "--output-dir", tmp, "--prerun-seconds", "2"]
    for k, v in params:
        cmd += ["--param", f"{k}={v}"]
    for k, v in (nparams or []):
        cmd += ["--nparam", f"{k}={v}"]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    stem = os.path.join(tmp, "s_stem.wav")
    if proc.returncode != 0 or not os.path.exists(stem):
        shutil.rmtree(tmp, ignore_errors=True)
        raise RuntimeError(f"render failed tag={tag} rc={proc.returncode}\n{proc.stderr[-800:]}")
    os.makedirs(OUT, exist_ok=True)
    # unique dest so parallel workers never clobber each other's tag files
    dest = os.path.join(OUT, f"{tag}_{os.getpid()}_{next(_rc)}.wav")
    shutil.move(stem, dest)
    shutil.rmtree(tmp, ignore_errors=True)
    return dest


def mine_params(p):
    """TapeMachine preset -> render params. W&F/noise off; HP/LP neutral (no UAD
    equivalent). Input Gain & Bias are FLOATs -> pass NORMALISED (0..1)."""
    ing = (p["inGain"] + 12.0) / 24.0            # -12..12 dB
    return [
        ("Tape Machine", p["machine"]), ("Tape Speed", p["speed"]),
        ("Tape Type", p["type"]), ("EQ Standard", p["eq"]),
        ("Calibration", p["cal"]), ("Signal Path", p["path"]),
        ("Head Width", p["head"]), ("Auto Calibration", 1 if p["autoCal"] else 0),
        ("Auto Compensation", 1), ("Bias", p["bias"] / 100.0),
        ("Input Gain", ing), ("Oversampling", 1),  # 2x default
        # per-preset output makeup trim (added to the gain-link inverse); loudness match.
        ("Output Gain", (p.get("outTrim", 0.0) + 12.0) / 24.0),
        # apply the preset's OWN hp/lp (linear-normalised; the 'g' skew is UI-only).
        # Clean presets (20/20k) => neutral; lo-fi presets get their real LP cliff.
        ("Highpass Freq", (p["hp"] - 20.0) / 480.0),
        ("Lowpass Freq", (p["lp"] - 3000.0) / 17000.0),
        ("Wow", 0.0), ("Flutter", 0.0), ("Noise Amount", 0.0), ("Noise Enabled", 0),
        # advanced repro-head 4-band EQ baked into the preset (float -> normalised).
        ("Repro LF",  (p.get("reproLf", 0.0) + 12.0) / 24.0),
        ("Repro LMF", (p.get("reproLmf", 0.0) + 12.0) / 24.0),
        ("Repro HMF", (p.get("reproHmf", 0.0) + 12.0) / 24.0),
        ("Repro HF",  (p.get("reproHf", 0.0) + 12.0) / 24.0),
    ]


def uad_nparams(machine, vec):
    """UAD factory param vector -> nparam list, with W&F + hiss/hum forced OFF."""
    v = dict(vec)
    if machine == 1:
        v["Wow & Flutter"] = 0.0
        v["Hiss & Hum"] = 0.0
    else:
        v["Noise"] = 0.0
    return [(k, val) for k, val in v.items()]


def fr_at(sweep_out):
    g, mag = freq_response(os.path.join(STIM, "sweep.wav"), sweep_out)
    return [mag[np.argmin(np.abs(g - f))] for f in FRq]


def null_rms_db(a_path, b_path):
    """Best-case null depth (dB below signal) after latency-align + level + fine
    broadband-gain match. Latency differs between plugins so a raw subtract is
    meaningless (~0 dB for everything) — cross-correlate first."""
    a, _ = sf.read(a_path); b, _ = sf.read(b_path)
    if a.ndim > 1: a = a.mean(1)
    if b.ndim > 1: b = b.mean(1)
    n = min(len(a), len(b)); a, b = a[:n], b[:n]
    # integer-lag align via FFT cross-correlation on the sustained middle
    seg0, seg1 = int(n*0.25), int(n*0.75)
    aw, bw = a[seg0:seg1], b[seg0:seg1]
    L = seg1 - seg0
    nf = 1 << int(np.ceil(np.log2(2*L)))
    xc = np.fft.irfft(np.fft.rfft(aw, nf) * np.conj(np.fft.rfft(bw, nf)), nf)
    lag = int(np.argmax(xc));  lag = lag - nf if lag > nf//2 else lag
    b = np.roll(b, lag)
    seg = slice(seg0, seg1)
    ka = np.sqrt(np.mean(a[seg]**2)); kb = np.sqrt(np.mean(b[seg]**2))
    if kb > 1e-9: b = b * (ka / kb)
    diff = a[seg] - b[seg]
    r = np.sqrt(np.mean(diff**2)); s = np.sqrt(np.mean(a[seg]**2))
    return 20*np.log10(r/s) if (r > 0 and s > 0) else float("nan")


def validate(p):
    machine = p["machine"]
    uad_bin = ATR if machine == 1 else STUDER
    base = UAD_JSON[p["name"]]
    vec, uname = decode_uad(machine, base)
    deck = "ATR-102" if machine == 1 else "Studer A800"

    mp = mine_params(p)
    unp = uad_nparams(machine, vec)

    m_sw = render(MINE, mp, "sweep.wav", "m_sw")
    u_sw = render(uad_bin, [], "sweep.wav", "u_sw", nparams=unp)
    m_th = render(MINE, mp, "thd_steps.wav", "m_th")
    u_th = render(uad_bin, [], "thd_steps.wav", "u_th", nparams=unp)
    m_al = render(MINE, mp, "alias.wav", "m_al")
    u_al = render(uad_bin, [], "alias.wav", "u_al", nparams=unp)

    mfr, ufr = fr_at(m_sw), fr_at(u_sw)
    mthd = dict(thd_curve(m_th)).get(-6, float("nan"))
    uthd = dict(thd_curve(u_th)).get(-6, float("nan"))
    malias, ualias = _aliasing(m_al), _aliasing(u_al)
    null = null_rms_db(m_sw, u_sw)
    frdiff = [m - u for m, u in zip(mfr, ufr)]
    maxfr = max(abs(x) for x in frdiff)
    meanfr = float(np.mean([abs(x) for x in frdiff]))   # mean per-point error over the dense grid
    # low-mid focus band (150-500 Hz) — where the old grid's hole hid the Fat 456 bulge
    lowmid = [abs(d) for f, d in zip(FRq, frdiff) if 150 <= f <= 500]
    lowmidfr = float(np.mean(lowmid)) if lowmid else 0.0

    # decoded UAD settings for the fidelity line
    if machine == 1:
        u_speed = {0.0:"7.5",0.3333:"15",0.6667:"30",1.0:"3.75"}  # ATR: 3.75/7.5/15/30 -> 0/.33/.66/1
        # ATR IPS norm: 0=3.75? decode: 0.6667->15. map by nearest of quarters
        ips = round(vec["IPS"]*3)/3
        ips_lbl = {0.0:"3.75",0.3333:"7.5",0.6667:"15",1.0:"30"}.get(round(ips,4), f"{vec['IPS']:.2f}")
        cal_lbl = {0.0:"+3",0.3333:"+6",0.6667:"+7.5",1.0:"+9"}.get(round(vec["Cal Level"],4), f"{vec['Cal Level']:.2f}")
        tp_lbl = {0.0:"250",0.3333:"456",0.6667:"900",1.0:"GP9"}.get(round(vec["Tape Type"],4), f"{vec['Tape Type']:.2f}")
        eq_lbl = "NAB" if vec["Emphasis EQ"] < 0.5 else "CCIR"
        acal = "On" if vec["Auto Cal"] > 0.5 else "Off"
        drive = f"L Rec {vec['L Rec Level']:.3f}"
    else:
        ips = round(vec["IPS"]*3)/3
        ips_lbl = {0.0:"3.75",0.3333:"7.5",0.6667:"15",1.0:"30"}.get(round(ips,4), f"{vec['IPS']:.2f}")
        cal_lbl = {0.0:"+3",0.3333:"+6",0.6667:"+7.5",1.0:"+9"}.get(round(vec["Cal Level"],4), f"{vec['Cal Level']:.2f}")
        tp_lbl = {0.0:"250",0.3333:"456",0.6667:"900",1.0:"GP9"}.get(round(vec["Tape Type"],4), f"{vec['Tape Type']:.2f}")
        eq_lbl = "NAB" if vec["Emphasis EQ"] < 0.5 else "CCIR"
        acal = "On" if vec["Auto Cal"] > 0.5 else "Off"
        drive = f"In {vec['Input Level']:.3f}"

    return dict(name=p["name"], deck=deck, uname=uname, mfr=mfr, ufr=ufr,
                frdiff=frdiff, maxfr=maxfr, meanfr=meanfr, lowmidfr=lowmidfr,
                mthd=mthd, uthd=uthd, null=null,
                malias=malias, ualias=ualias,
                mine=dict(speed=SPEED_LBL[p["speed"]], type=TYPE_LBL[p["type"]],
                          eq=("NAB","CCIR")[p["eq"]], cal=CAL_LBL[p["cal"]],
                          acal="On" if p["autoCal"] else "Off", inGain=p["inGain"]),
                uad=dict(speed=ips_lbl, type=tp_lbl, eq=eq_lbl, cal=cal_lbl,
                         acal=acal, drive=drive))


def main():
    filters = [s.lower() for s in sys.argv[1:]]
    presets = [p for p in parse_presets()
               if not filters or any(f in p["name"].lower() for f in filters)]
    print(f"validating {len(presets)} preset(s) vs UAD factory presets\n")
    rows = []
    failed = []
    for p in presets:
        try:
            r = validate(p)
        except Exception as e:
            print(f"!! {p['name']}: {e}\n")
            failed.append(p["name"])
            continue
        rows.append(r)
        m, u = r["mine"], r["uad"]
        print(f"### {r['name']}  ({r['deck']}  <- \"{r['uname']}\")")
        print(f"  settings mine: {m['speed']}ips {m['type']} {m['eq']} cal{m['cal']} "
              f"acal={m['acal']} inGain={m['inGain']:+.1f}dB")
        print(f"  settings UAD : {u['speed']}ips {u['type']} {u['eq']} cal{u['cal']} "
              f"acal={u['acal']} {u['drive']}")
        print("  freq " + " ".join(f"{f:>6}" for f in FRq) + "   THD-6   null")
        print("  mine " + " ".join(f"{v:+6.1f}" for v in r["mfr"]) + f"   {r['mthd']:5.2f}%")
        print("  UAD  " + " ".join(f"{v:+6.1f}" for v in r["ufr"]) + f"   {r['uthd']:5.2f}%")
        print("  diff " + " ".join(f"{v:+6.1f}" for v in r["frdiff"]) +
              f"   {r['mthd']-r['uthd']:+5.2f}  {r['null']:+5.1f}dB  (maxFR {r['maxfr']:.1f})\n")

    if rows:
        print("=" * 84)
        # metric definitions (dense 15-pt grid):
        #   meanFR = mean |mine-UAD| over all grid points (overall tone match)
        #   maxFR  = worst single-point |mine-UAD| (stricter on 15 pts than the old 7)
        #   lowmid = mean |diff| over 150-500 Hz (the band the old grid's hole hid)
        print(f"{'preset':22} {'meanFR':>6} {'maxFR':>6} {'lowMid':>6} {'THDΔ':>7}  {'alias mine/UAD':>16} {'ok':>3}")
        all_ok = True
        for r in sorted(rows, key=lambda x: -x["meanfr"]):
            # composite gate: alias (mine <= UAD + 1 dB tol), meanFR/maxFR <= 1.5 dB, |THDΔ| <= 1%
            row_ok = (r["malias"] <= r["ualias"] + 1.0
                      and r["meanfr"] <= 1.5 and r["maxfr"] <= 1.5
                      and abs(r["mthd"] - r["uthd"]) <= 1.0)
            all_ok = all_ok and row_ok
            print(f"{r['name']:22} {r['meanfr']:6.2f} {r['maxfr']:6.1f} {r['lowmidfr']:6.2f} "
                  f"{r['mthd']-r['uthd']:+7.2f}  {r['malias']:7.1f}/{r['ualias']:6.1f} "
                  f"{'y' if row_ok else 'NO':>3}")
        mmean = np.mean([r["meanfr"] for r in rows])
        mmax = np.mean([r["maxfr"] for r in rows])
        mlm = np.mean([r["lowmidfr"] for r in rows])
        nalias = sum(1 for r in rows if r["malias"] <= r["ualias"] + 1.0)
        # pass gate keeps the <=1.5 dB per-point spirit but on the mean over the dense
        # grid (max over 15 pts is naturally harsher than over the old 7).
        print(f"\nmean meanFR {mmean:.2f} dB | mean maxFR {mmax:.2f} dB | mean lowMid {mlm:.2f} dB")
        print(f"meanFR<=1.5dB: {sum(1 for r in rows if r['meanfr']<=1.5)}/{len(rows)}"
              f"   |  maxFR<=1.5dB: {sum(1 for r in rows if r['maxfr']<=1.5)}/{len(rows)}"
              f"   |  THD<=1%: {sum(1 for r in rows if abs(r['mthd']-r['uthd'])<=1.0)}/{len(rows)}"
              f"   |  alias<=UAD: {nalias}/{len(rows)}")
        if not all_ok:
            print("\nFAIL: one or more presets missed a gate (see 'ok' column)")
            sys.exit(1)

    if failed:
        print(f"FAIL: {len(failed)} preset(s) failed to validate: {', '.join(failed)}")
        sys.exit(1)
    if not rows:
        print("FAIL: no presets validated")
        sys.exit(1)


if __name__ == "__main__":
    main()
