#!/usr/bin/env python3
"""CMA-ES refinement of HallReverb (algo 10, P11 8-ch FDN champion).

Seeded local search around the P11 9/19 Mac baseline. Search bounds are
tight (~±15-30% of each axis) so CMA refines micro-tuning rather than
shifting topology. The exact baseline is x0 (initial mean of CMA's
distribution) AND enqueued as trial 0 so it is always sampled.

Run:
    python3 hall_optuna.py --trials 1000 --workers 8 \
        --target-file plugins/DuskVerb/tools/targets/lex_med_hall.json
"""
from __future__ import annotations

import argparse
import json
import math
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

# Frame + locked Hall internals. Spec Ms (specular tap times) and Mid EQ
# Q/Fc stay locked at DE v4 winner positions.
LOCKED = {
    'Algorithm':            'Hall (Lex)',
    'Dry/Wet':              '1.0',
    'Bus Mode':             'on',
    'Freeze':               'Off',
    'Pre-Delay':            '14',
    'Pre-Delay Sync':       'Free',
    'Lo Cut':               '60',
    'Early Ref Level':      '0.0',
    'Early Ref Size':       '0.30',
    'Width':                '1.0',
    'Saturation':           '0.0',
    # Mod Depth / Mod Rate UN-LOCKED — moved into BOUNDS so CMA can
    # break static comb-filter ringing via LFO sidebands. Per user
    # acoustic feedback: untuned Algo 12 had metallic ring + phasey
    # flutter; modulation is the cure.
    'Hall Spec 0 Ms':       '6.0984',
    'Hall Spec 1 Ms':       '6.6598',
    'Hall Spec 2 Ms':       '8.0472',
    'Hall Spec 3 Ms':       '48.8765',
    'Hall Mid EQ Q':        '4',
    'Hall Mid EQ Fc':       '380',
}

# P11 12/19 micro-squeeze seed — winner of trial 463 (CMA pass v1, seeded
# from 9/19 Mac baseline). Closes decay_envelope_db (12/12), bass_ratio,
# treble_ratio above the 9/19 baseline. Used as x0 + enqueue_trial for
# the second CMA pass that targets the remaining 2 soft-fails
# (rt60_per_band, edt_500) without breaking the 12 PASS metrics.
BASELINE = {
    # v10 — seed from v9 t546 winner (9/19, spectral_crest +4.25,
    # decay_envelope 12/12 PASS, d50 PASS). Bilateral time_crest
    # + tight gain_trim → recover a_weighted + time_crest PASS.
    'mod_depth':    0.1003,
    'mod_rate':     2.6641,
    'decay':        1.8571,
    'size':         0.7192,
    'bass_mult':    1.4558,
    'mid_mult':     1.8737,
    'treble_mult':  1.7097,
    'diffusion':    0.9789,
    'low_xov':      553.66,
    'high_xov':     4356.30,
    'hi_cut':       4713.91,
    'gain_trim':   -9.7744,
    'h_inline':     0.1003,
    'h_stereo':    -0.1811,
    'h_b_damp':     0.0440,
    'h_m_damp':     0.0586,
    'h_t_damp':     0.0151,
    'h_b_gain':     1.0737,
    'h_m_gain':     0.9213,
    'h_t_gain':     0.9003,
    'h_b_eq':      -7.2304,
    'h_m_eq':     -14.1677,
    'h_t_eq':       1.3737,
    'h_b_sh_g':   -10.2827,
    'h_b_sh_fc':   3406.39,
    'h_m_sh_g':   -15.9508,
    'h_m_sh_fc':   1567.96,
    'h_t_sh_g':    -2.5993,
    'h_t_sh_fc':   2317.76,
    'h_chan_sp':    0.0286,
    'spec_w0':      1.0079,
    'spec_w1':      0.1935,
    'spec_w2':      3.5792,
    'spec_w3':      3.7211,
    'spec_hfcut':   5775.50,
}

# Micro-squeeze bounds — half-radius around the 12/19 t463 seed. The
# rt60-relevant axes (Decay, Bass/Mid/Treble Mult, Hi Cut) and the
# specular-energy axes (Spec 0-3 Weight, Spec HF Cut) keep slightly
# larger windows to give the rt60_per_band + edt_500 soft-fails room
# to close. Everything else is tight to lock the 12 PASS metrics.
BOUNDS = {
    # Mod axes UN-LOCKED — wide bounds so CMA can break combs / phase
    # statics. Lex spec: Spin=2.9 Hz, Wander=15ms (~720 samples @48k).
    'mod_depth':   (0.05, 0.50),
    'mod_rate':    (0.5, 8.0),
    'decay':       (1.70, 2.05),
    'size':        (0.60, 0.80),
    'bass_mult':   (1.30, 1.55),
    'mid_mult':    (1.75, 2.00),
    'treble_mult': (1.45, 1.90),   # widened up — chase rt60 treble (bins 6,7 short)
    'diffusion':   (0.93, 0.98),
    'low_xov':     (510.0, 620.0),
    'high_xov':    (4150.0, 4550.0),
    'hi_cut':      (4500.0, 5200.0),  # widened up — chase rt60 16k
    'gain_trim':  (-10.10, -9.50),   # v10 — tight around v9 winner -9.77 to lock a_weighted PASS
    'h_inline':    (0.06, 0.13),
    'h_stereo':   (-0.27, -0.15),
    'h_b_damp':    (0.00, 0.12),
    'h_m_damp':    (0.00, 0.12),
    'h_t_damp':    (0.00, 0.06),
    'h_b_gain':    (0.95, 1.20),
    'h_m_gain':    (0.80, 1.00),
    'h_t_gain':    (0.78, 0.95),
    'h_b_eq':     (-9.00, -5.50),
    'h_m_eq':    (-16.00, -13.50),
    'h_t_eq':      (-0.50, 3.00),
    'h_b_sh_g':   (-12.00, -8.50),
    'h_b_sh_fc':   (2900.0, 3700.0),
    'h_m_sh_g':   (-17.50, -14.00),
    'h_m_sh_fc':   (1400.0, 1700.0),
    'h_t_sh_g':    (-4.00, -1.50),
    'h_t_sh_fc':   (2000.0, 2600.0),
    'h_chan_sp':   (0.02, 0.05),
    # Specular weights/HF: widened slightly — early reflection energy
    # distribution governs both edt_500 and peak_locations residual.
    'spec_w0':     (0.75, 1.30),
    'spec_w1':     (0.05, 0.45),
    'spec_w2':     (3.20, 4.00),
    'spec_w3':     (3.50, 4.00),
    'spec_hfcut':  (5200.0, 6500.0),
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
    'c80':                     MetricWeight (weight=20.0, jnd=0.8),
    'd50':                     MetricWeight (weight=20.0, jnd=0.8),
    'peak_locations_ms':       MetricWeight (weight=10.0, jnd=1.0),
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
    outdir = Path (f'/tmp/dv_hall_opt/{pid}_{tid}')
    if outdir.exists(): shutil.rmtree (outdir)
    outdir.mkdir (parents=True)

    mod_depth  = _suggest (trial, 'mod_depth')
    mod_rate   = _suggest (trial, 'mod_rate')
    decay      = _suggest (trial, 'decay')
    size       = _suggest (trial, 'size')
    bass_mult  = _suggest (trial, 'bass_mult')
    mid_mult   = _suggest (trial, 'mid_mult')
    treble_mult= _suggest (trial, 'treble_mult')
    diffusion  = _suggest (trial, 'diffusion')
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

    params = dict (LOCKED)
    params['Mod Depth']                  = f'{mod_depth:.4f}'
    params['Mod Rate']                   = f'{mod_rate:.4f}'
    params['Decay Time']                 = f'{decay:.4f}'
    params['Size']                       = f'{size:.4f}'
    params['Bass Multiply']              = f'{bass_mult:.4f}'
    params['Mid Multiply']               = f'{mid_mult:.4f}'
    params['Treble Multiply']            = f'{treble_mult:.4f}'
    params['Diffusion']                  = f'{diffusion:.4f}'
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

    cmd = [str (RENDER), '--slug', 'hall', '--output-dir', str (outdir)]
    for k, v in params.items():
        cmd += ['--param', f'{k}={v}']
    try:
        subprocess.run (cmd, check=True, capture_output=True, timeout=60)
        dv = measure_pair (outdir / 'hall_impulse.wav', outdir / 'hall_noiseburst.wav')
        lex = _load_lex()
        loss, _, _ = compute_loss (dv, lex, WEIGHTS)
        pc, total = count_pass (dv, lex)
    except Exception:
        return 1e6

    aw_lex = lex.get ('a_weighted_rms_db', 0.0) or 0.0
    aw_dv  = dv.get ('a_weighted_rms_db', 0.0) or 0.0
    aw_excess = max (0.0, abs (aw_dv - aw_lex) - 0.5)
    vol_penalty = 5000.0 * aw_excess if aw_excess > 0 else 0.0

    # Micro-squeeze lock: if c80 or d50 drifts beyond JND (0.8 dB) the
    # whole pass-12-PASS edifice collapses. Hard penalty so CMA refuses
    # any trial that loses either, even if it gains rt60/edt_500.
    c80_dv  = dv.get  ('c80', 0.0) or 0.0
    c80_lex = lex.get ('c80', 0.0) or 0.0
    d50_dv  = dv.get  ('d50', 0.0) or 0.0
    d50_lex = lex.get ('d50', 0.0) or 0.0
    c80_excess = max (0.0, abs (c80_dv - c80_lex) - 0.8)
    d50_excess = max (0.0, abs (d50_dv - d50_lex) - 0.8)
    clarity_penalty = 50000.0 * (c80_excess + d50_excess)

    # ── Perceptual penalties (post-user acoustic listening test) ──────
    # The grader has 19 metrics but is deaf to perceptual artifacts.
    # User reported metallic ring + phasey flutter on untuned Algo 12.
    # Translation into math: spectral_crest_db OVER Lex anchor sounds
    # spiky/ringy; time_domain_crest OVER sounds clicky; stereo_correlation
    # drifting positive sounds mono/phasey. ASYMMETRIC — below Lex
    # (smoother/wider) is free; above (ringier/narrower) is 10× heavy.
    # v8 — penalties DECOUPLED from `loss`. v7 used `10 * loss * sc_over`
    # which exploded (loss ~30000 + sc_over ~6 → 1.8M penalty, dominated
    # PC bonus). v8 uses additive constant: 5000 * sc_over = 5000 per dB
    # ring excess = 0.5 PC-unit per dB. Same magnitude as vol_penalty.
    # Lets CMA trade ~0.5 PC for 1 dB ring reduction — sane balance.
    sc_lex = lex.get  ('spectral_crest_db', 0.0) or 0.0
    sc_dv  = dv.get   ('spectral_crest_db', 0.0) or 0.0
    sc_over = max (0.0, float (sc_dv) - float (sc_lex))      # only over
    # v9 — mid-weight 25000× sc_over. Sweet spot: 1 dB ring = 25000
    # penalty = 2.5 PC bonus. Forces CMA to drop ~1 PC per ~0.4 dB ring
    # reduction — economically incentivizes audible smear.
    metallic_spectral_penalty = 25000.0 * sc_over

    # v10 — BILATERAL time-crest penalty. v9 over-smoothed past Lex
    # (Δ -2.35) chasing zero ring; bilateral pulls both directions.
    # Below-target penalty 5000× tc_under / 1.5 — keeps natural
    # transient pulse without re-spawning ring.
    tc_lex = lex.get  ('time_domain_crest', 0.0) or 0.0
    tc_dv  = dv.get   ('time_domain_crest', 0.0) or 0.0
    tc_over  = max (0.0, float (tc_dv) - float (tc_lex))
    tc_under = max (0.0, float (tc_lex) - float (tc_dv))
    metallic_time_penalty = 5000.0 * max (tc_over / 1.5, tc_under / 1.5)

    # Phase penalty — exponential when stereo_correlation drifts toward
    # +1 (mono). Lex anchor at -0.071 (slight L/R anti-correlation).
    # Drift past anchor + 0.05 toward positive is audible as collapsed
    # stereo width.
    stc_lex = lex.get ('stereo_correlation', 0.0) or 0.0
    stc_dv  = dv.get  ('stereo_correlation', 0.0) or 0.0
    stc_drift_toward_mono = max (0.0, float (stc_dv) - (float (stc_lex) + 0.05))
    phase_penalty = 100000.0 * (math.exp (5.0 * stc_drift_toward_mono) - 1.0) \
                    if stc_drift_toward_mono > 0.0 else 0.0

    score = (-pc * 10000.0 + loss * 0.01 + vol_penalty + clarity_penalty
             + metallic_spectral_penalty + metallic_time_penalty + phase_penalty)
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
               f'dec={p["decay"]:.3f} bM={p["bass_mult"]:.2f} mM={p["mid_mult"]:.2f} tM={p["treble_mult"]:.2f} '
               f'trim={p["gain_trim"]:+.2f} '
               f'spec=[{p["spec_w0"]:.2f},{p["spec_w1"]:.2f},{p["spec_w2"]:.2f},{p["spec_w3"]:.2f}]@hf{p["spec_hfcut"]:.0f} '
               f'mEQ={p["h_m_eq"]:+.1f} bSh={p["h_b_sh_g"]:+.1f}@{p["h_b_sh_fc"]:.0f} '
               f'mSh={p["h_m_sh_g"]:+.1f}@{p["h_m_sh_fc"]:.0f} '
               f'{flag}', flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument ('--trials',  type=int, default=500)
    ap.add_argument ('--workers', type=int, default=8)
    ap.add_argument ('--seed',    type=int, default=43)
    ap.add_argument ('--sigma0',  type=float, default=0.025)
    ap.add_argument ('--target-file', type=Path, default=None,
                     help='Load anchor metrics from JSON (targets/lex_*.json).')
    args = ap.parse_args()

    global _TARGET_FILE
    _TARGET_FILE = args.target_file

    anchor_label = (f'JSON: {args.target_file.name}' if args.target_file
                    else 'live: lex_hall_med_hall_*.wav')
    n_axes = len (BOUNDS)
    print (f'CMA-ES — Hall (Lex) seeded refinement — trials={args.trials} workers={args.workers}')
    print (f'  Anchor: {anchor_label}')
    print (f'  Axes:   {n_axes} (narrow bounds, x0 = P11 9/19 Mac baseline, sigma0={args.sigma0})')
    print (f'  Restart strategy: ipop (increasing population on convergence)')
    print()

    sampler = CmaEsSampler (x0=dict (BASELINE), sigma0=args.sigma0,
                            seed=args.seed, restart_strategy='ipop',
                            inc_popsize=2, n_startup_trials=1,
                            warn_independent_sampling=False)
    Path ('/tmp/dv_hall_opt').mkdir (exist_ok=True)
    study = optuna.create_study (study_name='hall_lex_p11_cma_v10_polish',
                                 direction='minimize', sampler=sampler,
                                 storage='sqlite:////tmp/hall_lex_optuna_v10.db',
                                 load_if_exists=True)

    # Force baseline as trial 0 — guarantees CMA sees the 9/19 anchor
    # before exploring. enqueue_trial bypasses the sampler.
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

    out = Path ('/tmp/hall_lex_optuna_best.json')
    with open (out, 'w') as f:
        json.dump ({'pc': pc, 'score': float (best.value), 'params': best.params,
                    'trial': best.number, 'wall_s': elapsed,
                    'n_trials_total': len (study.trials)}, f, indent=2)
    print (f'  Saved: {out}')


if __name__ == '__main__':
    main()
