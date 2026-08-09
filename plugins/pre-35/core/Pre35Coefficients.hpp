// Pre35Coefficients.hpp - GENERATED FILE. DO NOT HAND-EDIT.
//
// Emitted by tools/m35/emit_coeffs.py (dusk-audio-tools, private repo) from a
// fitted M-35 channel model. Regenerate it, never patch it:
//
//     python3 emit_coeffs.py --model model_ch1.json
//                            --out <plugins-repo>/plugins/pre-35/core/Pre35Coefficients.hpp
//
// Provenance
//   model file    : model_ch1.json
//   model sha256  : fdc86650dc5f2aeb1273d8691dee386d444d15bd70567dcb4f952d34d7785551
//   schema        : m35-channel-model v1
//   channel       : 1
//   model created : 2026-08-08T22:53:09+00:00
//   capture       : ch1-20260807
//   emitter       : emit_coeffs.py v2
//   header sha256 : 54325c231869117fa92f75f36c6de1f41671c40c4bb40b698ff506e5bebb66f9
//                    ^ sha256 of THIS FILE with the 64 hex chars above
//                      replaced by zeros. The model hash says which fit the
//                      numbers came from; this one says nobody has edited
//                      them since. Check it without needing the model:
//                        python3 emit_coeffs.py --verify-self <this file>
//
// Everything below is ANALOG: corner frequencies in Hz, depths in dB, ratios
// dimensionless. The core bilinear-transforms them at run time for the actual
// (oversampled) rate, so this header is sample-rate independent by construction.
// Do NOT add z-domain coefficients to it - the transformer corners sit above
// 30 kHz and the amplifier's GBW shelf corner moves with the trim knob.

#pragma once

namespace pre35 {
namespace coeffs {

//==============================================================================
// Measurement conventions. "cal" gain is the median of |H1| over 200 Hz-2 kHz;
// the number quoted on the bench is cal + kRawMinusCalDb.
inline constexpr double kRawMinusCalDb    = 3.03;
inline constexpr double kMidbandLoHz      = 200.0;
inline constexpr double kMidbandHiHz      = 2000.0;
inline constexpr int    kMidbandGridPoints = 64;
inline constexpr double kResponseBandLoHz = 20.0;
inline constexpr double kResponseBandHiHz = 18000.0;

//==============================================================================
// Trim taper: a two-parameter logistic in dB-vs-knob, pinned to both hard stops
// so it is exact at 0 % and 100 % (the only zero-uncertainty measurements) and
// monotone in between.
//
//   u    = knobPercent / 100
//   s(v) = 1 / (1 + exp(-k (v - u0)))
//   dB   = g0 + (g1 - g0) * (s(u) - s(0)) / (s(1) - s(0))
struct TaperParams
{
    double g0Db;   ///< cal gain at the fully counter-clockwise stop
    double g1Db;   ///< cal gain at the fully clockwise stop
    double k;      ///< logistic steepness
    double u0;     ///< logistic midpoint, in knob fraction
};

inline constexpr TaperParams kTaper {
    33.41120149774905,
    58.20367036483841,
    5.301242530210118,
    0.49397237349048284
};

//==============================================================================
// Pads. Measured constants, not fitted, and they sit AHEAD of the input
// transformer - engaging the pad really does buy that much less iron
// distortion. The taper above is a single pad-0-referenced curve.
struct PadEntry
{
    int    labelDb;    ///< the number silkscreened on the switch
    double offsetDb;   ///< measured cal-gain offset it costs
};

inline constexpr int kNumPads = 3;

inline constexpr PadEntry kPads[kNumPads] = {
    {  0, 0.0 },
    { 20, -19.86 },
    { 40, -39.27 },
};

//==============================================================================
// Response. Three first-order analog sections per setting:
//   * hpHz       input transformer loaded by the pad (highpass, per pad)
//   * lpHz       transformer's own HF corner (lowpass, per pad)
//   * GBW shelf  amplifier running out of loop gain. Corner = gbwHz / ampGain,
//                so it MOVES WITH TRIM; depth is capped at gbwShelfDb, which is
//                what lets the top octave flatten instead of falling forever.
//                Inactive when gbwShelfDb <= 0.
//
// The chain is normalised to 0 dB at the midband median, exactly the way the
// measurement was, so response and taper gain never double-count.
struct ResponseParams
{
    double hpHz;
    double lpHz;
    double gbwHz;
    double gbwShelfDb;
};

// Indexed the same as kPads.
inline constexpr ResponseParams kResponse[kNumPads] = {
    // pad  0
    {
        3.8695973446911345,
        33564.387652588906,
        5579429.048916538,
        0.19264930189131463
    },
    // pad 20  (GBW shelf inactive in the fit)
    {
        10.366542482920968,
        33526.83621398501,
        560519959.7637541,
        1.249986664453953e-28
    },
    // pad 40
    {
        8.851794304959705,
        56751.6427039282,
        7465339.209220147,
        0.7336097600572383
    },
};

//==============================================================================
// Iron: constant-percentage odd hysteresis, level-tracked Chebyshev shaper.
//
//   w  = W(x)                    LF weighting, |W(fRef)| = 1
//   Aw = sqrt(2 <w^2>)           peak-equivalent envelope of w
//   u  = w / Aw                  unit-amplitude drive
//   y  = x + Aw (d3 (4u^3 - 3u) + d2 (2u^2 - 1))
//
// For a sine, 4u^3-3u is exactly -sin(3wt) and 2u^2-1 exactly -cos(2wt): pure
// harmonics with NO fundamental term, so the layer cannot shift the gain it is
// bolted onto. Dividing by Aw and multiplying it back is what buys level
// independence; W is what buys the f^-alpha frequency law.
struct IronPowerLaw
{
    double r3Ref;     ///< h3/h1 at fRefHz, linear (this is also d3)
    double alpha;     ///< R3 falls as f^-alpha
    double fRefHz;
    double fShelfHz;  ///< subsonic guard; without it the law is unbounded at DC
};

inline constexpr IronPowerLaw kIronPowerLaw {
    0.002616349598459667,
    1.5373916208378229,
    20.0,
    3.0
};

// d2 = d3 * 10^(-kIronH2OffsetDb/20). MEASURED MEDIAN, not identified - h2 sat
// at a near-constant absolute level across the band, so this tracks h3 and is a
// LOWER bound on the real offset (contamination can only raise h2).
inline constexpr double kIronH2OffsetDb = 18.77313232657533;

// The LF weighting filter W, as a cascade of first-order analog sections.
// zeroHz <= 0 means the section is a bare pole (no zero).
struct SidechainSection
{
    double poleHz;
    double zeroHz;
};

inline constexpr int kNumSidechainSections = 5;

inline constexpr SidechainSection kSidechain[kNumSidechainSections] = {
    { 2.4008340609177905, 16.230265144460365 },
    { 6.494107482532746, 93.15702598843825 },
    { 36.34134799053587, 541.6912136209138 },
    { 209.83763850962495, 3874.5443483577765 },
    { 1251.7692303769134, 0.0 },
};

// Overall gain of W. Together with the sections it gives |W(fRefHz)| = 1.
inline constexpr double kSidechainGainDb = 25.65731184498781;

// Envelope detector: one-pole mean-square of the weighted sidechain.
inline constexpr double kDetectorTauS     = 0.25;
inline constexpr double kDetectorFloorDbfs = -120.0;

// Band the constant-percentage law was actually measured over, and the drive
// window it holds across. Outside these the layer is EXTRAPOLATION, not fit.
inline constexpr double kIronValidLoHz = 20.0;
inline constexpr double kIronValidHiHz = 100.0;

//==============================================================================
// Noise. Input-referred to the AMPLIFIER input (post-pad), which is why the pad
// does not attenuate it and the trim does scale it. Flat above the corner, 1/f
// below: PSD ~ N0^2 (1 + fc/f). The tail is realised as pole/zero pairs spaced
// `ratio` apart with the zero at sqrt(ratio) - each pair spends half its span
// falling and half flat, averaging +10 dB/decade - plus a closing pole at fc.
struct NoiseParams
{
    double inputReferredDbfs;  ///< broadband RMS, input-referred
    double lfCornerHz;
    double lfDecades;          ///< how far below the corner the tail is modelled
    int    lfSections;         ///< pole/zero pairs spanning those decades
};

inline constexpr NoiseParams kNoise {
    -108.5,
    50.0,
    2.0,
    3
};

} // namespace coeffs
} // namespace pre35
