#pragma once

#include <JuceHeader.h>
#include <array>
#include <functional>
#include "EQBand.h"
#include "FFTAnalyzer.h"

class MultiQ;

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
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    std::function<void(int, bool)> onBandEnabledChanged;
    void setSelectedBand(int bandIndex);
    int getSelectedBand() const { return selectedBand; }

    std::function<void(int)> onBandSelected;

    void setDisplayScaleMode(DisplayScaleMode mode);
    void setAnalyzerVisible(bool visible);

    void setShowPreSpectrum(bool show)
    {
        if (analyzer)
            analyzer->setShowPreSpectrum(show);
    }
    bool isPreSpectrumVisible() const
    {
        return analyzer ? analyzer->isPreSpectrumVisible() : false;
    }

    void setAnalyzerSmoothingMode(FFTAnalyzer::SmoothingMode mode)
    {
        if (analyzer)
            analyzer->setSmoothingMode(mode);
    }

    void setShowMasterGainOverlay(bool show) { showMasterGain = show; repaint(); }
    void setShowPianoOverlay(bool show) { showPianoOverlay = show; backgroundCacheDirty = true; repaint(); }
    bool isPianoOverlayVisible() const { return showPianoOverlay; }
    void setMasterGain(float gainDB) { masterGainDB = gainDB; repaint(); }

    void toggleSpectrumFreeze()
    {
        if (analyzer)
            analyzer->toggleFreeze();
        repaint();
    }

    bool isSpectrumFrozen() const
    {
        return analyzer ? analyzer->isFrozen() : false;
    }

    void clearFrozenSpectrum()
    {
        if (analyzer)
            analyzer->clearFrozen();
        repaint();
    }

private:
    void timerCallback() override;

    MultiQ& processor;
    std::unique_ptr<FFTAnalyzer> analyzer;

    DisplayScaleMode scaleMode = DisplayScaleMode::Linear24dB;
    float minDisplayDB = -24.0f;
    float maxDisplayDB = 24.0f;
    float minFrequency = 20.0f;
    float maxFrequency = 20000.0f;

    bool showMasterGain = false;
    float masterGainDB = 0.0f;
    bool showPianoOverlay = true;

    int selectedBand = 0;
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

    static constexpr float CONTROL_POINT_RADIUS = 12.0f;
    static constexpr float BASE_HIT_RADIUS = 16.0f;


    float getHitRadius() const
    {
        float w = static_cast<float>(getDisplayBounds().getWidth());
        return juce::jmax(12.0f, BASE_HIT_RADIUS * (w / 900.0f));
    }

    float getXForFrequency(float freq) const;
    float getFrequencyAtX(float x) const;
    float getYForDB(float dB) const;
    float getDBAtY(float y) const;
    juce::Rectangle<float> getDisplayBounds() const;

    void drawGrid(juce::Graphics& g);
    void drawPianoOverlay(juce::Graphics& g);
    void drawBandCurve(juce::Graphics& g, int bandIndex);
    void drawCombinedCurve(juce::Graphics& g);
    void drawControlPoints(juce::Graphics& g);
    void drawInactiveBandIndicator(juce::Graphics& g, int bandIndex);
    void drawBandControlPoint(juce::Graphics& g, int bandIndex);
    void drawMasterGainOverlay(juce::Graphics& g);
    void drawMatchOverlays(juce::Graphics& g);

    juce::Point<float> getControlPointPosition(int bandIndex) const;
    juce::Point<float> getStaticControlPointPosition(int bandIndex) const;  // Without dynamic gain offset
    int hitTestControlPoint(juce::Point<float> point) const;

    float getBandFrequency(int bandIndex) const;
    float getBandGain(int bandIndex) const;
    float getBandQ(int bandIndex) const;
    bool isBandEnabled(int bandIndex) const;

    void setBandFrequency(int bandIndex, float freq);
    void setBandGain(int bandIndex, float gain);
    void setBandQ(int bandIndex, float q);
    void showBandContextMenu(int bandIndex, juce::Point<int> position);
    void setBandEnabled(int bandIndex, bool enabled);

    juce::Image backgroundCache;
    bool backgroundCacheDirty = true;
    void renderBackground();

    static constexpr int kNumBands = 8;
    std::array<float, kNumBands> smoothedDynamicGains{};

    bool showHoverReadout = false;
    juce::Point<float> hoverPosition;

    std::array<float, kNumBands> lastBandFreqs{};
    std::array<float, kNumBands> lastBandGains{};
    std::array<float, kNumBands> lastBandQs{};
    std::array<bool, kNumBands> lastBandEnabled{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EQGraphicDisplay)
};
