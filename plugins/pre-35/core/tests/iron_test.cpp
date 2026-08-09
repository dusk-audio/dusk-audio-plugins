// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// iron_test — the transformer layer, measured rather than asserted.
//
// Three claims, one gate each:
//
//   1. THD3 through the whole chain lands on the published power law,
//      r3Ref * (f/20)^-alpha, at 20 / 40 / 100 Hz.
//   2. The Chebyshev terms carry NO fundamental, so the layer cannot shift the
//      gain it is bolted onto. Measured on the layer's own contribution
//      (y - x), where a fundamental leak has nowhere to hide.
//   3. CONSTANT PERCENTAGE: the same THD3 at every drive level inside the
//      measured window. That is the property the level-tracked envelope buys,
//      and the one a naive waveshaper would fail.
//
// Everything is measured with the reference's own method (Hann, whole cycles,
// 5-bin sum — see TestSupport.hpp), at the reference's validation tone level,
// so a number here is comparable to a number from `render_ref --validate`.

#include "IronLayer.hpp"
#include "Pre35DSP.hpp"
#include "Pre35Model.hpp"
#include "TestSupport.hpp"

#include <algorithm>
#include <vector>

using namespace pre35;
using namespace pre35test;

namespace
{

constexpr double kSr = 48000.0;
constexpr int    kOs = 8;

/** render_ref.py's VALIDATION_TONE_DBFS: the middle of the measured drive
    window, quoted as the RMS of the tone at the transformer. */
constexpr double kToneDbfs = -45.0;
constexpr double kToneSeconds = 6.0;
constexpr double kSkipSeconds = 3.0;   // >= 12 detector time constants

std::vector<float> renderThdTone(double f0, double levelDbfs, double ironAmount)
{
    Pre35DSP dsp;
    dsp.setPadIndex(0);
    dsp.setTrimPercent(0.0);
    dsp.setIronAmount(ironAmount);
    dsp.setNoiseEnabled(false);
    dsp.prepare(kSr, kOs);

    // sqrt(2) turns the quoted RMS into a peak amplitude, as the reference does.
    std::vector<float> buf = makeSine(f0, dbToLinT(levelDbfs) * std::sqrt(2.0),
                                      kToneSeconds, kSr);
    dsp.process(buf.data(), (int)buf.size());
    return buf;
}

void testThdVsSpec(Report& r)
{
    double worst3 = 0.0;
    for (double f0 : { 20.0, 40.0, 100.0 })
    {
        const auto y = renderThdTone(f0, kToneDbfs, 1.0);
        const ThdResult m = measureThd(y, f0, kSr, kSkipSeconds);
        const double spec3 = linToDb(ironR3PowerLaw(f0));
        const double spec2 = spec3 - coeffs::kIronH2OffsetDb;

        char buf[192];
        std::snprintf(buf, sizeof(buf), "h3 %.2f dBc vs spec %.2f (err %+.2f), h2 %.2f vs %.2f",
                      m.thd3Dbc, spec3, m.thd3Dbc - spec3, m.thd2Dbc, spec2);
        r.note(std::to_string((int)f0) + " Hz: " + std::string(buf));

        r.near(m.thd3Dbc, spec3, 1.0, "THD3 at " + std::to_string((int)f0) + " Hz");
        // h2's depth is a measured lower bound rather than an identification, so
        // it gets the looser gate its provenance deserves.
        r.near(m.thd2Dbc, spec2, 1.5, "THD2 at " + std::to_string((int)f0) + " Hz");
        worst3 = std::max(worst3, std::fabs(m.thd3Dbc - spec3));
    }
    r.below(worst3, 1.0, "worst THD3 error across 20 / 40 / 100 Hz");
}

void testConstantPercentage(Report& r)
{
    // The measured validity window is roughly -6 dB to the amplifier rails; these
    // three levels sit inside it. Outside it the model extrapolates and this
    // property is not claimed.
    for (double f0 : { 20.0, 40.0 })
    {
        double lo = 1e30, hi = -1e30;
        for (double lvl : { -55.0, -45.0, -35.0 })
        {
            const ThdResult m = measureThd(renderThdTone(f0, lvl, 1.0), f0, kSr, kSkipSeconds);
            lo = std::min(lo, m.thd3Dbc);
            hi = std::max(hi, m.thd3Dbc);
        }
        r.below(hi - lo, 0.05,
                "THD3 is level-independent at " + std::to_string((int)f0) + " Hz (20 dB span)");
    }
}

void testZeroFundamental(Report& r)
{
    // Drive the layer directly so the measurement sees its contribution alone,
    // undiluted by 60 dB of amplifier gain and unfiltered by the response.
    const double srOs = kSr * kOs;
    IronLayer iron;
    iron.prepare(srOs);

    for (double f0 : { 20.0, 40.0, 100.0 })
    {
        const size_t n = (size_t)(srOs * 4.0);
        std::vector<float> added(n);
        double fundamental = 0.0;
        {
            std::vector<float> in(n);
            for (size_t i = 0; i < n; ++i)
                in[i] = (float)(0.05 * std::sin(2.0 * kPiT * f0 * (double)i / srOs));
            for (size_t i = 0; i < n; ++i)
                added[i] = (float)(iron.processSample(in[i], 1.0) - in[i]);
            iron.reset();
            fundamental = measureAmplitude(in, f0, srOs, 2.0);
        }

        const double leak = measureAmplitude(added, f0, srOs, 2.0);
        const double third = measureAmplitude(added, 3.0 * f0, srOs, 2.0);

        // There IS a fundamental in the added signal, and it is not a bug: the
        // mean-square detector ripples at 2*f0, and that ripple beating against
        // the 3*f0 Chebyshev term lands back on the fundamental. Its depth is
        // exactly the one-pole's attenuation at 2*f0, 1 / (2*pi * 2*f0 * tau) —
        // measured -35.963 dB at 20 Hz here and -35.963 dB in the Python
        // reference, so it is a property of the model both share.
        //
        // Gating against that prediction rather than against "small" is the
        // stronger test: it says the ONLY fundamental present is the detector
        // ripple, at the level the detector's own time constant dictates.
        const double predicted =
            linToDb(1.0 / (2.0 * kPiT * 2.0 * f0 * coeffs::kDetectorTauS));
        r.near(linToDb(leak / third), predicted, 0.4,
               "fundamental in the iron term is the detector ripple at "
                   + std::to_string((int)f0) + " Hz");

        // What actually matters: relative to the signal it is bolted onto, the
        // layer's fundamental contribution is inaudible, so it cannot shift gain.
        r.below(linToDb(leak / fundamental), -80.0,
                "Chebyshev fundamental leak vs input at " + std::to_string((int)f0) + " Hz");
    }
}

void testIronAmount(Report& r)
{
    // amount = 0 must return the input UNCHANGED, bit for bit. Test the claim
    // directly against the layer: y == x, not "two renders of the same settings
    // agree with each other", which is a determinism check that cannot fail.
    //
    // What amount = 0 does NOT do is skip the work — the sidechain and the
    // detector still run, so the layer stays warmed up and turning the knob back
    // up does not restart the envelope from its floor.
    {
        IronLayer iron;
        iron.prepare(kSr * kOs);

        bool exact = true;
        double worst = 0.0;
        for (int i = 0; i < 200000; ++i)
        {
            // Deliberately ugly drive: a tone plus a slow ramp plus values small
            // enough to be near the detector floor and large enough to be past it.
            const double t = (double)i / (kSr * kOs);
            const double x = 0.3 * std::sin(2.0 * kPiT * 47.0 * t)
                           + 0.01 * std::sin(2.0 * kPiT * 3100.0 * t)
                           + 1e-9 * (double)(i % 17);
            const double y = iron.processSample(x, 0.0);
            if (y != x)
            {
                exact = false;
                worst = std::max(worst, std::fabs(y - x));
            }
        }
        r.check(exact, "iron amount 0 returns the input bit for bit",
                exact ? "" : "worst |y-x| = " + std::to_string(worst));

        // And the layer is not inert at amount 0 — it is still tracking, which is
        // why the knob is a depth control and not a bypass switch.
        const double warm = iron.processSample(0.2, 1.0);
        r.check(warm != 0.2, "amount 0 keeps the detector running (knob is depth, not bypass)");
    }

    // Doubling the amount doubles the harmonic (+6.02 dB): the layer is linear in
    // its own depth, which is what makes the control legible.
    const ThdResult one = measureThd(renderThdTone(40.0, kToneDbfs, 1.0), 40.0, kSr, kSkipSeconds);
    const ThdResult two = measureThd(renderThdTone(40.0, kToneDbfs, 2.0), 40.0, kSr, kSkipSeconds);
    r.near(two.thd3Dbc - one.thd3Dbc, 6.0206, 0.05, "iron amount 2 is +6 dB of h3");
}

} // namespace

int main()
{
    Report r("pre35 iron_test");
    testThdVsSpec(r);
    testConstantPercentage(r);
    testZeroFundamental(r);
    testIronAmount(r);
    return r.exitCode();
}
