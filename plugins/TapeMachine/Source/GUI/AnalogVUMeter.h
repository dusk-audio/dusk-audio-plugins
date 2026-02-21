#pragma once

#include <JuceHeader.h>

//==============================================================================
// Premium Analog VU Meter Component
// Photorealistic vintage styling inspired by classic professional tape machines
//
// STANDARDS COMPLIANCE (IEC 60268-17 / ANSI C16.5):
// - 300ms integration time (99% of steady-state reading)
// - Rise time: 300ms +/-10% for 99% of final value
// - Overshoot: 1-1.5% (per mechanical meter specs)
// - Scale: -20 VU to +3 VU (0 VU = +4 dBu reference level)
// - Logarithmic response with RMS-equivalent ballistics
//
// PREMIUM VISUAL FEATURES:
// - Photorealistic aged cream faceplate with subtle texture
// - Accurate scale markings with red zone highlighting
// - Realistic needle with shadow and proper pivot
// - Glass reflection overlay with gradient
// - Optional decorative screws on faceplate
// - Subtle backlighting effect
//
class AnalogVUMeter : public juce::Component, private juce::Timer
{
public:
    AnalogVUMeter();
    ~AnalogVUMeter() override;

    // Set levels for L/R (linear 0-1+ range)
    void setLevels(float leftLevel, float rightLevel);

    // Set stereo mode - when true, shows two VU meters; when false, shows single VU meter
    void setStereoMode(bool isStereo);
    bool isStereoMode() const { return stereoMode; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    // Premium rendering methods
    void paintSingleMeter(juce::Graphics& g, const juce::Rectangle<float>& bounds,
                          float needlePos, float peakLevel, const juce::String& label);

    void drawMeterFrame(juce::Graphics& g, const juce::Rectangle<float>& bounds, float scale);
    void drawMeterFace(juce::Graphics& g, const juce::Rectangle<float>& bounds, float scale);
    void drawScaleMarkings(juce::Graphics& g, float centreX, float pivotY,
                            float needleLength, float scale);
    void drawNeedleWithShadow(juce::Graphics& g, float centreX, float pivotY,
                               float needleLength, float needleAngle, float scale);
    void drawGlassReflection(juce::Graphics& g, const juce::Rectangle<float>& bounds, float scale);
    void drawDecoScrews(juce::Graphics& g, const juce::Rectangle<float>& bounds, float scale);

    // Stereo/mono mode
    bool stereoMode = true;

    // Target levels (dB) set by setLevels()
    float targetLevelL = -60.0f;
    float targetLevelR = -60.0f;

    // Needle physics state (normalized 0-1 position)
    float needlePositionL = 0.0f;
    float needlePositionR = 0.0f;
    float needleVelocityL = 0.0f;
    float needleVelocityR = 0.0f;

    // Peak hold
    float peakLevelL = -60.0f;
    float peakLevelR = -60.0f;
    float peakHoldTimeL = 0.0f;
    float peakHoldTimeR = 0.0f;

    // Cached rendering
    juce::Image faceplateCache;
    int cachedWidth = 0;
    int cachedHeight = 0;
    bool cacheNeedsUpdate = true;

    // =========================================================================
    // VU BALLISTICS CONSTANTS - IEC 60268-17 / ANSI C16.5 Compliant
    // =========================================================================
    // RC time constant for ~300ms rise time (IEC 60268-17 compliant)
    // The 65ms RC constant yields 99% of final value in ~300ms (5 time constants)
    static constexpr float kVUTimeConstantMs = 65.0f;
    static constexpr float kRefreshRateHz = 60.0f;      // UI refresh rate

    // MECHANICAL OVERSHOOT SIMULATION (Damped Spring Model)
    static constexpr float kOvershootDamping = 0.78f;    // Damping ratio for 1.5% overshoot
    static constexpr float kOvershootStiffness = 180.0f; // Spring constant for 300ms rise

    // VU scale angle range
    static constexpr float kScaleStartAngle = -2.7f;     // -20 VU position (radians)
    static constexpr float kScaleEndAngle = -0.44f;      // +3 VU position (radians)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalogVUMeter)
};
