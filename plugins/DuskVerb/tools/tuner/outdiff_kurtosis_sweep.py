#!/usr/bin/env python3
"""Optimize the Bright Hall post-tank OutputDiffusion params to smear the FDN's
sparse HF tail modes toward the anchor's dense wash. Objective = overall 2-14k
tail spectral kurtosis + the 4-6k / 6-9k / 9-14k sub-band kurtosis, driven
toward the anchor baselines. Renders via the DUSKVERB_OUTDIFF env override
(amount,lfoScale,delayScale) — no rebuild per trial. Holds a light guard on the
broadband HF level so the smear doesn't dull the (bright) preset.

Run from repo root:  python3 outdiff_kurtosis_sweep.py --trials 160
"""
import argparse, os, sys, glob, shutil, subprocess
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import soundfile as sf, numpy as np, optuna
from scipy.signal import butter, sosfiltfilt

REPO = Path(__file__).resolve().parents[4]
REND = REPO / "build/tests/duskverb_render/duskverb_render"
ANCH = Path.home() / "projects/dusk-audio-tools/tuner_runs/anchors/vvv-bright-hall"
WET  = ["--param","Dry/Wet=1.0","--param","Bus Mode=1","--param","Freeze=0"]
SUB  = [(2000,4000),(4000,6000),(6000,9000),(9000,14000)]   # last 3 are the spike bands

def tail_kurt(p, lo, hi):
    x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim > 1 else x
    pk = int(np.argmax(np.abs(m))); seg = m[pk+int(0.3*sr):pk+int(1.5*sr)]
    if len(seg) < sr//4: return None
    sos = butter(4, [lo, min(hi, sr*0.49)], 'band', fs=sr, output='sos')
    y = sosfiltfilt(sos, seg)
    sp = np.abs(np.fft.rfft(y*np.hanning(len(y)))) + 1e-12
    fr = np.fft.rfftfreq(len(y), 1/sr); b = (fr>=lo)&(fr<=hi); spb = sp[b]
    return float(((spb-spb.mean())**4).mean() / (spb.var()**2 + 1e-30))

def hf_level(p, lo=4000, hi=12000):
    x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim > 1 else x
    sp = np.abs(np.fft.rfft(m)); fr = np.fft.rfftfreq(len(m), 1/sr)
    return 10*np.log10(np.mean(sp[(fr>=lo)&(fr<hi)]**2) + 1e-30)

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--trials", type=int, default=160)
    ap.add_argument("--workers", type=int, default=6); a = ap.parse_args()
    anb = str(ANCH / "vvv-bright-hall_noiseburst.wav")
    a_overall = tail_kurt(anb, 2000, 14000)
    a_sub = {b: tail_kurt(anb, *b) for b in SUB}
    a_hf = hf_level(anb)
    print(f"anchor: overall kurt={a_overall:.1f}  sub={ {f'{l//1000}-{h//1000}k':round(a_sub[(l,h)],1) for l,h in SUB} }")

    def obj(trial):
        amt = trial.suggest_float("amount", 0.3, 1.0)
        lfo = trial.suggest_float("lfoScale", 0.0, 1.0)
        dsc = trial.suggest_float("delayScale", 0.5, 4.0)
        d = f"/tmp/odk_{trial.number}"; shutil.rmtree(d, ignore_errors=True); os.makedirs(d)
        env = dict(os.environ, DUSKVERB_OUTDIFF=f"{amt},{lfo},{dsc}")
        r = subprocess.run([str(REND),"--program","Bright Hall","--output-dir",d,*WET],
                           capture_output=True, env=env)
        nb = glob.glob(f"{d}/*_noiseburst.wav")
        if r.returncode != 0 or not nb: return 1e3
        ov = tail_kurt(nb[0], 2000, 14000)
        if ov is None: return 1e3
        pen = abs(ov - a_overall)                          # overall toward anchor
        for b in SUB[1:]:                                  # 4-6/6-9/9-14k toward ~5
            s = tail_kurt(nb[0], *b)
            if s is None: return 1e3
            pen += 0.6*max(0.0, s - max(a_sub[b], 5.0))
        pen += 0.5*max(0.0, a_hf - hf_level(nb[0]) - 1.0)  # don't dull >1 dB below anchor HF
        return pen

    study = optuna.create_study(direction="minimize", sampler=optuna.samplers.TPESampler(seed=42))
    optuna.logging.set_verbosity(optuna.logging.WARNING)
    study.optimize(obj, n_trials=a.trials, n_jobs=a.workers)
    bp = study.best_params
    print(f"\nBEST  amount={bp['amount']:.3f} lfoScale={bp['lfoScale']:.3f} delayScale={bp['delayScale']:.3f}  pen={study.best_value:.2f}")
    # final scoreboard
    d = "/tmp/odk_best"; shutil.rmtree(d, ignore_errors=True); os.makedirs(d)
    env = dict(os.environ, DUSKVERB_OUTDIFF=f"{bp['amount']},{bp['lfoScale']},{bp['delayScale']}")
    proc = subprocess.run([str(REND),"--program","Bright Hall","--output-dir",d,*WET], capture_output=True, env=env)
    hits = glob.glob(f"{d}/*_noiseburst.wav")
    if proc.returncode != 0 or not hits:
        print(f"\n[final render failed rc={proc.returncode}; best params above]")
        return
    nb = hits[0]
    print(f"\n{'band':10s} {'baseline':>9s} {'diffused':>9s} {'anchor':>8s}")
    base_dir = "/tmp/pcheck_bright_hall"
    bnb = glob.glob(f"{base_dir}/*_noiseburst.wav")
    bnb = bnb[0] if bnb else None
    for lab,(lo,hi) in [("2-14k",(2000,14000)),("4-6k",(4000,6000)),("6-9k",(6000,9000)),("9-14k",(9000,14000))]:
        bk = tail_kurt(bnb, lo, hi) if bnb else float('nan')
        dk = tail_kurt(nb, lo, hi); ak = tail_kurt(anb, lo, hi)
        print(f"{lab:10s} {bk:9.1f} {dk:9.1f} {ak:8.1f}")
    print(f"\nbaked line: {{ \"Bright Hall\", {{ {bp['amount']:.3f}f, {bp['lfoScale']:.3f}f, {bp['delayScale']:.3f}f }} }},")

if __name__ == "__main__":
    main()
