// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).

#pragma once

#include <cstdint>

namespace duskverb::sectionreverb
{
struct Rate
{
    float sr = 48000.0f, invSr = 1.0f / 48000.0f, srMs = 48.0f, srScale = 48000.0f / 44100.0f;
    float lenScale = 48000.0f / 44100.0f, sr32 = 1500.0f, nyquist = 24000.0f;
    static Rate make (double sampleRate) noexcept;
};

// A value stepped once per 32-sample block toward its target.
class Ramp
{
public:
    void reset (float value) noexcept;
    void setTarget (float target, float msPerUnit, const Rate& rate) noexcept;
    // A ramp to a new target, a snap when the value is already there.
    void arm (float target, float msPerUnit, const Rate& rate) noexcept;
    void advanceBlock() noexcept;
    float current() const noexcept { return current_; }
    float target() const noexcept { return target_; }
    bool ramping() const noexcept { return blocksLeft_ != 0; }

private:
    float target_ = 0.0f, current_ = 0.0f, step_ = 0.0f;
    std::uint32_t blocksLeft_ = 0;
};

} // namespace duskverb::sectionreverb
