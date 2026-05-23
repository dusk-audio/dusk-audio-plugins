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

    # ── Engine 13 (TrueLex) defaults — inert when Algorithm != Hall (TrueLex)
    # since the APVTS axes only route to hallTrueLex_. Locked here so the
    # render is reproducible between hall_iter and hall_truelex_optuna.
    'TrueLex ER Level':     '1.0',
    'TrueLex Tank Level':   '1.0',
    'TrueLex ER W0':        '1.0',
    'TrueLex ER W1':        '0.65',
    'TrueLex ER W2':        '0.45',
    'TrueLex ER W3':        '0.30',
    'TrueLex AP Coeff':     '0.075',

    # ── Engine 14 (TrueLex 16) defaults — inert for non-Engine-14.
    'TrueLex16 ER Level':   '1.0',
    'TrueLex16 Tank Level': '1.0',
    'TrueLex16 ER W0':      '1.0',
    'TrueLex16 ER W1':      '0.65',
    'TrueLex16 ER W2':      '0.45',
    'TrueLex16 ER W3':      '0.30',
    'TrueLex16 ER W4':      '0.0',
    'TrueLex16 AP Coeff':   '0.0',
    # Engine 15 (LexFigure8) — in-loop structural HF damping cutoff.
    'LexFig8 Struct HF':    '8000.0',
    # Phase A — Pre-tank ER TDL (4 taps). Defaults match Lex Med Hall
    # peak_locations anchor [0.0, 4.0, 7.52, 9.79] ms with progressive
    # attenuation. Inert when Algorithm != Hall (LexFigure8).
    'LexFig8 ER Tap0 Dly':       '0.0',
    'LexFig8 ER Tap1 Dly':       '4.0',
    'LexFig8 ER Tap2 Dly':       '7.52',
    'LexFig8 ER Tap3 Dly':       '9.79',
    # v27 baseline — ER taps audible at -27/-30/-33/-36 dB (each below tank
    # peak amp ~0.055 so argmax stays on tank → late_tail still measured
    # at tank+500 ms via argmax-relative grader). Combined with the
    # 2026-05-22 first-significant peak_locations anchor, ER spikes at
    # 0/4/7.52/9.79 ms now drive peak_locations PASS without flipping
    # late_tail. Lifts v14 14/19 → v27 15/19.
    'LexFig8 ER Tap0 Gain':      '-27.0',
    'LexFig8 ER Tap1 Gain':      '-30.0',
    'LexFig8 ER Tap2 Gain':      '-33.0',
    'LexFig8 ER Tap3 Gain':      '-36.0',
    'LexFig8 ER Stereo Offset':  '0.20',
    'LexFig8 Tank Atten':        '1.0',
    'LexFig8 Tank In':           '1.0',
    # v18 Bloom — tank pre-delay default 0 (baseline behavior); CMA
    # pushes to ~11 ms so tank emerges AFTER ER tap 3 (9.79 ms).
    'LexFig8 Tank PreDly':       '0.0',
    # Phase B-redux — density-AP jitter exposed as APVTS. Defaults
    # match hardcoded pre-redux values so engine state is unchanged
    # when defaults are used. CMA pushes these up to smear box_ratio
    # + spectral_crest comb teeth at 200-500 Hz.
    'LexFig8 Density Jitter':    '0.02',
    'LexFig8 Density Rate':      '1.5',
    # Phase C — sub-bass band damping. Default 1.0 = neutral / shelf
    # bypassed entirely (Engine 10 + plates unchanged when this stays
    # at default). LexFigure8 lets CMA push >1 for warmer sub-bass or
    # <1 for tighter sub-bass.
    'LexFig8 Sub-Bass Mult':     '1.0',
    'LexFig8 Sub-Bass Xover':    '300',
    # Phase D — in-loop tilt high-shelf at 2 kHz. Default 0 = neutral
    # (shelf bypassed). Engine 10 + plates never set this.
    'LexFig8 Tilt':              '0.0',
    # Phase F — air-band shelf. v27 default 0.78 @ 7500 Hz delivers rt60
    # bin 7 PASS + centroid_drift bin 3 PASS without trashing bin 6.
    'LexFig8 Air Mult':          '0.78',
    'LexFig8 Air Xover':         '7500.0',
    # Phase J — per-stage density-AP delay overrides. Default 0 = no
    # override → tank uses hardcoded hall-scale densityAPBase. Tuner
    # explores > 0 to reshape rt60/density (does NOT move peak_locations
    # — output tap structure determines that, see DattorroTank.h:349).
    'LexFig8 DAP 0 Dly':         '0.0',
    'LexFig8 DAP 1 Dly':         '0.0',
    'LexFig8 DAP 2 Dly':         '0.0',
    'LexFig8 DAP 3 Dly':         '0.0',
    # Phase K (v28) — first-4 output-tap positionFrac overrides per channel.
    # Defaults match DattorroTank::kLeftOutputTaps[0..3] / kRightOutputTaps[0..3]
    # → identical audible state until tuner moves them.
    'LexFig8 OTap L0':           '0.120',
    'LexFig8 OTap L1':           '0.675',
    'LexFig8 OTap L2':           '0.480',
    'LexFig8 OTap L3':           '0.450',
    'LexFig8 OTap R0':           '0.140',
    'LexFig8 OTap R1':           '0.710',
    'LexFig8 OTap R2':           '0.520',
    'LexFig8 OTap R3':           '0.410',
    # Phase L (v29) — per-channel delay1/delay2 base ms override. Default
    # 0 = no override sentinel → tank uses hardcoded Dattorro hall constants
    # (44.1 kHz reference: 4507/4219/3769/3299 = 102.2/95.7/85.5/74.8 ms).
    # Tuner explores 0..150 ms; override = default ms is NOT bit-equivalent
    # to override = 0 (float-path divergence flips 5 PASSes).
    'LexFig8 Del1 L':            '0.0',
    'LexFig8 Del1 R':            '0.0',
    'LexFig8 Del2 L':            '0.0',
    'LexFig8 Del2 R':            '0.0',
    # Phase M (v30) — per-channel AP1/AP2 base ms override (diffuser rearch).
    # 0 = no override sentinel. Hall defaults at 16.1/21.6/42.4/62.3 ms.
    'LexFig8 AP1 L':             '0.0',
    'LexFig8 AP1 R':             '0.0',
    'LexFig8 AP2 L':             '0.0',
    'LexFig8 AP2 R':             '0.0',
    # Phase N+O (v31) — front-door rearch. Cross-feed coefficients default
    # 1.0 (Dattorro canonical full bleed). Bypass diffuser default 0 (active).
    'LexFig8 XFd L':             '1.0',
    'LexFig8 XFd R':             '1.0',
    'LexFig8 Bypass Diff':       '0.0',
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
