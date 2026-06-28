#pragma once

#include <algorithm>
#include <cmath>

// ReverbDucker — sidechain ducking of the wet reverb by the dry input.
//
// ChromaVerb-style: when the dry signal is loud (a vocal phrase, a drum hit),
// the wet tail is pushed down so the source stays clear; as the dry decays the
// tail swells back in. Applied to the WET output post-engine, pre-mix, so it is
// engine-agnostic — every reverb "space" gets it identically.
//
// Envelope follower: instantaneous attack (the tail ducks the instant the dry
// transient arrives, so the source punches through), smooth one-pole release
// (the tail returns over `release` time after the dry decays).
//
// `depth` 0 → duck gain is always 1.0 → the wet is passed through byte-for-byte
// unchanged (bit-null default). depth 1 → the tail is fully suppressed while the
// dry is at/above the reference level.
class ReverbDucker
{
public:
    void prepare (double sampleRate)
    {
        sampleRate_ = sampleRate;
        updateReleaseCoeff();
        reset();
    }

    void reset() { envelope_ = 0.0f; }

    // depth 0..1 — how far the tail is pushed down at full drive.
    void setDepth (float d) { depth_ = std::clamp (d, 0.0f, 1.0f); }

    // Release time for the tail to return after the dry decays.
    void setReleaseMs (float ms)
    {
        releaseMs_ = std::max (1.0f, ms);
        updateReleaseCoeff();
    }

    // Below this dry level the ducker does nothing; at/above refLevel above it
    // the duck is fully engaged. Linear amplitude.
    // Keep the invariant refLevelLin_ > thresholdLin_ after EITHER setter so the
    // ducking span (refLevel − threshold) can never collapse to zero/negative.
    void setThresholdLin (float t)
    {
        thresholdLin_ = std::max (0.0f, t);
        if (refLevelLin_ <= thresholdLin_) refLevelLin_ = thresholdLin_ + 1.0e-4f;
    }
    void setRefLevelLin  (float r)
    {
        refLevelLin_ = std::max ({ 1.0e-4f, r, thresholdLin_ + 1.0e-4f });
    }

    bool isActive() const { return depth_ > 0.0f; }

    // dry[] is the sidechain (pre-mix dry input); wet[] is modified in place.
    void process (const float* dryL, const float* dryR,
                  float* wetL, float* wetR, int numSamples)
    {
        if (depth_ <= 0.0f)
            return;                                  // bit-null: untouched wet

        for (int n = 0; n < numSamples; ++n)
        {
            const float level = std::max (std::abs (dryL[n]), std::abs (dryR[n]));
            if (level > envelope_)
                envelope_ = level;                   // instantaneous attack
            else
                envelope_ = level + releaseCoeff_ * (envelope_ - level);

            // Drive 0..1 from how far the dry env sits above threshold, normalized
            // over the span from threshold to the absolute reference level (the dry
            // level that fully engages the duck). Using the span — not refLevelLin_
            // alone — keeps "full duck at refLevel" true regardless of threshold.
            const float span  = std::max (refLevelLin_ - thresholdLin_, 1.0e-6f);
            const float drive = std::clamp ((envelope_ - thresholdLin_) / span, 0.0f, 1.0f);
            const float duckGain = 1.0f - depth_ * drive;

            wetL[n] *= duckGain;
            wetR[n] *= duckGain;
        }
    }

private:
    void updateReleaseCoeff()
    {
        releaseCoeff_ = std::exp (-1.0f / (0.001f * releaseMs_ * static_cast<float> (sampleRate_)));
    }

    double sampleRate_   = 48000.0;
    float  depth_        = 0.0f;        // 0 = no ducking (bit-null)
    float  releaseMs_    = 250.0f;
    float  thresholdLin_ = 0.003162f;   // ~ -50 dBFS — ignore the noise floor
    float  refLevelLin_  = 0.25f;       // ~ -12 dBFS dry fully engages the duck
    float  releaseCoeff_ = 0.0f;
    float  envelope_     = 0.0f;
};
