// Pre35Coefficients.hpp - GENERATED FILE. DO NOT HAND-EDIT.
//
// Emitted by tools/m35/emit_coeffs.py (dusk-audio-tools, private repo) from a
// fitted M-35 channel model. Regenerate it, never patch it:
//
//     python3 emit_coeffs.py --model model_ch1.json
//                            --out <plugins-repo>/plugins/pre-35/core/Pre35Coefficients.hpp
//
// Provenance
//   model file    : model_ch1_send.json
//   model sha256  : 66dd243f5a81e39d3223336bceb5788761ac83fcb8b39c14e054edea0e66cfe1
//   schema        : m35-channel-model v1
//   channel       : 1
//   model created : 2026-08-11T10:56:38+00:00
//   capture       : ch1-send-20260810
//   emitter       : emit_coeffs.py v2
//   header sha256 : 284b8db482a5e600632456b63b350db72e40271867c27124fcff084fabe458f2
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
inline constexpr double kRawMinusCalDb    = 0.067;
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
    29.87385702687847,
    54.65603237787491,
    5.298505312900655,
    0.49399287749489673
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
    { 20, -19.843 },
    { 40, -39.382 },
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
    // pad  0  (GBW shelf inactive in the fit)
    {
        3.9587193514213794,
        26444.574785917142,
        842344900.1484587,
        1.155714964394195e-28
    },
    // pad 20
    {
        8.375926258533413,
        27858.228923394447,
        2515997.8914972856,
        0.13577205709029933
    },
    // pad 40
    {
        6.663804290064796,
        40626.86062194506,
        4029800.8037794675,
        0.6963049273116398
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

//------------------------------------------------------------------------------
// W2, the SECOND weighting filter, for the even harmonic.
//
// h2 needs its own frequency law: it falls at alpha2 (about -14 dB/oct) where
// h3 falls at alpha3 (about -9.3). Sharing W would force both to the same
// slope and get h2 wrong by ~10 dB across the band. Same machinery, steeper
// target, its OWN normalising gain - see kIronW2 below, which binds the two
// together so they cannot be mixed up.
inline constexpr int kNumSidechain2Sections = 5;

inline constexpr SidechainSection kSidechain2[kNumSidechain2Sections] = {
    { 2.6277056050233636, 24.828124268888725 },
    { 2.6278993643418462, 216.9497854037987 },
    { 11.910685935483981, 2063.2230445791324 },
    { 102.96762183907734, 0.0 },
    { 919.7516395437851, 0.0 },
};

// Overall gain of W2. Gives |W2(fRefHz)| = 1, the same convention as W.
inline constexpr double kSidechain2GainDb = 39.18013504295066;

// A weighting filter is its sections, their count AND its own normalising
// gain. Passing those three separately invites using W2's sections with W's
// gain, which yields the right shape at the wrong level - a flat dB error in
// h2, the one error shape a frequency-law gate is least likely to catch.
// Bundled, that combination cannot be written down.
struct WeightingFilter
{
    const SidechainSection* sections;
    int    numSections;
    double gainDb;
};

inline constexpr WeightingFilter kIronW  { kSidechain,  kNumSidechainSections,  kSidechainGainDb };
inline constexpr WeightingFilter kIronW2 { kSidechain2, kNumSidechain2Sections, kSidechain2GainDb };

// The even harmonic's DEPTH tracks drive, where the odd harmonic's does not.
// d2Eff = d2Ref * (ax / axRefLin)^slope, with `ax` the envelope of the
// UNWEIGHTED signal at the transformer.
//
// `ax` must NOT be the W2-weighted envelope. That is the obvious-looking
// choice and it lands the frequency dependence twice, making the effective
// exponent alpha2*(1+slope) instead of alpha2 - measured as a flat 16.7 dB
// error at 50 Hz while the reference was being built. Flux is set by the
// actual level at the transformer, not by a weighted copy of it.
//
// slope is the RATIO slope: the harmonic's own amplitude rises 1+slope dB
// per dB, which is the square law the bench measured.
struct IronH2Law
{
    double d2Ref;     ///< h2/h1 at fRefHz when ax == axRefLin
    double slope;     ///< dB of h2/h1 per dB of drive
    double axRefLin;  ///< drive the law is referenced to, in internal units
};

inline constexpr IronH2Law kIronH2 {
    7.981948715840945e-05,
    0.9136891732194938,
    0.17080477200597077
};

// The exponent h2 falls at, for the published-law helper and the gates.
inline constexpr double kIronH2Alpha = 2.3430315974049813;

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

//==============================================================================
// The mic amp clipping into its supply rails. Memoryless and frequency-
// independent by measurement: identical at 315 Hz, 1 kHz and 5 kHz to 0.03 dB
// of gain and 0.5 dB of h3, so there is no slew limiting to model. The corner
// is hard - flat to 0.000 dB one dB below onset, h3 up 55 dB across that dB.
// The two thresholds differ because the measured h2 does not vanish.
//
// PLUGIN INPUT MAPPING. The threshold is quoted at the amp OUTPUT in dBu, so
// the core needs a reference joining dBu to the plugin's dBFS. It is:
//
//     0 dBFS in  ->  kInputDbuAt0dBFS at the transformer input
//
// chosen so that at pad 0 with the trim at its CCW stop, a nominal -18 dBFS
// DAW signal (+4 dBu) produces the console's own nominal -10 dBV (-7.78 dBu)
// at its output. That puts the transformer 2.65 dB under its -35 dBu OEM
// ceiling, which is where a console at its operating point belongs. The
// Tascam is -10 dBV prosumer gear, so it runs 11.78 dB below DAW nominal and
// the mapping carries that offset rather than equating dBu to dBu.
//
// Anchored at the TRANSFORMER INPUT, deliberately. Anchoring at the output
// instead pins the amp node to the plugin's own output level, so the drive
// can never change and the trim becomes a null knob.
inline constexpr double kInputDbuAt0dBFS  = -19.65;
inline constexpr double kRailThresholdDbu = 18.926;

// Amp-output magnitudes in the core's internal units, i.e. relative to a
// 0 dBFS plugin input carried through kInputDbuAt0dBFS. Derived here so the
// core never repeats the arithmetic.
inline constexpr double kRailPosLinear    = 84.87895030072433;
inline constexpr double kRailNegLinear    = 82.99080935545712;

} // namespace coeffs
} // namespace pre35
