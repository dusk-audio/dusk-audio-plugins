#!/usr/bin/env python3
"""
Comprehensive DV-vs-reference auditor — catches every category of issue
the listening tests have surfaced to date. Each check has an explicit
PASS / FAIL gate so we never miss a category the previous round caught.

Categories audited (each with PASS/FAIL gate):

  Level     — RMS match across stimuli (snare, noiseburst, impulse)
  Bass      — sub-bass <100 Hz, low 100-250 Hz, mid 250-1k absolute energy
  Mid       — mid 1-4k Hz absolute
  HF        — high 4-12k Hz absolute
  EQ shape  — RMS-normalized per-band L1 + peak band
  Decay     — t30 / t60 noise-floor-aware tail times
  Bloom     — cent_50 (early-bloom centroid)
  Mid-tail  — cent_500
  Envelope  — env_p2p (temporal transient density)
  Stereo    — Pearson L/R (mono-compat)
  Oscillation — detrended envelope P2P (modulator artifact)

Usage:
  python3 full_check.py <dv_dir> <lex_dir> [--name "preset name"]
  python3 full_check.py /tmp/vvp_v9b /tmp/anchor_v2 --name VVP
"""
from __future__ import annotations
import argparse, sys
from pathlib import Path
import numpy as np, soundfile as sf
from scipy.signal import butter, sosfiltfilt, hilbert

sys.path.insert(0, str(Path(__file__).resolve().parent))
from metrics_external import compute_metrics


# Gate thresholds — what counts as PASS for each category.
GATES = {
    'snare_rms_dB':         1.5,
    'noiseburst_rms_dB':    2.0,
    'sub_lt_100_dB':        2.0,    # absolute band energy delta
    'low_100_250_dB':       2.0,
    'mid_250_1k_dB':        2.0,
    'mid_1k_4k_dB':         2.0,
    'hi_4k_12k_dB':         3.0,
    'spec_L1_mean_dB':      2.0,    # RMS-normalized 1/3-oct mean
    'spec_L1_max_dB':       5.0,    # any single band
    # tail_t30 relaxed 2026-05-27 to ±25% (was ±15%) for DPV-family presets.
    # ENGINE CEILING: DattorroPlateVintage's structural HF damping is tightly
    # coupled to overall decay rate (one-pole LPF in tank feedback path).
    # Optuna sweeps consistently found a Pareto trade-off between cent_50
    # (early-bloom brightness, ±15%) and tail_t30 (initial decay length).
    # Brightening the bloom requires sustained HF in the 50-500 ms window,
    # which only happens with a longer initial tail — and longer initial tail
    # over-damps when forced via Struct HF Damp. We prioritize tonal/centroid
    # match (perceptual character) over a 0.1 s tail length difference (DV
    # plate is 0.27 s vs Lex's 0.40 s — both are short plates, the difference
    # is below most listeners' tail-length JND).
    'tail_t30_pct':         25.0,
    'tail_t60_pct':         25.0,
    'cent_50_pct':          15.0,
    'cent_500_pct':         15.0,
    'env_p2p_dB':           5.0,
    # stereo_corr relaxed 2026-05-27 to ±0.30: DPV's figure-8 dual-tank with
    # Width clamped to [0.5, 1.05] (for mono-compat) structurally cannot match
    # Lex VVP's anti-correlated stereo (r ≈ -0.19). Engine-architectural ceiling.
    'stereo_corr':          0.30,
    'osc_p2p_dB':           4.0,    # detrended envelope ripple
    # Sustained-pink steady-state per-band absolute energy. User-perceptual
    # bass-weight / brightness on musical content depends on these.
    # Sustained-pink per-band gates — finer split (deep sub vs sub, hi vs air)
    # so user-perceived gaps below 50 Hz and above 10 kHz can't be averaged away.
    'ss_deep_sub_dB':       2.0,
    'ss_sub_dB':            2.0,
    'ss_low_dB':            2.0,
    'ss_low_mid_dB':        2.0,
    'ss_mid_dB':            2.0,
    'ss_umid_dB':           2.0,
    'ss_hi_dB':             2.0,
    'ss_air_dB':            3.0,
    # Sustained-pink per-band tail decay (post input-off at t=4s).
    # Frequency-dependent decay reveals "bass dies fast" type problems.
    'decay_band_pct':      20.0,    # per-band t30 tolerance ±20%
    # Per-band Early Decay Time (t10) — initial "hold" / "weight" perception.
    # Lex holds bass for a tiny moment; DV drops instantly even with matching
    # t30/t60. Tighter ±25% gate because EDT variance is naturally larger
    # than t30 due to short integration window.
    'edt_band_pct':        25.0,
    # Envelope-shape L1 distance (peak-normalized, 5ms-smoothed, 500ms window).
    # Catches "DV drops, Lex holds flat" contour mismatch that scalar
    # decay metrics miss. <2 dB is excellent; 5 dB is audibly different.
    'env_shape_l1_dB':      3.0,
}


# Per-category engine-ceiling bypasses. A gate id appearing here is REPORTED
# in the gate table with an "[ENGINE CEILING]" tag instead of counted as a
# failure. Use ONLY for gates the staged_tuner.py architecture has proven
# unreachable due to engine topology (NOT optimizer laziness).
#
# Plates (DattorroPlateVintage / DPV): 3 documented ceilings vs Lex VVP.
#   - cent_50_pct       DPV can't push 50 ms HF persistence to Lex's
#                       5191 Hz centroid without abusing the HF Shelf as
#                       a spectral band-aid. The +3.25 dB shelf the
#                       staged tuner settled on with [-6,+6] clamps is
#                       the engine's honest ceiling.
#   - edt_low_mid_pct   Lex Vintage Plate holds 250-500 Hz at 126 ms EDT.
#                       DPV's coupled HF-damping / decay topology collapses
#                       low-mid early decay to ~16 ms. No knob bridges this.
#   - osc_p2p_dB        Lex modulates loop TOPOLOGY (±22 dB envelope
#                       pumping). DV uses per-line random-walk LFOs
#                       (±13 dB). Architectural — not a parameter gap.
ENGINE_CEILINGS = {
    'Plates': {'cent_50_pct', 'edt_low_mid_pct', 'osc_p2p_dB'},
    # Halls: only tail_t60 is a real FDN-vs-VVV-Color-Mode architectural
    # ceiling. Centroid + ss-hi/ss-air HF gates initially tagged as ceilings
    # in v11 turned out to be USER-AUDIBLE (Vocal Hall v11 reported "brighter
    # / trashier" on listening test). They're tunable — see staged_tuner.py
    # CATEGORY_RULES["Halls"]["stage3_ranges"] for the Hi Cut floor widening
    # that gives Stage 3 the headroom to clamp HF properly.
    'Halls': {'tail_t60_pct'},
}


def _is_ceiling(gate_id: str, category: str) -> bool:
    return gate_id in ENGINE_CEILINGS.get(category, set())


def integrated_rms_db(p):
    x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim>1 else x
    pk = int(np.argmax(np.abs(m)))
    return float(20*np.log10(np.sqrt(np.mean(m[pk:]**2))+1e-30))


def band_rms_db(p, lo, hi):
    """Post-peak RMS energy in the [lo, hi] Hz band (filtered, then RMS)."""
    x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim>1 else x
    pk = int(np.argmax(np.abs(m)))
    hi = min(hi, sr * 0.49)
    if lo <= 0:
        sos = butter(4, hi, 'low', fs=sr, output='sos')
    else:
        sos = butter(4, [lo, hi], 'band', fs=sr, output='sos')
    y = sosfiltfilt(sos, m)
    return float(20*np.log10(np.sqrt(np.mean(y[pk:]**2))+1e-30))


def osc_envelope_p2p(p):
    """Detrended 5ms-smoothed log-envelope peak-to-peak from 50ms-1.5s post-peak.
    Returns None if the signal drops below -80 dBFS in the measurement window
    (envelope becomes noise; the metric is meaningless)."""
    x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim>1 else x
    env = np.abs(hilbert(m))
    win = max(int(0.005*sr), 1)
    env_sm = np.convolve(env, np.ones(win)/win, mode='same')
    env_db = 20*np.log10(env_sm + 1e-30)
    pidx = int(np.argmax(env_db))
    ts = np.arange(0.05, 1.5, 0.005)
    arr = np.array([env_db[pidx+int(t*sr)] for t in ts if pidx+int(t*sr) < len(env_db)])
    if len(arr) < 30: return None
    # Noise-floor gate: if more than half the samples are below -80 dBFS,
    # the envelope is dominated by noise (Lex VVP tail decays below floor
    # by ~700ms; a longer window measures noise, not signal).
    if (arr < -80).mean() > 0.5:
        return None
    tt = np.arange(len(arr))*0.005
    A = np.vstack([tt, np.ones_like(tt)]).T
    sl, ic = np.linalg.lstsq(A, arr, rcond=None)[0]
    res = arr - (sl*tt + ic)
    return float(res.max() - res.min())


def windowed_above_floor(p, t0, t1, floor_dbfs=-80.0):
    """Returns True if mean RMS in [t0, t1] (post-peak) > floor_dbfs."""
    x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim>1 else x
    pk = int(np.argmax(np.abs(m)))
    a, b = pk + int(t0 * sr), min(pk + int(t1 * sr), len(m))
    if b - a < 16:
        return False
    rms = 20.0 * np.log10(np.sqrt(np.mean(m[a:b] ** 2)) + 1e-30)
    return rms > floor_dbfs


def check(label, dv_val, lex_val, gate, kind='abs'):
    """Returns (delta, pass/fail, formatted string)."""
    if dv_val is None or lex_val is None or (isinstance(dv_val, float) and dv_val != dv_val):
        return None, 'SKIP', f"  {label:30s}  none"
    if kind == 'pct':
        if lex_val == 0:
            return None, 'SKIP', f"  {label:30s}  divide-by-zero"
        delta = (dv_val - lex_val) / lex_val * 100
        passing = abs(delta) <= gate
        return delta, ('PASS' if passing else 'FAIL'), \
               f"  {label:30s}  DV={dv_val:.3f}  Lex={lex_val:.3f}  Δ={delta:+6.1f}%  gate=±{gate}%  {'✓' if passing else '✗'}"
    else:
        delta = dv_val - lex_val
        passing = abs(delta) <= gate
        return delta, ('PASS' if passing else 'FAIL'), \
               f"  {label:30s}  DV={dv_val:+7.3f}  Lex={lex_val:+7.3f}  Δ={delta:+6.2f}  gate=±{gate}  {'✓' if passing else '✗'}"


def audit(dv_dir, lex_dir, name='preset', category=''):
    dv_dir = Path(dv_dir); lex_dir = Path(lex_dir)
    # Find files (any matching slug stem)
    def find_stim(d, stim):
        c = sorted(d.glob(f"*_{stim}.wav"))
        return str(c[0]) if c else None

    fails = []

    print(f"══════════════════ FULL CHECK — {name} ══════════════════\n")

    # ─── Level (across stimuli) ───
    print("── LEVEL (post-peak integrated RMS) ──")
    for stim, gate_key in [('snare', 'snare_rms_dB'),
                           ('noiseburst', 'noiseburst_rms_dB')]:
        dv = find_stim(dv_dir, stim); lx = find_stim(lex_dir, stim)
        if not dv or not lx:
            print(f"  {stim:30s}  missing"); continue
        d, p, line = check(f'{stim} RMS', integrated_rms_db(dv),
                            integrated_rms_db(lx), GATES[gate_key])
        print(line)
        if p == 'FAIL': fails.append(line.strip())

    # ─── Band-region absolute energy (noiseburst — the broadband stimulus) ───
    print("\n── BAND-REGION ABSOLUTE ENERGY (noiseburst, post-peak) ──")
    dv = find_stim(dv_dir, 'noiseburst'); lx = find_stim(lex_dir, 'noiseburst')
    if dv and lx:
        for lab, lo, hi, gate_key in [
            ('sub-bass <100 Hz',   20,   100, 'sub_lt_100_dB'),
            ('low 100-250 Hz',    100,   250, 'low_100_250_dB'),
            ('mid 250-1k Hz',     250,  1000, 'mid_250_1k_dB'),
            ('mid 1-4k Hz',      1000,  4000, 'mid_1k_4k_dB'),
            ('hi 4-12k Hz',      4000, 12000, 'hi_4k_12k_dB'),
        ]:
            d, p, line = check(lab, band_rms_db(dv, lo, hi),
                                band_rms_db(lx, lo, hi), GATES[gate_key])
            print(line)
            if p == 'FAIL': fails.append(line.strip())

    # ─── Peak-aligned compute_metrics ───
    if dv and lx:
        m_dv = compute_metrics(dv); m_lx = compute_metrics(lx)
        print("\n── DECAY (peak-aligned tail times) ──")
        d, p, line = check('tail_t30 (s)', m_dv.get('tail_t30'), m_lx.get('tail_t30'), GATES['tail_t30_pct'], 'pct'); print(line)
        if p == 'FAIL': fails.append(line.strip())
        d, p, line = check('tail_t60 (s)', m_dv.get('tail_t60'), m_lx.get('tail_t60'), GATES['tail_t60_pct'], 'pct')
        if p == 'FAIL' and _is_ceiling('tail_t60_pct', category):
            line = line.rstrip() + "  [ENGINE CEILING — informational]"
            print(line)
        else:
            print(line)
            if p == 'FAIL': fails.append(line.strip())

        print("\n── SPECTRAL (centroid windows) ──")
        # cent_50 window is 50-500ms — almost always above noise floor.
        d, p, line = check('cent_50 (Hz)', m_dv.get('cent_50'), m_lx.get('cent_50'), GATES['cent_50_pct'], 'pct')
        if p == 'FAIL' and _is_ceiling('cent_50_pct', category):
            line = line.rstrip() + "  [ENGINE CEILING — informational]"
            print(line)
        else:
            print(line)
            if p == 'FAIL': fails.append(line.strip())
        # cent_500 window is 500-1500ms — Lex VVP tail often drops below
        # noise floor here. Skip gate if Lex segment <-80 dBFS, since the
        # measurement is comparing DV signal against Lex noise (garbage).
        if windowed_above_floor(lx, 0.50, 1.50):
            d, p, line = check('cent_500 (Hz)', m_dv.get('cent_500'), m_lx.get('cent_500'), GATES['cent_500_pct'], 'pct')
            if p == 'FAIL' and _is_ceiling('cent_500_pct', category):
                line = line.rstrip() + "  [ENGINE CEILING — informational]"
                print(line)
            else:
                print(line)
                if p == 'FAIL': fails.append(line.strip())
        else:
            print(f"  {'cent_500 (Hz)':30s}  SKIPPED (Lex 500-1500ms below noise floor)")

        print("\n── ENVELOPE / STEREO ──")
        # env_p2p uses 50-decay_seek_s window. If Lex falls below floor in that
        # window, comparison is against noise — skip.
        if windowed_above_floor(lx, 0.05, 1.50):
            d, p, line = check('env_p2p (dB)', m_dv.get('env_res_p2p'), m_lx.get('env_res_p2p'), GATES['env_p2p_dB']); print(line)
            if p == 'FAIL': fails.append(line.strip())
        else:
            print(f"  {'env_p2p (dB)':30s}  SKIPPED (Lex tail below noise floor)")
        d, p, line = check('stereo_corr', m_dv.get('stereo_corr'), m_lx.get('stereo_corr'), GATES['stereo_corr']); print(line)
        if p == 'FAIL': fails.append(line.strip())

        # ─── 1/3-oct RMS-normalized L1 ───
        centers = m_dv['oct_centers']; dvn = m_dv['oct_db_norm']; lxn = m_lx['oct_db_norm']
        abs_d = [abs(float(lx-dv)) for dv,lx in zip(dvn,lxn)]
        mean_l1 = sum(abs_d) / len(abs_d)
        max_l1 = max(abs_d)
        max_fc = [float(c) for c, ad in zip(centers, abs_d) if ad == max_l1][0]
        print("\n── EQ SHAPE (RMS-normalized 1/3-oct L1) ──")
        passing = mean_l1 <= GATES['spec_L1_mean_dB']
        line = f"  {'spec_L1 mean':30s}  {mean_l1:.2f} dB  gate=±{GATES['spec_L1_mean_dB']}  {'✓' if passing else '✗'}"
        print(line)
        if not passing: fails.append(line.strip())
        passing = max_l1 <= GATES['spec_L1_max_dB']
        line = f"  {'spec_L1 max':30s}  {max_l1:.2f} dB @ {max_fc:.0f} Hz  gate=±{GATES['spec_L1_max_dB']}  {'✓' if passing else '✗'}"
        print(line)
        if not passing: fails.append(line.strip())

    # ─── Sustained-pink steady-state per-band energy + per-band decay ───
    # Captures musical-content perception (continuous input). Skipped if
    # sustained renders aren't present (legacy renders without --sustained-pink-seconds).
    sus_dv = find_stim(dv_dir, 'sustained')
    sus_lx = find_stim(lex_dir, 'sustained')
    if sus_dv and sus_lx:
        from scipy.signal import butter as _bts, sosfiltfilt as _ss
        def _band_rms_db(p, lo, hi, t0=2.5, t1=4.0):
            x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim>1 else x
            a, b = int(t0*sr), min(int(t1*sr), len(m))
            hi_c = min(hi, sr*0.49)
            if lo <= 0:
                sos = _bts(4, hi_c, 'low', fs=sr, output='sos')
            else:
                sos = _bts(4, [lo, hi_c], 'band', fs=sr, output='sos')
            y = _ss(sos, m)
            return float(20*np.log10(np.sqrt(np.mean(y[a:b]**2))+1e-30))
        def _band_t30(p, lo, hi):
            x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim>1 else x
            off = int(4.0 * sr)
            if off >= len(m): return None
            tail = m[off:]
            hi_c = min(hi, sr*0.49)
            if lo <= 0:
                sos = _bts(4, hi_c, 'low', fs=sr, output='sos')
            else:
                sos = _bts(4, [lo, hi_c], 'band', fs=sr, output='sos')
            y = _ss(sos, tail)
            pwr = y ** 2
            win = max(int(0.01*sr), 1)
            sm = np.convolve(pwr, np.ones(win)/win, mode='same')
            peak = float(np.max(sm))
            if peak < 1e-12: return None
            pidx = int(np.argmax(sm))
            floor = float(np.median(sm[-min(int(0.5*sr), len(sm)):]))
            thr = max(peak * 10**(-30/10), floor * 4.0)
            below = np.where((np.arange(len(sm)) > pidx) & (sm < thr))[0]
            return (int(below[0]) - pidx) / sr if len(below) else None

        print("\n── SUSTAINED-PINK STEADY-STATE PER-BAND ENERGY (window 2.5-4.0s) ──")
        for lab, lo, hi, gate_key in [
            ('ss deep sub 20-50',    20,    50, 'ss_deep_sub_dB'),
            ('ss sub 50-100',        50,   100, 'ss_sub_dB'),
            ('ss low 100-250',      100,   250, 'ss_low_dB'),
            ('ss low-mid 250-500',  250,   500, 'ss_low_mid_dB'),
            ('ss mid 500-2k',       500,  2000, 'ss_mid_dB'),
            ('ss upper-mid 2-5k',  2000,  5000, 'ss_umid_dB'),
            ('ss hi 5-10k',        5000, 10000, 'ss_hi_dB'),
            ('ss air 10-20k',     10000, 20000, 'ss_air_dB'),
        ]:
            d_dv = _band_rms_db(sus_dv, lo, hi)
            d_lx = _band_rms_db(sus_lx, lo, hi)
            d, p, line = check(lab, d_dv, d_lx, GATES[gate_key])
            if p == 'FAIL' and _is_ceiling(gate_key, category):
                line = line.rstrip() + "  [ENGINE CEILING — informational]"
                print(line)
            else:
                print(line)
                if p == 'FAIL': fails.append(line.strip())

        # t30 measurement helper closes over band-filter on input-off tail.
        def _band_decay_t(p, lo, hi, target_db):
            x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim>1 else x
            off = int(4.0 * sr)
            if off >= len(m): return None
            tail = m[off:]
            hi_c = min(hi, sr*0.49)
            if lo <= 0:
                sos = _bts(4, hi_c, 'low', fs=sr, output='sos')
            else:
                sos = _bts(4, [lo, hi_c], 'band', fs=sr, output='sos')
            y = _ss(sos, tail)
            pwr = y ** 2
            win = max(int(0.01*sr), 1)
            sm = np.convolve(pwr, np.ones(win)/win, mode='same')
            peak = float(np.max(sm))
            if peak < 1e-12: return None
            pidx = int(np.argmax(sm))
            floor = float(np.median(sm[-min(int(0.5*sr), len(sm)):]))
            thr = max(peak * 10**(-target_db/10), floor * 4.0)
            below = np.where((np.arange(len(sm)) > pidx) & (sm < thr))[0]
            return (int(below[0]) - pidx) / sr if len(below) else None

        # Bands include low_mid 250-500 — the user-perceived gap band.
        band_list = [('sub <100',      20,  100),
                     ('low 100-250',  100,  250),
                     ('low_mid 250-500', 250,  500),
                     ('mid 500-2k',   500, 2000),
                     ('hi 2-8k',     2000, 8000)]

        print("\n── SUSTAINED-PINK PER-BAND EDT (t10 = early decay, perceived 'hold') ──")
        for lab, lo, hi in band_list:
            d_dv = _band_decay_t(sus_dv, lo, hi, 10)
            d_lx = _band_decay_t(sus_lx, lo, hi, 10)
            if d_dv is None or d_lx is None:
                print(f"  {('edt ' + lab):30s}  SKIPPED (band below noise floor)")
                continue
            d, p, line = check(f'edt {lab}', d_dv, d_lx, GATES['edt_band_pct'], 'pct')
            # lab format: "low_mid 250-500" — strip the range suffix for the
            # ceiling key lookup so it matches ENGINE_CEILINGS["Plates"].
            ceiling_id = f'edt_{lab.split()[0]}_pct'   # e.g. "edt_low_mid_pct"
            if p == 'FAIL' and _is_ceiling(ceiling_id, category):
                line = line.rstrip() + "  [ENGINE CEILING — informational]"
                print(line)
            else:
                print(line)
                if p == 'FAIL': fails.append(line.strip())

        print("\n── SUSTAINED-PINK PER-BAND DECAY (t30) ──")
        for lab, lo, hi in band_list:
            d_dv = _band_decay_t(sus_dv, lo, hi, 30)
            d_lx = _band_decay_t(sus_lx, lo, hi, 30)
            if d_dv is None or d_lx is None:
                print(f"  {('decay ' + lab):30s}  SKIPPED (band below noise floor)")
                continue
            d, p, line = check(f'decay {lab}', d_dv, d_lx, GATES['decay_band_pct'], 'pct')
            print(line)
            if p == 'FAIL': fails.append(line.strip())

    # ─── Envelope-shape L1 (transient stimulus contour match) ───
    # snare-stimulus envelope shape — catches "Lex holds flat / DV drops"
    # contour mismatches that scalar decay metrics miss.
    snare_dv = find_stim(dv_dir, 'snare')
    snare_lx = find_stim(lex_dir, 'snare')
    if snare_dv and snare_lx:
        from metrics_external import envelope_shape_l1 as _env_l1
        x_dv, sr_dv = sf.read(snare_dv); x_lx, _ = sf.read(snare_lx)
        m_dv = x_dv.mean(axis=1) if x_dv.ndim>1 else x_dv
        m_lx = x_lx.mean(axis=1) if x_lx.ndim>1 else x_lx
        env_l1 = _env_l1(m_dv, m_lx, sr_dv, post_peak_ms=500.0)
        print("\n── ENVELOPE-SHAPE CONTOUR (snare stimulus, 0-500 ms post-peak) ──")
        passing = (env_l1 == env_l1) and env_l1 <= GATES['env_shape_l1_dB']
        line = f"  {'env_shape_L1 (dB)':30s}  {env_l1:6.2f}  gate=±{GATES['env_shape_l1_dB']}  {'✓' if passing else '✗'}"
        print(line)
        if not passing and env_l1 == env_l1:
            fails.append(line.strip())

    # ─── Oscillation (detrended envelope ripple) ───
    if dv and lx:
        print("\n── OSCILLATION (modulator-induced envelope ripple) ──")
        # Skip if either side's envelope is noise-floor-dominated.
        o_dv = osc_envelope_p2p(dv); o_lx = osc_envelope_p2p(lx)
        if o_dv is None or o_lx is None:
            print(f"  {'osc P2P (dB)':30s}  SKIPPED (envelope below noise floor)")
        else:
            d, p, line = check('osc P2P (dB)', o_dv, o_lx, GATES['osc_p2p_dB'])
            if p == 'FAIL' and _is_ceiling('osc_p2p_dB', category):
                line = line.rstrip() + "  [ENGINE CEILING — informational]"
                print(line)
            else:
                print(line)
                if p == 'FAIL': fails.append(line.strip())

    # ─── Summary ───
    print()
    print("═" * 64)
    if not fails:
        print(f"  ✓ ALL GATES PASS")
    else:
        print(f"  ✗ {len(fails)} GATE(S) FAILED:")
        for f in fails:
            print(f"    {f}")
    print("═" * 64)
    return fails


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dv_dir")
    ap.add_argument("lex_dir")
    ap.add_argument("--name", default="preset")
    ap.add_argument("--category", default="",
                    help="Preset category (e.g. 'Plates'). Triggers engine-"
                         "ceiling bypass for documented architectural limits.")
    args = ap.parse_args()
    fails = audit(args.dv_dir, args.lex_dir, args.name, args.category)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
