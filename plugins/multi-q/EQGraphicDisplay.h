#pragma once

#include <JuceHeader.h>
#include <array>
#include <functional>
#include "EQBand.h"
#include "FFTAnalyzer.h"

class MultiQ;

//==============================================================================
/**
    Interactive EQ Graphic Display for Multi-Q

    Features:
    - Logarithmic frequency axis (20 Hz - 20 kHz)
    - Configurable dB axis (±12, ±30, ±60 dB, or warped)
    - Color-coded band curves with shaded fill
    - Combined EQ curve display
    - Draggable control points for each band
    - FFT analyzer overlay
    - Grid lines at standard frequencies and dB intervals
*/
class EQGraphicDisplay : public juce::Component,
                         private juce::Timer
{
public:
    explicit EQGraphicDisplay(MultiQ& processor);
    ~EQGraphicDisplay() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    // Callback when band enabled state should change
    std::function<void(int, bool)> onBandEnabledChanged;

    // Set the selected band (for parameter editing)
    void setSelectedBand(int bandIndex);
    int getSelectedBand() const { return selectedBand; }

    // Callback when a band is selected by clicking
    std::function<void(int)> onBandSelected;

    // Set display scale mode
    void setDisplayScaleMode(DisplayScaleMode mode);

    // Show/hide analyzer
    void setAnalyzerVisible(bool visible);

    // Set analyzer smoothing mode
    void setAnalyzerSmoothingMode(FFTAnalyzer::SmoothingMode mode)
    {
        if (analyzer)
            analyzer->setSmoothingMode(mode);
    }

    // Show/hide master gain overlay
    void setShowMasterGainOverlay(bool show) { showMasterGain = show; repaint(); }

    // Toggle piano keyboard overlay
    void setShowPianoOverlay(bool show) { showPianoOverlay = show; backgroundCacheDirty = true; repaint(); }
    bool isPianoOverlayVisible() const { return showPianoOverlay; }

    // Update master gain value for overlay
    void setMasterGain(float gainDB) { masterGainDB = gainDB; repaint(); }

    // Toggle spectrum freeze (captures current spectrum as reference)
    void toggleSpectrumFreeze()
    {
        if (analyzer)
            analyzer->toggleFreeze();
        repaint();
    }

    // Check if spectrum is frozen
    bool isSpectrumFrozen() const
    {
        return analyzer ? analyzer->isFrozen() : false;
    }

    // Clear frozen spectrum
    void clearFrozenSpectrum()
    {
        if (analyzer)
            analyzer->clearFrozen();
        repaint();
    }

private:
    void timerCallback() override;

    // Reference to processor for getting frequency response
    MultiQ& processor;

    // Analyzer component (child component)
    std::unique_ptr<FFTAnalyzer> analyzer;

    // Display settings
    DisplayScaleMode scaleMode = DisplayScaleMode::Linear24dB;
    float minDisplayDB = -24.0f;
    float maxDisplayDB = 24.0f;
    float minFrequency = 20.0f;
    float maxFrequency = 20000.0f;

    bool showMasterGain = false;
    float masterGainDB = 0.0f;
    bool showPianoOverlay = true;  // Piano keyboard note overlay

    // Interaction state
    int selectedBand = 0;  // Start with band 1 selected
    int hoveredBand = -1;
    bool isDragging = false;
    juce::Point<float> dragStartPoint;
    float dragStartFreq = 0.0f;
    float dragStartGain = 0.0f;
    float dragStartQ = 0.0f;

    enum class DragMode
    {
        None,
        FrequencyAndGain,
        GainOnly,
        QOnly,
        FrequencyOnly
    };
    DragMode currentDragMode = DragMode::None;

    // Control point hit testing - larger nodes for Pro-Q style visibility
    static constexpr float CONTROL_POINT_RADIUS = 12.0f;  // 24px diameter
    static constexpr float BASE_HIT_RADIUS = 16.0f;  // Base hit area at reference width

    // Scale hit radius with display width for consistent interaction at all sizes
    float getHitRadius() const
    {
        float w = static_cast<float>(getDisplayBounds().getWidth());
        return juce::jmax(12.0f, BASE_HIT_RADIUS * (w / 900.0f));
    }

    // Coordinate conversion
    float getXForFrequency(float freq) const;
    float getFrequencyAtX(float x) const;
    float getYForDB(float dB) const;
    float getDBAtY(float y) const;

    // Get display bounds (excluding margins)
    juce::Rectangle<float> getDisplayBounds() const;

    // Drawing helpers
    void drawGrid(juce::Graphics& g);
    void drawPianoOverlay(juce::Graphics& g);
    void drawBandCurve(juce::Graphics& g, int bandIndex);
    void drawCombinedCurve(juce::Graphics& g);
    void drawControlPoints(juce::Graphics& g);
    void drawInactiveBandIndicator(juce::Graphics& g, int bandIndex);
    void drawBandControlPoint(juce::Graphics& g, int bandIndex);
    void drawMasterGainOverlay(juce::Graphics& g);

    // Get control point position for a band (includes dynamic gain offset)
    juce::Point<float> getControlPointPosition(int bandIndex) const;

    // Get static control point position (without dynamic gain, for ghost display)
    juce::Point<float> getStaticControlPointPosition(int bandIndex) const;

    // Hit test for control points
    int hitTestControlPoint(juce::Point<float> point) const;

    // Get band parameters
    float getBandFrequency(int bandIndex) const;
    float getBandGain(int bandIndex) const;
    float getBandQ(int bandIndex) const;
    bool isBandEnabled(int bandIndex) const;

    // Set band parameters (notifies processor)
    void setBandFrequency(int bandIndex, float freq);
    void setBandGain(int bandIndex, float gain);
    void setBandQ(int bandIndex, float q);

    // Show context menu for band
    void showBandContextMenu(int bandIndex, juce::Point<int> position);

    // Enable/disable a band
    void setBandEnabled(int bandIndex, bool enabled);

    // Cached background image (gradients + grid + piano overlay)
    juce::Image backgroundCache;
    bool backgroundCacheDirty = true;
    void renderBackground();

    // Smoothed dynamic gains for visual continuity (UI-side smoothing)
    static constexpr int kNumBands = 8;
    std::array<float, kNumBands> smoothedDynamicGains{};

    // Change detection for smart repaint
    std::array<float, kNumBands> lastBandFreqs{};
    std::array<float, kNumBands> lastBandGains{};
    std::array<float, kNumBands> lastBandQs{};
    std::array<bool, kNumBands> lastBandEnabled{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EQGraphicDisplay)
};
