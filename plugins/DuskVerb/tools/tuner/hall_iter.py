#!/usr/bin/env python3
"""Hall (Lex) manual tuning helper — strict per-metric PASS/FAIL grader.

LOCKED = P11 architectural baseline (DE v4 no-crutch winner, 10/19) +
post-DE manual nudges (Bass Mult 1.55, Trim -9.59). Manual single-axis
iteration from here for further calibration sweeps.

VOLUME-FIRST RULE: after every parameter change, re-trim Gain Trim to
bring a_weighted_rms_db within JND of the anchor BEFORE evaluating
other metrics. Energy-coupled metrics (decay_envelope_db, late_tail_*)
are invalid until volume matches.

FIX A — engine-neutral grader: if --param "Algorithm=..." selects a
non-Hall-(Lex) algorithm, LOCKED auto-neutralizes Diffusion / band
Multiplies / engine ER (all P11-Hall-specific defaults that otherwise
leak 14 ms of phantom latency into other-engine measurements). The
neutralization is no-op on Hall (Lex) so P11 calibration is preserved.

ANCHOR sources (priority):
    1. --target-file <path>   load 19 metrics from JSON snapshot (Mac-safe)
    2. fallback                live-render LEX_IMP/LEX_NB via measure_pair

Usage:
    python3 hall_iter.py                              # P11 baseline, live anchor
    python3 hall_iter.py --param "Algorithm=Hall (Hybrid)"   # neutralized
    python3 hall_iter.py --target-file plugins/DuskVerb/tools/targets/lex_med_hall.json
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

_HERE = Path (__file__).resolve().parent
sys.path.insert (0, str (_HERE))            # tuner/
sys.path.insert (0, str (_HERE.parent))     # tools/

from metrics import measure_pair
from target import load_target

# ─── Repo paths ──────────────────────────────────────────────────────
REPO_ROOT = _HERE.parent.parent.parent.parent
RENDER  = REPO_ROOT / 'build' / 'tests' / 'duskverb_render' / 'duskverb_render'
LEX_IMP = Path ('/home/marc/projects/dusk-audio-tools/anchors/lex/wavs/hall_med_hall/lex_hall_med_hall_impulse.wav')
LEX_NB  = Path ('/home/marc/projects/dusk-audio-tools/anchors/lex/wavs/hall_med_hall/lex_hall_med_hall_noiseburst.wav')
OUTDIR  = Path ('/tmp/dv_hall_iter')

LOCKED = {
    # ── Frame ──────────────────────────────────────────────────
    'Algorithm':            'Hall (Lex)',
    'Dry/Wet':              '1.0',
    'Bus Mode':             'on',
    'Freeze':               'Off',
    'Pre-Delay':            '14',
    'Pre-Delay Sync':       'Free',

    # ── P11 architectural baseline (10/19) — DE v4 winner + nudges ─
    'Decay Time':           '1.6859',
    'Size':                 '0.7',
    'Bass Multiply':        '1.55',
    'Mid Multiply':         '1.7567',
    'Treble Multiply':      '1.6',
    'Low Crossover':        '600',
    'High Crossover':       '4500',
    'Mod Depth':            '0.15',
    'Mod Rate':             '2.9',
    'Lo Cut':               '60',
    'Hi Cut':               '4750',
    'Width':                '1.0',
    'Diffusion':            '0.97',
    'Saturation':           '0.0',
    'Gain Trim':            '-9.59',
    'Early Ref Level':      '0.0',
    'Early Ref Size':       '0.30',

    # ── P11 Hall internals — in-loop damping LOCKED 0, post-tank shelves
    'Hall Inline Diffusion':'0.10',
    'Hall Bass Damping':    '0.0',
    'Hall Mid Damping':     '0.0',
    'Hall Treble Damping':  '0.0',
    'Hall Bass Gain':       '1.0',
    'Hall Mid Gain':        '1.0',
    'Hall Treble Gain':     '1.0',
    'Hall Stereo Width':    '-0.15',
    'Hall Mid Shelf Gain':       '-16.8995',
    'Hall Mid Shelf Fc':         '1622.7323',
    'Hall Mid Chan Gain Spread': '0.0306',
    'Hall Bass Shelf Gain':      '-12.8072',
    'Hall Bass Shelf Fc':        '3079.0669',
    'Hall Treble Shelf Gain':    '-1.6880',
    'Hall Treble Shelf Fc':      '2509.3244',

    # ── CMA discoveries: EQ + Spec weights ──
    'Hall Bass EQ Gain':    '-8.69773',
    'Hall Mid EQ Gain':     '-14',
    'Hall Mid EQ Q':        '4',
    'Hall Mid EQ Fc':       '380',
    'Hall Treble EQ Gain':  '1.17623',
    'Hall Spec 0 Ms':       '6.0984',
    'Hall Spec 1 Ms':       '6.6598',
    'Hall Spec 2 Ms':       '8.0472',
    'Hall Spec 3 Ms':       '48.8765',
    'Hall Spec 0 Weight':   '0.9166',
    'Hall Spec 1 Weight':   '0.1299',
    'Hall Spec 2 Weight':   '3.8090',
    'Hall Spec 3 Weight':   '3.8090',
    'Hall Spec HF Cut':     '6276.25',
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


def parse_param_args (argv: list[str]) -> dict[str, str]:
    """Pull --param NAME=VALUE pairs out of argv. Other flags pass through."""
    overrides: dict[str, str] = {}
    i = 0
    while i < len (argv):
        if argv[i] == '--param' and i + 1 < len (argv):
            k, _, v = argv[i + 1].partition ('=')
            overrides[k.strip()] = v.strip()
            i += 2
        else:
            i += 1
    return overrides


def render (params: dict[str, str]) -> None:
    if OUTDIR.exists(): shutil.rmtree (OUTDIR)
    OUTDIR.mkdir (parents=True)
    cmd = [str (RENDER), '--slug', 'iter', '--output-dir', str (OUTDIR)]
    for k, v in params.items():
        cmd += ['--param', f'{k}={v}']
    subprocess.run (cmd, check=True, capture_output=True, timeout=60)


def fvec (v):
    if v is None: return 'None'
    out = []
    for x in v:
        if x is None: out.append ('  None')
        else:
            try:
                fx = float (x)
                if fx != fx: out.append ('   NaN')
                else: out.append (f'{fx:+6.2f}')
            except (TypeError, ValueError):
                out.append ('  None')
    return '[' + ' '.join (out) + ']'


def main():
    ap = argparse.ArgumentParser (allow_abbrev=False)
    ap.add_argument ('--target-file', type=Path, default=None,
                     help='Load anchor metrics from JSON (targets/lex_*.json) instead of '
                          'live-rendering the Lex VST2. Required on Mac (no yabridge).')
    args, rest = ap.parse_known_args()
    overrides = parse_param_args (rest)

    params = dict (LOCKED)
    # FIX A — engine-neutral neutralization for non-P11 algos.
    algo_override = overrides.get ('Algorithm', '')
    if algo_override and algo_override != 'Hall (Lex)':
        neutral = {
            'Bass Multiply':   '1.0',
            'Mid Multiply':    '1.0',
            'Treble Multiply': '1.0',
            'Diffusion':       '0.0',
            'Saturation':      '0.0',
            'Early Ref Level': '0.0',
        }
        params.update (neutral)
        for k in list (params.keys()):
            if k.startswith ('Hall '):
                params.pop (k)
    params.update (overrides)

    if overrides:
        print ("Overrides applied:")
        for k, v in overrides.items():
            print (f"  {k} = {v}  (was {LOCKED.get (k, 'unset')})")

    render (params)

    # ─── Anchor source: JSON snapshot OR live render ─────────────
    if args.target_file:
        anchor = load_target (args.target_file)
        lex = anchor.as_measure_pair_dict()
    else:
        lex = measure_pair (LEX_IMP, LEX_NB)
    dv = measure_pair (OUTDIR / 'iter_impulse.wav', OUTDIR / 'iter_noiseburst.wav')

    pass_count = total = 0
    rows = []
    for k, jnd in JND.items():
        l, d = lex.get (k), dv.get (k)
        if l is None or d is None: continue
        if isinstance (l, list):
            n_expected = len (l)
            deltas = []
            n_missing = 0
            if not isinstance (d, list) or len (d) != n_expected:
                n_missing = n_expected
                d_iter = d if isinstance (d, list) else []
            else:
                d_iter = d
            for a, b in zip (l, d_iter):
                if a is None or b is None: deltas.append (None); n_missing += 1; continue
                try:
                    fa, fb = float (a), float (b)
                except (TypeError, ValueError):
                    deltas.append (None); n_missing += 1; continue
                if fa != fa or fb != fb:
                    deltas.append (float ('nan')); n_missing += 1; continue
                deltas.append (fb - fa)
            while len (deltas) < n_expected:
                deltas.append (None); n_missing += 1
            within = sum (1 for x in deltas if x is not None and x == x and abs (x) < jnd)
            ok = (within == n_expected) and n_missing == 0
            pass_count += (1 if ok else 0); total += 1
            if n_missing > 0:
                flag = f'{within}/{n_expected}*'
            else:
                flag = 'PASS' if ok else f'{within}/{n_expected}'
            rows.append (f'  {k:30}  jnd={jnd:>5.2f}  {flag:<7}  Δ={fvec (deltas)}')
        else:
            try:
                delta = float (d) - float (l)
            except (TypeError, ValueError):
                continue
            ok = abs (delta) < jnd
            pass_count += (1 if ok else 0); total += 1
            flag = 'PASS' if ok else 'OUT '
            rows.append (f'  {k:30}  jnd={jnd:>5.2f}  {flag:<7}  '
                         f'lex={float (l):+8.3f}  dv={float (d):+8.3f}  Δ={delta:+7.3f}')

    print()
    for r in rows: print (r)
    print (f'\n  TOTAL: {pass_count}/{total}')
    print (f'  rt60 lex: {fvec (lex.get ("rt60_per_band"))}')
    print (f'  rt60 dv : {fvec (dv.get ("rt60_per_band"))}')


if __name__ == '__main__':
    main()
