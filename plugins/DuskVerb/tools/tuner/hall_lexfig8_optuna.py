#!/usr/bin/env python3
"""CMA-ES tuner for LexFigure8Reverb (Engine 15) — v7 Phase A: ER TDL.

Engine 15 now has a pre-tank ER TDL added in parallel with the tank
output (4 taps, independent delay/gain + stereo offset). This v7 run
searches 22 axes:
   * 13 macros (decay, bass/mid/treble mult, crossovers, mod, etc.)
   * 9 ER TDL axes (4 dly, 4 gain, 1 stereo offset)

Penalty design per plan Phase E — per-metric directed multipliers
scaled to PC_REWARD (10000) magnitude so CMA cannot flee PC plateau
for cheaper local penalty wins. Targets all 7 failing v5 metrics.
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

# v7 seed: v5 elastic winner t352 macros + ER TDL defaults matching
# Lex Med Hall peak_locations anchor [0.0, 4.0, 7.52, 9.79] ms.
BASELINE = {
    # macros (v14 winner — 14/19 with spectral_crest + box_ratio PASS)
    'decay':        1.4316,
    'bass_mult':    1.6903,
    'mid_mult':     1.2317,
    'treble_mult':  0.9025,
    'low_xov':      1018.90,
    'high_xov':     4662.50,
    'hi_cut':       8109.78,
    'gain_trim':   12.6627,
    'sat':          0.0077,
    'mod_depth':    0.2110,
    'mod_rate':     7.3019,
    'diffusion':    0.4811,
    'struct_hf':   10434.52,
    # ER TDL — Lex peak_locations anchor with progressive attenuation
    'er_tap0_dly':  0.0,
    'er_tap1_dly':  4.0,
    'er_tap2_dly':  7.52,
    'er_tap3_dly':  9.79,
    # v27 — ER taps stay BELOW tank peak amp (~0.055 = -25 dB FS) so
    # argmax stays on the tank tail; first-significant peak_locations
    # grader (metrics.py 2026-05-22) still anchors at t=0 because the
    # ER taps clear the -20 dB-below-global-max floor. This is the
    # configuration that delivered v27 = 15/19 manually.
    'er_tap0_gain': -27.0,
    'er_tap1_gain': -30.0,
    'er_tap2_gain': -33.0,
    'er_tap3_gain': -36.0,
    'er_stereo_offset': 0.20,
    # Phase B-redux jitter defaults match v5 hardcoded values
    'density_jitter':   0.0554,
    'density_rate':     0.8657,
    # v15 — seed sub-bass at v14 winner state (effectively 1.0 = neutral).
    'sub_bass_mult':    1.0,
    'sub_bass_xover':   300.0,
    'tilt_db_oct':      0.0,
    # v27 — air shelf seeded at 0.78 @ 7500 Hz (delivers rt60 bin 7 PASS
    # + centroid_drift bin 3 PASS). Tuner explores ±2% leash.
    'air_mult':         0.78,
    'air_xover':        7500.0,
    # v27 — density-AP delay overrides default 0 (use hardcoded hall-scale
    # densityAPBase). Tuner explores 0..20 ms to reshape rt60/density.
    # NOTE: density-AP delays do NOT move peak_locations (output tap
    # structure does — see DattorroTank.h:349). Use this lever for the
    # remaining rt60_per_band / c80_per_octave / centroid_drift bins only.
    'dap_delay_0':      0.0,
    'dap_delay_1':      0.0,
    'dap_delay_2':      0.0,
    'dap_delay_3':      0.0,
    # v28 — first-4 output-tap positionFrac overrides per channel. Seeds
    # match DattorroTank::kLeftOutputTaps[0..3].positionFrac and right-
    # tank equivalents → identical audible state until CMA moves them.
    # Reshapes tank's natural early-energy emission geometry to attack
    # c80_per_octave / centroid_drift / time_domain_crest residuals.
    'otap_L0':          0.120,
    'otap_L1':          0.675,
    'otap_L2':          0.480,
    'otap_L3':          0.450,
    'otap_R0':          0.140,
    'otap_R1':          0.710,
    'otap_R2':          0.520,
    'otap_R3':          0.410,
    # v29 — per-channel delay1/delay2 base ms override (Lexicon-primes rearch).
    # Seed at 0 = no override sentinel → tank uses hardcoded Dattorro hall
    # constants → seed reproduces v27 15/19. Bounds let CMA explore the
    # ±20% range around hall defaults; values > 0 activate the override path.
    'del1_L_ms':        0.0,
    'del1_R_ms':        0.0,
    'del2_L_ms':        0.0,
    'del2_R_ms':        0.0,
    # v30 — per-channel AP1/AP2 base ms override (diffuser rearch). Seed at 0
    # = no override sentinel → tank uses hardcoded Dattorro hall constants
    # (AP1L=16.1, AP1R=21.6, AP2L=42.4, AP2R=62.3 ms). Tuner explores
    # 0..100 ms to reshape diffuser smear for transient crest + c80_per_octave.
    'ap1_L_ms':         0.0,
    'ap1_R_ms':         0.0,
    'ap2_L_ms':         0.0,
    'ap2_R_ms':         0.0,
    # v31 — front-door rearch. Cross-feed coefficient per channel (default
    # 1.0 = Dattorro canonical) + diffuser bypass (default 0.0 = active).
    'xfd_L':            1.0,
    'xfd_R':            1.0,
    'bypass_diff':      0.0,
    # v19 Bloom — tank pre-delay seed at 0 (v14 winner state). CMA
    # explores upward if peak_locations PASS gain outweighs other costs.
    'tank_pdly':        0.0,
    # Phase G — 8-band damping per octave. Seed all 1.0 = neutral
    # (uses 3-band damping). CMA explores per-band as needed.
    'band_mult_0':      1.0,
    'band_mult_1':      1.0,
    'band_mult_2':      1.0,
    'band_mult_3':      1.0,
    'band_mult_4':      1.0,
    'band_mult_5':      1.0,
    'band_mult_6':      1.0,
    'band_mult_7':      1.0,
    # v27 — ducker locked off (pure LTI). Depth bound (0,0) prevents CMA
    # from re-enabling; thresh/atk/rel become inert.
    'duck_thresh':      0.01,
    'duck_atk':         0.3,
    'duck_rel':         8.0,
    'duck_depth':       0.0,
}

# v8 — rollback to v5 elastic penalty + force ER taps audible.
# v7 lesson: broad ER gain bounds (-30..+6) let CMA mute ER and CMA
# then optimised tank-only configs that fought v5's known-working
# macros. Lock ER gains to AUDIBLE range so they're guaranteed to
# contribute peaks at 4/7.52/9.79 ms.
BOUNDS = {
    # v15 — tightened around v14 14/19 winner to keep PASSed metrics
    # stable; let new sub-bass axes do the heavy lifting on rt60 bins
    # 6,7 + c80_per_octave bin1.
    'decay':       (1.39, 1.48),
    'bass_mult':   (1.60, 1.80),
    'mid_mult':    (1.18, 1.30),
    'treble_mult': (0.70, 1.05),
    'low_xov':     (920.0, 1150.0),
    'high_xov':    (4400.0, 5000.0),
    'hi_cut':      (7500.0, 8500.0),
    'gain_trim':  (11.5, 13.5),
    'sat':         (0.00, 0.03),
    'mod_depth':   (0.18, 0.27),
    'mod_rate':    (6.5, 8.0),
    'diffusion':   (0.42, 0.55),
    'struct_hf':   (9000.0, 12000.0),
    # v13 — Phase B isolation. ER taps MUTED (-60 dB locked) so CMA
    # measures pure effect of 4th density AP. ER delays still searched
    # (cheap) but gains pinned near silence.
    # v19 — bounds span mute (-60) → audible (-6) so CMA can reproduce
    # v14 14/19 at seed (-57 dB ≈ muted) and explore upward.
    'er_tap0_dly':  (0.0, 0.1),
    'er_tap1_dly':  (3.9, 4.1),
    'er_tap2_dly':  (7.42, 7.62),
    'er_tap3_dly':  (9.69, 9.89),
    # v27 — ER tap gain upper bound clamped to -25 dB so ER amp stays
    # below tank peak (~0.055 = -25 dB FS). Otherwise argmax flips to
    # the ER tap → late_tail window shifts forward → late_tail measurement
    # +5 dB hot vs Lex (the artifact that capped us at 14/19).
    'er_tap0_gain': (-60.0, -25.0),
    'er_tap1_gain': (-60.0, -25.0),
    'er_tap2_gain': (-60.0, -25.0),
    'er_tap3_gain': (-60.0, -25.0),
    'er_stereo_offset': (0.10, 0.35),
    # Phase B-redux — density-AP jitter mechanism. Manual sweep
    # showed 0.04..0.09 jitter all reach 13/19 (spectral_crest PASS,
    # box_ratio improved from +4.70 to +3.32 dB). CMA searches the
    # full audible-safe range.
    'density_jitter':   (0.01, 0.18),
    'density_rate':     (0.3, 5.0),
    # Phase C — sub-bass band. CMA can boost (>1) or cut (<1) sub-bass
    # decay independently of main bass. Crossover sweep covers
    # 100-400 Hz (overlapping rt60 bins 0 + 1).
    'sub_bass_mult':    (0.5, 2.5),
    'sub_bass_xover':   (120.0, 400.0),
    # Phase D — in-loop tilt EQ at 2 kHz pivot. Manual sweep showed
    # tilt=-3 dB brings centroid bin 2 to PASS but breaks other PASSes.
    # CMA searches range to balance.
    'tilt_db_oct':      (-6.0, 6.0),
    # Phase E (Bloom) — tank internal pre-delay (ms). Lower bound 0
    # so CMA can reproduce v14 14/19 state; upper bound 18 ms lets
    # tank emerge past ER tap 3 when ER becomes audible.
    'tank_pdly':        (0.0, 18.0),
    # Phase G — 8-band damping bounds. Each band: 0.5–2.0 (decay scale).
    # Wide enough for CMA to reshape rt60_per_band into all-PASS bins.
    'band_mult_0':      (0.5, 2.5),
    'band_mult_1':      (0.5, 2.5),
    'band_mult_2':      (0.5, 2.5),
    'band_mult_3':      (0.5, 2.5),
    'band_mult_4':      (0.5, 2.5),
    'band_mult_5':      (0.5, 2.5),
    'band_mult_6':      (0.5, 2.5),
    'band_mult_7':      (0.5, 2.5),
    # Phase H — ducker bounds. v27 returns to pure LTI; ducker depth
    # locked at 0 via 0..0 bound so CMA cannot re-enable it.
    'duck_thresh':      (0.001, 0.3),
    'duck_atk':         (0.1, 10.0),
    'duck_rel':         (1.0, 100.0),
    'duck_depth':       (0.0, 0.0),
    # v27 air shelf — ±elastic leash around the manual sweet spot.
    'air_mult':         (0.50, 1.20),
    'air_xover':        (4500.0, 10000.0),
    # v27 density-AP delay overrides (ms at 44.1 kHz reference).
    # 0 = no override (default). 1..18 ms explores reshape range.
    'dap_delay_0':      (0.0, 18.0),
    'dap_delay_1':      (0.0, 18.0),
    'dap_delay_2':      (0.0, 18.0),
    'dap_delay_3':      (0.0, 18.0),
    # v28 — output-tap positionFrac bounds. Full 0.05..0.95 range gives
    # CMA full reshape authority over tank's natural early-energy geometry.
    # Avoid 0.0 and 1.0 to keep buffer reads inside delay-line span.
    'otap_L0':          (0.05, 0.95),
    'otap_L1':          (0.05, 0.95),
    'otap_L2':          (0.05, 0.95),
    'otap_L3':          (0.05, 0.95),
    'otap_R0':          (0.05, 0.95),
    'otap_R1':          (0.05, 0.95),
    'otap_R2':          (0.05, 0.95),
    'otap_R3':          (0.05, 0.95),
    # v29 — delay-base bounds. Lower bound 0 = no-override sentinel so seed
    # at 0 reaches v27 15/19 ceiling exactly. Upper bound = hall + 20%.
    # Hall defaults: del1L=102.2, del1R=95.7, del2L=85.5, del2R=74.8 ms.
    # CMA explores override-active region (0, upper) for Lexicon-prime fit.
    # v29 confirmed dead end: lock delay1/delay2 at sentinel 0 by clamping
    # range to (0, 0). CMA only explores override-OFF region for these axes.
    'del1_L_ms':        (0.0, 0.0),
    'del1_R_ms':        (0.0, 0.0),
    'del2_L_ms':        (0.0, 0.0),
    'del2_R_ms':        (0.0, 0.0),
    # v30 — AP1/AP2 base bounds. Lower 0 = sentinel = hardcoded path.
    # Upper covers ±50% around hall defaults (AP1 16-22, AP2 42-62 ms).
    'ap1_L_ms':         (0.0, 35.0),
    'ap1_R_ms':         (0.0, 40.0),
    'ap2_L_ms':         (0.0, 80.0),
    'ap2_R_ms':         (0.0, 95.0),
    # v31 — front door. Cross-feed bounds 0..2 (centered at 1.0 canonical).
    # Bypass-diff bool encoded as 0..1 (>=0.5 = bypass active in setter).
    'xfd_L':            (0.0, 2.0),
    'xfd_R':            (0.0, 2.0),
    'bypass_diff':      (0.0, 1.0),
}

LOCKED = {
    'Algorithm':            'Hall (LexFigure8)',
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
    outdir = Path (f'/tmp/dv_lexfig8_opt/{pid}_{tid}')
    if outdir.exists(): shutil.rmtree (outdir)
    outdir.mkdir (parents=True)

    def _sg (name):
        lo, hi = BOUNDS[name]
        return trial.suggest_float (name, lo, hi)

    # 13 macro axes
    decay       = _sg ('decay')
    bass_mult   = _sg ('bass_mult')
    mid_mult    = _sg ('mid_mult')
    treble_mult = _sg ('treble_mult')
    low_xov     = _sg ('low_xov')
    high_xov    = _sg ('high_xov')
    hi_cut      = _sg ('hi_cut')
    gain_trim   = _sg ('gain_trim')
    sat         = _sg ('sat')
    mod_depth   = _sg ('mod_depth')
    mod_rate    = _sg ('mod_rate')
    diffusion   = _sg ('diffusion')
    struct_hf   = _sg ('struct_hf')
    # 9 ER TDL axes
    er_t0_dly   = _sg ('er_tap0_dly')
    er_t1_dly   = _sg ('er_tap1_dly')
    er_t2_dly   = _sg ('er_tap2_dly')
    er_t3_dly   = _sg ('er_tap3_dly')
    er_t0_gain  = _sg ('er_tap0_gain')
    er_t1_gain  = _sg ('er_tap1_gain')
    er_t2_gain  = _sg ('er_tap2_gain')
    er_t3_gain  = _sg ('er_tap3_gain')
    er_stereo   = _sg ('er_stereo_offset')
    density_jit = _sg ('density_jitter')
    density_rt  = _sg ('density_rate')
    sub_bass_m  = _sg ('sub_bass_mult')
    sub_bass_x  = _sg ('sub_bass_xover')
    tilt_db     = _sg ('tilt_db_oct')
    tank_pdly   = _sg ('tank_pdly')
    band_mults  = [_sg (f'band_mult_{i}') for i in range (8)]
    duck_thresh = _sg ('duck_thresh')
    duck_atk    = _sg ('duck_atk')
    duck_rel    = _sg ('duck_rel')
    duck_depth  = _sg ('duck_depth')
    air_mult    = _sg ('air_mult')
    air_xover   = _sg ('air_xover')
    dap_delays  = [_sg (f'dap_delay_{i}') for i in range (4)]
    otap_L      = [_sg (f'otap_L{i}') for i in range (4)]
    otap_R      = [_sg (f'otap_R{i}') for i in range (4)]
    del1_L_ms   = _sg ('del1_L_ms')
    del1_R_ms   = _sg ('del1_R_ms')
    del2_L_ms   = _sg ('del2_L_ms')
    del2_R_ms   = _sg ('del2_R_ms')
    ap1_L_ms    = _sg ('ap1_L_ms')
    ap1_R_ms    = _sg ('ap1_R_ms')
    ap2_L_ms    = _sg ('ap2_L_ms')
    ap2_R_ms    = _sg ('ap2_R_ms')
    xfd_L       = _sg ('xfd_L')
    xfd_R       = _sg ('xfd_R')
    bypass_diff = _sg ('bypass_diff')

    params = dict (LOCKED)
    params['Decay Time']           = f'{decay:.4f}'
    params['Bass Multiply']        = f'{bass_mult:.4f}'
    params['Mid Multiply']         = f'{mid_mult:.4f}'
    params['Treble Multiply']      = f'{treble_mult:.4f}'
    params['Low Crossover']        = f'{low_xov:.4f}'
    params['High Crossover']       = f'{high_xov:.4f}'
    params['Hi Cut']               = f'{hi_cut:.4f}'
    params['Gain Trim']            = f'{gain_trim:.4f}'
    params['Saturation']           = f'{sat:.4f}'
    params['Mod Depth']            = f'{mod_depth:.4f}'
    params['Mod Rate']             = f'{mod_rate:.4f}'
    params['Diffusion']            = f'{diffusion:.4f}'
    params['LexFig8 Struct HF']    = f'{struct_hf:.4f}'
    params['LexFig8 ER Tap0 Dly']  = f'{er_t0_dly:.4f}'
    params['LexFig8 ER Tap1 Dly']  = f'{er_t1_dly:.4f}'
    params['LexFig8 ER Tap2 Dly']  = f'{er_t2_dly:.4f}'
    params['LexFig8 ER Tap3 Dly']  = f'{er_t3_dly:.4f}'
    params['LexFig8 ER Tap0 Gain'] = f'{er_t0_gain:.4f}'
    params['LexFig8 ER Tap1 Gain'] = f'{er_t1_gain:.4f}'
    params['LexFig8 ER Tap2 Gain'] = f'{er_t2_gain:.4f}'
    params['LexFig8 ER Tap3 Gain'] = f'{er_t3_gain:.4f}'
    params['LexFig8 ER Stereo Offset'] = f'{er_stereo:.4f}'
    params['LexFig8 Density Jitter']   = f'{density_jit:.4f}'
    params['LexFig8 Density Rate']     = f'{density_rt:.4f}'
    params['LexFig8 Sub-Bass Mult']    = f'{sub_bass_m:.4f}'
    params['LexFig8 Sub-Bass Xover']   = f'{sub_bass_x:.4f}'
    params['LexFig8 Tilt']             = f'{tilt_db:.4f}'
    params['LexFig8 Tank PreDly']      = f'{tank_pdly:.4f}'
    for i in range (8):
        params[f'LexFig8 Band {i} Mult'] = f'{band_mults[i]:.4f}'
    params['LexFig8 Duck Thresh'] = f'{duck_thresh:.4f}'
    params['LexFig8 Duck Atk']    = f'{duck_atk:.4f}'
    params['LexFig8 Duck Rel']    = f'{duck_rel:.4f}'
    params['LexFig8 Duck Depth']  = f'{duck_depth:.4f}'
    params['LexFig8 Air Mult']    = f'{air_mult:.4f}'
    params['LexFig8 Air Xover']   = f'{air_xover:.4f}'
    for i in range (4):
        params[f'LexFig8 DAP {i} Dly'] = f'{dap_delays[i]:.4f}'
    for i in range (4):
        params[f'LexFig8 OTap L{i}'] = f'{otap_L[i]:.4f}'
        params[f'LexFig8 OTap R{i}'] = f'{otap_R[i]:.4f}'
    params['LexFig8 Del1 L'] = f'{del1_L_ms:.4f}'
    params['LexFig8 Del1 R'] = f'{del1_R_ms:.4f}'
    params['LexFig8 Del2 L'] = f'{del2_L_ms:.4f}'
    params['LexFig8 Del2 R'] = f'{del2_R_ms:.4f}'
    params['LexFig8 AP1 L'] = f'{ap1_L_ms:.4f}'
    params['LexFig8 AP1 R'] = f'{ap1_R_ms:.4f}'
    params['LexFig8 AP2 L'] = f'{ap2_L_ms:.4f}'
    params['LexFig8 AP2 R'] = f'{ap2_R_ms:.4f}'
    params['LexFig8 XFd L'] = f'{xfd_L:.4f}'
    params['LexFig8 XFd R'] = f'{xfd_R:.4f}'
    params['LexFig8 Bypass Diff'] = f'{bypass_diff:.4f}'

    cmd = [str (RENDER), '--slug', 'lf8', '--output-dir', str (outdir)]
    for k, v in params.items():
        cmd += ['--param', f'{k}={v}']
    try:
        subprocess.run (cmd, check=True, capture_output=True, timeout=60)
        dv = measure_pair (outdir / 'lf8_impulse.wav', outdir / 'lf8_noiseburst.wav')
        lex = _load_lex()
        loss, _, _ = compute_loss (dv, lex, WEIGHTS)
        pc, total = count_pass (dv, lex)
    except Exception:
        return 1e6

    # v8 — rollback to v5 elastic penalty design (delivered 12/19).
    # Phase A engine adds ER axes; tuner penalty stays minimal so PC
    # reward dominates the optimisation surface.
    aw_lex = float (lex.get ('a_weighted_rms_db', 0.0) or 0.0)
    aw_dv  = float (dv .get ('a_weighted_rms_db', 0.0) or 0.0)
    aw_excess = max (0.0, abs (aw_dv - aw_lex) - 0.5)
    vol_penalty = 5000.0 * aw_excess

    sc_lex = float (lex.get ('spectral_crest_db', 0.0) or 0.0)
    sc_dv  = float (dv .get ('spectral_crest_db', 0.0) or 0.0)
    metallic_penalty = 25000.0 * max (0.0, sc_dv - sc_lex)

    c80_lex = float (lex.get ('c80', 0.0) or 0.0)
    c80_dv  = float (dv .get ('c80', 0.0) or 0.0)
    d50_lex = float (lex.get ('d50', 0.0) or 0.0)
    d50_dv  = float (dv .get ('d50', 0.0) or 0.0)
    clarity_penalty = 50000.0 * (
        max (0.0, abs (c80_dv - c80_lex) - 0.8)
        + max (0.0, abs (d50_dv - d50_lex) - 0.8))

    # v15 — Phase C. Penalties target NEW failing axes:
    #   rt60_per_band bins 6,7 (8/16 kHz long under v14)
    #   c80_per_octave bin 1 (500 Hz, +3.07 dB hot)
    # Plus carry forward v14's box_ratio penalty.
    br_lex = float (lex.get ('box_ratio_db', 0.0) or 0.0)
    br_dv  = float (dv .get ('box_ratio_db', 0.0) or 0.0)
    box_penalty = 15000.0 * max (0.0, abs (br_dv - br_lex) - 1.0)

    # rt60_per_band — per-bin 5000× excess. Target HF bins.
    rt_lex = lex.get ('rt60_per_band')
    rt_dv  = dv .get ('rt60_per_band')
    rt_penalty = 0.0
    if isinstance (rt_lex, list) and isinstance (rt_dv, list):
        for i in range (min (len (rt_lex), len (rt_dv))):
            try:
                a = float (rt_lex[i]); b = float (rt_dv[i])
                if a != a or b != b: continue
                rt_penalty += 5000.0 * max (0.0, abs (b - a) - 0.10)
            except (TypeError, ValueError):
                pass

    # c80_per_octave — per-bin 6000× excess (NEW penalty for bin 1).
    cpo_lex = lex.get ('c80_per_octave')
    cpo_dv  = dv .get ('c80_per_octave')
    cpo_penalty = 0.0
    if isinstance (cpo_lex, list) and isinstance (cpo_dv, list):
        for i in range (min (len (cpo_lex), len (cpo_dv))):
            try:
                a = float (cpo_lex[i]); b = float (cpo_dv[i])
                if a != a or b != b: continue
                cpo_penalty += 6000.0 * max (0.0, abs (b - a) - 1.5)
            except (TypeError, ValueError):
                pass

    # v17 — centroid_drift penalty CUT 3× (6000 → 2000) so CMA
    # respects PC plateau instead of sacrificing PASSes.
    cd_lex = lex.get ('centroid_drift_per_band')
    cd_dv  = dv .get ('centroid_drift_per_band')
    cd_penalty = 0.0
    if isinstance (cd_lex, list) and isinstance (cd_dv, list):
        for i in range (min (len (cd_lex), len (cd_dv))):
            try:
                a = float (cd_lex[i]); b = float (cd_dv[i])
                if a != a or b != b: continue
                cd_penalty += 2000.0 * max (0.0, abs (b - a) - 2.5)
            except (TypeError, ValueError):
                pass

    # v17 — peak_locations_ms penalty (15000× per ms excess). ER
    # unmuted should flip this to PASS; penalty ensures CMA uses ER.
    pl_lex = lex.get ('peak_locations_ms')
    pl_dv  = dv .get ('peak_locations_ms')
    pl_penalty = 0.0
    if isinstance (pl_lex, list) and isinstance (pl_dv, list):
        for i in range (min (len (pl_lex), len (pl_dv))):
            try:
                a = float (pl_lex[i]); b = float (pl_dv[i])
                if a != a or b != b: pl_penalty += 15000.0 * 5.0; continue
                pl_penalty += 15000.0 * max (0.0, abs (b - a) - 1.0)
            except (TypeError, ValueError):
                pl_penalty += 15000.0 * 5.0

    # v17 — time_domain_crest asymmetric (under only). DV needs to be
    # SPIKIER to match Lex. ER taps deliver spikes; penalty 8000× per
    # dB UNDER Lex outside JND.
    tdc_lex = float (lex.get ('time_domain_crest', 0.0) or 0.0)
    tdc_dv  = float (dv .get ('time_domain_crest', 0.0) or 0.0)
    tdc_penalty = 8000.0 * max (0.0, (tdc_lex - tdc_dv) - 1.5)

    # v20 — PC reward boosted 10x (10000 -> 100000) so CMA cannot trade
    # away PASSes for penalty reduction. Honors the 14/19 verified
    # ceiling and only accepts trades that ADD PC.
    score = (-pc * 100000.0 + loss * 0.01 + vol_penalty
             + metallic_penalty + clarity_penalty + box_penalty
             + rt_penalty + cpo_penalty + cd_penalty
             + pl_penalty + tdc_penalty)
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
        print (f'[t{trial.number:4d}] sc={trial.value:9.1f} pass={pc:2d}/{total} '
               f'dec={p["decay"]:.3f} bM={p["bass_mult"]:.2f} mM={p["mid_mult"]:.2f} tM={p["treble_mult"]:.3f} '
               f'trim={p["gain_trim"]:+.2f} mod=[{p["mod_depth"]:.2f},{p["mod_rate"]:.1f}] '
               f'hxov={p["high_xov"]:.0f} hi={p["hi_cut"]:.0f} sHF={p["struct_hf"]:.0f} '
               f'ER=[{p["er_tap0_dly"]:.1f}/{p["er_tap0_gain"]:+.1f} '
               f'{p["er_tap1_dly"]:.1f}/{p["er_tap1_gain"]:+.1f} '
               f'{p["er_tap2_dly"]:.1f}/{p["er_tap2_gain"]:+.1f} '
               f'{p["er_tap3_dly"]:.1f}/{p["er_tap3_gain"]:+.1f}] '
               f'so={p["er_stereo_offset"]:+.2f} {flag}', flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument ('--trials',  type=int, default=600)
    ap.add_argument ('--workers', type=int, default=4)
    ap.add_argument ('--seed',    type=int, default=48)
    ap.add_argument ('--sigma0',  type=float, default=0.05)
    ap.add_argument ('--target-file', type=Path, default=None)
    args = ap.parse_args()

    global _TARGET_FILE
    _TARGET_FILE = args.target_file

    anchor_label = (f'JSON: {args.target_file.name}' if args.target_file
                    else 'no target')
    print (f'CMA-ES — Hall (LexFigure8) Engine 15 v31 FRONT-DOOR REARCH — trials={args.trials} workers={args.workers}')
    print (f'  Anchor: {anchor_label}')
    print (f'  Seed: v27 15/19 + 3 v31 axes (xfd_L=1, xfd_R=1, bypass_diff=0 = canonical).')
    print (f'  New levers: per-channel cross-feed coeff (0..2), input diffuser bypass.')
    print (f'  Reshapes inter-tank coupling → centroid_drift, transient smear → c80/tdc.')
    print (f'  Grader: peak_locations first-significant arrival. PC reward 100000×.')
    print (f'  Ducker locked off (LTI). Engine 10/plates shielded.')
    print ()

    x0_v31 = {k: BASELINE[k] for k in BOUNDS.keys()}
    sampler = CmaEsSampler (x0=x0_v31, sigma0=args.sigma0,
                            seed=args.seed, n_startup_trials=1,
                            warn_independent_sampling=False)
    Path ('/tmp/dv_lexfig8_opt').mkdir (exist_ok=True)
    study = optuna.create_study (study_name='hall_lexfig8_cma_v31_frontdoor',
                                 direction='minimize', sampler=sampler,
                                 storage='sqlite:////tmp/hall_lexfig8_optuna_v31.db',
                                 load_if_exists=True)
    study.enqueue_trial (dict (x0_v31))

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

    out = Path ('/tmp/hall_lexfig8_optuna_best.json')
    with open (out, 'w') as f:
        json.dump ({'pc': pc, 'score': float (best.value), 'params': best.params,
                    'trial': best.number, 'wall_s': elapsed,
                    'n_trials_total': len (study.trials)}, f, indent=2)
    print (f'  Saved: {out}')


if __name__ == '__main__':
    main()
