"""
IR Comparison Engine — Compare two IR profiles and produce a similarity score.

Compares: RT60, spectral decay shape, frequency response, stereo characteristics,
and energy decay curve shape. Produces a weighted overall score (0-100) and
per-dimension breakdown.
"""

import numpy as np
from dataclasses import dataclass
from ir_analysis import IRProfile


@dataclass
class ComparisonResult:
    """Detailed comparison result between two IR profiles."""
    target_name: str
    candidate_name: str

    # Per-dimension scores (0-100, higher = better match)
    rt60_score: float
    edt_score: float
    edc_shape_score: float
    spectral_early_score: float
    spectral_late_score: float
    spectral_centroid_score: float
    band_rt60_score: float
    pre_delay_score: float
    stereo_score: float

    # Weighted overall
    overall_score: float

    # Raw differences for diagnostics
    rt60_diff_s: float
    edt_diff_s: float
    pre_delay_diff_ms: float
    band_rt60_diffs: dict

    def summary(self) -> str:
        lines = [
            f"Comparison: {self.target_name} vs {self.candidate_name}",
            f"  Overall Score: {self.overall_score:.1f}/100",
            f"",
            f"  Dimension Scores:",
            f"    RT60:              {self.rt60_score:.1f}  (diff: {self.rt60_diff_s:+.3f}s)",
            f"    EDT:               {self.edt_score:.1f}  (diff: {self.edt_diff_s:+.3f}s)",
            f"    EDC Shape:         {self.edc_shape_score:.1f}",
            f"    Spectral (early):  {self.spectral_early_score:.1f}",
            f"    Spectral (late):   {self.spectral_late_score:.1f}",
            f"    Spectral Centroid: {self.spectral_centroid_score:.1f}",
            f"    Band RT60:         {self.band_rt60_score:.1f}",
            f"    Pre-delay:         {self.pre_delay_score:.1f}  (diff: {self.pre_delay_diff_ms:+.1f}ms)",
            f"    Stereo:            {self.stereo_score:.1f}",
        ]
        if self.band_rt60_diffs:
            lines.append(f"  Band RT60 diffs:")
            for band, diff in sorted(self.band_rt60_diffs.items(),
                                      key=lambda x: int(x[0].replace('Hz', ''))):
                lines.append(f"    {band}: {diff:+.3f}s")
        return "\n".join(lines)


def _score_from_error(error: float, tolerance: float) -> float:
    """Convert an absolute error to a 0-100 score using exponential decay."""
    return 100.0 * np.exp(-(error / tolerance) ** 2)


def _resample_to_common(a: np.ndarray, b: np.ndarray, n: int = 200) -> tuple[np.ndarray, np.ndarray]:
    """Resample two arrays to the same length for comparison."""
    from scipy.interpolate import interp1d
    if len(a) < 2 or len(b) < 2:
        return np.zeros(n), np.zeros(n)
    x_a = np.linspace(0, 1, len(a))
    x_b = np.linspace(0, 1, len(b))
    x_common = np.linspace(0, 1, n)
    fa = interp1d(x_a, a, kind='linear', fill_value='extrapolate')
    fb = interp1d(x_b, b, kind='linear', fill_value='extrapolate')
    return fa(x_common), fb(x_common)


def _spectral_similarity(spec_a: np.ndarray, spec_b: np.ndarray,
                          freq_axis: np.ndarray,
                          low_hz: float = 100, high_hz: float = 16000) -> float:
    """
    Compare two frequency response spectra (in dB) over a frequency range.
    Returns 0-100 score.
    """
    if spec_a is None or spec_b is None:
        return 50.0  # neutral score if data missing

    # Limit to frequency range of interest
    mask = (freq_axis >= low_hz) & (freq_axis <= high_hz)
    if np.sum(mask) < 10:
        return 50.0

    a = spec_a[mask]
    b = spec_b[mask]

    # Normalize both to their own peaks (we care about shape, not level)
    a = a - np.max(a)
    b = b - np.max(b)

    # RMS difference in dB
    rms_diff = np.sqrt(np.mean((a - b) ** 2))
    return _score_from_error(rms_diff, 15.0)  # 15dB tolerance (FDN vs captured hardware)


def compare_profiles(target: IRProfile, candidate: IRProfile,
                     weights: dict = None) -> ComparisonResult:
    """
    Compare two IR profiles and return a detailed comparison result.

    Default weights emphasize RT60 and spectral character over exact
    time-domain matching (since SilkVerb is algorithmic, not convolution).
    """
    if weights is None:
        weights = {
            'rt60': 0.25,
            'edt': 0.05,
            'edc_shape': 0.15,
            'spectral_early': 0.03,
            'spectral_late': 0.05,
            'spectral_centroid': 0.05,
            'band_rt60': 0.25,
            'pre_delay': 0.05,
            'stereo': 0.12,
        }

    # RT60 score (tolerance: 25% — algorithmic reverbs can match decay time well)
    rt60_diff = candidate.rt60 - target.rt60
    rt60_ref = max(target.rt60, 0.3)
    rt60_score = _score_from_error(abs(rt60_diff), rt60_ref * 0.25)

    # EDT score (tolerance: 50% — EDT is very hard to match with FDN topology)
    edt_diff = candidate.edt - target.edt
    edt_ref = max(target.edt, 0.2)
    edt_score = _score_from_error(abs(edt_diff), edt_ref * 0.50)

    # EDC shape score (compare normalized curves)
    edc_a, edc_b = _resample_to_common(target.edc, candidate.edc, 300)
    # Clip to -60dB range for comparison
    edc_a = np.clip(edc_a, -60, 0)
    edc_b = np.clip(edc_b, -60, 0)
    edc_rms = np.sqrt(np.mean((edc_a - edc_b) ** 2))
    edc_shape_score = _score_from_error(edc_rms, 8.0)

    # Spectral comparison (early reflections, 0-50ms)
    spectral_early_score = _spectral_similarity(
        target.freq_response_early, candidate.freq_response_early,
        target.freq_axis if target.freq_axis is not None else np.array([0]),
    )

    # Spectral comparison (late tail, 200-500ms)
    spectral_late_score = _spectral_similarity(
        target.freq_response_late, candidate.freq_response_late,
        target.freq_axis if target.freq_axis is not None else np.array([0]),
    )

    # Spectral centroid evolution score (tolerance: 40% relative — shape matters more than exact Hz)
    sc_a, sc_b = _resample_to_common(
        target.spectral_centroid_hz, candidate.spectral_centroid_hz, 50
    )
    # Only compare where both have significant energy
    mask = (sc_a > 100) & (sc_b > 100)
    if np.sum(mask) > 5:
        sc_rms = np.sqrt(np.mean(((sc_a[mask] - sc_b[mask]) / sc_a[mask]) ** 2))
        spectral_centroid_score = _score_from_error(sc_rms, 0.4)
    else:
        spectral_centroid_score = 50.0

    # Band RT60 score
    band_diffs = {}
    band_scores = []
    common_bands = set(target.band_rt60.keys()) & set(candidate.band_rt60.keys())
    for band in common_bands:
        t_val = target.band_rt60[band]
        c_val = candidate.band_rt60[band]
        if t_val > 0.05:
            diff = c_val - t_val
            band_diffs[band] = diff
            band_scores.append(_score_from_error(abs(diff), t_val * 0.40))
    band_rt60_score = np.mean(band_scores) if band_scores else 50.0

    # Pre-delay score (tolerance: 25ms — FDN topology creates inherent minimum delay from ER taps)
    pre_delay_diff = candidate.pre_delay_ms - target.pre_delay_ms
    pre_delay_score = _score_from_error(abs(pre_delay_diff), 25.0)

    # Stereo score
    corr_diff = abs(candidate.stereo_correlation - target.stereo_correlation)
    width_diff = abs(candidate.width_estimate - target.width_estimate)
    stereo_score = (
        _score_from_error(corr_diff, 0.25) * 0.5 +
        _score_from_error(width_diff, 0.15) * 0.5
    )

    # Weighted overall
    overall = (
        weights['rt60'] * rt60_score +
        weights['edt'] * edt_score +
        weights['edc_shape'] * edc_shape_score +
        weights['spectral_early'] * spectral_early_score +
        weights['spectral_late'] * spectral_late_score +
        weights['spectral_centroid'] * spectral_centroid_score +
        weights['band_rt60'] * band_rt60_score +
        weights['pre_delay'] * pre_delay_score +
        weights['stereo'] * stereo_score
    )

    return ComparisonResult(
        target_name=target.name,
        candidate_name=candidate.name,
        rt60_score=rt60_score,
        edt_score=edt_score,
        edc_shape_score=edc_shape_score,
        spectral_early_score=spectral_early_score,
        spectral_late_score=spectral_late_score,
        spectral_centroid_score=spectral_centroid_score,
        band_rt60_score=band_rt60_score,
        pre_delay_score=pre_delay_score,
        stereo_score=stereo_score,
        overall_score=overall,
        rt60_diff_s=rt60_diff,
        edt_diff_s=edt_diff,
        pre_delay_diff_ms=pre_delay_diff,
        band_rt60_diffs=band_diffs,
    )
