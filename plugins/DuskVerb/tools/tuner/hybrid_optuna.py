#!/usr/bin/env python3
"""Optuna TPE optimizer for HybridHallReverb (algo 12, P16) — parallel
ER + Ring with independent er_level + ring_level (Fix B post-zero-sum)
+ post-mix shelves.

16-axis search solving each OUT metric with a dedicated lever:
    peak_locations_ms        ← ER tap times (hardcoded; not searched)
    c80, d50                 ← er_level / ring_level (independent — Fix B)
    c80_per_octave           ← low_shelf + high_shelf
    bass_ratio, treble_ratio ← shelves + ring damping
    centroid_drift bin3      ← ring_damping_fc (low fc kills tank HF early)
    late_tail × 3            ← P15 Ring (already PASSes)
    spectral_crest_db        ← P15 Ring (already PASSes)
    a_weighted_rms_db        ← gain_trim (hard penalty)

ANCHOR sources (priority):
    1. --target-file <path>   load 19 metrics from JSON snapshot (Mac-safe)
    2. fallback                live-render anchor wavs via measure_pair

Run:
    python3 hybrid_optuna.py --trials 300 --workers 8 \
        --target-file plugins/DuskVerb/tools/targets/lex_med_hall.json
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
from optuna.samplers import TPESampler

REPO_ROOT = _HERE.parent.parent.parent.parent
RENDER  = REPO_ROOT / 'build' / 'tests' / 'duskverb_render' / 'duskverb_render'
LEX_IMP = Path ('/home/marc/projects/dusk-audio-tools/anchors/lex/wavs/hall_med_hall/lex_hall_med_hall_impulse.wav')
LEX_NB  = Path ('/home/marc/projects/dusk-audio-tools/anchors/lex/wavs/hall_med_hall/lex_hall_med_hall_noiseburst.wav')

# Engine-neutral LOCKED for HybridHallReverb (algo 12). No P11-Hall
# leakage — Diffusion=0 + Multiplies=1 + Early Ref Level=0 prevents
# the 14 ms phantom latency that breaks ER tap measurement.
LOCKED = {
    'Algorithm':       'Hall (Hybrid)',
    'Dry/Wet':         '1.0',
    'Bus Mode':        'on',
    'Freeze':          'Off',
    'Pre-Delay':       '14',
    'Pre-Delay Sync':  'Free',
    'Size':            '1.0',
    'Bass Multiply':   '1.0',
    'Mid Multiply':    '1.0',
    'Treble Multiply': '1.0',
    'Low Crossover':   '600',
    'High Crossover':  '4500',
    'Mod Depth':       '0.15',
    'Mod Rate':        '2.9',
    'Lo Cut':          '60',
    'Hi Cut':          '4750',
    'Width':           '1.0',
    'Diffusion':       '0.0',     # engine-wide diffuser bypassed
    'Saturation':      '0.0',
    'Early Ref Level': '0.0',     # engine-wide ER bypassed (Hybrid has its own)
    'Early Ref Size':  '0.30',
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
    'centroid_drift_per_band': MetricWeight (weight=30.0, jnd=2.5),
    'c80':                     MetricWeight (weight=30.0, jnd=0.8),
    'd50':                     MetricWeight (weight=30.0, jnd=0.8),
    'rt60_per_band':           MetricWeight (weight=30.0, jnd=0.10),
    'peak_locations_ms':       MetricWeight (weight=20.0, jnd=1.0),
    'edt_500':                 MetricWeight (weight=15.0, jnd=0.10),
    'edt':                     MetricWeight (weight=15.0, jnd=0.10),
    'decay_envelope_db':       MetricWeight (weight=10.0, jnd=3.0),
    'c80_per_octave':          MetricWeight (weight=15.0, jnd=1.5),
    'bass_ratio':              MetricWeight (weight=15.0, jnd=0.15),
    'treble_ratio':            MetricWeight (weight=15.0, jnd=0.15),
    'box_ratio_db':            MetricWeight (weight=15.0, jnd=1.0),
    'spectral_crest_db':       MetricWeight (weight=15.0, jnd=1.0),
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
_BEST_PC = 0


def _load_lex():
    global _LEX
    if _LEX is not None: return _LEX
    if _TARGET_FILE is not None:
        _LEX = load_target (_TARGET_FILE).as_measure_pair_dict()
    else:
        _LEX = measure_pair (LEX_IMP, LEX_NB)
    return _LEX


def objective (trial):
    pid = os.getpid(); tid = trial.number
    outdir = Path (f'/tmp/dv_hybrid_opt/{pid}_{tid}')
    if outdir.exists(): shutil.rmtree (outdir)
    outdir.mkdir (parents=True)

    decay      = trial.suggest_float ('decay',           1.0, 8.0)
    gain_trim  = trial.suggest_float ('gain_trim',      -12.0, 24.0)
    # Fix B: INDEPENDENT er_level + ring_level (no more zero-sum macro_mix).
    er_level   = trial.suggest_float ('er_level',        0.0, 2.0)
    ring_level = trial.suggest_float ('ring_level',      0.0, 2.0)
    er_w1      = trial.suggest_float ('er_w1',           0.0, 2.0)
    er_w2      = trial.suggest_float ('er_w2',           0.0, 2.0)
    er_w3      = trial.suggest_float ('er_w3',           0.0, 2.0)
    lsg        = trial.suggest_float ('low_shelf_gain', -12.0, 12.0)
    lsf        = trial.suggest_float ('low_shelf_fc',   100.0, 1500.0, log=True)
    hsg        = trial.suggest_float ('high_shelf_gain',-12.0, 12.0)
    hsf        = trial.suggest_float ('high_shelf_fc', 1500.0, 12000.0, log=True)
    r_damp     = trial.suggest_float ('ring_damping',    0.0, 0.7)
    r_damp_fc  = trial.suggest_float ('ring_damping_fc', 800.0, 16000.0, log=True)
    r_spin     = trial.suggest_float ('ring_spin',       0.5, 12.0, log=True)
    r_wander   = trial.suggest_float ('ring_wander',     0.0, 32.0)
    r_stereo   = trial.suggest_float ('ring_stereo',    -1.0, 1.0)

    params = dict (LOCKED)
    params['Decay Time']             = f'{decay:.4f}'
    params['Gain Trim']              = f'{gain_trim:.4f}'
    params['Hybrid ER Level']        = f'{er_level:.4f}'
    params['Hybrid Ring Level']      = f'{ring_level:.4f}'
    params['Hybrid ER W1']           = f'{er_w1:.4f}'
    params['Hybrid ER W2']           = f'{er_w2:.4f}'
    params['Hybrid ER W3']           = f'{er_w3:.4f}'
    params['Hybrid Low Shelf Gain']  = f'{lsg:.4f}'
    params['Hybrid Low Shelf Fc']    = f'{lsf:.4f}'
    params['Hybrid High Shelf Gain'] = f'{hsg:.4f}'
    params['Hybrid High Shelf Fc']   = f'{hsf:.4f}'
    params['Hybrid Ring Damping']    = f'{r_damp:.4f}'
    params['Hybrid Ring Damping Fc'] = f'{r_damp_fc:.4f}'
    params['Hybrid Ring Spin']       = f'{r_spin:.4f}'
    params['Hybrid Ring Wander']     = f'{r_wander:.4f}'
    params['Hybrid Ring Stereo']     = f'{r_stereo:.4f}'

    cmd = [str (RENDER), '--slug', 'hyb', '--output-dir', str (outdir)]
    for k, v in params.items():
        cmd += ['--param', f'{k}={v}']
    try:
        subprocess.run (cmd, check=True, capture_output=True, timeout=60)
        dv = measure_pair (outdir / 'hyb_impulse.wav', outdir / 'hyb_noiseburst.wav')
        lex = _load_lex()
        loss, _, _ = compute_loss (dv, lex, WEIGHTS)
        pc, total = count_pass (dv, lex)
    except Exception:
        return 1e6

    aw_lex = lex.get ('a_weighted_rms_db', 0.0) or 0.0
    aw_dv  = dv.get ('a_weighted_rms_db', 0.0) or 0.0
    aw_excess = max (0.0, abs (aw_dv - aw_lex) - 0.5)
    vol_penalty = 5000.0 * aw_excess if aw_excess > 0 else 0.0

    score = -pc * 10000.0 + loss * 0.01 + vol_penalty
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
        print (f'[t{trial.number:3d}] score={trial.value:8.2f} pass={pc:2d}/{total}  '
               f'erL={p["er_level"]:.2f} rL={p["ring_level"]:.2f} '
               f'erW=[{p["er_w1"]:.2f},{p["er_w2"]:.2f},{p["er_w3"]:.2f}] '
               f'dec={p["decay"]:.2f} trim={p["gain_trim"]:+.1f} '
               f'lsh={p["low_shelf_gain"]:+.1f}@{p["low_shelf_fc"]:.0f} '
               f'hsh={p["high_shelf_gain"]:+.1f}@{p["high_shelf_fc"]:.0f} '
               f'rdamp={p["ring_damping"]:.2f}@{p["ring_damping_fc"]:.0f} '
               f'rspin={p["ring_spin"]:.2f} rwan={p["ring_wander"]:.1f} '
               f'rsw={p["ring_stereo"]:+.2f} {flag}', flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument ('--trials',  type=int, default=400)
    ap.add_argument ('--workers', type=int, default=8)
    ap.add_argument ('--seed',    type=int, default=42)
    ap.add_argument ('--startup', type=int, default=50)
    ap.add_argument ('--target-file', type=Path, default=None,
                     help='Load anchor metrics from JSON (targets/lex_*.json) instead of '
                          'live-rendering the Lex VST2. Required on Mac.')
    args = ap.parse_args()

    global _TARGET_FILE
    _TARGET_FILE = args.target_file

    anchor_label = (f'JSON: {args.target_file.name}' if args.target_file
                    else 'live: lex_hall_med_hall_*.wav')
    print (f'Optuna TPE — Hall (Hybrid) — trials={args.trials} workers={args.workers}')
    print (f'  Anchor: {anchor_label}')
    print (f'  16 axes (2 shared + 14 hybrid; Fix B independent er/ring levels)')
    print()

    sampler = TPESampler (seed=args.seed, n_startup_trials=args.startup,
                          n_ei_candidates=24, multivariate=True)
    Path ('/tmp/dv_hybrid_opt').mkdir (exist_ok=True)
    study = optuna.create_study (study_name='hall_hybrid_p16r2',
                                 direction='minimize', sampler=sampler,
                                 storage='sqlite:////tmp/hall_hybrid_optuna.db',
                                 load_if_exists=True)

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

    out = Path ('/tmp/hall_hybrid_optuna_best.json')
    with open (out, 'w') as f:
        json.dump ({'pc': pc, 'score': float (best.value), 'params': best.params,
                    'trial': best.number, 'wall_s': elapsed,
                    'n_trials_total': len (study.trials)}, f, indent=2)
    print (f'  Saved: {out}')


if __name__ == '__main__':
    main()
