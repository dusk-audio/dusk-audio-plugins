// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// chain_test — what the assembled Pre35DSP does to a signal.
//
// coeff_test proves the coefficients are the model's. This proves the chain
// wired around them delivers the model's GAIN: that the taper curve reaches the
// output undamaged, that the iron layer adds no fundamental on the way (so it
// cannot shift the gain it is bolted onto), that auto-gain cancels exactly what
// it claims to, and that none of it depends on the host's block size.

#include "Pre35DSP.hpp"
#include "TestSupport.hpp"

#include <algorithm>
#include <vector>

using namespace pre35;
using namespace pre35test;

namespace
{

constexpr double kSr = 48000.0;
constexpr int    kOs = 8;

/** Render a steady tone through a freshly prepared core. */
std::vector<float> renderTone(double f0, double amplitudeDbfs, double seconds,
                              int padIndex, double trim, double ironAmount,
                              bool autoGain, int block = 512)
{
    Pre35DSP dsp;
    dsp.setPadIndex(padIndex);
    dsp.setTrimPercent(trim);
    dsp.setIronAmount(ironAmount);
    dsp.setAutoGain(autoGain);
    dsp.setNoiseEnabled(false);
    dsp.prepare(kSr, kOs);

    std::vector<float> buf = makeSine(f0, dbToLinT(amplitudeDbfs), seconds, kSr);
    for (size_t i = 0; i < buf.size(); i += (size_t)block)
        dsp.process(buf.data() + i, (int)std::min((size_t)block, buf.size() - i));
    return buf;
}

//==============================================================================
void testGainVsModel(Report& r)
{
    // 1 kHz sits inside the 200 Hz-2 kHz midband the whole gain convention is
    // referenced to, so the expected level is the cal gain plus whatever the
    // normalised response does at exactly 1 kHz (a few hundredths of a dB, not
    // zero — the normalisation pins the MEDIAN, not any one point).
    const double f0 = 1000.0;
    const double inDbfs = -60.0;   // +58 dB of trim still lands under 0 dBFS

    double worst = 0.0;
    for (int pad = 0; pad < coeffs::kNumPads; ++pad)
        for (double trim : { 0.0, 10.0, 25.0, 50.0, 75.0, 90.0, 100.0 })
        {
            const auto y = renderTone(f0, inDbfs, 1.0, pad, trim, 0.0, false);
            const double measured = linToDb(measureAmplitude(y, f0, kSr, 0.2)) - inDbfs;
            const double expected = gainCalDb(trim, pad) + responseDb(f0, pad, trim);
            worst = std::max(worst, std::fabs(measured - expected));
        }
    r.below(worst, 0.02, "chain gain vs model curve, 21 (pad, trim) grid points");

    // The Chebyshev terms carry no fundamental, so switching the iron layer on
    // must not move the gain. This is the chain-level statement of the property
    // iron_test proves on the layer in isolation.
    double worstIron = 0.0;
    for (int pad = 0; pad < coeffs::kNumPads; ++pad)
        for (double trim : { 0.0, 50.0, 100.0 })
        {
            const auto dry = renderTone(f0, -45.0, 1.0, pad, trim, 0.0, false);
            const auto wet = renderTone(f0, -45.0, 1.0, pad, trim, 1.0, false);
            worstIron = std::max(worstIron,
                                 std::fabs(linToDb(measureAmplitude(wet, f0, kSr, 0.2))
                                         - linToDb(measureAmplitude(dry, f0, kSr, 0.2))));
        }
    r.below(worstIron, 0.005, "iron layer does not shift the fundamental");
}

void testAutoGain(Report& r)
{
    const double f0 = 1000.0;
    const double inDbfs = -20.0;

    // With auto-gain the only thing left between input and output is the
    // response, which is 0 dB at midband by construction. State that first —
    // otherwise "flat" would be flat against a moving reference.
    double worstResp = 0.0;
    for (int pad = 0; pad < coeffs::kNumPads; ++pad)
        for (double trim : { 0.0, 50.0, 100.0 })
            worstResp = std::max(worstResp, std::fabs(responseDb(f0, pad, trim)));
    r.below(worstResp, 0.05, "response at 1 kHz is within a whisker of its midband median");

    double worst = 0.0, lo = 1e30, hi = -1e30;
    for (int pad = 0; pad < coeffs::kNumPads; ++pad)
        for (double trim : { 0.0, 10.0, 25.0, 50.0, 75.0, 90.0, 100.0 })
        {
            const auto y = renderTone(f0, inDbfs, 1.0, pad, trim, 1.0, true);
            const double measured = linToDb(measureAmplitude(y, f0, kSr, 0.2)) - inDbfs;
            worst = std::max(worst, std::fabs(measured - responseDb(f0, pad, trim)));
            lo = std::min(lo, measured);
            hi = std::max(hi, measured);
        }
    r.below(worst, 0.02, "auto-gain cancels the modelled taper+pad exactly");

    // The user-visible claim: sweep trim across its whole 25 dB range and the
    // level does not move. 76 dB of raw range collapsing to this is the point.
    r.below(hi - lo, 0.1, "auto-gain output spread across the full pad+trim grid");
}

void testBlockSizeIndependence(Report& r)
{
    const auto ref = renderTone(440.0, -20.0, 0.5, 0, 42.0, 1.0, false, 512);

    bool identical = true;
    for (int block : { 1, 7, 31, 32, 33, 64, 1024 })
    {
        const auto other = renderTone(440.0, -20.0, 0.5, 0, 42.0, 1.0, false, block);
        for (size_t i = 0; i < ref.size() && identical; ++i)
            identical = ref[i] == other[i];
        if (! identical)
        {
            r.check(false, "block-size independence", "diverged at block=" + std::to_string(block));
            return;
        }
    }
    r.check(identical, "render is bit-identical at block sizes 1, 7, 31, 32, 33, 64, 512, 1024");
}

void testToggleIsBlockSizeIndependent(Report& r)
{
    // Every parameter is snapshotted at the control step, so a toggle must land
    // at the same SAMPLE INDEX no matter how the host cut up the buffer. The
    // noise flag is the interesting one: it also gates whether the RNG stream
    // advances, so getting it wrong desynchronises the hiss as well as moving it.
    //
    // The toggles are applied at fixed sample offsets; only the block size varies.
    constexpr size_t kToggleA = 12000;   // noise on
    constexpr size_t kToggleB = 24000;   // auto-gain on, noise off

    auto render = [](int block) {
        Pre35DSP dsp;
        dsp.setNoiseSeed(0xABCDEF);
        dsp.setPadIndex(0);
        dsp.setTrimPercent(60.0);
        dsp.setNoiseEnabled(false);
        dsp.prepare(kSr, kOs);

        std::vector<float> buf = makeSine(220.0, dbToLinT(-24.0), 0.75, kSr);
        size_t i = 0;
        while (i < buf.size())
        {
            if (i == kToggleA) dsp.setNoiseEnabled(true);
            if (i == kToggleB) { dsp.setNoiseEnabled(false); dsp.setAutoGain(true); }

            size_t n = std::min((size_t)block, buf.size() - i);
            for (size_t edge : { kToggleA, kToggleB })   // never straddle a toggle
                if (i < edge && edge < i + n)
                    n = edge - i;
            dsp.process(buf.data() + i, (int)n);
            i += n;
        }
        return buf;
    };

    const auto ref = render(512);
    for (int block : { 1, 7, 31, 32, 33, 64, 1024 })
    {
        const auto other = render(block);
        for (size_t i = 0; i < ref.size(); ++i)
            if (ref[i] != other[i])
            {
                r.check(false, "toggles are block-size independent",
                        "block=" + std::to_string(block) + " diverged at sample "
                            + std::to_string(i));
                return;
            }
    }
    r.check(true, "mid-render noise and auto-gain toggles are block-size independent");
}

void testTogglesAreSmoothed(Report& r)
{
    // pad 0 / trim 100 is the worst case for both: 39.27 dB of pad and 58.20 dB
    // of cal gain, each of which would be a full-scale bang if it were switched
    // rather than faded.
    constexpr double kTone = 120.0;
    constexpr size_t kFlipAt = 24000;   // 0.5 s

    auto render = [](int flip) {        // 0 = nothing, 1 = pad, 2 = auto-gain
        Pre35DSP dsp;
        dsp.setPadIndex(0);
        dsp.setTrimPercent(100.0);
        dsp.setIronAmount(1.0);
        dsp.setNoiseEnabled(false);
        dsp.prepare(kSr, kOs);

        std::vector<float> buf = makeSine(kTone, dbToLinT(-60.0), 1.5, kSr);
        const size_t block = 64;
        for (size_t i = 0; i < buf.size(); i += block)
        {
            if (i == kFlipAt)
            {
                if (flip == 1) dsp.setPadIndex(2);
                if (flip == 2) dsp.setAutoGain(true);
            }
            dsp.process(buf.data() + i, (int)std::min(block, buf.size() - i));
        }
        return buf;
    };

    // Skip 0.25 s: the render's own onset is a real discontinuity and would set a
    // floor high enough to hide a small one at the flip.
    constexpr double kSkip = 0.25;
    const auto steadyRender = render(0);
    const double steady   = maxSineDiscontinuity(steadyRender, kTone, kSr, kSkip);
    const double padFlip  = maxSineDiscontinuity(render(1), kTone, kSr, kSkip);
    const double autoFlip = maxSineDiscontinuity(render(2), kTone, kSr, kSkip);

    // The metric has to be able to FAIL. Splice the transition in as a hard step
    // instead of a fade and confirm it lights up — that is what an unsmoothed
    // auto-gain flip looked like before this was a crossfade.
    std::vector<float> spliced = steadyRender;
    const double stepGain = dbToLinT(-gainCalDb(100.0, 0));
    for (size_t i = kFlipAt; i < spliced.size(); ++i)
        spliced[i] = (float)((double)spliced[i] * stepGain);
    const double stepped = maxSineDiscontinuity(spliced, kTone, kSr, kSkip);

    // Normalise by the size of the transition in nepers. A one-pole smoother's
    // worst per-sample step is proportional to the log of the gain ratio it has
    // to cover, so this is the number that should MATCH between the two switches
    // — comparing raw discontinuities would just say "58 dB is more than 39 dB".
    const double padSpan  = std::fabs(coeffs::kPads[2].offsetDb) / 8.685889638;
    const double autoSpan = std::fabs(gainCalDb(100.0, 0)) / 8.685889638;

    char buf[288];
    std::snprintf(buf, sizeof(buf),
                  "steady %.3e, pad %.3e (/%.2f Np = %.3e), auto %.3e (/%.2f Np = %.3e), "
                  "hard step %.3e",
                  steady, padFlip, padSpan, padFlip / padSpan,
                  autoFlip, autoSpan, autoFlip / autoSpan, stepped);
    r.note(std::string("discontinuity: ") + buf);

    r.check(stepped > 100.0 * steady, "the metric detects a spliced hard step",
            "stepped/steady = " + std::to_string(stepped / steady));
    r.check(padFlip > steady, "the pad switch is visible to the discontinuity metric");
    // The bound the pad switch meets, per neper of transition. An unsmoothed
    // auto-gain flip put a 0.106 step on a signal whose steady state was 0.001,
    // i.e. two orders of magnitude above this.
    r.below(autoFlip / autoSpan, 1.25 * (padFlip / padSpan),
            "auto-gain toggle is smoothed to the pad switch's bound", "/Np");
    // It comfortably beats the pad switch, and should: auto-gain only moves a
    // scalar, while a pad switch ALSO retunes the response cascade (3.9 -> 8.9 Hz
    // highpass, 33.5 -> 56.8 kHz lowpass) under its own filter state. That part is
    // a real switch on real hardware and is not smoothed.
    r.below(autoFlip, stepped / 50.0,
            "auto-gain toggle is a fade, not the step it replaced", "");
}

void testLatencyAndOversampling(Report& r)
{
    Pre35DSP dsp;
    dsp.setIronAmount(0.0);
    dsp.setTrimPercent(0.0);
    dsp.prepare(kSr, kOs);

    r.check(dsp.latencySamples() == 2 * kResamplerStageLatency, "reported latency",
            std::to_string(dsp.latencySamples()) + " samples");

    std::vector<float> imp(256, 0.0f);
    imp[0] = 1.0f;
    dsp.process(imp.data(), (int)imp.size());

    size_t peak = 0;
    for (size_t i = 1; i < imp.size(); ++i)
        if (std::fabs(imp[i]) > std::fabs(imp[peak]))
            peak = i;
    r.check((int)peak == dsp.latencySamples(), "impulse peak lands at the reported latency",
            "peak at " + std::to_string(peak));

    // The auto factor has to keep the internal rate above the transformer's
    // 33-57 kHz corners at every rate a host might hand over. Note the last row:
    // a host already running above the target rate gets factor 1, and factor 1
    // has NO resampler and therefore NO latency. Anything that assumes a constant
    // 20 is wrong there.
    struct { double sr; int expect; } cases[] = {
        { 44100.0, 8 }, { 48000.0, 8 }, { 88200.0, 4 }, { 96000.0, 4 },
        { 176400.0, 2 }, { 192000.0, 2 }, { 384000.0, 1 },
    };
    for (const auto& c : cases)
    {
        Pre35DSP d;
        d.prepare(c.sr);
        r.check(d.oversampleFactor() == c.expect,
                "auto oversampling at " + std::to_string((int)c.sr) + " Hz",
                std::to_string(d.oversampleFactor()) + "x, internal "
                    + std::to_string((int)d.oversampledRate()) + " Hz");
        const int expectLatency = (c.expect > 1) ? 2 * kResamplerStageLatency : 0;
        r.check(d.latencySamples() == expectLatency,
                "latency at " + std::to_string((int)c.sr) + " Hz",
                std::to_string(d.latencySamples()) + " samples");
    }

    // Factor 1 must still be a working (if band-limited) signal path, not just a
    // configuration that reports zero latency and then misbehaves.
    Pre35DSP flat;
    flat.setTrimPercent(50.0);
    flat.prepare(kSr, 1);
    r.check(flat.latencySamples() == 0, "explicit factor 1 reports zero latency");
    std::vector<float> tone = makeSine(1000.0, dbToLinT(-60.0), 0.5, kSr);
    flat.process(tone.data(), (int)tone.size());
    bool finite = true;
    for (float v : tone)
        finite = finite && std::isfinite(v);
    r.check(finite, "factor 1 renders finite audio");
}

void testResetIsSteadyState(Report& r)
{
    // A render must not depend on how long the smoothers have been running: an
    // offline comparison that quietly ramps for its first 20 ms is a null test
    // that fails for a reason nobody can find.
    //
    // NOISE IS EXCLUDED, and not by accident: reset() does not rewind the RNG
    // stream (it is also the fault-recovery path, where a rewind would make one
    // glitch repeat), so warm and fresh only agree with the hiss disabled. Both
    // renders below have noise off.
    const auto a = renderTone(1000.0, -30.0, 0.3, 2, 88.0, 1.0, false);

    Pre35DSP dsp;
    dsp.setPadIndex(2);
    dsp.setTrimPercent(88.0);
    dsp.setNoiseEnabled(false);
    dsp.prepare(kSr, kOs);
    std::vector<float> warm = makeSine(1000.0, dbToLinT(-30.0), 0.3, kSr);
    dsp.process(warm.data(), (int)warm.size());     // run it, then rewind
    dsp.reset();
    std::vector<float> b = makeSine(1000.0, dbToLinT(-30.0), 0.3, kSr);
    dsp.process(b.data(), (int)b.size());

    bool identical = true;
    for (size_t i = 0; i < a.size(); ++i)
        identical = identical && (a[i] == b[i]);
    r.check(identical, "reset() returns the core to a fresh steady state (noise off)");

    // And the documented exception, asserted rather than assumed: with noise ON
    // the same comparison must NOT match, or the header comment is a lie.
    Pre35DSP nz;
    nz.setNoiseEnabled(true);
    nz.setTrimPercent(50.0);
    nz.prepare(kSr, kOs);
    std::vector<float> first((size_t)(kSr * 0.1), 0.0f);
    nz.process(first.data(), (int)first.size());
    nz.reset();
    std::vector<float> second((size_t)(kSr * 0.1), 0.0f);
    nz.process(second.data(), (int)second.size());

    bool differs = false;
    for (size_t i = 0; i < first.size() && ! differs; ++i)
        differs = first[i] != second[i];
    r.check(differs, "reset() deliberately does NOT rewind the noise stream");
}

} // namespace

int main()
{
    Report r("pre35 chain_test");
    testGainVsModel(r);
    testAutoGain(r);
    testBlockSizeIndependence(r);
    testToggleIsBlockSizeIndependent(r);
    testTogglesAreSmoothed(r);
    testLatencyAndOversampling(r);
    testResetIsSteadyState(r);
    return r.exitCode();
}
