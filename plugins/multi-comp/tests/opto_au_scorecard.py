#!/usr/bin/env python3
"""Opto done-criteria scorecard through the actual Audio Units (R11a).

Same criteria, stimuli, native references and metric arithmetic as the lab
scorer (dusk-audio-tools .../programme_parity/opto/dc_score.py and the C++
core tests), but every MC-2 render goes through the installed AU hosted by
duskverb_render (48 kHz, 512-sample blocks, 2 s pre-roll, latency
compensated), never through a lab kernel.  Native (UAD) references are the
existing native renders or the fixture tables.  This script fits nothing.

  score <tag> [criteria...]     criteria: settled knee recovery dense twotone
                                pink link  (default: all)
  sidefx <plugin> <tag>         side-effect probes (LF Limit notch, sustained
                                Limit offset); plugin uad|mc2
  sidefx-report <tag_on> <tag_off>
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np
import soundfile as sf

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import opto_au_harmonic_map as M  # noqa: E402

TOOLS = Path.home() / "projects/dusk-audio-tools/plugins/MultiComp/tests/programme_parity/opto"
sys.path.insert(0, str(TOOLS))

REPO = M.REPO
FIXTURES = REPO / "plugins/multi-comp/core/tests/MultiCompOptoParityFixtures.hpp"
CORE_TESTS = REPO / "plugins/multi-comp/core/tests/MultiCompCoreTests.cpp"
OUT = M.OUT / "r11a-scorecard"
STAGE = OUT / "stimuli"
FS = M.FS
UNITY_GAIN_KNOB = 25.0          # dc_score's gain knob (normalised 0.25)
DYNAMICS_GAIN_KNOB = 32.1868896  # the C++ recovery/dense fixtures' Gain
WORKERS = int(os.environ.get("OPTO_SCORE_WORKERS", "4"))


# ------------------------------------------------------------------ AU rendering
def _mc2_params(pr: float, gain: float, limit: bool) -> list[tuple[str, float]]:
    return M.params("mc2", pr, gain, limit)


def _stage(path_or_array, name_hint: str) -> Path:
    """A uniquely named stimulus file in the staging directory."""
    STAGE.mkdir(parents=True, exist_ok=True)
    if isinstance(path_or_array, (str, Path)):
        src = Path(path_or_array)
        digest = hashlib.sha1(str(src).encode()).hexdigest()[:10]
        dst = STAGE / f"{name_hint}_{digest}.wav"
        if not dst.exists():
            dst.symlink_to(src)
        return dst
    x = np.asarray(path_or_array, dtype=np.float32)
    digest = hashlib.sha1(x.tobytes()).hexdigest()[:12]
    dst = STAGE / f"{name_hint}_{digest}.wav"
    if not dst.exists():
        tmp = dst.with_suffix(".tmp.wav")
        sf.write(tmp, x, FS, subtype="FLOAT")
        tmp.replace(dst)
    return dst


def _run(plugin: str, out: Path, inputs: list[Path], pr: float, gain: float,
         limit: bool, channels: int) -> None:
    out.mkdir(parents=True, exist_ok=True)
    argv = [str(M.HOST), "--au", str(M.plugin_path(plugin)), "--slug", "s",
            "--output-dir", str(out), "--sample-rate", str(FS),
            "--block-size", str(M.BLOCK), "--channels", str(channels),
            "--disable-aux-inputs", "--prerun-seconds", str(M.PRERUN),
            "--print-params", M.print_indices(plugin)]
    for name, value in M.params(plugin, pr, gain, limit):
        argv += ["--nparam", f"{name}={value:.9g}"]
    for p in inputs:
        argv += ["--input-wav", str(p)]
    r = subprocess.run(argv, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (out / "log.txt").write_text(r.stdout)
    if r.returncode:
        raise RuntimeError(f"AU host failed; see {out / 'log.txt'}")
    readbacks = M.READBACK.findall(r.stdout)
    want = dict(M.params(plugin, pr, gain, limit))
    worst = max(abs(float(n) - want[nm]) for _i, nm, n, _d in readbacks if nm in want)
    if worst > 1.23e-4:
        raise RuntimeError(f"parameter readback error {worst}")
    json.dump({"plugin": plugin, "component_binary_sha256": M.component_hash(plugin),
               "pr": pr, "gain": gain, "limit": limit, "channels": channels,
               "inputs": [str(p) for p in inputs]},
              open(out / "complete.json", "w"), indent=1)


class Renderer:
    """Collects (stimulus, setting) requests, renders them in batched AU runs."""

    def __init__(self, plugin: str, tag: str):
        self.plugin, self.tag = plugin, tag
        self.groups: dict = {}

    def want(self, stim: Path, pr: float, gain_knob: float, limit: bool,
             channels: int = 2) -> tuple:
        key = (round(pr, 6), round(gain_knob / 100.0, 9), bool(limit), channels)
        self.groups.setdefault(key, set()).add(stim)
        return key + (stim,)

    def _dir(self, key) -> Path:
        pr, gain, limit, ch = key
        return OUT / "renders" / self.tag / self.plugin / \
            f"pr{pr:.6f}_g{gain:.6f}_{'limit' if limit else 'comp'}_ch{ch}"

    def run(self) -> None:
        jobs = []
        for key, stims in self.groups.items():
            d = self._dir(key)
            missing = [s for s in sorted(stims) if not (d / f"s_{s.stem}_stem.wav").exists()]
            for i in range(0, len(missing), 64):
                jobs.append((key, d, missing[i:i + 64]))
        print(f"{self.plugin}/{self.tag}: {len(jobs)} AU runs", flush=True)

        def go(job):
            key, d, stims = job
            pr, gain, limit, ch = key
            _run(self.plugin, d / "_batch" / hashlib.sha1("".join(map(str, stims)).encode()).hexdigest()[:10],
                 stims, pr, gain, limit, ch)
        with ThreadPoolExecutor(WORKERS) as ex:
            list(ex.map(go, jobs))
        # Flatten batch outputs into the setting directory.
        for key in self.groups:
            d = self._dir(key)
            for f in d.glob("_batch/*/s_*_stem.wav"):
                target = d / f.name
                if not target.exists():
                    f.replace(target)

    def output(self, ref: tuple, n: int | None = None) -> np.ndarray:
        """Latency-compensated channel-0 output of one request."""
        key, stim = ref[:4], ref[4]
        y, _ = sf.read(self._dir(key) / f"s_{stim.stem}_stem.wav", always_2d=True, dtype="float64")
        lat = 87 if self.plugin == "uad" else M.MC2_LATENCY
        y = y[lat:, 0]
        return y if n is None else y[:n]

    def raw(self, ref: tuple) -> np.ndarray:
        key, stim = ref[:4], ref[4]
        y, _ = sf.read(self._dir(key) / f"s_{stim.stem}_stem.wav", always_2d=True, dtype="float64")
        return y[:, 0]


# ------------------------------------------------------------------ fixtures
STEADY_RE = re.compile(r"\{([-+0-9.eE]+)f,\s*([-+0-9.eE]+),\s*([-+0-9.eE]+)f,\s*"
                       r"([-+0-9.eE]+)f,\s*([-+0-9.eE]+)f,\s*(true|false)\}")


def steady_table(name: str, count: int) -> list[dict]:
    text = FIXTURES.read_text()
    body = text.split(f"inline constexpr std::array<Steady, {count}> {name}{{{{", 1)[1].split("}};", 1)[0]
    rows = [dict(level=float(a), f=float(b), pr=float(c), gain=float(d), ref=float(e), limit=g == "true")
            for a, b, c, d, e, g in STEADY_RE.findall(body)]
    assert len(rows) == count, (name, len(rows))
    return rows


def recovery_table() -> list[dict]:
    text = FIXTURES.read_text()
    a = text.index("inline constexpr std::array<Recovery, 20> recovery{{")
    b = text.index("}};", a + 60)
    body = text[a:b]
    heads = list(re.finditer(r"\{(-?[\d.]+)f, (-?[\d.]+)f, ([\d.]+)f, (\d+), (true|false), \{\{", body))
    rows = []
    for i, h in enumerate(heads):
        end = heads[i + 1].start() if i + 1 < len(heads) else len(body)
        vals = [float(x) for x in re.findall(r"(-?[\d.]+(?:e-?\d+)?)f", body[h.end():end])]
        assert len(vals) == 405
        rows.append(dict(ped=float(h.group(1)), event=float(h.group(2)), pr=float(h.group(3)),
                         dur=int(h.group(4)), held=h.group(5) == "true", trace=np.array(vals)))
    assert len(rows) == 20
    return rows


def dense_references() -> dict:
    text = CORE_TESTS.read_text()
    a = text.index("void testOptoDenseProgrammeParity()")
    out = {}
    for pr, name in ((40.0, "referenceAt40"), (70.0, "referenceAt70"), (85.0, "referenceAt85"),
                     (100.0, "referenceAt100")):
        i = text.index(f"std::array<float, 80> {name}{{{{", a)
        j = text.index("}};", i)
        vals = [float(x) for x in re.findall(r"(-?[\d.]+)f", text[text.index("{{", i) + 2:j])]
        assert len(vals) == 80
        out[pr] = np.array(vals)
    return out


DENSE_BARS = {40.0: 0.35, 70.0: 0.70, 85.0: 0.85, 100.0: 0.90}
EARLY = [3, 4, 5, 7, 10, 15, 20, 30, 50, 79]


# ------------------------------------------------------------------ criteria
def settled_like(R: Renderer, rows: list[dict], bar: float, name: str):
    reqs = []
    for i, r in enumerate(rows):
        t = np.arange(8 * FS) / FS
        x = 10 ** (r["level"] / 20) * np.sin(2 * np.pi * r["f"] * t)
        st = _stage(np.column_stack((x, x)), f"{name}{i:03d}")
        reqs.append((R.want(st, r["pr"] / 100.0, r["gain"], r["limit"]),
                     R.want(st, 0.0, r["gain"], r["limit"])))

    def finish():
        out = []
        b, e = 6 * FS, 15 * FS // 2
        for r, (ra, r0) in zip(rows, reqs):
            ya, y0 = R.output(ra), R.output(r0)
            meas = 10 * math.log10(np.sum(y0[b:e] ** 2) / np.sum(ya[b:e] ** 2))
            out.append({**r, "measured": meas, "error": meas - r["ref"],
                        "pass": abs(meas - r["ref"]) < bar})
        err = np.array([o["error"] for o in out])
        return dict(n=len(out), n_pass=sum(o["pass"] for o in out), worst=float(np.max(np.abs(err))),
                    rms=float(np.sqrt(np.mean(err ** 2))), bar=bar, rows=out)
    return finish


def recovery(R: Renderer):
    rows = recovery_table()
    reqs = []
    n = 9 * FS // 2
    start = 4 * FS
    k = np.arange(n)
    carrier = np.sin(2 * np.pi * 1000.0 * k / FS).astype(np.float32).astype(np.float64)
    for i, r in enumerate(rows):
        ped = 10 ** (r["ped"] / 20)
        ev = 10 ** (r["event"] / 20)
        without = ped * carrier
        amp = np.full(n, ped)
        amp[start:start + r["dur"] * FS // 1000] = ev
        with_ = amp * carrier
        sw = _stage(np.column_stack((with_, with_)), f"rec{i:02d}w")
        so = _stage(np.column_stack((without, without)), f"rec{i:02d}o")
        reqs.append((R.want(sw, r["pr"], DYNAMICS_GAIN_KNOB, False),
                     R.want(so, r["pr"], DYNAMICS_GAIN_KNOB, False), with_, without))

    def cycle_rms(x, b):
        c = x[b:b + 48]
        return math.sqrt(np.mean((c - np.mean(c)) ** 2))

    def locate(control):
        thr = np.max(np.abs(control)) * 0.10
        cross = int(np.argmax(np.abs(control) > thr))
        for s in range(cross - 1, max(0, cross - 96) - 1, -1):
            if control[s] <= 0.0 and control[s + 1] > 0.0:
                return s
        return cross

    def finish():
        out = []
        for r, (rw, ro, xin_w, xin_o) in zip(rows, reqs):
            yw, yo = R.raw(rw), R.raw(ro)
            s0 = locate(yo)
            trace = np.zeros(405)
            for off in range(-5, 400):
                ob = s0 + start + off * 48
                ib = start + off * 48
                inc = 20 * math.log10(cycle_rms(xin_w, ib) / cycle_rms(xin_o, ib))
                outr = 20 * math.log10(cycle_rms(yw, ob) / cycle_rms(yo, ob))
                trace[off + 5] = inc - outr
            ref = r["trace"]
            first = r["dur"] + 1
            ee = [trace[ms + 5] - ref[ms + 5] for ms in range(first, 80)]
            el = [trace[ms + 5] - ref[ms + 5] for ms in range(80, 400)]
            early_rms = math.sqrt(np.sum(np.square(ee)) / (80 - first))
            sparse = math.sqrt(np.mean([(trace[ms + 5] - ref[ms + 5]) ** 2 for ms in EARLY]))
            ok = early_rms < 0.75 and max(map(abs, ee)) < 1.0 and max(map(abs, el)) < 1.0 \
                and (r["dur"] != 2 or sparse < 0.50)
            out.append(dict(ped=r["ped"], event=r["event"], pr=r["pr"], dur=r["dur"], held=r["held"],
                            carrier_start=s0, early_rms=early_rms, early_max=max(map(abs, ee)),
                            late_max=max(map(abs, el)), sparse_rms=sparse, pass_=bool(ok)))
        return dict(n=len(out), n_pass=sum(o["pass_"] for o in out), rows=out)
    return finish


def dense(R: Renderer):
    prog = np.fromfile(STAGE.parent.parent / "r11a-scorecard-stimuli/dense.f32", dtype=np.float32).astype(np.float64)
    assert len(prog) == 80 * 4800
    st = _stage(np.column_stack((prog, prog)), "dense")
    ref0 = R.want(st, 0.0, DYNAMICS_GAIN_KNOB, False)
    refs = {pr: R.want(st, pr / 100.0, DYNAMICS_GAIN_KNOB, False) for pr in (40.0, 70.0, 85.0, 100.0)}
    refenv = dense_references()

    def finish():
        y0 = R.output(ref0)
        out = {}
        for pr, rq in refs.items():
            y = R.output(rq)
            env = np.array([10 * math.log10((np.sum(y0[f * 4800:(f + 1) * 4800] ** 2) + 1e-30)
                                            / (np.sum(y[f * 4800:(f + 1) * 4800] ** 2) + 1e-30)) for f in range(80)])
            ref = refenv[pr]
            rm, mm = ref.mean(), env.mean()
            rms = float(np.sqrt(np.mean((env - ref) ** 2)))
            corr = float(np.sum((ref - rm) * (env - mm)) / math.sqrt(max(np.sum((ref - rm) ** 2) * np.sum((env - mm) ** 2), 1e-30)))
            out[pr] = dict(mean=float(mm - rm), rms=rms, corr=corr,
                           pass_=bool(abs(mm - rm) < DENSE_BARS[pr] and rms < 0.5 and corr > 0.9))
        return dict(n=4, n_pass=sum(v["pass_"] for v in out.values()), rows=out)
    return finish


def _native(path: Path, n: int) -> np.ndarray:
    y, _ = sf.read(path, always_2d=True, dtype="float64")
    return y[87:87 + n, 0]


def twotone(R: Renderer):
    import dc_score as D
    meta = json.loads((D.COMBO / "stimuli" / "stimuli.json").read_text())["stimuli"]
    sets = {"p": 0.5, "q": 0.71875}
    tag = lambda pr: f"pr{pr:.6f}_comp"
    names = sorted(n for n in meta if n[0] in sets and n[1] == "_")
    reqs = []
    for n in names:
        st = _stage(D.COMBO / "stimuli" / f"{n}.wav", "tt_" + n)
        reqs.append((n, R.want(st, sets[n[0]], UNITY_GAIN_KNOB, False), R.want(st, 0.0, UNITY_GAIN_KNOB, False)))

    def finish():
        a, b = int(1.5 * FS), int(2.9 * FS)
        cells, singles = [], []
        for n, ra, r0 in reqs:
            x, _ = sf.read(D.COMBO / "stimuli" / f"{n}.wav", always_2d=True)
            L = len(x)
            ym, ym0 = R.output(ra, L), R.output(r0, L)
            yn = _native(D.COMBO / n[0] / "native" / tag(sets[n[0]]) / f"s_{n}_stem.wav", L)
            y0n = _native(D.COMBO / n[0] / "native" / tag(0.0) / f"s_{n}_stem.wav", L)
            m = 10 * math.log10(np.sum(ym0[a:b] ** 2) / np.sum(ym[a:b] ** 2))
            nat = 10 * math.log10(np.sum(y0n[a:b] ** 2) / np.sum(yn[a:b] ** 2))
            row = dict(name=n, e=m - nat, nat=nat, model=m)
            (cells if meta[n]["kind"] == "mix" else singles).append(row)
        e = np.array([c["e"] for c in cells])
        npass = int(np.sum(np.abs(e) <= 0.3))
        es = np.array([s["e"] for s in singles])
        return dict(n=len(cells), n_pass=npass, share=npass / len(cells), pass_=npass >= 0.9 * len(cells),
                    rms=float(np.sqrt(np.mean(e ** 2))), mean=float(e.mean()),
                    singles=dict(n=len(es), within=int(np.sum(np.abs(es) <= 0.3)),
                                 rms=float(np.sqrt(np.mean(es ** 2)))),
                    cells=cells, single_cells=singles)
    return finish


def pink(R: Renderer):
    import dc_score as D
    import t4_p2_extract as X
    shapes = ["pnF", "tt1", "tt2"] + [f"ob{f}" for f in (63, 125, 250, 500, 1000, 2000, 4000, 8000)]
    meta = json.loads((D.BB / "stimuli" / "stimuli.json").read_text())["stimuli"]
    names = sorted(n for n, m in meta.items() if m["shape"] in shapes)
    prs = (0.40625, 0.71875, 1.0)
    tag = lambda pr: f"pr{pr:.6f}_comp"
    bb = []
    for n in names:
        st = _stage(D.BB / "stimuli" / f"{n}.wav", "bb_" + n)
        bb.append((n, R.want(st, 0.0, UNITY_GAIN_KNOB, False),
                   [(pr, R.want(st, pr, UNITY_GAIN_KNOB, False)) for pr in prs]))
    ctags = [t for t in X.tags("c") if t != X.PR0_TAG]
    xp = X.stim("c_pink")[:, 0]
    stp = _stage(np.column_stack((xp, xp)), "c_pink")
    cp0 = R.want(stp, 0.0, UNITY_GAIN_KNOB, False)
    cps = []
    for t in ctags:
        pr, lim = X.parse_tag(t)
        cps.append((t, pr, int(lim), R.want(stp, pr, UNITY_GAIN_KNOB, bool(lim))))

    def summ(rows, bar):
        e = np.array([r["e"] for r in rows])
        return dict(n=len(rows), n_pass=int(np.sum(np.abs(e) <= bar)), pass_=bool(np.all(np.abs(e) <= bar)),
                    rms=float(np.sqrt(np.mean(e ** 2))), worst=float(np.max(np.abs(e))))

    def finish():
        a, b = int(1.5 * FS), int(3.9 * FS)
        rows = {}
        for n, r0, sets in bb:
            x, _ = sf.read(D.BB / "stimuli" / f"{n}.wav", always_2d=True)
            L = len(x)
            ym0 = R.output(r0, L)
            y0n = _native(D.BB / "b" / "native" / tag(0.0) / f"s_{n}_stem.wav", L)
            for pr, rq in sets:
                ym = R.output(rq, L)
                yn = _native(D.BB / "b" / "native" / tag(pr) / f"s_{n}_stem.wav", L)
                m = 10 * math.log10(np.sum(ym0[a:b] ** 2) / np.sum(ym[a:b] ** 2))
                nat = 10 * math.log10(np.sum(y0n[a:b] ** 2) / np.sum(yn[a:b] ** 2))
                rows.setdefault(meta[n]["shape"], []).append(dict(name=n, pr=pr, e=m - nat, nat=nat, model=m))
        meta_c = X.smeta()["c_pink"]
        edges = X.edges_of(meta_c)
        L = len(xp)
        ym0 = R.output(cp0, L)
        y0n = X.align(X.stem("c", X.PR0_TAG, "c_pink")[:, 0], 87, L)
        cp = []
        for t, pr, lim, rq in cps:
            ym = R.output(rq, L)
            yn = X.align(X.stem("c", t, "c_pink")[:, 0], 87, L)
            gn, kn = D._power_gr(xp, yn, edges, y0n)
            gm, km = D._power_gr(xp, ym, edges, ym0)
            LL = min(len(gm), len(gn))
            k = kn[:LL] & km[:LL] & np.isfinite(gm[:LL]) & np.isfinite(gn[:LL])
            e = (gm[:LL] - gn[:LL])[k]
            cp.append(dict(name="c_pink", tag=t, pr=pr, lim=lim, e=float(np.mean(e)),
                           nat=float(np.mean(gn[:LL][k])), model=float(np.mean(gm[:LL][k]))))
        full = rows["pnF"] + cp
        octv = [r for s in shapes if s.startswith("ob") for r in rows.get(s, [])]
        f_, o_ = summ(full, 0.3), summ(octv, 0.15)
        return dict(pass_=f_["pass_"] and o_["pass_"], full=f_, octave=o_,
                    tt1=summ(rows["tt1"], 0.3), tt2=summ(rows["tt2"], 0.3), cells=dict(full=full, octave=octv))
    return finish


def link(R: Renderer):
    import dc_score as D
    import r20_p as RP
    import mus_kernel as K
    cells, c2, c3, c4 = D.link_cells()
    reqs = {}

    def want(c, name, mode, f):
        key = (c["src"], c["fam"], name, c["pr"], c["lim"], mode, f)
        if key not in reqs:
            x, _ = sf.read(D._stim_path(c, name), always_2d=True)
            left, right = (x[:, 1], x[:, 0]) if mode == "swap" else (x[:, 0], x[:, 1])
            st = _stage(np.column_stack((left, right)), f"lk_{name}_{mode}")
            reqs[key] = (left, f, R.want(st, c["pr"], UNITY_GAIN_KNOB, bool(c["lim"])),
                         R.want(st, 0.0, UNITY_GAIN_KNOB, bool(c["lim"])))
        return key
    for c in c2 + c3:
        c["_k"] = (want(c, c["name"], "plain", 3200), want(c, c["silent"], "plain", 3200))
    for c in c4:
        c["_k"] = (want(c, c["name"], "swap", c["f"]), want(c, c["ref60"], "swap", c["f"]))
    mono_src = [("tone", D._stim_path(c, c["name"])) for c in cells
                if c["src"] == "ck" and c["dep"] in ("b0", "b1") and c["f"] == 1025]
    mono_src += [("clip", cl) for cl in K.clips("v")]
    mono = []
    for kind, src in mono_src:
        if kind == "clip":
            xm = K.load_clip(src)["x"][:, 0]
        else:
            xm = sf.read(src, always_2d=True)[0][:, 0]
        s2 = _stage(np.column_stack((xm, xm)), "c1s")
        s1 = _stage(xm[:, None], "c1m")
        mono.append((R.want(s2, 1.0, UNITY_GAIN_KNOB, False, 2), R.want(s1, 1.0, UNITY_GAIN_KNOB, False, 1), len(xm)))

    def finish():
        a, b = int(3.5 * FS), int(5.8 * FS)
        G = {}
        for key, (left, f, ra, r0) in reqs.items():
            L = len(left)
            g = {}
            for prk, rq in ((key[3], ra), (0.0, r0)):
                y = R.output(rq, L)
                g[prk] = 20 * math.log10(abs(RP.demod(left, y, f, a, b)))
            G[key] = g[0.0] - g[key[3]]
        o2 = [dict(nat=c["nat_dL"], model=G[c["_k"][0]] - G[c["_k"][1]]) for c in c2]
        o3 = [dict(nat=c["nat_dL"], model=G[c["_k"][0]] - G[c["_k"][1]]) for c in c3]
        o4 = [dict(nat=c["nat_S"], model=G[c["_k"][0]] - G[c["_k"][1]]) for c in c4]
        d2 = np.array([abs(r["model"] - r["nat"]) for r in o2])
        r3 = [(r["model"] - r["nat"]) / r["nat"] for r in o3]
        r4 = [(r["model"] - r["nat"]) / r["nat"] for r in o4]
        c1 = []
        for r2, r1, n in mono:
            ys, ym = R.output(r2, n), R.output(r1, n)
            big = np.abs(ym) > 1e-4
            with np.errstate(divide="ignore", invalid="ignore"):
                ddb = np.abs(20 * np.log10(np.abs(ys[big]) / np.abs(ym[big])))
            c1.append(dict(max_db=float(np.nanmax(ddb)) if big.any() else 0.0,
                           max_out=float(np.max(np.abs(ys - ym)))))
        out = dict(C1=dict(n=len(c1), max_db=max(c["max_db"] for c in c1), max_out=max(c["max_out"] for c in c1),
                           note="AU has no cell-gain probe: output ratio L=R stereo vs mono instance"),
                   C2=dict(n=len(o2), n_pass=int(np.sum(d2 <= 0.005)), worst=float(d2.max())),
                   C3=dict(rel=r3, pass_=all(abs(x) <= 0.25 for x in r3)),
                   C4=dict(rel=r4, pass_=all(abs(x) <= 0.25 for x in r4)))
        out["C1"]["pass_"] = out["C1"]["max_db"] <= 1e-6
        out["C2"]["pass_"] = out["C2"]["n_pass"] == out["C2"]["n"]
        out["pass_"] = all(out[k]["pass_"] for k in ("C1", "C2", "C3", "C4"))
        return out
    return finish


CRITERIA = ("settled", "knee", "recovery", "dense", "twotone", "pink", "link")


def score(tag: str, crits) -> None:
    R = Renderer("mc2", tag)
    finishers = {}
    for c in crits:
        if c == "settled":
            finishers[c] = settled_like(R, steady_table("steady", 122), 0.5, "st")
        elif c == "knee":
            finishers[c] = settled_like(R, steady_table("knee", 9), 0.1, "kn")
        else:
            finishers[c] = globals()[c](R)
    R.run()
    result = {c: f() for c, f in finishers.items()}
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / f"scorecard-{tag}.json"
    old = json.loads(path.read_text()) if path.exists() else {}
    old.update(result)
    old["component_binary_sha256"] = M.component_hash("mc2")
    path.write_text(json.dumps(old, indent=1, default=float) + "\n")
    for c, r in result.items():
        brief = {k: v for k, v in r.items() if k not in ("rows", "cells", "single_cells")}
        print(c, json.dumps(brief, default=float)[:600])


# ------------------------------------------------------------------ side effects
SFX_LF = [(f, lvl, pr) for f in (50, 100) for lvl in (-18, -12, -8) for pr in (0.35, 0.50, 0.70, 0.85)]
SFX_T8 = [(-24,), (-12,), (-3,)]


def sidefx(plugin: str, tag: str) -> None:
    R = Renderer(plugin, tag)
    t = np.arange(10 * FS) / FS
    reqs = {}
    for f, lvl, pr in SFX_LF:
        x = 10 ** (lvl / 20) * np.sin(2 * np.pi * f * t)
        st = _stage(np.column_stack((x, x)), f"sfxlf{f}_{lvl}")
        reqs[("lf", f, lvl, pr)] = (x, R.want(st, pr, UNITY_GAIN_KNOB, True), R.want(st, 0.0, UNITY_GAIN_KNOB, True))
    for (lvl,) in SFX_T8:
        x = 10 ** (lvl / 20) * np.sin(2 * np.pi * 1000.0 * t)
        st = _stage(np.column_stack((x, x)), f"sfxt8_{lvl}")
        reqs[("t8", 1000, lvl, 0.60)] = (x, R.want(st, 0.60, UNITY_GAIN_KNOB, True), R.want(st, 0.0, UNITY_GAIN_KNOB, True))
    R.run()
    rows = []
    b, e = 7 * FS, 9 * FS
    for key, (x, ra, r0) in reqs.items():
        ya, y0 = R.output(ra, len(x)), R.output(r0, len(x))
        gr = 10 * math.log10(np.sum(y0[b:e] ** 2) / np.sum(ya[b:e] ** 2))
        # Within-cycle gain: output/input where the input is near its zero
        # crossings (5-25% of peak) against near its peaks (> 70%).
        xs, ys = x[b:e], ya[b:e]
        pk = np.max(np.abs(xs))
        near_zero = (np.abs(xs) > 0.05 * pk) & (np.abs(xs) < 0.25 * pk)
        near_peak = np.abs(xs) > 0.70 * pk
        g = ys / np.where(np.abs(xs) > 1e-12, xs, 1.0)
        notch = 20 * math.log10(np.median(g[near_zero]) / np.median(g[near_peak]))
        c = M.complex_harmonic(ys[:FS], key[1])
        h = {k: 20 * math.log10(max(abs(M.complex_harmonic(ys[:FS], k * key[1])), 1e-30)) for k in (3, 5, 7)}
        rows.append(dict(kind=key[0], f=key[1], level=key[2], pr=key[3], gr_db=gr, zero_vs_peak_gain_db=notch,
                         h1_dbfs=20 * math.log10(abs(c)), h3=h[3], h5=h[5], h7=h[7]))
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / f"sidefx-{plugin}-{tag}.json").write_text(json.dumps(rows, indent=1) + "\n")
    print(f"{len(rows)} side-effect rows for {plugin}/{tag}")


def main() -> None:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("score"); p.add_argument("tag"); p.add_argument("crits", nargs="*")
    p = sub.add_parser("sidefx"); p.add_argument("plugin", choices=("uad", "mc2")); p.add_argument("tag")
    a = ap.parse_args()
    if a.cmd == "score":
        score(a.tag, a.crits or CRITERIA)
    else:
        sidefx(a.plugin, a.tag)


if __name__ == "__main__":
    main()
