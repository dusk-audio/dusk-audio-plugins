#!/usr/bin/env python3
"""TPE Optuna optimizer for HallTrueLex16Reverb (algo 14, Engine 14).

Engine 14 = ER TDL + 16-ch FDNReverb tank + post-mix Schroeder AP.
Fresh start: no x0 seed from Engine 10/13. TPE samples broadly across
the 18-axis space to find the natural c80/d50 + decay equilibrium for
the 16-channel tank interacting with hardcoded ER taps.

Run:
    python3 hall_truelex16_optuna.py --trials 800 --workers 4 \
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
from optuna.samplers import TPESampler, CmaEsSampler

REPO_ROOT = _HERE.parent.parent.parent.parent
RENDER  = REPO_ROOT / 'build' / 'tests' / 'duskverb_render' / 'duskverb_render'
LEX_IMP = Path ('/home/marc/projects/dusk-audio-tools/anchors/lex/wavs/hall_med_hall/lex_hall_med_hall_impulse.wav')
LEX_NB  = Path ('/home/marc/projects/dusk-audio-tools/anchors/lex/wavs/hall_med_hall/lex_hall_med_hall_noiseburst.wav')

# Frame locks — matching hall_iter.py LOCKED so renderer state is
# bit-identical between Optuna and the validation grader.
LOCKED = {
    'Algorithm':            'Hall (TrueLex 16)',
    'Dry/Wet':              '1.0',
    'Bus Mode':             'on',
    'Freeze':               'Off',
    'Gate':                 'On',
    'Pre-Delay':            '14',
    'Pre-Delay Sync':       'Free',
    'Size':                 '1.0',
    'Lo Cut':               '60',
    'Width':                '1.0',
    'Mono Below':           '20',
    'Early Ref Level':      '0.0',
    'Early Ref Size':       '0.30',
    'First Refl L Dly':     '3.0',
    'First Refl R Dly':     '8.0',
    'First Refl L Gain':    '-60.0',
    'First Refl R Gain':    '-60.0',
    'First Refl HF Cut':    '20000',
    # v4 — Diffusion goes back to a SEARCHED axis in a microscopic
    # window (0.01, 0.08). v3 proved Diffusion=0 cracks peak_locations
    # + d50 but over-flattens box_ratio + treble_ratio. The "golden
    # ratio" sweet spot is just enough density to keep modal richness
    # WITHOUT the inline AP cascade organising into a discrete 2.42ms
    # echo. tank_diff axis re-added below; LOCKED entry removed.
}

# ── Hybrid seed (v2) ─────────────────────────────────────────────────
# Tank axes inherited from Engine 14 v1 TPE winner t514 — those values
# delivered the 3 NEW PASS metrics (box_ratio, edt_500, spectral_crest
# cracked). Preserve them. ER axes overridden to "louder + Lex-anchor-
# proportional" so generation 1 already has ER tap amplitudes sufficient
# to dominate peak_locations + lift c80 above the −9.26 dB v1 crater.
BASELINE = {
    # v6 — seeded from v5 winner t574 (c80 PASS, box_ratio PASS,
    # late_tail × 3 PASS). v6 adds tl_er_w4 — a dedicated C80-only
    # booster at ER tap 4 = 49 ms (= 63 ms post-pre-delay, INSIDE
    # C80 window but OUTSIDE D50 window). Decouples c80/d50 trade.
    'decay':        1.4344,
    'bass_mult':    1.0938,
    'mid_mult':     1.8129,
    'treble_mult':  1.0830,
    'low_xov':      778.22,
    'high_xov':     4065.12,
    'hi_cut':       3562.52,
    'gain_trim':   -10.1670,
    'sat':          0.0163,
    'mod_depth':    0.2558,
    'mod_rate':     1.9247,
    'tank_diff':    0.0282,
    'tl_er_level':   1.4584,
    'tl_tank_level': 0.9552,
    'tl_er_w0':      0.9656,
    'tl_er_w1':      0.7138,
    'tl_er_w2':      1.2289,
    'tl_er_w3':      0.1288,
    'tl_er_w4':      0.6,        # mid of v6 bound — dedicated C80 booster
    'tl_ap_coeff':  -0.3599,
}

# Narrow bounds around the t514 tank cluster (±10% radius) so CMA can't
# escape the structural-wins basin. ER/AP bounds wider for rapid ER
# energy scaling per user spec (sigma0=0.075 compromise; effective
# sigma per axis = sigma0 × bound_range, so wider bound = larger steps).
BOUNDS = {
    # v4 — "Golden ratio" pass. tank_diff back in micro-window;
    # bass/mid_mult ±10% from v3 seed to re-balance bass_ratio.
    # ER level (1.0, 3.5) per user — give CMA runway to close c80.
    'decay':       (1.10, 1.50),
    'bass_mult':   (0.985, 1.205),   # ±10% from v3 seed 1.0954
    'mid_mult':    (1.675, 2.047),   # ±10% from v3 seed 1.8607
    'treble_mult': (0.95, 1.30),
    'low_xov':     (620.0, 820.0),
    'high_xov':    (3500.0, 4300.0),
    'hi_cut':      (3300.0, 4200.0),
    'gain_trim':  (-12.00, -7.50),
    'sat':         (0.0, 0.05),
    'mod_depth':   (0.22, 0.38),
    'mod_rate':    (1.2, 2.8),
    'tank_diff':   (0.01, 0.08),     # micro-window: enough density, no 2.42ms leak
    'tl_er_level':   (1.00, 3.50),   # USER SPEC — push for c80 closure
    'tl_tank_level': (0.70, 1.30),
    'tl_er_w0':      (0.50, 4.00),
    'tl_er_w1':      (0.30, 4.00),
    'tl_er_w2':      (0.20, 4.00),
    'tl_er_w3':      (0.10, 4.00),
    # v6 — C80 fill tap at 49ms. Wide bounds so CMA can crank as
    # dedicated C80 booster without disturbing D50.
    'tl_er_w4':      (0.00, 3.00),
    'tl_ap_coeff':   (-0.40, 0.40),
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
    'centroid_drift_per_band': MetricWeight (weight=30.0, jnd=2.5),
    'c80':                     MetricWeight (weight=25.0, jnd=0.8),
    'd50':                     MetricWeight (weight=25.0, jnd=0.8),
    'peak_locations_ms':       MetricWeight (weight=25.0, jnd=1.0),
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
        _LEX = measure_pair (LEX_IMP, LEX_NB)
    return _LEX


def objective (trial):
    pid = os.getpid(); tid = trial.number
    outdir = Path (f'/tmp/dv_truelex16_opt/{pid}_{tid}')
    if outdir.exists(): shutil.rmtree (outdir)
    outdir.mkdir (parents=True)

    def _sg (name):
        lo, hi = BOUNDS[name]
        return trial.suggest_float (name, lo, hi)
    decay      = _sg ('decay')
    bass_mult  = _sg ('bass_mult')
    mid_mult   = _sg ('mid_mult')
    treble_mult= _sg ('treble_mult')
    low_xov    = _sg ('low_xov')
    high_xov   = _sg ('high_xov')
    hi_cut     = _sg ('hi_cut')
    gain_trim  = _sg ('gain_trim')
    sat        = _sg ('sat')
    mod_depth  = _sg ('mod_depth')
    mod_rate   = _sg ('mod_rate')
    tank_diff  = _sg ('tank_diff')   # v4 — micro-window (0.01, 0.08)
    tl_er_level   = _sg ('tl_er_level')
    tl_tank_level = _sg ('tl_tank_level')
    tl_er_w0      = _sg ('tl_er_w0')
    tl_er_w1      = _sg ('tl_er_w1')
    tl_er_w2      = _sg ('tl_er_w2')
    tl_er_w3      = _sg ('tl_er_w3')
    tl_er_w4      = _sg ('tl_er_w4')
    tl_ap_coeff   = _sg ('tl_ap_coeff')

    params = dict (LOCKED)
    params['Decay Time']        = f'{decay:.4f}'
    params['Bass Multiply']     = f'{bass_mult:.4f}'
    params['Mid Multiply']      = f'{mid_mult:.4f}'
    params['Treble Multiply']   = f'{treble_mult:.4f}'
    params['Low Crossover']     = f'{low_xov:.4f}'
    params['High Crossover']    = f'{high_xov:.4f}'
    params['Hi Cut']            = f'{hi_cut:.4f}'
    params['Gain Trim']         = f'{gain_trim:.4f}'
    params['Saturation']        = f'{sat:.4f}'
    params['Mod Depth']         = f'{mod_depth:.4f}'
    params['Mod Rate']          = f'{mod_rate:.4f}'
    # v4 — Diffusion searched in micro-window (0.01, 0.08).
    params['Diffusion']         = f'{tank_diff:.4f}'
    params['TrueLex16 ER Level']      = f'{tl_er_level:.4f}'
    params['TrueLex16 Tank Level']    = f'{tl_tank_level:.4f}'
    params['TrueLex16 ER W0']         = f'{tl_er_w0:.4f}'
    params['TrueLex16 ER W1']         = f'{tl_er_w1:.4f}'
    params['TrueLex16 ER W2']         = f'{tl_er_w2:.4f}'
    params['TrueLex16 ER W3']         = f'{tl_er_w3:.4f}'
    params['TrueLex16 ER W4']         = f'{tl_er_w4:.4f}'
    params['TrueLex16 AP Coeff']      = f'{tl_ap_coeff:.4f}'

    cmd = [str (RENDER), '--slug', 'tl16', '--output-dir', str (outdir)]
    for k, v in params.items():
        cmd += ['--param', f'{k}={v}']
    try:
        subprocess.run (cmd, check=True, capture_output=True, timeout=60)
        dv = measure_pair (outdir / 'tl16_impulse.wav', outdir / 'tl16_noiseburst.wav')
        lex_real = _load_lex()
    except Exception:
        return 1e6

    # v6 — restore -2.5 dB hollow-tank offset per user spec. Combined
    # with the clarity hard-lock penalty (which is against REAL Lex),
    # the optimizer is pulled toward an undershooting tank that the
    # 49ms tap-4 will then fill back up to Lex c80 without bloating
    # d50 (tap-4 lands at IR t=63ms — outside D50 window).
    lex = dict (lex_real)
    if isinstance (lex.get ('c80'), (int, float)):
        lex['c80'] = float (lex['c80']) - 2.5
    if isinstance (lex.get ('d50'), (int, float)):
        lex['d50'] = float (lex['d50']) - 2.5

    loss, _, _ = compute_loss (dv, lex, WEIGHTS)
    pc, total = count_pass (dv, lex)

    aw_lex = lex.get ('a_weighted_rms_db', 0.0) or 0.0
    aw_dv  = dv.get ('a_weighted_rms_db', 0.0) or 0.0
    aw_excess = max (0.0, abs (aw_dv - aw_lex) - 0.5)
    vol_penalty = 5000.0 * aw_excess if aw_excess > 0 else 0.0

    # v3 — edt overrun penalty. Lex edt = 1.381 s. v2 sat at +1.17,
    # well over JND. Penalize CMA when DV edt drifts past Lex edt
    # plus 1 JND so the optimizer can't keep extending decay to
    # chase decay_envelope at the cost of edt.
    edt_lex = float (lex_real.get ('edt') or 1.381)
    edt_dv  = dv.get  ('edt')
    edt_penalty = 0.0
    if isinstance (edt_dv, (int, float)) and edt_dv == edt_dv:
        edt_excess = max (0.0, float (edt_dv) - edt_lex - 0.10)
        edt_penalty = 5000.0 * edt_excess

    # v5 — clarity hard-lock. c80 and d50 are within 0.04 dB of JND
    # at the v4 seed. Massive penalty (50000×) on any excess past
    # JND so CMA refuses any trial that drifts. Compared against
    # the REAL Lex anchor (not the offset copy) so this fence aligns
    # with the validation grader.
    c80_lex_real = lex_real.get ('c80', 0.0) or 0.0
    c80_dv       = dv.get  ('c80', 0.0) or 0.0
    d50_lex_real = lex_real.get ('d50', 0.0) or 0.0
    d50_dv       = dv.get  ('d50', 0.0) or 0.0
    c80_excess = max (0.0, abs (float (c80_dv) - float (c80_lex_real)) - 0.8)
    d50_excess = max (0.0, abs (float (d50_dv) - float (d50_lex_real)) - 0.8)
    clarity_penalty = 50000.0 * (c80_excess + d50_excess)

    score = -pc * 10000.0 + loss * 0.01 + vol_penalty + edt_penalty + clarity_penalty
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
        print (f'[t{trial.number:4d}] score={trial.value:9.2f} pass={pc:2d}/{total}  '
               f'erL={p["tl_er_level"]:.2f} tkL={p["tl_tank_level"]:.2f} '
               f'erW=[{p["tl_er_w0"]:.2f},{p["tl_er_w1"]:.2f},{p["tl_er_w2"]:.2f},{p["tl_er_w3"]:.2f},{p["tl_er_w4"]:.2f}] '
               f'apG={p["tl_ap_coeff"]:+.2f} '
               f'dec={p["decay"]:.2f} bM={p["bass_mult"]:.2f} mM={p["mid_mult"]:.2f} tM={p["treble_mult"]:.2f} '
               f'trim={p["gain_trim"]:+.1f} tdiff={p["tank_diff"]:.3f} '
               f'{flag}', flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument ('--trials',  type=int, default=600)
    ap.add_argument ('--workers', type=int, default=4)
    ap.add_argument ('--seed',    type=int, default=46)
    ap.add_argument ('--sigma0',  type=float, default=0.04,
                     help='v5 — tighter sigma to keep close to v4 winner (which '
                          'already had box_ratio + late_tail × 3 PASS). Clarity '
                          'hard-lock penalty (50000×) does the c80/d50 closing.')
    ap.add_argument ('--target-file', type=Path, default=None)
    args = ap.parse_args()

    global _TARGET_FILE
    _TARGET_FILE = args.target_file

    anchor_label = (f'JSON: {args.target_file.name}' if args.target_file
                    else 'live: lex_hall_med_hall_*.wav')
    print (f'CMA-ES — Hall (TrueLex 16) v6 temporal-fill — trials={args.trials} workers={args.workers}')
    print (f'  5th ER tap @ 49ms = C80-only booster; offset -2.5 dB + 50000x clarity penalty')
    print (f'  Anchor: {anchor_label}')
    print (f'  Hybrid seed: tank from v1 t514 (3 PASS metrics) + louder ER taps')
    print (f'  Target offset: c80/d50 -= 2.5 dB (hollow-tank, ER fills)')
    print (f'  sigma0={args.sigma0}; tight tank bounds + wide ER/AP bounds')
    print()

    sampler = CmaEsSampler (x0=dict (BASELINE), sigma0=args.sigma0,
                            seed=args.seed, n_startup_trials=1,
                            warn_independent_sampling=False)
    Path ('/tmp/dv_truelex16_opt').mkdir (exist_ok=True)
    study = optuna.create_study (study_name='hall_truelex16_cma_v6_temporal',
                                 direction='minimize', sampler=sampler,
                                 storage='sqlite:////tmp/hall_truelex16_optuna_v6.db',
                                 load_if_exists=True)

    study.enqueue_trial (dict (BASELINE))

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

    out = Path ('/tmp/hall_truelex16_optuna_best.json')
    with open (out, 'w') as f:
        json.dump ({'pc': pc, 'score': float (best.value), 'params': best.params,
                    'trial': best.number, 'wall_s': elapsed,
                    'n_trials_total': len (study.trials)}, f, indent=2)
    print (f'  Saved: {out}')


if __name__ == '__main__':
    main()
