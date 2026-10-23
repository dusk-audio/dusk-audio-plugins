// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>

namespace multicompp
{
// A gain-reduction needle's movement: second order, 1.25% overshoot, reaching 99%
// of a step in a set time. Measured against the references' GR meters by screen-
// recording each beside its MC-2 face (2026-10-22):
//  - The leveller's and the limiter's meters are VU movements (IEC 60268-17, 99%
//    in 300 ms; the standard allows 1-1.5% overshoot). On a drum loop the
//    leveller's needle moved at most 1.2 scale units per 33 ms frame, while one
//    drawn straight from the cell's gain swung 5-6 units in a frame on every kick;
//    the limiter's needle fitted a 300 ms movement fed our gain reduction with no
//    added delay (rms 0.345 dB, best 0.337 at 250 ms).
//  - The VCA reference's meter is quicker: with both units on one audio clock
//    and identical settings, its needle fitted a movement reaching 99% in
//    120-150 ms against its own measured gain reduction (rms 1.00-1.04 dB, 1.12 at
//    80 ms), so 140 ms. It also reads a static law, not the gain reduction: 1:1 to
//    3.9 dB, then 0.817 dB of dial per dB (rms 0.23 dB over five threshold and
//    compression settings on a 1 kHz staircase; the law puts a true -27.2 dB at
//    -22.9 on the dial, the reference read -22.7).
// Display only: it reads the DSP's published gain reduction and never touches the
// audio or the host's GR output.
//
// A VU movement is linear in voltage, so it integrates the linear gain (the
// vintage dial's deflection is 10^(dB/20), see vintageVuDeflection). The VCA
// reference's dial is linear in decibels, so its movement integrates decibels.
// The DSP publishes gain reduction once per block, so the input is held over each
// block and the movement advanced by its exact solution for the block's duration:
// stable for any block length and the same at every sample rate for a steady input.
// The held value is the block's last, so a transient reads up to a block early and,
// at large host buffers, slightly deep (a 6 dB kick: about 0.1 dB at 512 samples,
// 0.5 dB at 2048; parity-playbook open item). A VU movement cannot swing below zero
// volts: that is its stop.
struct NeedleBallistics
{
    double damping;            // 1.25% overshoot
    double naturalFrequency;   // rad/s
    bool linearInVoltage;      // false: the dial and the movement are linear in dB
    double lawKneeDb = 0.0;    // the dial reads the gain reduction down to the knee...
    double lawSlope = 1.0;     // ...and this many dB of dial per dB beyond it
};
inline constexpr NeedleBallistics kVuNeedle{0.8127169864, 13.5119126, true};    // 99% at 300 ms, 1:1
inline constexpr NeedleBallistics kVcaNeedle{0.8127169864, 13.5119126 * 300.0 / 140.0, false,
                                             -3.9, 0.817};                        // 99% at 140 ms, measured law

class GainReductionNeedle
{
public:
    explicit GainReductionNeedle(const NeedleBallistics& b) noexcept : ballistics(b) { reset(); }

    void prepare(double sampleRate) noexcept
    {
        rate = std::isfinite(sampleRate) && sampleRate > 0 ? sampleRate : 48000;
        reset();
    }
    void reset() noexcept
    {
        position = rest();
        velocity = 0.0;
        stepSamples = 0;
        published.store(0.0f, std::memory_order_relaxed);
    }
    void process(float gainReductionDb, int samples) noexcept
    {
        if (samples <= 0) return;
        if (samples != stepSamples) prepareStep(samples);
        const double target = targetFor(gainReductionDb);
        const double error = position - target;
        position = target + a11 * error + a12 * velocity;
        velocity = a21 * error + a22 * velocity;
        if (ballistics.linearInVoltage && position < 0.0)
        {
            position = 0.0;   // the stop
            velocity = 0.0;
        }
        if (!std::isfinite(position) || !std::isfinite(velocity))
        {
            position = rest();
            velocity = 0.0;
        }
        publish();
    }
    // Another meter comes into view (a mode switch): it already shows its reading.
    void snap(float gainReductionDb) noexcept
    {
        position = targetFor(gainReductionDb);
        velocity = 0.0;
        publish();
    }
    // The needle's reading on the dial in decibels, never below -60 (0 at rest;
    // briefly above 0 while the movement overshoots on its way back, which the
    // vintage dials draw at 0). With a meter law this is the dial reading, not the
    // gain reduction.
    float levelDb() const noexcept { return published.load(std::memory_order_relaxed); }

private:
    double rest() const noexcept { return ballistics.linearInVoltage ? 1.0 : 0.0; }
    double targetFor(float gainReductionDb) const noexcept
    {
        double db = std::isfinite(gainReductionDb) ? static_cast<double>(gainReductionDb) : 0.0;
        if (db < ballistics.lawKneeDb)
            db = ballistics.lawKneeDb + ballistics.lawSlope * (db - ballistics.lawKneeDb);
        return ballistics.linearInVoltage ? std::pow(10.0, 0.05 * db) : db;
    }
    void publish() noexcept
    {
        const double db = ballistics.linearInVoltage ? 20 * std::log10(std::max(1.0e-3, position)) : position;
        published.store(static_cast<float>(std::max(-60.0, db)), std::memory_order_relaxed);
    }
    void prepareStep(int samples) noexcept
    {
        const double dt = samples / rate;
        const double w = ballistics.naturalFrequency;
        const double decay = ballistics.damping * w;
        const double frequency = w * std::sqrt(1.0 - ballistics.damping * ballistics.damping);
        const double e = std::exp(-decay * dt);
        const double s = std::sin(frequency * dt) / frequency;
        const double c = std::cos(frequency * dt);
        a11 = e * (c + decay * s); a12 = e * s;
        a21 = -e * w * w * s; a22 = e * (c - decay * s);
        stepSamples = samples;
    }

    NeedleBallistics ballistics;
    double rate = 48000;
    double position = 1.0, velocity = 0.0;
    double a11 = 1.0, a12 = 0.0, a21 = 0.0, a22 = 1.0;
    int stepSamples = 0;
    std::atomic<float> published{0.0f};
};
}
