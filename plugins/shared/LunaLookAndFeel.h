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
 * Professional slider with FabFilter-style knob behavior
 *
 * Features (matching industry standard - FabFilter, Tokyo Dawn Labs):
 * - Shift+drag for fine control (3x finer - matches FabFilter/TDR standard)
 * - Velocity-sensitive dragging: slow movements = precise, fast = coarse
 * - Ctrl/Cmd+click to reset to default value
 * - Smooth, jitter-free operation
 * - Shift+scroll wheel for fine wheel control (3x finer)
 *
 * The velocity curve makes slow, deliberate movements more precise while
 * allowing fast sweeps across the full range - just like FabFilter Pro-Q.
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
        // Disable JUCE velocity mode - we implement our own smoother version
        setVelocityBasedMode(false);
    }

public:
    void mouseDown(const juce::MouseEvent& e) override
    {
        // Ctrl/Cmd+click = reset to default (FabFilter standard)
        if (e.mods.isCommandDown() || e.mods.isCtrlDown())
        {
            // Reset to default value if double-click reset is enabled
            if (isDoubleClickReturnEnabled())
            {
                setValue(getDoubleClickReturnValue(), juce::sendNotificationSync);
                return;
            }
        }

        setVelocityBasedMode(false);

        // Track drag in proportion space (0..1) so behavior is consistent
        // across all parameter ranges and respects skew/log mapping
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

        // Shift = fine mode (industry standard - FabFilter, most pro plugins)
        bool fineMode = e.mods.isShiftDown();

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
            juce::Slider::mouseDrag(e);
            return;
        }

        // Fixed sensitivity: 200px for full range, Shift: 600px (3x finer)
        // Static scaling — no velocity curve — consistent for all parameter ranges
        double sensitivity = fineMode ? 600.0 : 200.0;

        // Proportion-based: works in 0..1 space so behavior is identical
        // regardless of value range (±12dB gain vs 20-20kHz frequency)
        // and respects any skew/log mapping on the parameter
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

        // Shift = fine mode (consistent with drag behavior)
        bool fineMode = e.mods.isShiftDown();

        float wheelDelta = std::abs(wheel.deltaY) > std::abs(wheel.deltaX)
                           ? wheel.deltaY
                           : -wheel.deltaX;

        if (wheel.isReversed)
            wheelDelta = -wheelDelta;

        // Normal: 10% of range per wheel unit | Fine: 3.3% (3x finer)
        double sensitivity = fineMode ? 0.033 : 0.10;
        double proportionDelta = wheelDelta * sensitivity;

        double currentProportion = valueToProportionOfLength(getValue());
        double newProportion = juce::jlimit(0.0, 1.0,
                                            currentProportion + proportionDelta);
        double newValue = proportionOfLengthToValue(newProportion);

        // Handle discrete intervals
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
    // Drag tracking state (proportion-based for consistent behavior)
    double lastDragProportion = 0.0;
    float lastDragY = 0.0f;
    float lastDragX = 0.0f;
};

//==============================================================================
/**
 * Standard slider/knob configuration for Luna Co. Audio plugins
 *
 * IMPORTANT: Use LunaSlider instead of juce::Slider for all knobs.
 * LunaSlider provides professional behavior matching FabFilter/TDR:
 * - Shift+drag for fine control
 * - Velocity-sensitive dragging
 * - Ctrl/Cmd+click to reset
 *
 * The configureKnob() function is DEPRECATED - do not use on LunaSlider,
 * it will break the built-in behavior.
 */
struct LunaSliderStyle
{
    // DEPRECATED: Only kept for backwards compatibility with juce::Slider
    static constexpr double sensitivity = 0.5;
    static constexpr int threshold = 2;

    /**
     * DEPRECATED: Do not use on LunaSlider - it breaks the built-in fine control.
     * Only use this for legacy juce::Slider instances.
     */
    static void configureKnob(juce::Slider& slider)
    {
        slider.setVelocityBasedMode(true);
        slider.setVelocityModeParameters(sensitivity, threshold, 0.0, false);
    }

    /**
     * DEPRECATED: Do not use on LunaSlider.
     */
    static void configureKnob(juce::Slider& slider, double customSensitivity)
    {
        slider.setVelocityBasedMode(true);
        slider.setVelocityModeParameters(customSensitivity, threshold, 0.0, false);
    }

    /**
     * DEPRECATED: Use LunaSlider directly instead.
     */
    static void setupRotaryKnob(juce::Slider& slider)
    {
        slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        slider.setScrollWheelEnabled(true);
        configureKnob(slider);
    }
};

//==============================================================================
/**
 * Shared tooltip strings and helpers for Luna Co. Audio plugins
 *
 * Use these to ensure consistent tooltip text across all plugins.
 * Centralized here so common phrases are only maintained in one place.
 */
struct LunaTooltips
{
    // Common modifier hints (matching FabFilter/industry standard)
    static inline const juce::String fineControlHint = " (Shift+drag for fine control)";
    static inline const juce::String resetHint = " (Ctrl/Cmd+click to reset)";

    // Common control descriptions
    static inline const juce::String bypass = "Bypass all processing (Shortcut: B)";
    static inline const juce::String analyzer = "Show/hide real-time FFT spectrum analyzer (Shortcut: H)";
    static inline const juce::String abComparison = "A/B Comparison: Click to switch between two settings (Shortcut: A)";
    static inline const juce::String hqMode = "Enable 2x oversampling for analog-matched response at high frequencies";

    // EQ-specific (for EQ plugins)
    static inline const juce::String frequency = "Frequency: Center frequency of this band";
    static inline const juce::String gain = "Gain: Boost or cut at this frequency";
    static inline const juce::String qBandwidth = "Q: Bandwidth/resonance - higher values = narrower bandwidth";
    static inline const juce::String filterSlope = "Filter slope: Steeper = sharper cutoff";

    // Dynamics-specific
    static inline const juce::String dynThreshold = "Threshold: Level where dynamic gain reduction starts";
    static inline const juce::String dynAttack = "Attack: How fast gain reduction responds to level increases";
    static inline const juce::String dynRelease = "Release: How fast gain returns after level drops";
    static inline const juce::String dynRange = "Range: Maximum amount of dynamic gain reduction";

    // Helper to add fine control hint to a tooltip
    static juce::String withFineControl(const juce::String& tooltip)
    {
        return tooltip + fineControlHint;
    }

    // Helper to add reset hint to a tooltip
    static juce::String withReset(const juce::String& tooltip)
    {
        return tooltip + resetHint;
    }

    // Helper to add both hints
    static juce::String withAllHints(const juce::String& tooltip)
    {
        return tooltip + fineControlHint + resetHint;
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
