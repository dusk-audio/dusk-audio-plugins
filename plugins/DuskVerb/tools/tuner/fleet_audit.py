#!/usr/bin/env python3
"""
fleet_audit.py - DuskVerb fleet root-cause audit + calibration verifier.

WHY THIS EXISTS
---------------
Gate COUNTS hide broken calibrations. "Vocal Hall" scored ~28 fails for MONTHS
while its per-octave T60 table commanded 16 kHz = 6.2 s and the engine realized
0.10 s - an inverted decay tilt (lows commanded long realize short, mids
commanded short realize long). ~15 of its 45 fails were ONE broken calibration,
never diagnosed because we minimized the aggregate count instead of reading
full_check as a diagnosis.

This tool does two jobs the count never did:
  1. AUDIT: render each preset, gain-match to its anchor, run full_check, and
     group the fails by root cause (commanded-vs-realized T60, decay tilt,
     brightness spike, early-field wall).
  2. VERIFY: for every AccurateHall preset (algo 10/12), assert the engine
     REALIZES the per-octave T60 it was COMMANDED (within CAL_TOL). Exits
     nonzero on drift so it can gate CI / a release.

The commanded table is parsed straight from PluginProcessor.cpp (single source
of truth) so it can never drift from what the plugin actually ships.

USAGE
-----
  fleet_audit.py                       # full fleet audit, ranked report
  fleet_audit.py --preset "Vocal Hall" # one preset
  fleet_audit.py --verify-calibration  # algo10/12 commanded-vs-realized; exit 1 on drift
  fleet_audit.py --no-render           # reuse existing /tmp/audit_* renders (fast re-analysis)
  fleet_audit.py --workers 3           # parallel render workers (keep low: Wine RAM)
"""
import os, re, sys, glob, json, shutil, argparse, subprocess
from concurrent.futures import ThreadPoolExecutor
import soundfile as sf
import numpy as np

ROOT = os.path.expanduser("~/projects/plugins")
REND = f"{ROOT}/build/tests/duskverb_render/duskverb_render"
VST3 = os.path.expanduser("~/.vst3/DuskVerb.vst3")
FC   = f"{ROOT}/plugins/DuskVerb/tools/tuner/full_check.py"
PROC = f"{ROOT}/plugins/DuskVerb/src/PluginProcessor.cpp"
TR   = os.path.expanduser("~/projects/dusk-audio-tools/tuner_runs/anchors")
RD   = os.path.expanduser("~/projects/dusk-audio-tools/anchors/rendered")
STIM = ["impulse", "noiseburst", "snare", "sine1k", "sustained"]
CAL_TOL = 0.15            # realized T60 must be within +/-15% of commanded
OCTAVE_HZ = [63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]

# preset -> (anchor_dir, shimmer). Shimmer presets render at native mix=0.5.
FLEET = {
    "Vocal Plate":          (f"{TR}/vvv-vocal-plate", False),
    "Vintage Vocal Plate":  (f"{RD}/lex-vintage-vocal-plate", False),
    "Drum Plate":           (f"{TR}/vvv-drum-plate", False),
    "Vintage Gold Plate":   (f"{RD}/lex-vintage-gold-plate", False),
    "Bright Hall":          (f"{TR}/vvv-bright-hall", False),
    "Vocal Hall":           (f"{TR}/vvv-vocal-hall", False),
    "Cathedral Large Hall": (f"{TR}/vvv-cathedral", False),
    "Blade Runner 224":     (f"{TR}/vvv-blade-runner", False),
    "79 Vocal Chamber":     (f"{TR}/vvv-79vc", False),
    "Large Chamber":        (f"{RD}/lex-chamber-large", False),
    "Small Drum Room":      (f"{TR}/vvv-84-small-room", False),
    "Medium Drum Room":     (f"{TR}/vvv-fat-snare-room", False),
    "Live Room":            (f"{RD}/lex-medium-live-room-1", False),
    "Tiled Room":           (f"{TR}/vvv-tiled-room", False),
    "Ambience":             (f"{TR}/vvv-ambience", False),
    "Reverse Taps":         (f"{RD}/lex-reverse-1", False),
    "Black Hole":           (f"{RD}/valhalla-shimmer-black-hole", True),
    "Deep Blue Day":        (f"{RD}/valhalla-shimmer-deep-blue-day", True),
}


def rms(p):
    x, _ = sf.read(p); m = x.mean(axis=1) if x.ndim > 1 else x
    return float(np.sqrt(np.mean(m * m)))


def commanded_octave_t60():
    """Parse kAccurateHallT60ByName from PluginProcessor.cpp -> {name: [9 floats]}.
    Single source of truth; can't drift from the shipped plugin."""
    src = open(PROC, encoding="utf-8").read()
    blk = re.search(r"kAccurateHallT60ByName\s*=\s*\{(.*?)\n\s*\};", src, re.S)
    out = {}
    if not blk:
        return out
    for m in re.finditer(r'\{\s*"([^"]+)"\s*,\s*\{\{([^}]+)\}\}', blk.group(1)):
        vals = [float(v) for v in re.findall(r"[-\d.]+", m.group(2))]
        if len(vals) == 9:
            out[m.group(1)] = vals
    return out


# Name-keyed config maps that ONLY route to a specific engine. A keyed entry
# whose preset runs on a DIFFERENT engine is DORMANT - inert config that no
# engine reads, yet sits in source looking authoritative (it misled the dev
# into "re-calibrate Vocal Hall's octave table" when Vocal Hall is DenseHall).
# The 2026-06-13 DenseHall migration orphaned several of these. map -> allowed algos.
TABLE_ENGINES = {
    "kAccurateHallT60ByName": (10, 12, 11, 13),  # accurateHall_ — algos 10/12 AND the
                                          # SparseField(11)+TiledRoom(13) composites,
                                          # whose tail IS accurateHall_.process()
                                          # (DuskVerbEngine case SparseField/TiledRoom).
                                          # Omitting 11/13 false-flagged Tiled Room's
                                          # LIVE octave entry as dormant -> wrongly deleted.
    "kDattorroOctaveT60ByName": (0, 1),   # consumed only by dattorro_/dattorroVintage_
}


def keyed_map_names(map_name):
    """Quoted preset names keyed in a kXxxByName map in PluginProcessor.cpp."""
    src = open(PROC, encoding="utf-8").read()
    blk = re.search(map_name + r"\s*=\s*\{(.*?)\n\s*\};", src, re.S)
    if not blk:
        return []
    # entries open with `{ "Name",` - skip // comment lines
    body = "\n".join(l for l in blk.group(1).splitlines()
                     if not l.lstrip().startswith("//"))
    return re.findall(r'\{\s*"([^"]+)"\s*,', body)


def dormant_table_entries():
    """Every keyed map entry whose preset's real engine ignores that map.
    Returns list of (map_name, preset, real_algo, allowed_algos)."""
    out = []
    for mp, allowed in TABLE_ENGINES.items():
        for name in keyed_map_names(mp):
            a = preset_algo(name)
            if a not in allowed:
                out.append((mp, name, a, allowed))
    return out


def preset_algo(name):
    """First int on the line after the `{ "<name>",` row in FactoryPresets.h."""
    fp = f"{ROOT}/plugins/DuskVerb/src/FactoryPresets.h"
    lines = open(fp, encoding="utf-8").readlines()
    for i, ln in enumerate(lines):
        if re.search(r'\{\s*"%s"\s*,' % re.escape(name), ln):
            for j in range(i, min(i + 4, len(lines))):
                mm = re.search(r"^\s*(\d+)\s*,", lines[j])
                if mm:
                    return int(mm.group(1))
    return None


def render_and_check(name, no_render=False):
    """Render (gain-matched) + full_check. Returns (full_check_text, ok)."""
    adir, shim = FLEET[name]
    apref = os.path.basename(adir)
    slug = name.lower().replace(" ", "_").replace("'", "")
    dv, lex = f"/tmp/audit_{slug}", f"/tmp/audit_{slug}_a"
    if not (no_render and os.path.isdir(dv) and glob.glob(f"{dv}/*_noiseburst.wav")):
        shutil.rmtree(dv, ignore_errors=True)
        os.makedirs(dv)
        cmd = [REND, "--vst3", VST3, "--program", name, "--output-dir", dv,
               "--sustained-pink-seconds", "4.0"]
        if shim:
            # Shimmer presets are rendered at their NATIVE 50% wet — the Valhalla Shimmer
            # anchors were captured at 50% (BH/DBD mix 0.50), so forcing full-wet compared a
            # dry-less DV tail against a mix that carries the dry transient, faking the whole
            # front-load/energy-arrival gate cluster (attack, energy_t50/first50, transient).
            # Add the sustained sine for the down-octave cascade gate.
            cmd += ["--long-sine-seconds", "15"]
        else:
            # Non-shimmer presets match 100%-wet Valhalla anchors → force full-wet.
            cmd += ["--param", "Dry/Wet=1.0", "--param", "Bus Mode=1"]
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=420)
        nb = glob.glob(f"{dv}/*_noiseburst.wav")
        if r.returncode != 0 or not nb:
            return f"RENDER_FAIL {name}\n{r.stderr[-400:]}", False
        # Gain-match the freshly rendered dv wavs to the anchor noiseburst.
        # Reuse path skips this: those wavs were already gain-matched earlier.
        anchor_nb = f"{adir}/{apref}_noiseburst.wav"
        if not os.path.exists(anchor_nb):
            return f"NO_ANCHOR {name} {adir}", False
        g = rms(anchor_nb) / max(rms(nb[0]), 1e-12)
        for f in glob.glob(f"{dv}/*.wav"):
            x, sr = sf.read(f)
            sf.write(f, x * g, sr, subtype="FLOAT")

    # ALWAYS (re)populate the anchor dir so full_check, run right after, never
    # sees an empty/stale lex on the --no-render reuse path.
    shutil.rmtree(lex, ignore_errors=True); os.makedirs(lex)
    for s in STIM + (["sinelong"] if shim else []):
        src = f"{adir}/{apref}_{s}.wav"
        if os.path.exists(src):
            shutil.copy(src, f"{lex}/anchor_{s}.wav")
    if not os.path.exists(f"{lex}/anchor_noiseburst.wav"):
        return f"NO_ANCHOR {name} {adir}", False
    fc = subprocess.run([sys.executable, FC, dv, lex, "--name", name],
                        capture_output=True, text=True, timeout=200)
    return fc.stdout, True


def parse_realized_t60(fc_text):
    """Pull realized per-octave T60 (DV) from full_check output."""
    out = {}
    for m in re.finditer(r"T60\s+([\d.]+)\s*(k?)\s*Hz\s+DV=\s*([\d.]+)s", fc_text):
        hz = float(m.group(1)) * (1000 if m.group(2) == "k" else 1)
        out[int(round(hz))] = float(m.group(3))
    return out


def parse_nfail(fc_text):
    m = re.search(r"(\d+)\s+GATE\(S\) FAILED", fc_text)
    return int(m.group(1)) if m else None


def calibration_report(name, commanded, fc_text):
    """Compare commanded vs realized per-octave T60. Returns dict or None."""
    if not commanded:
        return None
    realized = parse_realized_t60(fc_text)
    rows, worst = [], 0.0
    low_short = mid_long = False
    missing = False
    for i, hz in enumerate(OCTAVE_HZ):
        cmd = commanded[i]
        got = realized.get(hz)
        if got is None:
            missing = True   # commanded octave with no realized measurement
            continue
        div = (got - cmd) / cmd if cmd > 0 else 0.0
        rows.append((hz, cmd, got, div))
        worst = max(worst, abs(div))
        if hz <= 250 and div < -CAL_TOL:
            low_short = True
        if 500 <= hz <= 4000 and div > CAL_TOL:
            mid_long = True
    # Missing/empty realized rows = malformed full_check output, NOT a perfect
    # calibration. Flag broken so it can't masquerade as worst=0%/PASS.
    broken = missing or not rows or worst > CAL_TOL
    return {"name": name, "worst_divergence": worst, "broken": broken,
            "missing": missing, "tilt_inverted": low_short and mid_long, "rows": rows}


def fmt_calibration(cr):
    if not cr:
        return ""
    s = [f"  octave T60 commanded-vs-realized (worst {cr['worst_divergence']*100:.0f}%"
         f"{', TILT INVERTED' if cr['tilt_inverted'] else ''}):"]
    for hz, cmd, got, div in cr["rows"]:
        flag = "  <-- DRIFT" if abs(div) > CAL_TOL else ""
        s.append(f"    {hz:6d} Hz  cmd {cmd:6.2f}s  got {got:6.2f}s  {div*100:+6.0f}%{flag}")
    return "\n".join(s)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preset", default=None)
    ap.add_argument("--verify-calibration", action="store_true",
                    help="algo10/12 commanded-vs-realized only; exit 1 on drift")
    ap.add_argument("--verify-tables", action="store_true",
                    help="source-only: flag dormant name-keyed map entries (no render); exit 1 if any")
    ap.add_argument("--no-render", action="store_true",
                    help="reuse existing /tmp/audit_* renders")
    ap.add_argument("--workers", type=int, default=3)
    args = ap.parse_args()

    # Source-only dormant-table check (no render). This is the cheapest, most
    # enforceable guard: it catches engine-migration orphans like the 6 octave
    # tables the DenseHall migration abandoned.
    dormant = dormant_table_entries()
    if args.verify_tables or not args.preset:
        print("=== DORMANT TABLE ENTRIES (config no engine reads) ===")
        if dormant:
            for mp, name, algo, allowed in dormant:
                print(f"  {mp}['{name}'] -> preset on algo {algo}, "
                      f"map only routes to algo {allowed} = IGNORED")
        else:
            print("  none - every keyed table entry routes to a live engine.")
        if args.verify_tables:
            return 1 if dormant else 0
        print()

    commanded = commanded_octave_t60()
    names = [args.preset] if args.preset else list(FLEET)
    for n in names:
        if n not in FLEET:
            print(f"unknown preset: {n}"); return 2

    def work(n):
        algo = preset_algo(n)
        txt, ok = render_and_check(n, no_render=args.no_render)
        if not ok:
            return {"name": n, "ok": False, "msg": txt.splitlines()[0]}
        nf = parse_nfail(txt)
        # Use the SAME consumer set as the live audit (TABLE_ENGINES): accurateHall_
        # also drives the SparseField(11) + TiledRoom(13) composites, so their
        # commanded octave T60 must be verified too — not just algos 10/12.
        cr = calibration_report(n, commanded.get(n), txt) \
            if algo in TABLE_ENGINES["kAccurateHallT60ByName"] else None
        return {"name": n, "ok": True, "algo": algo, "n_fail": nf, "cal": cr}

    with ThreadPoolExecutor(max_workers=max(1, args.workers)) as ex:
        results = list(ex.map(work, names))

    drift = [r for r in results if r.get("ok") and r.get("cal") and r["cal"]["broken"]]

    if args.verify_calibration:
        print("=== CALIBRATION VERIFICATION (AccurateHall consumers: algo 10/12/11/13) ===")
        any10 = False
        for r in sorted([x for x in results if x.get("ok") and x.get("cal")],
                        key=lambda x: -x["cal"]["worst_divergence"]):
            any10 = True
            print(f"\n{r['name']}  (algo {r['algo']}, n_fail {r['n_fail']})")
            print(fmt_calibration(r["cal"]))
        if not any10:
            print("  (no algo 10/12 presets measured)")
        # A render/check failure (ok=False) must also fail verification — else a
        # preset that never produced measurable output silently passes.
        failed = [r for r in results if not r.get("ok")]
        if drift or failed:
            if drift:
                print(f"\nFAIL: {len(drift)} preset(s) with octave-T60 drift > {CAL_TOL*100:.0f}%: "
                      + ", ".join(d["name"] for d in drift))
            if failed:
                print(f"FAIL: {len(failed)} preset(s) failed render/check: "
                      + ", ".join(f"{r['name']} ({r.get('msg', '?')})" for r in failed))
            return 1
        print("\nPASS: all AccurateHall presets realize their commanded T60.")
        return 0

    # full audit report
    print("=== DuskVerb FLEET AUDIT ===\n")
    print(f"{'preset':24s} {'algo':>4s} {'n_fail':>6s}  calibration")
    for r in sorted(results, key=lambda x: -(x.get("n_fail") or 0)):
        if not r.get("ok"):
            print(f"{r['name']:24s}  ----  {r['msg']}"); continue
        cal = ""
        if r.get("cal"):
            cal = (f"BROKEN ({r['cal']['worst_divergence']*100:.0f}%"
                   f"{', inverted' if r['cal']['tilt_inverted'] else ''})"
                   if r["cal"]["broken"] else "ok")
        print(f"{r['name']:24s} {str(r.get('algo')):>4s} {str(r.get('n_fail')):>6s}  {cal}")
    if drift:
        print(f"\n>>> {len(drift)} BROKEN CALIBRATION(S) - fix via calibrate_octave_t60.py:")
        for d in sorted(drift, key=lambda x: -x["cal"]["worst_divergence"]):
            print(f"\n{d['name']}:")
            print(fmt_calibration(d["cal"]))
    print(json.dumps([{k: v for k, v in r.items() if k != "cal"} for r in results]),
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
