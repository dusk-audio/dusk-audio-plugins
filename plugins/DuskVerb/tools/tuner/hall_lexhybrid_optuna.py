#!/usr/bin/env python3
"""CMA-ES tuner for HybridLexicon (Engine 17) — v38 SYNTHESIS.

Engine 17 runs Engine 15 (Dattorro fig-8, owns spectral wash) and
Engine 16 (LexiconMTDL, owns temporal chatter) in parallel off the
same dry input. v15 winner = 15/19. v16 winner = 12/19. They are
complementary on the metric blocks each misses — hybrid mix should
combine them.

CMA searches ONLY the 2 hybrid mix axes. Both engines' internal
parameters are LOCKED at their respective winning seeds (5000+
trials already spent on the internal geometries).
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

_HERE = Path (__file__).resolve().parent
sys.path.insert (0, str (_HERE))
sys.path.insert (0, str (_HERE.parent))

from metrics import measure_pair, compute_loss
from config import MetricWeight
from target import load_target
import optuna
from optuna.samplers import CmaEsSampler

REPO_ROOT = _HERE.parent.parent.parent.parent
RENDER  = REPO_ROOT / 'build' / 'tests' / 'duskverb_render' / 'duskverb_render'

# v38 — only 2 axes searched. Internal params locked.
BASELINE = {
    'hybrid_wash_level':    1.0,
    'hybrid_chatter_level': 1.0,
}

BOUNDS = {
    'hybrid_wash_level':    (0.0, 2.0),
    'hybrid_chatter_level': (0.0, 2.0),
}

# LOCKED dict — Engine 15 v27 winner (15/19) + Engine 16 v35 winner (12/19) +
# misc globals.
LOCKED = {
    'Algorithm':            'Hall (Lex Hybrid)',
    'Dry/Wet':              '1.0',
    'Bus Mode':             'on',
    'Freeze':               'Off',
    'Gate':                 'On',
    'Pre-Delay':            '14',
    'Pre-Delay Sync':       'Free',
    'Size':                 '1.0',
    'Lo Cut':               '60',
    'Low Crossover':        '1018.90',   # v27
    'Width':                '1.0',
    'Mono Below':           '20',
    'Early Ref Level':      '0.0',
    'Early Ref Size':       '0.30',
    'First Refl L Dly':     '3.0',
    'First Refl R Dly':     '8.0',
    'First Refl L Gain':    '-60.0',
    'First Refl R Gain':    '-60.0',
    'First Refl HF Cut':    '20000',
    # ===== Engine 15 v27 winner (15/19) =====
    'Decay Time':           '1.4316',
    'Bass Multiply':        '1.6903',
    'Mid Multiply':         '1.2317',
    'Treble Multiply':      '0.9025',
    'High Crossover':       '4662.50',
    'Hi Cut':               '8109.78',
    'Gain Trim':            '12.6627',
    'Saturation':           '0.0077',
    'Mod Depth':            '0.2110',
    'Mod Rate':             '7.3019',
    'Diffusion':            '0.4811',
    'LexFig8 Struct HF':    '10434.52',
    'LexFig8 Density Jitter': '0.0554',
    'LexFig8 Density Rate':   '0.8657',
    'LexFig8 ER Tap0 Gain': '-27.0',
    'LexFig8 ER Tap1 Gain': '-30.0',
    'LexFig8 ER Tap2 Gain': '-33.0',
    'LexFig8 ER Tap3 Gain': '-36.0',
    'LexFig8 Air Mult':     '0.78',
    'LexFig8 Air Xover':    '7500',
    # ===== Engine 16 v35 winner (12/19) =====
    'MTDL Feedback':            '0.9446',
    'MTDL Damping Hz 0':        '6704.53',
    'MTDL Damping Hz 1':        '14832.32',
    'MTDL Damping Hz 2':        '13219.70',
    'MTDL Damping Hz 3':        '10489.03',
    'MTDL Damping Hz 4':        '11309.45',
    'MTDL Damping Hz 5':        '6283.26',
    'MTDL Damping Hz 6':        '4020.63',
    'MTDL Damping Hz 7':        '3233.70',
    'MTDL ER Level':            '1.1784',
    'MTDL Late Level':          '0.4248',
    'MTDL Schroeder':           '0.0',
    'MTDL Tilt dB':             '0.0',
}

JND = {
    'rt60_per_band': 0.10, 'edt_500': 0.10, 'edt': 0.10,
    'decay_envelope_db': 3.0, 'peak_locations_ms': 1.0,
    'c80': 0.8, 'c80_per_octave': 1.5, 'd50': 0.8,
    'bass_ratio': 0.15, 'treble_ratio': 0.15,
    'centroid_drift_per_band': 2.5, 'box_ratio_db': 1.0,
    'a_weighted_rms_db': 0.5, 'spectral_crest_db': 1.0,
    'time_domain_crest': 1.5, 'stereo_correlation': 0.08,
    'late_tail_500ms_1s': 0.8, 'late_tail_1s_2s': 1.0, 'late_tail_2s_3s': 1.5,
}

WEIGHTS = {
    'a_weighted_rms_db':       MetricWeight (weight=40.0, jnd=0.5),
    'rt60_per_band':           MetricWeight (weight=30.0, jnd=0.10),
    'centroid_drift_per_band': MetricWeight (weight=25.0, jnd=2.5),
    'c80':                     MetricWeight (weight=25.0, jnd=0.8),
    'd50':                     MetricWeight (weight=25.0, jnd=0.8),
    'peak_locations_ms':       MetricWeight (weight=20.0, jnd=1.0),
    'edt_500':                 MetricWeight (weight=15.0, jnd=0.10),
    'edt':                     MetricWeight (weight=15.0, jnd=0.10),
    'decay_envelope_db':       MetricWeight (weight=15.0, jnd=3.0),
    'c80_per_octave':          MetricWeight (weight=20.0, jnd=1.5),
    'bass_ratio':              MetricWeight (weight=15.0, jnd=0.15),
    'treble_ratio':            MetricWeight (weight=15.0, jnd=0.15),
    'box_ratio_db':            MetricWeight (weight=20.0, jnd=1.0),
    'spectral_crest_db':       MetricWeight (weight=20.0, jnd=1.0),
    'time_domain_crest':       MetricWeight (weight=15.0, jnd=1.5),
    'stereo_correlation':      MetricWeight (weight=15.0, jnd=0.08),
    'late_tail_500ms_1s':      MetricWeight (weight=15.0, jnd=0.8),
    'late_tail_1s_2s':         MetricWeight (weight=15.0, jnd=1.0),
    'late_tail_2s_3s':         MetricWeight (weight=15.0, jnd=1.5),
}


def count_pass (meas, lex):
    pc = 0; total = 0
    for k, jnd in JND.items():
        l, d = lex.get (k), meas.get (k)
        if l is None or d is None: continue
        if isinstance (l, list):
            total += 1
            if not isinstance (d, list) or len (l) != len (d): continue
            ok = True
            for a, b in zip (l, d):
                if a is None or b is None: ok = False; break
                try: fa, fb = float (a), float (b)
                except (TypeError, ValueError): ok = False; break
                if fa != fa or fb != fb: ok = False; break
                if abs (fb - fa) >= jnd: ok = False; break
        else:
            try: ok = abs (float (d) - float (l)) < jnd
            except (TypeError, ValueError): continue
            total += 1
        pc += (1 if ok else 0)
    return pc, total


_LEX = None
_TARGET_FILE: Path | None = None


def _load_lex():
    global _LEX
    if _LEX is not None: return _LEX
    if _TARGET_FILE is not None:
        _LEX = load_target (_TARGET_FILE).as_measure_pair_dict()
    else:
        _LEX = measure_pair (Path('/nonexistent_impulse.wav'), Path('/nonexistent_noiseburst.wav'))
    return _LEX


def objective (trial):
    pid = os.getpid(); tid = trial.number
    outdir = Path (f'/tmp/dv_lexhybrid_opt/{pid}_{tid}')
    if outdir.exists(): shutil.rmtree (outdir)
    outdir.mkdir (parents=True)

    def _sg (name):
        lo, hi = BOUNDS[name]
        return trial.suggest_float (name, lo, hi)

    wash    = _sg ('hybrid_wash_level')
    chatter = _sg ('hybrid_chatter_level')

    params = dict (LOCKED)
    params['Hybrid Wash']    = f'{wash:.4f}'
    params['Hybrid Chatter'] = f'{chatter:.4f}'

    cmd = [str (RENDER), '--slug', 'hyb', '--output-dir', str (outdir)]
    for k, v in params.items():
        cmd += ['--param', f'{k}={v}']
    try:
        subprocess.run (cmd, check=True, capture_output=True, timeout=60)
        dv  = measure_pair (outdir / 'hyb_impulse.wav', outdir / 'hyb_noiseburst.wav')
        lex = _load_lex()
        loss, _, _ = compute_loss (dv, lex, WEIGHTS)
        pc, total = count_pass (dv, lex)
    except Exception:
        return 1e6

    # Aggressive penalties: vol drift kills mix-axis exploitation,
    # tdc undershoot must stay below 1.5 dB JND.
    aw_lex = float (lex.get ('a_weighted_rms_db', 0.0) or 0.0)
    aw_dv  = float (dv .get ('a_weighted_rms_db', 0.0) or 0.0)
    vol_penalty = 20000.0 * max (0.0, abs (aw_dv - aw_lex) - 0.5)

    tdc_lex = float (lex.get ('time_domain_crest', 0.0) or 0.0)
    tdc_dv  = float (dv .get ('time_domain_crest', 0.0) or 0.0)
    tdc_penalty = 8000.0 * max (0.0, (tdc_lex - tdc_dv) - 1.5) \
                + 3000.0 * max (0.0, (tdc_dv - tdc_lex) - 1.5)

    score = -pc * 100000.0 + loss * 0.01 + vol_penalty + tdc_penalty
    shutil.rmtree (outdir, ignore_errors=True)
    trial.set_user_attr ('pc', pc)
    trial.set_user_attr ('total', total)
    return score


_BEST = {'pc': 0}
def cb (study, trial):
    global _BEST
    if trial.state != optuna.trial.TrialState.COMPLETE: return
    pc = trial.user_attrs.get ('pc', 0)
    total = trial.user_attrs.get ('total', 19)
    if trial.number == study.best_trial.number or pc > _BEST['pc']:
        if pc > _BEST['pc']: _BEST['pc'] = pc
        flag = '★ NEW BEST' if trial.number == study.best_trial.number else ''
        p = trial.params
        print (f'[t{trial.number:4d}] sc={trial.value:10.1f} pass={pc:2d}/{total} '
               f'wash={p["hybrid_wash_level"]:.3f} chat={p["hybrid_chatter_level"]:.3f} {flag}',
               flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument ('--trials',  type=int, default=400)
    ap.add_argument ('--workers', type=int, default=4)
    ap.add_argument ('--seed',    type=int, default=38)
    ap.add_argument ('--sigma0',  type=float, default=0.25)
    ap.add_argument ('--target-file', type=Path, default=None)
    args = ap.parse_args()

    global _TARGET_FILE
    _TARGET_FILE = args.target_file

    print (f'CMA-ES — Hall (Lex Hybrid) Engine 17 v38 SYNTHESIS — trials={args.trials} workers={args.workers}')
    print (f'  Engine 15 (Dattorro) at its v27 15/19 winner (LOCKED).')
    print (f'  Engine 16 (MTDL)     at its v35 12/19 winner (LOCKED).')
    print (f'  Only 2 mix axes searched: hybrid_wash_level, hybrid_chatter_level.')
    print ()

    x0 = {k: BASELINE[k] for k in BOUNDS.keys()}
    sampler = CmaEsSampler (x0=x0, sigma0=args.sigma0,
                            seed=args.seed, n_startup_trials=1,
                            warn_independent_sampling=False)
    Path ('/tmp/dv_lexhybrid_opt').mkdir (exist_ok=True)
    study = optuna.create_study (study_name='hall_lexhybrid_cma_v38_synthesis',
                                 direction='minimize', sampler=sampler,
                                 storage='sqlite:////tmp/hall_lexhybrid_optuna_v38.db',
                                 load_if_exists=True)
    study.enqueue_trial (dict (x0))

    t0 = time.time()
    try:
        study.optimize (objective, n_trials=args.trials, n_jobs=args.workers,
                        callbacks=[cb], show_progress_bar=False)
    except KeyboardInterrupt:
        print ('\nInterrupted.')

    elapsed = time.time() - t0
    print()
    print ('=' * 70)
    best = study.best_trial
    pc = best.user_attrs.get ('pc', 0)
    total = best.user_attrs.get ('total', 19)
    print (f'BEST: score={best.value:.2f}  pass={pc}/{total}')
    print (f'  Trial: {best.number}  Wall: {elapsed:.0f}s')
    for k, v in best.params.items():
        print (f'    {k:24s} = {v:12.4f}')

    out = Path ('/tmp/hall_lexhybrid_optuna_v38_best.json')
    with open (out, 'w') as f:
        json.dump ({'pc': pc, 'score': float (best.value), 'params': best.params,
                    'trial': best.number, 'wall_s': elapsed,
                    'n_trials_total': len (study.trials)}, f, indent=2)
    print (f'  Saved: {out}')


if __name__ == '__main__':
    main()
