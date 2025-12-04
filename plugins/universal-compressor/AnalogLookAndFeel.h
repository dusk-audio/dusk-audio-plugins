#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

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

protected:
    ColorScheme colors;
    
    void drawMetallicKnob(juce::Graphics& g, float x, float y, float width, float height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider);
                          
    void drawVintageKnob(juce::Graphics& g, float x, float y, float width, float height,
                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                         juce::Slider& slider);
};

//==============================================================================
// LA-2A Opto Style (warm vintage cream)
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
// 1176 FET Style (blackface)
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
// DBX 160 VCA Style (retro beige)
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
// SSL G Bus Style (modern analog)
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
// VU Meter wrapper with LEVEL label
class VUMeterWithLabel : public juce::Component
{
public:
    VUMeterWithLabel();
    
    void setLevel(float newLevel);
    void resized() override;
    void paint(juce::Graphics& g) override;
    
private:
    std::unique_ptr<AnalogVUMeter> vuMeter;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VUMeterWithLabel)
};

//==============================================================================
// LED-style meter for input/output
class LEDMeter : public juce::Component
{
public:
    enum Orientation { Vertical, Horizontal };
    
    LEDMeter(Orientation orient = Vertical);
    
    void setLevel(float newLevel);
    void paint(juce::Graphics& g) override;
    
private:
    Orientation orientation;
    float currentLevel = -60.0f;
    int numLEDs = 12;
    
    juce::Colour getLEDColor(int ledIndex, int totalLEDs);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LEDMeter)
};

//==============================================================================
// Ratio button group for FET mode (like 1176)
class RatioButtonGroup : public juce::Component, public juce::Button::Listener
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
    void buttonClicked(juce::Button* button) override;
    
private:
    std::vector<std::unique_ptr<juce::TextButton>> ratioButtons;
    juce::ListenerList<Listener> listeners;
    int currentRatio = 0;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RatioButtonGroup)
};