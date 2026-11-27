// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).

#pragma once

#include <cstdint>

namespace duskverb::sectionreverb
{

// The two allpass sections share the feedback ring with the output taps.
// Their coefficients follow opposite lanes of the second four-phase sequencer.
class NestedSection
{
  public:
    // "Unchanged", not "None": X11 defines None as a macro, and the editor test includes both.
    enum class MorphAction
    {
        Unchanged,
        Step,
        FadeOut,
        Swap,
        Finish
    };

    void reset(unsigned id, float delayCoefficient, float sizeScale, unsigned write, unsigned length)
    {
        shortGroup_ = id;
        coefficient_ = delayCoefficient;
        pending_ = false;
        nextSize_ = sizeScale;
        random_ = shortEnabled_ = longEnabled_ = true;
        state_ = 0;
        setSizeImmediate(sizeScale, write, length);
    }

    void setSizeImmediate(float sizeScale, unsigned write, unsigned length)
    {
        setTargets(sizeScale);
        shortNow_ = shortTarget_;
        longNow_ = longTarget_;
        shortHead_ = wrap(int(float(write) - shortNow_), length);
        longHead_ = wrap(int(float(write) - longNow_), length);
        size_ = sizeScale;
    }

    MorphAction requestSize(float sizeScale)
    {
        nextSize_ = sizeScale;
        if (state_ != 0)
        {
            pending_ = true;
            return MorphAction::Unchanged;
        }
        pending_ = false;
        setTargets(sizeScale);
        const float previous = size_;
        size_ = sizeScale;
        if (size_ > previous)
        {
            state_ = 1;
            random_ = shortEnabled_ = false;
            return MorphAction::FadeOut;
        }
        if (size_ < previous)
        {
            // The short head must retarget in state 9 before the long head can move.
            state_ = 9;
            random_ = longEnabled_ = false;
        }
        return MorphAction::Unchanged;
    }

    // Called after the morph read and the 32-sample write advance.
    MorphAction advanceSize(bool morphRamping)
    {
        switch (state_)
        {
        case 0:
            if (pending_) return requestSize(nextSize_);
            break;
        case 1:
        case 3:
        case 14:
        case 16:
            if (morphRamping)
                return MorphAction::Step;
            ++state_;
            break;
        case 2:
        case 15:
            ++state_;
            return MorphAction::Swap;
        case 5:
            shortEnabled_ = true;
            ++state_;
            break;
        case 6:
        case 11:
            ++state_;
            break;
        case 8:
            state_ = 17;
            break;
        case 10:
            longEnabled_ = true;
            ++state_;
            break;
        case 13:
            ++state_;
            return MorphAction::FadeOut;
        case 17:
            state_ = 0;
            random_ = true;
            return MorphAction::Finish;
        default:
            break; // waiting for a matching sequencer group
        }
        return MorphAction::Unchanged;
    }

    unsigned shortGroup() const { return shortGroup_; }
    unsigned longGroup() const { return (shortGroup_ + 2u) & 3u; }
    float sizeScale() const { return size_; }

    template <class Random>
    void retarget(unsigned group, float wanderSamples, unsigned write, unsigned length,
                  unsigned maxDelaySamples, Random &&random)
    {
        if (group == shortGroup() && shortEnabled_)
        {
            shortNow_ = shortTarget_;
            if (random_)
            {
                shortNow_ += float(int(random() * wanderSamples));
                if (shortNow_ > float(int(longNow_ - 4.0f)))
                    shortNow_ = longNow_ * 0.618034f;
            }
            shortHead_ = wrap(int(write) - int(shortNow_), length);
            if (state_ == 7 || state_ == 9)
                ++state_;
        }
        else if (group == longGroup() && longEnabled_)
        {
            longNow_ = longTarget_;
            if (random_)
            {
                longNow_ += float(int(random() * wanderSamples));
                const float lower = shortNow_ + 4.0f;
                if (longNow_ < lower)
                    longNow_ = lower;
                else if (longNow_ >= float(maxDelaySamples))
                    longNow_ = (float(maxDelaySamples) - lower) * 0.618034f + lower;
            }
            longHead_ = wrap(int(write) - int(longNow_), length);
            if (state_ == 4 || state_ == 12)
                ++state_;
        }
    }

    void apply(float *ring, unsigned length, unsigned writeSample, float shortLane, float longLane,
               float gain)
    {
        const float shortGain = gain * shortLane;
        const float longGain = gain * longLane;
        const float shortSample = ring[shortHead_];
        const float longSample = ring[longHead_];
        const float inner = longSample * longGain + ring[writeSample];
        const float output = shortSample * shortGain + inner;
        store(ring, length, writeSample, output);
        store(ring, length, shortHead_, shortSample - output * shortGain);
        store(ring, length, longHead_, longSample - inner * longGain);
        if (++shortHead_ == length)
            shortHead_ = 0;
        if (++longHead_ == length)
            longHead_ = 0;
    }

  private:
    static unsigned wrap(int index, unsigned length)
    {
        return unsigned(index < 0 ? index + int(length) : index);
    }
    static void store(float *ring, unsigned length, unsigned index, float value)
    {
        ring[index] = value;
        if (index < 32)
            ring[length + index] = value;
    }
    void setTargets(float size)
    {
        longTarget_ = float(int(size * coefficient_));
        shortTarget_ = float(double(longTarget_) * 0.3819659948348999);
    }

    unsigned shortGroup_ = 0, shortHead_ = 0, longHead_ = 0;
    unsigned state_ = 0;
    float coefficient_ = 0, size_ = 0, nextSize_ = 0;
    float shortTarget_ = 0, longTarget_ = 0, shortNow_ = 0, longNow_ = 0;
    bool pending_ = false, random_ = true, shortEnabled_ = true, longEnabled_ = true;
};

} // namespace duskverb::sectionreverb
