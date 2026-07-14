#!/usr/bin/env python3
"""program_probe.py — drum-program comparison of TapeMachine 2 vs the UAD deck.

The level_probe surfaces show mine's FR drifts with SIGNAL LEVEL (HF droops, LF
thickens above ~-12 dBFS) where the UAD's does not. A high-crest drum hit sweeps
that whole level range on every transient, so this probe renders a SYNTHESIZED
drum loop (kick + snare + hats, ~110 BPM, peaks near -6 dBFS, deterministic so it
is checkable-in) through both plugins and asks two questions:

  (a) LTAS diff per 1/3-octave band 30 Hz-16 kHz  (loudness-matched) — the tonal
      difference you actually hear on the full program.
  (b) TRANSIENT vs SUSTAIN split: spectra of the 0-20 ms post-onset windows vs
      the rest, diffed mine-UAD. If the transient-window diff EXCEEDS the
      sustain-window diff, the tonal error is concentrated in the loud transients
      — i.e. it is level/crest-linked, the smoking gun for H1.

W&F + hiss/hum are OFF (decorrelated, don't null). Onsets are known from the
synth, latency-aligned per render via cross-correlation.

  python3 program_probe.py            # all cases
  python3 program_probe.py "Drum"     # substring filter
  python3 program_probe.py --wf-on    # re-render presets with W&F+noise at shipped values
"""
import os, sys, json, tempfile, shutil, subprocess, itertools
from concurrent.futures import ThreadPoolExecutor
import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from preset_validate import (parse_presets, decode_uad, uad_nparams, mine_params,
                             UAD_JSON, ATR, STUDER, MINE, BIN)  # noqa: E402
from level_probe import ref_mine, ref_uad, TARGET_PRESETS  # noqa: E402

SR = 48000
OUT = os.path.join(HERE, "renders", "program_probe")
_rc = itertools.count()

# 1/3-octave centres (IEC) 31.5 Hz .. 16 kHz
THIRD_OCT = [31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630,
             800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000,
             10000, 12500, 16000]


def synth_drums(seconds=8.0, bpm=110.0, seed=7):
    """Deterministic kick+snare+hat loop. Returns (mono float32, onset_samples).
    onset_samples = kick+snare hits (the high-crest transients)."""
    rng = np.random.default_rng(seed)
    n = int(seconds * SR)
    x = np.zeros(n)
    step = 60.0 / bpm / 4.0                       # 16th-note grid
    nsteps = int(seconds / step)
    onsets = []

    def env(a, dur):
        m = int(dur * SR)
        return np.exp(-np.linspace(0, a, m)), m

    for s in range(nsteps):
        t0 = int(s * step * SR)
        beat16 = s % 16
        # KICK on 1 & 3 (steps 0,8) + a syncopated 11
        if beat16 in (0, 8, 11):
            e, m = env(9.0, 0.28)
            if t0 + m > n: m = n - t0; e = e[:m]
            tt = np.arange(m) / SR
            f = 50 + 70 * np.exp(-tt / 0.03)      # 120 -> 50 Hz pitch drop
            ph = 2 * np.pi * np.cumsum(f) / SR
            click = np.exp(-tt / 0.002) * 0.5
            x[t0:t0 + m] += (np.sin(ph) + click) * e * 0.9
            onsets.append(t0)
        # SNARE on 2 & 4 (steps 4,12)
        if beat16 in (4, 12):
            e, m = env(7.0, 0.18)
            if t0 + m > n: m = n - t0; e = e[:m]
            tt = np.arange(m) / SR
            body = np.sin(2 * np.pi * 185 * tt) * 0.4
            noise = rng.standard_normal(m) * 0.9
            x[t0:t0 + m] += (noise + body) * e * 0.6
            onsets.append(t0)
        # HATS on every 8th (even steps)
        if beat16 % 2 == 0:
            e, m = env(14.0, 0.05)
            if t0 + m > n: m = n - t0; e = e[:m]
            hn = rng.standard_normal(m)
            # crude HF emphasis: differentiate (1st diff) twice
            hn = np.diff(np.concatenate([[0], hn]))
            hn = np.diff(np.concatenate([[0], hn]))
            x[t0:t0 + m] += hn / (np.max(np.abs(hn)) + 1e-9) * e * 0.28

    x[:200] *= np.linspace(0, 1, 200)
    x /= (np.max(np.abs(x)) + 1e-9)
    x *= 10 ** (-6.0 / 20.0)                        # peak ~ -6 dBFS
    return x.astype(np.float32), np.array(sorted(onsets))


def build_stimulus():
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, "drumloop.wav")
    mono, onsets = synth_drums()
    sf.write(path, np.column_stack([mono, mono]), SR, subtype="FLOAT")
    return path, mono, onsets


def _pace_up():
    """UAD plugins bypass (pristine passthrough) when the PACE licensing daemon is down."""
    return os.path.isdir("/var/tmp/com.paceap.eden.licensed")


def render(plugin, params, inp, mode, tag):
    # Reject bypassed UAD output up front (matches the other probes' PACE guard): a
    # passthrough render would pollute the LTAS/transient comparison with the raw input.
    if plugin in (ATR, STUDER) and not _pace_up():
        raise RuntimeError(f"render {tag}: PACE down — UAD {os.path.basename(plugin)} would BYPASS")
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


def load_mono(p):
    x, _ = sf.read(p)
    return (x.mean(1) if x.ndim > 1 else x).astype(np.float64)


def align_lag(a, b):
    """integer lag that best aligns b to a (roll b by +lag)."""
    n = min(len(a), len(b))
    s0, s1 = int(n * 0.1), int(n * 0.9)
    aw, bw = a[s0:s1], b[s0:s1]
    L = len(aw)
    nf = 1 << int(np.ceil(np.log2(2 * L)))
    xc = np.fft.irfft(np.fft.rfft(aw, nf) * np.conj(np.fft.rfft(bw, nf)), nf)
    lag = int(np.argmax(xc))
    return lag - nf if lag > nf // 2 else lag


def loud_region(x):
    thr = np.max(np.abs(x)) * 1e-3
    nz = np.where(np.abs(x) > thr)[0]
    return (nz[0], nz[-1] + 1) if len(nz) else (0, len(x))


def third_oct_levels(x):
    """power per 1/3-oct band (dB) over signal x."""
    w = x * np.hanning(len(x))
    X = np.abs(np.fft.rfft(w)) ** 2
    f = np.fft.rfftfreq(len(w), 1 / SR)
    out = []
    for fc in THIRD_OCT:
        lo, hi = fc / 2 ** (1 / 6), fc * 2 ** (1 / 6)
        band = (f >= lo) & (f < hi)
        p = np.sum(X[band])
        out.append(10 * np.log10(p + 1e-20))
    return np.array(out)


_K1K = THIRD_OCT.index(1000)


def rel1k(v):
    """normalise a 1/3-oct level vector to its 1 kHz band (tonal tilt, level-free)."""
    return v - v[_K1K]


def windowed_spec(x, wins):
    """average power spectrum (1/3-oct) over a set of (start,len) windows."""
    acc = np.zeros(len(THIRD_OCT))
    tot = 0
    for a, ln in wins:
        if a < 0 or a + ln > len(x):
            continue
        acc = acc + 10 ** (third_oct_levels(x[a:a + ln]) / 10)
        tot += 1
    return 10 * np.log10(acc / max(tot, 1) + 1e-20)


def analyse(m_out, u_out, in_mono, onsets):
    m = load_mono(m_out); u = load_mono(u_out)
    # latency-align both to the input timeline
    lm = align_lag(in_mono, m); lu = align_lag(in_mono, u)
    m = np.roll(m, lm); u = np.roll(u, lu)   # now onset[k] in input time ~ same in m,u
    lo, hi = loud_region(in_mono)
    ms, us = m[lo:hi], u[lo:hi]
    # loudness-match u to m over the loud region (equal integrated RMS)
    km = np.sqrt(np.mean(ms ** 2)); ku = np.sqrt(np.mean(us ** 2))
    if ku > 1e-9:
        u = u * (km / ku); us = us * (km / ku)
    # All diffs are rel-1k (tonal tilt) so they're immune to the loudness match and
    # to transient-shape level differences — matching level_probe's rel-1k DELTA.
    # (a) full-program LTAS diff
    ltas_d = rel1k(third_oct_levels(ms)) - rel1k(third_oct_levels(us))
    # (b) transient (0-20 ms post onset) vs sustain (20-120 ms). NOTE a 20 ms window
    # resolves ~50 Hz, so 1/3-oct bands below ~63 Hz in the TRANSIENT column are not
    # reliable (too few FFT bins) — read >=80 Hz there; LTAS/sustain carry the LF.
    tw = int(0.020 * SR); s_a = int(0.020 * SR); s_len = int(0.100 * SR)
    trans = [(o, tw) for o in onsets]
    sust = [(o + s_a, s_len) for o in onsets]
    tr_d = rel1k(windowed_spec(m, trans)) - rel1k(windowed_spec(u, trans))
    su_d = rel1k(windowed_spec(m, sust)) - rel1k(windowed_spec(u, sust))
    return ltas_d, tr_d, su_d


def build_cases(filt, wf_on):
    ps = {p["name"]: p for p in parse_presets()}
    cases = []
    for mac, ubin, deck in [(0, STUDER, "A800"), (1, ATR, "ATR")]:
        cases.append((f"REF-{deck}", deck, ref_mine(mac), "param", ref_uad(mac), ubin))
    for name in TARGET_PRESETS:
        p = ps[name]
        vec, _ = decode_uad(p["machine"], UAD_JSON[name])
        ubin = ATR if p["machine"] == 1 else STUDER
        deck = "ATR" if p["machine"] == 1 else "A800"
        mp = mine_params(p)
        up = uad_nparams(p["machine"], vec)
        if wf_on:                     # restore shipped W&F + noise on both sides
            mp = [(k, v) for k, v in mp if k not in
                  ("Wow", "Flutter", "Noise Amount", "Noise Enabled")]
            mp += [("Wow", p["wow"] / 100.0), ("Flutter", p["flutter"] / 100.0),
                   ("Noise Amount", p["noise"] / 100.0), ("Noise Enabled", 1)]
            vecw = dict(vec)
            up = [(k, val) for k, val in vecw.items()]   # factory W&F/noise as-is
        cases.append((name, deck, mp, "nparam", up, ubin))
    return [c for c in cases if filt.lower() in c[0].lower()]


def run_case(c, stim, in_mono, onsets):
    name, deck, mp, mode, up, ubin = c
    m_out = render(MINE, mp, stim, "param", f"m_{name.replace(' ','_')}")
    u_out = render(ubin, up, stim, mode, f"u_{name.replace(' ','_')}")
    ltas, tr, su = analyse(m_out, u_out, in_mono, onsets)
    return name, deck, ltas, tr, su


def fmt_band(vec, label):
    # compact: print every other band to fit width, but keep 30-16k anchors
    idx = list(range(0, len(THIRD_OCT), 2))
    hz = " ".join(f"{THIRD_OCT[i]:>6.0f}" for i in idx)
    vals = " ".join(f"{vec[i]:+6.1f}" for i in idx)
    return f"  {label}\n   Hz {hz}\n   dB {vals}"


def main():
    wf_on = "--wf-on" in sys.argv
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    filt = args[0] if args else ""
    stim, in_mono, onsets = build_stimulus()
    cases = build_cases(filt, wf_on)
    print(f"program_probe: {len(cases)} case(s), {len(onsets)} onsets, "
          f"W&F/noise {'ON' if wf_on else 'OFF'}\n")
    results = {}
    with ThreadPoolExecutor(max_workers=6) as ex:
        futs = [ex.submit(run_case, c, stim, in_mono, onsets) for c in cases]
        for f in futs:
            name, deck, ltas, tr, su = f.result()
            results[name] = dict(deck=deck, ltas=ltas.tolist(),
                                 trans=tr.tolist(), sust=su.tolist())
            # HF summary: mean |diff| over 8-16k for LTAS/trans/sust
            hf = [i for i, fc in enumerate(THIRD_OCT) if fc >= 8000]
            lf = [i for i, fc in enumerate(THIRD_OCT) if fc <= 63]
            def band_mean(v, ix): return float(np.mean([v[i] for i in ix]))
            # Mean ABSOLUTE error for the crest-linkage test: averaging signed band diffs
            # lets a +HF band cancel a -HF band and understate the tonal error.
            def band_mae(v, ix): return float(np.mean([abs(v[i]) for i in ix]))
            print("=" * 92)
            print(f"### {name}  ({deck})   [+ = mine louder than UAD]")
            print(fmt_band(ltas, "LTAS diff (full program, loudness-matched)"))
            print(fmt_band(tr, "TRANSIENT-window diff (0-20 ms post onset)"))
            print(fmt_band(su, "SUSTAIN-window diff (20-120 ms)"))
            print(f"  HF(>=8k) mean diff: LTAS {band_mean(ltas,hf):+.1f}  "
                  f"transient {band_mean(tr,hf):+.1f}  sustain {band_mean(su,hf):+.1f}   "
                  f"| LF(<=63) LTAS {band_mean(ltas,lf):+.1f} "
                  f"trans {band_mean(tr,lf):+.1f} sust {band_mean(su,lf):+.1f}")
            print(f"  crest linkage: |transient HF diff| {band_mae(tr,hf):.1f} vs "
                  f"|sustain HF diff| {band_mae(su,hf):.1f} "
                  f"-> {'TRANSIENT-dominated (H1)' if band_mae(tr,hf)>band_mae(su,hf)+0.3 else 'similar'}\n")
    tag = "wfon" if wf_on else "wfoff"
    dump = os.path.join(OUT, f"program_bands_{tag}.json")
    json.dump(dict(bands=THIRD_OCT, cases=results), open(dump, "w"), indent=1)
    print(f"bands written: {dump}")


if __name__ == "__main__":
    main()
