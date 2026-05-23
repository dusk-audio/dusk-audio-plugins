#!/usr/bin/env python3
"""CMA-ES tuner for LexiconMTDLEngine (Engine 16) — v33 GENESIS.

Brand-new metric-driven topology after the 15-of-19 LTI Pareto wall on
Engine 15 (Dattorro fig-8) was proven empirically (see memory:
duskverb_tdc_noiseburst_blocker.md).

Engine 16 already cracks the two architecturally-hard metrics
(peak_locations_ms, time_domain_crest) on first compile with raw
defaults — those are solved by topology, not parameters. CMA's job is
to dial the remaining 14 metrics by balancing:

  - feedback (FDN per-line gain margin → rt60, late_tail)
  - damping_hz (in-loop one-pole LP cutoff → centroid_drift, treble_ratio)
  - er_level / late_level (mix balance → c80, d50, a_weighted_rms)
  - Decay Time + Gain Trim + Hi Cut (shared globals)

ER tap delays are LOCKED at the measured Lex Med Hall anchor positions
(0/4/7.52/9.79 ms) inside LexiconMTDLEngine.h and NOT exposed here.
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

# v33 GENESIS — wide-open search since topology is new. Seed values come
# from the hand-sanity-tuned defaults that produced 6/19 first-render
# (LexiconMTDLEngine.h coded defaults).
BASELINE = {
    # v36 — Lexicon Complete. Seed strictly from v35 (12/19) winner.
    'decay':            8.6348,
    'gain_trim':        -1.3375,
    'hi_cut':           19899.65,
    'mtdl_feedback':    0.9446,
    'mtdl_er_level':    1.1784,
    'mtdl_late_level':  0.4248,
    # v35 per-line damping (seed from winner)
    'mtdl_damping_hz_0': 6704.53,
    'mtdl_damping_hz_1': 14832.32,
    'mtdl_damping_hz_2': 13219.70,
    'mtdl_damping_hz_3': 10489.03,
    'mtdl_damping_hz_4': 11309.45,
    'mtdl_damping_hz_5': 6283.26,
    'mtdl_damping_hz_6': 4020.63,
    'mtdl_damping_hz_7': 3233.70,
    # v36 per-line feedback override (default 1.0 = no change)
    'mtdl_fb_0': 1.0, 'mtdl_fb_1': 1.0, 'mtdl_fb_2': 1.0, 'mtdl_fb_3': 1.0,
    'mtdl_fb_4': 1.0, 'mtdl_fb_5': 1.0, 'mtdl_fb_6': 1.0, 'mtdl_fb_7': 1.0,
    # v36 per-tap ER gain dB (current hardcoded -15/-18/-21/-24)
    'mtdl_er_tap_gain_db_0': -15.0,
    'mtdl_er_tap_gain_db_1': -18.0,
    'mtdl_er_tap_gain_db_2': -21.0,
    'mtdl_er_tap_gain_db_3': -24.0,
    # v37 — Schroeder pre-diffuser + tilt EQ. Seed at 0 (bypass) so the
    # seed reproduces v35 12/19 ceiling exactly.
    'mtdl_schroeder_coeff': 0.0,
    'mtdl_tilt_db':         0.0,
}

# Wide bounds — first-generation search, let CMA find global minimum.
BOUNDS = {
    # v34/v35 — decay widened to overshoot the in-loop LP energy drain.
    'decay':            (0.5, 10.0),
    'gain_trim':        (-12.0, 18.0),
    'hi_cut':           (4000.0, 20000.0),
    'mtdl_feedback':    (0.50, 0.999),
    'mtdl_er_level':    (0.0, 2.0),
    'mtdl_late_level':  (0.0, 2.0),
    'mtdl_damping_hz_0': (2000.0, 18000.0),
    'mtdl_damping_hz_1': (2000.0, 18000.0),
    'mtdl_damping_hz_2': (2000.0, 18000.0),
    'mtdl_damping_hz_3': (2000.0, 18000.0),
    'mtdl_damping_hz_4': (2000.0, 18000.0),
    'mtdl_damping_hz_5': (2000.0, 18000.0),
    'mtdl_damping_hz_6': (2000.0, 18000.0),
    'mtdl_damping_hz_7': (2000.0, 18000.0),
    # v36 — per-line feedback multiplier (1.0 = unchanged, < 1 dampens,
    # > 1 boosts). Decouples decay from damping for fine sculpting.
    'mtdl_fb_0': (0.5, 1.5), 'mtdl_fb_1': (0.5, 1.5), 'mtdl_fb_2': (0.5, 1.5), 'mtdl_fb_3': (0.5, 1.5),
    'mtdl_fb_4': (0.5, 1.5), 'mtdl_fb_5': (0.5, 1.5), 'mtdl_fb_6': (0.5, 1.5), 'mtdl_fb_7': (0.5, 1.5),
    # v36 — per-tap ER gain dB. Bounds -36..-3 dB (locked above 0 dB so
    # ER stays at moderate level vs late tank).
    'mtdl_er_tap_gain_db_0': (-36.0, -3.0),
    'mtdl_er_tap_gain_db_1': (-36.0, -3.0),
    'mtdl_er_tap_gain_db_2': (-36.0, -3.0),
    'mtdl_er_tap_gain_db_3': (-36.0, -3.0),
    # v37 — Schroeder pre-diffuser coeff + tilt EQ dB.
    'mtdl_schroeder_coeff': (0.0, 0.85),
    'mtdl_tilt_db':         (-6.0, 6.0),
}

LOCKED = {
    'Algorithm':            'Hall (Lex MTDL)',
    'Dry/Wet':              '1.0',
    'Bus Mode':             'on',
    'Freeze':               'Off',
    'Gate':                 'On',
    'Pre-Delay':            '14',
    'Pre-Delay Sync':       'Free',
    'Size':                 '1.0',
    'Lo Cut':               '60',
    'Low Crossover':        '20',
    'Width':                '1.0',
    'Mono Below':           '20',
    'Diffusion':            '0.0',
    'Saturation':           '0.0',
    'Mod Depth':            '0.0',
    'Mod Rate':             '1.0',
    'Early Ref Level':      '0.0',
    'Early Ref Size':       '0.30',
    'First Refl L Dly':     '3.0',
    'First Refl R Dly':     '8.0',
    'First Refl L Gain':    '-60.0',
    'First Refl R Gain':    '-60.0',
    'First Refl HF Cut':    '20000',
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
    outdir = Path (f'/tmp/dv_lexmtdl_opt/{pid}_{tid}')
    if outdir.exists(): shutil.rmtree (outdir)
    outdir.mkdir (parents=True)

    def _sg (name):
        lo, hi = BOUNDS[name]
        return trial.suggest_float (name, lo, hi)

    decay        = _sg ('decay')
    gain_trim    = _sg ('gain_trim')
    hi_cut       = _sg ('hi_cut')
    feedback     = _sg ('mtdl_feedback')
    damping_hz   = [_sg (f'mtdl_damping_hz_{i}') for i in range (8)]
    fb_at        = [_sg (f'mtdl_fb_{i}') for i in range (8)]
    er_db        = [_sg (f'mtdl_er_tap_gain_db_{i}') for i in range (4)]
    er_level     = _sg ('mtdl_er_level')
    late_level   = _sg ('mtdl_late_level')
    schro_coeff  = _sg ('mtdl_schroeder_coeff')
    tilt_db      = _sg ('mtdl_tilt_db')

    params = dict (LOCKED)
    params['Decay Time']     = f'{decay:.4f}'
    params['Gain Trim']      = f'{gain_trim:.4f}'
    params['Hi Cut']         = f'{hi_cut:.4f}'
    params['MTDL Feedback']  = f'{feedback:.4f}'
    for i in range (8):
        params[f'MTDL Damping Hz {i}']  = f'{damping_hz[i]:.4f}'
        params[f'MTDL Feedback {i}']    = f'{fb_at[i]:.4f}'
        # LFO axes LOCKED to 0 (v36 proved them toxic)
        params[f'MTDL Line Mod ms {i}'] = '0.0000'
    for i in range (4):
        params[f'MTDL ER Tap Gain dB {i}'] = f'{er_db[i]:.4f}'
    params['MTDL ER Level']  = f'{er_level:.4f}'
    params['MTDL Late Level']= f'{late_level:.4f}'
    params['MTDL Schroeder'] = f'{schro_coeff:.4f}'
    params['MTDL Tilt dB']   = f'{tilt_db:.4f}'

    cmd = [str (RENDER), '--slug', 'mtdl', '--output-dir', str (outdir)]
    for k, v in params.items():
        cmd += ['--param', f'{k}={v}']
    try:
        subprocess.run (cmd, check=True, capture_output=True, timeout=60)
        dv  = measure_pair (outdir / 'mtdl_impulse.wav', outdir / 'mtdl_noiseburst.wav')
        lex = _load_lex()
        loss, _, _ = compute_loss (dv, lex, WEIGHTS)
        pc, total = count_pass (dv, lex)
    except Exception:
        return 1e6

    # v34 — strengthened vol + late_tail penalties so CMA cannot game
    # the a_weighted_rms drift (+6.5 dB) it found at v33. PC reward 100k
    # still dominates but penalties scale aggressively above JND.
    aw_lex = float (lex.get ('a_weighted_rms_db', 0.0) or 0.0)
    aw_dv  = float (dv .get ('a_weighted_rms_db', 0.0) or 0.0)
    vol_penalty = 20000.0 * max (0.0, abs (aw_dv - aw_lex) - 0.5)

    tdc_lex = float (lex.get ('time_domain_crest', 0.0) or 0.0)
    tdc_dv  = float (dv .get ('time_domain_crest', 0.0) or 0.0)
    # v35 — asymmetric tdc penalty. UNDERSHOOT (DV less spiky than Lex)
    # is the killer mode — that's where the v34 9/19 winner regressed
    # (tdc -2.93). 8000x per dB below Lex-1.5 forces CMA to preserve
    # envelope chatter. Overshoot uses the gentler symmetric penalty.
    tdc_undershoot = max (0.0, (tdc_lex - tdc_dv) - 1.5)
    tdc_overshoot  = max (0.0, (tdc_dv - tdc_lex) - 1.5)
    tdc_penalty    = 8000.0 * tdc_undershoot + 3000.0 * tdc_overshoot

    # v34 — late_tail penalty 2000 -> 8000 to force CMA into the decay
    # bound region that achieves Lex's broadband RT60 ~2.4s.
    lt_penalty = 0.0
    for key, jnd in [('late_tail_500ms_1s', 0.8),
                     ('late_tail_1s_2s', 1.0),
                     ('late_tail_2s_3s', 1.5)]:
        try:
            lt_lex = float (lex.get (key, 0.0) or 0.0)
            lt_dv  = float (dv .get (key, 0.0) or 0.0)
            lt_penalty += 8000.0 * max (0.0, abs (lt_dv - lt_lex) - jnd)
        except (TypeError, ValueError):
            pass

    score = -pc * 100000.0 + loss * 0.01 + vol_penalty + tdc_penalty + lt_penalty
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
        dmps = '/'.join (f'{int (p[f"mtdl_damping_hz_{i}"])}' for i in range (8))
        print (f'[t{trial.number:4d}] sc={trial.value:10.1f} pass={pc:2d}/{total} '
               f'dec={p["decay"]:.2f} trim={p["gain_trim"]:+.2f} '
               f'fb={p["mtdl_feedback"]:.3f} er={p["mtdl_er_level"]:.2f} late={p["mtdl_late_level"]:.2f} '
               f'schro={p["mtdl_schroeder_coeff"]:.3f} tilt={p["mtdl_tilt_db"]:+.2f} '
               f'dmp=[{dmps}] {flag}', flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument ('--trials',  type=int, default=600)
    ap.add_argument ('--workers', type=int, default=4)
    ap.add_argument ('--seed',    type=int, default=33)
    ap.add_argument ('--sigma0',  type=float, default=0.20)
    ap.add_argument ('--target-file', type=Path, default=None)
    args = ap.parse_args()

    global _TARGET_FILE
    _TARGET_FILE = args.target_file

    anchor_label = (f'JSON: {args.target_file.name}' if args.target_file
                    else 'no target')
    print (f'CMA-ES — Hall (Lex MTDL) Engine 16 v37 CLASSIC MTDL — trials={args.trials} workers={args.workers}')
    print (f'  Anchor: {anchor_label}')
    print (f'  v37 strips LFO (v36 toxicity). Adds Schroeder pre-diffuser + tilt EQ.')
    print (f'  28 axes: 14 v35 baseline + 8 fb override + 4 ER gain dB + Schroeder + Tilt.')
    print (f'  Penalties: vol 20kx, late_tail 8kx, tdc_undershoot 8kx, tdc_overshoot 3kx.')
    print (f'  Seeded from v35 winner (12/19); schroeder=0 + tilt=0 = v35 exact reproduce.')
    print ()

    x0 = {k: BASELINE[k] for k in BOUNDS.keys()}
    sampler = CmaEsSampler (x0=x0, sigma0=args.sigma0,
                            seed=args.seed, n_startup_trials=1,
                            warn_independent_sampling=False)
    Path ('/tmp/dv_lexmtdl_opt').mkdir (exist_ok=True)
    study = optuna.create_study (study_name='hall_lexmtdl_cma_v37_classic',
                                 direction='minimize', sampler=sampler,
                                 storage='sqlite:////tmp/hall_lexmtdl_optuna_v37.db',
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

    out = Path ('/tmp/hall_lexmtdl_optuna_v37_best.json')
    with open (out, 'w') as f:
        json.dump ({'pc': pc, 'score': float (best.value), 'params': best.params,
                    'trial': best.number, 'wall_s': elapsed,
                    'n_trials_total': len (study.trials)}, f, indent=2)
    print (f'  Saved: {out}')


if __name__ == '__main__':
    main()
