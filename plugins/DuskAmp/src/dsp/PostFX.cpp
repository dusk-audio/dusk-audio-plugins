#include "PostFX.h"

void PostFX::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;

    // Allocate delay buffers (max 2 seconds)
    int maxSamples = static_cast<int> (sampleRate * 2.0) + 1;
    delayBufL_.assign (static_cast<size_t> (maxSamples), 0.0f);
    delayBufR_.assign (static_cast<size_t> (maxSamples), 0.0f);

    // Spring tank — handles its own internal sizing/state.
    reverb_.prepare (sampleRate, maxBlockSize);
    reverbBuffer_.setSize (2, maxBlockSize, false, true, true);

    updateDelayFbFilterCoeff();
    reset();
}

void PostFX::reset()
{
    std::fill (delayBufL_.begin(), delayBufL_.end(), 0.0f);
    std::fill (delayBufR_.begin(), delayBufR_.end(), 0.0f);
    delayWritePos_ = 0;
    delayFbFilterStateL_ = 0.0f;
    delayFbFilterStateR_ = 0.0f;
    reverb_.clearBuffers();
}

void PostFX::setDelayEnabled (bool on)
{
    delayEnabled_ = on;
}

void PostFX::setDelayTime (float ms)
{
    int maxDelay = delayBufL_.empty() ? 1 : static_cast<int> (delayBufL_.size()) - 1;
    delaySamples_ = std::clamp (static_cast<int> (ms * 0.001f * static_cast<float> (sampleRate_)),
                                1, std::max (1, maxDelay));
}

void PostFX::setDelayFeedback (float fb01)
{
    delayFeedback_ = std::clamp (fb01, 0.0f, 0.95f); // Cap at 0.95 to prevent runaway
}

void PostFX::setDelayMix (float mix01)
{
    delayMix_ = std::clamp (mix01, 0.0f, 1.0f);
}

void PostFX::setReverbEnabled (bool on)
{
    reverbEnabled_ = on;
}

void PostFX::setReverbMix (float mix01)
{
    reverbMix_ = std::clamp (mix01, 0.0f, 1.0f);
}

void PostFX::setReverbDecay (float decay01)
{
    // Map 0..1 user knob to seconds: 0 → 0.5 s (short tank flutter), 1 → 5 s
    // (long, sustained spring tail). SpringEngine's per-spring feedback is
    // computed from this RT60 target.
    const float seconds = 0.5f + std::clamp (decay01, 0.0f, 1.0f) * 4.5f;
    reverb_.setDecayTime (seconds);
}

void PostFX::process (float* left, float* right, int numSamples)
{
    int bufSize = static_cast<int> (delayBufL_.size());

    // --- Delay ---
    if (delayEnabled_ && bufSize > 0)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            // Read from delay buffer
            int readPos = delayWritePos_ - delaySamples_;
            if (readPos < 0)
                readPos += bufSize;

            float delayedL = delayBufL_[static_cast<size_t> (readPos)];
            float delayedR = delayBufR_[static_cast<size_t> (readPos)];

            // Apply feedback filter (one-pole lowpass in feedback loop)
            delayFbFilterStateL_ += delayFbFilterCoeff_ * (delayedL - delayFbFilterStateL_);
            delayFbFilterStateR_ += delayFbFilterCoeff_ * (delayedR - delayFbFilterStateR_);
            // Flush denormals
            if (std::abs(delayFbFilterStateL_) < 1e-15f) delayFbFilterStateL_ = 0.0f;
            if (std::abs(delayFbFilterStateR_) < 1e-15f) delayFbFilterStateR_ = 0.0f;

            float filteredFbL = delayFbFilterStateL_;
            float filteredFbR = delayFbFilterStateR_;

            // Write to delay buffer: input + filtered feedback
            delayBufL_[static_cast<size_t> (delayWritePos_)] = left[i] + filteredFbL * delayFeedback_;
            delayBufR_[static_cast<size_t> (delayWritePos_)] = right[i] + filteredFbR * delayFeedback_;

            // Mix dry/wet
            left[i]  = left[i]  * (1.0f - delayMix_) + delayedL * delayMix_;
            right[i] = right[i] * (1.0f - delayMix_) + delayedR * delayMix_;

            delayWritePos_++;
            if (delayWritePos_ >= bufSize)
                delayWritePos_ = 0;
        }
    }

    // --- Reverb (spring tank, shared SpringEngine) ---
    if (reverbEnabled_ && reverbMix_ > 0.0f)
    {
        // SpringEngine takes separate L/R buffers for in/out. Use the
        // pre-allocated reverbBuffer_ as the wet destination so the dry
        // signal in left[]/right[] stays intact for the mix below.
        float* wetL = reverbBuffer_.getWritePointer (0);
        float* wetR = reverbBuffer_.getWritePointer (1);
        reverb_.process (left, right, wetL, wetR, numSamples);

        // Crossfade: keep dry at (1 − mix), add wet at mix.
        const float wet = reverbMix_;
        const float dry = 1.0f - reverbMix_;
        for (int i = 0; i < numSamples; ++i)
        {
            left[i]  = left[i]  * dry + wetL[i] * wet;
            right[i] = right[i] * dry + wetR[i] * wet;
        }
    }
}

void PostFX::updateDelayFbFilterCoeff()
{
    // One-pole lowpass at ~4kHz for feedback darkening
    float fc = 4000.0f;
    float w = 2.0f * 3.14159265359f * fc / static_cast<float> (sampleRate_);
    delayFbFilterCoeff_ = w / (w + 1.0f);
}
