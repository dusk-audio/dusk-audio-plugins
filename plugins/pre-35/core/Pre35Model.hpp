// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Pre35Model.hpp — the M-35 channel model's algebra, in C++.
//
// Pure functions over Pre35Coefficients.hpp: no state, no allocation, nothing
// rate-dependent. Every function here is a line-for-line port of its counterpart
// in the Python reference (dusk-audio-tools tools/m35/fit/model.py), named after
// it so a diff is a diff:
//
//     taperGainDb        <- model.taper_gain_db
//     padOffsetDb        <- model.pad_offset_db
//     gainCalDb          <- model.gain_cal_db
//     ampGainLin         <- model.amp_gain_lin
//     buildResponseZPK   <- model.response_zpk
//     buildSidechainZPK  <- model.sidechain_zpk
//     buildNoiseTailZPK  <- model.noise_sos (analog half)
//     ironR3 / ironR3PowerLaw <- model.iron_r3 / iron_r3_powerlaw
//
// If one of these ever has to diverge from the Python, the null test in
// plugins/pre-35/tools is what will catch it. Change both or neither.

#pragma once

#include "Filters.hpp"
#include "Pre35Coefficients.hpp"

#include <algorithm>
#include <cmath>

namespace pre35
{

// AnalogZPK has fixed capacity and its add* methods clamp rather than grow, so a
// model with more sections than kMaxTerms would silently lose poles and render a
// plausible-looking wrong filter. The section counts come from the GENERATED
// header, so they can change without anyone touching this file — pin them here.
static_assert(coeffs::kNumSidechainSections <= AnalogZPK::kMaxTerms,
              "iron sidechain has more sections than AnalogZPK can hold");
static_assert(coeffs::kNumSidechain2Sections <= AnalogZPK::kMaxTerms,
              "iron sidechain 2 (h2 weighting) has more sections than AnalogZPK can hold");
static_assert(coeffs::kNoise.lfSections + 1 <= AnalogZPK::kMaxTerms,
              "noise 1/f tail has more poles than AnalogZPK can hold");
// Response: one LF pole, one HF pole, one optional GBW pole, plus their zeros.
static_assert(3 <= AnalogZPK::kMaxTerms,
              "response chain has more sections than AnalogZPK can hold");

//==============================================================================
// Gain

/** Pad-0-referenced cal gain in dB for a trim knob position in percent.

    Two-parameter logistic in dB-vs-knob, pinned to both hard stops: exact at
    0 % and 100 % (the only zero-uncertainty measurements) and monotone between.
*/
inline double taperGainDb(double knobPercent) noexcept
{
    const auto& t = coeffs::kTaper;
    const double u  = knobPercent / 100.0;
    const double s  = 1.0 / (1.0 + std::exp(-t.k * (u - t.u0)));
    const double s0 = 1.0 / (1.0 + std::exp( t.k * t.u0));
    const double s1 = 1.0 / (1.0 + std::exp(-t.k * (1.0 - t.u0)));
    return t.g0Db + (t.g1Db - t.g0Db) * ((s - s0) / (s1 - s0));
}

inline int clampPadIndex(int padIndex) noexcept
{
    return std::min(std::max(padIndex, 0), coeffs::kNumPads - 1);
}

inline double padOffsetDb(int padIndex) noexcept
{
    return coeffs::kPads[clampPadIndex(padIndex)].offsetDb;
}

/** Total channel cal gain (dB) for a knob position and pad. */
inline double gainCalDb(double knobPercent, int padIndex) noexcept
{
    return taperGainDb(knobPercent) + padOffsetDb(padIndex);
}

/** Linear gain of the amplifier alone (pad excluded) — this is what moves the
    GBW shelf corner, so it is deliberately pad-independent. */
inline double ampGainLin(double knobPercent) noexcept
{
    return dbToLin(taperGainDb(knobPercent));
}

//==============================================================================
// Response

/** The project's midband normalisation grid: kMidbandGridPoints log-spaced
    points from 200 Hz to 2 kHz, endpoints pinned exactly. Reproduces
    numpy.geomspace, which forces the two endpoints after computing the log grid.
*/
inline void midbandGrid(double* out, int n) noexcept
{
    const double a = std::log10(coeffs::kMidbandLoHz);
    const double b = std::log10(coeffs::kMidbandHiHz);
    const double step = (b - a) / static_cast<double>(n - 1);
    for (int i = 0; i < n; ++i)
        out[i] = std::pow(10.0, a + static_cast<double>(i) * step);
    out[0]     = coeffs::kMidbandLoHz;
    out[n - 1] = coeffs::kMidbandHiHz;
}

/** Median of `n` doubles, numpy convention (mean of the two middle values when
    n is even). Sorts a stack copy — no allocation, and n is 64. */
inline double medianOf(const double* v, int n) noexcept
{
    double tmp[coeffs::kMidbandGridPoints];
    const int m = std::min(n, coeffs::kMidbandGridPoints);
    for (int i = 0; i < m; ++i)
        tmp[i] = v[i];
    std::sort(tmp, tmp + m);
    return (m % 2) ? tmp[m / 2] : 0.5 * (tmp[m / 2 - 1] + tmp[m / 2]);
}

/** Analog response prototype for one (pad, trim) setting.

    Three first-order sections:
      * lf         highpass, per pad — the input transformer loaded by the pad
      * hf_static  lowpass, per pad — the transformer's own HF corner
      * hf_gbw     shelf, corner = GBW / ampGain — the amplifier running out of
                   loop gain. Present whenever the fitted depth is > 0, which is
                   the same test the reference makes: pad 20's depth fitted to a
                   denormal-size number rather than to exactly zero, and matching
                   the reference means keeping that pole/zero pair (it cancels to
                   within 1e-28 dB, so it costs nothing but a null).

    With `normalise`, the gain is scaled so the ANALOG magnitude has a 0 dB
    median across the midband grid — exactly how the measurement was normalised,
    which is what stops the response and the taper double-counting.
*/
inline AnalogZPK buildResponseZPK(int padIndex, double knobPercent,
                                  bool normalise = true) noexcept
{
    const auto& r = coeffs::kResponse[clampPadIndex(padIndex)];

    const double wHp = 2.0 * kPi * r.hpHz;
    const double wLp = 2.0 * kPi * r.lpHz;

    AnalogZPK zpk;
    zpk.addZero(0.0);
    zpk.addPole(-wHp);
    zpk.addPole(-wLp);
    zpk.gain = wLp;

    if (r.gbwShelfDb > 0.0)
    {
        const double fp = r.gbwHz / ampGainLin(knobPercent);
        const double wp = 2.0 * kPi * fp;
        const double wz = wp * dbToLin(r.gbwShelfDb);
        zpk.addZero(-wz);
        zpk.addPole(-wp);
        zpk.gain *= wp / wz;
    }

    if (normalise)
    {
        double grid[coeffs::kMidbandGridPoints];
        double mag [coeffs::kMidbandGridPoints];
        midbandGrid(grid, coeffs::kMidbandGridPoints);
        for (int i = 0; i < coeffs::kMidbandGridPoints; ++i)
            mag[i] = zpk.magnitudeDb(grid[i]);
        zpk.gain *= dbToLin(-medianOf(mag, coeffs::kMidbandGridPoints));
    }

    return zpk;
}

/** Normalised response magnitude in dB (0 dB at midband) — the fit target. */
inline double responseDb(double f, int padIndex, double knobPercent) noexcept
{
    return buildResponseZPK(padIndex, knobPercent).magnitudeDb(f);
}

//==============================================================================
// Iron

/** Model h3/h1 (linear) at frequency f, with the subsonic guard.

    Constant-percentage: no level term. Without fShelfHz the power law puts
    unbounded distortion at DC, which neither the transformer nor the measurement
    supports. Above ~20 Hz the guard costs < 0.15 dB.
*/
inline double ironR3(double f) noexcept
{
    const auto& pl = coeffs::kIronPowerLaw;
    const double fs = pl.fShelfHz;
    const double f0 = pl.fRefHz;
    const double ratio = std::sqrt(fs * fs + f * f) / std::sqrt(fs * fs + f0 * f0);
    return pl.r3Ref * std::pow(ratio, -pl.alpha);
}

/** The published spec: r3Ref * (f/fRef)^-alpha, no subsonic guard. This is what
    the iron gates are measured against. */
inline double ironR3PowerLaw(double f) noexcept
{
    const auto& pl = coeffs::kIronPowerLaw;
    return pl.r3Ref * std::pow(f / pl.fRefHz, -pl.alpha);
}

/** Analog prototype of an iron weighting filter.

    |W(fRef)| = 1 by construction: the emitted gainDb carries exactly the
    normalisation the fit produced.

    Takes the whole `WeightingFilter` rather than three loose arguments. The
    sections, their count and the normalising gain belong to each other, and
    passing them separately is how you end up running W2's sections with W's
    gain: the right shape at the wrong level, which is a flat dB error in the
    harmonic it feeds and the hardest error shape for a frequency-law gate to
    catch. Bundled, that call cannot be written.
*/
inline AnalogZPK buildWeightingZPK(const coeffs::WeightingFilter& wf) noexcept
{
    AnalogZPK zpk;
    double k = dbToLin(wf.gainDb);

    for (int i = 0; i < wf.numSections; ++i)
    {
        const auto& sec = wf.sections[i];
        const double p = -2.0 * kPi * sec.poleHz;
        zpk.addPole(p);
        k *= -p;
        if (sec.zeroHz > 0.0)
        {
            const double z = -2.0 * kPi * sec.zeroHz;
            zpk.addZero(z);
            k /= -z;
        }
    }

    zpk.gain = k;
    return zpk;
}

/** The odd harmonic's weighting filter W. */
inline AnalogZPK buildSidechainZPK() noexcept
{
    return buildWeightingZPK(coeffs::kIronW);
}

/** The even harmonic's weighting filter W2, at its own steeper exponent. */
inline AnalogZPK buildSidechain2ZPK() noexcept
{
    return buildWeightingZPK(coeffs::kIronW2);
}

/** Published h2 law: the even-harmonic ratio h2/h1 at frequency f and drive ax.

    Counterpart to ironR3PowerLaw. Unlike h3 this is NOT level-independent - the
    depth tracks drive, which is the whole point of the second filter.
*/
inline double ironR2PowerLaw(double f, double axLin) noexcept
{
    const double freqTerm = std::pow(f / coeffs::kIronPowerLaw.fRefHz,
                                     -coeffs::kIronH2Alpha);
    const double driveTerm = std::pow(axLin / coeffs::kIronH2.axRefLin,
                                      coeffs::kIronH2.slope);
    return coeffs::kIronH2.d2Ref * freqTerm * driveTerm;
}

//==============================================================================
// Noise

/** Analog prototype of the 1/f tail alone: |H| ~ sqrt(fc/f) below fc, nothing
    above it. The flat term is a separate white generator summed with this.

    A +10 dB/decade rise is half a pole per decade, approximated by pole/zero
    pairs spaced `ratio` apart with the zero at sqrt(ratio) — each pair spends
    half its span falling and half flat, averaging the right slope. A closing
    pole at fc stops the tail contributing above the corner, where the flat term
    already accounts for the whole floor.
*/
inline AnalogZPK buildNoiseTailZPK() noexcept
{
    const auto& n = coeffs::kNoise;
    const double fc    = n.lfCornerHz;
    const double fMin  = fc / std::pow(10.0, n.lfDecades);
    const double ratio = std::pow(fc / fMin, 1.0 / static_cast<double>(n.lfSections));

    AnalogZPK zpk;
    for (int i = 0; i < n.lfSections; ++i)
    {
        const double pole = fMin * std::pow(ratio, static_cast<double>(i));
        zpk.addPole(-2.0 * kPi * pole);
        zpk.addZero(-2.0 * kPi * pole * std::sqrt(ratio));
    }
    zpk.addPole(-2.0 * kPi * fc);       // closing pole

    // Normalise numerically against the target inside the tail rather than by
    // hand-multiplying corner frequencies, which is where the sign and the
    // reciprocal both hide.
    zpk.gain = 1.0;
    const double fRef = fc / 10.0;
    zpk.gain = std::sqrt(fc / fRef) / dbToLin(zpk.magnitudeDb(fRef));
    return zpk;
}

} // namespace pre35
