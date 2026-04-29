// SPDX-License-Identifier: GPL-3.0-or-later

// PostFX.cpp — Delay types + Dattorro plate reverb

#include "PostFX.h"
#include <cmath>

static constexpr float kPi = 3.14159265359f;
static constexpr float kTwoPi = 2.0f * kPi;

// ============================================================================
// Prepare / Reset
// ============================================================================

void PostFX::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;

    // Delay buffers (max 2 seconds)
    int maxSamples = static_cast<int> (sampleRate * 2.0) + 1;
    delayBufL_.assign (static_cast<size_t> (maxSamples), 0.0f);
    delayBufR_.assign (static_cast<size_t> (maxSamples), 0.0f);

    // Pre-delay buffer (max 200ms)
    int maxPreDelay = static_cast<int> (sampleRate * 0.2) + 1;
    preDelayBuf_.assign (static_cast<size_t> (maxPreDelay), 0.0f);

    // Initialize Dattorro reverb delays
    initDattorroDelays();
    updateDattorroParams();
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
    lfoPhase_ = 0.0f;
    wowPhase_ = 0.0f;
    flutterPhase_ = 0.0f;

    // Reverb reset
    std::fill (preDelayBuf_.begin(), preDelayBuf_.end(), 0.0f);
    preDelayWritePos_ = 0;
    for (auto& d : inputDiffuser_) d.clear();
    tankL_.apf.clear();
    tankR_.apf.clear();
    if (! tankL_.delay.empty()) std::fill (tankL_.delay.begin(), tankL_.delay.end(), 0.0f);
    if (! tankR_.delay.empty()) std::fill (tankR_.delay.begin(), tankR_.delay.end(), 0.0f);
    tankL_.delayWritePos = 0;
    tankR_.delayWritePos = 0;
    tankL_.dampState = 0.0f;
    tankR_.dampState = 0.0f;
}

// ============================================================================
// Delay setters
// ============================================================================

void PostFX::setDelayEnabled (bool on) { delayEnabled_ = on; }

void PostFX::setDelayType (int type)
{
    delayType_ = static_cast<DelayType> (std::clamp (type, 0, 2));
}

void PostFX::setDelayTime (float ms)
{
    int maxDelay = delayBufL_.empty() ? 1 : static_cast<int> (delayBufL_.size()) - 1;
    delaySamples_ = std::clamp (static_cast<int> (ms * 0.001f * static_cast<float> (sampleRate_)),
                                1, std::max (1, maxDelay));
}

void PostFX::setDelayFeedback (float fb01)
{
    delayFeedback_ = std::clamp (fb01, 0.0f, 0.95f);
}

void PostFX::setDelayMix (float mix01)
{
    delayMix_ = std::clamp (mix01, 0.0f, 1.0f);
}

// ============================================================================
// Reverb setters
// ============================================================================

void PostFX::setReverbEnabled (bool on) { reverbEnabled_ = on; }

void PostFX::setReverbMix (float mix01)
{
    reverbMix_ = std::clamp (mix01, 0.0f, 1.0f);
}

void PostFX::setReverbDecay (float decay01)
{
    reverbDecay_ = std::clamp (decay01, 0.0f, 0.99f);
    updateDattorroParams();
}

void PostFX::setReverbPreDelay (float ms)
{
    int maxPD = preDelayBuf_.empty() ? 1 : static_cast<int> (preDelayBuf_.size()) - 1;
    preDelaySamples_ = std::clamp (static_cast<int> (ms * 0.001f * static_cast<float> (sampleRate_)),
                                   0, std::max (0, maxPD));
}

void PostFX::setReverbDamping (float damping01)
{
    reverbDamping_ = std::clamp (damping01, 0.0f, 1.0f);
    updateDattorroParams();
}

void PostFX::setReverbSize (float size01)
{
    reverbSize_ = std::clamp (size01, 0.0f, 1.0f);
    // Size affects delay lengths — would need to re-init delays for full effect.
    // For now, it modulates decay and damping interaction.
    updateDattorroParams();
}

// ============================================================================
// Process
// ============================================================================

void PostFX::process (float* left, float* right, int numSamples)
{
    int bufSize = static_cast<int> (delayBufL_.size());
    float sr = static_cast<float> (sampleRate_);

    // === DELAY ===
    if (delayEnabled_ && bufSize > 0)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float readPosL = 0.0f, readPosR = 0.0f;
            float baseReadPos = static_cast<float> (delayWritePos_ - delaySamples_);
            if (baseReadPos < 0.0f) baseReadPos += static_cast<float> (bufSize);

            switch (delayType_)
            {
                case DelayType::Digital:
                    readPosL = readPosR = baseReadPos;
                    break;

                case DelayType::Analog:
                {
                    // Subtle chorus modulation on read position
                    float lfo = std::sin (lfoPhase_) * 0.5f; // ±0.5 samples
                    lfoPhase_ += kTwoPi * kLFOFreq / sr;
                    if (lfoPhase_ >= kTwoPi) lfoPhase_ -= kTwoPi;
                    readPosL = baseReadPos + lfo;
                    readPosR = baseReadPos - lfo; // opposite phase for stereo width
                    break;
                }

                case DelayType::Tape:
                {
                    // Wow (slow pitch drift) + flutter (fast jitter)
                    float wow = std::sin (wowPhase_) * 1.5f;
                    float flutter = std::sin (flutterPhase_) * 0.3f;
                    wowPhase_ += kTwoPi * kWowFreq / sr;
                    flutterPhase_ += kTwoPi * kFlutterFreq / sr;
                    if (wowPhase_ >= kTwoPi) wowPhase_ -= kTwoPi;
                    if (flutterPhase_ >= kTwoPi) flutterPhase_ -= kTwoPi;
                    float mod = wow + flutter;
                    readPosL = baseReadPos + mod;
                    readPosR = baseReadPos + mod;
                    break;
                }
            }

            // Wrap read positions (both negative and positive overflow)
            while (readPosL < 0.0f) readPosL += static_cast<float> (bufSize);
            while (readPosL >= static_cast<float> (bufSize)) readPosL -= static_cast<float> (bufSize);
            while (readPosR < 0.0f) readPosR += static_cast<float> (bufSize);
            while (readPosR >= static_cast<float> (bufSize)) readPosR -= static_cast<float> (bufSize);

            float delayedL = readDelayInterp (delayBufL_, readPosL);
            float delayedR = readDelayInterp (delayBufR_, readPosR);

            // Feedback filter (one-pole LPF — darkens repeats)
            delayFbFilterStateL_ += delayFbFilterCoeff_ * (delayedL - delayFbFilterStateL_);
            delayFbFilterStateR_ += delayFbFilterCoeff_ * (delayedR - delayFbFilterStateR_);
            if (std::abs (delayFbFilterStateL_) < 1e-15f) delayFbFilterStateL_ = 0.0f;
            if (std::abs (delayFbFilterStateR_) < 1e-15f) delayFbFilterStateR_ = 0.0f;

            float fbL = delayFbFilterStateL_;
            float fbR = delayFbFilterStateR_;

            // Tape mode: soft saturate the feedback signal
            if (delayType_ == DelayType::Tape)
            {
                // Simple tanh-style soft clip for tape warmth
                fbL = std::tanh (fbL * 1.2f) * 0.83f;
                fbR = std::tanh (fbR * 1.2f) * 0.83f;
            }

            // Write to buffer
            delayBufL_[static_cast<size_t> (delayWritePos_)] = left[i] + fbL * delayFeedback_;
            delayBufR_[static_cast<size_t> (delayWritePos_)] = right[i] + fbR * delayFeedback_;

            // Mix
            left[i]  = left[i]  * (1.0f - delayMix_) + delayedL * delayMix_;
            right[i] = right[i] * (1.0f - delayMix_) + delayedR * delayMix_;

            delayWritePos_ = (delayWritePos_ + 1) % bufSize;
        }
    }

    // === DATTORRO PLATE REVERB ===
    if (reverbEnabled_ && reverbMix_ > 0.0f)
    {
        int pdBufSize = static_cast<int> (preDelayBuf_.size());

        for (int i = 0; i < numSamples; ++i)
        {
            // Sum to mono for reverb input
            float input = (left[i] + right[i]) * 0.5f;

            // Pre-delay
            float preDelayed = input;
            if (pdBufSize > 0 && preDelaySamples_ > 0)
            {
                preDelayBuf_[static_cast<size_t> (preDelayWritePos_)] = input;
                int pdRead = preDelayWritePos_ - preDelaySamples_;
                if (pdRead < 0) pdRead += pdBufSize;
                preDelayed = preDelayBuf_[static_cast<size_t> (pdRead)];
                preDelayWritePos_ = (preDelayWritePos_ + 1) % pdBufSize;
            }

            // Input diffusers (4 series allpass filters)
            float diffused = preDelayed;
            diffused = inputDiffuser_[0].process (diffused, kInputDiffCoeff1);
            diffused = inputDiffuser_[1].process (diffused, kInputDiffCoeff1);
            diffused = inputDiffuser_[2].process (diffused, kInputDiffCoeff2);
            diffused = inputDiffuser_[3].process (diffused, kInputDiffCoeff2);

            // Read from tank delay tails (for cross-feeding)
            float tankLOut = 0.0f, tankROut = 0.0f;
            {
                int rdL = tankL_.delayWritePos - tankL_.delaySamples;
                if (rdL < 0) rdL += static_cast<int> (tankL_.delay.size());
                tankLOut = tankL_.delay.empty() ? 0.0f : tankL_.delay[static_cast<size_t> (rdL)];

                int rdR = tankR_.delayWritePos - tankR_.delaySamples;
                if (rdR < 0) rdR += static_cast<int> (tankR_.delay.size());
                tankROut = tankR_.delay.empty() ? 0.0f : tankR_.delay[static_cast<size_t> (rdR)];
            }

            // Cross-feed: left tank gets diffused + decayed right, and vice versa
            float tankLIn = diffused + tankDecay_ * tankROut;
            float tankRIn = diffused + tankDecay_ * tankLOut;

            // Left tank: allpass → damping → delay
            float apOutL = tankL_.apf.process (tankLIn, kDecayDiffCoeff);
            tankL_.dampState += dampCoeff_ * (apOutL - tankL_.dampState);
            if (std::abs (tankL_.dampState) < 1e-15f) tankL_.dampState = 0.0f;
            float dampedL = tankL_.dampState * tankDecay_;
            if (! tankL_.delay.empty())
            {
                tankL_.delay[static_cast<size_t> (tankL_.delayWritePos)] = dampedL;
                tankL_.delayWritePos = (tankL_.delayWritePos + 1) % static_cast<int> (tankL_.delay.size());
            }

            // Right tank: allpass → damping → delay
            float apOutR = tankR_.apf.process (tankRIn, kDecayDiffCoeff);
            tankR_.dampState += dampCoeff_ * (apOutR - tankR_.dampState);
            if (std::abs (tankR_.dampState) < 1e-15f) tankR_.dampState = 0.0f;
            float dampedR = tankR_.dampState * tankDecay_;
            if (! tankR_.delay.empty())
            {
                tankR_.delay[static_cast<size_t> (tankR_.delayWritePos)] = dampedR;
                tankR_.delayWritePos = (tankR_.delayWritePos + 1) % static_cast<int> (tankR_.delay.size());
            }

            // Output: tap from tank delays at different points for stereo
            float wetL = apOutL * 0.6f + dampedR * 0.4f;
            float wetR = apOutR * 0.6f + dampedL * 0.4f;

            // Mix
            left[i]  = left[i]  * (1.0f - reverbMix_) + wetL * reverbMix_;
            right[i] = right[i] * (1.0f - reverbMix_) + wetR * reverbMix_;
        }
    }
}

// ============================================================================
// Delay helpers
// ============================================================================

float PostFX::readDelayInterp (const std::vector<float>& buf, float readPos) const
{
    int bufSize = static_cast<int> (buf.size());
    int i0 = static_cast<int> (std::floor (readPos));
    int i1 = i0 + 1;
    if (i0 >= bufSize) i0 -= bufSize;
    if (i1 >= bufSize) i1 -= bufSize;
    if (i0 < 0) i0 += bufSize;
    if (i1 < 0) i1 += bufSize;
    float frac = readPos - std::floor (readPos);
    return buf[static_cast<size_t> (i0)] * (1.0f - frac)
         + buf[static_cast<size_t> (i1)] * frac;
}

void PostFX::updateDelayFbFilterCoeff()
{
    float fc = 4000.0f;
    float w = kTwoPi * fc / static_cast<float> (sampleRate_);
    delayFbFilterCoeff_ = w / (w + 1.0f);
}

// ============================================================================
// Dattorro reverb initialization
// ============================================================================

int PostFX::scaleDelay (int referenceSamples) const
{
    // Dattorro reference rate is 29761 Hz. Scale to actual sample rate.
    return std::max (1, static_cast<int> (
        static_cast<double> (referenceSamples) * sampleRate_ / 29761.0));
}

void PostFX::initDattorroDelays()
{
    // Input diffuser delay lengths (from Dattorro's paper, Table 1)
    int idLens[4] = {
        scaleDelay (142),
        scaleDelay (107),
        scaleDelay (379),
        scaleDelay (277)
    };

    for (int i = 0; i < 4; ++i)
    {
        inputDiffuser_[i].allocate (idLens[i] + 1);
        inputDiffuser_[i].delaySamples = idLens[i];
    }

    // Tank allpass delay lengths
    int apfDelayL = scaleDelay (672);
    int apfDelayR = scaleDelay (908);
    tankL_.apf.allocate (apfDelayL + 1);
    tankL_.apf.delaySamples = apfDelayL;
    tankR_.apf.allocate (apfDelayR + 1);
    tankR_.apf.delaySamples = apfDelayR;

    // Tank main delay lengths
    int mainDelayL = scaleDelay (4453);
    int mainDelayR = scaleDelay (4217);
    tankL_.delay.assign (static_cast<size_t> (mainDelayL + 1), 0.0f);
    tankL_.delaySamples = mainDelayL;
    tankL_.delayWritePos = 0;
    tankR_.delay.assign (static_cast<size_t> (mainDelayR + 1), 0.0f);
    tankR_.delaySamples = mainDelayR;
    tankR_.delayWritePos = 0;
}

void PostFX::updateDattorroParams()
{
    // Map decay 0-1 to feedback coefficient 0.1-0.95
    // Size scales the decay range
    float sizeScale = 0.7f + reverbSize_ * 0.3f; // 0.7 to 1.0
    tankDecay_ = 0.1f + reverbDecay_ * 0.85f * sizeScale;
    tankDecay_ = std::clamp (tankDecay_, 0.0f, 0.95f);

    // Damping one-pole LPF: state += coeff * (input - state).
    // dampCoeff_ = 0.9 - reverbDamping_ * 0.8, so higher reverbDamping_
    // produces a lower dampCoeff_ (range 0.1–0.9).
    // Higher coeff = faster tracking = brighter; lower coeff = darker.
    dampCoeff_ = 0.9f - reverbDamping_ * 0.8f;
}
