#!/usr/bin/env python3
"""Targeted CLI sweep: diffusion × early-ref × decay → perceptual metrics
vs Lex reference. Uses duskverb_render (CLI, NOT pedalboard) + the
psychoacoustic helpers from perceptual_diff.py.

Goal: drive C80 up (DV currently -0.37, target +6.75) and Crest up
(DV currently 19.87, target 32.63) WITHOUT breaking BR/TR/A-weighted
that we just achieved.

Usage:
  python3 sweep_perceptual.py
"""
import os
import subprocess
import sys
import numpy as np
from scipy.io import wavfile

# Reuse psychoacoustic helpers from perceptual_diff.py (no re-derivation).
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from perceptual_diff import (
    load_mono, c80, a_weighted_rms_db,
    bass_ratio, treble_ratio, spectral_crest_db,
    d50, edt, stereo_correlation, time_domain_crest, c80_per_octave,
)

RENDER_BIN = '/home/marc/projects/plugins/build/tests/duskverb_render/duskverb_render'
LEX_IMP = '/tmp/lex_compare/lex_v3_impulse.wav'
LEX_NB  = '/tmp/lex_compare/lex_v3_noiseburst.wav'

# Locked params reflect the current FactoryPresets.h Vintage Vocal Plate.
BASE_PARAMS = {
    'Algorithm':       'Plate (Dattorro Vintage)',
    'Bus Mode':        'on',
    'Pre-Delay':       '20',
    'Size':            '0.55',
    'Mod Depth':       '0.0',
    'Mod Rate':        '1.8',
    'Treble Multiply': '1.1',
    'Bass Multiply':   '0.10',
    'Low Crossover':   '200',
    'Lo Cut':          '30',
    'Hi Cut':          '18000',
    'Width':           '1.1',
    'Gain Trim':       '3.5',
    'High Crossover':  '3500',
    'Saturation':      '0.05',
    'Bass Choke':      '300',
    'Early Ref Size':  '0.30',
}

# Sweep axes — target the two newly-exposed gaps:
#   • C80@250 +9.1 dB (DV too "front" in mids) → ER level DOWN
#   • EDT@500 -0.26 s (DV decay too short to ear) → Decay UP + Mid Mult UP
DIFFUSION_GRID = [0.40]                   # locked (single value)
ER_LEVEL_GRID  = [0.02, 0.04, 0.06]       # sparse-tap level — tame mid C80
DECAY_GRID     = [0.85, 1.05, 1.30]       # decay time — extend EDT
MID_GRID       = [0.85, 1.10]             # mid multiply — extend EDT @ 500/1k

# JNDs for the composite "how far off" score
JND = {
    'a_weighted': 0.5,    # dBA
    'br':         0.10,
    'tr':         0.10,
    'c80':        1.0,    # dB
    'crest':      1.5,    # dB
    'd50':        1.0,    # dB — 50 ms clarity
    'stereo':     0.10,   # correlation diff
    'td_crest':   2.0,    # dB — time-domain envelope crest
    'edt_500':    0.10,   # seconds — perceived RT60 @ 500 Hz octave
    'c80_250':    2.0,    # dB — per-octave clarity in mids (catches sparse-tap over-front)
}


def render(params, slug):
    cmd = [RENDER_BIN, '--preset', 'Vintage Vocal Plate',
           '--slug', slug, '--output-dir', '/tmp']
    for k, v in params.items():
        cmd += ['--param', f'{k}={v}']
    result = subprocess.run(cmd, capture_output=True, timeout=30)
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
        'd50':        d50(ir_trim, sr_i),
        'stereo':     stereo_correlation(noiseburst_path),
        'td_crest':   time_domain_crest(nb, sr_n),
        'edt_500':    edt(ir, sr_i, 500),
        'c80_250':    c80_per_octave(ir, sr_i, 250),
    }


def main():
    # Lex baseline
    lex = measure(LEX_IMP, LEX_NB)
    print(f"Lex baseline: A-wt={lex['a_weighted']:+.2f}  "
          f"BR={lex['br']:.3f}  TR={lex['tr']:.3f}  "
          f"C80={lex['c80']:+.2f}  Crest={lex['crest']:+.2f}  "
          f"D50={lex['d50']:+.2f}  Stereo={lex['stereo']:+.3f}  "
          f"TDcr={lex['td_crest']:+.2f}  EDT500={lex['edt_500']:.2f}s  "
          f"C80@250={lex['c80_250']:+.2f}")
    print()

    header = (
        f"{'dcy|mm|er':>14s}  "
        f"{'A':>5s} {'BR':>5s} {'TR':>5s} {'C80':>5s} {'Crs':>5s} "
        f"{'D50':>5s} {'Std':>5s} {'TDc':>5s} {'EDT':>5s} {'C80m':>5s}  "
        f"{'sum':>6s}  {'audit':>10s}"
    )
    print(header)
    print('-' * len(header))

    results = []
    for dcy in DECAY_GRID:
        for mm in MID_GRID:
            for er in ER_LEVEL_GRID:
                for diff in DIFFUSION_GRID:
                    params = dict(BASE_PARAMS)
                    params['Decay Time']      = f'{dcy}'
                    params['Early Ref Level'] = f'{er}'
                    params['Diffusion']       = f'{diff}'
                    params['Mid Multiply']    = f'{mm}'
                    slug = (f'sw_d{int(diff*100):02d}e{int(er*100):02d}'
                            f'c{int(dcy*100):03d}m{int(mm*100):03d}')
                    render(params, slug)

                    imp = f'/tmp/{slug}_impulse.wav'
                    nb  = f'/tmp/{slug}_noiseburst.wav'
                    m = measure(imp, nb)

                    errs = {k: abs(m[k] - lex[k]) / JND[k] for k in JND}
                    composite_max = max(errs.values())
                    composite_sum = sum(errs.values())
                    au = (('A' if errs['a_weighted'] <= 1 else 'a')
                          + ('B' if errs['br'] <= 1 else 'b')
                          + ('T' if errs['tr'] <= 1 else 't')
                          + ('C' if errs['c80'] <= 1 else 'c')
                          + ('X' if errs['crest'] <= 1 else 'x')
                          + ('D' if errs['d50'] <= 1 else 'd')
                          + ('S' if errs['stereo'] <= 1 else 's')
                          + ('E' if errs['td_crest'] <= 1 else 'e')
                          + ('R' if errs['edt_500'] <= 1 else 'r')
                          + ('M' if errs['c80_250'] <= 1 else 'm'))
                    print(f"{dcy:>4.2f}|{mm:>4.2f}|{er:>4.2f}  "
                          f"{m['a_weighted']-lex['a_weighted']:>+5.1f} "
                          f"{m['br']-lex['br']:>+5.2f} "
                          f"{m['tr']-lex['tr']:>+5.2f} "
                          f"{m['c80']-lex['c80']:>+5.1f} "
                          f"{m['crest']-lex['crest']:>+5.1f} "
                          f"{m['d50']-lex['d50']:>+5.1f} "
                          f"{m['stereo']-lex['stereo']:>+5.2f} "
                          f"{m['td_crest']-lex['td_crest']:>+5.1f} "
                          f"{m['edt_500']-lex['edt_500']:>+5.2f} "
                          f"{m['c80_250']-lex['c80_250']:>+5.1f}  "
                          f"{composite_sum:>6.1f}  {au:>10s}")
                    results.append((composite_sum, composite_max,
                                    diff, er, dcy, mm, m, errs, au))

    # Best by composite_sum
    results.sort(key=lambda r: r[0])
    print()
    print("─── Top 5 cells by composite (sum of |Δ|/JND) ───")
    for cs, cm, diff, er, dcy, mm, m, errs, audit in results[:5]:
        print(f"  dcy={dcy:.2f} mm={mm:.2f} er={er:.2f}  "
              f"max={cm:.2f}×JND  sum={cs:.2f}  audit={audit}  "
              f"EDT500={m['edt_500']:.2f}s  C80@250={m['c80_250']:+.1f}  "
              f"C80={m['c80']:+.1f}  BR={m['br']:.3f}")

    print()
    print("audit legend: A=A-wt B=BR T=TR C=C80 X=spectral-Crest "
          "D=D50 S=Stereo E=TD-crest R=EDT@500 M=C80@250 (capital=pass)")


if __name__ == '__main__':
    main()
