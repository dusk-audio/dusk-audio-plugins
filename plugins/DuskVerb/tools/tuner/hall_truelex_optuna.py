#!/usr/bin/env python3
"""CMA-ES optimizer for HallTrueLexReverb (algo 13, Engine 13).

Engine 13 topology: ER TDL (hardcoded Lex anchor [0/4/7.52/9.79] ms) +
Engine 10 8-ch FDN tank + 2-stage post-mix Schroeder AP. Independent
er_level + tank_level (Fix B). Nothing locked beyond the structural
constants — EQ frequencies, crossovers, gains, damping, shelves, taps,
specular weights all live in the search space alongside the Engine 13
axes (er_w0-3, er_level, tank_level, ap_coeff).

Anchor source priority:
    1. --target-file <path>   load 19 metrics from JSON (Mac-safe)
    2. fallback                live-render anchor wavs via measure_pair

Run:
    python3 hall_truelex_optuna.py --trials 1000 --workers 8 \
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
from optuna.samplers import CmaEsSampler

REPO_ROOT = _HERE.parent.parent.parent.parent
RENDER  = REPO_ROOT / 'build' / 'tests' / 'duskverb_render' / 'duskverb_render'
LEX_IMP = Path ('/home/marc/projects/dusk-audio-tools/anchors/lex/wavs/hall_med_hall/lex_hall_med_hall_impulse.wav')
LEX_NB  = Path ('/home/marc/projects/dusk-audio-tools/anchors/lex/wavs/hall_med_hall/lex_hall_med_hall_noiseburst.wav')

# Engine 13 frame — structural locks + every frame axis hall_iter.py's
# LOCKED dict declares, so the renderer state is bit-identical between
# Optuna and the validation grader. APVTS defaults for these (when
# unset) differ from hall_iter's explicit values, which previously
# caused first-reflection energy + gate state to drift between scoring
# environments (Optuna's 10/19 trial vs hall_iter's 6/19 re-grade).
LOCKED = {
    'Algorithm':            'Hall (TrueLex)',
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
    'Saturation':           '0.0',
    'Early Ref Level':      '0.0',     # engine-wide ER off (Engine 13 has own)
    'Early Ref Size':       '0.30',
    'Diffusion':            '0.0',     # engine-wide diffuser off
    'Mod Depth':            '0.15',
    'Mod Rate':             '2.9',
    # Pre-tank FirstReflections silenced at -60 dB so they don't smear
    # Engine 13's ER TDL anchor peaks. Matches hall_iter.py LOCKED.
    'First Refl L Dly':     '3.0',
    'First Refl R Dly':     '8.0',
    'First Refl L Gain':    '-60.0',
    'First Refl R Gain':    '-60.0',
    'First Refl HF Cut':    '20000',
    # Spec Ms locked at DE v4 winner positions; weights/HFCut searched.
    'Hall Spec 0 Ms':       '6.0984',
    'Hall Spec 1 Ms':       '6.6598',
    'Hall Spec 2 Ms':       '8.0472',
    'Hall Spec 3 Ms':       '48.8765',
    'Hall Mid EQ Q':        '4',
    'Hall Mid EQ Fc':       '380',
}

# x0 seed — Engine 10 trial 463 (CMA pass v1) 12/19 winner for the tank
# knobs, plus Engine-13-specific unity defaults per user spec. CMA-ES
# is forbidden from throwing away the tank physics: sigma0=0.03 keeps
# the search inside the 12/19 basin, and the new ER + AP axes are
# expected to do the heavy lifting on the remaining 7 OUT metrics.
BASELINE = {
    # Tank knobs — Engine 10 trial 463 winner (12/19 PASS)
    'decay':        1.8583,
    'bass_mult':    1.4294,
    'mid_mult':     1.8821,
    'treble_mult':  1.6653,
    'low_xov':      558.18,
    'high_xov':     4309.89,
    'hi_cut':       4739.14,
    'gain_trim':   -9.7936,
    'h_inline':     0.0979,
    'h_stereo':    -0.2044,
    'h_b_damp':     0.0494,
    'h_m_damp':     0.0549,
    'h_t_damp':     0.0082,
    'h_b_gain':     1.0849,
    'h_m_gain':     0.9087,
    'h_t_gain':     0.8634,
    'h_b_eq':      -7.2545,
    'h_m_eq':     -14.6842,
    'h_t_eq':       1.2952,
    'h_b_sh_g':   -10.2576,
    'h_b_sh_fc':   3313.66,
    'h_m_sh_g':   -15.8793,
    'h_m_sh_fc':   1559.61,
    'h_t_sh_g':    -2.7923,
    'h_t_sh_fc':   2283.73,
    'h_chan_sp':    0.0310,
    'spec_w0':      1.0176,
    'spec_w1':      0.1970,
    'spec_w2':      3.5684,
    'spec_w3':      3.8049,
    'spec_hfcut':   5701.00,
    # Engine 13 axes — unity defaults per user spec.
    # ER weights match the Hybrid (Engine 12) Lex-target amplitude
    # taper: tap 0 direct (1.0), then 0.65 / 0.45 / 0.30 decay.
    'tl_er_level':   0.20,    # mid of v4 bound (0.05, 0.35)
    'tl_tank_level': 1.0,
    'tl_er_w0':      1.00,
    'tl_er_w1':      0.65,
    'tl_er_w2':      0.45,
    'tl_er_w3':      0.30,
    'tl_ap_coeff':   0.10,    # mid of v4 bound (0.0, 0.25)
}

# Wide bounds — Engine 13 is brand new, no prior CMA evidence. Let
# CMA-ES explore broadly. Tank axes mirror hall_optuna's micro-squeeze
# bounds but ~2x wider; Engine 13 axes get full physical ranges.
BOUNDS = {
    # Tank axes
    'decay':       (1.20, 2.40),
    'bass_mult':   (1.10, 2.00),
    'mid_mult':    (1.40, 2.20),
    'treble_mult': (1.10, 2.10),
    'low_xov':     (400.0, 800.0),
    'high_xov':    (3500.0, 5500.0),
    'hi_cut':      (4000.0, 7000.0),
    'gain_trim':  (-14.0, 14.0),
    'h_inline':    (0.04, 0.30),
    'h_stereo':   (-0.30, 0.20),
    'h_b_damp':    (0.00, 0.30),
    'h_m_damp':    (0.00, 0.30),
    'h_t_damp':    (0.00, 0.20),
    'h_b_gain':    (0.70, 1.40),
    'h_m_gain':    (0.70, 1.30),
    'h_t_gain':    (0.70, 1.30),
    'h_b_eq':    (-12.00, 8.00),
    'h_m_eq':    (-18.00, -8.00),
    'h_t_eq':     (-3.00, 8.00),
    'h_b_sh_g':  (-16.00, 0.00),
    'h_b_sh_fc':   (2000.0, 5500.0),
    'h_m_sh_g':  (-20.00, -8.00),
    'h_m_sh_fc':   (1100.0, 2400.0),
    'h_t_sh_g':   (-6.00, 4.00),
    'h_t_sh_fc':   (1800.0, 4000.0),
    'h_chan_sp':   (0.00, 0.10),
    'spec_w0':     (0.20, 2.00),
    'spec_w1':     (0.00, 1.00),
    'spec_w2':     (2.00, 4.00),
    'spec_w3':     (2.00, 4.00),
    'spec_hfcut':  (3500.0, 9000.0),
    # Engine 13 axes — TIGHT bounds per user "hollow tank" spec (v3).
    # ER + AP act as corrective polish on top of a tank that's been
    # actively pushed off-Lex (c80/d50 under by 3.5 dB via target offset
    # trick). ER weights can stay wide so the four hardcoded ER peaks
    # can rise above tank's intrinsic peaks for peak_locations PASS,
    # but ER LEVEL is capped low so ER doesn't dominate macro energy.
    'tl_er_level':   (0.05, 0.35),    # v4 — slightly looser so ER prints over FDN build-up
    'tl_tank_level': (0.80, 1.20),    # tight — tank does the macro work
    'tl_er_w0':      (0.50, 4.00),    # tap 0 = direct
    'tl_er_w1':      (0.20, 4.00),    # tap 1 @ 4.0 ms
    'tl_er_w2':      (0.20, 4.00),    # tap 2 @ 7.52 ms
    'tl_er_w3':      (0.20, 4.00),    # tap 3 @ 9.79 ms
    'tl_ap_coeff':   (0.00, 0.25),    # v4 — slightly more leverage on spectral_crest
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
    'decay_envelope_db':       MetricWeight (weight=10.0, jnd=3.0),
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


def _suggest (trial, name):
    lo, hi = BOUNDS[name]
    return trial.suggest_float (name, lo, hi)


def objective (trial):
    pid = os.getpid(); tid = trial.number
    outdir = Path (f'/tmp/dv_truelex_opt/{pid}_{tid}')
    if outdir.exists(): shutil.rmtree (outdir)
    outdir.mkdir (parents=True)

    # Tank axes
    decay      = _suggest (trial, 'decay')
    bass_mult  = _suggest (trial, 'bass_mult')
    mid_mult   = _suggest (trial, 'mid_mult')
    treble_mult= _suggest (trial, 'treble_mult')
    low_xov    = _suggest (trial, 'low_xov')
    high_xov   = _suggest (trial, 'high_xov')
    hi_cut     = _suggest (trial, 'hi_cut')
    gain_trim  = _suggest (trial, 'gain_trim')
    h_inline   = _suggest (trial, 'h_inline')
    h_stereo   = _suggest (trial, 'h_stereo')
    h_b_damp   = _suggest (trial, 'h_b_damp')
    h_m_damp   = _suggest (trial, 'h_m_damp')
    h_t_damp   = _suggest (trial, 'h_t_damp')
    h_b_gain   = _suggest (trial, 'h_b_gain')
    h_m_gain   = _suggest (trial, 'h_m_gain')
    h_t_gain   = _suggest (trial, 'h_t_gain')
    h_b_eq     = _suggest (trial, 'h_b_eq')
    h_m_eq     = _suggest (trial, 'h_m_eq')
    h_t_eq     = _suggest (trial, 'h_t_eq')
    h_b_sh_g   = _suggest (trial, 'h_b_sh_g')
    h_b_sh_fc  = _suggest (trial, 'h_b_sh_fc')
    h_m_sh_g   = _suggest (trial, 'h_m_sh_g')
    h_m_sh_fc  = _suggest (trial, 'h_m_sh_fc')
    h_t_sh_g   = _suggest (trial, 'h_t_sh_g')
    h_t_sh_fc  = _suggest (trial, 'h_t_sh_fc')
    h_chan_sp  = _suggest (trial, 'h_chan_sp')
    spec_w0    = _suggest (trial, 'spec_w0')
    spec_w1    = _suggest (trial, 'spec_w1')
    spec_w2    = _suggest (trial, 'spec_w2')
    spec_w3    = _suggest (trial, 'spec_w3')
    spec_hfcut = _suggest (trial, 'spec_hfcut')
    # Engine 13 axes
    tl_er_level   = _suggest (trial, 'tl_er_level')
    tl_tank_level = _suggest (trial, 'tl_tank_level')
    tl_er_w0      = _suggest (trial, 'tl_er_w0')
    tl_er_w1      = _suggest (trial, 'tl_er_w1')
    tl_er_w2      = _suggest (trial, 'tl_er_w2')
    tl_er_w3      = _suggest (trial, 'tl_er_w3')
    tl_ap_coeff   = _suggest (trial, 'tl_ap_coeff')

    params = dict (LOCKED)
    # Tank
    params['Decay Time']                 = f'{decay:.4f}'
    params['Bass Multiply']              = f'{bass_mult:.4f}'
    params['Mid Multiply']               = f'{mid_mult:.4f}'
    params['Treble Multiply']            = f'{treble_mult:.4f}'
    params['Low Crossover']              = f'{low_xov:.4f}'
    params['High Crossover']             = f'{high_xov:.4f}'
    params['Hi Cut']                     = f'{hi_cut:.4f}'
    params['Gain Trim']                  = f'{gain_trim:.4f}'
    params['Hall Inline Diffusion']      = f'{h_inline:.4f}'
    params['Hall Stereo Width']          = f'{h_stereo:.4f}'
    params['Hall Bass Damping']          = f'{h_b_damp:.4f}'
    params['Hall Mid Damping']           = f'{h_m_damp:.4f}'
    params['Hall Treble Damping']        = f'{h_t_damp:.4f}'
    params['Hall Bass Gain']             = f'{h_b_gain:.4f}'
    params['Hall Mid Gain']              = f'{h_m_gain:.4f}'
    params['Hall Treble Gain']           = f'{h_t_gain:.4f}'
    params['Hall Bass EQ Gain']          = f'{h_b_eq:.4f}'
    params['Hall Mid EQ Gain']           = f'{h_m_eq:.4f}'
    params['Hall Treble EQ Gain']        = f'{h_t_eq:.4f}'
    params['Hall Bass Shelf Gain']       = f'{h_b_sh_g:.4f}'
    params['Hall Bass Shelf Fc']         = f'{h_b_sh_fc:.4f}'
    params['Hall Mid Shelf Gain']        = f'{h_m_sh_g:.4f}'
    params['Hall Mid Shelf Fc']          = f'{h_m_sh_fc:.4f}'
    params['Hall Treble Shelf Gain']     = f'{h_t_sh_g:.4f}'
    params['Hall Treble Shelf Fc']       = f'{h_t_sh_fc:.4f}'
    params['Hall Mid Chan Gain Spread']  = f'{h_chan_sp:.4f}'
    params['Hall Spec 0 Weight']         = f'{spec_w0:.4f}'
    params['Hall Spec 1 Weight']         = f'{spec_w1:.4f}'
    params['Hall Spec 2 Weight']         = f'{spec_w2:.4f}'
    params['Hall Spec 3 Weight']         = f'{spec_w3:.4f}'
    params['Hall Spec HF Cut']           = f'{spec_hfcut:.4f}'
    # Engine 13
    params['TrueLex ER Level']           = f'{tl_er_level:.4f}'
    params['TrueLex Tank Level']         = f'{tl_tank_level:.4f}'
    params['TrueLex ER W0']              = f'{tl_er_w0:.4f}'
    params['TrueLex ER W1']              = f'{tl_er_w1:.4f}'
    params['TrueLex ER W2']              = f'{tl_er_w2:.4f}'
    params['TrueLex ER W3']              = f'{tl_er_w3:.4f}'
    params['TrueLex AP Coeff']           = f'{tl_ap_coeff:.4f}'

    cmd = [str (RENDER), '--slug', 'tl', '--output-dir', str (outdir)]
    for k, v in params.items():
        cmd += ['--param', f'{k}={v}']
    try:
        subprocess.run (cmd, check=True, capture_output=True, timeout=60)
        dv = measure_pair (outdir / 'tl_impulse.wav', outdir / 'tl_noiseburst.wav')
        lex_real = _load_lex()
    except Exception:
        return 1e6

    # ── Hollow-tank target-offset trick ──────────────────────────────
    # The 12/19 seed has tank c80/d50 ≈ Lex. Adding ER taps then pushes
    # c80/d50 above Lex by the ER early-energy contribution. To find the
    # tank cluster where (tank + ER) lands exactly at Lex c80, the CMA
    # must search for tank configurations that UNDERSHOOT Lex c80/d50
    # by ~3.5 dB — leaving room for the hardcoded ER taps to fill in.
    # Apply this offset only to the COPY used for scoring. Validation
    # (hall_iter.py) scores against the real Lex anchor — so a winner
    # here is only meaningful if (tank+ER) sum lands on real Lex.
    lex = dict (lex_real)
    # v4 — DSP specular bypass is live; tank c80 dropped ~2 dB inherently.
    # ER-fillable headroom is now ~1.8 dB instead of ~3.5. Smaller offset
    # = optimizer's hollow-tank basin sits closer to Engine 10's 12/19
    # winner, so narrow-CMA exploration around the seed is sufficient.
    if isinstance (lex.get ('c80'), (int, float)):
        lex['c80'] = float (lex['c80']) - 1.8
    if isinstance (lex.get ('d50'), (int, float)):
        lex['d50'] = float (lex['d50']) - 1.8

    loss, _, _ = compute_loss (dv, lex, WEIGHTS)
    pc, total = count_pass (dv, lex)

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
        print (f'[t{trial.number:4d}] score={trial.value:9.2f} pass={pc:2d}/{total}  '
               f'erL={p["tl_er_level"]:.2f} tkL={p["tl_tank_level"]:.2f} '
               f'erW=[{p["tl_er_w0"]:.2f},{p["tl_er_w1"]:.2f},{p["tl_er_w2"]:.2f},{p["tl_er_w3"]:.2f}] '
               f'apG={p["tl_ap_coeff"]:+.2f} '
               f'dec={p["decay"]:.2f} trim={p["gain_trim"]:+.1f} '
               f'mEQ={p["h_m_eq"]:+.1f} bSh={p["h_b_sh_g"]:+.1f} mSh={p["h_m_sh_g"]:+.1f} '
               f'{flag}', flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument ('--trials',  type=int, default=600)
    ap.add_argument ('--workers', type=int, default=4)   # AU race mitigation
    ap.add_argument ('--seed',    type=int, default=44)
    ap.add_argument ('--sigma0',  type=float, default=0.12)  # hollow-tank escape velocity
    ap.add_argument ('--target-file', type=Path, default=None,
                     help='Load anchor metrics from JSON (targets/lex_*.json).')
    args = ap.parse_args()

    global _TARGET_FILE
    _TARGET_FILE = args.target_file

    anchor_label = (f'JSON: {args.target_file.name}' if args.target_file
                    else 'live: lex_hall_med_hall_*.wav')
    n_axes = len (BOUNDS)
    print (f'CMA-ES — Hall (TrueLex) Engine 13 — trials={args.trials} workers={args.workers}')
    print (f'  Anchor: {anchor_label}')
    print (f'  Axes:   {n_axes} (31 tank + 7 Engine 13; sigma0={args.sigma0})')
    print (f'  x0 seed: 12/19 winner tank knobs + Engine 13 unity defaults')
    print()

    sampler = CmaEsSampler (x0=dict (BASELINE), sigma0=args.sigma0,
                            seed=args.seed, n_startup_trials=1,
                            warn_independent_sampling=False)
    Path ('/tmp/dv_truelex_opt').mkdir (exist_ok=True)
    study = optuna.create_study (study_name='hall_truelex_cma_v4_specmute',
                                 direction='minimize', sampler=sampler,
                                 storage='sqlite:////tmp/hall_truelex_optuna_v4.db',
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

    out = Path ('/tmp/hall_truelex_optuna_best.json')
    with open (out, 'w') as f:
        json.dump ({'pc': pc, 'score': float (best.value), 'params': best.params,
                    'trial': best.number, 'wall_s': elapsed,
                    'n_trials_total': len (study.trials)}, f, indent=2)
    print (f'  Saved: {out}')


if __name__ == '__main__':
    main()
