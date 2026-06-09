#!/usr/bin/env python3
"""Closed-loop per-octave T60 calibrator for the AccurateHall engine (algo 10).

For each FDN preset, forces it onto algo 10 and iteratively corrects the nine
octave T60 targets in kAccurateHallT60ByName (PluginProcessor.cpp) so each
octave lands within ±5% of its VVV anchor. The achieved band T60 differs from
the target you pass (shelf transition slopes + in-loop structural HF/AA damping),
so we do a gain-match-style fixed point: target *= anchor/measured, rebuild,
re-measure, repeat.

Edits the map between the BEGIN/END_OCTAVE_T60_MAP markers, rebuilds the VST3 +
render, renders each preset forced to algo 10, measures, corrects. Run from the
repo root.
"""
import os, re, sys, subprocess, glob
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from full_check import _t60_band_schroeder

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))
PROC = os.path.join(REPO, "plugins/DuskVerb/src/PluginProcessor.cpp")
REND = os.path.join(REPO, "build/tests/duskverb_render/duskverb_render")
OUTD = os.path.join(REPO, "tests/duskverb_render/output")
ANCH = os.path.expanduser("~/projects/dusk-audio-tools/tuner_runs/anchors")
VVV  = os.path.join(REPO, "tests/duskverb_render/output/vvv")

BANDS = [(44,88),(88,177),(177,355),(355,710),(710,1420),
         (1420,2840),(2840,5680),(5680,11360),(11360,18000)]
# NB: NO "--param Algorithm=..." — render on the preset's NATIVE algo via
# --program. Forcing a normalized Algorithm value is divisor-dependent and broke
# when engine count changed (1.0 = highest index, which moved 10→11 when
# SparseField was added). The octave T60 setter routes to the GEQ tail engine,
# which is active iff the preset's native algo selects it (AccurateHall algo 10
# or SparseField algo 11). Calibrate presets on the algo they actually ship on.
WET = ["--param","Dry/Wet=1.0","--param","Bus Mode=1","--param","Freeze=0"]

# preset name -> anchor noiseburst path
ANCHORS = {
    "Vocal Plate":          f"{VVV}/vvv_vocal_plate_noiseburst.wav",
    "Drum Plate":           f"{VVV}/vvv_Drum_Plate_noiseburst.wav",
    "Vocal Hall":           f"{VVV}/vvv_Vocal_Hall_noiseburst.wav",
    "Cathedral Large Hall": f"{ANCH}/vvv-cathedral/vvv-cathedral_noiseburst.wav",
    "Blade Runner 224":     f"{ANCH}/vvv-blade-runner/vvv-blade-runner_noiseburst.wav",
    "Tiled Room":           f"{ANCH}/vvv-tiled-room/vvv-tiled-room_noiseburst.wav",
    "79 Vocal Chamber":     f"{ANCH}/vvv-79vc/vvv-79vc_noiseburst.wav",
    "Ambience":             f"{ANCH}/vvv-ambience/vvv-ambience_noiseburst.wav",
    "Bright Hall":          f"{ANCH}/vvv-bright-hall/vvv-bright-hall_noiseburst.wav",
}


def read_map():
    src = open(PROC).read()
    block = re.search(r"BEGIN_OCTAVE_T60_MAP.*?END_OCTAVE_T60_MAP", src, re.S).group(0)
    targets = {}
    for m in re.finditer(r'\{\s*"([^"]+)"\s*,\s*\{\{([^}]+)\}\}', block):
        name = m.group(1)
        vals = [float(x.strip().rstrip("f")) for x in m.group(2).split(",")]
        targets[name] = vals
    return targets


def write_map(targets):
    src = open(PROC).read()
    lines = ["            // BEGIN_OCTAVE_T60_MAP (maintained by tools/tuner/calibrate_octave_t60.py)"]
    for name, vals in targets.items():
        v = ", ".join(f"{x:.4f}f" for x in vals)
        lines.append(f'            {{ "{name}", {{{{ {v} }}}} }},')
    lines.append("            // END_OCTAVE_T60_MAP")
    new = re.sub(r"\s*// BEGIN_OCTAVE_T60_MAP.*?// END_OCTAVE_T60_MAP",
                 "\n" + "\n".join(lines), src, flags=re.S)
    open(PROC, "w").write(new)


def build():
    # CCACHE_DISABLE: ccache served STALE FactoryPresets/PluginProcessor objects
    # across iterations (the map edits weren't recompiled) → bogus per-octave T60
    # readings (reported 9/9 while a clean build measured 5/9). Disable the cache
    # so every iteration links the actual edited map. (memory: ccache staleness.)
    env = dict(os.environ, CCACHE_DISABLE="1")
    r = subprocess.run(["cmake","--build","build","--target","DuskVerb_VST3","duskverb_render",
                        f"-j{os.cpu_count()}"], cwd=REPO, capture_output=True, text=True, env=env)
    if r.returncode != 0:
        print(r.stdout[-3000:]); print(r.stderr[-3000:]); sys.exit("BUILD FAILED")


def render(name):
    for f in glob.glob(f"{OUTD}/*_noiseburst.wav"):
        try: os.remove(f)
        except OSError: pass
    r = subprocess.run([REND,"--program",name,*WET], cwd=REPO, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"render failed for {name!r} (rc={r.returncode})\n{r.stderr[-2000:]}")
    c = sorted(glob.glob(f"{OUTD}/*_noiseburst.wav"))
    if not c:
        raise RuntimeError(f"render produced no noiseburst WAV for {name!r}")
    return c[0]


def measure(dv, anchor):
    out = []
    for lo, hi in BANDS:
        d = _t60_band_schroeder(dv, lo, hi)
        a = _t60_band_schroeder(anchor, lo, hi)
        out.append((d, a))
    return out


def main():
    iters = int(sys.argv[1]) if len(sys.argv) > 1 else 3
    # Optional preset-name filter (args after the iter count) so a run can
    # calibrate only NEW presets without drifting already-shipped ones.
    only = set(sys.argv[2:])
    targets = read_map()
    names = [n for n in targets if n in ANCHORS and (not only or n in only)]
    for it in range(iters):
        write_map(targets); build()
        print(f"\n===== ITERATION {it+1}/{iters} =====")
        for name in names:
            dv = render(name)
            res = measure(dv, ANCHORS[name])
            npass = 0; new = list(targets[name])
            cells = []
            for k, (d, a) in enumerate(res):
                if d is None or a is None or a <= 0.05:
                    cells.append(f"{['63','125','250','500','1k','2k','4k','8k','16k'][k]}:skip")
                    continue
                pct = (d - a) / a * 100
                ok = abs(pct) <= 5; npass += ok
                new[k] = round(targets[name][k] * (a / d), 4)
                cells.append(f"{['63','125','250','500','1k','2k','4k','8k','16k'][k]}:{pct:+.0f}%")
            print(f"  {name:22s} {npass}/9  " + " ".join(cells))
            targets[name] = new
    write_map(targets); build()
    print("\nFinal targets written. Run full_check A/B next.")


if __name__ == "__main__":
    main()
