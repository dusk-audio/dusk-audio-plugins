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
// Vintage FET Style (blackface)
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
    
    float currentLevel = -60.0f;
    float targetLevel = -60.0f;
    float needlePosition = 0.0f;
    float peakLevel = -60.0f;
    float peakHoldTime = 0.0f;
    bool displayPeaks = true;
    
    // Ballistics
    const float attackTime = 0.3f;  // 300ms VU standard
    const float releaseTime = 0.3f;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalogVUMeter)
};

//==============================================================================
// GR History Graph Component - shows gain reduction over time
class GRHistoryGraph : public juce::Component
{
public:
    GRHistoryGraph();

    // Update with circular buffer data from processor
    void updateHistory(const std::array<float, 128>& history, int writePos);
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
    void setGRHistory(const std::array<float, 128>& history, int writePos);
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
    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    juce::StringArray ratioLabels {"4:1", "8:1", "12:1", "20:1", "All"};
    juce::ListenerList<Listener> listeners;
    int currentRatio = 0;
    std::vector<juce::Rectangle<int>> buttonBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RatioButtonGroup)
};