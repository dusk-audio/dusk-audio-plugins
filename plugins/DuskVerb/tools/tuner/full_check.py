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
import argparse, json, sys
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
    # stereo_corr TIGHTENED 2026-06-02 from ±0.30 → ±0.05. The loose gate passed
    # an audibly-narrower DV (DV +0.19 vs VVV +0.04, Δ0.15 < 0.30). Width is a
    # primary perceptual cue; the per-band spatial_width_band gate below adds the
    # L/R-decorrelation detail this broadband number averages away. (DPV's
    # mono-compat width clamp may now fail this — that is the intended exposure,
    # not a regression: track it as an engine ceiling if a sweep can't close it.)
    'stereo_corr':          0.05,
    # ── ONSET / SPATIAL / DIFFUSION (2026-06-02 perceptual gates, impulse) ──
    'attack_time_ms_abs':   5.0,    # onset→peak buildup time, absolute ms ...
    'attack_time_pct':     10.0,    # ... OR within ±10% (pass if EITHER holds)
    'onset_slope_pct':     30.0,    # rising-swell slope dB/ms (noisy → wider gate)
    'spatial_width_band':   0.06,   # per-band L/R corr abs-diff (low/mid/high)
    'diffusion_flux':       1.50,   # kurtosis-trajectory L1 over first 150 ms
    # ── ADVISORY (2026-06-02): printed + ✓/✗ shown, NOT counted in n_fail until
    # ear-validated. Thresholds calibrated from anchor-vs-anchor (≈0) + observed
    # DV-vs-anchor spread; |Δ| vs anchor unless noted. ──
    'spectral_flux_variance': 0.15,  # |Δ| late-tail flux-var (modal ring/boing)
    'decay_curvature_r1':     0.08,  # |Δ| EDT/T30 ratio (early-decay curvature)
    'decay_curvature_r2':     0.08,  # |Δ| T30/T60 ratio (late-decay curvature)
    'bark_masking_l1':        1.50,  # mean-L1 Bark masked-loudness trajectory
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
    # Late-window low-band integrated RMS — catches "boomy" tail (DV bass
    # rings 0.5-2 s past where Lex's structural damping has attenuated it).
    # spec_L1 averaging is steady-state-spectrum-only and can pass while
    # the temporal bass envelope is wrong. Verdict 2026-05-29 listening:
    # VH v14 measured +3.17 dB DV-hot in 500ms-1s × 40-100 Hz — audible boom.
    'boom_late_low_dB':     2.5,
    # Per-band tail-envelope ripple std (Hilbert + detrend). Broadband
    # osc_p2p averaged out VH v14's mid 1-4 kHz wobble: DV ripple std
    # 4.15 dB vs Lex 0.95 dB (+3.2 hot). Per-band catches per-band wobble.
    'tail_mod_ripple_dB':   1.5,
    # HF BLOOM — early-window (50-300 ms post-peak) per-band hot ceiling.
    # VH v15 audition (2026-05-29): 4-8 kHz +1.6 dB DV-hot during bloom =
    # audible "brighter than VVV". Asymmetric gate (DV-hot only).
    'hf_bloom_hot_dB':      1.5,
    # BODY SUSTAIN — mid-window (300-800 ms post-peak) per-band cold ceiling.
    # VH v15 audition: 100-2k Hz mean -1.4 dB DV-cold during body window =
    # audible "VVV fuller / DV thinner". Asymmetric gate (DV-cold only).
    'body_sustain_cold_dB': 1.5,
    # PER-BAND RT60 (Schroeder backward integration on noiseburst tail, fit
    # slope between -5 and -25 dB → ×3 = T60). 9 octave bands 63 Hz–16 kHz.
    # ±5% = musical JND for reverberation time per ITU-R BS.1116 and classical
    # hall acoustic literature. Missing this gate was a multi-week blind spot:
    # the per-band sustained-pink gates above only fire when the harness
    # renders a sustained-pink stimulus (it doesn't), so per-band RT60 shape
    # was effectively UNGATED across every VH iteration since v13.
    't60_band_pct':         5.0,
    # PER-BAND TAIL MOD PEAK FREQUENCY — Hilbert envelope FFT, dominant peak
    # in 0.3-8 Hz range per band. VH listening v15: DV bass mod at 3.3 Hz vs
    # VVV 1.83 Hz, DV mid at 1.5 Hz vs VVV 0.46 Hz — DV mod runs at WRONG
    # rates per band (per-line LFO harmonic stack vs tank-coupled mod). Gate
    # ±30% relative — narrow enough to flag rate inversion, wide enough to
    # tolerate noise-floor jitter.
    'tail_mod_freq_pct':   30.0,   # DEPRECATED: per-band envelope-AM rate. Rewarded
                                   # a tremolo pump (mono-sum hid it). Replaced by
                                   # tail_pitch_chorus_pct (pitch, not amplitude).
    'tail_pitch_chorus_pct': 30.0, # smooth-IF pitch-drift std vs anchor (sine stimulus)
    # SINE 1 kHz STEADY-STATE FULL RMS DELTA — exposes mid-presence
    # coloration that broadband noiseburst RMS averages out. VH listening
    # v21 (gain-matched): DV +6.15 dB hot on sine1k (1 kHz feedback gain too
    # high) — heard as "DV brighter / more honky" on vocal material.
    # Asymmetric tolerance because hot is the audible defect.
    'sine1k_full_rms_dB':   2.0,
    # PER-STIMULUS RMS DELTA — broadband level on each stimulus. Single
    # Gain Trim knob can't match all stimuli simultaneously when the
    # spectrum diverges, so this is informational + advisory. Gate as
    # symmetric tolerance per stimulus.
    'per_stim_rms_dB':      2.0,
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


def _late_low_rms_db(p, t0, t1, lo, hi):
    """Peak-aligned late-window low-band integrated RMS (dB)."""
    import soundfile as sf
    x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim > 1 else x
    peak = int(np.argmax(np.abs(m)))
    i0 = max(0, peak + int(t0 * sr))
    i1 = min(len(m), peak + int(t1 * sr))
    if i1 - i0 < 200: return None
    seg = m[i0:i1]
    hi_c = min(hi, sr * 0.49)
    sos = butter(4, [max(lo, 10.0), hi_c], 'band', fs=sr, output='sos')
    y = sosfiltfilt(sos, seg)
    rms = float(np.sqrt(np.mean(y ** 2)))
    return 20.0 * np.log10(max(rms, 1e-12))


def _post_peak_band_rms_db(p, t0_ms, t1_ms, lo, hi):
    """Peak-aligned band-passed integrated RMS in a [t0, t1] ms window."""
    import soundfile as sf
    x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim > 1 else x
    peak = int(np.argmax(np.abs(m)))
    i0 = max(0, peak + int(t0_ms * sr / 1000))
    i1 = min(len(m), peak + int(t1_ms * sr / 1000))
    if i1 - i0 < 200: return None
    seg = m[i0:i1]
    hi_c = min(hi, sr * 0.49)
    sos = butter(4, [max(lo, 10.0), hi_c], 'band', fs=sr, output='sos')
    y = sosfiltfilt(sos, seg)
    rms = float(np.sqrt(np.mean(y ** 2)))
    return 20.0 * np.log10(max(rms, 1e-12))


def _tail_mod_peak_freq (p, t0, t1, lo, hi, env_smooth_ms=30,
                          f_lo=0.3, f_hi=8.0):
    """Find dominant envelope-modulation peak frequency in [f_lo, f_hi] Hz
    for a band-passed tail segment. Hilbert envelope → linear detrend →
    Hanning window → rFFT → arg max within target range. Returns Hz or None."""
    import soundfile as sf
    x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim > 1 else x
    peak = int(np.argmax(np.abs(m)))
    i0 = max(0, peak + int(t0 * sr))
    i1 = min(len(m), peak + int(t1 * sr))
    if i1 - i0 < int(sr * 0.5):
        return None
    seg = m[i0:i1]
    hi_c = min(hi, sr * 0.49)
    sos = butter(4, [max(lo, 10.0), hi_c], 'band', fs=sr, output='sos')
    y = sosfiltfilt(sos, seg)
    env = np.abs(hilbert(y))
    win = max(1, int(sr * env_smooth_ms / 1000.0))
    env_s = np.convolve(env, np.ones(win) / win, mode='same')
    env_db = 20.0 * np.log10(np.maximum(env_s, 1.0e-12))
    t = np.arange(len(env_db)) / sr
    slope, intercept = np.polyfit(t, env_db, 1)
    env_ac = env_db - (slope * t + intercept)
    if len(env_ac) < 64:
        return None
    n = len(env_ac)
    nfft = 1 << int(np.ceil(np.log2(n * 4)))
    spec = np.abs(np.fft.rfft(env_ac * np.hanning(n), n=nfft))
    f = np.fft.rfftfreq(nfft, d=1 / sr)
    mask = (f >= f_lo) & (f <= f_hi)
    if not np.any(mask):
        return None
    idx = int(np.argmax(spec[mask]))
    return float(f[mask][idx])


def _true_pitch_chorus_hz (p, t0=0.8, t1=1.9, fc=1000.0, bw=600.0):
    """Pitch-chorus depth of the tail (Hz), from a steady sine-Nk stimulus.
    Lush hall tails come from PITCH/delay modulation (smooth chorus/detune),
    NOT amplitude modulation. An envelope/AM detector can't tell a smooth
    chorus from a tremolo pump, so it rewarded a catastrophic 16-tap AM VCA.
    This instead tracks the carrier's INSTANTANEOUS FREQUENCY and low-pass
    filters it (<12 Hz) to isolate the slow LFO pitch-drift while discarding
    the high-freq modal-beating of dense colliding taps. Returns the std (Hz)
    of that smooth IF drift — higher = more chorus. (A deep AM pump also
    inflates this via amplitude-null phase instability, so it correctly
    penalises the tremolo too.)"""
    import soundfile as sf
    x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim > 1 else x
    i0, i1 = int(t0 * sr), min(int(t1 * sr), len(m))
    if i1 - i0 < int(sr * 0.4):
        return None
    seg = m[i0:i1]
    y = sosfiltfilt(butter(4, [fc - bw, fc + bw], 'band', fs=sr, output='sos'), seg)
    a = hilbert(y); env = np.abs(a)
    inst = np.diff(np.unwrap(np.angle(a))) / (2.0 * np.pi) * sr
    w = env[1:]; thr = np.median(w) * 0.3
    inst = inst[w > thr]
    inst = inst[(inst > fc - bw) & (inst < fc + bw)]
    if len(inst) < 100:
        return None
    smooth = sosfiltfilt(butter(2, 12.0, 'low', fs=sr, output='sos'), inst - fc)
    return float(np.std(smooth))


def _full_rms_db (p):
    """Integrated RMS dB of entire file (mono mix)."""
    import soundfile as sf
    x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim > 1 else x
    return float(20.0 * np.log10(max(np.sqrt(np.mean(m ** 2)), 1.0e-12)))


def _t60_band_schroeder (p, lo, hi):
    """Per-band T60 via Schroeder backward integration on the post-peak
    noiseburst tail. Bandpass → cumulative reverse energy → log-scale slope
    fit between -5 and -25 dB → T60 = -60 / slope. Returns seconds or None
    if the band's decay never crosses -25 dB (noise floor) in the 4 s window."""
    import soundfile as sf
    x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim > 1 else x
    nyq = sr * 0.49
    sos = butter(4, [max(lo, 10.0), min(hi, nyq)], 'band', fs=sr, output='sos')
    y = sosfiltfilt(sos, m)
    peak = int(np.argmax(np.abs(y)))
    tail = y[peak : peak + int(sr * 4.0)]
    if len(tail) < int(sr * 0.5):
        return None
    sq  = tail ** 2
    edc = np.cumsum(sq[::-1])[::-1]
    if edc[0] <= 1.0e-30:
        return None
    edc_db = 10.0 * np.log10 (np.maximum (edc / edc[0], 1.0e-12))
    t = np.arange(len(edc_db)) / sr
    try:
        i5  = int (np.where (edc_db <= -5.0) [0][0])
        i25 = int (np.where (edc_db <= -25.0)[0][0])
    except IndexError:
        return None
    if i25 <= i5:
        return None
    slope = np.polyfit (t[i5:i25], edc_db[i5:i25], 1)[0]   # dB/sec
    if slope >= -1.0e-3:
        return None
    return -60.0 / slope


def _tail_env_ripple_db(p, t0, t1, lo, hi, env_smooth_ms=30):
    """Detrended dB-envelope std on the tail post-peak (Hilbert + linear-
    detrend). Lex natural decay ~0.9-1.3 dB; engine mod wobble inflates it."""
    import soundfile as sf
    x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim > 1 else x
    peak = int(np.argmax(np.abs(m)))
    i0 = max(0, peak + int(t0 * sr))
    i1 = min(len(m), peak + int(t1 * sr))
    if i1 - i0 < int(sr * 0.5): return None
    seg = m[i0:i1]
    hi_c = min(hi, sr * 0.49)
    sos = butter(4, [max(lo, 10.0), hi_c], 'band', fs=sr, output='sos')
    y = sosfiltfilt(sos, seg)
    env = np.abs(hilbert(y))
    win = max(1, int(sr * env_smooth_ms / 1000.0))
    env_s = np.convolve(env, np.ones(win) / win, mode='same')
    env_db = 20.0 * np.log10(np.maximum(env_s, 1e-12))
    t = np.arange(len(env_db)) / sr
    slope, intercept = np.polyfit(t, env_db, 1)
    env_db_ac = env_db - (slope * t + intercept)
    return float(np.std(env_db_ac))


def windowed_above_floor(p, t0, t1, floor_dbfs=-80.0):
    """Returns True if mean RMS in [t0, t1] (post-peak) > floor_dbfs."""
    x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim>1 else x
    pk = int(np.argmax(np.abs(m)))
    a, b = pk + int(t0 * sr), min(pk + int(t1 * sr), len(m))
    if b - a < 16:
        return False
    rms = 20.0 * np.log10(np.sqrt(np.mean(m[a:b] ** 2)) + 1e-30)
    return rms > floor_dbfs


# ─────────────── PERCEPTUAL ONSET / SPATIAL / DIFFUSION (2026-06-02) ───────────
# Listening tests exposed two blind spots the optimizer was gaming:
#   1. ATTACK/BUILDUP. env_shape_l1 peak-ALIGNS before comparing, so it deletes
#      onset-to-peak time. The VVV anchor blooms in ~24 ms; DV's FDN halls take
#      70-172 ms — the single most salient "sounds like Lexicon, not VVV" cue,
#      and totally ungated until now.
#   2. STEREO WIDTH. The old stereo_corr ±0.30 gate passed an audibly narrower DV
#      (DV +0.19 vs VVV +0.04). Tightened to ±0.05 + a per-band L/R correlation.
# Plus echo-density (diffusion flux) so grainy/fluttery tails can't pass as smooth.
# These run on the IMPULSE response — the only stimulus that isolates pure reverb
# buildup (the snare/noiseburst envelope peak is confounded by the stimulus's own
# body envelope: on the snare VVV actually peaks LATER than DV, on the impulse it
# peaks far earlier — the impulse is the truth).

def _onset_index(env_db, pk, drop_db=40.0):
    """Stimulus onset = FIRST sample whose envelope crosses within drop_db of the
    peak (scanning forward from t0). This is the arrival of the response; the
    span onset→peak is then the full buildup/swell the ear hears as 'attack'.
    (A backward 'last dip before peak' definition collapses to the final few ms
    of approach and hides the gross VVV-24ms vs DV-70ms buildup difference.)"""
    thr = env_db[pk] - drop_db
    post = np.where(env_db[:pk + 1] >= thr)[0]
    return int(post[0]) if len(post) else 0


def attack_profile(p, drop_db=40.0):
    """(attack_time_ms, onset_slope_dB_per_ms) of the 2 ms-smoothed Hilbert
    energy envelope, measured onset->peak. attack_time = reverb buildup speed;
    onset_slope = linear-regression rise (dB/ms) capturing the swell character."""
    x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim > 1 else x
    env = np.abs(hilbert(m))
    win = max(int(0.002 * sr), 1)
    env = np.convolve(env, np.ones(win) / win, mode='same')
    env_db = 20.0 * np.log10(env + 1e-30)
    pk = int(np.argmax(env_db))
    on = _onset_index(env_db, pk, drop_db)
    attack_ms = (pk - on) / sr * 1000.0
    if pk - on < 4:
        return float(attack_ms), None
    seg = env_db[on:pk + 1]
    t_ms = np.arange(len(seg)) / sr * 1000.0
    A = np.vstack([t_ms, np.ones_like(t_ms)]).T
    slope = float(np.linalg.lstsq(A, seg, rcond=None)[0][0])
    return float(attack_ms), slope


def spatial_width_bands(p, t_ms=500.0):
    """Per-band L/R Pearson correlation over the first t_ms post-onset. 3 bands
    via 4th-order (LR4-equivalent, zero-phase) crossovers at 300 Hz / 5 kHz.
    Lower r = wider / more decorrelated. Returns (low, mid, high); None where a
    band is silent or the file is mono."""
    x, sr = sf.read(p)
    if x.ndim < 2:
        return (None, None, None)
    L = x[:, 0]; R = x[:, 1]
    env = np.abs(hilbert((L + R) * 0.5))
    win = max(int(0.002 * sr), 1)
    env_db = 20.0 * np.log10(np.convolve(env, np.ones(win) / win, 'same') + 1e-30)
    pk = int(np.argmax(env_db)); on = _onset_index(env_db, pk)
    i1 = min(len(L), on + int(t_ms / 1000.0 * sr))
    if i1 - on < int(0.05 * sr):
        return (None, None, None)
    nyq = sr * 0.49
    bands = [(None, 300.0), (300.0, 5000.0), (5000.0, None)]
    out = []
    for lo, hi in bands:
        if lo is None:
            sos = butter(4, min(hi, nyq), 'low', fs=sr, output='sos')
        elif hi is None:
            sos = butter(4, lo, 'high', fs=sr, output='sos')
        else:
            sos = butter(4, [lo, min(hi, nyq)], 'band', fs=sr, output='sos')
        lb = sosfiltfilt(sos, L)[on:i1]; rb = sosfiltfilt(sos, R)[on:i1]
        if np.std(lb) < 1e-9 or np.std(rb) < 1e-9:
            out.append(None)
        else:
            out.append(float(np.corrcoef(lb, rb)[0, 1]))
    return tuple(out)


def diffusion_flux_curve(p, span_ms=150.0, win_ms=10.0):
    """Sliding excess-kurtosis (Pearson, Gaussian = 3.0) of the waveform over a
    10 ms moving window across the first span_ms post-onset. A dense, well-
    diffused field relaxes toward 3.0; a sparse/fluttery one stays spiky. Returns
    the per-hop kurtosis curve (compared trajectory-vs-trajectory against the
    anchor)."""
    from scipy.stats import kurtosis
    x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim > 1 else x
    env = np.abs(hilbert(m))
    win = max(int(0.002 * sr), 1)
    env_db = 20.0 * np.log10(np.convolve(env, np.ones(win) / win, 'same') + 1e-30)
    pk = int(np.argmax(env_db)); on = _onset_index(env_db, pk)
    w = max(int(win_ms / 1000.0 * sr), 8); hop = max(w // 2, 1)
    end = min(len(m), on + int(span_ms / 1000.0 * sr))
    curve = []
    for s in range(on, end - w, hop):
        curve.append(float(kurtosis(m[s:s + w], fisher=False)))   # 3.0 = Gaussian
    return np.array(curve)


# ─── ADVISORY perceptual metrics (2026-06-02) — modal ring / decay curvature /
# Bark masking. Corrected from the provided drafts before integration:
#   • r2 (T30/T60) is REAL — the draft hardcoded `return r1, 1.0` (always-pass).
#   • Schroeder backward-integrated decay — raw-envelope dB crossings are noise
#     (same reason the existing T60 gate uses EDC, not instantaneous crossings).
#   • Flux window is FLOOR-GUARDED — 1.5-3 s is dither on short tails; STFT-var
#     of −90 dBFS noise is the silence-gate bug, already burned us once.
#   • Bark masking spread sign fixed — the draft's +25 dB/bark lower slope is a
#     316× GAIN away from the masker; masking ATTENUATES with bark distance.
#   • Bark made a true per-window TRAJECTORY (the draft took one FFT of the
#     whole segment despite the name).
# Wired ADVISORY: printed + pass/fail shown, but NOT counted in n_fail until a
# listening test confirms each tracks an audible defect (the bar attack/width
# cleared). All compared DV-vs-anchor.

def adv_spectral_flux_var(p, t0=0.5, t1=2.5):
    """Late-tail STFT spectral-flux variance — modal-ring / 'boing' detector.
    Post-peak window, FLOOR-GUARDED (None if < −80 dBFS = decayed to dither).
    Per-frame energy-normalized → measures spectral-SHAPE motion, not level.
    Low var = static metallic ring lockup; high = choppy modal beating."""
    from scipy.signal import stft
    x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim > 1 else x
    pk = int(np.argmax(np.abs(m)))
    a, b = pk + int(t0 * sr), min(pk + int(t1 * sr), len(m))
    if b - a < int(0.3 * sr): return None
    if 20.0 * np.log10(np.sqrt(np.mean(m[a:b] ** 2)) + 1e-30) < -80.0: return None
    nper = max(int(0.04 * sr), 64)
    _, _, Z = stft(m[a:b], fs=sr, nperseg=nper, noverlap=int(nper * 0.75))
    mag = np.abs(Z)
    if mag.shape[1] < 4: return None
    magn = mag / (np.sum(mag, axis=0, keepdims=True) + 1e-9)
    flux = np.sum(np.diff(magn, axis=1) ** 2, axis=0)
    return float(np.var(flux) * 1e4)


def adv_decay_curvature(p, lo=710.0, hi=1420.0):
    """(r1, r2) log-decay curvature ratios from the Schroeder EDC of a ~1 kHz
    band. r1 = EDT·6 / T30·2 ; r2 = T30·2 / T60. Exponential (linear-dB) decay
    → 1.0; deviation = sagging / hooked non-linear tail. r2 is REAL (draft
    stubbed it). None entries where a span can't be measured."""
    x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim > 1 else x
    sos = butter(4, [lo, min(hi, sr * 0.49)], 'band', fs=sr, output='sos')
    y = sosfiltfilt(sos, m)
    pk = int(np.argmax(np.abs(y)))
    tail = y[pk: pk + int(sr * 6.0)]
    if len(tail) < int(sr * 0.5): return (None, None)
    edc = np.cumsum((tail ** 2)[::-1])[::-1]
    if edc[0] <= 1e-30: return (None, None)
    db = 10.0 * np.log10(np.maximum(edc / edc[0], 1e-12))
    t = np.arange(len(db)) / sr
    def cross(d):
        i = np.where(db <= d)[0]
        return float(t[i[0]]) if len(i) else None
    e0, e1 = cross(-1.0), cross(-11.0)     # EDT span (10 dB)
    c5, t1 = cross(-5.0), cross(-35.0)     # T30 span (30 dB)
    s65 = cross(-65.0)                      # T60 endpoint
    r1 = r2 = None
    if None not in (e0, e1, c5, t1) and (t1 - c5) > 0:
        edt = (e1 - e0) * 6.0; t30 = (t1 - c5) * 2.0
        if t30 > 0:
            r1 = edt / t30
            if s65 is not None and (s65 - c5) > 0:
                t60 = (s65 - c5)            # −5..−65 = 60 dB already
                if t60 > 0: r2 = t30 / t60
    return (r1, r2)


def adv_bark_masking_traj(p, t0=0.2, t1=1.5, win_ms=50.0):
    """Per-window 24-Bark masked-loudness trajectory. Each window: power → 24
    Bark bands → simultaneous-masking spread (upper −10 dB/bark, lower −25 dB/
    bark — BOTH attenuate away from the masker; the draft's +25 was a 316× gain
    sign error) → sone (^0.23) → shape-normalized. Flattened; compared DV-vs-
    anchor by mean-L1. FLOOR-GUARDED. Returns vector or None."""
    x, sr = sf.read(p); m = x.mean(axis=1) if x.ndim > 1 else x
    pk = int(np.argmax(np.abs(m)))
    a, b = pk + int(t0 * sr), min(pk + int(t1 * sr), len(m))
    if b - a < int(0.1 * sr): return None
    if 20.0 * np.log10(np.sqrt(np.mean(m[a:b] ** 2)) + 1e-30) < -80.0: return None
    w = max(int(win_ms / 1000.0 * sr), 64); hop = w // 2
    spread = np.empty((24, 24))
    for bi in range(24):
        for mi in range(24):
            dx = bi - mi
            sl = -25.0 if dx < 0 else -10.0
            spread[bi, mi] = 10.0 ** ((sl * abs(dx)) / 10.0)
    traj = []
    for s in range(a, b - w, hop):
        seg = m[s:s + w] * np.hanning(w)
        mag = np.abs(np.fft.rfft(seg)); fr = np.fft.rfftfreq(w, 1.0 / sr)
        bk = 13.0 * np.arctan(0.00076 * fr) + 3.5 * np.arctan((fr / 7500.0) ** 2)
        be = np.zeros(24)
        for bb in range(24):
            mm = (bk >= bb) & (bk < bb + 1)
            if mm.any(): be[bb] = np.sum(mag[mm] ** 2)
        sone = np.maximum(spread @ be, 1e-12) ** 0.23
        sone /= (np.sum(sone) + 1e-12)
        traj.append(sone)
    if len(traj) < 2: return None
    return np.array(traj).ravel()


def check(label, dv_val, lex_val, gate, kind='abs'):
    """Returns (delta, pass/fail, formatted string)."""
    if dv_val is None or lex_val is None:
        return None, 'SKIP', f"  {label:30s}  none"
    # Treat any non-finite value (NaN or ±Inf, incl. numpy scalars) as SKIP —
    # the old `dv_val != dv_val` test only caught NaN on dv_val and let an Inf
    # metric fall through to a spurious FAIL.
    if not (np.isfinite(dv_val) and np.isfinite(lex_val)):
        return None, 'SKIP', f"  {label:30s}  non-finite"
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


def audit(dv_dir, lex_dir, name='preset', category='', sustained_pink_seconds=4.0):
    dv_dir = Path(dv_dir); lex_dir = Path(lex_dir)
    # Find files (any matching slug stem)
    def find_stim(d, stim):
        c = sorted(d.glob(f"*_{stim}.wav"))
        # Fail loud on ambiguity rather than silently picking the first match —
        # a shared folder with multiple anchors/renders would otherwise score
        # the wrong file. Callers must keep one preset's renders per directory.
        if len(c) > 1:
            raise SystemExit(f"find_stim: {len(c)} '*_{stim}.wav' matches in {d} "
                             f"(ambiguous): {[x.name for x in c]}")
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

        # ─── ONSET / SPATIAL / DIFFUSION (impulse buildup) ───
        # These run on the IMPULSE — the only stimulus that isolates pure reverb
        # buildup. See the helper-block header for why snare/noiseburst can't.
        imp_dv = find_stim(dv_dir, 'impulse'); imp_lx = find_stim(lex_dir, 'impulse')
        if imp_dv and imp_lx:
            print("\n── ONSET / SPATIAL / DIFFUSION (impulse) ──")
            # Attack/buildup time — onset→peak. Pass if within ±5 ms OR ±10%.
            a_dv, s_dv = attack_profile(imp_dv); a_lx, s_lx = attack_profile(imp_lx)
            if a_dv is not None and a_lx is not None:
                dms = a_dv - a_lx
                passing = (abs(dms) <= GATES['attack_time_ms_abs']) or \
                          (a_lx > 0 and abs(dms) / a_lx * 100.0 <= GATES['attack_time_pct'])
                line = (f"  {'attack_time (ms)':30s}  DV={a_dv:7.1f}  Lex={a_lx:7.1f}  "
                        f"Δ={dms:+6.1f}ms  gate=±{GATES['attack_time_ms_abs']}ms/"
                        f"±{GATES['attack_time_pct']}%  {'✓' if passing else '✗'}")
                print(line)
                if not passing: fails.append(line.strip())
            # Onset slope (swell character) dB/ms.
            if s_dv is not None and s_lx is not None:
                d, p, line = check('onset_slope (dB/ms)', s_dv, s_lx, GATES['onset_slope_pct'], 'pct')
                print(line)
                if p == 'FAIL': fails.append(line.strip())
            # Per-band stereo width (L/R corr over 0-500 ms post-onset).
            w_dv = spatial_width_bands(imp_dv); w_lx = spatial_width_bands(imp_lx)
            for bi, bn in enumerate(('width low <300', 'width mid .3-5k', 'width hi >5k')):
                cd, cl = w_dv[bi], w_lx[bi]
                if cd is None or cl is None:
                    print(f"  {bn:30s}  SKIPPED (mono/silent band)"); continue
                d, p, line = check(bn, cd, cl, GATES['spatial_width_band'])
                print(line)
                if p == 'FAIL': fails.append(line.strip())
            # Diffusion flux — kurtosis-trajectory match over first 150 ms.
            k_dv = diffusion_flux_curve(imp_dv); k_lx = diffusion_flux_curve(imp_lx)
            n = min(len(k_dv), len(k_lx))
            if n >= 4:
                flux = float(np.mean(np.abs(k_dv[:n] - k_lx[:n])))
                passing = flux <= GATES['diffusion_flux']
                line = (f"  {'diffusion_flux (kurt L1)':30s}  Δ={flux:6.2f}  "
                        f"gate=≤{GATES['diffusion_flux']}  {'✓' if passing else '✗'}")
                print(line)
                if not passing: fails.append(line.strip())

        # ─── 1/3-oct RMS-normalized L1 ───
        # Floor-guard: skip bands where the ANCHOR is below -55 dB (RMS-norm) —
        # it carries no real signal there (the dark VVV/Lex anchors roll off to
        # the dither floor above ~10 kHz at -100+ dB). Without this, comparing
        # DV's normal top-octave HF to the anchor's floor blew the normalized-dB
        # L1 up to +71 dB at 12902 Hz — a measurement ghost, not real energy
        # (verified: no HF resonance in the audio). Same silence-gate principle
        # as the ripple/mod/boom fix. Bands where the anchor HAS energy (incl.
        # a real DV HF deficit vs a bright anchor) are still scored.
        FLOOR_NORM_DB = -55.0
        centers = m_dv['oct_centers']; dvn = m_dv['oct_db_norm']; lxn = m_lx['oct_db_norm']
        scored = [(float(c), abs(float(lx-dv)))
                  for c, dv, lx in zip(centers, dvn, lxn) if float(lx) > FLOOR_NORM_DB]
        if not scored:
            scored = [(float(centers[0]), 0.0)]   # degenerate guard
        abs_d   = [ad for _, ad in scored]
        mean_l1 = sum(abs_d) / len(abs_d)
        max_l1  = max(abs_d)
        max_fc  = [c for c, ad in scored if ad == max_l1][0]
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
        # t1 (steady-state window end) + the tail offsets below must match the
        # sustained-pink hold length the harness rendered with (render.cpp's
        # --sustained-pink-seconds); a hardcoded 4.0 mis-scores any other hold.
        def _band_rms_db(p, lo, hi, t0=2.5, t1=sustained_pink_seconds):
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
            off = int(sustained_pink_seconds * sr)
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

        print(f"\n── SUSTAINED-PINK STEADY-STATE PER-BAND ENERGY (window 2.5-{sustained_pink_seconds:g}s) ──")
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
            off = int(sustained_pink_seconds * sr)
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

    # ─── Boom (late-window low-band integrated RMS) ───
    if dv and lx:
        print("\n── BOOM (late-window low-band integrated RMS, peak-aligned) ──")
        boom_gate = GATES['boom_late_low_dB']
        for (t0, t1, win_lab) in [(0.5, 1.0, '500ms-1s'),
                                   (1.0, 2.0, '1-2s')]:
            for (lo, hi, b_lab) in [(40, 100,  'sub 40-100'),
                                     (80, 200,  'low 80-200'),
                                     (100, 300, 'low 100-300')]:
                dv_db = _late_low_rms_db(dv, t0, t1, lo, hi)
                lx_db = _late_low_rms_db(lx, t0, t1, lo, hi)
                if dv_db is None or lx_db is None: continue
                # Floor-skip: if the anchor band is below -90 dB in this window
                # it carries no real low-end energy (a short-decay room is dead
                # by 500ms-1s; its noiseburst sub at 1-2s is -126..-132 dB =
                # digital silence). Comparing DV's floor to that scores noise,
                # not boom. Long-decay presets keep real energy here and still
                # gate normally. Same principle as the ripple/mod sustained fix.
                if lx_db < -90.0:
                    print(f"  {f'boom {b_lab} {win_lab}':30s}  "
                          f"SKIPPED (anchor band below -90 dB floor)")
                    continue
                delta = dv_db - lx_db
                passing = abs(delta) <= boom_gate
                line = (f"  {f'boom {b_lab} {win_lab}':30s}  "
                        f"DV={dv_db:+7.2f}  Lex={lx_db:+7.2f}  Δ={delta:+6.2f}  "
                        f"gate=±{boom_gate}  {'✓' if passing else '✗'}")
                print(line)
                if not passing: fails.append(line.strip())

    # ─── HF bloom (early-window per-band hot ceiling) ───
    if dv and lx:
        print("\n── HF BLOOM (50-300 ms post-peak, per-band hot ceiling) ──")
        bloom_gate = GATES['hf_bloom_hot_dB']
        for (lo, hi, b_lab) in [(2000, 4000,  '2-4k'),
                                 (4000, 8000,  '4-8k'),
                                 (8000, 12000, '8-12k')]:
            dv_db = _post_peak_band_rms_db(dv, 50, 300, lo, hi)
            lx_db = _post_peak_band_rms_db(lx, 50, 300, lo, hi)
            if dv_db is None or lx_db is None: continue
            delta = dv_db - lx_db
            passing = delta <= bloom_gate
            line = (f"  {f'bloom {b_lab}':30s}  "
                    f"DV={dv_db:+7.2f}  Lex={lx_db:+7.2f}  Δ={delta:+5.2f}  "
                    f"gate≤+{bloom_gate}  {'✓' if passing else '✗'}")
            print(line)
            if not passing: fails.append(line.strip())

    # ─── Body sustain (mid-window per-band cold floor) ───
    if dv and lx:
        print("\n── BODY SUSTAIN (300-800 ms post-peak, per-band cold floor) ──")
        body_gate = GATES['body_sustain_cold_dB']
        for (lo, hi, b_lab) in [(125, 250,  '125-250'),
                                 (250, 500,  '250-500'),
                                 (500, 1000, '500-1k'),
                                 (1000, 2000, '1-2k')]:
            dv_db = _post_peak_band_rms_db(dv, 300, 800, lo, hi)
            lx_db = _post_peak_band_rms_db(lx, 300, 800, lo, hi)
            if dv_db is None or lx_db is None: continue
            delta = dv_db - lx_db
            passing = delta >= -body_gate
            line = (f"  {f'body {b_lab}':30s}  "
                    f"DV={dv_db:+7.2f}  Lex={lx_db:+7.2f}  Δ={delta:+5.2f}  "
                    f"gate≥-{body_gate}  {'✓' if passing else '✗'}")
            print(line)
            if not passing: fails.append(line.strip())

    # ─── Per-stimulus broadband RMS ─── catches spectrum-dependent level
    # mismatch a single Gain Trim knob cannot fix simultaneously.
    if dv_dir and lex_dir:
        print("\n── PER-STIMULUS FULL RMS (broadband level vs anchor) ──")
        ps_gate = GATES['per_stim_rms_dB']
        for stim in ['noiseburst', 'snare', 'sine1k']:
            dv_f = find_stim(dv_dir, stim); lx_f = find_stim(lex_dir, stim)
            if not dv_f or not lx_f: continue
            dv_db = _full_rms_db (dv_f); lx_db = _full_rms_db (lx_f)
            delta = dv_db - lx_db
            passing = abs(delta) <= ps_gate
            line = (f"  {f'full RMS {stim}':30s}  "
                    f"DV={dv_db:+7.2f}  VVV={lx_db:+7.2f}  Δ={delta:+5.2f}  "
                    f"gate=±{ps_gate}  {'✓' if passing else '✗'}")
            print(line)
            if not passing: fails.append(line.strip())

    # ─── Sine 1 kHz steady-state delta — exposes mid-presence coloration ───
    if dv_dir and lex_dir:
        print("\n── SINE 1 kHz STEADY-STATE (mid coloration probe) ──")
        s1_gate = GATES['sine1k_full_rms_dB']
        dv_s = find_stim(dv_dir, 'sine1k'); lx_s = find_stim(lex_dir, 'sine1k')
        if dv_s and lx_s:
            dv_db = _full_rms_db (dv_s); lx_db = _full_rms_db (lx_s)
            delta = dv_db - lx_db
            passing = abs(delta) <= s1_gate
            line = (f"  {'sine1k full RMS':30s}  "
                    f"DV={dv_db:+7.2f}  VVV={lx_db:+7.2f}  Δ={delta:+5.2f}  "
                    f"gate=±{s1_gate}  {'✓' if passing else '✗'}")
            print(line)
            if not passing: fails.append(line.strip())

    # ─── Tail PITCH-CHORUS (COARSE sanity guard) ───
    # Replaces the per-band envelope-AM rate gate, which measured AMPLITUDE
    # modulation and so rewarded a tremolo pump (a 16-tap AM VCA at depth 0.537
    # phase-cancelled in the mono sum to fake a "match" while pumping audibly).
    # This estimator (sine1k carrier IF, LP-filtered <12 Hz) is NOT robust enough
    # for a tight match — std is spike-dominated by beat-null phase-wraps, so the
    # absolute value swings 100-1000x across estimator variants. It IS reliable
    # at catching GROSS defects (dead LFO ≈ 0, or a violent pump reading far high).
    # So it's a COARSE guard: pass if DV sits within a 0.3x..3.0x window of the
    # anchor. Fine tail-CHARACTER matching is done by ear, not this gate.
    pc_dv = find_stim(dv_dir, 'sine1k'); pc_lx = find_stim(lex_dir, 'sine1k')
    if pc_dv and pc_lx:
        print("\n── TAIL PITCH-CHORUS (coarse guard, 0.3x-3.0x, sine1k) ──")
        dv_c = _true_pitch_chorus_hz (pc_dv)
        lx_c = _true_pitch_chorus_hz (pc_lx)
        if dv_c is None or lx_c is None or lx_c < 0.5:
            print("  tail pitch-chorus            SKIPPED (carrier below floor)")
        else:
            ratio = dv_c / lx_c
            passing = 0.3 <= ratio <= 3.0
            line = (f"  {'tail pitch-chorus (Hz)':30s}  "
                    f"DV={dv_c:5.2f}  VVV={lx_c:5.2f}  ratio={ratio:4.2f}x  "
                    f"gate=0.3-3.0x  {'✓' if passing else '✗'}")
            print(line)
            if not passing: fails.append(line.strip())

    # ─── Per-band RT60 (Schroeder backward integration, noiseburst tail) ───
    if dv and lx:
        print("\n── PER-BAND RT60 (Schroeder backward int, ±5% JND gate) ──")
        rt_gate = GATES['t60_band_pct']
        for (lo, hi, b_lab) in [(44,   88,    '63 Hz'),
                                 (88,   177,   '125 Hz'),
                                 (177,  355,   '250 Hz'),
                                 (355,  710,   '500 Hz'),
                                 (710,  1420,  '1 kHz'),
                                 (1420, 2840,  '2 kHz'),
                                 (2840, 5680,  '4 kHz'),
                                 (5680, 11360, '8 kHz'),
                                 (11360, 18000, '16 kHz')]:
            dv_t = _t60_band_schroeder (dv, lo, hi)
            lx_t = _t60_band_schroeder (lx, lo, hi)
            if dv_t is None or lx_t is None or lx_t <= 0.05:
                print(f"  {f'T60 {b_lab}':30s}  SKIPPED (band below noise floor)")
                continue
            pct = (dv_t - lx_t) / lx_t * 100.0
            passing = abs(pct) <= rt_gate
            line = (f"  {f'T60 {b_lab}':30s}  "
                    f"DV={dv_t:5.2f}s  VVV={lx_t:5.2f}s  Δ={pct:+6.1f}%  "
                    f"gate=±{rt_gate}%  {'✓' if passing else '✗'}")
            print(line)
            if not passing: fails.append(line.strip())

    # ─── Tail mod ripple (per-band detrended envelope std) ───
    # Same stimulus rule as the mod-freq gate: measure on sustained pink, not
    # the noiseburst's post-decay silence (where every diffuse engine's modal
    # residue reads ripple ~18 vs the anchor's -118dB dither floor ~3-6 — a
    # noise-vs-noise comparison, not modulation). Fall back to noiseburst when
    # no sustained render exists; skip when the anchor window is below floor.
    rip_dv = sus_dv if sus_dv else dv
    rip_lx = sus_lx if sus_lx else lx
    if rip_dv and rip_lx:
        print("\n── TAIL MOD RIPPLE (per-band detrended env std, 0.5-3s) ──")
        rip_gate = GATES['tail_mod_ripple_dB']
        rip_active = windowed_above_floor(rip_lx, 0.5, 3.0)
        for (lo, hi, b_lab) in [(40, 250,   'bass 40-250'),
                                 (250, 1000, 'lowmid 250-1k'),
                                 (1000, 4000, 'mid 1-4k'),
                                 (4000, 12000, 'high 4-12k')]:
            if not rip_active:
                print(f"  {f'ripple {b_lab}':30s}  SKIPPED (anchor window below noise floor)")
                continue
            dv_std = _tail_env_ripple_db(rip_dv, 0.5, 3.0, lo, hi)
            lx_std = _tail_env_ripple_db(rip_lx, 0.5, 3.0, lo, hi)
            if dv_std is None or lx_std is None: continue
            delta = dv_std - lx_std
            # Asymmetric gate: DV-cooler is fine (DV smoother than Lex);
            # DV-hotter is what we're catching.
            passing = delta <= rip_gate
            line = (f"  {f'ripple {b_lab}':30s}  "
                    f"DV={dv_std:5.2f}  Lex={lx_std:5.2f}  Δ={delta:+5.2f}  "
                    f"gate≤+{rip_gate}  {'✓' if passing else '✗'}")
            print(line)
            if not passing: fails.append(line.strip())

    # ─── ADVISORY perceptual metrics (NOT counted in n_fail) ───
    # Instrumentation only until ear-validated. Computed on the noiseburst tail
    # (dv/lx). Prints ✓/✗ vs the calibrated thresholds but never touches `fails`.
    if dv and lx:
        print("\n── ADVISORY (perceptual, NOT gated — ear-validation pending) ──")
        fv_dv = adv_spectral_flux_var(dv); fv_lx = adv_spectral_flux_var(lx)
        if fv_dv is None or fv_lx is None:
            print(f"  {'spectral_flux_var':30s}  SKIPPED (tail below floor)")
        else:
            dd = abs(fv_dv - fv_lx); ok = dd <= GATES['spectral_flux_variance']
            print(f"  {'spectral_flux_var':30s}  DV={fv_dv:6.3f}  VVV={fv_lx:6.3f}  "
                  f"|Δ|={dd:6.3f}  gate≤{GATES['spectral_flux_variance']}  "
                  f"{'✓' if ok else '✗'}  [ADVISORY]")
        r_dv = adv_decay_curvature(dv); r_lx = adv_decay_curvature(lx)
        for ri, rn, gk in ((0, 'decay_curv_r1 (EDT/T30)', 'decay_curvature_r1'),
                           (1, 'decay_curv_r2 (T30/T60)', 'decay_curvature_r2')):
            av, bv = r_dv[ri], r_lx[ri]
            if av is None or bv is None:
                print(f"  {rn:30s}  SKIPPED (span unmeasurable)"); continue
            dd = abs(av - bv); ok = dd <= GATES[gk]
            print(f"  {rn:30s}  DV={av:5.3f}  VVV={bv:5.3f}  |Δ|={dd:5.3f}  "
                  f"gate≤{GATES[gk]}  {'✓' if ok else '✗'}  [ADVISORY]")
        bk_dv = adv_bark_masking_traj(dv); bk_lx = adv_bark_masking_traj(lx)
        if bk_dv is None or bk_lx is None:
            print(f"  {'bark_masking_l1':30s}  SKIPPED (tail below floor)")
        else:
            n = min(len(bk_dv), len(bk_lx))
            l1 = float(np.mean(np.abs(bk_dv[:n] - bk_lx[:n]))) * 1e3
            ok = l1 <= GATES['bark_masking_l1']
            print(f"  {'bark_masking_l1 (x1e3)':30s}  L1={l1:6.3f}  "
                  f"gate≤{GATES['bark_masking_l1']}  {'✓' if ok else '✗'}  [ADVISORY]")

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
    ap.add_argument("--json", action="store_true",
                    help="After the human-readable sheets, emit one machine-"
                         "readable 'JSON_RESULT: {...}' line (n_fail + the "
                         "failed-gate strings) for the optimizer to parse.")
    ap.add_argument("--sustained-pink-seconds", type=float, default=4.0,
                    help="Sustained-pink hold length the render used (must match "
                         "render.cpp's --sustained-pink-seconds). Sets the "
                         "steady-state scoring window end + tail-decay offset.")
    args = ap.parse_args()
    fails = audit(args.dv_dir, args.lex_dir, args.name, args.category,
                  args.sustained_pink_seconds)
    if args.json:
        print("JSON_RESULT: " + json.dumps({"n_fail": len(fails), "fails": fails}))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
