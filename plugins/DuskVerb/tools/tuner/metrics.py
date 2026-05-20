"""Composite-loss adapter on top of analyze_ir + perceptual_diff.

`measure_pair(impulse_wav, noiseburst_wav)` computes every supported metric
in one pass (so cached results survive config changes). `compute_loss`
sums weighted |Δ|/JND only for metric keys the user enabled in YAML.

Adding a new metric: add an entry to the `_measure` function and (if needed)
a special-case in `compute_loss` for vector-typed metrics.
"""
from __future__ import annotations

import os
import sys
from pathlib import Path
from typing import Any

import numpy as np
import soundfile as sf

# Shared libs live in the parent tools/ dir.
_HERE = os.path.dirname(os.path.abspath(__file__))
_TOOLS = os.path.dirname(_HERE)
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)

import analyze_ir as ai
import perceptual_diff as pd


# Octave anchors for c80_per_octave. Order matters — composite_loss zips
# `meas['c80_per_octave']` against `anchor['c80_per_octave']` positionally.
C80_OCTAVES_HZ = (250.0, 500.0, 1000.0, 2000.0)

# Octave anchors for centroid_drift_per_band. Captures how HF content
# evolves over the tail in each octave — the signed dB difference between
# late and early windows. Lex Concert Hall measured ~-47 dB (natural HF
# decay over tail); SixAPTank Smooth Concert Hall measured +39 dB
# (late-tail modal ringing emerging *above* the early content). Opposite
# sign = engine-character mismatch the prior metric set didn't catch.
CENTROID_DRIFT_OCTAVES_HZ = (500.0, 1000.0, 2000.0, 4000.0)

# Vector-typed metric keys — compute_loss / format_report treat these as
# per-band arrays. Add new vector metrics here and to measure_pair only.
_VECTOR_METRIC_KEYS = ("rt60_per_band", "c80_per_octave",
                       "centroid_drift_per_band",
                       "decay_envelope_db", "peak_locations_ms",
                       "rt60_per_band_stereo")


def _stereo_corr_stability(impulse_path: Path) -> float:
    """Std of L/R correlation across 100 ms windows in the IR tail.

    Captures audible "spinning" / image-wander artifacts that mean
    stereo_correlation misses. Lex Rich Plate measures ~0.029 (rock-
    stable image); a DV PlateEngine with symmetric cross-feed measured
    0.254 (image cycles mono ↔ spread → perceived rotation). JND ≈ 0.05.
    """
    sig, sr = sf.read(str(impulse_path))
    if sig.ndim == 1:
        return 0.0  # mono file, no stereo wander to measure
    L = sig[:, 0].astype(np.float64)
    R = sig[:, 1].astype(np.float64)
    pk = int(np.argmax(np.abs(L + R)))
    tail_secs = 2.0
    end = pk + int(tail_secs * sr)
    L = L[pk:end]
    R = R[pk:end]
    if len(L) < int(0.5 * sr):
        return 0.0
    win = int(0.1 * sr)
    corrs: list[float] = []
    for i in range(0, len(L) - win, win):
        l = L[i : i + win]
        r = R[i : i + win]
        if np.std(l) > 1e-10 and np.std(r) > 1e-10:
            corrs.append(float(np.corrcoef(l, r)[0, 1]))
    if not corrs:
        return 0.0
    return float(np.std(corrs))


def _modal_density(impulse_path: Path) -> tuple[float, float]:
    """(peak_count, max_peak_prominence_dB) for the 200-8000 Hz IR tail.

    Detects "metallic" discrete-tone ringing — DV plate engines with
    few/sharp modal peaks at high prominence sound tonal vs Lex's many
    moderate peaks. Returns peak count + max prominence (dB above local
    floor) so a tuner can penalize both sparsity AND prominence.
    """
    sig, sr = sf.read(str(impulse_path))
    if sig.ndim > 1:
        mono = sig.mean(axis=1).astype(np.float64)
    else:
        mono = sig.astype(np.float64)
    pk = int(np.argmax(np.abs(mono)))
    tail = mono[pk : pk + int(1.0 * sr)]
    if len(tail) < int(0.5 * sr):
        return (0.0, 0.0)
    N = 16384
    pad = max(N - len(tail), 0)
    y = np.concatenate([tail[:N], np.zeros(pad)])[:N]
    F = np.fft.rfft(y * np.hanning(N))
    S = 20.0 * np.log10(np.abs(F) + 1e-12)
    freqs = np.fft.rfftfreq(N, 1.0 / sr)
    mask = (freqs >= 200.0) & (freqs <= 8000.0)
    region = S[mask]
    if len(region) < 50:
        return (0.0, 0.0)
    peaks: list[float] = []
    for i in range(20, len(region) - 20):
        if region[i] > region[i - 1] and region[i] > region[i + 1]:
            local_avg = (
                float(np.mean(region[i - 20 : i]))
                + float(np.mean(region[i + 1 : i + 21]))
            ) * 0.5
            prom = float(region[i] - local_avg)
            if prom > 4.0:
                peaks.append(prom)
    if not peaks:
        return (0.0, 0.0)
    return (float(len(peaks)), float(np.max(peaks)))


def _rt60_per_band_stereo(impulse_path: Path,
                          ai_module: Any) -> list[float]:
    """Per-band RT60 averaged over L and R channels separately, then averaged.

    The original rt60_per_band sums L+R to mono before measuring, which
    under-reads RT60 when L and R late tails are anti-correlated (a side
    effect of polarity-flipped cross-feed in PlateEngine). Per-channel
    measurement reflects what a listener actually perceives at each ear.
    """
    sig, sr = sf.read(str(impulse_path))
    if sig.ndim == 1:
        rt = ai_module.per_band_rt60(sig.astype(np.float64), sr)
        return [float(v) if v else 0.0 for v in rt]
    L = sig[:, 0].astype(np.float64)
    R = sig[:, 1].astype(np.float64)
    rL = ai_module.per_band_rt60(L, sr)
    rR = ai_module.per_band_rt60(R, sr)
    return [(float(a) + float(b)) * 0.5 if a and b else 0.0
            for a, b in zip(rL, rR)]

# Decay-envelope window edges (seconds, peak-relative). 12 windows × 250 ms
# = 3 s coverage; finer-grained than the three late_tail_* metrics so the
# optimizer can shape the 0-3 s decay curve point-by-point.
DECAY_ENVELOPE_EDGES_S = tuple(round(0.0 + i * 0.25, 3) for i in range(13))


def measure_pair(impulse_path: Path, noiseburst_path: Path) -> dict[str, Any]:
    """Compute every supported metric from one (impulse, noiseburst) pair."""
    sr_i, ir_raw = ai.load_ir(str(impulse_path))
    ir = ai.to_mono(ir_raw).astype(np.float64)
    sr_n, nb = pd.load_mono(str(noiseburst_path))
    peak = int(np.argmax(np.abs(ir)))
    ir_trim = ir[peak:]
    nb_tail = nb[int(0.1 * sr_n):] if len(nb) > int(0.1 * sr_n) else nb

    # Late-tail energy windows (peak-relative): captures how quickly the
    # reverb decays past the early-decay window. Lex Concert Hall has
    # consistent -55 to -65 dB at 1-2s post-peak across tested musical
    # signals; DV measured 10 dB QUIETER in same window → DV tail decays
    # faster than Lex. Adding as explicit loss terms forces optimizer to
    # preserve late energy instead of trading it off for early clarity.
    def _window_db (signal: np.ndarray, sr: float, t0: float, t1: float) -> float:
        p = int(np.argmax(np.abs(signal)))
        s0 = p + int(t0 * sr)
        s1 = min(p + int(t1 * sr), len(signal))
        if s1 <= s0:
            return -120.0
        rms = float(np.sqrt(np.mean(signal[s0:s1] ** 2)) + 1e-12)
        return 20.0 * float(np.log10(rms))

    # Decay envelope: dense RMS-in-dB sampling over 12×250 ms windows from
    # the impulse peak. Captures the actual SHAPE of the decay curve, not
    # just its endpoints. Three late_tail_* metrics give coarse anchors at
    # 0.5-1, 1-2, 2-3 s; this fills in the 12 evenly-spaced bins so the
    # optimizer can match the precise slope (e.g. Lex Rich Plate's 1.24 s
    # plate decay produces a near-exponential dB drop while DV's FDN
    # initially drops fast then plateaus — that shape mismatch is invisible
    # to broadband RT60 alone).
    def _decay_envelope_db (signal: np.ndarray, sr: float,
                            edges: tuple[float, ...]) -> list[float]:
        out: list[float] = []
        for i in range(len(edges) - 1):
            out.append(_window_db(signal, sr, edges[i], edges[i + 1]))
        return out

    # Peak locations (ms, peak-relative): detect the first N local maxima
    # in the impulse response within 0-50 ms of the main peak. Pads to a
    # fixed length of 4 with sentinel value 999.0 so vector-loss math
    # stays length-stable across presets. Used to lock specular reflection
    # timings (Lex's 3 ms L / 8 ms R direct reflections); presets without
    # ER (like Lex Rich Plate) will have a single peak at 0.0 and zeros
    # elsewhere — anchor and measured both pad the same way so the loss
    # is well-defined.
    def _peak_locations_ms (signal: np.ndarray, sr: float,
                            max_peaks: int = 4,
                            window_ms: float = 50.0) -> list[float]:
        """First N specular peak times in 0-50ms post-onset.

        Patched 2026-05-20 (P8c follow-up): the prior implementation used
        only a -40 dB amplitude gate which picked up sub-tank modal
        ripples in the 0-4ms post-peak window as "peaks", drowning the
        true specular reflections that fired later. Three additions:

          - HEIGHT THRESHOLD: -20 dB below global peak (was -40 dB) so
            sub-modal ripples that sit 40-50 dB below peak are dropped
            below the gate.

          - PROMINENCE: candidate peak must exceed its local 0.5 ms
            envelope minimum by at least 6 dB. Filters bumps that
            wobble around their own baseline without a real specular
            energy spike.

          - MIN SEPARATION: 1 ms between accepted peaks. Lex anchor
            specular reflections are 3-4 ms apart minimum; modal
            density artefacts on a < 1 ms scale aren't true specular
            events.
        """
        env = np.abs(signal).astype(np.float64)
        p0 = int(np.argmax(env))
        end = min(len(env), p0 + int(window_ms * 1e-3 * sr))
        seg = env[p0:end]
        if len(seg) < 3:
            return [0.0] + [999.0] * (max_peaks - 1)
        peak_amp = seg[0] if seg[0] > 0 else 1.0
        thresh = peak_amp * 10.0 ** (-20.0 / 20.0)        # was -40 dB
        prom_thresh_db = 6.0
        min_sep_samples = int(round(0.001 * sr))           # 1 ms
        prom_window_samples = int(round(0.0005 * sr))      # 0.5 ms

        idx_peaks: list[int] = [0]
        last_accepted = 0
        for i in range(1, len(seg) - 1):
            if not (seg[i] > seg[i - 1] and seg[i] >= seg[i + 1]):
                continue
            if seg[i] < thresh:
                continue
            if i - last_accepted < min_sep_samples:
                continue
            # Prominence: peak must rise prom_thresh_db above the local
            # minimum in the ±0.5 ms window centred on the candidate.
            lo = max(0, i - prom_window_samples)
            hi = min(len(seg), i + prom_window_samples)
            local_min = float(seg[lo:hi].min())
            if local_min <= 0.0:
                local_min = peak_amp * 1e-9
            prom_db = 20.0 * float(np.log10(seg[i] / local_min))
            if prom_db < prom_thresh_db:
                continue
            idx_peaks.append(i)
            last_accepted = i
            if len(idx_peaks) >= max_peaks:
                break
        ms = [round(idx / sr * 1000.0, 3) for idx in idx_peaks]
        while len(ms) < max_peaks:
            ms.append(999.0)
        return ms[:max_peaks]

    return {
        # Per-band vectors
        "rt60_per_band":   ai.per_band_rt60(ir, sr_i),     # 8 floats / None
        "c80_per_octave":  [pd.c80_per_octave(ir, sr_i, fc) for fc in C80_OCTAVES_HZ],
        # Late-tail energy at three windows past the peak. Penalizes DV
        # presets whose tail dies faster (or slower) than Lex anchor.
        "late_tail_500ms_1s":  _window_db(ir, sr_i, 0.5, 1.0),
        "late_tail_1s_2s":     _window_db(ir, sr_i, 1.0, 2.0),
        "late_tail_2s_3s":     _window_db(ir, sr_i, 2.0, 3.0),
        # Decay envelope: fine-grained shape of the 0-3 s tail decay.
        "decay_envelope_db":   _decay_envelope_db(ir, sr_i, DECAY_ENVELOPE_EDGES_S),
        # Peak timing of first 4 specular peaks in 0-50 ms post-onset.
        "peak_locations_ms":   _peak_locations_ms(ir, sr_i),
        # Centroid drift per octave: signed dB shift (late_window - early_window).
        # Lex Concert Hall: -40 to -55 dB across 500/1k/2k/4k (HF dies naturally).
        # SixAPTank Smooth Concert Hall: +30 to +50 dB (late-tail modal ringing
        # emerges above early signal — engine artifact). Wider JND (5-10 dB).
        "centroid_drift_per_band":
            [pd.centroid_drift_db(ir, sr_i, fc) for fc in CENTROID_DRIFT_OCTAVES_HZ],
        # Broadband / single-value energy decay metrics (impulse)
        "edt":             pd.edt(ir, sr_i),
        "edt_500":         pd.edt(ir, sr_i, 500),
        "c80":             pd.c80(ir, sr_i),
        "d50":             pd.d50(ir_trim, sr_i),
        "bass_ratio":      pd.bass_ratio(ir_trim, sr_i),
        "treble_ratio":    pd.treble_ratio(ir_trim, sr_i),
        # Steady-state / spectral metrics (noise burst)
        "box_ratio_db":    pd.box_ratio_db(nb, sr_n),
        "a_weighted_rms_db": pd.a_weighted_rms_db(nb, sr_n),
        "k_weighted_lufs": pd.k_weighted_lufs(nb, sr_n),
        "spectral_crest_db": pd.spectral_crest_db(nb_tail, sr_n),
        "time_domain_crest": pd.time_domain_crest(nb, sr_n),
        # Stereo metric needs the file path (perceptual_diff loads the stereo
        # version internally — load_mono above would collapse channels).
        "stereo_correlation": pd.stereo_correlation(str(noiseburst_path)),
        # Stereo image stability over time. High value = image cycles
        # mono ↔ spread (audible as "spinning" / rotation). Added 2026-05-18
        # after Rich Plate iteration where mono-sum metrics looked clean but
        # listening revealed strong image wander from symmetric cross-feed.
        # Lex Rich Plate measures ~0.03; PlateEngine symmetric-cross
        # baseline measured 0.25.
        "stereo_corr_stability": _stereo_corr_stability(impulse_path),
        # Per-channel RT60: average of L and R RT60 (not mono sum). Catches
        # cases where polarity-flipped feedback or L/R-asymmetric processing
        # makes the mono sum read a different decay than each ear perceives.
        "rt60_per_band_stereo": _rt60_per_band_stereo(impulse_path, ai),
        # Modal density (peak count, max prominence dB) in 200-8000 Hz tail.
        # Detects "metallic" discrete-tone ringing — few sharp peaks at high
        # prominence vs Lex's many moderate peaks. Returned as a 2-tuple
        # but exposed as two scalar keys for the loss function.
        "modal_peak_count":     _modal_density(impulse_path)[0],
        "modal_peak_max_db":    _modal_density(impulse_path)[1],
    }


def measure_anchor(impulse_path: Path, noiseburst_path: Path) -> dict[str, Any]:
    """Same as measure_pair — exposed under a separate name so future
    anchor-specific preprocessing (level matching, etc.) has a hook."""
    return measure_pair(impulse_path, noiseburst_path)


def _safe_pair(m: Any, a: Any) -> tuple[float, float] | None:
    if m is None or a is None:
        return None
    try:
        mf, af = float(m), float(a)
    except (TypeError, ValueError):
        return None
    if np.isnan(mf) or np.isnan(af):
        return None
    return mf, af


def compute_loss(meas: dict, anchor: dict,
                 weights_cfg: dict) -> tuple[float, dict[str, float]]:
    """Weighted L1 (each term divided by its JND). Returns (total, breakdown).
    Missing metric values are skipped (do not contribute), so a partially-
    measured render still produces a finite loss instead of NaN."""
    total = 0.0
    breakdown: dict[str, float] = {}

    for key, w in weights_cfg.items():
        if key in _VECTOR_METRIC_KEYS:
            m_vec = meas.get(key) or []
            a_vec = anchor.get(key) or []
            if len(m_vec) != len(a_vec):
                print(
                    f"warn: {key} length mismatch — m_vec={len(m_vec)} "
                    f"a_vec={len(a_vec)}; zip will truncate to shorter",
                    file=sys.stderr,
                )
            pairs = [p for p in (_safe_pair(mv, av)
                                 for mv, av in zip(m_vec, a_vec)) if p]
            if not pairs:
                continue
            d = sum(abs(mv - av) for mv, av in pairs) / (w.jnd * len(pairs))
        else:
            pair = _safe_pair(meas.get(key), anchor.get(key))
            if pair is None:
                continue
            d = abs(pair[0] - pair[1]) / w.jnd
        breakdown[key] = d
        total += w.weight * d
    return total, breakdown


def format_report(meas: dict, anchor: dict,
                  weights_cfg: dict) -> str:
    """Plain-text before/after table for stdout / report.md.
    Columns: metric, anchor, measured, |Δ|, |Δ|/JND, status."""
    loss, breakdown = compute_loss(meas, anchor, weights_cfg)
    lines = [f"{'metric':<22s} {'anchor':>10s} {'measured':>10s} "
             f"{'|Δ|':>8s} {'|Δ|/JND':>8s} {'w·d':>8s}  status"]
    lines.append("-" * 80)
    for key, w in weights_cfg.items():
        if key in _VECTOR_METRIC_KEYS:
            m_vec = meas.get(key) or []
            a_vec = anchor.get(key) or []
            for i, (mv, av) in enumerate(zip(m_vec, a_vec)):
                pair = _safe_pair(mv, av)
                if pair is None:
                    continue
                delta = abs(pair[0] - pair[1])
                norm = delta / w.jnd
                status = "ok" if norm <= 1.0 else "OFF"
                lines.append(
                    f"  {key}[{i}]{'':<10s} {pair[1]:>10.3f} {pair[0]:>10.3f} "
                    f"{delta:>8.3f} {norm:>8.2f} {'':>8s}  {status}")
            agg = breakdown.get(key, 0.0)
            lines.append(f"{key:<22s} {'(mean)':>10s} {'':>10s} {'':>8s} "
                         f"{agg:>8.2f} {w.weight * agg:>8.2f}  "
                         f"{'ok' if agg <= 1.0 else 'OFF'}")
        else:
            pair = _safe_pair(meas.get(key), anchor.get(key))
            if pair is None:
                lines.append(f"{key:<22s} {'(missing)':>10s}")
                continue
            delta = abs(pair[0] - pair[1])
            norm = delta / w.jnd
            status = "ok" if norm <= 1.0 else "OFF"
            lines.append(
                f"{key:<22s} {pair[1]:>10.3f} {pair[0]:>10.3f} "
                f"{delta:>8.3f} {norm:>8.2f} {w.weight * norm:>8.2f}  {status}")
    lines.append("-" * 80)
    lines.append(f"{'total loss':<22s} {'':>10s} {'':>10s} {'':>8s} {'':>8s} "
                 f"{loss:>8.2f}")
    return "\n".join(lines)
