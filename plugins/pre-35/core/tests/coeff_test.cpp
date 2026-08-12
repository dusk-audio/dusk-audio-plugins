// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// coeff_test — coefficient sanity for the PRE-35 core.
//
// Nothing here renders audio. These are the checks that catch a wrong sign, a
// reciprocal, a Hz-vs-rad/s slip or an unstable realisation BEFORE anything is
// measured through a signal, where the same bug would look like a tuning problem.

#include "Filters.hpp"
#include "IronLayer.hpp"
#include "Pre35Model.hpp"
#include "Resampler.hpp"
#include "TestSupport.hpp"

#include <algorithm>
#include <vector>

using namespace pre35;
using namespace pre35test;

namespace
{

void testResamplerTaps(Report& r)
{
    for (int factor : { 2, 4, 8 })
    {
        std::vector<double> h(kMaxResamplerTaps);
        const int n = designResamplerTaps(factor, h.data());

        r.check(n == 20 * factor + 1,
                "resampler tap count (" + std::to_string(factor) + "x)",
                std::to_string(n));

        double sum = 0.0, worstSym = 0.0;
        for (int i = 0; i < n; ++i)
        {
            sum += h[i];
            worstSym = std::max(worstSym, std::fabs(h[i] - h[n - 1 - i]));
        }
        // firwin(scale=True) normalises DC gain to exactly 1; a sum that is not 1
        // means the window or the normalisation moved.
        r.near(sum, 1.0, 1e-12, "resampler DC gain (" + std::to_string(factor) + "x)", "");
        // Symmetry is structural — mirrored indices give exactly negated `m`, and
        // r*r, the Kaiser window and the (-a)/(-b) division are all sign-even — so
        // this measures 0.0 exactly wherever libm's sin is exactly odd (it is on
        // glibc for every argument used here). It is NOT guaranteed to be, and one
        // ulp of a tap is 2.8e-17 at 8x, so a bit-exact 1e-18 gate would fail on a
        // conforming libm that rounds sin(-x) differently. 1e-15 is ~40 ulp: still
        // twelve orders of magnitude below the O(1e-2) error a genuine asymmetry
        // (wrong alpha, wrong index) would produce.
        r.below(worstSym, 1e-15, "resampler linear phase (" + std::to_string(factor) + "x)", "");
    }
}

void testResamplerRoundTrip(Report& r)
{
    constexpr int factor = 8;
    Upsampler up;
    Downsampler down;
    up.prepare(factor);
    down.prepare(factor);

    // Impulse in: the round trip must be a pure delay of 2 * kResamplerStageLatency
    // host samples with unity peak, because both stages are linear phase and both
    // are normalised to unity DC gain.
    const int n = 128;
    std::vector<double> os(factor);
    std::vector<double> y(n);
    for (int i = 0; i < n; ++i)
    {
        up.processSample(i == 0 ? 1.0 : 0.0, os.data());
        y[i] = down.processBlock(os.data());
    }

    const int peak = (int)(std::max_element(y.begin(), y.end()) - y.begin());
    r.check(peak == 2 * kResamplerStageLatency, "resampler round-trip delay",
            std::to_string(peak) + " samples");

    // DC in, DC out. The residual is the polyphase DC imbalance of scipy's own
    // kaiser-5 design — resample_poly(resample_poly(ones, 8, 1), 1, 8) returns
    // 1.0000001112101191 in its interior, and this implementation reproduces that
    // to the last digit. -140 dB, and matching it is the point.
    up.reset();
    down.reset();
    double last = 0.0;
    for (int i = 0; i < 512; ++i)
    {
        up.processSample(1.0, os.data());
        last = down.processBlock(os.data());
    }
    r.near(last, 1.0, 2e-7, "resampler DC round trip", "");

    // Passband gain. NOT zero: the round trip is the prototype applied twice, so
    // it carries 2 * |H(f)| of the design's own passband ripple — +0.00899 dB at
    // 1 kHz, which scipy produces to within 2e-7 dB of this number. Pinning the
    // measurement to the prototype (rather than to 0 dB) is what makes this a
    // test of the implementation instead of a test of the design.
    const double sr = 48000.0, srOs = sr * factor;
    for (double f0 : { 1000.0, 10000.0 })
    {
        up.reset();
        down.reset();
        std::vector<float> out;
        out.reserve((size_t)sr);
        for (int i = 0; i < (int)sr; ++i)
        {
            up.processSample(std::sin(2.0 * kPiT * f0 * (double)i / sr), os.data());
            out.push_back((float)down.processBlock(os.data()));
        }

        std::vector<double> h(kMaxResamplerTaps);
        const int nTaps = designResamplerTaps(factor, h.data());
        double re = 0.0, im = 0.0;
        for (int j = 0; j < nTaps; ++j)
        {
            const double a = -2.0 * kPiT * f0 * (double)j / srOs;
            re += h[j] * std::cos(a);
            im += h[j] * std::sin(a);
        }
        const double expected = 2.0 * linToDb(std::sqrt(re * re + im * im));

        r.near(linToDb(measureAmplitude(out, f0, sr, 0.1)), expected, 1e-4,
               "resampler round-trip gain at " + std::to_string((int)f0) + " Hz");
    }
}

void testResponse(Report& r)
{
    const double srOs = 48000.0 * 8.0;

    for (int pad = 0; pad < coeffs::kNumPads; ++pad)
        for (double trim : { 0.0, 25.0, 50.0, 75.0, 100.0 })
        {
            const AnalogZPK z = buildResponseZPK(pad, trim);

            // Midband median must be 0 dB — that is the whole normalisation
            // convention, and if it drifts the taper double-counts.
            double grid[coeffs::kMidbandGridPoints], mag[coeffs::kMidbandGridPoints];
            midbandGrid(grid, coeffs::kMidbandGridPoints);
            for (int i = 0; i < coeffs::kMidbandGridPoints; ++i)
                mag[i] = z.magnitudeDb(grid[i]);
            const double median = medianOf(mag, coeffs::kMidbandGridPoints);

            FirstOrderCascade c;
            const bool built = c.setFromAnalog(z, srOs);

            double worstPole = 0.0, worstErr = 0.0;
            for (int i = 0; i < c.getNumSections(); ++i)
                worstPole = std::max(worstPole, c.poleRadius(i));
            for (int i = 0; i < 400; ++i)
            {
                const double f = coeffs::kResponseBandLoHz
                    * std::pow(coeffs::kResponseBandHiHz / coeffs::kResponseBandLoHz,
                               (double)i / 399.0);
                worstErr = std::max(worstErr, std::fabs(c.magnitudeDb(f, srOs) - z.magnitudeDb(f)));
            }

            const std::string tag = " (pad " + std::to_string(coeffs::kPads[pad].labelDb)
                                  + ", trim " + std::to_string((int)trim) + "%)";
            r.check(built, "response cascade builds" + tag);
            r.near(median, 0.0, 1e-9, "response midband median" + tag);
            r.below(worstPole, 0.9999999, "response pole radius" + tag, "");
            // The reference gates its own discrete realisation at 0.05 dB; the
            // C++ realises the same prototype at the same rate, so it inherits
            // the same budget and nothing more.
            r.below(worstErr, 0.05, "response discrete-vs-analog 20 Hz-18 kHz" + tag);
        }
}

void testSidechain(Report& r)
{
    const AnalogZPK w = buildSidechainZPK();

    // |W(f_ref)| = 1 by construction — the emitted gain carries that
    // normalisation, so a wrong sign on the k accumulation shows up here first.
    r.near(w.magnitudeDb(coeffs::kIronPowerLaw.fRefHz), 0.0, 0.01, "|W(20 Hz)|");

    // W is what buys the f^-alpha law, so its shape must track R3(f)/R3(f_ref).
    // The fit's own worst error against that target is 0.101 dB (gates_iron
    // "sidechain fit max"), so anything under ~0.15 dB is the fit, not the port.
    double worst = 0.0;
    for (int i = 0; i < 200; ++i)
    {
        const double f = 20.0 * std::pow(100.0 / 20.0, (double)i / 199.0);
        const double target = linToDb(ironR3(f) / coeffs::kIronPowerLaw.r3Ref);
        worst = std::max(worst, std::fabs(w.magnitudeDb(f) - target));
    }
    r.below(worst, 0.15, "W tracks R3(f)/R3(f_ref) over the validity band");

    FirstOrderCascade c;
    r.check(c.setFromAnalog(w, 48000.0 * 8.0), "sidechain cascade builds");
    double worstPole = 0.0;
    for (int i = 0; i < c.getNumSections(); ++i)
        worstPole = std::max(worstPole, c.poleRadius(i));
    r.below(worstPole, 0.9999999, "sidechain pole radius", "");
}

void testNoiseShaper(Report& r)
{
    const AnalogZPK z = buildNoiseTailZPK();

    // Normalised so |H| = sqrt(fc/f) exactly one decade below the corner.
    const double fc = coeffs::kNoise.lfCornerHz;
    r.near(z.magnitudeDb(fc / 10.0), linToDb(std::sqrt(10.0)), 1e-9, "1/f tail level at fc/10");

    // +10 dB/decade below the corner: half a pole, which is what "1/f in power"
    // means. Measured across the middle of the modelled span to stay away from
    // the end sections' own roll-off.
    const double lo = z.magnitudeDb(fc / 20.0);
    const double hi = z.magnitudeDb(fc / 2.0);
    r.near((lo - hi) / std::log10(10.0), 10.0, 1.0, "1/f tail slope per decade");

    // And nothing above the corner: the flat generator already owns that region.
    r.below(z.magnitudeDb(fc * 20.0) - z.magnitudeDb(fc), -12.0,
            "1/f tail is dead an octave-and-a-bit above fc");
}

void testTaperAndPads(Report& r)
{
    // Pinned to the hard stops: these two are the only zero-uncertainty
    // measurements in the whole gain model.
    r.near(taperGainDb(0.0), coeffs::kTaper.g0Db, 1e-12, "taper at 0 %");
    r.near(taperGainDb(100.0), coeffs::kTaper.g1Db, 1e-12, "taper at 100 %");

    bool monotone = true;
    double prev = -1e30;
    for (int i = 0; i <= 1000; ++i)
    {
        const double g = taperGainDb((double)i * 0.1);
        monotone = monotone && (g > prev);
        prev = g;
    }
    r.check(monotone, "taper is strictly monotone over 0-100 %");

    r.near(gainCalDb(0.0, 0), coeffs::kTaper.g0Db + coeffs::kPads[0].offsetDb, 1e-12,
           "cal gain = taper + pad (pad 0)");
    r.near(gainCalDb(100.0, 2), coeffs::kTaper.g1Db + coeffs::kPads[2].offsetDb, 1e-12,
           "cal gain = taper + pad (pad 40)");

    // The pad is a fixed offset on a single pad-0-referenced curve; if that ever
    // stops being true the taper has to be refitted per pad, not patched here.
    bool padIsOffset = true;
    for (double trim : { 0.0, 37.5, 100.0 })
        for (int pad = 0; pad < coeffs::kNumPads; ++pad)
            padIsOffset = padIsOffset
                && std::fabs((gainCalDb(trim, pad) - gainCalDb(trim, 0))
                             - coeffs::kPads[pad].offsetDb) < 1e-12;
    r.check(padIsOffset, "pad is a pure offset at every trim position");
}

void testIronCoefficients(Report& r)
{
    IronLayer iron;
    const double sr = 48000.0 * 8.0;
    iron.prepare(sr);

    r.near(iron.detectorPoleValue(), std::exp(-1.0 / (coeffs::kDetectorTauS * sr)), 1e-15,
           "detector pole", "");
    r.near(iron.d3Value(), coeffs::kIronPowerLaw.r3Ref, 1e-18, "d3 = r3Ref", "");
    // The even depth is no longer a fixed ratio of the odd one, so the old exact
    // identity would now assert a vestigial number. Three checks replace it and
    // between them cover what it used to guarantee implicitly.

    // 1. The law is pinned at its own reference. One assertion catches an
    //    inverted ratio, a flipped slope sign and a dB-for-linear slip, because
    //    all three break the pin at exactly this point.
    r.near(iron.d2EffAt(coeffs::kIronH2.axRefLin), coeffs::kIronH2.d2Ref, 1e-18,
           "the h2 drive law is unity at its reference drive", "");

    // 2. The reference DRIVE is where the emitter said it is. axRefLin is the
    //    only place the pad-0 OEM ceiling and the plugin's input mapping are
    //    multiplied together, and getting it wrong slides the whole h2 curve up
    //    or down without changing its shape, which no frequency-law gate can
    //    see. -35 dBu at the transformer against 0 dBFS mapped to -19.65 dBu is
    //    -15.35 dBFS in the units the layer carries.
    //
    //    NOT compared against kIronH2OffsetDb. That constant is the measured
    //    median of a flat approximation the model itself records as overstating
    //    h2 at low drive and understating it at high, so agreement with it would
    //    be evidence of nothing. It stays emitted as provenance only.
    r.near(linToDb(coeffs::kIronH2.axRefLin), -15.35, 0.01,
           "the h2 reference drive matches the ceiling and the input mapping",
           "dBFS");

    // 3. Bounded and monotone across the drive window, which the exact identity
    //    used to give for free.
    const double dLo = iron.d2EffAt(dbToLin(coeffs::kDetectorFloorDbfs));
    const double dHi = iron.d2EffAt(coeffs::kIronH2.axRefLin * 100.0);
    r.check(std::isfinite(dLo) && dLo > 0.0 && dHi > dLo,
            "the h2 drive law is finite, positive and monotone");
    r.note("h2 depth swing across the drive window: "
           + std::to_string(linToDb(dHi / dLo)) + " dB");

    // Both weighting cascades must have actually realised. A failed cascade
    // degrades to a wrong-gain pass-through rather than to silence, so nothing
    // else in the suite would notice.
    r.check(iron.cascadesAreValid(), "both iron weighting cascades realised");

    // The guard costs the model's own quoted "< 0.15 dB above 20 Hz" (it is
    // 0.143 dB at 100 Hz, rising as the law flattens) and diverges hard below the
    // shelf, which is the entire reason it exists.
    r.below(std::fabs(linToDb(ironR3(100.0) / ironR3PowerLaw(100.0))), 0.15,
            "subsonic guard stays inside its quoted cost at 100 Hz");
    r.check(ironR3(1.0) < ironR3PowerLaw(1.0),
            "subsonic guard bounds the law below the shelf");
}

} // namespace

int main()
{
    Report r("pre35 coeff_test");
    testResamplerTaps(r);
    testResamplerRoundTrip(r);
    testResponse(r);
    testSidechain(r);
    testNoiseShaper(r);
    testTaperAndPads(r);
    testIronCoefficients(r);
    return r.exitCode();
}
