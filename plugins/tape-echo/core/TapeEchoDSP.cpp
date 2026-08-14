// TapeEchoDSP.cpp — vintage three-head tape echo emulation core (framework-free C++17).

#include "TapeEchoDSP.hpp"
#include "DuskDenormals.hpp"   // ScopedFlushDenormals (SSE + ARM64 FPCR)

#include <algorithm>

namespace duskaudio
{

namespace
{
    // 4-point, 3rd-order Hermite. Interpolates between x0 and x1 at `frac`.
    inline float hermite(float frac, float xm1, float x0, float x1, float x2) noexcept
    {
        const float c = 0.5f * (x1 - xm1);
        const float v = x0 - x1;
        const float w = c + v;
        const float a = w + v + 0.5f * (x2 - x0);
        const float b = w + a;
        return (((a * frac - b) * frac + c) * frac) + x0;
    }

    inline int nextPowerOfTwo(int n) noexcept
    {
        int p = 1;
        while (p < n)
            p <<= 1;
        return p;
    }

    // Mode matrix: which read heads and whether the spring tank is active.
    struct ModeConfig { float h1, h2, h3, reverb; };

    constexpr ModeConfig kModeTable[TapeEchoDSP::kNumModes] =
    {
        { 1, 0, 0, 0 },   // 1:  Head 1
        { 0, 1, 0, 0 },   // 2:  Head 2
        { 0, 0, 1, 0 },   // 3:  Head 3
        { 0, 0.724f, 0.724f, 0 }, // 4: Heads 2 + 3
        { 1, 0, 0, 1 },   // 5:  Head 1 + Reverb
        { 0, 1, 0, 1 },   // 6:  Head 2 + Reverb
        { 0, 0, 1, 1 },   // 7:  Head 3 + Reverb
        { 0.724f, 0.724f, 0, 1 }, // 8: Heads 1 + 2 + Reverb
        { 0, 0.724f, 0.724f, 1 }, // 9: Heads 2 + 3 + Reverb
        { 0.724f, 0, 0.724f, 1 }, // 10: Heads 1 + 3 + Reverb
        { 0.596f, 0.596f, 0.596f, 1 }, // 11: all heads + Reverb
        { 0, 0, 0, 1 },   // 12: Reverb only
    };

    // Hosted single-head trims. Multi-head attenuation lives in the mode table
    // above and keeps the summed programs near the single-head loudness.
    constexpr float kHeadTrim[3] = { 1.0f, 1.122f, 0.958f };

    // Multi-head loop-gain normalization, indexed by ACTIVE HEAD COUNT. The
    // derivation and the measured sweep are at the setTarget site in
    // refreshBlockRateControls(); this lives here so prepare() can snap the
    // smoother to the initial mode without duplicating the table.
    constexpr float kLoopHeadNorm[4] = { 1.0f, 1.0f, 1.0f, 0.93f };

    constexpr float loopHeadNormForMode(const ModeConfig& m) noexcept
    {
        return kLoopHeadNorm[(m.h1 > 0.0f ? 1 : 0)
                           + (m.h2 > 0.0f ? 1 : 0)
                           + (m.h3 > 0.0f ? 1 : 0)];
    }

    // Transport motion. Flutter frequency follows tape speed; its depth grows
    // toward the slow end of the motor range. The second harmonic gives the
    // measured capstan cycle its slightly non-sinusoidal shape.
    constexpr float kWowHz       = 0.5f;
    constexpr float kFlutterMaxHz= 3.857f;
    constexpr float kWowDepth    = 0.0008f;
    constexpr float kNoiseDepth  = 0.0004f;

    // Scrape flutter. The reference's flutter band (6-100 Hz) is stochastic,
    // not a tone: band noise centred near 6 Hz, not a harmonic of the loop
    // rate. Modelled as white noise through a resonant band-pass plus a
    // one-pole skirt, so the resulting speed deviation (which differentiates
    // the delay modulation, i.e. tilts +6 dB/oct) peaks just above 6 Hz and
    // then falls away instead of running flat to Nyquist.
    constexpr float kFlutterNoiseHz    = 6.0f;
    constexpr float kFlutterNoiseQ     = 2.0f;
    constexpr float kFlutterNoiseLPHz  = 9.0f;
    constexpr float kFlutterNoiseDepth = 0.00515f;
    // Age coupling. Measured reference flutter grows 1.00 / 1.76 / 3.36 over
    // Loop Age 0 / 0.5 / 1.0. The common `wf` factor below already contributes
    // (1 + 0.20*age), so this polynomial carries the remainder.
    constexpr float kFlutterAgeLin = 0.589f;
    constexpr float kFlutterAgeSq  = 1.214f;

    // Captured Galaxy cartridges expose a quiet 60 Hz odd-harmonic bed. The
    // fifth harmonic is strongest; the others are normalized to it from the
    // hosted New/Used/Old noise spectra. A separate, much quieter random layer
    // supplies the residual tape/electronics texture above these lines.
    constexpr float kAgeHumHarmonicGain[7] =
        { 0.213f, 0.610f, 1.0f, 0.501f, 0.274f, 0.118f, 0.076f };
    constexpr float kAgeHumGain  = 0.000000923f;
    constexpr float kAgeBedGain  = 0.00000647f;
    constexpr float kAgeHissGain = 0.000001637f;

    constexpr float kTwoPi = 6.28318530717958647692f;

    // clap-validator exercises fractional sample rates down to 1 kHz. Keep
    // every bilinear-transform design safely below Nyquist; this is a no-op
    // throughout the plug-in's normal supported audio-rate range.
    //
    // Thin alias over the ONE shared implementation (DuskFilters.hpp) rather
    // than a private copy. The copy this replaces clamped ceiling-first /
    // floor-last, the ordering the shared helper rejects: with the reverse
    // order the 1 Hz floor wins once fs * 0.45 drops below 1 Hz and hands the
    // designer a corner at or above Nyquist, the exact instability the guard
    // exists to prevent. Harmless here (it needs fs < 2.22 Hz) but it was the
    // last instance of the idiom in the tree, and the one a future reader would
    // have copied.
    //
    // The ceiling stays 0.45 * fs, NOT the shared designers' 0.4998: this core
    // has always used 0.45 and the highest frequency any of these call sites
    // asks for is ~13.8 kHz fixed / ~11.6 kHz computed, so the clamp is inert at
    // every rate at or above 25.9 kHz. It now evaluates in double rather than
    // float, which can move the clamped value by an ulp, but only at rates where
    // it engages at all -- below 25.9 kHz, outside the byte-compare matrix.
    constexpr double kTapeEchoDesignFreqRatio = 0.45;

    inline float safeBiquadFrequency(double sampleRate, float frequency) noexcept
    {
        return (float) nyquistSafeDesignHzD(sampleRate, (double) frequency,
                                            kTapeEchoDesignFreqRatio);
    }

    // DCBlocker's default pole (R = 0.9975) is FIXED, so its corner rides the
    // sample rate: ~19 Hz at 48 kHz but ~38 Hz at 96 kHz and ~76 Hz at 192 kHz,
    // audibly thinning the low end on high-rate sessions. Driving it from a
    // cutoff instead makes the corner track fs. This value is the exact cutoff
    // that reproduces R = 0.9975 at 48 kHz (-48000*ln(0.9975)/2pi), so the
    // calibrated 48 kHz response is preserved bit-for-bit.
    constexpr float kDcBlockerCutoffHz = 19.122506f;

    // Sample rate the spring dispersion bank was fitted at. Only the radii
    // need rate compensation; the centre frequencies are already handled by
    // theta = 2*pi*f/fs.
    constexpr double kSpringDispersionFitFs = 48000.0;
}

float TapeEchoDSP::leadingHeadRatioForMode(int mode1to12) noexcept
{
    const auto& mode = kModeTable[clampInt(mode1to12, 1, kNumModes) - 1];
    if (mode.h1 > 0.0f) return kHeadRatio[0];
    if (mode.h2 > 0.0f) return kHeadRatio[1];
    if (mode.h3 > 0.0f) return kHeadRatio[2];
    return 1.0f;
}

//==============================================================================
// Oversampled preamp — halfband taps now shared (plugins/shared-dpf/dsp/
// DuskOversampler.hpp, duskaudio::hbtaps::kA/kB; scipy remez, identical values).
//==============================================================================
float TapeEchoDSP::preampOversampled(Channel& ch, float x, float drive) noexcept
{
    // 4x oversampled saturation: upsample (2 cascaded 2x halfbands), shape,
    // decimate through the same filters. Zero-stuffed interpolation needs
    // the x2 gain on each upsampling stage.
    for (int p = 0; p < 2; ++p)
    {
        ch.upA.push(p == 0 ? x : 0.0f);
        const float a = 2.0f * ch.upA.out(hbtaps::kA);
        for (int q = 0; q < 2; ++q)
        {
            ch.upB.push(q == 0 ? a : 0.0f);
            const float b = 2.0f * ch.upB.out(hbtaps::kB);
            ch.downB.push(preampShape(b * drive));
        }
        ch.downA.push(ch.downB.out(hbtaps::kB));
    }
    return ch.downA.out(hbtaps::kA);
}

//==============================================================================
// (ShelfFilter coefficient design moved to shared Biquad::shelfSlope1 — see
//  plugins/shared-dpf/dsp/DuskFilters.hpp. Bit-identical float op-order.)
//==============================================================================

//==============================================================================
// SpringReverb
//==============================================================================
void SpringReverb::Spring::prepare(double fs, float oneWaySeconds,
                                   float dispersionScale,
                                   float reflectionAmount, float gain)
{
    const int delaySamples =
        std::max(16, (int)std::lround(oneWaySeconds * fs));
    outgoingDelay.assign((size_t)delaySamples, 0.0f);
    returningDelay.assign((size_t)delaySamples, 0.0f);
    outgoingWriteIdx = 0;
    returningWriteIdx = 0;
    driverReflection = -reflectionAmount;
    pickupReflection = -reflectionAmount;
    outputGain = gain;

    // One-way dispersion, FITTED to the reference tank rather than assumed.
    //
    // The measured reference (probe_spring_dispersion.py, first-packet phase
    // slope) is NOT a broadband downward chirp: it is flat within 1.5 ms below
    // 1.8 kHz, rises to a narrow +21.2 ms hump at 3.6 kHz, and collapses back
    // to zero by 4.2 kHz. An UPWARD chirp. The previous log-sweep bank from
    // 100 Hz to 3.5 kHz ran the other way (+35 ms at 1 kHz, falling), which is
    // what put the onset slope at 9.03 dB/ms against the reference's 3.28 and
    // seated the tail resonance 325 Hz low.
    //
    // Every second-order allpass obeys the exact identity  integral of its
    // group delay over [0, pi) = 2*pi , independent of radius and centre. The
    // delay area a cascade can deliver is therefore fixed by the section COUNT
    // alone and the fit only chooses where to spend it -- which also sets the
    // hard lower bound of ~23 sections for this hump. Centres and radii below
    // come from a constrained least-squares fit to the measurement
    // (fit_spring_dispersion.py: RMS 0.41 ms, max 1.09 ms, peak 21.12 ms at
    // 3590 Hz, zero ripple, verified against the real filter with tone bursts).
    // Do not hand-edit them; re-run the fitter.
    static constexpr float kDispersionCentreHz[kNumDispersionSections] = {
        2040.65f, 2368.04f, 2546.26f, 2683.92f,
        2800.40f, 2902.19f, 2993.10f, 3075.36f,
        3149.81f, 3218.83f, 3281.80f, 3340.32f,
        3395.19f, 3447.18f, 3497.33f, 3545.71f,
        3592.69f, 3638.11f, 3686.99f, 3739.05f,
        3794.78f, 3855.77f, 3928.71f, 4023.25f,
    };
    static constexpr float kDispersionRadius48k[kNumDispersionSections] = {
        0.982972f, 0.985042f, 0.987679f, 0.989309f,
        0.990338f, 0.991239f, 0.991859f, 0.992565f,
        0.993083f, 0.993496f, 0.993931f, 0.994288f,
        0.994608f, 0.994782f, 0.994970f, 0.995158f,
        0.995390f, 0.995144f, 0.994978f, 0.994795f,
        0.994638f, 0.994049f, 0.993247f, 0.991808f,
    };
    // Peak group delay in SECONDS is (1+r)/((1-r)*fs), so a radius held fixed
    // across rates would halve the dispersion at 96 kHz. Hold (1-r) inversely
    // proportional to fs instead; exactly the fitted value at 48 kHz.
    const double radiusRateScale = kSpringDispersionFitFs / fs;
    for (size_t i = 0; i < outgoingDispersion.size(); ++i)
    {
        const double radius = std::min(
            0.99995,
            1.0 - (1.0 - (double)kDispersionRadius48k[i]) * radiusRateScale);
        const float centreHz = dispersionScale * kDispersionCentreHz[i];
        const float theta = kTwoPi
            * safeBiquadFrequency(fs, centreHz) / (float)fs;
        const float radial = -2.0f * (float)radius * std::cos(theta);
        const float radiusSquared = (float)(radius * radius);
        const BiquadCoeffs coeffs {
            radiusSquared, radial, 1.0f, radial, radiusSquared
        };
        outgoingDispersion[i].setCoeffs(coeffs);
        returningDispersion[i].setCoeffs(coeffs);
    }

    // Loss belongs at the termination: every completed trip is progressively
    // darker, while the first outgoing chirp retains the transducer bandwidth.
    //
    // ROUND-TRIP LOSS MODEL -- read this before touching any constant below.
    // These four filters run ONCE per round trip, at the pickup termination
    // only (see Spring::process): the driver termination applies the bare
    // scalar driverReflection with no filtering. So the round-trip gain is
    //     G(f) = reflectionAmount^2 * |H_filters(f)|
    // -- the SCALAR is squared, the FILTERS are not. An earlier comment here
    // claimed |R| was the square of "these filters plus reflectionAmount",
    // and the loss shape fitted against that claim therefore delivered only
    // about half of its own intended low-frequency correction. That is a
    // measured error, not a theoretical one; see below.
    //
    // Loss SHAPE, solved per band from measured decay rather than dialled.
    // With  L(f) = 60 * T_roundtrip / T60(f)  in dB per round trip and a mean
    // round trip of 51.53 ms (springs at 45.6 / 52.2 / 56.8 ms), causal
    // third-octave decay fits against the reference at 44.1 kHz gave:
    //
    //     band     L ours    L ref     needed
    //     200 Hz    1.167    1.027    -0.140 dB
    //     250 Hz    1.162    1.017    -0.145
    //     315 Hz    1.149    0.997    -0.152
    //     400 Hz    1.145    0.994    -0.151
    //     630 Hz    1.093    1.007    -0.086
    //     1000 Hz   1.081    1.052    -0.029
    //
    // i.e. ~0.15 dB LESS loss per round trip below ~500 Hz, tapering to zero
    // by ~1.2 kHz. Bands above 1.6 kHz are deliberately excluded from this
    // fit: the fitted dispersion adds up to 21 ms of one-way delay at 3.6 kHz,
    // so T_roundtrip is not 51.53 ms up there and the simple inversion does
    // not apply. The reference also decays FASTER than this tank above
    // 1.6 kHz (2.12 s vs 2.68 s at 4 kHz), which is a separate correction and
    // is deliberately left for its own measured pass.
    // 49.5, not 44. With the shelf below corrected, 100-160 Hz overshot the
    // reference's decay (100 Hz ran 2.77 s against its 2.56). The residual
    // needed is +0.092 dB of extra round-trip loss at 100 Hz, +0.041 at 125,
    // ~0 by 200; solving this second-order corner against that curve lands on
    // 49.5 Hz (RMS residual 0.009 dB). A high-pass skirt is too steep to also
    // supply the +0.035 dB wanted at 160 Hz, which is left 0.06 s long rather
    // than distorting the corner to chase it.
    reflectionHighPass.setCoeffs(Biquad::highPass(
        fs, safeBiquadFrequency(fs, 49.5f), 0.70710678f));
    // +0.05, not -0.10: a net +0.15 dB per round trip below the corner, which
    // is the measured deficit above. This is a small BOOST inside the loop and
    // that is safe here -- the round-trip gain stays lossy at every frequency
    // because reflectionAmount^2 is at most 0.9472^2 = 0.8972 (-0.943 dB), so
    // the worst-case loop gain is 0.8972 * 10^(0.05/20) = 0.9024 < 1, before
    // the high-pass and low-pass take their own further bite.
    reflectionLowLoss.setCoeffs(Biquad::shelfSlope1(
        fs, safeBiquadFrequency(fs, 550.0f), 0.05f, false));
    // 5.5 kHz rather than 7: the second-order skirt gives -1.03 dB at 4 kHz
    // against the 7 kHz version's -0.44, supplying the extra per-reflection
    // loss the dispersion-inflated 4 kHz round trip needs. Above 6 kHz this
    // is well past the tank's own band and the output ceiling, so the extra
    // attenuation there costs nothing audible.
    reflectionLowPass.setCoeffs(Biquad::lowPass(
        fs, safeBiquadFrequency(fs, 5500.0f), 0.70710678f));
    reflectionAirLoss.setCoeffs(Biquad::shelfSlope1(
        fs, safeBiquadFrequency(fs, 2500.0f), -0.35f, true));
    reset();
}

void SpringReverb::Spring::reset()
{
    std::fill(outgoingDelay.begin(), outgoingDelay.end(), 0.0f);
    std::fill(returningDelay.begin(), returningDelay.end(), 0.0f);
    outgoingWriteIdx = 0;
    returningWriteIdx = 0;
    for (auto& section : outgoingDispersion)
        section.reset();
    for (auto& section : returningDispersion)
        section.reset();
    reflectionHighPass.reset();
    reflectionLowLoss.reset();
    reflectionLowPass.reset();
    reflectionAirLoss.reset();
}

float SpringReverb::Spring::process(float x) noexcept
{
    // Read the wave arriving back at the driver termination.
    float returned = returningDelay[(size_t)returningWriteIdx];
    for (auto& section : returningDispersion)
        returned = section.process(returned);

    // The driver launches the input plus the inverted returning wave.
    const float launched = x + driverReflection * returned;
    float farEnd = outgoingDelay[(size_t)outgoingWriteIdx];
    outgoingDelay[(size_t)outgoingWriteIdx] = launched;
    if (++outgoingWriteIdx >= (int)outgoingDelay.size())
        outgoingWriteIdx = 0;
    for (auto& section : outgoingDispersion)
        farEnd = section.process(farEnd);

    // The pickup observes the outgoing wave, then its termination reflects a
    // filtered, inverted copy into the return propagation path.
    float reflected = reflectionHighPass.process(farEnd);
    reflected = reflectionLowLoss.process(reflected);
    reflected = reflectionLowPass.process(reflected);
    reflected = reflectionAirLoss.process(reflected);
    returningDelay[(size_t)returningWriteIdx] =
        pickupReflection * reflected;
    if (++returningWriteIdx >= (int)returningDelay.size())
        returningWriteIdx = 0;

    // The pickup hears the dispersed wave only.
    //
    // A broadband undispersed tap used to be summed in here at +0.6 dB
    // relative to the dispersed wave, on the theory that a tank carries a
    // second, faster propagation mode. The reference does have a
    // non-dispersive branch, but it sits 19-24 dB down and exists only above
    // 4.2 kHz -- where the output ceiling already suppresses it -- not
    // broadband at unity. Summing a full-level copy of the input is the
    // sharpest attack physically available, and it measured as such: onset
    // slope 9.03 dB/ms against the reference's 3.28, plus per-band arrival
    // times that jumped between the two copies depending on which happened to
    // dominate after each band's filtering.
    return outputGain * farEnd;
}

void SpringReverb::prepare(double sampleRate, float detune)
{
    // THREE slightly unequal waveguides model the parallel springs in the tank
    // -- see the `springs` member declaration for why three and not four, and
    // do not restore a fourth without re-reading that argument.
    // The pure delay is the fastest one-way transit; the dispersive allpasses
    // add the frequency-dependent travel time on both the outgoing and return
    // journeys. The first arrivals remain tightly grouped, while successive
    // round trips separate naturally as their chirps accumulate dispersion.
    // Transits measured from the reference's own return series: its envelope
    // peaks fall into three families at odd multiples of the one-way time
    // (70.4/116.2/161.2, 80.4/132.8/184.6, 87.5/144.1/206.9 ms), and
    // regressing each family against 1, 3, 5 recovers 22.8 / 26.1 / 28.4 ms.
    //
    // dispersionScale spread is kept to +/-0.7%, not the former +/-3%: the
    // fitted dispersion is a narrow hump at 3.6 kHz, and scaling the centres
    // +/-3% would smear it across 216 Hz and flatten the very peak the fit was
    // built to produce. The spread exists only to decorrelate the combs.
    // reflectionAmount is applied at BOTH terminations, so a round trip is
    // multiplied by its SQUARE -- the old 0.925 was really 0.856 per trip. Each
    // spring therefore needs its own value: a longer spring makes fewer trips
    // per second and must lose less on each to land on the same decay time.
    //   |R| = 10^(-3 * T_roundtrip / T60_target),  reflectionAmount = sqrt(|R|)
    // against the reference's measured 2.90 s mid-band T60 and the round trips
    // 2 x 22.8 / 26.1 / 28.4 ms.
    springs[0].prepare(sampleRate, 0.0228f * detune,
                       0.993f, 0.9472f, 1.30f);
    springs[1].prepare(sampleRate, 0.0261f * detune,
                       1.000f, 0.9397f, 1.30f);
    springs[2].prepare(sampleRate, 0.0284f * detune,
                       1.007f, 0.9346f, 1.30f);
    dcBlock.setSampleRate(sampleRate, kDcBlockerCutoffHz);
    // Diffuser lengths are a deliberately irrational-ish ratio (1:2.38) so the
    // two stages never line up into a periodic comb.
    {
        constexpr double kDiffuserSeconds[kNumDiffusers] = { 0.0013, 0.0031 };
        for (int i = 0; i < kNumDiffusers; ++i)
        {
            diffusionBuf[(size_t)i].assign(
                (size_t)std::max(1, (int)std::lround(
                    kDiffuserSeconds[i] * sampleRate)), 0.0f);
            diffusionWriteIdx[(size_t)i] = 0;
        }
    }
    inputHP.setCutoff(140.0f, sampleRate);
    inputLP.setCutoff(4200.0f, sampleRate);
    // Real tank transducers reject the sub-bass sharply. A fourth-order
    // Butterworth high-pass supplies the missing lower edge of the measured
    // roughly 100 Hz-5 kHz passband without disturbing the spring body.
    //
    // 60 Hz, not 90. Four low-end removers were stacked here in series --
    // this one, outputHP, outputLowContour and the pre-tank inputHP -- for a
    // combined 7th order where the reference's measured low slope is about
    // 3rd. At 90 Hz the fourth-order skirt was still taking 100-125 Hz with
    // it. Fitting the stack against the measured deficit drove this corner to
    // its 30 Hz search bound; 60 Hz is taken instead, because between 60 and
    // 90 the whole-band mean error only moves 0.71 vs 0.82 dB while 30 Hz
    // would abandon the sub-bass rejection this filter exists for.
    constexpr float kFourthOrderButterworthQ[2] =
        { 0.54119610f, 1.30656296f };
    for (size_t i = 0; i < outputTransducerHP.size(); ++i)
        outputTransducerHP[i].setCoeffs(Biquad::highPass(
            sampleRate, safeBiquadFrequency(sampleRate, 60.0f),
            kFourthOrderButterworthQ[i]));
    // 320 Hz, not 400 -- the dominant lever in the low-end refit. Measured
    // against the reference, this tank was 5.2 dB light at 100 Hz, 3.4 at
    // 125, 3.25 at 160 and 2.45 at 200, and a second-order corner sitting at
    // 400 Hz is what put it there (it alone costs 12 dB at 200 Hz and 20 dB
    // at 125). The figures above are the MEAN of two independent measurements
    // that now agree within about 1 dB: a Logic bounce of program material
    // and a 44.1 kHz impulse render. Predicted mean |error| over 100 Hz-1 kHz
    // after this change: 1.97 -> 0.71 dB, with every band above 1.25 kHz
    // moving less than 0.1 dB.
    outputHP.setCoeffs(Biquad::highPass(
        sampleRate, safeBiquadFrequency(sampleRate, 320.0f), 0.70710678f));
    outputVoiceLP.setCoeffs(Biquad::lowPass(
        sampleRate, safeBiquadFrequency(sampleRate, 1800.0f), 0.70710678f));
    // The pickup transducer is effectively brick-walled above 5 kHz. The
    // steep ceiling is deliberately out of the waveguide so it cannot create
    // or prolong tank modes; unlike the former return, there is no resonant
    // presence boost at its corner.
    for (size_t i = 0; i < outputCeilingLP.size(); ++i)
    {
        const float theta =
            ((2.0f * (float)i + 1.0f) * kDuskPi)
            / (4.0f * (float)outputCeilingLP.size());
        const float q = 1.0f / (2.0f * std::cos(theta));
        // 5250 Hz / 13 sections (26th order), was 5150 / 9 (18th).
        //
        // The corner stays where it is for the reason recorded before: an
        // 18th-order wall at 4900 put its knee on 5.0-5.3 kHz, where the
        // reference still has usable sizzle, and that hole was audible on
        // cymbals. What was WRONG in that note is the claim that "above ~5.7 kHz
        // both are ~-56 dB and inaudible, so the small excess this leaves up
        // there costs nothing". The excess is not small. The reference's spring
        // runs at a reduced internal rate and its output falls off a CLIFF just
        // above 5.5 kHz, far steeper than an 18th-order Butterworth, so we sat
        // hot in exactly the band it abandons.
        //
        // Measured at the user's own A/B state ("AB Spring Match": reverb only,
        // Used tape, Wow & Flutter 0), 1/12-octave, ours minus reference:
        //     5040 Hz  -3.2      5339 Hz  -2.8      5657 Hz   +8.0
        //     5993 Hz +21.9      6350 Hz +19.4      6727 Hz  +16.2
        //     7127 Hz  +2.7      7551 Hz -22.0
        // A +22 dB bump the listener heard as brightness at ~5.9 kHz, and one
        // that EVERY octave gate is blind to (band "8000" spans 5657-11314 Hz
        // and both sides sit >50 dB below midband there, so it is dropped as
        // noise floor). Raising the order steepens the transition where the
        // reference's decimation filter does; the corner moves only 100 Hz so
        // the 5.0-5.3 kHz sizzle above is preserved. Do NOT lower the corner to
        // chase this -- that reopens the cymbal hole. The remaining peak is
        // taken out by outputFizzNotch below.
        outputCeilingLP[i].setCoeffs(Biquad::lowPass(
            sampleRate, safeBiquadFrequency(sampleRate, 5250.0f), q));
    }
    // Residual of the reference's spring cliff that the ceiling above cannot
    // reach. Even at 26th order a Butterworth knee is far gentler than a
    // decimation filter, leaving roughly +13 dB centred near 6 kHz; this takes
    // that peak out without touching 5.0-5.4 kHz, where we are already about
    // 3 dB DARK and must not be cut further. Fitted against the measured
    // 1/12-octave excess listed at the ceiling above (least squares over
    // 4-7.3 kHz, with an explicit penalty on any attenuation below 5.4 kHz).
    // Bands above ~7.3 kHz are deliberately excluded from the fit: the
    // reference's content up there is reconstruction imaging from its own
    // decimation, we are already 22-60 dB below it, and it must never be
    // copied.
    outputFizzNotch.setCoeffs(Biquad::peak(
        sampleRate, safeBiquadFrequency(sampleRate, 6100.0f),
        -7.5f, 6.0f));
    // Static return voicing follows the measured pickup response. These
    // filters are outside the propagation/reflection paths and therefore do
    // not alter the fitted group delay or per-trip decay.
    outputLowContour.setCoeffs(Biquad::peak(
        sampleRate, safeBiquadFrequency(sampleRate, 120.0f),
        -10.0f, 2.5f));
    outputBody.setCoeffs(Biquad::peak(
        sampleRate, safeBiquadFrequency(sampleRate, 1000.0f),
        0.5f, 0.70f));
    // -0.6 dB, not -4.5. Measured 1/3-octave against the reference, the tank
    // sat 3.9 dB dark at 4 kHz and 4.4 dB dark at 5 kHz while 500 Hz-3.15 kHz
    // was already within 1 dB -- a notch, not a tilt, and this seat was the
    // notch. It was dialled in against the OLD downward-chirp dispersion,
    // which piled energy into this band; the fitted upward chirp does not, so
    // the correction is no longer needed.
    outputFourKhzSeat.setCoeffs(Biquad::peak(
        sampleRate, safeBiquadFrequency(sampleRate, 4050.0f),
        -0.6f, 8.0f));
    // Restores the tank's body. Two independent measurements agree that this
    // band is the one audible tone error left: the reference impulse at
    // 44.1 kHz sits 5.5 dB above this tank at 250 Hz, 3.9 at 400 and 2.2 at
    // 630, recovering by 1 kHz, and a DAW bounce of the same program through
    // both plugins shows the same shape at roughly half the depth. The target
    // here is 65% of the impulse figure, because the impulse is the cleaner
    // measurement but overshooting this band is what made an earlier pass
    // sound wrong. Deliberately a peak, not a lower high-pass corner: the
    // bounce puts this tank 4 dB HOT at 125 Hz, so anything that lifts the
    // whole bottom would make that worse. The former -1.5 dB cut at 350 Hz was
    // hand-dialled and was pulling in exactly the wrong direction.
    // The +2.75 dB at 280 Hz this filter originally carried was fitted to a
    // LEVEL deficit at 250-500 Hz that later measurement showed was really a
    // DECAY deficit, since fixed at source in the reflection filters. With
    // the decay correct the boost was over-correcting, and neutralising it
    // to 0 dB left a residual (predicted -2.4 dB at 315, -2.0 at 400: dropping
    // the boost and lowering the outputHP corner very nearly cancel there).
    //
    // 346 Hz / -4.7 dB / Q4, not 0 dB -- a fresh 1/3-octave measurement
    // (mid-anchored 400-2000 Hz, 18 s program bounce, both plugins) confirmed
    // the predicted residual almost exactly: mine +3.06 dB hot at 315, +2.16
    // at 400, both neighbouring bands (160-250, 500-800) already within
    // 0.8 dB. Fit (least-squares against the RBJ peak response, Q bounded to
    // stay a narrow surgical cut rather than a searchable-but-audible notch)
    // lands -2.98 dB at 315 and -1.98 at 400 while moving 250 Hz only -0.6 dB
    // and 500 Hz only -0.5 dB -- the two-band feature the comment above
    // predicted, now closed instead of left neutralised.
    outputLowMidBalance.setCoeffs(Biquad::peak(
        sampleRate, safeBiquadFrequency(sampleRate, 346.0f),
        -4.7f, 4.0f));
    outputOneKhzSeat.setCoeffs(Biquad::peak(
        sampleRate, safeBiquadFrequency(sampleRate, 1000.0f),
        1.0f, 3.0f));
    reset();
}

void SpringReverb::reset()
{
    for (auto& s : springs)
        s.reset();
    for (auto& buf : diffusionBuf)
        std::fill(buf.begin(), buf.end(), 0.0f);
    diffusionWriteIdx.fill(0);
    inputHP.reset();
    inputLP.reset();
    dcBlock.reset();
    for (auto& hp : outputTransducerHP)
        hp.reset();
    outputHP.reset();
    outputVoiceLP.reset();
    for (auto& lp : outputCeilingLP)
        lp.reset();
    outputFizzNotch.reset();
    outputLowContour.reset();
    outputBody.reset();
    outputFourKhzSeat.reset();
    outputLowMidBalance.reset();
    outputOneKhzSeat.reset();
}

float SpringReverb::process(float in) noexcept
{
    const float voiced = inputLP.process(inputHP.process(in));
    float wet = 0.0f;
    for (auto& spring : springs)
        wet += spring.process(voiced);

    // Pickup diffusion (see the member declaration for why). Standard
    // single-multiply allpass per stage, so each is magnitude-flat by
    // construction and the tank's calibration cannot drift through it.
    constexpr float kDiffusionCoeff[kNumDiffusers] = { 0.40f, 0.32f };
    for (int i = 0; i < kNumDiffusers; ++i)
    {
        std::vector<float>& buf = diffusionBuf[(size_t)i];
        int& idx = diffusionWriteIdx[(size_t)i];
        const float delayed = buf[(size_t)idx];
        const float diffused = delayed - kDiffusionCoeff[i] * wet;
        buf[(size_t)idx] = wet + kDiffusionCoeff[i] * diffused;
        if (++idx >= (int)buf.size())
            idx = 0;
        wet = diffused;
    }

    // Output trim is calibrated per channel. The wet path is mono and panned
    // later; measuring an L/R average here would overstate the spring level
    // because the former dual-mono tanks partially cancelled at center.
    // Master wet trim, re-derived AFTER the round-trip loss was solved.
    //
    // It was briefly 1.201 while the tail was still 30% short, purely to put
    // the first-packet strike back after the undispersed pickup tap was
    // removed. Once the per-band loss made the decay correct, that same trim
    // left the tank 2.46 dB hot broadband -- which is the honest order of
    // operations here: decay first, level second, because the integrated level
    // of a reverb is mostly its decay time and only partly its gain.
    float voicedWet = dcBlock.process(0.904f * wet);
    for (auto& hp : outputTransducerHP)
        voicedWet = hp.process(voicedWet);
    voicedWet = outputHP.process(voicedWet);
    voicedWet = outputVoiceLP.process(voicedWet);
    for (auto& lp : outputCeilingLP)
        voicedWet = lp.process(voicedWet);
    voicedWet = outputFizzNotch.process(voicedWet);
    voicedWet = outputLowContour.process(voicedWet);
    voicedWet = outputFourKhzSeat.process(voicedWet);
    const float springReturn = outputOneKhzSeat.process(
        outputLowMidBalance.process(
            outputBody.process(voicedWet)));

    return springReturn;
}

//==============================================================================
// TapeEchoDSP
//==============================================================================
constexpr float TapeEchoDSP::kHeadRatio[3];
constexpr float TapeEchoDSP::kHeadOffsetMs[3];

float TapeEchoDSP::delayMsForRepeatRate(float v01) noexcept
{
    const float x  = clamp01(v01);
    const float x2 = x * x;
    // Monotonic endpoint-constrained fit to hosted head-1 arrivals at five
    // motor positions. A linear knob law was over 10 ms early at midpoint.
    const float slow01 = clamp01(
        1.0f - 2.14728717f * x2
             + 0.90911783f * x2 * x
             + 0.23816934f * x2 * x2
             + 0.06891f * x2 * (1.0f - x) * (1.0f - x));
    return kMinDelayMs + slow01 * (kMaxDelayMs - kMinDelayMs);
}

float TapeEchoDSP::repeatRateForDelayMs(float delayMs) noexcept
{
    const float target = clampF(delayMs, kMinDelayMs, kMaxDelayMs);
    float lo = 0.0f;
    float hi = 1.0f;
    for (int i = 0; i < 24; ++i)
    {
        const float mid = 0.5f * (lo + hi);
        if (delayMsForRepeatRate(mid) > target)
            lo = mid;
        else
            hi = mid;
    }
    return 0.5f * (lo + hi);
}

float TapeEchoDSP::leadingHeadOffsetMsForMode(int mode1to12) noexcept
{
    // Same selection as leadingHeadRatioForMode above, off the same table, so
    // the ratio and its companion offset can never disagree about which head
    // leads. Reverb-only (mode 12) has no active head and falls through to the
    // head-1 entry, which is 0 ms.
    const auto& mode = kModeTable[clampInt(mode1to12, 1, kNumModes) - 1];
    if (mode.h1 > 0.0f) return kHeadOffsetMs[0];
    if (mode.h2 > 0.0f) return kHeadOffsetMs[1];
    if (mode.h3 > 0.0f) return kHeadOffsetMs[2];
    return 0.0f;
}

void TapeEchoDSP::prepare(double sampleRate, int /*maxBlockSize*/)
{
    // Validate BEFORE anything reads it. `sampleRate <= 0.0` alone would let NaN
    // through (every comparison against NaN is false) and +Inf through outright,
    // and this rate then feeds three things that cannot cope: noiseRateComp
    // takes its square root, the tape buffer length converts ceil(seconds * fs)
    // to int (undefined for NaN or Inf), and safeBiquadFrequency documents a
    // finite-positive precondition because fs divides in every designer behind
    // it. 44100 is the member's own default, so a bad rate lands where an
    // un-prepared instance already sits.
    if (! std::isfinite(sampleRate) || sampleRate <= 0.0)
        sampleRate = 44100.0;

    fs = sampleRate;

    // Noise-modulator rate compensation; see kNoiseCalibrationFs in the header.
    // Exactly 1.0f at 48 kHz, so the calibrated render path is bit-identical.
    noiseRateComp = std::sqrt((float)(fs / kNoiseCalibrationFs));

    // Longest possible read: head 3 at the slowest motor speed, plus wow
    // headroom, plus interpolation guard.
    const float maxDelaySec = (kMaxDelayMs * 0.001f) * kHeadRatio[2] * 1.05f;
    const int   needed      = (int)std::ceil(maxDelaySec * fs) + 8;
    const int   tapeLen     = nextPowerOfTwo(needed);
    mask            = tapeLen - 1;
    maxDelaySamples = (float)(tapeLen - 8);
    writeIdx        = 0;

    for (auto& ch : channels)
    {
        ch.tape.assign((size_t)tapeLen, 0.0f);
        ch.preampDC.setSampleRate(fs, kDcBlockerCutoffHz);
        ch.recordHP.setCoeffs(Biquad::highPass(
            fs, safeBiquadFrequency(fs, 115.0f), 0.70710678f));
        const float initialMs = delayMsForRepeatRate(
            pRepeatRate.load(std::memory_order_relaxed));
        const float initialSlow01 = clamp01(
            (initialMs - kMinDelayMs) / (kMaxDelayMs - kMinDelayMs));
        const float initialVoicing = 1.05f + 0.08f * initialSlow01
            + 0.48f * initialSlow01 * (1.0f - initialSlow01);
        ch.speedLP.setCoeffs(Biquad::lowPass(
            fs, safeBiquadFrequency(
                fs, 7200.0f * kMinDelayMs / initialMs * initialVoicing),
            0.70710678f));
        constexpr float kButterworthQ[3] =
            { 0.51763809f, 0.70710678f, 1.93185165f };
        const float initialFast01 = 1.0f - initialSlow01;
        const float initialAntiAliasCutoff =
            9710.0f + 1488.0f * initialFast01
                    + 422.0f * initialFast01 * initialFast01;
        for (int i = 0; i < 3; ++i)
            ch.antiAliasLP[(size_t)i].setCoeffs(Biquad::lowPass(
                fs, safeBiquadFrequency(
                    fs, initialAntiAliasCutoff),
                kButterworthQ[i]));
        // The direct monitor path retains full midrange level while its
        // coupling network and output amplifier gently soften the extremes.
        // These shelves are outside the echo/reverb sends.
        ch.dryLowShelf.setCoeffs(Biquad::shelf(
            fs, safeBiquadFrequency(fs, 46.3316f),
            -6.17579f, 0.492074f, /*high=*/false));
        ch.dryHighShelf.setCoeffs(Biquad::shelf(
            fs, safeBiquadFrequency(fs, 13762.47f),
            -6.13329f, 0.492299f, /*high=*/true));
    }
    spring.prepare(fs, 1.0f);

    noiseLP.setCutoff(2.0f, fs);
    flutterBand.set(kFlutterNoiseHz, kFlutterNoiseQ, fs);
    flutterBandLP.setCutoff(kFlutterNoiseLPHz, fs);
    wowInc     = kTwoPi * kWowHz     / (float)fs;
    flutterInc = kTwoPi * kFlutterMaxHz / (float)fs;
    // The record VU owns the needle ballistics. The UI deliberately adds no
    // second smoothing stage.
    //
    // TRUE VU BALLISTIC: a CRITICALLY DAMPED SECOND-ORDER movement, symmetric.
    //
    // IEC 60268-17 / ANSI C16.5 specify that a suddenly applied 0 VU tone drives
    // the needle to 99% of full deflection in 300 ms. The ORDER matters as much
    // as that number: a moving-coil VU is a mass on a spring with damping, so
    // its step response starts with ZERO slope and eases into motion. Two
    // cascaded one-poles of equal tau give exactly that critically damped shape,
    //     y(t) = 1 - (1 + t/tau) e^(-t/tau)
    // and reaching 99% needs t = 6.638 tau, so tau = 300 ms / 6.638 = 45.2 ms.
    //
    // Do NOT collapse this back to a single pole. A one-pole hits 99% at 300 ms
    // with tau = 65 ms, so it satisfies the spec's headline number while having
    // its MAXIMUM slope at t = 0 -- it lurches on every transient. Measured on a
    // 12 s drum loop, the single-pole version left the needle pinned at 0% between
    // hits, flicking to 20-40% on each one: 24.8 direction reversals per second,
    // mean deflection 4.4%, and it bottomed out in every gap. It reads as
    // twitching rather than swinging with the music.
    //
    // History, so neither mistake is repeated: this was 0.225 / 0.200 s split
    // attack/release, which was ~3x TOO SLOW in both directions (measured 455 ms
    // to fall 0 VU -> -20 VU, ~1.0 s to reach 99% on the rise). Correcting that
    // to a 65 ms single pole fixed the numbers and broke the behaviour, because
    // it was validated against a tone step response and never against program
    // material. Validate any change here on the drum loop, not just a step.
    //
    // Metering only: meterVu is read exclusively to publish recordVu, so no
    // audio sample anywhere in the plugin depends on these constants.
    constexpr float kVuTauSeconds = 0.0452f;
    meterVuCoeff = 1.0f - std::exp(-1.0f / (kVuTauSeconds * (float)fs));
    meterPeakDecayPerSample = std::exp(-1.0f / (0.3f * (float)fs));
    recordEnvelopeAttack =
        1.0f - std::exp(-1.0f / (0.00005f * (float)fs));
    recordEnvelopeRelease = std::exp(-1.0f / (0.12f * (float)fs));
    meterVu = 0.0f;
    meterVuStage1 = 0.0f;
    meterPeak = 0.0f;
    recordVu.store(0.0f, std::memory_order_relaxed);
    recordPeak.store(0.0f, std::memory_order_relaxed);

    delaySmoother.prepare(fs, 0.35f);        // motor/capstan inertia
    intensitySmoother.prepare(fs, 0.03f);
    for (auto& g : headGain)
        g.prepare(fs, 0.015f);
    // Same 15 ms ramp as headGain: the two must move together, or a mode change
    // applies one mode's loop normalization to the other mode's head sum.
    loopHeadNormSmoother.prepare(fs, 0.015f);
    reverbSendSmoother.prepare(fs, 0.015f);
    echoLevelSmoother.prepare(fs, 0.02f);
    reverbLevelSmoother.prepare(fs, 0.02f);
    dryLevelSmoother.prepare(fs, 0.02f);
    outputVolumeSmoother.prepare(fs, 0.02f);
    echoPanSmoother.prepare(fs, 0.02f);
    reverbPanSmoother.prepare(fs, 0.02f);
    inputSendSmoother.prepare(fs, 0.01f);
    mixSmoother.prepare(fs, 0.02f);
    driveSmoother.prepare(fs, 0.02f);
    wowFlutterSmoother.prepare(fs, 0.05f);
    powerSmoother.prepare(fs, 0.03f);
    ageSmoother.prepare(fs, 0.10f);
    hissVoice.setCoeffs(Biquad::lowPass(
        fs, safeBiquadFrequency(fs, 1912.5f), 0.70710678f));
    hissCeiling.setCoeffs(Biquad::shelf(
        fs, safeBiquadFrequency(fs, 9000.0f),
        -6.0f, 1.0f, /*high=*/true));
    ageBedHighPass.setCoeffs(Biquad::highPass(
        fs, safeBiquadFrequency(fs, 118.1f), 3.0f));
    ageBedLowPass.setCoeffs(Biquad::lowPass(
        fs, safeBiquadFrequency(fs, 427.7f), 3.0f));
    wobbleLP.setCutoff(5.0f, fs);
    ageHumRotSin = std::sin(kTwoPi * 60.0f / (float)fs);
    ageHumRotCos = std::cos(kTwoPi * 60.0f / (float)fs);

    // Snap smoothers to current parameter values so prepare() never glides.
    const auto& m = kModeTable[pMode.load(std::memory_order_relaxed) - 1];
    const float t = delayMsForRepeatRate(
        pRepeatRate.load(std::memory_order_relaxed));
    delaySmoother.snap(t * 0.001f * (float)fs);
    intensitySmoother.snap(pIntensity.load(std::memory_order_relaxed));
    headGain[0].snap(m.h1);
    headGain[1].snap(m.h2);
    headGain[2].snap(m.h3);
    loopHeadNormSmoother.snap(loopHeadNormForMode(m));
    reverbSendSmoother.snap(m.reverb);
    echoLevelSmoother.snap(pEchoLevel.load(std::memory_order_relaxed));
    reverbLevelSmoother.snap(pReverbLevel.load(std::memory_order_relaxed));
    dryLevelSmoother.snap(pDryLevel.load(std::memory_order_relaxed));
    outputVolumeSmoother.snap(
        pOutputVolume.load(std::memory_order_relaxed));
    echoPanSmoother.snap(pEchoPan.load(std::memory_order_relaxed));
    reverbPanSmoother.snap(pReverbPan.load(std::memory_order_relaxed));
    inputSendSmoother.snap(pInputSend.load(std::memory_order_relaxed));
    mixSmoother.snap(pMix.load(std::memory_order_relaxed));
    driveSmoother.snap(pInputGain.load(std::memory_order_relaxed));
    wowFlutterSmoother.snap(pWowFlutter.load(std::memory_order_relaxed));
    powerSmoother.snap(1.0f - pBypass.load(std::memory_order_relaxed));
    ageSmoother.snap(pTapeAge.load(std::memory_order_relaxed));
    lastPlaybackCutoff = -1.0f;
    lastAntiAliasCutoff = -1.0f;
    lastAgeContourDb = 999.0f;

    lastBass = lastTreble = -999.0f; // force shelf recompute
    reset();
}

void TapeEchoDSP::reset()
{
    for (auto& ch : channels)
    {
        std::fill(ch.tape.begin(), ch.tape.end(), 0.0f);
        ch.preampDC.reset();
        ch.upA.reset(); ch.downA.reset();
        ch.upB.reset(); ch.downB.reset();
        ch.recordHP.reset();
        ch.speedLP.reset();
        ch.ageContour.reset();
        for (auto& lp : ch.antiAliasLP)
            lp.reset();
        ch.bassShelf.reset();
        ch.trebleShelf.reset();
        ch.dryLowShelf.reset();
        ch.dryHighShelf.reset();
        ch.springCleanDelay.fill(0.0f);
        ch.springCleanDelayWriteIdx = 0;
        ch.recordEnvelope = 0.0f;
        ch.loopEnvelope = 0.0f;
        ch.magneticEnvelope = 0.0f;
    }
    spring.reset();
    noiseLP.reset();
    flutterBand.reset();
    flutterBandLP.reset();
    hissVoice.reset();
    hissCeiling.reset();
    ageBedHighPass.reset();
    ageBedLowPass.reset();
    wobbleLP.reset();
    ageHumSin = 0.0f;
    ageHumCos = 1.0f;
    ageHumRenormalize = 0u;
    writeIdx = 0;
    wowPhase = flutterPhase = 0.0f;
    meterVu = 0.0f;
    meterVuStage1 = 0.0f;
    meterPeak = 0.0f;
    recordVu.store(0.0f, std::memory_order_relaxed);
    recordPeak.store(0.0f, std::memory_order_relaxed);
    // Initial cartridge position is deterministic. Hosted captures place the
    // first head-1 splice at about 7.0 s over most of the motor range and
    // 9.1 s at the extreme fast end (after renderer pre-roll).
    spliceSamplesToHead1 = 7.58f * (float)fs;
    spliceClockStarted = false;
    lastClearRequest =
        pClearRequest.load(std::memory_order_relaxed);
}

float TapeEchoDSP::readTape(const std::vector<float>& buf, float delaySamples) const noexcept
{
    const float rp   = (float)writeIdx - delaySamples;
    int         i0   = (int)std::floor(rp);
    const float frac = rp - (float)i0;

    i0 &= mask; // power-of-two wrap (two's complement handles negative i0)
    const int im1 = (i0 - 1) & mask;
    const int i1  = (i0 + 1) & mask;
    const int i2  = (i0 + 2) & mask;

    return hermite(frac, buf[(size_t)im1], buf[(size_t)i0],
                         buf[(size_t)i1],  buf[(size_t)i2]);
}

void TapeEchoDSP::refreshBlockRateControls()
{
    const uint32_t clearRequest =
        pClearRequest.load(std::memory_order_relaxed);
    if (clearRequest != lastClearRequest)
    {
        // POWER off clears the circulating tape and spring tank. reset() only
        // clears preallocated state, so it is allocation-free on this thread.
        reset();
        lastClearRequest = clearRequest;
    }

    // Snapshot atomics once per block; smoothers handle the rest per sample.
    const auto& m = kModeTable[pMode.load(std::memory_order_relaxed) - 1];
    headGain[0].setTarget(m.h1);
    headGain[1].setTarget(m.h2);
    headGain[2].setTarget(m.h3);
    reverbSendSmoother.setTarget(m.reverb);
    {
        // MULTI-HEAD LOOP-GAIN NORMALIZATION. One table cannot serve two sums.
        //
        // kModeTable attenuates multi-head modes with POWER-sum gains (0.724
        // for two heads, 0.596 for three). That is correct for OUTPUT loudness,
        // where program material sums incoherently, and the mode table exists
        // to keep summed programs near single-head loudness. But those same
        // gains also feed the feedback loop, where the relevant quantity is the
        // ARITHMETIC sum. Including kHeadTrim the loop sum is 1.000 for one
        // head, ~1.42-1.54 for two and 1.836 for three -- so multi-head modes
        // ran +3 to +5 dB of extra LOOP gain while their output level was right.
        //
        // Measured consequence, mode 11 at feedback 0.616 (the "Full Wash" /
        // "Radio Head" state): its first burst matched the reference to 0.04 dB,
        // then the error compounded monotonically (+0.67 / +2.43 / +5.57 dB over
        // successive 250 ms windows) and plateaued near +7 dB. Both plugins dip
        // and regenerate; ours climbed 11 dB off the trough where the reference
        // climbed 6. Accumulation, not level and not tone.
        //
        // Applied to the LOOP FEED ONLY -- amplifiedHeadSum still reaches the
        // echo output at full strength, so nothing here changes the calibrated
        // multi-head output loudness. Entries 0 and 1 are exactly 1.0f, which
        // keeps every single-head and reverb-only state BIT-IDENTICAL and so
        // preserves the calibrated repeat ladder and the fb <= 0.5 anchors.
        //
        // Coefficients are per head count because the correction is NOT a single
        // exponent: two-head states already matched (Runaway Drone +0.09 dB,
        // Multi-Head Bounce +0.16 dB) and only degrade when scaled, while the
        // three-head state needed roughly a halving. An earlier pass swept one
        // shared pow(activeHeads, -alpha) and read the MEAN OF ABSOLUTE deltas,
        // which hid a sign flip and made an 11 dB lever look inert -- always
        // read signed per-stimulus deltas when calibrating this.
        //
        // The three-head value is a BIFURCATION, not a gain law: at feedback
        // 0.616 the loop is either above or below unity and the state jumps
        // between sustaining and decaying. Measured Full Wash noiseburst/snare
        // against the reference, one rebuild and render per point:
        //     1.00 -> +5.31 / +6.55      0.97 -> +4.34 / +3.82
        //     0.95 -> +3.41 / +1.50      0.93 -> +2.11 / -0.72   <- chosen
        //     0.90 -> -0.97 / -3.06      0.85 -> -4.34 / -4.95
        //     0.80 -> -5.04 / -5.47      0.75 -> -5.18 / -5.63
        // Below about 0.85 it saturates near -5 dB (the loop simply stops
        // regenerating) and there is nothing further to gain. 0.93 straddles
        // the two stimuli and takes factory_mean_abs_level_delta from
        // 0.7402 to 0.4663 against a 0.65 limit. Do not chase a lower number
        // by pushing past 0.90: that trades a positive error for a larger
        // negative one and worsens the octave gates.
        //
        // SMOOTHED, not assigned: this shares headGain's 15 ms ramp. Stepping it
        // at the block boundary while headGain ramps would apply one mode's
        // normalization to the other mode's head sum for the length of that
        // ramp -- entering mode 11 the loop would take the 0.93 scale while only
        // the outgoing mode's heads were still contributing -- and would put a
        // 0.63 dB step into the signal being written to tape.
        loopHeadNormSmoother.setTarget(loopHeadNormForMode(m));
    }

    const float rate = pRepeatRate.load(std::memory_order_relaxed);
    if (!spliceClockStarted)
    {
        const float initialSpliceSeconds =
            7.58f + 2.07f * std::pow(rate, 6.0f);
        spliceSamplesToHead1 =
            initialSpliceSeconds * (float)fs;
        spliceClockStarted = true;
    }
    const float tMs  = delayMsForRepeatRate(rate);
    delaySmoother.setTarget(tMs * 0.001f * (float)fs);

    intensitySmoother.setTarget(pIntensity.load(std::memory_order_relaxed));
    echoLevelSmoother.setTarget(pEchoLevel.load(std::memory_order_relaxed));
    reverbLevelSmoother.setTarget(pReverbLevel.load(std::memory_order_relaxed));
    dryLevelSmoother.setTarget(pDryLevel.load(std::memory_order_relaxed));
    outputVolumeSmoother.setTarget(
        pOutputVolume.load(std::memory_order_relaxed));
    echoPanSmoother.setTarget(pEchoPan.load(std::memory_order_relaxed));
    reverbPanSmoother.setTarget(pReverbPan.load(std::memory_order_relaxed));
    inputSendSmoother.setTarget(pInputSend.load(std::memory_order_relaxed));
    mixSmoother.setTarget(pMix.load(std::memory_order_relaxed));
    driveSmoother.setTarget(pInputGain.load(std::memory_order_relaxed));
    wowFlutterSmoother.setTarget(pWowFlutter.load(std::memory_order_relaxed));
    powerSmoother.setTarget(1.0f - pBypass.load(std::memory_order_relaxed));
    ageSmoother.setTarget(pTapeAge.load(std::memory_order_relaxed));

    // Playback bandwidth is proportional to tape speed. Tape condition splits
    // the wear into a broad 5 kHz shoulder and a steeper upper ceiling: this
    // preserves the hosted Old cartridge's 3-4 kHz presence while matching its
    // much larger 8-12 kHz loss.
    {
        const float age = ageSmoother.value();
        const float age2 = age * age;
        const float slow01 = clamp01(
            (tMs - kMinDelayMs) / (kMaxDelayMs - kMinDelayMs));
        const float fast01 = 1.0f - slow01;
        const float voicing = 1.05f + 0.08f * slow01
                            + 0.48f * slow01 * (1.0f - slow01);
        const float playbackAgeScale =
            1.0f + 0.2335f * age - 0.1634f * age2;
        const float cutoff = safeBiquadFrequency(
            fs, 7200.0f * kMinDelayMs / tMs * voicing
              * playbackAgeScale);
        if (cutoff != lastPlaybackCutoff)
        {
            lastPlaybackCutoff = cutoff;
            for (int c = 0; c < kMaxChannels; ++c)
                channels[(size_t)c].speedLP.setCoeffs(
                    Biquad::lowPass(fs, cutoff, 0.70710678f));
        }
        const float oldTape01 = clamp01((age - 0.5f) * 2.0f);
        // Depth is 0.60x the originally fitted curve (-12.954 / 3.752 / 3.2
        // scaled to -7.7724 / 2.2512 / 1.92). This shelf is INSIDE the loop, so
        // its error is paid once per pass and COMPOUNDS: the original depth was
        // fitted against single-repeat tone and measured correct there, but on
        // program material through many passes it drove the whole echo dark.
        // Measured across the factory bank, every preset showed the same tilt
        // (125 Hz hot, 4-16 kHz light), scaling with tape age AND with pass
        // count -- age 0 states were fine at 0.46-0.57 octave median while the
        // Old-tape three-head state sat at 3.57.
        // Swept with a fresh render per point, worst-case (Full Wash) 8 kHz
        // band: 1.00 -> -3.6 dB, 0.85 -> -2.3, 0.70 -> -0.9, 0.60 -> 0.0,
        // 0.50 -> +1.0. 0.60 lands 8 kHz exactly on the reference without
        // pushing any state bright; 0.50 overshoots three of them positive.
        // factory_mean_octave_median 0.9108 -> 0.7825 against a 0.90 limit.
        // Orthogonal to the multi-head loop fix above: the level gate moved
        // only 0.4663 -> 0.4688 across this entire sweep.
        // The residual 125/250 Hz excess is NOT this filter -- it tracks reverb
        // send (Full Wash +6.7 and Ambient Trails +2.5 both at rev 0.588,
        // Worn Tape +0.8 at rev 0), i.e. it is spring LF, deliberately untouched.
        const float ageContourDb =
            -7.7724f * age + 2.2512f * age2
            - 1.92f * oldTape01 * oldTape01 * fast01;
        if (ageContourDb != lastAgeContourDb)
        {
            lastAgeContourDb = ageContourDb;
            const bool active = std::abs(ageContourDb) > 1.0e-6f;
            if (active != ageContourActive)
                for (auto& ch : channels) ch.ageContour.reset();
            ageContourActive = active;
            for (int c = 0; c < kMaxChannels; ++c)
                channels[(size_t)c].ageContour.setCoeffs(
                    Biquad::shelf(
                        fs, safeBiquadFrequency(fs, 5000.0f),
                        ageContourDb, 0.70710678f, /*high=*/true));
        }
        const float antiAliasAgeScale =
            1.0f - 0.1332f * age + 0.0584f * age2;
        const float baseAntiAliasCutoff =
            9710.0f + 1488.0f * fast01 + 422.0f * fast01 * fast01;
        const float antiAliasCutoff = safeBiquadFrequency(
            fs, baseAntiAliasCutoff * antiAliasAgeScale);
        if (antiAliasCutoff != lastAntiAliasCutoff)
        {
            lastAntiAliasCutoff = antiAliasCutoff;
            constexpr float kButterworthQ[3] =
                { 0.51763809f, 0.70710678f, 1.93185165f };
            for (int c = 0; c < kMaxChannels; ++c)
                for (int i = 0; i < 3; ++i)
                    channels[(size_t)c].antiAliasLP[(size_t)i].setCoeffs(
                        Biquad::lowPass(fs, antiAliasCutoff, kButterworthQ[i]));
        }
    }

    // Echo-path tone shelves: recompute only when the knobs actually moved.
    const float bass   = pBass.load(std::memory_order_relaxed);
    const float treble = pTreble.load(std::memory_order_relaxed);
    if (bass != lastBass || treble != lastTreble)
    {
        lastBass   = bass;
        lastTreble = treble;
        const bool bassActive = std::abs(bass) > 1.0e-6f;
        const bool trebleActive = std::abs(treble) > 1.0e-6f;
        if (bassActive != bassShelfActive)
            for (auto& ch : channels) ch.bassShelf.reset();
        if (trebleActive != trebleShelfActive)
            for (auto& ch : channels) ch.trebleShelf.reset();
        bassShelfActive = bassActive;
        trebleShelfActive = trebleActive;
        // Configure ALL channels (like the age-retune block above), not just the
        // current numChannels: a later mono→stereo switch without a knob move
        // would otherwise leave channels[1]'s shelves at their passthrough
        // default while channels[0] is shelved — an L/R mismatch.
        for (int c = 0; c < kMaxChannels; ++c)
        {
            const float bassAmount = std::abs(bass);
            const float bassGainDb = std::copysign(
                bassAmount * (11.31f + 6.06f * bassAmount), bass);
            const float bassFrequency =
                67.55f + 86.70f * bassAmount;
            const float bassQ =
                1.074f - 0.580f * bassAmount;
            channels[(size_t)c].bassShelf.setCoeffs(
                Biquad::shelf(
                    fs, safeBiquadFrequency(fs, bassFrequency),
                    bassGainDb, bassQ, /*high=*/false));

            const float trebleAmount = std::abs(treble);
            const float trebleGainDb = std::copysign(
                trebleAmount * (4.375f + 13.09f * trebleAmount),
                treble);
            // The passive boost and cut arms have different turnover laws.
            // Hosted impulse sweeps place the half-travel corners at 991 Hz
            // (boost) and 1441 Hz (cut), converging near 3 kHz at full travel.
            const float trebleFrequency = treble >= 0.0f
                ? 2850.8f * std::pow(trebleAmount, 1.525f)
                : 3440.8f * std::pow(trebleAmount, 1.255f);
            const float trebleQ =
                0.545f - 0.102f * trebleAmount;
            channels[(size_t)c].trebleShelf.setCoeffs(
                Biquad::shelf(
                    fs, safeBiquadFrequency(
                        fs, std::max(trebleFrequency, 20.0f)),
                    trebleGainDb, trebleQ, /*high=*/true));
        }
    }
}

void TapeEchoDSP::processBlock(const float* const* inputs, float* const* outputs,
                                int numChannels, int numSamples) noexcept
{
    if (numSamples <= 0 || mask == 0)
        return;

    ScopedFlushDenormals ftz;

    numChannels = clampInt(numChannels, 1, kMaxChannels);
    refreshBlockRateControls();

    constexpr float kVuSineCalibration = 1.110720735f;

    for (int n = 0; n < numSamples; ++n)
    {
        //--- shared per-sample control signals (one motor, one tape) ----------
        const float nominalT1 = delaySmoother.next();
        const float nominalMs = nominalT1 * (1000.0f / (float)fs);
        const float slow01 = clamp01(
            (nominalMs - kMinDelayMs) / (kMaxDelayMs - kMinDelayMs));
        // The recurring transport cycle speeds up with tape velocity: hosted
        // 3150 Hz captures measured 2.00 Hz at midpoint and 3.857 Hz at fast.
        flutterInc = kTwoPi * kFlutterMaxHz
                   * (kMinDelayMs / nominalMs) / (float)fs;

        wowPhase += wowInc;
        if (wowPhase > kTwoPi)     wowPhase     -= kTwoPi;
        flutterPhase += flutterInc;
        if (flutterPhase > kTwoPi) flutterPhase -= kTwoPi;

        // The reference transport always moves, even in its freshest state.
        // The user control adds an intentionally wider creative range.
        const float age = ageSmoother.next();
        const float wf  = 1.0f + 1.5f * wowFlutterSmoother.next() + 0.20f * age;
        // slow playback-level wobble (worn pinch roller / dropout precursor);
        // exactly 1.0 at age 0.
        const float wobble =
            1.0f + age * 0.11f
                 * wobbleLP.process(ageRand() * noiseRateComp);
        // New/Used/Old noise rises 1:2:4 in amplitude. Even New retains the
        // reference's tiny electronics/tape floor instead of digital silence.
        const float ageNoiseScale =
            0.25f * std::exp(1.38629436112f * age);
        float ageHum = 0.0f;
        float previousHarmonic = 0.0f;
        float harmonic = ageHumSin;
        int harmonicGainIndex = 0;
        for (int number = 1; number <= 13; ++number)
        {
            if ((number & 1) != 0)
                ageHum += kAgeHumHarmonicGain[harmonicGainIndex++] * harmonic;
            const float nextHarmonic =
                2.0f * ageHumCos * harmonic - previousHarmonic;
            previousHarmonic = harmonic;
            harmonic = nextHarmonic;
        }
        const float nextHumSin =
            ageHumSin * ageHumRotCos + ageHumCos * ageHumRotSin;
        const float nextHumCos =
            ageHumCos * ageHumRotCos - ageHumSin * ageHumRotSin;
        ageHumSin = nextHumSin;
        ageHumCos = nextHumCos;
        if (++ageHumRenormalize == 4096u)
        {
            const float inverseMagnitude = 1.0f / std::sqrt(
                ageHumSin * ageHumSin + ageHumCos * ageHumCos);
            ageHumSin *= inverseMagnitude;
            ageHumCos *= inverseMagnitude;
            ageHumRenormalize = 0u;
        }
        const float reproAgeNoise = ageNoiseScale
            * (kAgeHumGain * ageHum
               + kAgeBedGain
                   * ageBedLowPass.process(
                       ageBedHighPass.process(
                           ageBedRand() * noiseRateComp))
               + kAgeHissGain
                   * hissCeiling.process(
                       hissVoice.process(ageRand() * noiseRateComp)));
        const float flutterDepth = 0.00237f + 0.00242f * slow01;
        const float flutterWave = std::sin(flutterPhase)
                                + 0.088f * std::sin(2.0f * flutterPhase);
        // Scrape flutter: band noise around 6 Hz. Age worsens it steeply on the
        // reference, far faster than the loop-rate wow it rides on.
        const float flutterNoiseGain =
            kFlutterNoiseDepth
            * (1.0f + age * (kFlutterAgeLin + kFlutterAgeSq * age));
        const float flutterNoise =
            flutterNoiseGain
            * flutterBandLP.process(
                  flutterBand.process(flutterRand() * noiseRateComp));
        const float mod = wf * (kWowDepth * std::sin(wowPhase)
                              + flutterDepth * flutterWave
                              + flutterNoise
                              + kNoiseDepth
                                    * noiseLP.process(frand() * noiseRateComp));

        // The oversampled preamp delays the tape feed by a fixed group delay;
        // subtract it AFTER the head-ratio scaling so all three heads stay at
        // their exact mechanical times (subtracting from T would scale the
        // compensation by the far-head ratios).
        const float t1 = nominalT1 * (1.0f + mod);
        const float d1 = clampF(t1                 - kPreampLatencySamples, 4.0f, maxDelaySamples);
        const float d2 = clampF(
            t1 * kHeadRatio[1]
                + kHeadOffsetMs[1] * 0.001f * (float)fs
                - kPreampLatencySamples,
            4.0f, maxDelaySamples);
        const float d3 = clampF(
            t1 * kHeadRatio[2]
                + kHeadOffsetMs[2] * 0.001f * (float)fs
                - kPreampLatencySamples,
            4.0f, maxDelaySamples);

        const float g1 = headGain[0].next() * kHeadTrim[0];
        const float g2 = headGain[1].next() * kHeadTrim[1];
        const float g3 = headGain[2].next() * kHeadTrim[2];

        // Intensity mapped past unity loop gain: > ~0.75 the loop exceeds
        // unity for small signals and the in-loop tape saturation clamps it
        // into stable, warm self-oscillation.
        const float intensity = intensitySmoother.next();
        const float feedbackCurve = feedbackKneeControl(intensity);
        const float fbGain = feedbackGainFromControl(feedbackCurve);
        const float revSend   = reverbSendSmoother.next();
        // Level controls use measured analog-style tapers. The effect-volume
        // curve includes a linear term, so low settings remain useful.
        const float echoRaw   = echoLevelSmoother.next();
        const float echoLvl   = echoGainFromControl(echoRaw);
        const float revRaw    = reverbLevelSmoother.next();
        const float revLvl    = echoGainFromControl(revRaw) * revSend;
        const float dryLvl    = dryLevelSmoother.next();
        const float mix       = mixSmoother.next();
        // Unity-overlap balance law: both paths are unity at the 50% default,
        // preserving the calibrated parallel mix (and old sessions that do not
        // contain kParamMix). Moving toward either endpoint fades only the
        // opposite path; 0% is dry-only and 100% is wet-only.
        const float dryMixGain = std::min(1.0f, 2.0f * (1.0f - mix));
        const float wetMixGain = std::min(1.0f, 2.0f * mix);
        const float outputVolume = outputVolumeSmoother.next();
        // Hosted output trim is linear in decibels: -20 dB at zero, unity at
        // midpoint, and +20 dB at maximum.
        const float outputGain = std::pow(
            10.0f, 2.0f * outputVolume - 1.0f);
        const float driveKnob = driveSmoother.next();
        const float inputGain = inputGainFromControl(driveKnob);
        // Equivalent-level hosted sweeps at multiple knob positions collapse
        // onto one transfer curve: the input control is a gain taper feeding
        // a fixed record-stage nonlinearity, not a second distortion control.
        constexpr float kInputStageDrive = 2.65f;
        constexpr float kInputStageTrim  = 0.459f;
        const float power     = powerSmoother.next(); // 0 = bypassed

        //--- mono effect path -------------------------------------------------
        // The hosted unit sums its stereo input before both wet paths. A
        // left-only impulse produces sample-identical L/R wet signals at
        // center, while duplicated stereo material drives the nonlinear
        // record stage about 6 dB harder. The 0.5 average here preserves the
        // transfer calibration made with duplicated stereo stimuli; the
        // measured pan matrix below restores the corresponding x2 wet gain.
        const float inL = inputs[0][n];
        const float inR = numChannels > 1 ? inputs[1][n] : inL;
        const float inputSend = inputSendSmoother.next();
        const float monoInput =
            power * (numChannels > 1 ? 0.5f * (inL + inR) : inL);
        Channel& ch = channels[0];
        const float drivenInput = monoInput * inputGain;
        const float tapeInput = inputSend * drivenInput;

        // The hosted record path has a broad tape-compression knee above
        // nominal level and asymptotically reaches about 4.5 dB of gain
        // reduction. Most of that reduction happens before the saturator
        // (limiting harmonic growth); the balance is clean record gain.
        const float tapeMagnitude =
            tapeInput < 0.0f ? -tapeInput : tapeInput;
        if (tapeMagnitude > ch.recordEnvelope)
            ch.recordEnvelope += recordEnvelopeAttack
                               * (tapeMagnitude - ch.recordEnvelope);
        else
            ch.recordEnvelope *= recordEnvelopeRelease;
        const float over = std::max(ch.recordEnvelope - 0.12f, 0.0f);
        const float reductionDb = 4.5f * (1.0f - std::exp(-2.7f * over));
        const float compression =
            std::pow(10.0f, -reductionDb * (1.0f / 20.0f));
        // Below the nominal tape threshold, gain reduction is clean and
        // leaves the measured harmonic curve intact. Above it, a growing
        // fraction moves ahead of the shaper so THD approaches the
        // measured high-level plateau instead of climbing without bound.
        const float preCompression01 = clamp01(
            (ch.recordEnvelope - 0.45f) * (1.0f / 0.75f));
        const float preExponent =
            1.2f * std::pow(preCompression01, 0.8f);
        const float postExponent =
            std::max(1.0f - 0.3f * preExponent, 0.6f);
        const float preDriveGain =
            std::pow(compression, preExponent);
        const float highLevel01 = clamp01(
            (ch.recordEnvelope - 0.65f) * (1.0f / 0.59f));
        const float highLevelTrim = std::pow(
            10.0f, -1.1f * std::sqrt(highLevel01) * (1.0f / 20.0f));
        const float postDriveGain =
            std::pow(compression, postExponent) * highLevelTrim;

        // FET preamp front-end, saturated at 4x to keep fold-back
        // products out of the echo passband.
        const float pre = ch.preampDC.process(
                              preampOversampled(
                                  ch, tapeInput * preDriveGain,
                                  kInputStageDrive))
                          * kInputStageTrim * postDriveGain;
        // The spring send is taken entirely ahead of the record-stage shaper
        // and its compressor. A drive ladder measured through the hosted
        // spring (impulse and pink burst, 48 dB of input range, reverb level
        // fixed) shows its tank gain constant to 0.00 dB: the hosted spring
        // send is linear. A partly shaped send instead compressed by up to
        // 3.9 dB across that ladder, which is what made the sparse-impulse and
        // dense-program level errors disagree. Only the preamp-latency
        // alignment below is kept, so the first arrival is unchanged.
        float cleanReadPos =
            (float)ch.springCleanDelayWriteIdx - kPreampLatencySamples;
        if (cleanReadPos < 0.0f)
            cleanReadPos += (float)ch.springCleanDelay.size();
        const int cleanRead0 = (int)cleanReadPos;
        const int cleanRead1 =
            (cleanRead0 + 1) % (int)ch.springCleanDelay.size();
        const float cleanReadFrac = cleanReadPos - (float)cleanRead0;
        const float delayedTapeInput =
            ch.springCleanDelay[(size_t)cleanRead0]
            + cleanReadFrac
                * (ch.springCleanDelay[(size_t)cleanRead1]
                   - ch.springCleanDelay[(size_t)cleanRead0]);
        // Input Send is the original Echo/Normal "dub" switch. It interrupts
        // the tape record feed, but not the independent spring-reverb path.
        ch.springCleanDelay[(size_t)ch.springCleanDelayWriteIdx] = drivenInput;
        if (++ch.springCleanDelayWriteIdx >=
            (int)ch.springCleanDelay.size())
            ch.springCleanDelayWriteIdx = 0;
        // Send trim, at measured parameter-matched parity. The former 0.60
        // shaped / 0.40 clean blend had a small-signal gain of 1.1385 x
        // tapeInput and read 1.60 dB hot against the hosted tank once its own
        // compression was taken out of the reading, which puts the linear
        // send at 0.9470 x tapeInput.
        constexpr float kCleanSpringSendGain =
            0.77855f * kInputStageDrive * kInputStageTrim;
        const float springPre =
            kCleanSpringSendGain * delayedTapeInput;

        // Three playback heads off the shared tape.
        // The joined ends of the physical tape create a recurring dropout.
        // Its loop period is 9.23 s at the fastest motor setting and scales
        // inversely with tape speed. A narrow join is followed by a broader
        // loss region; age determines how audible both become.
        const float speedScale = nominalMs / kMinDelayMs;
        float spliceDepthDb;
        if (age <= 0.5f)
            spliceDepthDb = 1.58f + 4.60f * age;
        else
        {
            const float old01 = (age - 0.5f) * 2.0f;
            spliceDepthDb =
                3.88f + 11.65f * std::pow(old01, 1.5f);
        }
        const float old01 = clamp01((age - 0.5f) * 2.0f);
        const float narrowWeight = age <= 0.5f
            ? 0.545f + 0.106f * age
            : 0.598f + 0.164f * old01;
        const float narrowSeconds = age <= 0.5f
            ? 0.0132f - 0.0012f * age
            : 0.0126f - 0.0028f * old01;
        const auto spliceGain = [&](float samplesToEvent) noexcept
        {
            const float secondsToEvent =
                samplesToEvent / (float)fs;
            const float narrow =
                std::exp(-0.5f * std::pow(
                    secondsToEvent / narrowSeconds, 2.0f));
            const float broad =
                std::exp(-0.5f * std::pow(
                    secondsToEvent / (0.080f * speedScale), 2.0f));
            const float shape =
                narrowWeight * narrow + (1.0f - narrowWeight) * broad;
            return std::pow(10.0f, -spliceDepthDb * shape * 0.05f);
        };
        const float h1 = readTape(ch.tape, d1)
                       * spliceGain(spliceSamplesToHead1);
        const float h2 = readTape(ch.tape, d2)
                       * spliceGain(
                           spliceSamplesToHead1 + (d2 - d1));
        const float h3 = readTape(ch.tape, d3)
                       * spliceGain(
                           spliceSamplesToHead1 + (d3 - d1));
        const float headSum = (h1 * g1 + h2 * g2 + h3 * g3) * wobble;
        const float amplifiedHeadSum =
            feedbackReadAmplifier(headSum, feedbackCurve);
        const float loopReadBlend = feedbackReadLoopBlend(feedbackCurve);
        // Multi-head loop-gain correction, loop path only. Advanced once per
        // sample alongside headGain above so the two track each other through a
        // mode change. See refreshBlockRateControls() for the derivation.
        const float loopHeadSum = loopHeadNormSmoother.next()
            * (headSum + loopReadBlend * (amplifiedHeadSum - headSum));

        // Record chain: program + feedback -> head EQ -> magnetic
        // saturation -> tape. Everything here is inside the loop, so
        // repeats darken and compress cumulatively.
        const float loopDrive = hardKnee(
            fbGain * softClip(loopHeadSum),
            kLoopCeilingKnee, kLoopCeiling);
        // Meter point: after Input Volume, BEFORE the regeneration sum. This
        // deliberately ignores Input Send. drivenInput carries the power gate
        // already (monoInput = power * ...), so both readings still die with
        // POWER off.
        //
        // The regeneration term was `+ power * loopDrive` and is deliberately
        // gone. That placement follows the UAD manual, which says "echo
        // feedback is applied just before the level detection circuit, so the
        // Feedback control will affect the level readings" -- but it does not
        // match the reference's observable behaviour. Watching both plugins
        // side by side in a host: stop the transport with feedback up and OUR
        // needle stepped down once per repeat all the way to the left stop,
        // while the reference's did not. Reproduced offline (mode 11, slowest
        // motor, feedback 0.6): 53 needle direction reversals in the 3.5 s
        // after the input stops, versus a smooth monotonic fall with this term
        // removed. At Repeat Rate 0.5 the repeats are ~120 ms apart and blur
        // together, which is why this only shows up on slow multi-head states.
        //
        // ATTENUATED to a quarter, not removed and not full. The reference does
        // show a little of the repeats after the transport stops -- "just a
        // tiny bit of delay bounces" -- so the feedback contribution the manual
        // describes is real, it is simply far smaller than a full-level tap of
        // the tape write drive. Swept against a hot loop (mode 11, slowest
        // motor, feedback 0.70), reading NEEDLE POSITION after the clamp at the
        // -20 VU stop, which is what is actually visible:
        //     regen  visible tail   needle @6.3s / 7.0s / 8.0s after stop
        //      0.00      none          0% /  0% /  0%
        //      0.12      none          0% /  0% /  0%
        //      0.25      4.0 s         8% / 16% / 11%   <- chosen
        //      0.50      4.0 s        34% / 42% / 38%
        //      1.00      4.0 s        60% / 68% / 64%   (the old behaviour)
        // 0.25 keeps the decaying repeats inside the first sixth of the scale,
        // a small movement near the left stop; 0.50 and above put them
        // mid-face, which is the "needle walks down bouncing per repeat" the
        // reference does not do.
        //
        // Note the reversal COUNT is useless as a metric here -- it is 53 at
        // every non-zero setting, because repeats always create local maxima
        // regardless of level. Judge this by visible needle excursion after the
        // -20 VU clamp, and on a HOT enough signal that the tail clears the
        // stop at all (at nominal level even 1.00 shows only 0.06 s of tail).
        //
        // This factor is calibrated to a description of the reference's needle,
        // not to a measurement of it -- Galaxy exposes no meter parameter, so
        // its VU cannot be read programmatically. Treat 0.25 as provisional.
        constexpr float kMeterRegenAmount = 0.25f;
        const float recordMeterSignal =
            drivenInput + kMeterRegenAmount * power * loopDrive;
        const float recordMeterMagnitude = std::abs(recordMeterSignal);
        // Average-responding VU, calibrated so a sine's average rectified value
        // reads its RMS amplitude. The peak lamp uses the unsmoothed magnitude.
        //
        // Two cascaded poles of equal tau = the critically damped needle. Same
        // coefficient in both directions: a moving-coil movement has no separate
        // attack and release. See the derivation at kVuTauSeconds.
        meterVuStage1 += meterVuCoeff * (recordMeterMagnitude - meterVuStage1);
        meterVu += meterVuCoeff * (meterVuStage1 - meterVu);
        meterPeak = std::max(
            recordMeterMagnitude,
            meterPeak * meterPeakDecayPerSample);
        // The low-flux gate below is a function of record flux, and the record
        // head sees program AND regeneration. Gating it on the program alone
        // left a runaway loop pinned in the low-flux region of the tape curve,
        // where the low-level harmonic term is still fully active, folds the
        // transfer back on itself and caps the tape at 0.130 - the whole
        // measured 11 dB self-oscillation shortfall. The same flux driven from
        // the input reached the matched plateau, because the input envelope
        // opened the gate. Only that gate is moved onto total flux: the hot /
        // very-hot Chebyshev terms and the magnetic makeup below stay on the
        // program envelope, because they were fitted as program-level makeup
        // laws. Feeding them total flux as well measured 0.5 dB worse on the
        // factory octave-band gate (2.66 against a 2.50 limit) - they are
        // harmonic generators, and opening them on program-plus-regeneration
        // put content in bands the reference does not have.
        const float loopMagnitude = loopDrive < 0.0f ? -loopDrive : loopDrive;
        if (loopMagnitude > ch.loopEnvelope)
            ch.loopEnvelope += recordEnvelopeAttack
                             * (loopMagnitude - ch.loopEnvelope);
        else
            ch.loopEnvelope *= recordEnvelopeRelease;
        // Referred back through the record-amp small-signal gain so the
        // measured gate thresholds - all calibrated in input units against the
        // program envelope - keep their meaning. Exactly zero with the
        // feedback control at minimum, so the harmonic calibration is
        // untouched there.
        constexpr float kRecordDriveToInput =
            1.0f / (kInputStageDrive * kInputStageTrim);
        const float fluxEnvelope =
            ch.recordEnvelope + ch.loopEnvelope * kRecordDriveToInput;

        float recorded = ch.speedLP.process(pre + loopDrive);
        if (ageContourActive)
            recorded = ch.ageContour.process(recorded);
        recorded = ch.recordHP.process(recorded);
        // The tape's low-level magnetisation curve is more strongly curved
        // than the record preamp, but its slope remains positive at overload.
        // This bounded cubic term raises the odd-harmonic ladder without
        // changing small-signal loop gain or the separately calibrated spring
        // send.
        const float recorded2 = recorded * recorded;
        // At low flux the measured odd harmonics fall much more slowly than a
        // cubic polynomial can produce. A regularized, scale-covariant p=2.15
        // term restores that quiet harmonic floor, then fades out before the
        // main magnetic curve reaches its already-matched -18 dB operating
        // point.
        const float lowFloor01 = clamp01(
            (fluxEnvelope - 0.02f) * (1.0f / 0.07f));
        const float lowFloorSmooth =
            lowFloor01 * lowFloor01 * (3.0f - 2.0f * lowFloor01);
        const float lowFloorFade = 1.0f - lowFloorSmooth;
        const float lowFloor = 1.325f * lowFloorFade * recorded
            * std::pow(recorded2 + 0.006f * 0.006f, 0.575f);
        const float magneticInput =
            recorded * (1.0f - 5.0f * recorded2
                        / (1.0f + 8.0f * recorded2))
            - lowFloor;
        const float baseTape = softClip(magneticInput);
        const float baseMagnitude = std::abs(baseTape);
        if (baseMagnitude > ch.magneticEnvelope)
            ch.magneticEnvelope = baseMagnitude;
        else
            ch.magneticEnvelope *= recordEnvelopeRelease;

        // At high record flux the hosted tape adds a steep odd-harmonic
        // ladder while its fundamental has already reached a level plateau.
        // Chebyshev terms supply those missing harmonics without changing the
        // steady-state fundamental. Their envelope-gated, normalized sum is
        // bounded to roughly thirteen percent of the tape signal and remains
        // zero through the already-matched nominal range.
        const float magneticAmplitude =
            std::max(ch.magneticEnvelope, 1.0e-4f);
        const float z = clampF(
            baseTape / magneticAmplitude, -1.0f, 1.0f);
        const float z2 = z * z;
        const float z3 = z2 * z;
        const float z5 = z3 * z2;
        const float z7 = z5 * z2;
        const float hot01 = clamp01(
            (ch.recordEnvelope - 0.26f) * (1.0f / 0.21f));
        const float hot =
            hot01 * hot01 * (3.0f - 2.0f * hot01);
        const float veryHot01 = clamp01(
            (ch.recordEnvelope - 0.52f) * (1.0f / 0.16f));
        const float veryHot =
            veryHot01 * veryHot01 * (3.0f - 2.0f * veryHot01);
        const float t3 = 4.0f * z3 - 3.0f * z;
        const float t5 = 16.0f * z5 - 20.0f * z3 + 5.0f * z;
        const float t7 =
            64.0f * z7 - 112.0f * z5 + 56.0f * z3 - 7.0f * z;
        // veryHot terms: the RECORD FOLD-BACK. Measured, not dialled.
        //
        // At the top of the hosted transfer ladder (Input Volume 0.50, the
        // -3 dBFS rung, record envelope 0.700) the reference stops behaving
        // like a compressor and turns over: its fundamental FALLS 1.35 dB as
        // the input rises 3 dB, its 3rd harmonic COLLAPSES -44.5 -> -55.2 dBFS
        // and its 5th RISES -64.3 -> -52.2, overtaking the 3rd. A falling
        // fundamental with 5th-over-3rd is a fold-back transfer curve -- the
        // magnetisation curve turning over at overload, where the third-order
        // coefficient crosses zero and higher orders take over. It is not a
        // ceiling: a ceiling drives odd harmonics UP monotonically, and every
        // tape-write ceiling tried previously (0.2396 / 0.262 / 0.300) clipped
        // ordinary program and broke other gates.
        //
        // veryHot01 spans record envelope 0.52..0.68, and the ladder rungs
        // measure 0.124 / 0.248 / 0.495 / 0.700 -- so veryHot is EXACTLY zero
        // at the -6 rung and saturated at -3. That is why these three
        // constants move the broken rung while leaving -18 / -12 / -6
        // bit-identical; they were already matched (dFund <= 0.29 dB,
        // dTHD <= 0.63 dB) and must not move.
        //
        // Fitted one knob at a time against the hosted reference, each with a
        // fresh render (offline harness replicating probe_transfer.py's
        // window and THD definition, validated against the hosted numbers):
        //   c3  0.0209 -> 0.35   nulls most of the base curve's 3rd
        //                        (H3 -43.8 -> -52.6 dBFS, reference -55.2)
        //   c5 -0.0255 -> -0.20  lands THD on 5.026% against 5.010%
        //   makeup 0.06 -> -2.70 dB   the fold-back's own level drop; this
        //                        term is a pure gain, so it moves the
        //                        fundamental 1:1 in dB and leaves THD alone
        // Result at the -3 rung: dFundamental +1.73 -> +0.00 dB,
        // dTHD +4.85 -> +0.03 dB.
        //
        // KNOWN RESIDUAL, deliberately not chased: the reference's 5th is
        // -52.2 dBFS and ours reaches only -62.3. c5 in EITHER direction
        // spills into the 3rd faster than it lifts the 5th, so a genuinely
        // 5th-dominant spectrum is not reachable with this Chebyshev pair.
        // Both harmonics move toward the reference and the gated quantities
        // match; the fold-back is PARTIALLY modelled. Closing the rest needs
        // a reshaped magnetisation curve, which re-opens the whole harmonic
        // calibration -- its own campaign. Do not "fix" it by pushing c5.
        const float c3 = -0.0983f * hot + 0.35f * veryHot;
        const float c5 = -0.0195f * hot - 0.20f * veryHot;
        const float c7 = 0.0f;
        float toTape = baseTape + magneticAmplitude
            * (c3 * t3 + c5 * t5 + c7 * t7);
        // Restore the measured level plateau without undoing the magnetic
        // curvature. The slowly tracked envelope makes this a program-level
        // makeup law (not a sample-by-sample waveshaper), so harmonic ratios
        // stay intact. It is inactive below ordinary musical peaks.
        const float magneticMakeup01 = clamp01(
            (ch.recordEnvelope - 0.18f) * (1.0f / 0.82f));
        // The veryHot coefficient is NEGATIVE: above the fold-back knee the
        // measured plateau does not just stop rising, it comes back down.
        // See the fold-back derivation at the Chebyshev coefficients above.
        const float magneticMakeupDb =
            1.8f * std::sqrt(std::sqrt(magneticMakeup01))
            + 0.46f * hot - 2.70f * veryHot;
        toTape *= std::pow(10.0f, magneticMakeupDb * (1.0f / 20.0f));
        for (auto& lp : ch.antiAliasLP)
            toTape = lp.process(toTape);
        ch.tape[(size_t)writeIdx] = toTape;

        // Echo output path only: bass/treble shelves (dry and reverb
        // are unaffected, matching the hardware layout).
        float echoWet = amplifiedHeadSum + reproAgeNoise;
        if (bassShelfActive)
            echoWet = ch.bassShelf.process(echoWet);
        if (trebleShelfActive)
            echoWet = ch.trebleShelf.process(echoWet);

        // Spring tank is fed from the same mono preamp signal.
        const float rev = spring.process(springPre * revSend);

        // Both wet-path pan controls are measured linear amplitude laws.
        // In stereo, the x2 factor pairs with the input average above:
        // center (0.5/0.5) preserves the calibrated wet level, while a hard
        // pan is 6.02 dB louder in its destination channel.
        const float echoPan = echoPanSmoother.next();
        const float reverbPan = reverbPanSmoother.next();
        for (int c = 0; c < numChannels; ++c)
        {
            const float in = c == 0 ? inL : inR;
            Channel& outputChannel = channels[(size_t)c];
            const float dry = outputChannel.dryHighShelf.process(
                outputChannel.dryLowShelf.process(in));
            const float echoPanGain = numChannels == 1
                ? 1.0f : 2.0f * (c == 0 ? 1.0f - echoPan : echoPan);
            const float reverbPanGain = numChannels == 1
                ? 1.0f : 2.0f * (c == 0 ? 1.0f - reverbPan : reverbPan);
            const float dryPath = dryLvl * inputGain * dry;
            const float wetPath = echoLvl * echoWet * echoPanGain
                                + revLvl * rev * reverbPanGain;
            const float mixed = dryMixGain * dryPath + wetMixGain * wetPath;
            // POWER off: clean passthrough. The wet state is cleared on the
            // rising bypass edge when POWER is switched off, with the clear
            // applied in the next block. Re-engaging starts with an empty
            // tape loop.
            outputs[c][n] = in + power * (outputGain * mixed - in);

        }

        writeIdx = (writeIdx + 1) & mask;
        spliceSamplesToHead1 -= 1.0f;
        const float spliceTailSamples =
            0.8f * speedScale * (float)fs;
        if (spliceSamplesToHead1 < -spliceTailSamples)
        {
            const float loopPeriodSamples =
                9.23f * speedScale * (float)fs;
            spliceSamplesToHead1 += loopPeriodSamples;
        }
    }

    recordVu.store(kVuSineCalibration * meterVu, std::memory_order_relaxed);
    recordPeak.store(meterPeak, std::memory_order_relaxed);
}

} // namespace duskaudio
