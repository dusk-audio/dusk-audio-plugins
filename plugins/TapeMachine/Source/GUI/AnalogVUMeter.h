#pragma once

#include <JuceHeader.h>

//==============================================================================
// Professional Analog VU Meter Component
// Supports both mono (single meter) and stereo (dual meter) display modes
// Inspired by Studer A800 and Ampex ATR-102 VU meters
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

    // VU Ballistics - ANSI/IEC standard
    // 300ms integration time means time constant τ ≈ 65ms (300ms / 4.6 for 99%)
    // At 60Hz refresh: coefficient = 1 - exp(-1 / (60 * 0.065)) ≈ 0.23
    static constexpr float kVUTimeConstantMs = 65.0f;   // For 300ms to 99%
    static constexpr float kRefreshRateHz = 60.0f;

    // Mechanical overshoot simulation (damped spring model)
    // Real VU meters overshoot by ~1.5-2% due to needle inertia
    static constexpr float kOvershootDamping = 0.78f;   // Damping ratio (0.78 ≈ 1.5-2% overshoot)
    static constexpr float kOvershootStiffness = 180.0f; // Spring constant (higher = faster response)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalogVUMeter)
};