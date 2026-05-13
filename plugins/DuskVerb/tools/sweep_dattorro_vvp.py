#!/usr/bin/env python3
"""Targeted CLI sweep for Dattorro Vintage Vocal Plate preset.

Two axes:
  1. hi_cut sweep 8000-11000 Hz (vintage Lex Nyquist ceiling)
  2. boxiness trio: low_crossover × bass_mult × mid_mult (kill 200-500 hump)

Scores composite |Δ|/JND vs Lex Vintage Plate IR.

Usage:
  python3 sweep_dattorro_vvp.py
"""
import os
import subprocess
import sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from perceptual_diff import (
    load_mono, c80, a_weighted_rms_db,
    bass_ratio, treble_ratio, spectral_crest_db,
    d50, edt, stereo_correlation, time_domain_crest,
    box_ratio_db, k_weighted_lufs,
    spectral_flatness_octave,
)

RENDER_BIN = '/home/marc/projects/plugins/build/tests/duskverb_render/duskverb_render'
LEX_IMP = '/tmp/lex_compare/lex_v3_impulse.wav'
LEX_NB  = '/tmp/lex_compare/lex_v3_noiseburst.wav'

# Iter-2 Dattorro winner as base. Sweep axes override these.
BASE_PARAMS = {
    'Algorithm':       'Plate (Dattorro)',
    'Bus Mode':        'on',
    'Pre-Delay':       '20',
    'Decay Time':      '0.85',
    'Size':            '0.45',
    'Mod Depth':       '0.30',
    'Mod Rate':        '0.60',
    'Treble Multiply': '0.85',
    'Diffusion':       '0.55',
    'Early Ref Level': '0.0',
    'Early Ref Size':  '0.30',
    'Lo Cut':          '80',
    'Width':           '1.10',
    'Gain Trim':       '11.0',
    'Mono Below':      '20',
    'High Crossover':  '4500',
    'Saturation':      '0.10',
}

# JND map. Includes user-flagged metrics + structural ones.
JND = {
    'a_weighted': 0.5,
    'br':         0.10,
    'tr':         0.10,
    'c80':        1.0,
    'crest':      1.5,
    'box_db':     3.0,    # boxiness — user pain point. Above 3 dB JND = audible hump
    'flat_250':   0.10,   # spectral flatness @ 250 Hz — tonal vs noisy
    'lufs':       1.0,
    'td_crest':   2.0,
}


def render(params, slug):
    cmd = [RENDER_BIN,
           '--slug', slug, '--output-dir', '/tmp/dv_sweep']
    for k, v in params.items():
        cmd += ['--param', f'{k}={v}']
    result = subprocess.run(cmd, capture_output=True, timeout=60)
    if result.returncode != 0:
        raise RuntimeError(
            f"duskverb_render failed (code {result.returncode}) for slug={slug}: "
            f"{result.stderr.decode('utf-8', errors='replace')}")


def measure(impulse_path, noiseburst_path):
    sr_i, ir = load_mono(impulse_path)
    sr_n, nb = load_mono(noiseburst_path)
    peak = int(np.argmax(np.abs(ir)))
    ir_trim = ir[peak:]
    return {
        'a_weighted': a_weighted_rms_db(nb, sr_n),
        'br':         bass_ratio(ir_trim, sr_i),
        'tr':         treble_ratio(ir_trim, sr_i),
        'c80':        c80(ir, sr_i),
        'crest':      spectral_crest_db(nb[int(0.1 * sr_n):], sr_n),
        'box_db':     box_ratio_db(nb, sr_n),
        'flat_250':   spectral_flatness_octave(nb, sr_n, 250, t_start=0.1, t_end=2.0),
        'lufs':       k_weighted_lufs(nb, sr_n),
        'td_crest':   time_domain_crest(nb, sr_n),
    }


def main():
    os.makedirs('/tmp/dv_sweep', exist_ok=True)

    lex = measure(LEX_IMP, LEX_NB)
    print(f"Lex baseline:")
    print(f"  A-wt={lex['a_weighted']:+.2f}  LUFS={lex['lufs']:+.2f}  "
          f"BR={lex['br']:.3f}  TR={lex['tr']:.3f}")
    print(f"  C80={lex['c80']:+.2f}  Crest={lex['crest']:+.2f}  "
          f"Box={lex['box_db']:+.2f}  Flat250={lex['flat_250']:.3f}  "
          f"TDcr={lex['td_crest']:+.2f}")
    print()

    # ── Sweep 1: hi_cut at iter-2 base (boxiness uncorrected) ──
    print("=" * 78)
    print("SWEEP 1: hi_cut sweep at iter-2 base params (boxiness still hot)")
    print("=" * 78)
    print(f"{'hi_cut':>8s}  {'A':>5s} {'TR':>5s} {'C80':>5s} {'Crs':>5s} "
          f"{'Box':>5s} {'F250':>5s} {'LUFS':>5s}  {'sum':>6s}  audit")
    print('-' * 78)

    hicut_results = []
    for hicut in [8000, 9000, 10000, 11000]:
        params = dict(BASE_PARAMS)
        params['Hi Cut']        = f'{hicut}'
        params['Low Crossover'] = '400'
        params['Bass Multiply'] = '0.50'
        params['Mid Multiply']  = '0.70'
        slug = f'h{hicut//1000}'
        render(params, slug)
        m = measure(f'/tmp/dv_sweep/{slug}_impulse.wav',
                    f'/tmp/dv_sweep/{slug}_noiseburst.wav')
        errs = {k: abs(m[k] - lex[k]) / JND[k] for k in JND}
        composite = sum(errs.values())
        au = ''.join([
            'A' if errs['a_weighted'] <= 1 else 'a',
            'B' if errs['br']         <= 1 else 'b',
            'T' if errs['tr']         <= 1 else 't',
            'C' if errs['c80']        <= 1 else 'c',
            'X' if errs['crest']      <= 1 else 'x',
            'O' if errs['box_db']     <= 1 else 'o',
            'F' if errs['flat_250']   <= 1 else 'f',
            'L' if errs['lufs']       <= 1 else 'l',
        ])
        print(f"{hicut:>8d}  "
              f"{m['a_weighted']-lex['a_weighted']:>+5.1f} "
              f"{m['tr']-lex['tr']:>+5.2f} "
              f"{m['c80']-lex['c80']:>+5.1f} "
              f"{m['crest']-lex['crest']:>+5.1f} "
              f"{m['box_db']-lex['box_db']:>+5.1f} "
              f"{m['flat_250']-lex['flat_250']:>+5.2f} "
              f"{m['lufs']-lex['lufs']:>+5.1f}  "
              f"{composite:>6.2f}  {au}")
        hicut_results.append((composite, hicut, m, au))

    hicut_results.sort(key=lambda x: x[0])
    best_hicut = hicut_results[0][1]
    print(f"\n  → Best hi_cut by composite: {best_hicut} Hz")

    # ── Sweep 2: boxiness trio at best hi_cut ──
    print()
    print("=" * 78)
    print(f"SWEEP 2: low_crossover × bass_mult × mid_mult at hi_cut={best_hicut}")
    print("=" * 78)
    print(f"{'xover':>5s} {'bass':>5s} {'mid':>5s}  "
          f"{'A':>5s} {'TR':>5s} {'C80':>5s} {'Crs':>5s} {'Box':>5s} "
          f"{'F250':>5s} {'BR':>5s} {'LUFS':>5s}  {'sum':>6s}  audit")
    print('-' * 88)

    box_results = []
    for xover in [300, 400, 500, 600]:
        for bass in [0.40, 0.50, 0.65]:
            for mid in [0.55, 0.65, 0.75]:
                params = dict(BASE_PARAMS)
                params['Hi Cut']        = f'{best_hicut}'
                params['Low Crossover'] = f'{xover}'
                params['Bass Multiply'] = f'{bass}'
                params['Mid Multiply']  = f'{mid}'
                slug = f'b{xover}b{int(bass*100):02d}m{int(mid*100):02d}'
                render(params, slug)
                m = measure(f'/tmp/dv_sweep/{slug}_impulse.wav',
                            f'/tmp/dv_sweep/{slug}_noiseburst.wav')
                errs = {k: abs(m[k] - lex[k]) / JND[k] for k in JND}
                composite = sum(errs.values())
                au = ''.join([
                    'A' if errs['a_weighted'] <= 1 else 'a',
                    'B' if errs['br']         <= 1 else 'b',
                    'T' if errs['tr']         <= 1 else 't',
                    'C' if errs['c80']        <= 1 else 'c',
                    'X' if errs['crest']      <= 1 else 'x',
                    'O' if errs['box_db']     <= 1 else 'o',
                    'F' if errs['flat_250']   <= 1 else 'f',
                    'L' if errs['lufs']       <= 1 else 'l',
                ])
                print(f"{xover:>5d} {bass:>5.2f} {mid:>5.2f}  "
                      f"{m['a_weighted']-lex['a_weighted']:>+5.1f} "
                      f"{m['tr']-lex['tr']:>+5.2f} "
                      f"{m['c80']-lex['c80']:>+5.1f} "
                      f"{m['crest']-lex['crest']:>+5.1f} "
                      f"{m['box_db']-lex['box_db']:>+5.1f} "
                      f"{m['flat_250']-lex['flat_250']:>+5.2f} "
                      f"{m['br']-lex['br']:>+5.2f} "
                      f"{m['lufs']-lex['lufs']:>+5.1f}  "
                      f"{composite:>6.2f}  {au}")
                box_results.append((composite, xover, bass, mid, m, au))

    box_results.sort(key=lambda x: x[0])
    print()
    print("─── Top 5 cells by composite ───")
    for comp, xover, bass, mid, m, au in box_results[:5]:
        print(f"  xover={xover}  bass={bass:.2f}  mid={mid:.2f}  "
              f"composite={comp:.2f}  audit={au}  "
              f"Box={m['box_db']-lex['box_db']:+.1f}  "
              f"C80={m['c80']-lex['c80']:+.1f}  "
              f"BR={m['br']-lex['br']:+.2f}")

    # ── Final winning preset ──
    print()
    print("=" * 78)
    print("FINAL WINNER")
    print("=" * 78)
    best = box_results[0]
    print(f"  hi_cut        = {best_hicut}")
    print(f"  low_crossover = {best[1]}")
    print(f"  bass_multiply = {best[2]:.2f}")
    print(f"  mid_multiply  = {best[3]:.2f}")
    print()
    print("  Updated preset row (paste into FactoryPresets.h):")
    print(f'  {{ "Vintage Vocal Plate",  "Plates",')
    print(f'    0,  0.5f,   true,  20.0f, 0,')
    print(f'    0.85f, 0.45f, 0.30f, 0.60f, 0.85f, {best[2]:.2f}f,  {float(best[1]):.1f}f,')
    print(f'    0.55f, 0.00f, 0.30f,  80.0f, {float(best_hicut):.1f}f, 1.10f, false, 11.0f,')
    print(f'    /* mono */ 20.0f, /* mid */ {best[3]:.2f}f, /* highX */ 4500.0f, /* sat */ 0.10f }},')


if __name__ == '__main__':
    main()
