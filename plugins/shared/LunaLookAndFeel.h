/*
  ==============================================================================

    LunaLookAndFeel.h
    Shared look and feel for Luna Co. Audio plugins

  ==============================================================================
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
/**
 * Standard LED meter styling constants for Luna Co. Audio plugins
 * Use these to ensure consistent meter appearance across all plugins
 */
struct LEDMeterStyle
{
    // Standard meter dimensions
    static constexpr int standardWidth = 32;           // Standard meter width in pixels (wider for visibility)
    static constexpr int meterAreaWidth = 60;          // Total area including labels
    static constexpr int labelHeight = 16;             // Height for "INPUT"/"OUTPUT" labels
    static constexpr int valueHeight = 20;             // Height for dB value display below meter
    static constexpr int labelSpacing = 4;             // Space between label and meter

    // Label styling
    static constexpr float labelFontSize = 10.0f;      // Font size for "INPUT"/"OUTPUT"
    static constexpr float valueFontSize = 10.0f;      // Font size for dB values

    // Colors
    static inline juce::Colour getLabelColor() { return juce::Colour(0xffe0e0e0); }
    static inline juce::Colour getValueColor() { return juce::Colour(0xffcccccc); }

    /**
     * Draw meter labels and values in a standard way
     * @param g Graphics context
     * @param meterBounds The bounds of the actual meter component
     * @param label The label text ("INPUT" or "OUTPUT")
     * @param levelDb The current level in dB to display
     * @param scaleFactor Optional scale factor for responsive layouts
     */
    static void drawMeterLabels(juce::Graphics& g,
                                 juce::Rectangle<int> meterBounds,
                                 const juce::String& label,
                                 float levelDb,
                                 float scaleFactor = 1.0f)
    {
        // Draw label above meter
        g.setFont(juce::Font(juce::FontOptions(labelFontSize * scaleFactor).withStyle("Bold")));
        g.setColour(getLabelColor());

        int labelWidth = static_cast<int>(50 * scaleFactor);
        int labelX = meterBounds.getCentreX() - labelWidth / 2;
        g.drawText(label, labelX, meterBounds.getY() - static_cast<int>((labelHeight + labelSpacing) * scaleFactor),
                   labelWidth, static_cast<int>(labelHeight * scaleFactor), juce::Justification::centred);

        // Draw value below meter
        g.setFont(juce::Font(juce::FontOptions(valueFontSize * scaleFactor).withStyle("Bold")));
        g.setColour(getValueColor());

        juce::String valueText = juce::String(levelDb, 1) + " dB";
        g.drawText(valueText, labelX, meterBounds.getBottom() + static_cast<int>(labelSpacing * scaleFactor),
                   labelWidth, static_cast<int>(valueHeight * scaleFactor), juce::Justification::centred);
    }
};

//==============================================================================
/**
 * Custom slider with proper Cmd/Ctrl+drag fine control
 *
 * Features:
 * - Cmd/Ctrl+drag reduces sensitivity by 10x for fine adjustments
 * - Smooth transition when modifier is pressed/released mid-drag (no jumps)
 * - Cmd/Ctrl+scroll wheel for fine wheel control
 * - Disables JUCE velocity mode to prevent cursor hiding
 *
 * Uses incremental drag tracking instead of JUCE's default which calculates
 * from mouseDown origin. This ensures seamless modifier key transitions.
 */
class LunaSlider : public juce::Slider
{
public:
    LunaSlider()
    {
        initLunaSlider();
    }

    explicit LunaSlider(const juce::String& componentName) : juce::Slider(componentName)
    {
        initLunaSlider();
    }

    // Constructor matching juce::Slider(SliderStyle, TextEntryBoxPosition)
    LunaSlider(SliderStyle style, TextEntryBoxPosition textBoxPosition)
        : juce::Slider(style, textBoxPosition)
    {
        initLunaSlider();
    }

private:
    void initLunaSlider()
    {
        // MUST disable velocity mode - it hides cursor and conflicts with our tracking
        setVelocityBasedMode(false);
    }

public:
    void mouseDown(const juce::MouseEvent& e) override
    {
        // Ensure velocity mode stays off
        setVelocityBasedMode(false);

        // Initialize incremental drag tracking
        lastDragValue = getValue();
        lastDragY = e.position.y;
        lastDragX = e.position.x;
        wasInFineMode = e.mods.isCommandDown() || e.mods.isCtrlDown();

        juce::Slider::mouseDown(e);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!isEnabled())
        {
            juce::Slider::mouseDrag(e);
            return;
        }

        bool fineMode = e.mods.isCommandDown() || e.mods.isCtrlDown();
        int sensitivity = fineMode ? fineSensitivity : normalSensitivity;

        // Calculate pixel movement since last event
        double pixelDiff = 0.0;
        auto style = getSliderStyle();

        if (style == RotaryVerticalDrag || style == Rotary ||
            style == LinearVertical || style == LinearBarVertical)
        {
            pixelDiff = lastDragY - e.position.y;  // Up = increase
        }
        else if (style == RotaryHorizontalDrag ||
                 style == LinearHorizontal || style == LinearBar)
        {
            pixelDiff = e.position.x - lastDragX;  // Right = increase
        }
        else if (style == RotaryHorizontalVerticalDrag)
        {
            pixelDiff = (e.position.x - lastDragX) + (lastDragY - e.position.y);
        }
        else
        {
            // Fall back to base class for other styles
            juce::Slider::mouseDrag(e);
            return;
        }

        // Calculate and apply value change
        double range = getMaximum() - getMinimum();
        double valueDelta = (pixelDiff / sensitivity) * range;
        double newValue = juce::jlimit(getMinimum(), getMaximum(),
                                        lastDragValue + valueDelta);

        setValue(newValue, juce::sendNotificationSync);

        // Update reference for next drag event (key to seamless modifier transitions)
        lastDragValue = getValue();
        lastDragY = e.position.y;
        lastDragX = e.position.x;
        wasInFineMode = fineMode;
    }

    void mouseWheelMove(const juce::MouseEvent& e,
                        const juce::MouseWheelDetails& wheel) override
    {
        if (!isEnabled() || !isScrollWheelEnabled())
        {
            juce::Slider::mouseWheelMove(e, wheel);
            return;
        }

        bool fineMode = e.mods.isCommandDown() || e.mods.isCtrlDown();

        // Determine wheel direction (prefer Y, fall back to X for horizontal scroll)
        float wheelDelta = std::abs(wheel.deltaY) > std::abs(wheel.deltaX)
                           ? wheel.deltaY
                           : -wheel.deltaX;

        if (wheel.isReversed)
            wheelDelta = -wheelDelta;

        // Normal: 15% of range per wheel unit | Fine: 1.5% (10x finer)
        double sensitivity = fineMode ? wheelFineSensitivity : wheelNormalSensitivity;
        double proportionDelta = wheelDelta * sensitivity;

        // Apply to current position
        double currentProportion = valueToProportionOfLength(getValue());
        double newProportion = juce::jlimit(0.0, 1.0,
                                            currentProportion + proportionDelta);
        double newValue = proportionOfLengthToValue(newProportion);

        // Ensure minimum interval step for discrete sliders
        double interval = getInterval();
        if (interval > 0)
        {
            double diff = newValue - getValue();
            if (std::abs(diff) > 0.0 && std::abs(diff) < interval)
                newValue = getValue() + interval * (diff < 0 ? -1.0 : 1.0);
        }

        setValue(snapValue(newValue, notDragging), juce::sendNotificationSync);
    }

    // Sensitivity constants
    static constexpr int normalSensitivity = 300;    // Pixels for full range drag
    static constexpr int fineSensitivity = 3000;     // Fine: 10x more pixels needed

    static constexpr double wheelNormalSensitivity = 0.15;   // Proportion per wheel unit
    static constexpr double wheelFineSensitivity = 0.015;    // Fine: 10x smaller steps

private:
    // Incremental drag tracking state
    double lastDragValue = 0.0;
    float lastDragY = 0.0f;
    float lastDragX = 0.0f;
    bool wasInFineMode = false;
};

//==============================================================================
/**
 * Standard slider/knob configuration for Luna Co. Audio plugins
 * Use these to ensure consistent knob behavior across all plugins
 *
 * For true Cmd/Ctrl+drag fine control, use LunaSlider instead of juce::Slider.
 * The configureKnob() function provides velocity-based control without fine mode.
 */
struct LunaSliderStyle
{
    // Velocity mode parameters for professional knob feel
    static constexpr double sensitivity = 0.5;    // Lower = slower, more controlled movement
    static constexpr int threshold = 2;           // Ignore tiny mouse movements (reduces jitter)

    /**
     * Configure a rotary slider with professional Luna knob behavior
     * Call this after setting slider style to RotaryVerticalDrag
     *
     * Features:
     * - 50% slower base movement for precise control
     * - Jitter filtering (ignores tiny mouse movements)
     *
     * Note: For Cmd/Ctrl fine control, use LunaSlider instead of juce::Slider
     *
     * @param slider The slider to configure
     */
    static void configureKnob(juce::Slider& slider)
    {
        slider.setVelocityBasedMode(true);
        slider.setVelocityModeParameters(sensitivity, threshold, 0.0, false);
    }

    /**
     * Configure a rotary slider with custom sensitivity
     * @param slider The slider to configure
     * @param customSensitivity Sensitivity multiplier (0.3 = slower, 1.0 = default JUCE)
     */
    static void configureKnob(juce::Slider& slider, double customSensitivity)
    {
        slider.setVelocityBasedMode(true);
        slider.setVelocityModeParameters(customSensitivity, threshold, 0.0, false);
    }

    /**
     * Full setup for a rotary knob with Luna defaults
     * Sets style, enables scroll wheel, and configures velocity mode
     * @param slider The slider to setup
     */
    static void setupRotaryKnob(juce::Slider& slider)
    {
        slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        slider.setScrollWheelEnabled(true);
        configureKnob(slider);
    }
};

class LunaLookAndFeel : public juce::LookAndFeel_V4
{
public:
    LunaLookAndFeel()
    {
        // Dark theme colors
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(0xff1a1a1a));
        setColour(juce::Slider::thumbColourId, juce::Colour(0xff4a9eff));
        setColour(juce::Slider::trackColourId, juce::Colour(0xff2a2a2a));
        setColour(juce::Slider::backgroundColourId, juce::Colour(0xff0f0f0f));
        setColour(juce::Label::textColourId, juce::Colours::white);
    }

    ~LunaLookAndFeel() override = default;
};
