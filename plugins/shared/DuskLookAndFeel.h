#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
struct LEDMeterStyle
{
    static constexpr int standardWidth = 32;
    static constexpr int meterAreaWidth = 60;
    static constexpr int labelHeight = 16;
    static constexpr int valueHeight = 20;
    static constexpr int labelSpacing = 4;
    static constexpr float labelFontSize = 10.0f;
    static constexpr float valueFontSize = 10.0f;

    static inline juce::Colour getLabelColor() { return juce::Colour(0xffe0e0e0); }
    static inline juce::Colour getValueColor() { return juce::Colour(0xffcccccc); }

    static void drawMeterLabels(juce::Graphics& g,
                                 juce::Rectangle<int> meterBounds,
                                 const juce::String& label,
                                 float levelDb,
                                 float scaleFactor = 1.0f)
    {
        g.setFont(juce::Font(juce::FontOptions(labelFontSize * scaleFactor).withStyle("Bold")));
        g.setColour(getLabelColor());

        int labelWidth = static_cast<int>(50 * scaleFactor);
        int labelX = meterBounds.getCentreX() - labelWidth / 2;
        g.drawText(label, labelX, meterBounds.getY() - static_cast<int>((labelHeight + labelSpacing) * scaleFactor),
                   labelWidth, static_cast<int>(labelHeight * scaleFactor), juce::Justification::centred);

        g.setFont(juce::Font(juce::FontOptions(valueFontSize * scaleFactor).withStyle("Bold")));
        g.setColour(getValueColor());

        juce::String valueText = juce::String(levelDb, 1) + " dB";
        g.drawText(valueText, labelX, meterBounds.getBottom() + static_cast<int>(labelSpacing * scaleFactor),
                   labelWidth, static_cast<int>(valueHeight * scaleFactor), juce::Justification::centred);
    }
};

//==============================================================================
// Slider with Shift+drag fine control and Ctrl/Cmd+click reset.
class DuskSlider : public juce::Slider
{
public:
    DuskSlider()
    {
        initDuskSlider();
    }

    explicit DuskSlider(const juce::String& componentName) : juce::Slider(componentName)
    {
        initDuskSlider();
    }

    DuskSlider(SliderStyle style, TextEntryBoxPosition textBoxPosition)
        : juce::Slider(style, textBoxPosition)
    {
        initDuskSlider();
    }

private:
    void initDuskSlider()
    {
        setVelocityBasedMode(false);
    }

public:
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isCommandDown() || e.mods.isCtrlDown())
        {
            if (isDoubleClickReturnEnabled())
            {
                setValue(getDoubleClickReturnValue(), juce::sendNotificationSync);
                return;
            }
        }

        setVelocityBasedMode(false);
        lastDragProportion = valueToProportionOfLength(getValue());
        lastDragY = e.position.y;
        lastDragX = e.position.x;

        juce::Slider::mouseDown(e);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!isEnabled())
        {
            juce::Slider::mouseDrag(e);
            return;
        }

        bool fineMode = e.mods.isShiftDown();
        double pixelDiff = 0.0;
        auto style = getSliderStyle();

        if (style == RotaryVerticalDrag || style == Rotary ||
            style == LinearVertical || style == LinearBarVertical)
        {
            pixelDiff = lastDragY - e.position.y;
        }
        else if (style == RotaryHorizontalDrag ||
                 style == LinearHorizontal || style == LinearBar)
        {
            pixelDiff = e.position.x - lastDragX;
        }
        else if (style == RotaryHorizontalVerticalDrag)
        {
            pixelDiff = (e.position.x - lastDragX) + (lastDragY - e.position.y);
        }
        else
        {
            juce::Slider::mouseDrag(e);
            return;
        }

        double sensitivity = fineMode ? 600.0 : 200.0;
        double proportionDelta = pixelDiff / sensitivity;
        lastDragProportion = juce::jlimit(0.0, 1.0, lastDragProportion + proportionDelta);
        double newValue = proportionOfLengthToValue(lastDragProportion);

        setValue(newValue, juce::sendNotificationSync);

        lastDragY = e.position.y;
        lastDragX = e.position.x;
    }

    void mouseWheelMove(const juce::MouseEvent& e,
                        const juce::MouseWheelDetails& wheel) override
    {
        if (!isEnabled() || !isScrollWheelEnabled())
        {
            juce::Slider::mouseWheelMove(e, wheel);
            return;
        }

        bool fineMode = e.mods.isShiftDown();

        float wheelDelta = std::abs(wheel.deltaY) > std::abs(wheel.deltaX)
                           ? wheel.deltaY
                           : -wheel.deltaX;

        if (wheel.isReversed)
            wheelDelta = -wheelDelta;

        double sensitivity = fineMode ? 0.033 : 0.10;
        double proportionDelta = wheelDelta * sensitivity;

        double currentProportion = valueToProportionOfLength(getValue());
        double newProportion = juce::jlimit(0.0, 1.0,
                                            currentProportion + proportionDelta);
        double newValue = proportionOfLengthToValue(newProportion);

        double interval = getInterval();
        if (interval > 0)
        {
            double diff = newValue - getValue();
            if (std::abs(diff) > 0.0 && std::abs(diff) < interval)
                newValue = getValue() + interval * (diff < 0 ? -1.0 : 1.0);
        }

        setValue(snapValue(newValue, notDragging), juce::sendNotificationSync);
    }

private:
    double lastDragProportion = 0.0;
    float lastDragY = 0.0f;
    float lastDragX = 0.0f;
};

//==============================================================================
struct DuskTooltips
{
    static inline const juce::String fineControlHint = " (Shift+drag for fine control)";
    static inline const juce::String resetHint = " (Ctrl/Cmd+click to reset)";

    static inline const juce::String bypass = "Bypass all processing (Shortcut: B)";
    static inline const juce::String analyzer = "Show/hide real-time FFT spectrum analyzer (Shortcut: H)";
    static inline const juce::String abComparison = "A/B Comparison: Click to switch between two settings (Shortcut: A)";
    static inline const juce::String hqMode = "Enable 2x oversampling for analog-matched response at high frequencies";

    static inline const juce::String frequency = "Frequency: Center frequency of this band";
    static inline const juce::String gain = "Gain: Boost or cut at this frequency";
    static inline const juce::String qBandwidth = "Q: Bandwidth/resonance - higher values = narrower bandwidth";
    static inline const juce::String filterSlope = "Filter slope: Steeper = sharper cutoff";

    static inline const juce::String dynThreshold = "Threshold: Level where dynamic gain reduction starts";
    static inline const juce::String dynAttack = "Attack: How fast gain reduction responds to level increases";
    static inline const juce::String dynRelease = "Release: How fast gain returns after level drops";
    static inline const juce::String dynRange = "Range: Maximum amount of dynamic gain reduction";

    static juce::String withFineControl(const juce::String& tooltip) { return tooltip + fineControlHint; }
    static juce::String withReset(const juce::String& tooltip) { return tooltip + resetHint; }
    static juce::String withAllHints(const juce::String& tooltip) { return tooltip + fineControlHint + resetHint; }
};

class DuskLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DuskLookAndFeel()
    {
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(0xff1a1a1a));
        setColour(juce::Slider::thumbColourId, juce::Colour(0xff4a9eff));
        setColour(juce::Slider::trackColourId, juce::Colour(0xff2a2a2a));
        setColour(juce::Slider::backgroundColourId, juce::Colour(0xff0f0f0f));
        setColour(juce::Label::textColourId, juce::Colours::white);
    }

    ~DuskLookAndFeel() override = default;
};
