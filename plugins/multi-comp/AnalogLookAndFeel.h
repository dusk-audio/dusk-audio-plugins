#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../shared/LunaLookAndFeel.h"

//==============================================================================
// Base class for analog-style looks
class AnalogLookAndFeelBase : public juce::LookAndFeel_V4
{
public:
    struct ColorScheme
    {
        juce::Colour background;
        juce::Colour panel;
        juce::Colour knobBody;
        juce::Colour knobPointer;
        juce::Colour knobTrack;
        juce::Colour knobFill;
        juce::Colour text;
        juce::Colour textDim;
        juce::Colour accent;
        juce::Colour shadow;
    };

    // Style slider text boxes with subtle background for better contrast
    juce::Label* createSliderTextBox(juce::Slider& slider) override;

    // Draw slider text box background
    void fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& textEditor) override;

protected:
    ColorScheme colors;

    void drawMetallicKnob(juce::Graphics& g, float x, float y, float width, float height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider);

    void drawVintageKnob(juce::Graphics& g, float x, float y, float width, float height,
                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                         juce::Slider& slider);

    // Common illuminated toggle button rendering for all themes
    void drawIlluminatedToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                     bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown,
                                     const juce::Colour& onGlowTop, const juce::Colour& onGlowBottom,
                                     const juce::Colour& onTextColor,
                                     const juce::Colour& offGradientTop, const juce::Colour& offGradientBottom,
                                     const juce::Colour& offTextColor,
                                     const juce::Colour& bezelColor = juce::Colour(0xFF0A0A0A));
};

//==============================================================================
// Vintage Opto Style (warm vintage cream)
class OptoLookAndFeel : public AnalogLookAndFeelBase
{
public:
    OptoLookAndFeel();
    
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                         juce::Slider& slider) override;
                         
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
};

//==============================================================================
// Vintage FET Style (blackface with amber/orange accent)
class FETLookAndFeel : public AnalogLookAndFeelBase
{
public:
    FETLookAndFeel();

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                         juce::Slider& slider) override;

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                             const juce::Colour& backgroundColour,
                             bool shouldDrawButtonAsHighlighted,
                             bool shouldDrawButtonAsDown) override;

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
};

//==============================================================================
// Studio FET Style (blackface with teal/cyan accent - cleaner, more modern)
class StudioFETLookAndFeel : public AnalogLookAndFeelBase
{
public:
    StudioFETLookAndFeel();

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                         juce::Slider& slider) override;

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                             const juce::Colour& backgroundColour,
                             bool shouldDrawButtonAsHighlighted,
                             bool shouldDrawButtonAsDown) override;

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
};

//==============================================================================
// Classic VCA Style (retro beige)
class VCALookAndFeel : public AnalogLookAndFeelBase
{
public:
    VCALookAndFeel();
    
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                         juce::Slider& slider) override;
                         
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
};

//==============================================================================
// Bus Compressor Style (modern analog)
class BusLookAndFeel : public AnalogLookAndFeelBase
{
public:
    BusLookAndFeel();

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                         juce::Slider& slider) override;

    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                     int buttonX, int buttonY, int buttonW, int buttonH,
                     juce::ComboBox& box) override;

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
};

//==============================================================================
// Studio VCA Style (precision red)
class StudioVCALookAndFeel : public AnalogLookAndFeelBase
{
public:
    StudioVCALookAndFeel();

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                         juce::Slider& slider) override;

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
};

//==============================================================================
// Modern Digital Style (transparent, clean)
class DigitalLookAndFeel : public AnalogLookAndFeelBase
{
public:
    DigitalLookAndFeel();

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                         juce::Slider& slider) override;

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
};

//==============================================================================
// Custom VU Meter Component with analog needle
class AnalogVUMeter : public juce::Component, private juce::Timer
{
public:
    AnalogVUMeter();
    ~AnalogVUMeter() override;
    
    void setLevel(float newLevel);
    void setMode(bool showPeaks) { displayPeaks = showPeaks; }
    void paint(juce::Graphics& g) override;
    
private:
    void timerCallback() override;

    float targetLevel = -60.0f;      // Target level from processor
    float needlePosition = 0.0f;     // Current needle position (0-1)
    float needleVelocity = 0.0f;     // For mechanical overshoot simulation
    float peakLevel = -60.0f;
    float peakNeedlePosition = 0.0f;  // Position of peak indicator on scale
    float peakHoldTime = 0.0f;
    bool displayPeaks = true;

    // GR Meter Ballistics - professional hardware-inspired timing
    // GR meters should be faster than VU meters to show actual compressor behavior
    // Reference: LA-2A meter ~100ms attack, 1176 meter ~50ms attack
    static constexpr float kRefreshRateHz = 60.0f;
    static constexpr float kAttackTimeMs = 50.0f;   // Fast attack to show compression
    static constexpr float kReleaseTimeMs = 150.0f; // Slower release for readability

    // Mechanical needle physics for authentic "bounce"
    // Real VU meters overshoot by ~1-1.5% due to needle inertia
    static constexpr float kOvershootDamping = 0.65f;    // Slightly underdamped for overshoot
    static constexpr float kOvershootStiffness = 200.0f; // Spring constant

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalogVUMeter)
};

//==============================================================================
// GR History Graph Component - shows gain reduction over time
// Forward declare UniversalCompressor to avoid circular include
class UniversalCompressor;

class GRHistoryGraph : public juce::Component
{
public:
    GRHistoryGraph();

    // Update with circular buffer data from processor (thread-safe)
    void updateHistory(const UniversalCompressor& processor);
    void paint(juce::Graphics& g) override;

private:
    std::array<float, 128> grHistory{};
    int historyWritePos = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GRHistoryGraph)
};

//==============================================================================
// VU Meter wrapper with LEVEL label - now with clickable toggle to GR history
class VUMeterWithLabel : public juce::Component
{
public:
    VUMeterWithLabel();

    void setLevel(float newLevel);
    void setGRHistory(const UniversalCompressor& processor);
    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

    // Toggle between VU meter and GR history graph
    bool isShowingHistory() const { return showHistory; }
    void setShowHistory(bool show);

private:
    std::unique_ptr<AnalogVUMeter> vuMeter;
    std::unique_ptr<GRHistoryGraph> grHistoryGraph;
    bool showHistory = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VUMeterWithLabel)
};

//==============================================================================
// Release Time Indicator - shows actual program-dependent release time
class ReleaseTimeIndicator : public juce::Component
{
public:
    ReleaseTimeIndicator();

    void setReleaseTime(float timeMs);  // Set the current actual release time
    void setTargetRelease(float timeMs); // Set user-set target release time
    void paint(juce::Graphics& g) override;

private:
    float currentReleaseMs = 100.0f;
    float targetReleaseMs = 100.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReleaseTimeIndicator)
};

// NOTE: LEDMeter class moved to shared/LEDMeter.h for consistency across all plugins.
// Include "../shared/LEDMeter.h" to use it.

//==============================================================================
// Ratio button group for FET mode - custom painted illuminated push buttons
class RatioButtonGroup : public juce::Component
{
public:
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void ratioChanged(int ratioIndex) = 0;
    };

    RatioButtonGroup();
    ~RatioButtonGroup() override;

    void addListener(Listener* l) { listeners.add(l); }
    void removeListener(Listener* l) { listeners.remove(l); }

    void setSelectedRatio(int index);
    void setAccentColor(juce::Colour color);  // Set the illuminated button color
    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    juce::StringArray ratioLabels {"4:1", "8:1", "12:1", "20:1", "All"};
    juce::ListenerList<Listener> listeners;
    int currentRatio = 0;
    std::vector<juce::Rectangle<int>> buttonBounds;
    juce::Colour accentColorBright = juce::Colour(0xFFFFAA00);  // Default amber
    juce::Colour accentColorDark = juce::Colour(0xFFCC6600);    // Default darker amber

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RatioButtonGroup)
};