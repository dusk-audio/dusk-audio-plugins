#!/usr/bin/env python3
"""Decay sweep for VVP. Find decay value that matches Lex EDT @ 500-2kHz.
Verify A-weighted, BR, TR, boxiness, C80 stay within tolerance."""
import os, sys, subprocess
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from perceptual_diff import (
    load_mono, c80, a_weighted_rms_db,
    bass_ratio, treble_ratio, spectral_crest_db,
    edt, box_ratio_db, k_weighted_lufs,
)

REPO_ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
RENDER_BIN = os.environ.get(
    'DUSKVERB_RENDER',
    os.path.join(REPO_ROOT, 'build/tests/duskverb_render/duskverb_render'))
LEX_IMP = os.environ.get('DUSKVERB_LEX_IMP', '/tmp/lex_compare/lex_v3_impulse.wav')
LEX_NB  = os.environ.get('DUSKVERB_LEX_NB',  '/tmp/lex_compare/lex_v3_noiseburst.wav')

BASE = {
    'Algorithm':       'Plate (Dattorro Vintage)',
    'Bus Mode':        'on',
    'Pre-Delay':       '10',
    'Size':            '0.45',
    'Mod Depth':       '0.30',
    'Mod Rate':        '0.60',
    'Treble Multiply': '0.72',
    'Bass Multiply':   '0.65',
    'Low Crossover':   '400',
    'Diffusion':       '0.55',
    'Early Ref Level': '0.0',
    'Early Ref Size':  '0.30',
    'Lo Cut':          '80',
    'Hi Cut':          '8000',
    'Width':           '1.10',
    'Gain Trim':       '11.0',
    'Mono Below':      '20',
    'Mid Multiply':    '0.85',
    'High Crossover':  '4500',
    'Saturation':      '0.10',
}


def render(params, slug):
    cmd = [RENDER_BIN, '--slug', slug, '--output-dir', '/tmp/dv_sweep']
    for k, v in params.items():
        cmd += ['--param', f'{k}={v}']
    r = subprocess.run(cmd, capture_output=True, timeout=60)
    if r.returncode != 0:
        raise RuntimeError(r.stderr.decode('utf-8', errors='replace'))


def measure(imp_path, nb_path):
    sr_i, ir = load_mono(imp_path)
    sr_n, nb = load_mono(nb_path)
    peak = int(np.argmax(np.abs(ir)))
    ir_t = ir[peak:]
    return {
        'a':     a_weighted_rms_db(nb, sr_n),
        'lufs':  k_weighted_lufs(nb, sr_n),
        'br':    bass_ratio(ir_t, sr_i),
        'tr':    treble_ratio(ir_t, sr_i),
        'c80':   c80(ir, sr_i),
        'crest': spectral_crest_db(nb[int(0.1*sr_n):], sr_n),
        'box':   box_ratio_db(nb, sr_n),
        'edt_500':  edt(ir_t, sr_i, 500),
        'edt_1k':   edt(ir_t, sr_i, 1000),
        'edt_2k':   edt(ir_t, sr_i, 2000),
    }


os.makedirs('/tmp/dv_sweep', exist_ok=True)
lex = measure(LEX_IMP, LEX_NB)
print(f"Lex: A={lex['a']:+.2f} LUFS={lex['lufs']:+.2f} BR={lex['br']:.3f} "
      f"TR={lex['tr']:.3f} C80={lex['c80']:+.2f} Box={lex['box']:+.2f} "
      f"EDT500={lex['edt_500']:.2f} EDT1k={lex['edt_1k']:.2f} EDT2k={lex['edt_2k']:.2f}")
print()
print(f"{'decay':>6s}  {'A-Δ':>6s} {'LUFS-Δ':>7s} {'BR-Δ':>6s} {'TR-Δ':>6s} "
      f"{'C80-Δ':>6s} {'Box-Δ':>6s}  "
      f"{'EDT500':>7s} {'EDT1k':>6s} {'EDT2k':>6s}  EDT-mid-avg-Δ")
print('-' * 100)

results = []
for dcy in [1.30, 1.40, 1.50, 1.60, 1.70, 1.80, 2.00, 2.20]:
    params = dict(BASE)
    params['Decay Time'] = f'{dcy}'
    slug = f'd{int(dcy*100):03d}'
    render(params, slug)
    m = measure(f'/tmp/dv_sweep/{slug}_impulse.wav',
                f'/tmp/dv_sweep/{slug}_noiseburst.wav')
    mid_avg = (m['edt_500'] + m['edt_1k'] + m['edt_2k']) / 3.0
    lex_mid_avg = (lex['edt_500'] + lex['edt_1k'] + lex['edt_2k']) / 3.0
    mid_delta = mid_avg - lex_mid_avg
    print(f"{dcy:>6.2f}  {m['a']-lex['a']:>+6.2f} {m['lufs']-lex['lufs']:>+7.2f} "
          f"{m['br']-lex['br']:>+6.3f} {m['tr']-lex['tr']:>+6.2f} "
          f"{m['c80']-lex['c80']:>+6.2f} {m['box']-lex['box']:>+6.2f}  "
          f"{m['edt_500']:>6.2f}s {m['edt_1k']:>5.2f}s {m['edt_2k']:>5.2f}s  "
          f"{mid_delta:>+6.2f}s")
    results.append((abs(mid_delta), dcy, m, mid_delta))

results.sort()
print()
print("Best by |EDT-mid-avg-Δ|:")
for score, dcy, m, mid_delta in results[:3]:
    print(f"  decay={dcy:.2f}  mid-Δ={mid_delta:+.2f}s  A-Δ={m['a']-lex['a']:+.2f} "
          f"BR-Δ={m['br']-lex['br']:+.3f} TR-Δ={m['tr']-lex['tr']:+.2f} "
          f"C80-Δ={m['c80']-lex['c80']:+.2f} Box-Δ={m['box']-lex['box']:+.2f}")
