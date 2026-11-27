// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
#pragma once

#include "../../src/VocalPlateCalibration.h"
#include "TwoPhaseLowpass.hpp"
#include "../../../shared-daf/dsp/DuskFilters.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace duskverb
{
// A stereo plate with three input allpasses per channel and an orthogonal eight-line
// feedback network. The four decay bands share the delays. Delay changes crossfade
// between read heads so automation never resizes or clears a sounding line.
class CompactPlate
{
public:
    struct Controls
    {
        float predelay, decayRatio, size, diffusion, erLevel, erSize;
        float modDepth, modRate, bass, mid, treble, crossover, highCrossover;
        bool freeze;
    };

    void prepare (double rate)
    {
        rate_ = static_cast<float> (rate);
        for (auto& line : input_) line.prepare (samples (400.0f));
        for (auto& channel : diffuser_)
            for (auto& line : channel) line.prepare (samples (32.0f));
        const auto tankSize = Delay::capacity (samples (144.0f) + 16);
        tank_.assign (tankSize, {});
        tankMask_ = tankSize - 1;
        highpass_ = pole (36.0631f);
        constexpr float outputCutoffHz = 10785.944f;
        lowpass_ = pole (outputCutoffHz);
        outputFeedthrough_ = tankOutputFeedthrough (outputCutoffHz);
        // A broad-band delay would move the whole attack. Reverse only the numerator
        // of a narrow peaking section to spread the kick resonance over its decay.
        const double omega = 3.141592653589793 * 73.0 / rate;
        const double alpha = std::sin (omega) / 16.0; // Q = 8, evaluated at 2x
        const double gain = std::pow (10.0, -11.0 / 40.0);
        const double norm = 1.0 / (1.0 + alpha / gain);
        const double middle = -2.0 * std::cos (omega) * norm;
        for (auto& filter : bassReturn_)
            filter.setCoeffs ({(1.0 - alpha * gain) * norm, middle, (1.0 + alpha * gain) * norm,
                               middle, (1.0 - alpha / gain) * norm});
        fadeSamples_ = std::max (1, samples (15.0f));
        clear();
    }

    void clear() noexcept
    {
        for (auto& line : input_) line.clear();
        for (auto& channel : diffuser_)
            for (auto& line : channel) line.clear();
        std::fill (tank_.begin(), tank_.end(), std::array<float, 8> {});
        clock_ = 0;
        fadeRemaining_ = 0;
        smoothingRemaining_ = 0;
        current_ = target_ = {};
        configured_ = false;
        damp_ = {};
        lp_ = hp_ = {};
        for (auto& filter : bassReturn_) filter.reset();
        for (int j = 0; j < 8; ++j) phase_[j] = 0.1f * j;
        speed_.fill (0.0f);
        inputGain_ = 1.0f;
    }

    void set (const Controls& c) noexcept
    {
        if (configured_ && c.predelay == last_.predelay && c.decayRatio == last_.decayRatio && c.size == last_.size
            && c.diffusion == last_.diffusion && c.erLevel == last_.erLevel && c.erSize == last_.erSize
            && c.modDepth == last_.modDepth && c.modRate == last_.modRate && c.bass == last_.bass
            && c.mid == last_.mid && c.treble == last_.treble && c.crossover == last_.crossover
            && c.highCrossover == last_.highCrossover && c.freeze == last_.freeze)
            return;
        const float scale = 0.94712f * (0.805f + 1.3f * c.size);
        const float decay = c.decayRatio;
        const float times[] { 0.78f * decay * c.bass, 0.86f * decay * c.mid,
                              0.74f * decay, 0.58f * decay * c.treble };
        const float lengths[] { 29.3f, 33.1f, 38.3f, 42.7f, 47.1f, 51.7f, 57.1f, 63.1f };
        const float reflections[2][4] { {2.4f, 9.4f, 16.9f, 25.1f}, {3.7f, 11.8f, 19.2f, 30.1f} };
        const float diffusion[2][3] { {2.73f, 4.87f, 8.29f}, {3.13f, 5.79f, 9.43f} };
        requested_.pre = samples (c.predelay);
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int i = 0; i < 4; ++i)
                requested_.early[ch][i] = samples (c.predelay + scale * (0.5f + c.erSize) * reflections[ch][i]);
            for (int i = 0; i < 3; ++i)
                requested_.ap[ch][i] = std::max (1, samples (diffusion[ch][i] * (0.25f + 2.40434f * c.diffusion)));
        }
        Settings next;
        for (int j = 0; j < 8; ++j)
        {
            requested_.tank[j] = std::max (2, samples (lengths[j] * scale));
            speed_[j] = std::copysign ((0.3f + 0.07f * j) * (c.modRate / vocalplate::modRate) / rate_, speed_[j]);
            float g[4];
            for (int b = 0; b < 4; ++b)
                g[b] = c.freeze ? 1.0f : std::pow (0.001f, requested_.tank[j] / (rate_ * std::max (0.02f, times[b])));
            for (int b = 0; b < 3; ++b) next.gains[b][j] = g[b] - g[b + 1];
            next.gains[3][j] = g[3];
        }
        // A band whose edges meet has zero width. Inverting its poles instead
        // would turn loss into positive feedback at legal crossover settings.
        const float low = std::min (c.crossover, c.highCrossover);
        const float corners[] {low, std::clamp (1350.0f, low, c.highCrossover), c.highCrossover};
        for (int b = 0; b < 3; ++b)
        {
            const float first = pole (corners[b]);
            // With held input both 2x phases are known from the initial error.
            // Cache their endpoint and mean for all eight feedback lines.
            next.endPole[b] = first * (2.0f - first);
            next.meanPole[b] = 0.5f * (first + next.endPole[b]);
        }
        next.earlyGain = 1.21983f * c.erLevel;
        next.depth = c.modDepth * (1.26597f / vocalplate::modDepth) * (rate_ / 48000.0f);
        if (!configured_)
        {
            geometry_ = previous_ = requested_;
            inputGain_ = c.freeze ? 0.0f : 1.0f;
            current_ = next;
        }
        else if (target_.gains != next.gains || target_.endPole != next.endPole || target_.meanPole != next.meanPole
                 || target_.earlyGain != next.earlyGain || target_.depth != next.depth)
            smoothingRemaining_ = fadeSamples_;
        target_ = next;
        configured_ = true;
        last_ = c;
    }

    void process (float* left, float* right, int count) noexcept
    {
        for (int offset = 0; offset < count;)
        {
            if (fadeRemaining_ == 0 && !(requested_ == geometry_))
            {
                previous_ = geometry_;
                geometry_ = requested_;
                fadeRemaining_ = fadeSamples_;
            }
            if (fadeRemaining_ != 0)
            {
                const int span = std::min (fadeRemaining_, count - offset);
                render<true> (left + offset, right + offset, span);
                offset += span;
            }
            else
            {
                render<false> (left + offset, right + offset, count - offset);
                break;
            }
        }
    }

private:
    struct Settings
    {
        std::array<std::array<float, 8>, 4> gains {};
        std::array<float, 3> endPole {}, meanPole {};
        float earlyGain = 0, depth = 0;
    };

    void advanceSettings() noexcept
    {
        if (--smoothingRemaining_ == 0)
        {
            current_ = target_;
            return;
        }
        const float step = 1.0f / (smoothingRemaining_ + 1);
        const auto advance = [step] (float& value, float target) { value += step * (target - value); };
        advance (current_.earlyGain, target_.earlyGain);
        advance (current_.depth, target_.depth);
        for (int b = 0; b < 3; ++b)
        {
            advance (current_.endPole[b], target_.endPole[b]);
            advance (current_.meanPole[b], target_.meanPole[b]);
        }
        for (int b = 0; b < 4; ++b)
            for (int j = 0; j < 8; ++j) advance (current_.gains[b][j], target_.gains[b][j]);
    }

    struct Delay
    {
        std::vector<float> data;
        std::uint32_t mask = 0;
        static std::uint32_t capacity (int maximum) noexcept
        {
            std::uint32_t size = 1;
            while (size <= static_cast<unsigned> (maximum)) size <<= 1;
            return size;
        }
        void prepare (int maximum)
        {
            const auto size = capacity (maximum);
            data.assign (size, 0.0f);
            mask = size - 1;
        }
        void clear() noexcept { std::fill (data.begin(), data.end(), 0.0f); }
        float read (std::uint32_t pos, int delay) const noexcept { return data[(pos - delay) & mask]; }
        void write (std::uint32_t pos, float x) noexcept { data[pos & mask] = x; }
    };

    struct Geometry
    {
        int pre = 0;
        std::array<std::array<int, 4>, 2> early {};
        std::array<std::array<int, 3>, 2> ap {};
        std::array<int, 8> tank {};
        bool operator== (const Geometry& g) const noexcept
        { return pre == g.pre && early == g.early && ap == g.ap && tank == g.tank; }
    };

    int samples (float ms) const noexcept { return static_cast<int> (std::lround (ms * rate_ * 0.001f)); }
    float pole (float hz) const noexcept
    { return tankPole2x (std::min (hz, 0.45f * rate_), rate_); }

    template<bool Fade>
    float tap (const Delay& line, int now, int old, float blend) const noexcept
    {
        const float current = line.read (clock_, now);
        if constexpr (Fade)
        {
            const float previous = line.read (clock_, old);
            return previous + blend * (current - previous);
        }
        return current;
    }

    template<bool Fade>
    void render (float* left, float* right, int count) noexcept
    {
        for (int i = 0; i < count; ++i, ++clock_)
        {
            if (smoothingRemaining_ > 0) advanceSettings();
            const float blend = Fade ? 1.0f - static_cast<float> (--fadeRemaining_) / fadeSamples_ : 1.0f;
            const float step = 1.0f / fadeSamples_;
            inputGain_ = last_.freeze ? std::max (0.0f, inputGain_ - step) : std::min (1.0f, inputGain_ + step);
            float inject[2], early[2];
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& input = input_[ch];
                input.write (clock_, (ch == 0 ? left[i] : right[i]) * inputGain_);
                float v = tap<Fade> (input, geometry_.pre, previous_.pre, blend);
                for (int a = 0; a < 3; ++a)
                {
                    auto& ap = diffuser_[ch][a];
                    const float y = tap<Fade> (ap, geometry_.ap[ch][a], previous_.ap[ch][a], blend) - 0.6f * v;
                    ap.write (clock_, v + 0.6f * y);
                    v = y;
                }
                inject[ch] = v;
                early[ch] = 0.30f * v;
                constexpr float levels[4] {0.33f, -0.24f, 0.20f, -0.17f};
                for (int j = 0; j < 4; ++j)
                    early[ch] += levels[j] * tap<Fade> (input, geometry_.early[ch][j], previous_.early[ch][j], blend);
            }
            std::array<float, 8> feedback, reads;
            if (!last_.freeze)
                for (int j = 0; j < 8; ++j)
                {
                    phase_[j] += speed_[j];
                    if (phase_[j] > 1.0f || phase_[j] < 0.0f) speed_[j] = -speed_[j];
                }
            for (int j = 0; j < 8; ++j)
            {
                const float m = current_.depth * (1.0f + phase_[j]);
                const int k = static_cast<int> (m);
                const auto pos = clock_ - geometry_.tank[j] - k;
                float u = tank_[pos & tankMask_][j];
                float v = tank_[(pos - 1) & tankMask_][j];
                if constexpr (Fade)
                {
                    const auto old = clock_ - previous_.tank[j] - k;
                    const float oldU = tank_[old & tankMask_][j];
                    const float oldV = tank_[(old - 1) & tankMask_][j];
                    u = oldU + blend * (u - oldU);
                    v = oldV + blend * (v - oldV);
                }
                reads[j] = u + inputGain_ * (m - k) * (v - u);
                feedback[j] = reads[j] * current_.gains[3][j];
            }
            for (int b = 0; b < 3; ++b)
                for (int j = 0; j < 8; ++j)
                {
                    const float delta = reads[j] - damp_[b][j];
                    const float band = damp_[b][j] + current_.meanPole[b] * delta;
                    damp_[b][j] += current_.endPole[b] * delta;
                    feedback[j] += current_.gains[b][j] * band;
                }
            for (int span = 1; span < 8; span *= 2)
                for (int group = 0; group < 8; group += 2 * span)
                    for (int k = group; k < group + span; ++k)
                    {
                        const float a = feedback[k], b = feedback[k + span];
                        feedback[k] = a + b;
                        feedback[k + span] = a - b;
                    }
            auto& write = tank_[clock_ & tankMask_];
            for (int j = 0; j < 8; ++j)
                write[j] = 0.35355339f * feedback[j] + (j % 4 < 2 ? 0.32f : -0.32f) * inject[j & 1];

            left[i] = current_.earlyGain * early[0] + 0.27036f * (reads[0]+reads[2]-reads[3]+reads[7]);
            right[i] = current_.earlyGain * early[1] + 0.27036f * (reads[1]-reads[4]+reads[5]+reads[6]);
        }
        // The return is outside the feedback loop. Process each channel contiguously
        // so both oversampled phases can keep their filter histories in registers.
        for (int ch = 0; ch < 2; ++ch)
        {
            float* output = ch == 0 ? left : right;
            float hp = hp_[ch], lp = lp_[ch];
            auto bass = bassReturn_[ch];
            for (int i = 0; i < count; ++i)
            {
                const float wet = output[i];
                const auto phase = [&] {
                    const float high = wet - lowpassPhase (wet, highpass_, hp);
                    const float low = lowpassPhase (high, lowpass_, lp);
                    return bass.process (low + outputFeedthrough_ * (high - low));
                };
                const float first = phase();
                output[i] = 0.5f * (first + phase());
            }
            hp_[ch] = hp;
            lp_[ch] = lp;
            bassReturn_[ch] = bass;
        }
    }

    Delay input_[2], diffuser_[2][3];
    // All feedback lines share a write clock. One row writes their eight new samples together.
    std::vector<std::array<float, 8>> tank_;
    std::uint32_t tankMask_ = 0;
    std::array<std::array<float, 8>, 3> damp_ {};
    Settings current_, target_;
    std::array<float, 8> phase_ {}, speed_ {};
    std::array<float, 2> hp_ {}, lp_ {};
    duskaudio::DoubleBiquad bassReturn_[2];
    Geometry requested_, geometry_, previous_;
    Controls last_ {};
    std::uint32_t clock_ = 0;
    float rate_ = 48000.0f, lowpass_ = 0.0f, highpass_ = 0.0f, outputFeedthrough_ = 0.0f, inputGain_ = 1.0f;
    int fadeSamples_ = 1, fadeRemaining_ = 0, smoothingRemaining_ = 0;
    bool configured_ = false;
};
} // namespace duskverb
