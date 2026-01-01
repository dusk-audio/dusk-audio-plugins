#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

//==============================================================================
/**
    Spectrum Display Component

    Renders the FFT spectrum with:
    - Logarithmic frequency scale (20Hz - 20kHz)
    - Gradient-filled spectrum path
    - Peak hold overlay
    - Grid lines and labels
    - Hover tooltip with frequency/dB
*/
class SpectrumDisplay : public juce::Component
{
public:
    static constexpr int NUM_BINS = 2048;

    SpectrumDisplay();
    ~SpectrumDisplay() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    //==========================================================================
    // Update spectrum data
    void updateMagnitudes(const std::array<float, NUM_BINS>& magnitudes);
    void updatePeakHold(const std::array<float, NUM_BINS>& peakHold);

    //==========================================================================
    // Display settings
    void setDisplayRange(float minDB, float maxDB);
    void setShowPeakHold(bool show) { showPeakHold = show; repaint(); }
    void setSpectrumColor(juce::Colour color) { spectrumColor = color; repaint(); }
    void setPeakHoldColor(juce::Colour color) { peakHoldColor = color; repaint(); }

    //==========================================================================
    // Coordinate conversion
    float getFrequencyAtX(float x) const;
    float getXForFrequency(float freq) const;
    float getYForDB(float dB) const;
    float getDBAtY(float y) const;

private:
    void drawGrid(juce::Graphics& g);
    void drawSpectrum(juce::Graphics& g);
    void drawPeakHold(juce::Graphics& g);
    void drawHoverInfo(juce::Graphics& g);

    juce::Path createSpectrumPath() const;

    //==========================================================================
    std::array<float, NUM_BINS> currentMagnitudes{};
    std::array<float, NUM_BINS> currentPeakHold{};

    float minDisplayDB = -60.0f;
    float maxDisplayDB = 6.0f;

    static constexpr float minFrequency = 20.0f;
    static constexpr float maxFrequency = 20000.0f;

    juce::Colour spectrumColor{0xff00aaff};
    juce::Colour peakHoldColor{0xffffaa00};
    juce::Colour gridColor{0xff3a3a3a};
    juce::Colour labelColor{0xff888888};

    bool showPeakHold = true;

    // Hover state
    bool isHovering = false;
    juce::Point<float> hoverPosition;

    // Display area (excluding labels)
    juce::Rectangle<float> displayArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumDisplay)
};
