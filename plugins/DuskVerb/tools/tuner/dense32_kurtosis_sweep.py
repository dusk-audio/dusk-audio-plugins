#!/usr/bin/env python3
"""Optimize the AccurateHall32 (Algorithm 12) 32-delay mode-spread so the FDN's
HF tail modes interleave into a uniform dense wash instead of the sparse metallic
clusters the naive doubled set produces.

Search: 32 prime-snapped delay variables, each bounded to a tight window around a
geometric (log-spaced) anchor. The anchor set's mean is rescaled to match the
shipping 16-line baseline mean, so the average loop delay (and therefore the
calibrated T60) stays put while Optuna micro-shifts individual spacings to
interleave harmonic poles. Realized mean is penalized so prime-snap/dedupe drift
can't move T60. Distinct primes (coprime) suppress flutter echoes.

Objective:
  PRIMARY  minimize aggregate 2-14 kHz tail spectral kurtosis toward target 12.0
  PENALTY  heavy penalty on any sub-band (2-4k, 4-6k, 6-9k, 9-14k) kurtosis > 6.0
  GUARD    small penalty on |mean(delays) - baseline_mean| to hold T60

Renders Bright Hall on Algorithm 12 via the DUSKVERB_FDN_DELAYS env override
(32-CSV) — no rebuild per trial.

Run from repo root:  python3 plugins/DuskVerb/tools/tuner/dense32_kurtosis_sweep.py --trials 100
"""
import argparse, os, sys, glob, shutil, subprocess
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import soundfile as sf, numpy as np, optuna
from scipy.signal import butter, sosfiltfilt

REPO = Path(__file__).resolve().parents[4]
REND = REPO / "build/tests/duskverb_render/duskverb_render"
ANCH = Path.home() / "projects/dusk-audio-tools/tuner_runs/anchors/vvv-bright-hall"
WET  = ["--program", "Bright Hall", "--param", "Algorithm=12",
        "--param", "Dry/Wet=1.0", "--param", "Bus Mode=1", "--param", "Freeze=0"]
SUB  = [(2000, 4000), (4000, 6000), (6000, 9000), (9000, 14000)]
TARGET_AGG = 12.0          # anchor aggregate 2-14k kurtosis
SUB_CAP    = 6.0           # any sub-band above this is heavily penalized

# Engine canonical 16-line baseline = FDNReverb.cpp kDefaultDelays first 16.
# This is "the working 16-line baseline" the ticket anchors the mean to.
BASE16 = [1151, 1289, 1447, 1619, 1823, 2039, 2287, 2579,
          2887, 3229, 3631, 4073, 4567, 5119, 5749, 6451]
# kDefaultDelays second 16 (interleaved primes) — the current naive AccurateHall32
# default. NAIVE32 = the real compiled default, the honest baseline column.
NEW16 = [1213, 1361, 1531, 1721, 1933, 2153, 2417, 2713,
         3041, 3413, 3833, 4297, 4817, 5417, 6079, 6469]
NAIVE32 = sorted(BASE16 + NEW16)
BASE_MEAN = float(np.mean(BASE16))


KMAX = 6700                # FDNReverb kMaxBaseDelay — clamp ceiling, must not collide

def primes_upto(n):
    s = np.ones(n + 1, dtype=bool); s[:2] = False
    for i in range(2, int(n ** 0.5) + 1):
        if s[i]: s[i * i::i] = False
    return np.flatnonzero(s)
PRIMES = primes_upto(7000)
PRIMES = PRIMES[PRIMES <= KMAX]    # never exceed the clamp ceiling (avoids dup-at-6700)


def nearest_prime(x):
    i = int(np.searchsorted(PRIMES, x))
    cands = PRIMES[max(0, i - 1):i + 2]
    return int(cands[np.argmin(np.abs(cands - x))])


# 32 geometric anchors over 1130..6300, rescaled so their mean == BASE_MEAN.
# Top kept below KMAX with window headroom so no prime snaps to the clamp ceiling.
_lo, _hi, _N = 1130.0, 6300.0, 32
_anchors = _lo * (_hi / _lo) ** (np.arange(_N) / (_N - 1))
_anchors *= BASE_MEAN / _anchors.mean()
ANCHORS = _anchors
# per-index window = 0.42 * local spacing (clamped) — keeps the search prime-tight.
_gap = np.gradient(ANCHORS)
WIN = np.clip(0.42 * _gap, 28, 240).astype(int)


def tail_kurt(p, lo, hi):
    x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim > 1 else x
    pk = int(np.argmax(np.abs(m))); seg = m[pk + int(0.3 * sr):pk + int(1.5 * sr)]
    if len(seg) < sr // 4: return None
    sos = butter(4, [lo, min(hi, sr * 0.49)], 'band', fs=sr, output='sos')
    y = sosfiltfilt(sos, seg)
    sp = np.abs(np.fft.rfft(y * np.hanning(len(y)))) + 1e-12
    fr = np.fft.rfftfreq(len(y), 1 / sr); b = (fr >= lo) & (fr <= hi); spb = sp[b]
    return float(((spb - spb.mean()) ** 4).mean() / (spb.var() ** 2 + 1e-30))


def _nearest_unused(target, used):
    """Nearest prime index to target whose prime is unused, spiralling outward."""
    c = int(np.clip(np.searchsorted(PRIMES, target), 0, len(PRIMES) - 1))
    for step in range(len(PRIMES)):
        for j in (c - step, c + step):            # 0, then ±1, ±2, ... (both dirs)
            if 0 <= j < len(PRIMES) and int(PRIMES[j]) not in used:
                return int(PRIMES[j])
    raise RuntimeError("prime pool exhausted")


def build_delays(offsets):
    """offsets[i] in [-WIN[i], WIN[i]] -> snapped, distinct, sorted 32 primes.
    Collisions fall back to the nearest unused prime in EITHER direction, so one
    near the KMAX ceiling steps downward instead of overshooting the clamp."""
    used = set(); out = []
    for i, off in enumerate(offsets):
        p = _nearest_unused(ANCHORS[i] + off, used)
        used.add(p); out.append(p)
    return sorted(out)


def render_kurts(delays, tag, keep=False):
    d = f"/tmp/d32_{tag}_{os.getpid()}"
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    try:
        env = dict(os.environ, DUSKVERB_FDN_DELAYS=",".join(str(x) for x in delays))
        try:
            r = subprocess.run([str(REND), *WET, "--output-dir", d], capture_output=True, env=env, timeout=180)
        except subprocess.TimeoutExpired:
            return None, None        # hung render → treat trial as failed
        nb = glob.glob(f"{d}/*_noiseburst.wav")
        if r.returncode != 0 or not nb: return None, None
        agg = tail_kurt(nb[0], 2000, 14000)
        sub = {b: tail_kurt(nb[0], *b) for b in SUB}
        return agg, sub
    finally:
        if not keep: shutil.rmtree(d, ignore_errors=True)   # don't retain per-trial renders


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trials", type=int, default=100)
    ap.add_argument("--workers", type=int, default=6)
    a = ap.parse_args()

    anb = str(ANCH / "vvv-bright-hall_noiseburst.wav")
    a_agg = tail_kurt(anb, 2000, 14000)
    a_sub = {b: tail_kurt(anb, *b) for b in SUB}
    print(f"anchor      agg2-14k={a_agg:.1f}  "
          f"sub={ {f'{l//1000}-{h//1000}k': round(a_sub[(l,h)],1) for l,h in SUB} }")
    n_agg, n_sub = render_kurts(NAIVE32, "naive", keep=True)
    if n_agg is None:
        print("naive-32    render FAILED (rc!=0 / timeout / no noiseburst)")
    else:
        print(f"naive-32    agg2-14k={n_agg:.1f}  "
              f"sub={ {f'{l//1000}-{h//1000}k': round(n_sub[(l,h)],1) for l,h in SUB} }")
    print(f"target agg={TARGET_AGG}  sub-cap={SUB_CAP}  base_mean={BASE_MEAN:.0f}\n")

    def obj(trial):
        offs = [trial.suggest_int(f"o{i}", -int(WIN[i]), int(WIN[i])) for i in range(_N)]
        delays = build_delays(offs)
        agg, sub = render_kurts(delays, str(trial.number))
        if agg is None: return 1e3
        loss = abs(agg - TARGET_AGG)                       # PRIMARY
        for b in SUB:                                      # PENALTY: heavy over-cap
            loss += 2.0 * max(0.0, sub[b] - SUB_CAP)
        loss += 0.012 * abs(float(np.mean(delays)) - BASE_MEAN)   # GUARD: hold T60
        trial.set_user_attr("delays", delays)
        trial.set_user_attr("agg", agg)
        trial.set_user_attr("sub", [round(sub[b], 1) for b in SUB])
        return loss

    study = optuna.create_study(direction="minimize",
                                sampler=optuna.samplers.TPESampler(seed=42))
    optuna.logging.set_verbosity(optuna.logging.WARNING)
    study.optimize(obj, n_trials=a.trials, n_jobs=a.workers)

    bt = study.best_trial
    best = bt.user_attrs["delays"]
    print(f"\nBEST  loss={study.best_value:.2f}  agg2-14k={bt.user_attrs['agg']:.1f}  "
          f"sub={bt.user_attrs['sub']}  mean={np.mean(best):.0f}")

    # Final scoreboard: naive-32 vs optimized vs anchor.
    b_agg, b_sub = render_kurts(best, "best", keep=True)
    # render_kurts(keep=True) leaves the dirs as /tmp/d32_<tag>_<pid>/ — match THIS
    # process's pid (not a bare wildcard) so a stale/concurrent run can't be picked up.
    naive_nb = glob.glob(f'/tmp/d32_naive_{os.getpid()}/*_noiseburst.wav')
    best_nb  = glob.glob(f'/tmp/d32_best_{os.getpid()}/*_noiseburst.wav')
    if b_agg is None or not naive_nb or not best_nb:
        print("\nFinal scoreboard skipped: a kept render failed (naive/best produced no noiseburst).")
    else:
        print(f"\n{'band':8s} {'naive32':>9s} {'optim':>9s} {'anchor':>9s}")
        rows = [("2-14k", (2000, 14000))] + [(f"{l//1000}-{h//1000}k", (l, h)) for l, h in SUB]
        for lab, (lo, hi) in rows:
            nk = tail_kurt(naive_nb[0], lo, hi)
            ok = tail_kurt(best_nb[0], lo, hi)
            ak = tail_kurt(anb, lo, hi)
            print(f"{lab:8s} {nk:9.1f} {ok:9.1f} {ak:9.1f}")

    print("\n// AccurateHall32 mode-spread (32 prime delays, mean-anchored to 16-line baseline)")
    print("static constexpr int kBrightHall32Delays[32] = {")
    for r in range(0, 32, 8):
        print("    " + ", ".join(f"{v:4d}" for v in best[r:r + 8]) + ",")
    print("};")

if __name__ == "__main__":
    main()
