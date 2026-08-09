// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Pre35DSP.cpp — see Pre35DSP.hpp for the chain, the latency contract and the
// list of things this model deliberately does not do.

#include "Pre35DSP.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace pre35
{

namespace
{
/** Above this internal rate the transformer corners (33-57 kHz) and the shaper's
    tripled bandwidth both fit comfortably. 344 kHz picks 8x at 44.1 and 48 kHz,
    4x at 88.2 and 96 kHz, 2x at 176.4 and 192 kHz. */
constexpr double kTargetInternalRate = 344000.0;

/** Coefficients are rebuilt when the smoothed trim has moved by more than this
    many percent. Small enough to be inaudible, large enough that a settled knob
    stops rebuilding entirely. */
constexpr double kTrimCoeffEpsilon = 1.0e-6;

double sanitise(double v) noexcept
{
    return std::isfinite(v) ? v : 0.0;
}
} // namespace

//==============================================================================
Pre35DSP::Pre35DSP()
{
    trimSm.snap(0.0);
    padDbSm.snap(padOffsetDb(0));
    ironSm.snap(1.0);
    outputDbSm.snap(0.0);
    autoGainSm.snap(0.0);
}

void Pre35DSP::prepare(double sampleRate, int oversampleFactor)
{
    hostRate = (sampleRate > 0.0) ? sampleRate : 48000.0;

    if (oversampleFactor > 0)
    {
        factor = std::min(oversampleFactor, kMaxOversampleFactor);
    }
    else
    {
        factor = 1;
        while (factor < kMaxOversampleFactor && hostRate * factor < kTargetInternalRate)
            factor *= 2;
    }

    latency = (factor > 1) ? 2 * kResamplerStageLatency : 0;

    upsampler.prepare(factor);
    downsampler.prepare(factor);

    const double osRate = hostRate * factor;
    iron.prepare(osRate);
    noise.prepare(osRate, factor, noiseSeed);

    const double stepSeconds = static_cast<double>(kChunk) / hostRate;
    trimSm.setTimeConstant(kSmoothingTauSeconds, stepSeconds);
    padDbSm.setTimeConstant(kSmoothingTauSeconds, stepSeconds);
    ironSm.setTimeConstant(kSmoothingTauSeconds, stepSeconds);
    outputDbSm.setTimeConstant(kSmoothingTauSeconds, stepSeconds);
    autoGainSm.setTimeConstant(kSmoothingTauSeconds, stepSeconds);

    // Force a rebuild: the cached (pad, trim) is meaningless at a new rate.
    coeffPad  = -1;
    coeffTrim = -1.0;
    prepared  = true;

    reset();
}

void Pre35DSP::reset()
{
    upsampler.reset();
    downsampler.reset();
    // NOTE: noise.reset() clears the 1/f shaper but NOT the RNG stream — see the
    // header. This is also the fault-recovery path, and rewinding the hiss there
    // would make a one-off glitch repeat.
    iron.reset();
    noise.reset();
    response.reset();
    chunkPhase = 0;

    // Snap every smoother to its target so the first sample out is steady state.
    // Anything else makes an offline render depend on how long ago prepare() ran.
    const int    pad  = clampPadIndex(padTarget.load(std::memory_order_relaxed));
    const double trim = trimTarget.load(std::memory_order_relaxed);

    trimSm.snap(trim);
    padDbSm.snap(padOffsetDb(pad));
    ironSm.snap(ironTarget.load(std::memory_order_relaxed));
    outputDbSm.snap(outputDbTarget.load(std::memory_order_relaxed));
    autoGainSm.snap(autoGainOn.load(std::memory_order_relaxed) ? 1.0 : 0.0);
    noiseActive = noiseOn.load(std::memory_order_relaxed);

    if (prepared)
        rebuildResponse(pad, trim);

    padGainPrev    = padGainNext    = dbToLin(padDbSm.value);
    taperGainPrev  = taperGainNext  = dbToLin(taperGainDb(trimSm.value));
    ironAmountPrev = ironAmountNext = ironSm.value;
    outGainPrev    = outGainNext    = dbToLin(currentOutputGainDb());
}

//==============================================================================
void Pre35DSP::setPadIndex(int padIndex)
{
    padTarget.store(clampPadIndex(padIndex), std::memory_order_relaxed);
}

void Pre35DSP::setTrimPercent(double percent)
{
    const double p = std::min(std::max(sanitise(percent), 0.0), 100.0);
    trimTarget.store(p, std::memory_order_relaxed);
}

void Pre35DSP::setIronAmount(double amount)
{
    const double a = std::min(std::max(sanitise(amount), 0.0), 2.0);
    ironTarget.store(a, std::memory_order_relaxed);
}

void Pre35DSP::setNoiseEnabled(bool enabled)
{
    noiseOn.store(enabled, std::memory_order_relaxed);
}

void Pre35DSP::setAutoGain(bool enabled)
{
    autoGainOn.store(enabled, std::memory_order_relaxed);
}

void Pre35DSP::setOutputGainDb(double db)
{
    const double g = std::min(std::max(sanitise(db), -60.0), 24.0);
    outputDbTarget.store(g, std::memory_order_relaxed);
}

void Pre35DSP::setNoiseSeed(uint64_t seed)
{
    noiseSeed = seed;
}

//==============================================================================
double Pre35DSP::currentGainCalDb() const noexcept
{
    return taperGainDb(trimSm.value) + padDbSm.value;
}

double Pre35DSP::currentOutputGainDb() const noexcept
{
    // Blend, not branch. autoGainSm sits at exactly 0 or 1 once settled, so the
    // steady-state answer is identical to the branch it replaced; in between it
    // crossfades the cancellation in over ~20 ms instead of dropping 58 dB in a
    // single control step.
    return outputDbSm.value - autoGainSm.value * currentGainCalDb();
}

//==============================================================================
void Pre35DSP::rebuildResponse(int padIndex, double trimPercent)
{
    // Cache ONLY on success. Recording the setting after a failed build would
    // pin the filter to whatever coefficients it last held and never try again;
    // leaving the cache stale makes the next control step retry. Unreachable for
    // this model (three sections at every pad, asserted in Pre35Model.hpp), which
    // is exactly why it must not fail silently if a future model changes that.
    if (! response.setFromAnalog(buildResponseZPK(padIndex, trimPercent),
                                 hostRate * factor))
        return;

    coeffPad  = padIndex;
    coeffTrim = trimPercent;
}

/** One control-rate step: advance the smoothers, retune what needs retuning, and
    publish the endpoints the next chunk's per-sample ramps interpolate towards. */
void Pre35DSP::updateControls()
{
    padGainPrev    = padGainNext;
    taperGainPrev  = taperGainNext;
    ironAmountPrev = ironAmountNext;
    outGainPrev    = outGainNext;

    const int pad = clampPadIndex(padTarget.load(std::memory_order_relaxed));

    trimSm.step(trimTarget.load(std::memory_order_relaxed));
    padDbSm.step(padOffsetDb(pad));
    ironSm.step(ironTarget.load(std::memory_order_relaxed));
    outputDbSm.step(outputDbTarget.load(std::memory_order_relaxed));
    autoGainSm.step(autoGainOn.load(std::memory_order_relaxed) ? 1.0 : 0.0);

    // Snapshotted here with everything else. Reading it once per process() call
    // instead would let a noise toggle land on a host buffer edge, which is the
    // one thing the control-rate scheme exists to prevent.
    noiseActive = noiseOn.load(std::memory_order_relaxed);

    // The GBW shelf corner is gbwHz / ampGain, so it tracks trim; the transformer
    // corners are per pad. Rebuild only when one of those two actually moved.
    if (pad != coeffPad || std::abs(trimSm.value - coeffTrim) > kTrimCoeffEpsilon)
        rebuildResponse(pad, trimSm.value);

    padGainNext    = dbToLin(padDbSm.value);
    taperGainNext  = dbToLin(taperGainDb(trimSm.value));
    ironAmountNext = ironSm.value;
    outGainNext    = dbToLin(currentOutputGainDb());
}

//==============================================================================
void Pre35DSP::process(float* buffer, int numSamples)
{
    if (buffer == nullptr || numSamples <= 0)
        return;

    // Guessing a sample rate here would produce audio at the wrong one — every
    // filter corner, the detector time constant and the smoothing rate all derive
    // from it. Silence is the honest failure; the assert is what makes it loud
    // during development. Unreachable from any plugin shell, which calls
    // prepare() in activate().
    if (! prepared)
    {
        assert(false && "Pre35DSP::process() called before prepare()");
        std::fill(buffer, buffer + numSamples, 0.0f);
        return;
    }

    for (int start = 0; start < numSamples; )
    {
        // Control steps land every kChunk HOST samples, never on buffer edges.
        // Anything else makes the smoothing rate a function of the host's block
        // size, and then the same render sounds different at 64 and at 512.
        if (chunkPhase == 0)
            updateControls();

        const int n = std::min(kChunk - chunkPhase, numSamples - start);
        float* block = buffer + start;

        const double inv = 1.0 / static_cast<double>(kChunk);
        const double dPad   = (padGainNext    - padGainPrev)    * inv;
        const double dTaper = (taperGainNext  - taperGainPrev)  * inv;
        const double dIron  = (ironAmountNext - ironAmountPrev) * inv;
        const double dOut   = (outGainNext    - outGainPrev)    * inv;

        bool bad = false;

        for (int i = 0; i < n; ++i)
        {
            const double t       = static_cast<double>(chunkPhase + i);
            const double padGain = padGainPrev    + dPad   * t;
            const double amp     = taperGainPrev  + dTaper * t;
            const double ironAmt = ironAmountPrev + dIron  * t;
            const double outGain = outGainPrev    + dOut   * t;

            // A host handing over a NaN must not poison the IIR state forever.
            const double x = sanitise(static_cast<double>(block[i]));

            upsampler.processSample(x, osBuffer);

            for (int k = 0; k < factor; ++k)
            {
                double s = osBuffer[k] * padGain;         // pad: ahead of the iron
                s = iron.processSample(s, ironAmt);       // input transformer
                if (noiseActive)
                    s += noise.nextSample();              // referred to the amp input
                s *= amp;                                 // the calibrated taper
                osBuffer[k] = response.process(s);        // unity at midband
            }

            const double y = downsampler.processBlock(osBuffer) * outGain;
            bad = bad || ! std::isfinite(y);
            block[i] = static_cast<float>(y);
        }

        // Recovery, not decoration: if anything in the chain has gone non-finite
        // (only reachable through a pathological parameter/state combination),
        // clearing state is the difference between a click and a dead channel.
        if (bad)
        {
            reset();                    // also puts chunkPhase back to 0
            for (int i = 0; i < n; ++i)
                block[i] = 0.0f;
        }
        else
        {
            chunkPhase += n;
            if (chunkPhase >= kChunk)
                chunkPhase = 0;
        }

        start += n;
    }
}

} // namespace pre35
