#pragma once

#include <JuceHeader.h>

//==============================================================================
// Professional Analog VU Meter Component
// Supports both mono (single meter) and stereo (dual meter) display modes
// Inspired by Studer A800 and Ampex ATR-102 VU meters
//
// STANDARDS COMPLIANCE (IEC 60268-17 / ANSI C16.5):
// - 300ms integration time (99% of steady-state reading)
// - Rise time: 300ms ±10% for 99% of final value
// - Overshoot: 1-1.5% (per mechanical meter specs)
// - Scale: -20 VU to +3 VU (0 VU = +4 dBu reference level)
// - Logarithmic response with RMS-equivalent ballistics
//
// The VU (Volume Unit) standard was developed in 1939 and defines:
// - Time constant τ ≈ 65ms (to reach 99% in ~300ms = 4.6τ)
// - Mechanical needle inertia causing characteristic overshoot
// - Symmetrical attack and release times (unlike PPM meters)
//
class AnalogVUMeter : public juce::Component, private juce::Timer
{
public:
    AnalogVUMeter();
    ~AnalogVUMeter() override;

    // Set levels for L/R (for stereo, call with left and right; for mono, both values are used)
    void setLevels(float leftLevel, float rightLevel);

    // Set stereo mode - when true, shows two VU meters; when false, shows single VU meter
    void setStereoMode(bool isStereo);
    bool isStereoMode() const { return stereoMode; }

    void paint(juce::Graphics& g) override;

private:
    void timerCallback() override;

    // Helper to paint a single VU meter in the given bounds
    void paintSingleMeter(juce::Graphics& g, const juce::Rectangle<float>& bounds,
                          float needlePos, float peakLevel, const juce::String& label);

    // Stereo/mono mode
    bool stereoMode = true;

    // Target levels (dB) set by setLevels()
    float targetLevelL = -60.0f;
    float targetLevelR = -60.0f;

    // Needle physics state (normalized 0-1 position)
    float needlePositionL = 0.0f;   // Current position
    float needlePositionR = 0.0f;
    float needleVelocityL = 0.0f;   // For overshoot physics
    float needleVelocityR = 0.0f;

    // Peak hold
    float peakLevelL = -60.0f;
    float peakLevelR = -60.0f;
    float peakHoldTimeL = 0.0f;
    float peakHoldTimeR = 0.0f;

    // =========================================================================
    // VU BALLISTICS CONSTANTS - IEC 60268-17 / ANSI C16.5 Compliant
    // =========================================================================
    //
    // TIME CONSTANT (τ = 65ms):
    // The VU standard specifies 300ms to reach 99% of steady-state value.
    // For exponential decay: 99% = 1 - e^(-t/τ), solving gives t ≈ 4.6τ
    // Therefore: τ = 300ms / 4.6 ≈ 65ms
    //
    // At 60Hz refresh rate:
    // Coefficient = 1 - exp(-1 / (60Hz * 0.065s)) ≈ 0.23
    static constexpr float kVUTimeConstantMs = 65.0f;   // IEC 60268-17 standard
    static constexpr float kRefreshRateHz = 60.0f;      // UI refresh rate

    // =========================================================================
    // MECHANICAL OVERSHOOT SIMULATION (Damped Spring Model)
    // =========================================================================
    //
    // Real VU meters use a d'Arsonval galvanometer movement with mechanical
    // needle inertia. This creates characteristic overshoot of 1-1.5%.
    //
    // DAMPING RATIO (ζ = 0.78):
    // - ζ < 1: Underdamped (overshoot occurs)
    // - ζ = 1: Critically damped (no overshoot, fastest settling)
    // - ζ > 1: Overdamped (slow, no overshoot)
    // Real VU meters have ζ ≈ 0.75-0.85, producing 1-2% overshoot.
    // We use 0.78 for authentic ~1.5% overshoot behavior.
    //
    // SPRING STIFFNESS (ω² = 180):
    // Natural frequency ωn = √(k/m) where k=stiffness, m=mass
    // Higher values = faster response. 180 gives proper 300ms rise time.
    //
    static constexpr float kOvershootDamping = 0.78f;    // ζ: Damping ratio for 1.5% overshoot
    static constexpr float kOvershootStiffness = 180.0f; // ω²: Spring constant for 300ms rise

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalogVUMeter)
};