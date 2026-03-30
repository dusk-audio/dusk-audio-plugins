#include "PostFX.h"

void PostFX::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;

    // Allocate delay buffers (max 2 seconds)
    int maxSamples = static_cast<int> (sampleRate * 2.0) + 1;
    delayBufL_.assign (static_cast<size_t> (maxSamples), 0.0f);
    delayBufR_.assign (static_cast<size_t> (maxSamples), 0.0f);

    // Prepare reverb
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32> (maxBlockSize);
    spec.numChannels = 2;
    reverb_.prepare (spec);

    reverbBuffer_.setSize (2, maxBlockSize, false, true, true);

    reverbParams_.roomSize = 0.5f;
    reverbParams_.damping = 0.5f;
    reverbParams_.width = 1.0f;
    reverbParams_.wetLevel = 1.0f;  // We handle mix externally
    reverbParams_.dryLevel = 0.0f;
    reverbParams_.freezeMode = 0.0f;
    reverb_.setParameters (reverbParams_);

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
    reverb_.reset();
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
    reverbParams_.roomSize = std::clamp (decay01, 0.0f, 1.0f);
    reverb_.setParameters (reverbParams_);
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

    // --- Reverb ---
    if (reverbEnabled_ && reverbMix_ > 0.0f)
    {
        // Process reverb on pre-allocated buffer for separate wet/dry mixing
        reverbBuffer_.copyFrom (0, 0, left, numSamples);
        reverbBuffer_.copyFrom (1, 0, right, numSamples);

        juce::dsp::AudioBlock<float> block (reverbBuffer_);
        auto subBlock = block.getSubBlock (0, static_cast<size_t> (numSamples));
        juce::dsp::ProcessContextReplacing<float> context (subBlock);
        reverb_.process (context);

        // Mix: keep dry signal at unity, add wet at reverbMix_ level
        const float* wetL = reverbBuffer_.getReadPointer (0);
        const float* wetR = reverbBuffer_.getReadPointer (1);

        for (int i = 0; i < numSamples; ++i)
        {
            left[i]  = left[i]  * (1.0f - reverbMix_) + wetL[i] * reverbMix_;
            right[i] = right[i] * (1.0f - reverbMix_) + wetR[i] * reverbMix_;
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
