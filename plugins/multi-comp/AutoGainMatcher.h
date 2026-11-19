#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

namespace MultiComp
{

/**
 * Auto-gain by slow level matching.
 *
 * The previous auto-makeup inverted the compressor's own gain reduction through
 * a ~200 ms smoother. That sits inside the pumping band: the makeup rides the
 * compression envelope and partially undoes it, so the compressor breathes. It
 * was also blind to everything downstream of the gain cell — the Opto's tube and
 * output transformer add energy the GR figure never sees, which is why that mode
 * carried a hand-tuned -1.5 dB correction.
 *
 * This measures instead. It tracks input and output power through a long
 * one-pole (default 2 s, far longer than program dynamics) and asks for the gain
 * that makes the two match. It cannot pump, because over any window shorter than
 * the estimator's it is effectively a constant; it needs no per-mode fudge,
 * because it measures the real output including tubes, transformers and the
 * dry/wet blend.
 *
 * Not real-time hostile: no allocation, no locks, a handful of flops per block.
 */
class AutoGainMatcher
{
public:
    /** @param sampleRate  Host sample rate. Call from prepareToPlay. */
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        reset();
    }

    /** Drops the estimate. The next block with signal primes it directly. */
    void reset()
    {
        meanSquareIn = 0.0;
        meanSquareOut = 0.0;
        primed = false;
        currentGain = 1.0f;
    }

    /** Estimator window in seconds. Longer = steadier, slower to settle. */
    void setWindowSeconds (double seconds) { windowSeconds = juce::jmax (0.05, seconds); }

    /** Hard limit on the correction, applied symmetrically. */
    void setMaxCorrectionDb (float db) { maxCorrectionDb = juce::jlimit (0.0f, 40.0f, db); }

    /**
        Feeds one block and returns the linear gain to apply.

        @param inRms      RMS of the block BEFORE processing
        @param outRms     RMS of the same block AFTER processing, before this gain
        @param numSamples Block length, used to keep the window wall-clock accurate
                          regardless of buffer size
    */
    float update (float inRms, float outRms, int numSamples)
    {
        if (numSamples <= 0)
            return currentGain;

        // Below the floor there is nothing to match, and dividing near-silence
        // by near-silence would wind the estimate up on noise. Hold instead.
        constexpr float kSilenceFloor = 1.0e-5f;    // about -100 dBFS
        if (inRms < kSilenceFloor || outRms < kSilenceFloor)
            return currentGain;

        const double msIn = static_cast<double> (inRms) * inRms;
        const double msOut = static_cast<double> (outRms) * outRms;

        if (! primed)
        {
            // Prime the ESTIMATOR, not the output gain: the caller ramps towards
            // whatever we return, so a mode or preset change converges quickly
            // without the instant jump the old prime path produced.
            meanSquareIn = msIn;
            meanSquareOut = msOut;
            primed = true;
        }
        else
        {
            const double blockSeconds = static_cast<double> (numSamples) / sampleRate;
            const double alpha = 1.0 - std::exp (-blockSeconds / windowSeconds);
            meanSquareIn += alpha * (msIn - meanSquareIn);
            meanSquareOut += alpha * (msOut - meanSquareOut);
        }

        if (meanSquareOut <= 0.0 || meanSquareIn <= 0.0)
            return currentGain;

        // Power ratio -> dB is 10*log10, not 20: these are squared magnitudes.
        const auto correctionDb = static_cast<float> (10.0 * std::log10 (meanSquareIn / meanSquareOut));
        currentGain = juce::Decibels::decibelsToGain (
            juce::jlimit (-maxCorrectionDb, maxCorrectionDb, correctionDb));
        return currentGain;
    }

    /** Last gain returned by update(), linear. */
    float getCurrentGain() const noexcept { return currentGain; }

    /** True once the estimator has seen signal. */
    bool isPrimed() const noexcept { return primed; }

private:
    double sampleRate = 44100.0;
    double windowSeconds = 2.0;
    float maxCorrectionDb = 12.0f;

    double meanSquareIn = 0.0;
    double meanSquareOut = 0.0;
    bool primed = false;
    float currentGain = 1.0f;
};

} // namespace MultiComp
